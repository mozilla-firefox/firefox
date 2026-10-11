/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Swizzle.h"

#include "Logging.h"
#include "Orientation.h"
#include "SwizzleHelpers.h"
#include "Tools.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/UniquePtr.h"
#include "src/base/SkVx.h"

#ifdef USE_SSE2
#  include "mozilla/SSE.h"
#endif

#ifdef USE_NEON
#  include "mozilla/arm.h"
#endif

#include <bit>
#include <new>

namespace mozilla {
namespace gfx {

#ifdef USE_SSE2
/**
 * SSE2 optimizations
 */

template <bool aSwapRB, bool aOpaqueAlpha>
void Premultiply_SSE2(const uint8_t*, int32_t, uint8_t*, int32_t, IntSize);

#  define PREMULTIPLY_SSE2(aSrcFormat, aDstFormat)                     \
    FORMAT_CASE(aSrcFormat, aDstFormat,                                \
                Premultiply_SSE2<ShouldSwapRB(aSrcFormat, aDstFormat), \
                                 ShouldForceOpaque(aSrcFormat, aDstFormat)>)

template <bool aSwapRB, bool aOpaqueAlpha>
void PremultiplyRow_SSE2(const uint8_t*, uint8_t*, int32_t);

#  define PREMULTIPLY_ROW_SSE2(aSrcFormat, aDstFormat)            \
    FORMAT_CASE_ROW(                                              \
        aSrcFormat, aDstFormat,                                   \
        PremultiplyRow_SSE2<ShouldSwapRB(aSrcFormat, aDstFormat), \
                            ShouldForceOpaque(aSrcFormat, aDstFormat)>)

template <bool aSwapRB>
void Unpremultiply_SSE2(const uint8_t*, int32_t, uint8_t*, int32_t, IntSize);

#  define UNPREMULTIPLY_SSE2(aSrcFormat, aDstFormat) \
    FORMAT_CASE(aSrcFormat, aDstFormat,              \
                Unpremultiply_SSE2<ShouldSwapRB(aSrcFormat, aDstFormat)>)

template <bool aSwapRB>
void UnpremultiplyRow_SSE2(const uint8_t*, uint8_t*, int32_t);

#  define UNPREMULTIPLY_ROW_SSE2(aSrcFormat, aDstFormat) \
    FORMAT_CASE_ROW(                                     \
        aSrcFormat, aDstFormat,                          \
        UnpremultiplyRow_SSE2<ShouldSwapRB(aSrcFormat, aDstFormat)>)

template <bool aSwapRB, bool aOpaqueAlpha>
void Swizzle_SSE2(const uint8_t*, int32_t, uint8_t*, int32_t, IntSize);

#  define SWIZZLE_SSE2(aSrcFormat, aDstFormat)                     \
    FORMAT_CASE(aSrcFormat, aDstFormat,                            \
                Swizzle_SSE2<ShouldSwapRB(aSrcFormat, aDstFormat), \
                             ShouldForceOpaque(aSrcFormat, aDstFormat)>)

template <bool aSwapRB, bool aOpaqueAlpha>
void SwizzleRow_SSE2(const uint8_t*, uint8_t*, int32_t);

#  define SWIZZLE_ROW_SSE2(aSrcFormat, aDstFormat)            \
    FORMAT_CASE_ROW(                                          \
        aSrcFormat, aDstFormat,                               \
        SwizzleRow_SSE2<ShouldSwapRB(aSrcFormat, aDstFormat), \
                        ShouldForceOpaque(aSrcFormat, aDstFormat)>)

template <bool aSwapRB, bool aInverted>
void SwizzleCmykRow_SSE2(const uint8_t*, uint8_t*, int32_t);

#  define SWIZZLE_CMYK_ROW_SSE2(aSrcFormat, aDstFormat)                       \
    FORMAT_CASE_ROW(aSrcFormat, aDstFormat,                                   \
                    SwizzleCmykRow_SSE2<ShouldSwapRB(aSrcFormat, aDstFormat), \
                                        ShouldInvert(aSrcFormat)>)

template <bool aSwapRB, bool aOpaqueAlpha>
void Swizzle_SSSE3(const uint8_t*, int32_t, uint8_t*, int32_t, IntSize);

#  define SWIZZLE_SSSE3(aSrcFormat, aDstFormat)                     \
    FORMAT_CASE(aSrcFormat, aDstFormat,                             \
                Swizzle_SSSE3<ShouldSwapRB(aSrcFormat, aDstFormat), \
                              ShouldForceOpaque(aSrcFormat, aDstFormat)>)

template <bool aSwapRB, bool aOpaqueAlpha>
void SwizzleRow_SSSE3(const uint8_t*, uint8_t*, int32_t);

#  define SWIZZLE_ROW_SSSE3(aSrcFormat, aDstFormat)            \
    FORMAT_CASE_ROW(                                           \
        aSrcFormat, aDstFormat,                                \
        SwizzleRow_SSSE3<ShouldSwapRB(aSrcFormat, aDstFormat), \
                         ShouldForceOpaque(aSrcFormat, aDstFormat)>)

template <bool aSwapRB>
void UnpackRowRGB24_SSSE3(const uint8_t*, uint8_t*, int32_t);

#  define UNPACK_ROW_RGB_SSSE3(aDstFormat) \
    FORMAT_CASE_ROW(                       \
        SurfaceFormat::R8G8B8, aDstFormat, \
        UnpackRowRGB24_SSSE3<ShouldSwapRB(SurfaceFormat::R8G8B8, aDstFormat)>)

template <bool aSwapRB, bool aOpaqueAlpha>
void Premultiply_AVX2(const uint8_t*, int32_t, uint8_t*, int32_t, IntSize);

#  define PREMULTIPLY_AVX2(aSrcFormat, aDstFormat)                     \
    FORMAT_CASE(aSrcFormat, aDstFormat,                                \
                Premultiply_AVX2<ShouldSwapRB(aSrcFormat, aDstFormat), \
                                 ShouldForceOpaque(aSrcFormat, aDstFormat)>)

template <bool aSwapRB, bool aOpaqueAlpha>
void PremultiplyRow_AVX2(const uint8_t*, uint8_t*, int32_t);

#  define PREMULTIPLY_ROW_AVX2(aSrcFormat, aDstFormat)            \
    FORMAT_CASE_ROW(                                              \
        aSrcFormat, aDstFormat,                                   \
        PremultiplyRow_AVX2<ShouldSwapRB(aSrcFormat, aDstFormat), \
                            ShouldForceOpaque(aSrcFormat, aDstFormat)>)

template <bool aSwapRB>
void Unpremultiply_AVX2(const uint8_t*, int32_t, uint8_t*, int32_t, IntSize);

#  define UNPREMULTIPLY_AVX2(aSrcFormat, aDstFormat) \
    FORMAT_CASE(aSrcFormat, aDstFormat,              \
                Unpremultiply_AVX2<ShouldSwapRB(aSrcFormat, aDstFormat)>)

template <bool aSwapRB>
void UnpremultiplyRow_AVX2(const uint8_t*, uint8_t*, int32_t);

#  define UNPREMULTIPLY_ROW_AVX2(aSrcFormat, aDstFormat) \
    FORMAT_CASE_ROW(                                     \
        aSrcFormat, aDstFormat,                          \
        UnpremultiplyRow_AVX2<ShouldSwapRB(aSrcFormat, aDstFormat)>)

template <bool aSwapRB, bool aOpaqueAlpha>
void Swizzle_AVX2(const uint8_t*, int32_t, uint8_t*, int32_t, IntSize);

#  define SWIZZLE_AVX2(aSrcFormat, aDstFormat)                     \
    FORMAT_CASE(aSrcFormat, aDstFormat,                            \
                Swizzle_AVX2<ShouldSwapRB(aSrcFormat, aDstFormat), \
                             ShouldForceOpaque(aSrcFormat, aDstFormat)>)

template <bool aSwapRB, bool aOpaqueAlpha>
void SwizzleRow_AVX2(const uint8_t*, uint8_t*, int32_t);

#  define SWIZZLE_ROW_AVX2(aSrcFormat, aDstFormat)            \
    FORMAT_CASE_ROW(                                          \
        aSrcFormat, aDstFormat,                               \
        SwizzleRow_AVX2<ShouldSwapRB(aSrcFormat, aDstFormat), \
                        ShouldForceOpaque(aSrcFormat, aDstFormat)>)

template <bool aSwapRB>
void UnpackRowRGB24_AVX2(const uint8_t*, uint8_t*, int32_t);

#  define UNPACK_ROW_RGB_AVX2(aDstFormat)  \
    FORMAT_CASE_ROW(                       \
        SurfaceFormat::R8G8B8, aDstFormat, \
        UnpackRowRGB24_AVX2<ShouldSwapRB(SurfaceFormat::R8G8B8, aDstFormat)>)

template <bool aSwapRB, bool aInverted>
void SwizzleCmykRow_AVX2(const uint8_t*, uint8_t*, int32_t);

#  define SWIZZLE_CMYK_ROW_AVX2(aSrcFormat, aDstFormat)                       \
    FORMAT_CASE_ROW(aSrcFormat, aDstFormat,                                   \
                    SwizzleCmykRow_AVX2<ShouldSwapRB(aSrcFormat, aDstFormat), \
                                        ShouldInvert(aSrcFormat)>)

#endif

#ifdef USE_NEON
/**
 * ARM NEON optimizations
 */

template <bool aSwapRB, bool aOpaqueAlpha>
void Premultiply_NEON(const uint8_t*, int32_t, uint8_t*, int32_t, IntSize);

#  define PREMULTIPLY_NEON(aSrcFormat, aDstFormat)                     \
    FORMAT_CASE(aSrcFormat, aDstFormat,                                \
                Premultiply_NEON<ShouldSwapRB(aSrcFormat, aDstFormat), \
                                 ShouldForceOpaque(aSrcFormat, aDstFormat)>)

template <bool aSwapRB, bool aOpaqueAlpha>
void PremultiplyRow_NEON(const uint8_t*, uint8_t*, int32_t);

#  define PREMULTIPLY_ROW_NEON(aSrcFormat, aDstFormat)            \
    FORMAT_CASE_ROW(                                              \
        aSrcFormat, aDstFormat,                                   \
        PremultiplyRow_NEON<ShouldSwapRB(aSrcFormat, aDstFormat), \
                            ShouldForceOpaque(aSrcFormat, aDstFormat)>)

template <bool aSwapRB>
void Unpremultiply_NEON(const uint8_t*, int32_t, uint8_t*, int32_t, IntSize);

#  define UNPREMULTIPLY_NEON(aSrcFormat, aDstFormat) \
    FORMAT_CASE(aSrcFormat, aDstFormat,              \
                Unpremultiply_NEON<ShouldSwapRB(aSrcFormat, aDstFormat)>)

template <bool aSwapRB>
void UnpremultiplyRow_NEON(const uint8_t*, uint8_t*, int32_t);

#  define UNPREMULTIPLY_ROW_NEON(aSrcFormat, aDstFormat) \
    FORMAT_CASE_ROW(                                     \
        aSrcFormat, aDstFormat,                          \
        UnpremultiplyRow_NEON<ShouldSwapRB(aSrcFormat, aDstFormat)>)

template <bool aSwapRB, bool aOpaqueAlpha>
void Swizzle_NEON(const uint8_t*, int32_t, uint8_t*, int32_t, IntSize);

#  define SWIZZLE_NEON(aSrcFormat, aDstFormat)                     \
    FORMAT_CASE(aSrcFormat, aDstFormat,                            \
                Swizzle_NEON<ShouldSwapRB(aSrcFormat, aDstFormat), \
                             ShouldForceOpaque(aSrcFormat, aDstFormat)>)

template <bool aSwapRB, bool aOpaqueAlpha>
void SwizzleRow_NEON(const uint8_t*, uint8_t*, int32_t);

#  define SWIZZLE_ROW_NEON(aSrcFormat, aDstFormat)            \
    FORMAT_CASE_ROW(                                          \
        aSrcFormat, aDstFormat,                               \
        SwizzleRow_NEON<ShouldSwapRB(aSrcFormat, aDstFormat), \
                        ShouldForceOpaque(aSrcFormat, aDstFormat)>)

template <bool aSwapRB>
void UnpackRowRGB24_NEON(const uint8_t*, uint8_t*, int32_t);

#  define UNPACK_ROW_RGB_NEON(aDstFormat)  \
    FORMAT_CASE_ROW(                       \
        SurfaceFormat::R8G8B8, aDstFormat, \
        UnpackRowRGB24_NEON<ShouldSwapRB(SurfaceFormat::R8G8B8, aDstFormat)>)

template <bool aSwapRB, bool aInverted>
void SwizzleCmykRow_NEON(const uint8_t*, uint8_t*, int32_t);

#  define SWIZZLE_CMYK_ROW_NEON(aSrcFormat, aDstFormat)                       \
    FORMAT_CASE_ROW(aSrcFormat, aDstFormat,                                   \
                    SwizzleCmykRow_NEON<ShouldSwapRB(aSrcFormat, aDstFormat), \
                                        ShouldInvert(aSrcFormat)>)
#endif

/**
 * Premultiplying
 */

// Fallback premultiply implementation that uses splayed pixel math to reduce
// the multiplications used. That is, the R and B components are isolated from
// the G and A components, which then can be multiplied as if they were two
// 2-component vectors. Otherwise, an approximation if divide-by-255 is used
// which is faster than an actual division. These optimizations are also used
// for the SSE2 and NEON implementations.
template <bool aSwapRB, bool aOpaqueAlpha, uint32_t aSrcRGBShift,
          uint32_t aSrcAShift, uint32_t aDstRGBShift, uint32_t aDstAShift>
static void PremultiplyChunkFallback(const uint8_t*& aSrc, uint8_t*& aDst,
                                     int32_t aLength) {
  const uint8_t* end = aSrc + 4 * aLength;
  do {
    // Load and process 1 entire pixel at a time.
    uint32_t color = *reinterpret_cast<const uint32_t*>(aSrc);

    uint32_t a = aSrcAShift ? color >> aSrcAShift : color & 0xFF;

    // Isolate the R and B components.
    uint32_t rb = (color >> aSrcRGBShift) & 0x00FF00FF;
    // Swap the order of R and B if necessary.
    if (aSwapRB) {
      rb = (rb >> 16) | (rb << 16);
    }
    // Approximate the multiply by alpha and divide by 255 which is
    // essentially:
    // c = c*a + 255; c = (c + (c >> 8)) >> 8;
    // However, we omit the final >> 8 to fold it with the final shift into
    // place depending on desired output format.
    rb = rb * a + 0x00FF00FF;
    rb = (rb + ((rb >> 8) & 0x00FF00FF)) & 0xFF00FF00;

    // Use same approximation as above, but G is shifted 8 bits left.
    // Alpha is left out and handled separately.
    uint32_t g = color & (0xFF00 << aSrcRGBShift);
    g = g * a + (0xFF00 << aSrcRGBShift);
    g = (g + (g >> 8)) & (0xFF0000 << aSrcRGBShift);

    // The above math leaves RGB shifted left by 8 bits.
    // Shift them right if required for the output format.
    // then combine them back together to produce output pixel.
    // Add the alpha back on if the output format is not opaque.
    *reinterpret_cast<uint32_t*>(aDst) =
        (rb >> (8 - aDstRGBShift)) | (g >> (8 + aSrcRGBShift - aDstRGBShift)) |
        (aOpaqueAlpha ? 0xFF << aDstAShift : a << aDstAShift);

    aSrc += 4;
    aDst += 4;
  } while (aSrc < end);
}

template <bool aSwapRB, bool aOpaqueAlpha, uint32_t aSrcRGBShift,
          uint32_t aSrcAShift, uint32_t aDstRGBShift, uint32_t aDstAShift>
static void PremultiplyRowFallback(const uint8_t* aSrc, uint8_t* aDst,
                                   int32_t aLength) {
  PremultiplyChunkFallback<aSwapRB, aOpaqueAlpha, aSrcRGBShift, aSrcAShift,
                           aDstRGBShift, aDstAShift>(aSrc, aDst, aLength);
}

template <bool aSwapRB, bool aOpaqueAlpha, uint32_t aSrcRGBShift,
          uint32_t aSrcAShift, uint32_t aDstRGBShift, uint32_t aDstAShift>
static void PremultiplyFallback(const uint8_t* aSrc, int32_t aSrcGap,
                                uint8_t* aDst, int32_t aDstGap, IntSize aSize) {
  for (int32_t height = aSize.height; height > 0; height--) {
    PremultiplyChunkFallback<aSwapRB, aOpaqueAlpha, aSrcRGBShift, aSrcAShift,
                             aDstRGBShift, aDstAShift>(aSrc, aDst, aSize.width);
    aSrc += aSrcGap;
    aDst += aDstGap;
  }
}

#define PREMULTIPLY_FALLBACK_CASE(aSrcFormat, aDstFormat)                     \
  FORMAT_CASE(                                                                \
      aSrcFormat, aDstFormat,                                                 \
      PremultiplyFallback<ShouldSwapRB(aSrcFormat, aDstFormat),               \
                          ShouldForceOpaque(aSrcFormat, aDstFormat),          \
                          RGBBitShift(aSrcFormat), AlphaBitShift(aSrcFormat), \
                          RGBBitShift(aDstFormat), AlphaBitShift(aDstFormat)>)

#define PREMULTIPLY_FALLBACK(aSrcFormat)                         \
  PREMULTIPLY_FALLBACK_CASE(aSrcFormat, SurfaceFormat::B8G8R8A8) \
  PREMULTIPLY_FALLBACK_CASE(aSrcFormat, SurfaceFormat::B8G8R8X8) \
  PREMULTIPLY_FALLBACK_CASE(aSrcFormat, SurfaceFormat::R8G8B8A8) \
  PREMULTIPLY_FALLBACK_CASE(aSrcFormat, SurfaceFormat::R8G8B8X8) \
  PREMULTIPLY_FALLBACK_CASE(aSrcFormat, SurfaceFormat::A8R8G8B8) \
  PREMULTIPLY_FALLBACK_CASE(aSrcFormat, SurfaceFormat::X8R8G8B8)

#define PREMULTIPLY_ROW_FALLBACK_CASE(aSrcFormat, aDstFormat)             \
  FORMAT_CASE_ROW(aSrcFormat, aDstFormat,                                 \
                  PremultiplyRowFallback<                                 \
                      ShouldSwapRB(aSrcFormat, aDstFormat),               \
                      ShouldForceOpaque(aSrcFormat, aDstFormat),          \
                      RGBBitShift(aSrcFormat), AlphaBitShift(aSrcFormat), \
                      RGBBitShift(aDstFormat), AlphaBitShift(aDstFormat)>)

#define PREMULTIPLY_ROW_FALLBACK(aSrcFormat)                         \
  PREMULTIPLY_ROW_FALLBACK_CASE(aSrcFormat, SurfaceFormat::B8G8R8A8) \
  PREMULTIPLY_ROW_FALLBACK_CASE(aSrcFormat, SurfaceFormat::B8G8R8X8) \
  PREMULTIPLY_ROW_FALLBACK_CASE(aSrcFormat, SurfaceFormat::R8G8B8A8) \
  PREMULTIPLY_ROW_FALLBACK_CASE(aSrcFormat, SurfaceFormat::R8G8B8X8) \
  PREMULTIPLY_ROW_FALLBACK_CASE(aSrcFormat, SurfaceFormat::A8R8G8B8) \
  PREMULTIPLY_ROW_FALLBACK_CASE(aSrcFormat, SurfaceFormat::X8R8G8B8)

// If rows are tightly packed, and the size of the total area will fit within
// the precision range of a single row, then process all the data as if it was
// a single row.
static inline IntSize CollapseSize(const IntSize& aSize, int32_t aSrcStrideGap,
                                   int32_t aDstStrideGap) {
  if (!aSrcStrideGap && !aDstStrideGap) {
    CheckedInt32 area = CheckedInt32(aSize.width) * CheckedInt32(aSize.height);
    if (area.isValid()) {
      return IntSize(area.value(), 1);
    }
  }
  return aSize;
}

static inline int32_t GetStrideGap(int32_t aWidth, SurfaceFormat aFormat,
                                   int32_t aStride) {
  CheckedInt32 used = CheckedInt32(aWidth) * BytesPerPixel(aFormat);
  if (!used.isValid() || used.value() < 0) {
    return -1;
  }
  return aStride - used.value();
}

bool PremultiplyData(const uint8_t* aSrc, int32_t aSrcStride,
                     SurfaceFormat aSrcFormat, uint8_t* aDst,
                     int32_t aDstStride, SurfaceFormat aDstFormat,
                     const IntSize& aSize,
                     SwizzleArch aArch /* = SwizzleArch::eAny */) {
  if (aSize.IsEmpty()) {
    return true;
  }
  // Find gap from end of row to the start of the next row.
  int32_t srcGap = GetStrideGap(aSize.width, aSrcFormat, aSrcStride);
  int32_t dstGap = GetStrideGap(aSize.width, aDstFormat, aDstStride);
  MOZ_ASSERT(srcGap >= 0 && dstGap >= 0);
  if (srcGap < 0 || dstGap < 0) {
    return false;
  }
  IntSize size = CollapseSize(aSize, srcGap, dstGap);

#define FORMAT_CASE_CALL(...) __VA_ARGS__(aSrc, srcGap, aDst, dstGap, size)

#ifdef USE_SSE2
  if (aArch & SwizzleArch::eAVX2 && mozilla::supports_avx2())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      PREMULTIPLY_AVX2(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8A8)
      PREMULTIPLY_AVX2(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8X8)
      PREMULTIPLY_AVX2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      PREMULTIPLY_AVX2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8X8)
      PREMULTIPLY_AVX2(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8A8)
      PREMULTIPLY_AVX2(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8X8)
      PREMULTIPLY_AVX2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      PREMULTIPLY_AVX2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8X8)
      default:
        break;
    }

  if (aArch & SwizzleArch::eSSE2 && mozilla::supports_sse2())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      PREMULTIPLY_SSE2(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8A8)
      PREMULTIPLY_SSE2(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8X8)
      PREMULTIPLY_SSE2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      PREMULTIPLY_SSE2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8X8)
      PREMULTIPLY_SSE2(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8A8)
      PREMULTIPLY_SSE2(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8X8)
      PREMULTIPLY_SSE2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      PREMULTIPLY_SSE2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8X8)
      default:
        break;
    }
#endif

#ifdef USE_NEON
  if (aArch & SwizzleArch::eNEON && mozilla::supports_neon())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      PREMULTIPLY_NEON(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8A8)
      PREMULTIPLY_NEON(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8X8)
      PREMULTIPLY_NEON(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      PREMULTIPLY_NEON(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8X8)
      PREMULTIPLY_NEON(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8A8)
      PREMULTIPLY_NEON(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8X8)
      PREMULTIPLY_NEON(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      PREMULTIPLY_NEON(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8X8)
      default:
        break;
    }
#endif

  if (!(aArch & SwizzleArch::eFallback)) {
    return false;
  }

  switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
    PREMULTIPLY_FALLBACK(SurfaceFormat::B8G8R8A8)
    PREMULTIPLY_FALLBACK(SurfaceFormat::R8G8B8A8)
    PREMULTIPLY_FALLBACK(SurfaceFormat::A8R8G8B8)
    default:
      break;
  }

#undef FORMAT_CASE_CALL

  MOZ_ASSERT_UNREACHABLE("Unsupported premultiply formats");
  return false;
}

SwizzleRowFn PremultiplyRow(SurfaceFormat aSrcFormat, SurfaceFormat aDstFormat,
                            SwizzleArch aArch /* = SwizzleArch::eAny */) {
#ifdef USE_SSE2
  if (aArch & SwizzleArch::eAVX2 && mozilla::supports_avx2())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      PREMULTIPLY_ROW_AVX2(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8A8)
      PREMULTIPLY_ROW_AVX2(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8X8)
      PREMULTIPLY_ROW_AVX2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      PREMULTIPLY_ROW_AVX2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8X8)
      PREMULTIPLY_ROW_AVX2(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8A8)
      PREMULTIPLY_ROW_AVX2(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8X8)
      PREMULTIPLY_ROW_AVX2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      PREMULTIPLY_ROW_AVX2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8X8)
      default:
        break;
    }

  if (aArch & SwizzleArch::eSSE2 && mozilla::supports_sse2())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      PREMULTIPLY_ROW_SSE2(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8A8)
      PREMULTIPLY_ROW_SSE2(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8X8)
      PREMULTIPLY_ROW_SSE2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      PREMULTIPLY_ROW_SSE2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8X8)
      PREMULTIPLY_ROW_SSE2(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8A8)
      PREMULTIPLY_ROW_SSE2(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8X8)
      PREMULTIPLY_ROW_SSE2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      PREMULTIPLY_ROW_SSE2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8X8)
      default:
        break;
    }
#endif

#ifdef USE_NEON
  if (aArch & SwizzleArch::eNEON && mozilla::supports_neon())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      PREMULTIPLY_ROW_NEON(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8A8)
      PREMULTIPLY_ROW_NEON(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8X8)
      PREMULTIPLY_ROW_NEON(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      PREMULTIPLY_ROW_NEON(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8X8)
      PREMULTIPLY_ROW_NEON(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8A8)
      PREMULTIPLY_ROW_NEON(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8X8)
      PREMULTIPLY_ROW_NEON(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      PREMULTIPLY_ROW_NEON(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8X8)
      default:
        break;
    }
#endif

  if (!(aArch & SwizzleArch::eFallback)) {
    return nullptr;
  }

  switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
    PREMULTIPLY_ROW_FALLBACK(SurfaceFormat::B8G8R8A8)
    PREMULTIPLY_ROW_FALLBACK(SurfaceFormat::R8G8B8A8)
    PREMULTIPLY_ROW_FALLBACK(SurfaceFormat::A8R8G8B8)
    default:
      break;
  }

  MOZ_ASSERT_UNREACHABLE("Unsupported premultiply formats");
  return nullptr;
}

/**
 * Unpremultiplying
 */

// Generate a table of 8.16 fixed-point reciprocals representing 1/alpha.
// This generates a table of 8.16 fixed-point reciprocals representing 1/alpha,
// identical to the scalar fallback's sUnpremultiplyTable (0xFF00FF / alpha).
// The full reciprocal can need up to 24 bits, so it does not fit in a single
// 16-bit lane; UnpremultiplyVector_SSE2 splits each entry into a high and low
// 16-bit half at runtime and computes an exact (channel * reciprocal) >> 16
// with two 16-bit multiplies. A previous implementation squeezed the reciprocal
// into 16 bits by scaling it down (by 8 or 0x100), which lost precision and
// rounded down by 1 LSB on a subset of anti-aliased / alpha pixels, making
// getImageData() differ by CPU architecture (x86 SSE2 vs scalar/NEON). Using
// the full reciprocal makes the SSE2 output bit-identical to the scalar/NEON
// paths.
#define UNPREMULQ(x) (0xFF00FFU / (x))
#define UNPREMULQ_2(x) UNPREMULQ(x), UNPREMULQ((x) + 1)
#define UNPREMULQ_4(x) UNPREMULQ_2(x), UNPREMULQ_2((x) + 2)
#define UNPREMULQ_8(x) UNPREMULQ_4(x), UNPREMULQ_4((x) + 4)
#define UNPREMULQ_16(x) UNPREMULQ_8(x), UNPREMULQ_8((x) + 8)
#define UNPREMULQ_32(x) UNPREMULQ_16(x), UNPREMULQ_16((x) + 16)
extern const uint32_t sUnpremultiplyTable[256] = {0,
                                                  UNPREMULQ(1),
                                                  UNPREMULQ_2(2),
                                                  UNPREMULQ_4(4),
                                                  UNPREMULQ_8(8),
                                                  UNPREMULQ_16(16),
                                                  UNPREMULQ_32(32),
                                                  UNPREMULQ_32(64),
                                                  UNPREMULQ_32(96),
                                                  UNPREMULQ_32(128),
                                                  UNPREMULQ_32(160),
                                                  UNPREMULQ_32(192),
                                                  UNPREMULQ_32(224)};

// Fallback unpremultiply implementation that uses 8.16 fixed-point reciprocal
// math to eliminate any division by the alpha component. This optimization is
// used for the SSE2 and NEON implementations, with some adaptations. This
// implementation also accesses color components using individual byte accesses
// as this profiles faster than accessing the pixel as a uint32_t and
// shifting/masking to access components.
template <bool aSwapRB, uint32_t aSrcRGBIndex, uint32_t aSrcAIndex,
          uint32_t aDstRGBIndex, uint32_t aDstAIndex>
static void UnpremultiplyChunkFallback(const uint8_t*& aSrc, uint8_t*& aDst,
                                       int32_t aLength) {
  const uint8_t* end = aSrc + 4 * aLength;
  do {
    uint8_t r = aSrc[aSrcRGBIndex + (aSwapRB ? 2 : 0)];
    uint8_t g = aSrc[aSrcRGBIndex + 1];
    uint8_t b = aSrc[aSrcRGBIndex + (aSwapRB ? 0 : 2)];
    uint8_t a = aSrc[aSrcAIndex];

    // Access the 8.16 reciprocal from the table based on alpha. Multiply by
    // the reciprocal and shift off the fraction bits to approximate the
    // division by alpha.
    uint32_t q = sUnpremultiplyTable[a];
    aDst[aDstRGBIndex + 0] = (r * q) >> 16;
    aDst[aDstRGBIndex + 1] = (g * q) >> 16;
    aDst[aDstRGBIndex + 2] = (b * q) >> 16;
    aDst[aDstAIndex] = a;

    aSrc += 4;
    aDst += 4;
  } while (aSrc < end);
}

template <bool aSwapRB, uint32_t aSrcRGBIndex, uint32_t aSrcAIndex,
          uint32_t aDstRGBIndex, uint32_t aDstAIndex>
static void UnpremultiplyRowFallback(const uint8_t* aSrc, uint8_t* aDst,
                                     int32_t aLength) {
  UnpremultiplyChunkFallback<aSwapRB, aSrcRGBIndex, aSrcAIndex, aDstRGBIndex,
                             aDstAIndex>(aSrc, aDst, aLength);
}

template <bool aSwapRB, uint32_t aSrcRGBIndex, uint32_t aSrcAIndex,
          uint32_t aDstRGBIndex, uint32_t aDstAIndex>
static void UnpremultiplyFallback(const uint8_t* aSrc, int32_t aSrcGap,
                                  uint8_t* aDst, int32_t aDstGap,
                                  IntSize aSize) {
  for (int32_t height = aSize.height; height > 0; height--) {
    UnpremultiplyChunkFallback<aSwapRB, aSrcRGBIndex, aSrcAIndex, aDstRGBIndex,
                               aDstAIndex>(aSrc, aDst, aSize.width);
    aSrc += aSrcGap;
    aDst += aDstGap;
  }
}

#define UNPREMULTIPLY_FALLBACK_CASE(aSrcFormat, aDstFormat)             \
  FORMAT_CASE(aSrcFormat, aDstFormat,                                   \
              UnpremultiplyFallback<                                    \
                  ShouldSwapRB(aSrcFormat, aDstFormat),                 \
                  RGBByteIndex(aSrcFormat), AlphaByteIndex(aSrcFormat), \
                  RGBByteIndex(aDstFormat), AlphaByteIndex(aDstFormat)>)

#define UNPREMULTIPLY_FALLBACK(aSrcFormat)                         \
  UNPREMULTIPLY_FALLBACK_CASE(aSrcFormat, SurfaceFormat::B8G8R8A8) \
  UNPREMULTIPLY_FALLBACK_CASE(aSrcFormat, SurfaceFormat::R8G8B8A8) \
  UNPREMULTIPLY_FALLBACK_CASE(aSrcFormat, SurfaceFormat::A8R8G8B8)

#define UNPREMULTIPLY_ROW_FALLBACK_CASE(aSrcFormat, aDstFormat)             \
  FORMAT_CASE_ROW(aSrcFormat, aDstFormat,                                   \
                  UnpremultiplyRowFallback<                                 \
                      ShouldSwapRB(aSrcFormat, aDstFormat),                 \
                      RGBByteIndex(aSrcFormat), AlphaByteIndex(aSrcFormat), \
                      RGBByteIndex(aDstFormat), AlphaByteIndex(aDstFormat)>)

#define UNPREMULTIPLY_ROW_FALLBACK(aSrcFormat)                         \
  UNPREMULTIPLY_ROW_FALLBACK_CASE(aSrcFormat, SurfaceFormat::B8G8R8A8) \
  UNPREMULTIPLY_ROW_FALLBACK_CASE(aSrcFormat, SurfaceFormat::R8G8B8A8) \
  UNPREMULTIPLY_ROW_FALLBACK_CASE(aSrcFormat, SurfaceFormat::A8R8G8B8)

bool UnpremultiplyData(const uint8_t* aSrc, int32_t aSrcStride,
                       SurfaceFormat aSrcFormat, uint8_t* aDst,
                       int32_t aDstStride, SurfaceFormat aDstFormat,
                       const IntSize& aSize,
                       SwizzleArch aArch /* = SwizzleArch::eAny */) {
  if (aSize.IsEmpty()) {
    return true;
  }
  // Find gap from end of row to the start of the next row.
  int32_t srcGap = GetStrideGap(aSize.width, aSrcFormat, aSrcStride);
  int32_t dstGap = GetStrideGap(aSize.width, aDstFormat, aDstStride);
  MOZ_ASSERT(srcGap >= 0 && dstGap >= 0);
  if (srcGap < 0 || dstGap < 0) {
    return false;
  }
  IntSize size = CollapseSize(aSize, srcGap, dstGap);

#define FORMAT_CASE_CALL(...) __VA_ARGS__(aSrc, srcGap, aDst, dstGap, size)

#ifdef USE_SSE2
  if (aArch & SwizzleArch::eAVX2 && mozilla::supports_avx2())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      UNPREMULTIPLY_AVX2(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8A8)
      UNPREMULTIPLY_AVX2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      UNPREMULTIPLY_AVX2(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8A8)
      UNPREMULTIPLY_AVX2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      default:
        break;
    }

  if (aArch & SwizzleArch::eSSE2 && mozilla::supports_sse2())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      UNPREMULTIPLY_SSE2(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8A8)
      UNPREMULTIPLY_SSE2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      UNPREMULTIPLY_SSE2(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8A8)
      UNPREMULTIPLY_SSE2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      default:
        break;
    }
#endif

#ifdef USE_NEON
  if (aArch & SwizzleArch::eNEON && mozilla::supports_neon())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      UNPREMULTIPLY_NEON(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8A8)
      UNPREMULTIPLY_NEON(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      UNPREMULTIPLY_NEON(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8A8)
      UNPREMULTIPLY_NEON(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      default:
        break;
    }
#endif

  if (!(aArch & SwizzleArch::eFallback)) {
    return false;
  }

  switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
    UNPREMULTIPLY_FALLBACK(SurfaceFormat::B8G8R8A8)
    UNPREMULTIPLY_FALLBACK(SurfaceFormat::R8G8B8A8)
    UNPREMULTIPLY_FALLBACK(SurfaceFormat::A8R8G8B8)
    default:
      break;
  }

#undef FORMAT_CASE_CALL

  MOZ_ASSERT_UNREACHABLE("Unsupported unpremultiply formats");
  return false;
}

SwizzleRowFn UnpremultiplyRow(SurfaceFormat aSrcFormat,
                              SurfaceFormat aDstFormat,
                              SwizzleArch aArch /* = SwizzleArch::eAny */) {
#ifdef USE_SSE2
  if (aArch & SwizzleArch::eAVX2 && mozilla::supports_avx2())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      UNPREMULTIPLY_ROW_AVX2(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8A8)
      UNPREMULTIPLY_ROW_AVX2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      UNPREMULTIPLY_ROW_AVX2(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8A8)
      UNPREMULTIPLY_ROW_AVX2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      default:
        break;
    }

  if (aArch & SwizzleArch::eSSE2 && mozilla::supports_sse2())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      UNPREMULTIPLY_ROW_SSE2(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8A8)
      UNPREMULTIPLY_ROW_SSE2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      UNPREMULTIPLY_ROW_SSE2(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8A8)
      UNPREMULTIPLY_ROW_SSE2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      default:
        break;
    }
#endif

#ifdef USE_NEON
  if (aArch & SwizzleArch::eNEON && mozilla::supports_neon())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      UNPREMULTIPLY_ROW_NEON(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8A8)
      UNPREMULTIPLY_ROW_NEON(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      UNPREMULTIPLY_ROW_NEON(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8A8)
      UNPREMULTIPLY_ROW_NEON(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      default:
        break;
    }
#endif

  if (!(aArch & SwizzleArch::eFallback)) {
    return nullptr;
  }

  switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
    UNPREMULTIPLY_ROW_FALLBACK(SurfaceFormat::B8G8R8A8)
    UNPREMULTIPLY_ROW_FALLBACK(SurfaceFormat::R8G8B8A8)
    UNPREMULTIPLY_ROW_FALLBACK(SurfaceFormat::A8R8G8B8)
    default:
      break;
  }

  MOZ_ASSERT_UNREACHABLE("Unsupported premultiply formats");
  return nullptr;
}

/**
 * Swizzling
 */

// Fallback swizzle implementation that uses shifting and masking to reorder
// pixels.
template <bool aSwapRB, bool aOpaqueAlpha, uint32_t aSrcRGBShift,
          uint32_t aSrcAShift, uint32_t aDstRGBShift, uint32_t aDstAShift>
static void SwizzleChunkFallback(const uint8_t*& aSrc, uint8_t*& aDst,
                                 int32_t aLength) {
  const uint8_t* end = aSrc + 4 * aLength;
  do {
    uint32_t rgba = *reinterpret_cast<const uint32_t*>(aSrc);

    if (aSwapRB) {
      // Handle R and B swaps by exchanging words and masking.
      uint32_t rb =
          ((rgba << 16) | (rgba >> 16)) & (0x00FF00FF << aSrcRGBShift);
      uint32_t ga = rgba & ((0xFF << aSrcAShift) | (0xFF00 << aSrcRGBShift));
      rgba = rb | ga;
    }

    // If src and dst shifts differ, rotate left or right to move RGB into
    // place, i.e. ARGB -> RGBA or ARGB -> RGBA.
    if (aDstRGBShift > aSrcRGBShift) {
      rgba = (rgba << 8) | (aOpaqueAlpha ? 0x000000FF : rgba >> 24);
    } else if (aSrcRGBShift > aDstRGBShift) {
      rgba = (rgba >> 8) | (aOpaqueAlpha ? 0xFF000000 : rgba << 24);
    } else if (aOpaqueAlpha) {
      rgba |= 0xFF << aDstAShift;
    }

    *reinterpret_cast<uint32_t*>(aDst) = rgba;

    aSrc += 4;
    aDst += 4;
  } while (aSrc < end);
}

template <bool aSwapRB, bool aOpaqueAlpha, uint32_t aSrcRGBShift,
          uint32_t aSrcAShift, uint32_t aDstRGBShift, uint32_t aDstAShift>
static void SwizzleRowFallback(const uint8_t* aSrc, uint8_t* aDst,
                               int32_t aLength) {
  SwizzleChunkFallback<aSwapRB, aOpaqueAlpha, aSrcRGBShift, aSrcAShift,
                       aDstRGBShift, aDstAShift>(aSrc, aDst, aLength);
}

template <bool aSwapRB, bool aOpaqueAlpha, uint32_t aSrcRGBShift,
          uint32_t aSrcAShift, uint32_t aDstRGBShift, uint32_t aDstAShift>
static void SwizzleFallback(const uint8_t* aSrc, int32_t aSrcGap, uint8_t* aDst,
                            int32_t aDstGap, IntSize aSize) {
  for (int32_t height = aSize.height; height > 0; height--) {
    SwizzleChunkFallback<aSwapRB, aOpaqueAlpha, aSrcRGBShift, aSrcAShift,
                         aDstRGBShift, aDstAShift>(aSrc, aDst, aSize.width);
    aSrc += aSrcGap;
    aDst += aDstGap;
  }
}

#define SWIZZLE_FALLBACK(aSrcFormat, aDstFormat)                          \
  FORMAT_CASE(                                                            \
      aSrcFormat, aDstFormat,                                             \
      SwizzleFallback<ShouldSwapRB(aSrcFormat, aDstFormat),               \
                      ShouldForceOpaque(aSrcFormat, aDstFormat),          \
                      RGBBitShift(aSrcFormat), AlphaBitShift(aSrcFormat), \
                      RGBBitShift(aDstFormat), AlphaBitShift(aDstFormat)>)

#define SWIZZLE_ROW_FALLBACK(aSrcFormat, aDstFormat)                         \
  FORMAT_CASE_ROW(                                                           \
      aSrcFormat, aDstFormat,                                                \
      SwizzleRowFallback<ShouldSwapRB(aSrcFormat, aDstFormat),               \
                         ShouldForceOpaque(aSrcFormat, aDstFormat),          \
                         RGBBitShift(aSrcFormat), AlphaBitShift(aSrcFormat), \
                         RGBBitShift(aDstFormat), AlphaBitShift(aDstFormat)>)

// Fast-path for matching formats.
template <int32_t aBytesPerPixel>
static void SwizzleRowCopy(const uint8_t* aSrc, uint8_t* aDst,
                           int32_t aLength) {
  if (aSrc != aDst) {
    memcpy(aDst, aSrc, aLength * aBytesPerPixel);
  }
}

// Fast-path for matching formats.
static void SwizzleCopy(const uint8_t* aSrc, int32_t aSrcGap, uint8_t* aDst,
                        int32_t aDstGap, IntSize aSize, int32_t aBPP) {
  if (aSrc != aDst) {
    int32_t rowLength = aBPP * aSize.width;
    for (int32_t height = aSize.height; height > 0; height--) {
      memcpy(aDst, aSrc, rowLength);
      aSrc += rowLength + aSrcGap;
      aDst += rowLength + aDstGap;
    }
  }
}

// Fast-path for conversions that swap all bytes.
template <bool aOpaqueAlpha, uint32_t aSrcAShift, uint32_t aDstAShift>
static void SwizzleChunkSwap(const uint8_t*& aSrc, uint8_t*& aDst,
                             int32_t aLength) {
  const uint8_t* end = aSrc + 4 * aLength;
  do {
    // Use an endian swap to move the bytes, i.e. BGRA -> ARGB.
    uint32_t rgba = *reinterpret_cast<const uint32_t*>(aSrc);
    if constexpr (std::endian::native == std::endian::little) {
      rgba = NativeEndian::swapToBigEndian(rgba);
    } else {
      rgba = NativeEndian::swapToLittleEndian(rgba);
    }
    if (aOpaqueAlpha) {
      rgba |= 0xFF << aDstAShift;
    }
    *reinterpret_cast<uint32_t*>(aDst) = rgba;
    aSrc += 4;
    aDst += 4;
  } while (aSrc < end);
}

template <bool aOpaqueAlpha, uint32_t aSrcAShift, uint32_t aDstAShift>
static void SwizzleRowSwap(const uint8_t* aSrc, uint8_t* aDst,
                           int32_t aLength) {
  SwizzleChunkSwap<aOpaqueAlpha, aSrcAShift, aDstAShift>(aSrc, aDst, aLength);
}

template <bool aOpaqueAlpha, uint32_t aSrcAShift, uint32_t aDstAShift>
static void SwizzleSwap(const uint8_t* aSrc, int32_t aSrcGap, uint8_t* aDst,
                        int32_t aDstGap, IntSize aSize) {
  for (int32_t height = aSize.height; height > 0; height--) {
    SwizzleChunkSwap<aOpaqueAlpha, aSrcAShift, aDstAShift>(aSrc, aDst,
                                                           aSize.width);
    aSrc += aSrcGap;
    aDst += aDstGap;
  }
}

#define SWIZZLE_SWAP(aSrcFormat, aDstFormat)                 \
  FORMAT_CASE(                                               \
      aSrcFormat, aDstFormat,                                \
      SwizzleSwap<ShouldForceOpaque(aSrcFormat, aDstFormat), \
                  AlphaBitShift(aSrcFormat), AlphaBitShift(aDstFormat)>)

#define SWIZZLE_ROW_SWAP(aSrcFormat, aDstFormat)                \
  FORMAT_CASE_ROW(                                              \
      aSrcFormat, aDstFormat,                                   \
      SwizzleRowSwap<ShouldForceOpaque(aSrcFormat, aDstFormat), \
                     AlphaBitShift(aSrcFormat), AlphaBitShift(aDstFormat)>)

static void SwizzleChunkSwapRGB24(const uint8_t*& aSrc, uint8_t*& aDst,
                                  int32_t aLength) {
  const uint8_t* end = aSrc + 3 * aLength;
  do {
    uint8_t r = aSrc[0];
    uint8_t g = aSrc[1];
    uint8_t b = aSrc[2];
    aDst[0] = b;
    aDst[1] = g;
    aDst[2] = r;
    aSrc += 3;
    aDst += 3;
  } while (aSrc < end);
}

static void SwizzleRowSwapRGB24(const uint8_t* aSrc, uint8_t* aDst,
                                int32_t aLength) {
  SwizzleChunkSwapRGB24(aSrc, aDst, aLength);
}

static void SwizzleSwapRGB24(const uint8_t* aSrc, int32_t aSrcGap,
                             uint8_t* aDst, int32_t aDstGap, IntSize aSize) {
  for (int32_t height = aSize.height; height > 0; height--) {
    SwizzleChunkSwapRGB24(aSrc, aDst, aSize.width);
    aSrc += aSrcGap;
    aDst += aDstGap;
  }
}

#define SWIZZLE_SWAP_RGB24(aSrcFormat, aDstFormat) \
  FORMAT_CASE(aSrcFormat, aDstFormat, SwizzleSwapRGB24)

#define SWIZZLE_ROW_SWAP_RGB24(aSrcFormat, aDstFormat) \
  FORMAT_CASE_ROW(aSrcFormat, aDstFormat, SwizzleRowSwapRGB24)

// Fast-path for conversions that force alpha to opaque.
template <uint32_t aDstAShift>
static void SwizzleChunkOpaqueUpdate(uint8_t*& aBuffer, int32_t aLength) {
  const uint8_t* end = aBuffer + 4 * aLength;
  do {
    uint32_t rgba = *reinterpret_cast<const uint32_t*>(aBuffer);
    // Just add on the alpha bits to the source.
    rgba |= 0xFF << aDstAShift;
    *reinterpret_cast<uint32_t*>(aBuffer) = rgba;
    aBuffer += 4;
  } while (aBuffer < end);
}

template <uint32_t aDstAShift>
static void SwizzleChunkOpaqueCopy(const uint8_t*& aSrc, uint8_t*& aDst,
                                   int32_t aLength) {
  const uint8_t* end = aSrc + 4 * aLength;
  do {
    uint32_t rgba = *reinterpret_cast<const uint32_t*>(aSrc);
    // Just add on the alpha bits to the source.
    rgba |= 0xFF << aDstAShift;
    *reinterpret_cast<uint32_t*>(aDst) = rgba;
    aSrc += 4;
    aDst += 4;
  } while (aSrc < end);
}

template <uint32_t aDstAShift>
static void SwizzleRowOpaque(const uint8_t* aSrc, uint8_t* aDst,
                             int32_t aLength) {
  if (aSrc == aDst) {
    SwizzleChunkOpaqueUpdate<aDstAShift>(aDst, aLength);
  } else {
    SwizzleChunkOpaqueCopy<aDstAShift>(aSrc, aDst, aLength);
  }
}

template <uint32_t aDstAShift>
static void SwizzleOpaque(const uint8_t* aSrc, int32_t aSrcGap, uint8_t* aDst,
                          int32_t aDstGap, IntSize aSize) {
  if (aSrc == aDst) {
    // Modifying in-place, so just write out the alpha.
    for (int32_t height = aSize.height; height > 0; height--) {
      SwizzleChunkOpaqueUpdate<aDstAShift>(aDst, aSize.width);
      aDst += aDstGap;
    }
  } else {
    for (int32_t height = aSize.height; height > 0; height--) {
      SwizzleChunkOpaqueCopy<aDstAShift>(aSrc, aDst, aSize.width);
      aSrc += aSrcGap;
      aDst += aDstGap;
    }
  }
}

#define SWIZZLE_OPAQUE(aSrcFormat, aDstFormat) \
  FORMAT_CASE(aSrcFormat, aDstFormat, SwizzleOpaque<AlphaBitShift(aDstFormat)>)

#define SWIZZLE_ROW_OPAQUE(aSrcFormat, aDstFormat) \
  FORMAT_CASE_ROW(aSrcFormat, aDstFormat,          \
                  SwizzleRowOpaque<AlphaBitShift(aDstFormat)>)

// Packing of 32-bit formats to RGB565.
template <bool aSwapRB, uint32_t aSrcRGBShift, uint32_t aSrcRGBIndex>
static void PackToRGB565(const uint8_t* aSrc, int32_t aSrcGap, uint8_t* aDst,
                         int32_t aDstGap, IntSize aSize) {
  for (int32_t height = aSize.height; height > 0; height--) {
    const uint8_t* end = aSrc + 4 * aSize.width;
    do {
      uint32_t rgba = *reinterpret_cast<const uint32_t*>(aSrc);

      // Isolate the R, G, and B components and shift to final endian-dependent
      // locations.
      uint16_t rgb565;
      if (aSwapRB) {
        rgb565 = ((rgba & (0xF8 << aSrcRGBShift)) << (8 - aSrcRGBShift)) |
                 ((rgba & (0xFC00 << aSrcRGBShift)) >> (5 + aSrcRGBShift)) |
                 ((rgba & (0xF80000 << aSrcRGBShift)) >> (19 + aSrcRGBShift));
      } else {
        rgb565 = ((rgba & (0xF8 << aSrcRGBShift)) >> (3 + aSrcRGBShift)) |
                 ((rgba & (0xFC00 << aSrcRGBShift)) >> (5 + aSrcRGBShift)) |
                 ((rgba & (0xF80000 << aSrcRGBShift)) >> (8 + aSrcRGBShift));
      }

      *reinterpret_cast<uint16_t*>(aDst) = rgb565;

      aSrc += 4;
      aDst += 2;
    } while (aSrc < end);

    aSrc += aSrcGap;
    aDst += aDstGap;
  }
}

// Packing of 32-bit formats to 24-bit formats.
template <bool aSwapRB, uint32_t aSrcRGBShift, uint32_t aSrcRGBIndex>
static void PackChunkToRGB24(const uint8_t*& aSrc, uint8_t*& aDst,
                             int32_t aLength) {
  const uint8_t* end = aSrc + 4 * aLength;
  do {
    uint8_t r = aSrc[aSrcRGBIndex + (aSwapRB ? 2 : 0)];
    uint8_t g = aSrc[aSrcRGBIndex + 1];
    uint8_t b = aSrc[aSrcRGBIndex + (aSwapRB ? 0 : 2)];

    aDst[0] = r;
    aDst[1] = g;
    aDst[2] = b;

    aSrc += 4;
    aDst += 3;
  } while (aSrc < end);
}

template <bool aSwapRB, uint32_t aSrcRGBShift, uint32_t aSrcRGBIndex>
static void PackRowToRGB24(const uint8_t* aSrc, uint8_t* aDst,
                           int32_t aLength) {
  PackChunkToRGB24<aSwapRB, aSrcRGBShift, aSrcRGBIndex>(aSrc, aDst, aLength);
}

template <bool aSwapRB, uint32_t aSrcRGBShift, uint32_t aSrcRGBIndex>
static void PackToRGB24(const uint8_t* aSrc, int32_t aSrcGap, uint8_t* aDst,
                        int32_t aDstGap, IntSize aSize) {
  for (int32_t height = aSize.height; height > 0; height--) {
    PackChunkToRGB24<aSwapRB, aSrcRGBShift, aSrcRGBIndex>(aSrc, aDst,
                                                          aSize.width);
    aSrc += aSrcGap;
    aDst += aDstGap;
  }
}

#define PACK_RGB_CASE(aSrcFormat, aDstFormat, aPackFunc)      \
  FORMAT_CASE(aSrcFormat, aDstFormat,                         \
              aPackFunc<ShouldSwapRB(aSrcFormat, aDstFormat), \
                        RGBBitShift(aSrcFormat), RGBByteIndex(aSrcFormat)>)

#define PACK_RGB(aDstFormat, aPackFunc)                         \
  PACK_RGB_CASE(SurfaceFormat::B8G8R8A8, aDstFormat, aPackFunc) \
  PACK_RGB_CASE(SurfaceFormat::B8G8R8X8, aDstFormat, aPackFunc) \
  PACK_RGB_CASE(SurfaceFormat::R8G8B8A8, aDstFormat, aPackFunc) \
  PACK_RGB_CASE(SurfaceFormat::R8G8B8X8, aDstFormat, aPackFunc) \
  PACK_RGB_CASE(SurfaceFormat::A8R8G8B8, aDstFormat, aPackFunc) \
  PACK_RGB_CASE(SurfaceFormat::X8R8G8B8, aDstFormat, aPackFunc)

#define PACK_ROW_RGB_CASE(aSrcFormat, aDstFormat, aPackFunc)                   \
  FORMAT_CASE_ROW(                                                             \
      aSrcFormat, aDstFormat,                                                  \
      aPackFunc<ShouldSwapRB(aSrcFormat, aDstFormat), RGBBitShift(aSrcFormat), \
                RGBByteIndex(aSrcFormat)>)

#define PACK_ROW_RGB(aDstFormat, aPackFunc)                         \
  PACK_ROW_RGB_CASE(SurfaceFormat::B8G8R8A8, aDstFormat, aPackFunc) \
  PACK_ROW_RGB_CASE(SurfaceFormat::B8G8R8X8, aDstFormat, aPackFunc) \
  PACK_ROW_RGB_CASE(SurfaceFormat::R8G8B8A8, aDstFormat, aPackFunc) \
  PACK_ROW_RGB_CASE(SurfaceFormat::R8G8B8X8, aDstFormat, aPackFunc) \
  PACK_ROW_RGB_CASE(SurfaceFormat::A8R8G8B8, aDstFormat, aPackFunc) \
  PACK_ROW_RGB_CASE(SurfaceFormat::X8R8G8B8, aDstFormat, aPackFunc)

// Packing of 32-bit formats to A8.
template <uint32_t aSrcAIndex>
static void PackToA8(const uint8_t* aSrc, int32_t aSrcGap, uint8_t* aDst,
                     int32_t aDstGap, IntSize aSize) {
  for (int32_t height = aSize.height; height > 0; height--) {
    const uint8_t* end = aSrc + 4 * aSize.width;
    do {
      *aDst++ = aSrc[aSrcAIndex];
      aSrc += 4;
    } while (aSrc < end);
    aSrc += aSrcGap;
    aDst += aDstGap;
  }
}

#define PACK_ALPHA_CASE(aSrcFormat, aDstFormat, aPackFunc) \
  FORMAT_CASE(aSrcFormat, aDstFormat, aPackFunc<AlphaByteIndex(aSrcFormat)>)

#define PACK_ALPHA(aDstFormat, aPackFunc)                         \
  PACK_ALPHA_CASE(SurfaceFormat::B8G8R8A8, aDstFormat, aPackFunc) \
  PACK_ALPHA_CASE(SurfaceFormat::R8G8B8A8, aDstFormat, aPackFunc) \
  PACK_ALPHA_CASE(SurfaceFormat::A8R8G8B8, aDstFormat, aPackFunc)

template <bool aSwapRB>
void UnpackRowRGB24(const uint8_t* aSrc, uint8_t* aDst, int32_t aLength) {
  // Because we are expanding, we can only process the data back to front in
  // case we are performing this in place.
  const uint8_t* src = aSrc + 3 * (aLength - 1);
  uint32_t* dst = reinterpret_cast<uint32_t*>(aDst + 4 * aLength);
  while (src >= aSrc) {
    uint8_t r = src[aSwapRB ? 2 : 0];
    uint8_t g = src[1];
    uint8_t b = src[aSwapRB ? 0 : 2];
    if constexpr (std::endian::native == std::endian::little) {
      *--dst = 0xFF000000 | (b << 16) | (g << 8) | r;
    } else {
      *--dst = 0x000000FF | (b << 8) | (g << 16) | (r << 24);
    }
    src -= 3;
  }
}

// Force instantiation of swizzle variants here.
template void UnpackRowRGB24<false>(const uint8_t*, uint8_t*, int32_t);
template void UnpackRowRGB24<true>(const uint8_t*, uint8_t*, int32_t);

#define UNPACK_ROW_RGB(aDstFormat)       \
  FORMAT_CASE_ROW(                       \
      SurfaceFormat::R8G8B8, aDstFormat, \
      UnpackRowRGB24<ShouldSwapRB(SurfaceFormat::R8G8B8, aDstFormat)>)

static void UnpackRowRGB24_To_ARGB(const uint8_t* aSrc, uint8_t* aDst,
                                   int32_t aLength) {
  // Because we are expanding, we can only process the data back to front in
  // case we are performing this in place.
  const uint8_t* src = aSrc + 3 * (aLength - 1);
  uint32_t* dst = reinterpret_cast<uint32_t*>(aDst + 4 * aLength);
  while (src >= aSrc) {
    uint8_t r = src[0];
    uint8_t g = src[1];
    uint8_t b = src[2];
    if constexpr (std::endian::native == std::endian::little) {
      *--dst = 0x000000FF | (r << 8) | (g << 16) | (b << 24);
    } else {
      *--dst = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
    src -= 3;
  }
}

#define UNPACK_ROW_RGB_TO_ARGB(aDstFormat) \
  FORMAT_CASE_ROW(SurfaceFormat::R8G8B8, aDstFormat, UnpackRowRGB24_To_ARGB)

// Fallback CMYK conversion implementation that uses splayed pixel math to
// reduce the multiplications used. That is, the C and M components are isolated
// from the Y and K components, which then can be multiplied as if they were two
// 2-component vectors. Otherwise, an approximation of divide-by-255 is used
// which is faster than an actual division. These optimizations are also used
// for the xsimd implementations.
template <bool aSwapRB, bool aInverted, uint32_t aDstRGBShift,
          uint32_t aDstAShift>
static void SwizzleCmykRowFallback(const uint8_t* aSrc, uint8_t* aDst,
                                   int32_t aLength) {
  // Source is 'Inverted CMYK', output is RGB.
  // See: http://www.easyrgb.com/math.php?MATH=M12#text12
  // Or:  http://www.ilkeratalay.com/colorspacesfaq.php#rgb

  // From CMYK to CMY
  // C = ( C * ( 1 - K ) + K )
  // M = ( M * ( 1 - K ) + K )
  // Y = ( Y * ( 1 - K ) + K )

  // From Inverted CMYK to CMY is thus:
  // C = ( (1-iC) * (1 - (1-iK)) + (1-iK) ) => 1 - iC*iK
  // Same for M and Y

  // Convert from CMY (0..1) to RGB (0..1)
  // R = 1 - C => 1 - (1 - iC*iK) => iC*iK
  // G = 1 - M => 1 - (1 - iM*iK) => iM*iK
  // B = 1 - Y => 1 - (1 - iY*iK) => iY*iK
  const uint8_t* end = aSrc + 4 * aLength;
  do {
    // Load and process 1 entire pixel at a time. The source is always inverted
    // CMYK with iC, iM, iY, iK in bytes 0..3, so iK is the high byte, iC/iY
    // form the even 16-bit lane pair and iM/iK form the odd pair.
    uint32_t color = *reinterpret_cast<const uint32_t*>(aSrc);

    // Invert if necessary, as the math expects inverted CMYK.
    if constexpr (!aInverted) {
      color = ~color;
    }

    uint32_t ik = color >> 24;

    // Isolate the iC and iY components (the even lanes).
    uint32_t icy = color & 0x00FF00FF;
    // Swap the order of iC and iY if necessary.
    if constexpr (aSwapRB) {
      icy = (icy >> 16) | (icy << 16);
    }
    // Approximate the multiply by iK and divide by 255 which is essentially (ic
    // means inverted channel here, not the iC channel):
    // ic = ic*ik; ic = (ic + (ic >> 8) + 1) >> 8;
    // However, we omit the final >> 8 to fold it with the final shift into
    // place depending on desired output format.
    icy = icy * ik;
    icy = (icy + ((icy >> 8) & 0x00FF00FF) + 0x00010001) & 0xFF00FF00;

    // Do the same for the M and K channels.
    uint32_t imk = (color >> 8) & 0x00FF00FF;
    imk = imk * ik;
    imk = (imk + ((imk >> 8) & 0x00FF00FF) + 0x00010001) & 0xFF00FF00;

    // The above math leaves each channel shifted left by 8 bits. Shift the C/Y
    // pair and the M channel into their destination positions and force
    // the alpha opaque.
    *reinterpret_cast<uint32_t*>(aDst) = (icy >> (8 - aDstRGBShift)) |
                                         ((imk & 0x0000FF00) << aDstRGBShift) |
                                         (0xFF << aDstAShift);

    aSrc += 4;
    aDst += 4;
  } while (aSrc < end);
}

#define SWIZZLE_CMYK_ROW(aSrcFormat, aDstFormat)                          \
  FORMAT_CASE_ROW(                                                        \
      aSrcFormat, aDstFormat,                                             \
      SwizzleCmykRowFallback<                                             \
          ShouldSwapRB(aSrcFormat, aDstFormat), ShouldInvert(aSrcFormat), \
          RGBBitShift(aDstFormat), AlphaBitShift(aDstFormat)>)

bool SwizzleData(const uint8_t* aSrc, int32_t aSrcStride,
                 SurfaceFormat aSrcFormat, uint8_t* aDst, int32_t aDstStride,
                 SurfaceFormat aDstFormat, const IntSize& aSize,
                 SwizzleArch aArch /* = SwizzleArch::eAny */) {
  if (aSize.IsEmpty()) {
    return true;
  }
  // Find gap from end of row to the start of the next row.
  int32_t srcGap = GetStrideGap(aSize.width, aSrcFormat, aSrcStride);
  int32_t dstGap = GetStrideGap(aSize.width, aDstFormat, aDstStride);
  MOZ_ASSERT(srcGap >= 0 && dstGap >= 0);
  if (srcGap < 0 || dstGap < 0) {
    return false;
  }
  IntSize size = CollapseSize(aSize, srcGap, dstGap);

#define FORMAT_CASE_CALL(...) __VA_ARGS__(aSrc, srcGap, aDst, dstGap, size)

#ifdef USE_SSE2
  if (aArch & SwizzleArch::eAVX2 && mozilla::supports_avx2())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      SWIZZLE_AVX2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      SWIZZLE_AVX2(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8X8)
      SWIZZLE_AVX2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8X8)
      SWIZZLE_AVX2(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8A8)
      SWIZZLE_AVX2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      SWIZZLE_AVX2(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8X8)
      SWIZZLE_AVX2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8X8)
      SWIZZLE_AVX2(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8A8)
      default:
        break;
    }

  if (aArch & SwizzleArch::eSSSE3 && mozilla::supports_ssse3())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      SWIZZLE_SSSE3(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      SWIZZLE_SSSE3(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8X8)
      SWIZZLE_SSSE3(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8X8)
      SWIZZLE_SSSE3(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8A8)
      SWIZZLE_SSSE3(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      SWIZZLE_SSSE3(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8X8)
      SWIZZLE_SSSE3(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8X8)
      SWIZZLE_SSSE3(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8A8)
      default:
        break;
    }

  if (aArch & SwizzleArch::eSSE2 && mozilla::supports_sse2())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      SWIZZLE_SSE2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      SWIZZLE_SSE2(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8X8)
      SWIZZLE_SSE2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8X8)
      SWIZZLE_SSE2(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8A8)
      SWIZZLE_SSE2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      SWIZZLE_SSE2(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8X8)
      SWIZZLE_SSE2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8X8)
      SWIZZLE_SSE2(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8A8)
      default:
        break;
    }
#endif

#ifdef USE_NEON
  if (aArch & SwizzleArch::eNEON && mozilla::supports_neon())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      SWIZZLE_NEON(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      SWIZZLE_NEON(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8X8)
      SWIZZLE_NEON(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8X8)
      SWIZZLE_NEON(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8A8)
      SWIZZLE_NEON(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      SWIZZLE_NEON(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8X8)
      SWIZZLE_NEON(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8X8)
      SWIZZLE_NEON(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8A8)
      default:
        break;
    }
#endif

  if (!(aArch & SwizzleArch::eFallback)) {
    return false;
  }

  switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
    SWIZZLE_FALLBACK(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
    SWIZZLE_FALLBACK(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8X8)
    SWIZZLE_FALLBACK(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8X8)
    SWIZZLE_FALLBACK(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8A8)

    SWIZZLE_FALLBACK(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
    SWIZZLE_FALLBACK(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8X8)
    SWIZZLE_FALLBACK(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8X8)
    SWIZZLE_FALLBACK(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8A8)
    SWIZZLE_FALLBACK(SurfaceFormat::R8G8B8A8, SurfaceFormat::A8R8G8B8)
    SWIZZLE_FALLBACK(SurfaceFormat::R8G8B8X8, SurfaceFormat::X8R8G8B8)

    SWIZZLE_FALLBACK(SurfaceFormat::A8R8G8B8, SurfaceFormat::R8G8B8A8)
    SWIZZLE_FALLBACK(SurfaceFormat::X8R8G8B8, SurfaceFormat::R8G8B8X8)
    SWIZZLE_FALLBACK(SurfaceFormat::A8R8G8B8, SurfaceFormat::R8G8B8X8)
    SWIZZLE_FALLBACK(SurfaceFormat::X8R8G8B8, SurfaceFormat::R8G8B8A8)

    SWIZZLE_SWAP(SurfaceFormat::B8G8R8A8, SurfaceFormat::A8R8G8B8)
    SWIZZLE_SWAP(SurfaceFormat::B8G8R8A8, SurfaceFormat::X8R8G8B8)
    SWIZZLE_SWAP(SurfaceFormat::B8G8R8X8, SurfaceFormat::X8R8G8B8)
    SWIZZLE_SWAP(SurfaceFormat::B8G8R8X8, SurfaceFormat::A8R8G8B8)
    SWIZZLE_SWAP(SurfaceFormat::A8R8G8B8, SurfaceFormat::B8G8R8A8)
    SWIZZLE_SWAP(SurfaceFormat::A8R8G8B8, SurfaceFormat::B8G8R8X8)
    SWIZZLE_SWAP(SurfaceFormat::X8R8G8B8, SurfaceFormat::B8G8R8X8)
    SWIZZLE_SWAP(SurfaceFormat::X8R8G8B8, SurfaceFormat::B8G8R8A8)

    SWIZZLE_SWAP_RGB24(SurfaceFormat::R8G8B8, SurfaceFormat::B8G8R8)
    SWIZZLE_SWAP_RGB24(SurfaceFormat::B8G8R8, SurfaceFormat::R8G8B8)

    SWIZZLE_OPAQUE(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8X8)
    SWIZZLE_OPAQUE(SurfaceFormat::B8G8R8X8, SurfaceFormat::B8G8R8A8)
    SWIZZLE_OPAQUE(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8X8)
    SWIZZLE_OPAQUE(SurfaceFormat::R8G8B8X8, SurfaceFormat::R8G8B8A8)
    SWIZZLE_OPAQUE(SurfaceFormat::A8R8G8B8, SurfaceFormat::X8R8G8B8)
    SWIZZLE_OPAQUE(SurfaceFormat::X8R8G8B8, SurfaceFormat::A8R8G8B8)

    PACK_RGB(SurfaceFormat::R5G6B5_UINT16, PackToRGB565)
    PACK_RGB(SurfaceFormat::B8G8R8, PackToRGB24)
    PACK_RGB(SurfaceFormat::R8G8B8, PackToRGB24)
    PACK_ALPHA(SurfaceFormat::A8, PackToA8)

    default:
      break;
  }

  if (aSrcFormat == aDstFormat) {
    // If the formats match, just do a generic copy.
    SwizzleCopy(aSrc, srcGap, aDst, dstGap, size, BytesPerPixel(aSrcFormat));
    return true;
  }

#undef FORMAT_CASE_CALL

  MOZ_ASSERT_UNREACHABLE("Unsupported swizzle formats");
  return false;
}

static bool SwizzleYFlipDataInternal(const uint8_t* aSrc, int32_t aSrcStride,
                                     SurfaceFormat aSrcFormat, uint8_t* aDst,
                                     int32_t aDstStride,
                                     SurfaceFormat aDstFormat,
                                     const IntSize& aSize,
                                     SwizzleRowFn aSwizzleFn) {
  if (!aSwizzleFn) {
    return false;
  }

  // Guarantee our width and height are both greater than zero.
  if (aSize.IsEmpty()) {
    return true;
  }

  // Unlike SwizzleData/PremultiplyData, we don't use the stride gaps directly,
  // but we can use it to verify that the stride is valid for our width and
  // format.
  int32_t srcGap = GetStrideGap(aSize.width, aSrcFormat, aSrcStride);
  int32_t dstGap = GetStrideGap(aSize.width, aDstFormat, aDstStride);
  MOZ_ASSERT(srcGap >= 0 && dstGap >= 0);
  if (srcGap < 0 || dstGap < 0) {
    return false;
  }

  // Swapping/swizzling to a new buffer is trivial.
  if (aSrc != aDst) {
    const uint8_t* src = aSrc;
    const uint8_t* srcEnd = aSrc + aSize.height * aSrcStride;
    uint8_t* dst = aDst + (aSize.height - 1) * aDstStride;
    while (src < srcEnd) {
      aSwizzleFn(src, dst, aSize.width);
      src += aSrcStride;
      dst -= aDstStride;
    }
    return true;
  }

  if (aSrcStride != aDstStride) {
    return false;
  }

  // If we are swizzling in place, then we need a temporary row buffer.
  UniquePtr<uint8_t[]> rowBuffer(new (std::nothrow) uint8_t[aDstStride]);
  if (!rowBuffer) {
    return false;
  }

  // Swizzle and swap the top and bottom rows until we meet in the middle.
  int32_t middleRow = aSize.height / 2;
  uint8_t* top = aDst;
  uint8_t* bottom = aDst + (aSize.height - 1) * aDstStride;
  for (int32_t row = 0; row < middleRow; ++row) {
    memcpy(rowBuffer.get(), bottom, aDstStride);
    aSwizzleFn(top, bottom, aSize.width);
    aSwizzleFn(rowBuffer.get(), top, aSize.width);
    top += aDstStride;
    bottom -= aDstStride;
  }

  // If there is an odd numbered row, we haven't swizzled it yet.
  if (aSize.height % 2 == 1) {
    top = aDst + middleRow * aDstStride;
    aSwizzleFn(top, top, aSize.width);
  }
  return true;
}

bool SwizzleYFlipData(const uint8_t* aSrc, int32_t aSrcStride,
                      SurfaceFormat aSrcFormat, uint8_t* aDst,
                      int32_t aDstStride, SurfaceFormat aDstFormat,
                      const IntSize& aSize,
                      SwizzleArch aArch /* = SwizzleArch::eAny */) {
  return SwizzleYFlipDataInternal(aSrc, aSrcStride, aSrcFormat, aDst,
                                  aDstStride, aDstFormat, aSize,
                                  SwizzleRow(aSrcFormat, aDstFormat, aArch));
}

bool PremultiplyYFlipData(const uint8_t* aSrc, int32_t aSrcStride,
                          SurfaceFormat aSrcFormat, uint8_t* aDst,
                          int32_t aDstStride, SurfaceFormat aDstFormat,
                          const IntSize& aSize,
                          SwizzleArch aArch /* = SwizzleArch::eAny */) {
  return SwizzleYFlipDataInternal(
      aSrc, aSrcStride, aSrcFormat, aDst, aDstStride, aDstFormat, aSize,
      PremultiplyRow(aSrcFormat, aDstFormat, aArch));
}

SwizzleRowFn SwizzleRow(SurfaceFormat aSrcFormat, SurfaceFormat aDstFormat,
                        SwizzleArch aArch /* = SwizzleArch::eAny */) {
#ifdef USE_SSE2
  if (aArch & SwizzleArch::eAVX2 && mozilla::supports_avx2())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      SWIZZLE_ROW_AVX2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      SWIZZLE_ROW_AVX2(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8X8)
      SWIZZLE_ROW_AVX2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8X8)
      SWIZZLE_ROW_AVX2(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8A8)
      SWIZZLE_ROW_AVX2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      SWIZZLE_ROW_AVX2(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8X8)
      SWIZZLE_ROW_AVX2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8X8)
      SWIZZLE_ROW_AVX2(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8A8)
      UNPACK_ROW_RGB_AVX2(SurfaceFormat::R8G8B8X8)
      UNPACK_ROW_RGB_AVX2(SurfaceFormat::R8G8B8A8)
      UNPACK_ROW_RGB_AVX2(SurfaceFormat::B8G8R8X8)
      UNPACK_ROW_RGB_AVX2(SurfaceFormat::B8G8R8A8)
      SWIZZLE_CMYK_ROW_AVX2(SurfaceFormat::CMYK, SurfaceFormat::B8G8R8X8)
      SWIZZLE_CMYK_ROW_AVX2(SurfaceFormat::CMYK, SurfaceFormat::B8G8R8A8)
      SWIZZLE_CMYK_ROW_AVX2(SurfaceFormat::InvertedCMYK,
                            SurfaceFormat::B8G8R8X8)
      SWIZZLE_CMYK_ROW_AVX2(SurfaceFormat::InvertedCMYK,
                            SurfaceFormat::B8G8R8A8)
      default:
        break;
    }

  if (aArch & SwizzleArch::eSSSE3 && mozilla::supports_ssse3())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      SWIZZLE_ROW_SSSE3(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      SWIZZLE_ROW_SSSE3(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8X8)
      SWIZZLE_ROW_SSSE3(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8X8)
      SWIZZLE_ROW_SSSE3(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8A8)
      SWIZZLE_ROW_SSSE3(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      SWIZZLE_ROW_SSSE3(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8X8)
      SWIZZLE_ROW_SSSE3(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8X8)
      SWIZZLE_ROW_SSSE3(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8A8)
      UNPACK_ROW_RGB_SSSE3(SurfaceFormat::R8G8B8X8)
      UNPACK_ROW_RGB_SSSE3(SurfaceFormat::R8G8B8A8)
      UNPACK_ROW_RGB_SSSE3(SurfaceFormat::B8G8R8X8)
      UNPACK_ROW_RGB_SSSE3(SurfaceFormat::B8G8R8A8)
      default:
        break;
    }

  if (aArch & SwizzleArch::eSSE2 && mozilla::supports_sse2())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      SWIZZLE_ROW_SSE2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      SWIZZLE_ROW_SSE2(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8X8)
      SWIZZLE_ROW_SSE2(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8X8)
      SWIZZLE_ROW_SSE2(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8A8)
      SWIZZLE_ROW_SSE2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      SWIZZLE_ROW_SSE2(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8X8)
      SWIZZLE_ROW_SSE2(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8X8)
      SWIZZLE_ROW_SSE2(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8A8)
      SWIZZLE_CMYK_ROW_SSE2(SurfaceFormat::CMYK, SurfaceFormat::B8G8R8X8)
      SWIZZLE_CMYK_ROW_SSE2(SurfaceFormat::CMYK, SurfaceFormat::B8G8R8A8)
      SWIZZLE_CMYK_ROW_SSE2(SurfaceFormat::InvertedCMYK,
                            SurfaceFormat::B8G8R8X8)
      SWIZZLE_CMYK_ROW_SSE2(SurfaceFormat::InvertedCMYK,
                            SurfaceFormat::B8G8R8A8)
      default:
        break;
    }
#endif

#ifdef USE_NEON
  if (aArch & SwizzleArch::eNEON && mozilla::supports_neon())
    switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
      UNPACK_ROW_RGB_NEON(SurfaceFormat::R8G8B8X8)
      UNPACK_ROW_RGB_NEON(SurfaceFormat::R8G8B8A8)
      UNPACK_ROW_RGB_NEON(SurfaceFormat::B8G8R8X8)
      UNPACK_ROW_RGB_NEON(SurfaceFormat::B8G8R8A8)
      SWIZZLE_ROW_NEON(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
      SWIZZLE_ROW_NEON(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8X8)
      SWIZZLE_ROW_NEON(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8X8)
      SWIZZLE_ROW_NEON(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8A8)
      SWIZZLE_ROW_NEON(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
      SWIZZLE_ROW_NEON(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8X8)
      SWIZZLE_ROW_NEON(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8X8)
      SWIZZLE_ROW_NEON(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8A8)
      SWIZZLE_CMYK_ROW_NEON(SurfaceFormat::CMYK, SurfaceFormat::B8G8R8X8)
      SWIZZLE_CMYK_ROW_NEON(SurfaceFormat::CMYK, SurfaceFormat::B8G8R8A8)
      SWIZZLE_CMYK_ROW_NEON(SurfaceFormat::InvertedCMYK,
                            SurfaceFormat::B8G8R8X8)
      SWIZZLE_CMYK_ROW_NEON(SurfaceFormat::InvertedCMYK,
                            SurfaceFormat::B8G8R8A8)
      default:
        break;
    }
#endif

  if (!(aArch & SwizzleArch::eFallback)) {
    return nullptr;
  }

  switch (FORMAT_KEY(aSrcFormat, aDstFormat)) {
    SWIZZLE_ROW_FALLBACK(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8)
    SWIZZLE_ROW_FALLBACK(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8X8)
    SWIZZLE_ROW_FALLBACK(SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8X8)
    SWIZZLE_ROW_FALLBACK(SurfaceFormat::B8G8R8X8, SurfaceFormat::R8G8B8A8)

    SWIZZLE_ROW_FALLBACK(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8A8)
    SWIZZLE_ROW_FALLBACK(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8X8)
    SWIZZLE_ROW_FALLBACK(SurfaceFormat::R8G8B8A8, SurfaceFormat::B8G8R8X8)
    SWIZZLE_ROW_FALLBACK(SurfaceFormat::R8G8B8X8, SurfaceFormat::B8G8R8A8)
    SWIZZLE_ROW_FALLBACK(SurfaceFormat::R8G8B8A8, SurfaceFormat::A8R8G8B8)
    SWIZZLE_ROW_FALLBACK(SurfaceFormat::R8G8B8X8, SurfaceFormat::X8R8G8B8)

    SWIZZLE_ROW_FALLBACK(SurfaceFormat::A8R8G8B8, SurfaceFormat::R8G8B8A8)
    SWIZZLE_ROW_FALLBACK(SurfaceFormat::X8R8G8B8, SurfaceFormat::R8G8B8X8)
    SWIZZLE_ROW_FALLBACK(SurfaceFormat::A8R8G8B8, SurfaceFormat::R8G8B8X8)
    SWIZZLE_ROW_FALLBACK(SurfaceFormat::X8R8G8B8, SurfaceFormat::R8G8B8A8)

    SWIZZLE_ROW_OPAQUE(SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8X8)
    SWIZZLE_ROW_OPAQUE(SurfaceFormat::B8G8R8X8, SurfaceFormat::B8G8R8A8)
    SWIZZLE_ROW_OPAQUE(SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8X8)
    SWIZZLE_ROW_OPAQUE(SurfaceFormat::R8G8B8X8, SurfaceFormat::R8G8B8A8)
    SWIZZLE_ROW_OPAQUE(SurfaceFormat::A8R8G8B8, SurfaceFormat::X8R8G8B8)
    SWIZZLE_ROW_OPAQUE(SurfaceFormat::X8R8G8B8, SurfaceFormat::A8R8G8B8)

    SWIZZLE_ROW_SWAP(SurfaceFormat::B8G8R8A8, SurfaceFormat::A8R8G8B8)
    SWIZZLE_ROW_SWAP(SurfaceFormat::B8G8R8A8, SurfaceFormat::X8R8G8B8)
    SWIZZLE_ROW_SWAP(SurfaceFormat::B8G8R8X8, SurfaceFormat::X8R8G8B8)
    SWIZZLE_ROW_SWAP(SurfaceFormat::B8G8R8X8, SurfaceFormat::A8R8G8B8)
    SWIZZLE_ROW_SWAP(SurfaceFormat::A8R8G8B8, SurfaceFormat::B8G8R8A8)
    SWIZZLE_ROW_SWAP(SurfaceFormat::A8R8G8B8, SurfaceFormat::B8G8R8X8)
    SWIZZLE_ROW_SWAP(SurfaceFormat::X8R8G8B8, SurfaceFormat::B8G8R8X8)
    SWIZZLE_ROW_SWAP(SurfaceFormat::X8R8G8B8, SurfaceFormat::B8G8R8A8)

    SWIZZLE_ROW_SWAP_RGB24(SurfaceFormat::R8G8B8, SurfaceFormat::B8G8R8)
    SWIZZLE_ROW_SWAP_RGB24(SurfaceFormat::B8G8R8, SurfaceFormat::R8G8B8)

    UNPACK_ROW_RGB(SurfaceFormat::R8G8B8X8)
    UNPACK_ROW_RGB(SurfaceFormat::R8G8B8A8)
    UNPACK_ROW_RGB(SurfaceFormat::B8G8R8X8)
    UNPACK_ROW_RGB(SurfaceFormat::B8G8R8A8)
    UNPACK_ROW_RGB_TO_ARGB(SurfaceFormat::A8R8G8B8)
    UNPACK_ROW_RGB_TO_ARGB(SurfaceFormat::X8R8G8B8)

    PACK_ROW_RGB(SurfaceFormat::R8G8B8, PackRowToRGB24)
    PACK_ROW_RGB(SurfaceFormat::B8G8R8, PackRowToRGB24)

    SWIZZLE_CMYK_ROW(SurfaceFormat::CMYK, SurfaceFormat::B8G8R8X8)
    SWIZZLE_CMYK_ROW(SurfaceFormat::CMYK, SurfaceFormat::B8G8R8A8)
    SWIZZLE_CMYK_ROW(SurfaceFormat::InvertedCMYK, SurfaceFormat::B8G8R8X8)
    SWIZZLE_CMYK_ROW(SurfaceFormat::InvertedCMYK, SurfaceFormat::B8G8R8A8)

    default:
      break;
  }

  if (aSrcFormat == aDstFormat) {
    switch (BytesPerPixel(aSrcFormat)) {
      case 4:
        return &SwizzleRowCopy<4>;
      case 3:
        return &SwizzleRowCopy<3>;
      default:
        break;
    }
  }

  MOZ_ASSERT_UNREACHABLE("Unsupported swizzle formats");
  return nullptr;
}

static IntRect ReorientRowRotate0FlipFallback(const uint8_t* aSrc,
                                              int32_t aSrcRow, uint8_t* aDst,
                                              const IntSize& aDstSize,
                                              int32_t aDstStride) {
  // Reverse order of pixels in the row.
  const uint32_t* src = reinterpret_cast<const uint32_t*>(aSrc);
  const uint32_t* end = src + aDstSize.width;
  uint32_t* dst = reinterpret_cast<uint32_t*>(aDst + aSrcRow * aDstStride) +
                  aDstSize.width - 1;
  do {
    *dst-- = *src++;
  } while (src < end);

  return IntRect(0, aSrcRow, aDstSize.width, 1);
}

static IntRect ReorientRowRotate90FlipFallback(const uint8_t* aSrc,
                                               int32_t aSrcRow, uint8_t* aDst,
                                               const IntSize& aDstSize,
                                               int32_t aDstStride) {
  // Copy row of pixels from top to bottom, into left to right columns.
  const uint32_t* src = reinterpret_cast<const uint32_t*>(aSrc);
  const uint32_t* end = src + aDstSize.height;
  uint32_t* dst = reinterpret_cast<uint32_t*>(aDst) + aSrcRow;
  int32_t stride = aDstStride / sizeof(uint32_t);
  do {
    *dst = *src++;
    dst += stride;
  } while (src < end);

  return IntRect(aSrcRow, 0, 1, aDstSize.height);
}

static IntRect ReorientRowRotate180FlipFallback(const uint8_t* aSrc,
                                                int32_t aSrcRow, uint8_t* aDst,
                                                const IntSize& aDstSize,
                                                int32_t aDstStride) {
  // Copy row of pixels from top to bottom, into bottom to top rows.
  uint8_t* dst = aDst + (aDstSize.height - aSrcRow - 1) * aDstStride;
  memcpy(dst, aSrc, aDstSize.width * sizeof(uint32_t));
  return IntRect(0, aDstSize.height - aSrcRow - 1, aDstSize.width, 1);
}

static IntRect ReorientRowRotate270FlipFallback(const uint8_t* aSrc,
                                                int32_t aSrcRow, uint8_t* aDst,
                                                const IntSize& aDstSize,
                                                int32_t aDstStride) {
  // Copy row of pixels in reverse order from top to bottom, into right to left
  // columns.
  const uint32_t* src = reinterpret_cast<const uint32_t*>(aSrc);
  const uint32_t* end = src + aDstSize.height;
  uint32_t* dst =
      reinterpret_cast<uint32_t*>(aDst + (aDstSize.height - 1) * aDstStride) +
      aDstSize.width - aSrcRow - 1;
  int32_t stride = aDstStride / sizeof(uint32_t);
  do {
    *dst = *src++;
    dst -= stride;
  } while (src < end);

  return IntRect(aDstSize.width - aSrcRow - 1, 0, 1, aDstSize.height);
}

static IntRect ReorientRowRotate0Fallback(const uint8_t* aSrc, int32_t aSrcRow,
                                          uint8_t* aDst,
                                          const IntSize& aDstSize,
                                          int32_t aDstStride) {
  // Copy row of pixels into the destination.
  uint8_t* dst = aDst + aSrcRow * aDstStride;
  memcpy(dst, aSrc, aDstSize.width * sizeof(uint32_t));
  return IntRect(0, aSrcRow, aDstSize.width, 1);
}

static IntRect ReorientRowRotate90Fallback(const uint8_t* aSrc, int32_t aSrcRow,
                                           uint8_t* aDst,
                                           const IntSize& aDstSize,
                                           int32_t aDstStride) {
  // Copy row of pixels from top to bottom, into right to left columns.
  const uint32_t* src = reinterpret_cast<const uint32_t*>(aSrc);
  const uint32_t* end = src + aDstSize.height;
  uint32_t* dst =
      reinterpret_cast<uint32_t*>(aDst) + aDstSize.width - aSrcRow - 1;
  int32_t stride = aDstStride / sizeof(uint32_t);
  do {
    *dst = *src++;
    dst += stride;
  } while (src < end);

  return IntRect(aDstSize.width - aSrcRow - 1, 0, 1, aDstSize.height);
}

static IntRect ReorientRowRotate180Fallback(const uint8_t* aSrc,
                                            int32_t aSrcRow, uint8_t* aDst,
                                            const IntSize& aDstSize,
                                            int32_t aDstStride) {
  // Copy row of pixels in reverse order from top to bottom, into bottom to top
  // rows.
  const uint32_t* src = reinterpret_cast<const uint32_t*>(aSrc);
  const uint32_t* end = src + aDstSize.width;
  uint32_t* dst = reinterpret_cast<uint32_t*>(
                      aDst + (aDstSize.height - aSrcRow - 1) * aDstStride) +
                  aDstSize.width - 1;
  do {
    *dst-- = *src++;
  } while (src < end);

  return IntRect(0, aDstSize.height - aSrcRow - 1, aDstSize.width, 1);
}

static IntRect ReorientRowRotate270Fallback(const uint8_t* aSrc,
                                            int32_t aSrcRow, uint8_t* aDst,
                                            const IntSize& aDstSize,
                                            int32_t aDstStride) {
  // Copy row of pixels in reverse order from top to bottom, into left to right
  // column.
  const uint32_t* src = reinterpret_cast<const uint32_t*>(aSrc);
  const uint32_t* end = src + aDstSize.height;
  uint32_t* dst =
      reinterpret_cast<uint32_t*>(aDst + (aDstSize.height - 1) * aDstStride) +
      aSrcRow;
  int32_t stride = aDstStride / sizeof(uint32_t);
  do {
    *dst = *src++;
    dst -= stride;
  } while (src < end);

  return IntRect(aSrcRow, 0, 1, aDstSize.height);
}

ReorientRowFn ReorientRow(const struct image::Orientation& aOrientation) {
  switch (aOrientation.flip) {
    case image::Flip::Unflipped:
      switch (aOrientation.rotation) {
        case image::Angle::D0:
          return &ReorientRowRotate0Fallback;
        case image::Angle::D90:
          return &ReorientRowRotate90Fallback;
        case image::Angle::D180:
          return &ReorientRowRotate180Fallback;
        case image::Angle::D270:
          return &ReorientRowRotate270Fallback;
        default:
          break;
      }
      break;
    case image::Flip::Horizontal:
      switch (aOrientation.rotation) {
        case image::Angle::D0:
          return &ReorientRowRotate0FlipFallback;
        case image::Angle::D90:
          if (aOrientation.flipFirst) {
            return &ReorientRowRotate270FlipFallback;
          } else {
            return &ReorientRowRotate90FlipFallback;
          }
        case image::Angle::D180:
          return &ReorientRowRotate180FlipFallback;
        case image::Angle::D270:
          if (aOrientation.flipFirst) {
            return &ReorientRowRotate90FlipFallback;
          } else {
            return &ReorientRowRotate270FlipFallback;
          }
        default:
          break;
      }
      break;
    default:
      break;
  }

  MOZ_ASSERT_UNREACHABLE("Unhandled orientation!");
  return nullptr;
}

void ConvertFloat16RowToUint16(const uint16_t* aSrc, uint16_t* aDst,
                               uint32_t aWidth, uint32_t aChannels) {
  uint32_t x = 0;
  // Process 4 pixels (16 channels) per iteration.
  for (; x + 4 <= aWidth; x += 4) {
    auto f32 = skvx::from_half(skvx::Vec<16, uint16_t>::Load(aSrc + x * 4));
    // Zero NaN lanes; ±inf is handled by the clamp below.
    auto bits = sk_bit_cast<skvx::Vec<16, int32_t>>(f32);
    f32 = sk_bit_cast<skvx::Vec<16, float>>(
        bits & ((bits & int32_t(0x7FFFFFFF)) <= int32_t(0x7F800000)));
    f32 = skvx::max(skvx::Vec<16, float>(0.0f),
                    skvx::min(f32, skvx::Vec<16, float>(1.0f)));
    auto u16 = skvx::cast<uint16_t>(f32 * 65535.0f + 0.5f);
    if (aChannels == 4) {
      u16.store(aDst + x * 4);
    } else {
      for (int p = 0; p < 4; p++) {
        aDst[(x + uint32_t(p)) * 3 + 0] = u16[p * 4 + 0];
        aDst[(x + uint32_t(p)) * 3 + 1] = u16[p * 4 + 1];
        aDst[(x + uint32_t(p)) * 3 + 2] = u16[p * 4 + 2];
      }
    }
  }
  // Remaining pixels.
  for (; x < aWidth; x++) {
    auto f32 = skvx::from_half(skvx::Vec<4, uint16_t>::Load(aSrc + x * 4));
    // Zero NaN lanes; ±inf is handled by the clamp below.
    auto bits = sk_bit_cast<skvx::Vec<4, int32_t>>(f32);
    f32 = sk_bit_cast<skvx::Vec<4, float>>(
        bits & ((bits & int32_t(0x7FFFFFFF)) <= int32_t(0x7F800000)));
    f32 = skvx::max(skvx::Vec<4, float>(0.0f),
                    skvx::min(f32, skvx::Vec<4, float>(1.0f)));
    auto u16 = skvx::cast<uint16_t>(f32 * 65535.0f + 0.5f);
    if (aChannels == 4) {
      u16.store(aDst + x * 4);
    } else {
      aDst[x * 3 + 0] = u16[0];
      aDst[x * 3 + 1] = u16[1];
      aDst[x * 3 + 2] = u16[2];
    }
  }
}

}  // namespace gfx
}  // namespace mozilla
