/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <bit>

#include "Orientation.h"
#include "gtest/MozGTestBench.h"
#include "gtest/gtest.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/gfx/Swizzle.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::image;

namespace mozilla::gfx {

// These are implementations of the row methods using SwizzleGeneric.h without
// any architecture specific specialization. This allows us to test the generics
// without any of the architecture specific template specializations and instead
// use only the pure generics. This is useful to test because if a new target
// doesn't use any specializations for the actual operations, we want to keep it
// working.
SwizzleRowFn PremultiplyRowGeneric(SurfaceFormat aSrcFormat,
                                   SurfaceFormat aDstFormat, SwizzleArch aArch);
SwizzleRowFn UnpremultiplyRowGeneric(SurfaceFormat aSrcFormat,
                                     SurfaceFormat aDstFormat,
                                     SwizzleArch aArch);
SwizzleRowFn SwizzleRowGeneric(SurfaceFormat aSrcFormat,
                               SurfaceFormat aDstFormat, SwizzleArch aArch);

}  // namespace mozilla::gfx

enum class SwizzleOp {
  Copy,
  YFlip,
  YFlipInplace,
  Premultiply,
  PremultiplyYFlip,
  PremultiplyYFlipInplace,
  Unpremultiply
};

// AVX2 is our widest at 32 bytes, or 8 pixels. Below we calculate the value
// needed to cover 3 full passes, including any remainders.
constexpr int32_t kMaxSweepPixels = 8 * 3;

// If the arch isn't available on this machine, the swizzle will just not
// happen, and then we will try the next one.
constexpr SwizzleArch kSwizzleArchs[] = {
    SwizzleArch::eAny,  SwizzleArch::eFallback, SwizzleArch::eGeneric,
#ifdef USE_SSE2
    SwizzleArch::eSSE2, SwizzleArch::eSSSE3,    SwizzleArch::eAVX2,
#endif
#ifdef USE_NEON
    SwizzleArch::eNEON,
#endif
};

static uint8_t RefPremultiply(uint8_t aColor, uint8_t aAlpha) {
  return (uint32_t(aColor) * aAlpha + 127) / 255;
}

static uint8_t RefUnpremultiply(uint8_t aColor, uint8_t aAlpha) {
  return aAlpha ? (uint32_t(aColor) * 255 + aAlpha / 2) / aAlpha : 0;
}

// Generate the expected pixel for testing with the given
// configuration/operation. It does this in the simplest way possible for manual
// verification of correctness.
static void GeneratePixel(SwizzleOp aOp, SurfaceFormat aDstFormat, uint8_t aB,
                          uint8_t aG, uint8_t aR, uint8_t aA, uint8_t* aDst) {
  uint8_t r = aR;
  uint8_t g = aG;
  uint8_t b = aB;
  switch (aOp) {
    default:
      break;
    case SwizzleOp::Premultiply:
    case SwizzleOp::PremultiplyYFlip:
    case SwizzleOp::PremultiplyYFlipInplace:
      r = RefPremultiply(aR, aA);
      g = RefPremultiply(aG, aA);
      b = RefPremultiply(aB, aA);
      break;
    case SwizzleOp::Unpremultiply:
      r = RefUnpremultiply(aR, aA);
      g = RefUnpremultiply(aG, aA);
      b = RefUnpremultiply(aB, aA);
      break;
  }

  switch (aDstFormat) {
    case SurfaceFormat::B8G8R8A8:
      aDst[0] = b;
      aDst[1] = g;
      aDst[2] = r;
      aDst[3] = aA;
      break;
    case SurfaceFormat::R8G8B8A8:
      aDst[0] = r;
      aDst[1] = g;
      aDst[2] = b;
      aDst[3] = aA;
      break;
    case SurfaceFormat::A8R8G8B8:
      aDst[0] = aA;
      aDst[1] = r;
      aDst[2] = g;
      aDst[3] = b;
      break;
    case SurfaceFormat::R8G8B8X8:
      aDst[0] = r;
      aDst[1] = g;
      aDst[2] = b;
      aDst[3] = 0xFF;
      break;
    case SurfaceFormat::B8G8R8X8:
      aDst[0] = b;
      aDst[1] = g;
      aDst[2] = r;
      aDst[3] = 0xFF;
      break;
    case SurfaceFormat::X8R8G8B8:
      aDst[0] = 0xFF;
      aDst[1] = r;
      aDst[2] = g;
      aDst[3] = b;
      break;
    default:
      ADD_FAILURE() << "Unhandled destination format";
      break;
  }
}

// Reference CMYK -> RGB conversion matching the scalar SwizzleCmykRowFallback:
// R = iC*iK/255, G = iM*iK/255, B = iY*iK/255 (truncating), with i meaning
// inverted. The result is packed via GeneratePixel so the destination byte
// order is handled consistently.
static void GenerateCmykPixel(bool aInverted, SurfaceFormat aDstFormat,
                              uint8_t aC, uint8_t aM, uint8_t aY, uint8_t aK,
                              uint8_t* aDst) {
  uint32_t iC = aC, iM = aM, iY = aY, iK = aK;

  // Invert if necessary, as the math expects inverted CMYK.
  if (!aInverted) {
    iC = 255 - iC;
    iM = 255 - iM;
    iY = 255 - iY;
    iK = 255 - iK;
  }
  uint8_t r = iC * iK / 255;
  uint8_t g = iM * iK / 255;
  uint8_t b = iY * iK / 255;
  GeneratePixel(SwizzleOp::Copy, aDstFormat, b, g, r, 0xFF, aDst);
}

// Generate a BGRA sample with a range of channel values. It can clamp the RGB
// values to ensure it is normal for unpremultiply.
static void FillTestBGRA(uint8_t* aDst, int32_t aWidth, bool aClampToAlpha) {
  for (int32_t i = 0; i < aWidth; ++i) {
    uint8_t a = (i % 17 == 0)   ? 0
                : (i % 13 == 0) ? 255
                                : uint8_t((i * 5 + 7) & 0xFF);
    uint8_t b = uint8_t((i * 7 + 1) & 0xFF);
    uint8_t g = uint8_t((i * 13 + 2) & 0xFF);
    uint8_t r = uint8_t((i * 29 + 3) & 0xFF);
    if (aClampToAlpha) {
      b = std::min(b, a);
      g = std::min(g, a);
      r = std::min(r, a);
    }
    aDst[i * 4 + 0] = b;
    aDst[i * 4 + 1] = g;
    aDst[i * 4 + 2] = r;
    aDst[i * 4 + 3] = a;
  }
}

static SwizzleRowFn RowFnFor(SwizzleOp aOp, SurfaceFormat aSrc,
                             SurfaceFormat aDst, SwizzleArch aArch) {
  switch (aOp) {
    case SwizzleOp::Premultiply:
      return PremultiplyRowGeneric(aSrc, aDst, aArch);
    case SwizzleOp::Unpremultiply:
      return UnpremultiplyRowGeneric(aSrc, aDst, aArch);
    case SwizzleOp::Copy:
      return SwizzleRowGeneric(aSrc, aDst, aArch);
    default:
      MOZ_ASSERT_UNREACHABLE("Unhandled row SwizzleOp!");
      break;
  }
  return nullptr;
}

// Verify the *Data methods exported for swizzling.
static void CheckBGRADataSweep(SwizzleOp aOp, SurfaceFormat aDstFormat) {
  constexpr int32_t kHeight = 3;
  constexpr int32_t kGapPixels = 5;
  constexpr int32_t kStride = (kMaxSweepPixels + kGapPixels) * 4;
  constexpr int32_t kSize = kStride * kHeight;
  uint8_t src[kSize];
  uint8_t dst[kSize];
  uint8_t expected[kSize];

  for (int32_t i = 0; i < kHeight; ++i) {
    uint8_t* rowStart = src + i * kStride;
    FillTestBGRA(rowStart, kMaxSweepPixels, aOp == SwizzleOp::Unpremultiply);
    memset(rowStart + kMaxSweepPixels * 4, 0xDF, kGapPixels * 4);
  }

  bool inplace = aOp == SwizzleOp::YFlipInplace ||
                 aOp == SwizzleOp::PremultiplyYFlipInplace;
  bool yflip =
      inplace || aOp == SwizzleOp::YFlip || aOp == SwizzleOp::PremultiplyYFlip;
  const uint8_t* srcOpPtr = inplace ? dst : src;

  for (SwizzleArch arch : kSwizzleArchs) {
    SCOPED_TRACE(testing::Message() << "arch=" << int(arch));
    // When we are inplace, there are pixels not just dummy values from src so
    // we need to copy src into expected first so that the unwritten pixels
    // still match.
    if (inplace) {
      memcpy(expected, src, sizeof(expected));
    } else {
      memset(expected, 0xCD, sizeof(expected));
    }

    for (int32_t i = 0; i < kMaxSweepPixels; ++i) {
      for (int32_t k = 0; k < kHeight; ++k) {
        int32_t srcPos = k * kStride + i * 4;
        int32_t dstPos = yflip ? (kHeight - k - 1) * kStride + i * 4 : srcPos;
        GeneratePixel(aOp, aDstFormat, src[srcPos], src[srcPos + 1],
                      src[srcPos + 2], src[srcPos + 3], expected + dstPos);
      }

      int32_t width = i + 1;

      if (inplace) {
        memcpy(dst, src, sizeof(dst));
      } else {
        memset(dst, 0xCD, sizeof(dst));
      }

      bool success = false;
      switch (aOp) {
        case SwizzleOp::Copy:
          success =
              SwizzleData(srcOpPtr, kStride, SurfaceFormat::B8G8R8A8, dst,
                          kStride, aDstFormat, IntSize(width, kHeight), arch);
          break;
        case SwizzleOp::YFlip:
        case SwizzleOp::YFlipInplace:
          success = SwizzleYFlipData(srcOpPtr, kStride, SurfaceFormat::B8G8R8A8,
                                     dst, kStride, aDstFormat,
                                     IntSize(width, kHeight), arch);
          break;
        case SwizzleOp::Premultiply:
          success = PremultiplyData(srcOpPtr, kStride, SurfaceFormat::B8G8R8A8,
                                    dst, kStride, aDstFormat,
                                    IntSize(width, kHeight), arch);
          break;
        case SwizzleOp::PremultiplyYFlip:
        case SwizzleOp::PremultiplyYFlipInplace:
          success = PremultiplyYFlipData(
              srcOpPtr, kStride, SurfaceFormat::B8G8R8A8, dst, kStride,
              aDstFormat, IntSize(width, kHeight), arch);
          break;
        case SwizzleOp::Unpremultiply:
          success = UnpremultiplyData(
              srcOpPtr, kStride, SurfaceFormat::B8G8R8A8, dst, kStride,
              aDstFormat, IntSize(width, kHeight), arch);
          break;
        default:
          MOZ_ASSERT_UNREACHABLE("Unhandled SwizzleOp!");
          break;
      }

      // We can fail if the arch isn't supported for this operation, but
      // any/fallback should always succeed.
      if (!success) {
        EXPECT_NE(arch, SwizzleArch::eAny);
        EXPECT_NE(arch, SwizzleArch::eFallback);
        break;
      }

      EXPECT_TRUE(ArrayEqual(dst, expected))
          << (inplace ? "same buffer" : "separate buffers") << ", width "
          << width;
    }
  }
}

// Verify the *Row methods exported for swizzling.
static void CheckBGRARowSweep(SwizzleOp aOp, SurfaceFormat aDstFormat) {
  constexpr int32_t kSize = kMaxSweepPixels * 4;
  uint8_t src[kSize];
  uint8_t dst[kSize];
  uint8_t expected[kSize];
  FillTestBGRA(src, kMaxSweepPixels, aOp == SwizzleOp::Unpremultiply);

  for (SwizzleArch arch : kSwizzleArchs) {
    SCOPED_TRACE(testing::Message()
                 << "dstFormat=" << int(aDstFormat) << " arch=" << int(arch));

    // We can fail if the arch isn't supported for this operation, but
    // any/fallback should always succeed.
    SwizzleRowFn func =
        RowFnFor(aOp, SurfaceFormat::B8G8R8A8, aDstFormat, arch);
    if (!func) {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
      continue;
    }

    memset(expected, 0xCD, sizeof(expected));

    for (int32_t i = 0; i < kMaxSweepPixels; ++i) {
      GeneratePixel(aOp, aDstFormat, src[i * 4 + 0], src[i * 4 + 1],
                    src[i * 4 + 2], src[i * 4 + 3], &expected[i * 4]);

      int32_t len = i + 1;
      memset(dst, 0xCD, sizeof(dst));
      func(src, dst, len);
      EXPECT_TRUE(ArrayEqual(dst, expected))
          << "separate buffers, length " << len;

      memcpy(dst, src, len * 4);
      func(dst, dst, len);
      EXPECT_TRUE(ArrayEqual(dst, expected)) << "in place, length " << len;
    }
  }
}

// Verify the SwizzleRow methods exported for unpacking RGB, which has
// different sizes and so needs a slightly different algorithm for generating
// the inputs.
static void CheckUnpackRowSweep(SurfaceFormat aDstFormat) {
  constexpr int32_t kSrcSize = kMaxSweepPixels * 3;
  constexpr int32_t kDstSize = kMaxSweepPixels * 4;
  uint8_t src[kSrcSize];
  uint8_t dst[kDstSize];
  uint8_t expected[kDstSize];
  for (int32_t i = 0; i < kSrcSize; ++i) {
    src[i] = uint8_t((i * 11 + 5) & 0xFF);
  }

  for (SwizzleArch arch : kSwizzleArchs) {
    SCOPED_TRACE(testing::Message()
                 << "dstFormat=" << int(aDstFormat) << " arch=" << int(arch));

    // We can fail if the arch isn't supported for this operation, but
    // any/fallback should always succeed.
    SwizzleRowFn func =
        RowFnFor(SwizzleOp::Copy, SurfaceFormat::R8G8B8, aDstFormat, arch);
    if (!func) {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
      continue;
    }

    memset(expected, 0xCD, sizeof(expected));

    for (int32_t i = 0; i < kMaxSweepPixels; ++i) {
      uint8_t r = src[i * 3 + 0];
      uint8_t g = src[i * 3 + 1];
      uint8_t b = src[i * 3 + 2];
      GeneratePixel(SwizzleOp::Copy, aDstFormat, b, g, r, 0xFF,
                    &expected[i * 4]);

      int32_t len = i + 1;
      memset(dst, 0xCD, sizeof(dst));
      func(src, dst, len);
      EXPECT_TRUE(ArrayEqual(dst, expected))
          << "unpack separate buffers, length " << len;

      memcpy(dst, src, len * 3);
      func(dst, dst, len);
      EXPECT_TRUE(ArrayEqual(dst, expected))
          << "unpack in place, length " << len;
    }
  }
}

// Verify the *Row methods exported for swizzling CMYK.
static void CheckCmykRowSweep(SurfaceFormat aSrcFormat) {
  constexpr int32_t kSize = kMaxSweepPixels * 4;
  uint8_t src[kSize];
  uint8_t dst[kSize];
  uint8_t expected[kSize];
  // Spread of C/M/Y/K values across the row. The multiplier and addition are
  // arbitrary values intended to select for values across the spectrum.
  for (int32_t i = 0; i < kSize; ++i) {
    src[i] = uint8_t((i * 37 + 11) & 0xFF);
  }
  const bool inverted = aSrcFormat == SurfaceFormat::InvertedCMYK;

  for (SwizzleArch arch : kSwizzleArchs) {
    SCOPED_TRACE(testing::Message()
                 << "srcFormat=" << int(aSrcFormat) << " arch=" << int(arch));

    SwizzleRowFn func =
        RowFnFor(SwizzleOp::Copy, aSrcFormat, SurfaceFormat::B8G8R8X8, arch);
    if (!func) {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
      continue;
    }

    memset(expected, 0xCD, sizeof(expected));

    for (int32_t i = 0; i < kMaxSweepPixels; ++i) {
      GenerateCmykPixel(inverted, SurfaceFormat::B8G8R8X8, src[i * 4 + 0],
                        src[i * 4 + 1], src[i * 4 + 2], src[i * 4 + 3],
                        &expected[i * 4]);

      int32_t len = i + 1;
      memset(dst, 0xCD, sizeof(dst));
      func(src, dst, len);
      EXPECT_TRUE(ArrayEqual(dst, expected))
          << "separate buffers, length " << len;

      memcpy(dst, src, len * 4);
      func(dst, dst, len);
      EXPECT_TRUE(ArrayEqual(dst, expected)) << "in place, length " << len;
    }
  }
}

TEST(Moz2D, PremultiplyData)
{
  const uint8_t in_bgra[5 * 4] = {
      255, 255, 0,   255,  // verify 255 alpha leaves RGB unchanged
      0,   0,   255, 255,
      0,   255, 255, 0,  // verify 0 alpha zeroes out RGB
      0,   0,   0,   0,
      255, 0,   0,   128,  // verify that 255 RGB maps to alpha
  };
  uint8_t out[5 * 4];
  const uint8_t check_bgra[5 * 4] = {
      255, 255, 0, 255, 0, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 128, 0, 0, 128,
  };
  // check swizzled output
  const uint8_t check_rgba[5 * 4] = {
      0, 255, 255, 255, 255, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 128,
  };
  const uint8_t check_argb[5 * 4] = {
      255, 0, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 0, 0, 128,
  };

  PremultiplyData(in_bgra, sizeof(in_bgra), SurfaceFormat::B8G8R8A8, out,
                  sizeof(in_bgra), SurfaceFormat::B8G8R8A8, IntSize(5, 1));
  EXPECT_TRUE(ArrayEqual(out, check_bgra));

  PremultiplyData(in_bgra, sizeof(in_bgra), SurfaceFormat::B8G8R8A8, out,
                  sizeof(in_bgra), SurfaceFormat::R8G8B8A8, IntSize(5, 1));
  EXPECT_TRUE(ArrayEqual(out, check_rgba));

  PremultiplyData(in_bgra, sizeof(in_bgra), SurfaceFormat::B8G8R8A8, out,
                  sizeof(in_bgra), SurfaceFormat::A8R8G8B8, IntSize(5, 1));
  EXPECT_TRUE(ArrayEqual(out, check_argb));
}

TEST(Moz2D, PremultiplyRow)
{
  const uint8_t in_bgra[5 * 4] = {
      255, 255, 0,   255,  // verify 255 alpha leaves RGB unchanged
      0,   0,   255, 255,
      0,   255, 255, 0,  // verify 0 alpha zeroes out RGB
      0,   0,   0,   0,
      255, 0,   0,   128,  // verify that 255 RGB maps to alpha
  };
  uint8_t out[5 * 4];
  const uint8_t check_bgra[5 * 4] = {
      255, 255, 0, 255, 0, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 128, 0, 0, 128,
  };
  // check swizzled output
  const uint8_t check_rgba[5 * 4] = {
      0, 255, 255, 255, 255, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 128,
  };
  const uint8_t check_argb[5 * 4] = {
      255, 0, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 0, 0, 128,
  };

  for (auto arch : kSwizzleArchs) {
    SwizzleRowFn func =
        RowFnFor(SwizzleOp::Premultiply, SurfaceFormat::B8G8R8A8,
                 SurfaceFormat::B8G8R8A8, arch);
    if (func) {
      func(in_bgra, out, 5);
      EXPECT_TRUE(ArrayEqual(out, check_bgra));
    } else {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
    }

    func = RowFnFor(SwizzleOp::Premultiply, SurfaceFormat::B8G8R8A8,
                    SurfaceFormat::R8G8B8A8, arch);
    if (func) {
      func(in_bgra, out, 5);
      EXPECT_TRUE(ArrayEqual(out, check_rgba));
    } else {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
    }

    func = RowFnFor(SwizzleOp::Premultiply, SurfaceFormat::B8G8R8A8,
                    SurfaceFormat::A8R8G8B8, arch);
    if (func) {
      func(in_bgra, out, 5);
      EXPECT_TRUE(ArrayEqual(out, check_argb));
    } else {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
    }
  }
}

TEST(Moz2D, PremultiplyYFlipData)
{
  const uint8_t stride = 2 * 4;
  const uint8_t in_bgra[6 * 4] = {
      255, 255, 0,   255,  // row 1: verify 255 alpha leaves RGB unchanged
      0,   0,   255, 255,
      0,   255, 255, 0,  // row 2: verify 0 alpha zeroes out RGB
      0,   0,   0,   0,
      255, 0,   0,   128,  // row 3: verify that 255 RGB maps to alpha
      255, 255, 255, 128,
  };
  const uint8_t in_bgra_2[4 * 4] = {
      255, 255, 0,   255,  // row 1: verify 255 alpha leaves RGB unchanged
      0,   0,   255, 255,
      0,   255, 255, 0,  // row 2: verify 0 alpha zeroes out RGB
      0,   0,   0,   0,
  };
  const uint8_t in_bgra_3[2 * 4] = {
      255, 0,   0,   128,  // row 1: verify that 255 RGB maps to alpha
      255, 255, 255, 128,
  };
  uint8_t out[6 * 4];
  uint8_t out_2[4 * 4];
  uint8_t out_3[2 * 4];
  const uint8_t check_bgra[6 * 4] = {
      128, 0, 0, 128, 128, 128, 128, 128, 0, 0, 0,   0,
      0,   0, 0, 0,   255, 255, 0,   255, 0, 0, 255, 255,
  };
  const uint8_t check_bgra_2[4 * 4] = {
      0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 0, 255, 0, 0, 255, 255,
  };
  const uint8_t check_bgra_3[2 * 4] = {
      128, 0, 0, 128, 128, 128, 128, 128,
  };
  // check swizzled output
  const uint8_t check_rgba[6 * 4] = {
      0, 0, 128, 128, 128, 128, 128, 128, 0,   0, 0, 0,
      0, 0, 0,   0,   0,   255, 255, 255, 255, 0, 0, 255,
  };

  // Premultiply.
  PremultiplyYFlipData(in_bgra, stride, SurfaceFormat::B8G8R8A8, out, stride,
                       SurfaceFormat::B8G8R8A8, IntSize(2, 3));
  EXPECT_TRUE(ArrayEqual(out, check_bgra));

  // Premultiply in-place with middle row.
  memcpy(out, in_bgra, sizeof(out));
  PremultiplyYFlipData(out, stride, SurfaceFormat::B8G8R8A8, out, stride,
                       SurfaceFormat::B8G8R8A8, IntSize(2, 3));
  EXPECT_TRUE(ArrayEqual(out, check_bgra));

  // Premultiply in-place without middle row.
  memcpy(out_2, in_bgra_2, sizeof(out_2));
  PremultiplyYFlipData(out_2, stride, SurfaceFormat::B8G8R8A8, out_2, stride,
                       SurfaceFormat::B8G8R8A8, IntSize(2, 2));
  EXPECT_TRUE(ArrayEqual(out_2, check_bgra_2));

  // Premultiply in-place only middle row.
  memcpy(out_3, in_bgra_3, sizeof(out_3));
  PremultiplyYFlipData(out_3, stride, SurfaceFormat::B8G8R8A8, out_3, stride,
                       SurfaceFormat::B8G8R8A8, IntSize(2, 1));
  EXPECT_TRUE(ArrayEqual(out_3, check_bgra_3));

  // Premultiply and swizzle with middle row.
  PremultiplyYFlipData(in_bgra, stride, SurfaceFormat::B8G8R8A8, out, stride,
                       SurfaceFormat::R8G8B8A8, IntSize(2, 3));
  EXPECT_TRUE(ArrayEqual(out, check_rgba));
}

TEST(Moz2D, UnpremultiplyData)
{
  const uint8_t in_bgra[5 * 4] = {
      255, 255, 0,   255,              // verify 255 alpha leaves RGB unchanged
      0,   0,   255, 255, 0, 0, 0, 0,  // verify 0 alpha leaves RGB at 0
      0,   0,   0,   64,   // verify 0 RGB stays 0 with non-zero alpha
      128, 0,   0,   128,  // verify that RGB == alpha maps to 255

  };
  uint8_t out[5 * 4];
  const uint8_t check_bgra[5 * 4] = {
      255, 255, 0, 255, 0, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 64, 255, 0, 0, 128,
  };
  // check swizzled output
  const uint8_t check_rgba[5 * 4] = {
      0, 255, 255, 255, 255, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 64, 0, 0, 255, 128,
  };
  const uint8_t check_argb[5 * 4] = {
      255, 0, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 64, 0, 0, 0, 128, 0, 0, 255,
  };

  UnpremultiplyData(in_bgra, sizeof(in_bgra), SurfaceFormat::B8G8R8A8, out,
                    sizeof(in_bgra), SurfaceFormat::B8G8R8A8, IntSize(5, 1));
  EXPECT_TRUE(ArrayEqual(out, check_bgra));

  UnpremultiplyData(in_bgra, sizeof(in_bgra), SurfaceFormat::B8G8R8A8, out,
                    sizeof(in_bgra), SurfaceFormat::R8G8B8A8, IntSize(5, 1));
  EXPECT_TRUE(ArrayEqual(out, check_rgba));

  UnpremultiplyData(in_bgra, sizeof(in_bgra), SurfaceFormat::B8G8R8A8, out,
                    sizeof(in_bgra), SurfaceFormat::A8R8G8B8, IntSize(5, 1));
  EXPECT_TRUE(ArrayEqual(out, check_argb));
}

TEST(Moz2D, UnpremultiplyRow)
{
  const uint8_t in_bgra[5 * 4] = {
      255, 255, 0,   255,              // verify 255 alpha leaves RGB unchanged
      0,   0,   255, 255, 0, 0, 0, 0,  // verify 0 alpha leaves RGB at 0
      0,   0,   0,   64,   // verify 0 RGB stays 0 with non-zero alpha
      128, 0,   0,   128,  // verify that RGB == alpha maps to 255

  };
  uint8_t out[5 * 4];
  const uint8_t check_bgra[5 * 4] = {
      255, 255, 0, 255, 0, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 64, 255, 0, 0, 128,
  };
  // check swizzled output
  const uint8_t check_rgba[5 * 4] = {
      0, 255, 255, 255, 255, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 64, 0, 0, 255, 128,
  };
  const uint8_t check_argb[5 * 4] = {
      255, 0, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 64, 0, 0, 0, 128, 0, 0, 255,
  };

  for (auto arch : kSwizzleArchs) {
    SwizzleRowFn func =
        RowFnFor(SwizzleOp::Unpremultiply, SurfaceFormat::B8G8R8A8,
                 SurfaceFormat::B8G8R8A8, arch);
    if (func) {
      func(in_bgra, out, 5);
      EXPECT_TRUE(ArrayEqual(out, check_bgra));
    } else {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
    }

    func = RowFnFor(SwizzleOp::Unpremultiply, SurfaceFormat::B8G8R8A8,
                    SurfaceFormat::R8G8B8A8, arch);
    if (func) {
      func(in_bgra, out, 5);
      EXPECT_TRUE(ArrayEqual(out, check_rgba));
    } else {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
    }

    func = RowFnFor(SwizzleOp::Unpremultiply, SurfaceFormat::B8G8R8A8,
                    SurfaceFormat::A8R8G8B8, arch);
    if (func) {
      func(in_bgra, out, 5);
      EXPECT_TRUE(ArrayEqual(out, check_argb));
    } else {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
    }
  }
}

TEST(Moz2D, AlphaConversionRounding)
{
  constexpr int32_t width = 257;
  constexpr int32_t stride = width * 4;
  uint8_t src[stride], dst[stride], expected[stride], restored[stride];
  uint8_t packed[stride];
  const SurfaceFormat formats[] = {
      SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8,
      SurfaceFormat::A8R8G8B8, SurfaceFormat::B8G8R8X8,
      SurfaceFormat::R8G8B8X8, SurfaceFormat::X8R8G8B8};

  for (auto op : {SwizzleOp::Premultiply, SwizzleOp::Unpremultiply}) {
    for (uint32_t alpha = 0; alpha <= 255; ++alpha) {
      for (int32_t x = 0; x < width; ++x) {
        uint32_t limit = op == SwizzleOp::Premultiply ? 256 : alpha + 1;
        src[4 * x] = (x & 255) % limit;
        src[4 * x + 1] = (255 - (x & 255)) % limit;
        src[4 * x + 2] = ((x & 255) ^ 0x55) % limit;
        src[4 * x + 3] = alpha;
      }
      for (auto sourceFormat :
           {SurfaceFormat::B8G8R8A8, SurfaceFormat::R8G8B8A8,
            SurfaceFormat::A8R8G8B8}) {
        SCOPED_TRACE(testing::Message() << "source=" << int(sourceFormat));
        for (int32_t x = 0; x < width; ++x) {
          GeneratePixel(SwizzleOp::Copy, sourceFormat, src[4 * x],
                        src[4 * x + 1], src[4 * x + 2], alpha, packed + 4 * x);
        }
        for (auto format : formats) {
          if (op == SwizzleOp::Unpremultiply &&
              (format == SurfaceFormat::B8G8R8X8 ||
               format == SurfaceFormat::R8G8B8X8 ||
               format == SurfaceFormat::X8R8G8B8)) {
            continue;
          }
          for (int32_t x = 0; x < width; ++x) {
            GeneratePixel(op, format, src[4 * x], src[4 * x + 1],
                          src[4 * x + 2], alpha, expected + 4 * x);
          }
          for (auto arch : kSwizzleArchs) {
            SCOPED_TRACE(testing::Message()
                         << "op=" << int(op) << " alpha=" << alpha
                         << " format=" << int(format) << " arch=" << int(arch));
            auto row = RowFnFor(op, sourceFormat, format, arch);
            if (!row) {
              EXPECT_NE(arch, SwizzleArch::eAny);
              EXPECT_NE(arch, SwizzleArch::eFallback);
              continue;
            }
            row(packed, dst, width);
            ASSERT_TRUE(ArrayEqual(dst, expected));
            memcpy(dst, packed, stride);
            row(dst, dst, width);
            ASSERT_TRUE(ArrayEqual(dst, expected));
            bool converted =
                op == SwizzleOp::Premultiply
                    ? PremultiplyData(packed, stride, sourceFormat, dst, stride,
                                      format, IntSize(width, 1), arch)
                    : UnpremultiplyData(packed, stride, sourceFormat, dst,
                                        stride, format, IntSize(width, 1),
                                        arch);
            if (converted) {
              ASSERT_TRUE(ArrayEqual(dst, expected));
            } else {
              EXPECT_NE(arch, SwizzleArch::eAny);
              EXPECT_NE(arch, SwizzleArch::eFallback);
            }
            if (op == SwizzleOp::Unpremultiply) {
              auto inverse =
                  RowFnFor(SwizzleOp::Premultiply, format, sourceFormat, arch);
              if (inverse) {
                inverse(dst, restored, width);
                ASSERT_TRUE(ArrayEqual(restored, packed));
              }
            }
          }
        }
      }
    }
  }
}

TEST(Moz2D, SwizzleData)
{
  const uint8_t in_bgra[5 * 4] = {
      253, 254, 0, 255, 0, 0, 255, 255, 0, 0, 0, 0, 1, 2, 3, 64, 127, 0, 9, 128,

  };
  uint8_t out[5 * 4];
  // check copy
  const uint8_t check_bgra[5 * 4] = {
      253, 254, 0, 255, 0, 0, 255, 255, 0, 0, 0, 0, 1, 2, 3, 64, 127, 0, 9, 128,
  };
  // check swaps
  const uint8_t check_rgba[5 * 4] = {
      0, 254, 253, 255, 255, 0, 0, 255, 0, 0, 0, 0, 3, 2, 1, 64, 9, 0, 127, 128,
  };
  const uint8_t check_argb[5 * 4] = {
      255, 0, 254, 253, 255, 255, 0, 0, 0, 0, 0, 0, 64, 3, 2, 1, 128, 9, 0, 127,
  };
  // check opaquifying
  const uint8_t check_rgbx[5 * 4] = {
      0, 254, 253, 255, 255, 0,   0, 255, 0,   0,
      0, 255, 3,   2,   1,   255, 9, 0,   127, 255,
  };
  // check opaquifying into alpha-first order
  const uint8_t check_xrgb[5 * 4] = {
      255, 0, 254, 253, 255, 255, 0,   0, 255, 0,
      0,   0, 255, 3,   2,   1,   255, 9, 0,   127,
  };
  // check packing
  uint8_t out24[5 * 3];
  const uint8_t check_bgr[5 * 3] = {253, 254, 0, 0, 0,   255, 0, 0,
                                    0,   1,   2, 3, 127, 0,   9};
  const uint8_t check_rgb[5 * 3] = {
      0, 254, 253, 255, 0, 0, 0, 0, 0, 3, 2, 1, 9, 0, 127,
  };
  uint8_t out8[5];
  const uint8_t check_a[5] = {255, 255, 0, 64, 128};
  uint16_t out16[5];
#define PACK_RGB565(b, g, r) \
  (((b & 0xF8) >> 3) | ((g & 0xFC) << 3) | ((r & 0xF8) << 8))
  const uint16_t check_16[5] = {
      PACK_RGB565(253, 254, 0), PACK_RGB565(0, 0, 255), PACK_RGB565(0, 0, 0),
      PACK_RGB565(1, 2, 3),     PACK_RGB565(127, 0, 9),
  };

  EXPECT_TRUE(SwizzleData(in_bgra, sizeof(in_bgra), SurfaceFormat::B8G8R8A8,
                          out, sizeof(out), SurfaceFormat::B8G8R8A8,
                          IntSize(5, 1)));
  EXPECT_TRUE(ArrayEqual(out, check_bgra));

  EXPECT_TRUE(SwizzleData(in_bgra, sizeof(in_bgra), SurfaceFormat::B8G8R8A8,
                          out, sizeof(out), SurfaceFormat::R8G8B8A8,
                          IntSize(5, 1)));
  EXPECT_TRUE(ArrayEqual(out, check_rgba));

  EXPECT_TRUE(SwizzleData(in_bgra, sizeof(in_bgra), SurfaceFormat::B8G8R8A8,
                          out, sizeof(out), SurfaceFormat::A8R8G8B8,
                          IntSize(5, 1)));
  EXPECT_TRUE(ArrayEqual(out, check_argb));

  EXPECT_TRUE(SwizzleData(in_bgra, sizeof(in_bgra), SurfaceFormat::B8G8R8A8,
                          out, sizeof(out), SurfaceFormat::R8G8B8X8,
                          IntSize(5, 1)));
  EXPECT_TRUE(ArrayEqual(out, check_rgbx));

  EXPECT_TRUE(SwizzleData(check_rgba, sizeof(check_rgba),
                          SurfaceFormat::R8G8B8X8, out, sizeof(out),
                          SurfaceFormat::A8R8G8B8, IntSize(5, 1)));
  EXPECT_TRUE(ArrayEqual(out, check_xrgb));

  EXPECT_TRUE(SwizzleData(check_rgba, sizeof(check_rgba),
                          SurfaceFormat::R8G8B8A8, out, sizeof(out),
                          SurfaceFormat::X8R8G8B8, IntSize(5, 1)));
  EXPECT_TRUE(ArrayEqual(out, check_xrgb));

  EXPECT_TRUE(SwizzleData(in_bgra, sizeof(in_bgra), SurfaceFormat::B8G8R8A8,
                          out24, sizeof(out24), SurfaceFormat::B8G8R8,
                          IntSize(5, 1)));
  EXPECT_TRUE(ArrayEqual(out24, check_bgr));

  EXPECT_TRUE(SwizzleData(in_bgra, sizeof(in_bgra), SurfaceFormat::B8G8R8A8,
                          out24, sizeof(out24), SurfaceFormat::R8G8B8,
                          IntSize(5, 1)));
  EXPECT_TRUE(ArrayEqual(out24, check_rgb));

  EXPECT_TRUE(SwizzleData(in_bgra, sizeof(in_bgra), SurfaceFormat::B8G8R8A8,
                          out8, sizeof(out8), SurfaceFormat::A8,
                          IntSize(5, 1)));
  EXPECT_TRUE(ArrayEqual(out8, check_a));

  const uint8_t* uint32_argb;

  if constexpr (std::endian::native == std::endian::big) {
    EXPECT_EQ(SurfaceFormat::A8R8G8B8_UINT32, SurfaceFormat::A8R8G8B8);
    uint32_argb = check_argb;
  } else {
    EXPECT_EQ(SurfaceFormat::A8R8G8B8_UINT32, SurfaceFormat::B8G8R8A8);
    uint32_argb = check_bgra;
  }

  EXPECT_TRUE(SwizzleData(uint32_argb, sizeof(in_bgra),
                          SurfaceFormat::A8R8G8B8_UINT32,
                          reinterpret_cast<uint8_t*>(out16), sizeof(out16),
                          SurfaceFormat::R5G6B5_UINT16, IntSize(5, 1)));
  EXPECT_TRUE(ArrayEqual(out16, check_16));
}

TEST(Moz2D, SwizzleYFlipData)
{
  const uint8_t stride = 2 * 4;
  const uint8_t in_bgra[6 * 4] = {
      255, 255, 0,   255,                      // row 1
      0,   0,   255, 255, 0,   255, 255, 0,    // row 2
      0,   0,   0,   0,   255, 0,   0,   128,  // row 3
      255, 255, 255, 128,
  };
  const uint8_t in_bgra_2[4 * 4] = {
      255, 255, 0,   255,                  // row 1
      0,   0,   255, 255, 0, 255, 255, 0,  // row 2
      0,   0,   0,   0,
  };
  const uint8_t in_bgra_3[2 * 4] = {
      255, 0,   0,   128,  // row 1
      255, 255, 255, 128,
  };
  uint8_t out[6 * 4];
  uint8_t out_2[4 * 4];
  uint8_t out_3[2 * 4];
  const uint8_t check_rgba[6 * 4] = {
      0, 0, 255, 128, 255, 255, 255, 128, 255, 255, 0, 0,
      0, 0, 0,   0,   0,   255, 255, 255, 255, 0,   0, 255,
  };
  const uint8_t check_rgba_2[4 * 4] = {
      255, 255, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 255,
  };
  const uint8_t check_rgba_3[2 * 4] = {
      0, 0, 255, 128, 255, 255, 255, 128,
  };

  // Swizzle.
  SwizzleYFlipData(in_bgra, stride, SurfaceFormat::B8G8R8A8, out, stride,
                   SurfaceFormat::R8G8B8A8, IntSize(2, 3));
  EXPECT_TRUE(ArrayEqual(out, check_rgba));

  // Swizzle in-place with middle row.
  memcpy(out, in_bgra, sizeof(out));
  SwizzleYFlipData(out, stride, SurfaceFormat::B8G8R8A8, out, stride,
                   SurfaceFormat::R8G8B8A8, IntSize(2, 3));
  EXPECT_TRUE(ArrayEqual(out, check_rgba));

  // Swizzle in-place without middle row.
  memcpy(out_2, in_bgra_2, sizeof(out_2));
  SwizzleYFlipData(out_2, stride, SurfaceFormat::B8G8R8A8, out_2, stride,
                   SurfaceFormat::R8G8B8A8, IntSize(2, 2));
  EXPECT_TRUE(ArrayEqual(out_2, check_rgba_2));

  // Swizzle in-place only middle row.
  memcpy(out_3, in_bgra_3, sizeof(out_3));
  SwizzleYFlipData(out_3, stride, SurfaceFormat::B8G8R8A8, out_3, stride,
                   SurfaceFormat::R8G8B8A8, IntSize(2, 1));
  EXPECT_TRUE(ArrayEqual(out_3, check_rgba_3));
}

TEST(Moz2D, SwizzleRow)
{
  const uint8_t in_bgra[5 * 4] = {
      253, 254, 0, 255, 0, 0, 255, 255, 0, 0, 0, 0, 1, 2, 3, 64, 127, 0, 9, 128,

  };
  uint8_t out[5 * 4];
  // check swaps
  const uint8_t check_rgba[5 * 4] = {
      0, 254, 253, 255, 255, 0, 0, 255, 0, 0, 0, 0, 3, 2, 1, 64, 9, 0, 127, 128,
  };
  // check opaquifying
  const uint8_t check_rgbx[5 * 4] = {
      0, 254, 253, 255, 255, 0,   0, 255, 0,   0,
      0, 255, 3,   2,   1,   255, 9, 0,   127, 255,
  };
  // check opaquifying into alpha-first order
  const uint8_t check_xrgb[5 * 4] = {
      255, 0, 254, 253, 255, 255, 0,   0, 255, 0,
      0,   0, 255, 3,   2,   1,   255, 9, 0,   127,
  };
  // check packing
  uint8_t out24[5 * 3];
  const uint8_t check_bgr[5 * 3] = {253, 254, 0, 0, 0,   255, 0, 0,
                                    0,   1,   2, 3, 127, 0,   9};
  const uint8_t check_rgb[5 * 3] = {
      0, 254, 253, 255, 0, 0, 0, 0, 0, 3, 2, 1, 9, 0, 127,
  };
  // check unpacking
  uint8_t out_unpack[16 * 4];
  const uint8_t in_rgb[16 * 3] = {
      0,  254, 253, 255, 0,  0,  0,  0,  0,  3,  2,  1,  9,  0,  127, 4,
      5,  6,   9,   8,   7,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19,  20,
      21, 22,  23,  24,  25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,  36,
  };
  const uint8_t check_unpack_rgbx[16 * 4] = {
      0,  254, 253, 255, 255, 0,  0,  255, 0,  0,  0,  255, 3,  2,  1,  255,
      9,  0,   127, 255, 4,   5,  6,  255, 9,  8,  7,  255, 10, 11, 12, 255,
      13, 14,  15,  255, 16,  17, 18, 255, 19, 20, 21, 255, 22, 23, 24, 255,
      25, 26,  27,  255, 28,  29, 30, 255, 31, 32, 33, 255, 34, 35, 36, 255,
  };
  const uint8_t check_unpack_bgrx[16 * 4] = {
      253, 254, 0,  255, 0,  0,  255, 255, 0,  0,  0,  255, 1,  2,  3,  255,
      127, 0,   9,  255, 6,  5,  4,   255, 7,  8,  9,  255, 12, 11, 10, 255,
      15,  14,  13, 255, 18, 17, 16,  255, 21, 20, 19, 255, 24, 23, 22, 255,
      27,  26,  25, 255, 30, 29, 28,  255, 33, 32, 31, 255, 36, 35, 34, 255,
  };
  const uint8_t check_unpack_xrgb[16 * 4] = {
      255, 0,  254, 253, 255, 255, 0,  0,  255, 0,  0,  0,  255, 3,  2,  1,
      255, 9,  0,   127, 255, 4,   5,  6,  255, 9,  8,  7,  255, 10, 11, 12,
      255, 13, 14,  15,  255, 16,  17, 18, 255, 19, 20, 21, 255, 22, 23, 24,
      255, 25, 26,  27,  255, 28,  29, 30, 255, 31, 32, 33, 255, 34, 35, 36,
  };

  for (auto arch : kSwizzleArchs) {
    SwizzleRowFn func = RowFnFor(SwizzleOp::Copy, SurfaceFormat::B8G8R8A8,
                                 SurfaceFormat::R8G8B8A8, arch);
    if (func) {
      func(in_bgra, out, 5);
      EXPECT_TRUE(ArrayEqual(out, check_rgba));
    } else {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
    }

    func = RowFnFor(SwizzleOp::Copy, SurfaceFormat::B8G8R8A8,
                    SurfaceFormat::R8G8B8X8, arch);
    if (func) {
      func(in_bgra, out, 5);
      EXPECT_TRUE(ArrayEqual(out, check_rgbx));
    } else {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
    }

    func = RowFnFor(SwizzleOp::Copy, SurfaceFormat::R8G8B8X8,
                    SurfaceFormat::A8R8G8B8, arch);
    if (func) {
      func(check_rgba, out, 5);
      EXPECT_TRUE(ArrayEqual(out, check_xrgb));
    } else {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
    }

    func = RowFnFor(SwizzleOp::Copy, SurfaceFormat::R8G8B8A8,
                    SurfaceFormat::X8R8G8B8, arch);
    if (func) {
      func(check_rgba, out, 5);
      EXPECT_TRUE(ArrayEqual(out, check_xrgb));
    } else {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
    }

    func = RowFnFor(SwizzleOp::Copy, SurfaceFormat::B8G8R8A8,
                    SurfaceFormat::B8G8R8A8, arch);
    if (func) {
      func(in_bgra, out, 5);
      EXPECT_TRUE(ArrayEqual(out, in_bgra));
    } else {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
    }

    func = RowFnFor(SwizzleOp::Copy, SurfaceFormat::B8G8R8A8,
                    SurfaceFormat::B8G8R8, arch);
    if (func) {
      func(in_bgra, out24, 5);
      EXPECT_TRUE(ArrayEqual(out24, check_bgr));
    } else {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
    }

    func = RowFnFor(SwizzleOp::Copy, SurfaceFormat::B8G8R8A8,
                    SurfaceFormat::R8G8B8, arch);
    if (func) {
      func(in_bgra, out24, 5);
      EXPECT_TRUE(ArrayEqual(out24, check_rgb));
    } else {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
    }

    func = RowFnFor(SwizzleOp::Copy, SurfaceFormat::R8G8B8,
                    SurfaceFormat::B8G8R8X8, arch);
    if (func) {
      func(in_rgb, out_unpack, 16);
      EXPECT_TRUE(ArrayEqual(out_unpack, check_unpack_bgrx));

      memset(out_unpack, 0xE5, sizeof(out_unpack));
      memcpy(out_unpack, in_rgb, sizeof(in_rgb));
      func(out_unpack, out_unpack, 16);
      EXPECT_TRUE(ArrayEqual(out_unpack, check_unpack_bgrx));
    } else {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
    }

    func = RowFnFor(SwizzleOp::Copy, SurfaceFormat::R8G8B8,
                    SurfaceFormat::R8G8B8X8, arch);
    if (func) {
      func(in_rgb, out_unpack, 16);
      EXPECT_TRUE(ArrayEqual(out_unpack, check_unpack_rgbx));

      memset(out_unpack, 0xE5, sizeof(out_unpack));
      memcpy(out_unpack, in_rgb, sizeof(in_rgb));
      func(out_unpack, out_unpack, 16);
      EXPECT_TRUE(ArrayEqual(out_unpack, check_unpack_rgbx));
    } else {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
    }

    func = RowFnFor(SwizzleOp::Copy, SurfaceFormat::R8G8B8,
                    SurfaceFormat::X8R8G8B8, arch);
    if (func) {
      func(in_rgb, out_unpack, 16);
      EXPECT_TRUE(ArrayEqual(out_unpack, check_unpack_xrgb));

      memset(out_unpack, 0xE5, sizeof(out_unpack));
      memcpy(out_unpack, in_rgb, sizeof(in_rgb));
      func(out_unpack, out_unpack, 16);
      EXPECT_TRUE(ArrayEqual(out_unpack, check_unpack_xrgb));
    } else {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
    }
  }
}

TEST(Moz2D, SwizzleRowCmyk)
{
  // We use inverted CMYK here as it's preferred in the field and is easier to
  // explain
  const uint8_t in_inverted_cmyk[10 * 4] = {
      200, 50,  10,  255,  // K=255: color passes through (R=iC, G=iM, B=iY)
      200, 50,  10,  0,    // K=0: fully black
      255, 255, 255, 255,  // all max -> 255,255,255
      0,   0,   0,   0,    // all min -> 0,0,0
      128, 128, 128, 128,  // floor edge: 128*128/255 = 64.25 -> 64
      255, 0,   0,   255,  // only iC -> only R (catches R/B swap)
      0,   0,   255, 255,  // only iY -> only B (catches R/B swap)
      2,   2,   2,   128,  // truncation: 2*128/255 = 1.003 -> 1
      255, 255, 255, 1,    // tiny K: 255*1/255 = 1
      100, 150, 200, 77,   // arbitrary mid values
  };
  uint8_t out[10 * 4];

  // clang-format off
  const uint8_t check_inverted_bgrx[10 * 4] = {
       10, 50, 200, 255,  0,  0,  0, 255, 255, 255, 255, 255,
        0,  0,   0, 255, 64, 64, 64, 255,   0,   0, 255, 255,
      255,  0,   0, 255,  1,  1,  1, 255,   1,   1,   1, 255,
       60, 45,  30, 255,
  };

  // Non-inverted CMYK: each channel is inverted (255 - x) during the conversion.
  const uint8_t check_bgrx[10 * 4] = {
        0,   0,   0, 255, 245, 205,  55, 255, 0,  0,   0, 255,
      255, 255, 255, 255,  63,  63,  63, 255, 0,  0,   0, 255,
        0,   0,   0, 255, 126, 126, 126, 255, 0,  0,   0, 255,
       38, 73,  108, 255,
  };
  // clang-format on

  for (SwizzleArch arch : kSwizzleArchs) {
    SwizzleRowFn func = RowFnFor(SwizzleOp::Copy, SurfaceFormat::CMYK,
                                 SurfaceFormat::B8G8R8X8, arch);
    if (func) {
      func(in_inverted_cmyk, out, 10);
      EXPECT_TRUE(ArrayEqual(out, check_bgrx));
    } else {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
    }

    func = RowFnFor(SwizzleOp::Copy, SurfaceFormat::InvertedCMYK,
                    SurfaceFormat::B8G8R8X8, arch);
    if (func) {
      func(in_inverted_cmyk, out, 10);
      EXPECT_TRUE(ArrayEqual(out, check_inverted_bgrx));
    } else {
      EXPECT_NE(arch, SwizzleArch::eAny);
      EXPECT_NE(arch, SwizzleArch::eFallback);
    }
  }
}

TEST(Moz2D, SwizzleCmykRowSweep)
{
  CheckCmykRowSweep(SurfaceFormat::CMYK);
  CheckCmykRowSweep(SurfaceFormat::InvertedCMYK);
}

TEST(Moz2D, ReorientRow)
{
  // Input is a 3x4 image.
  const uint8_t in_row0[3 * 4] = {
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
  };
  const uint8_t in_row1[3 * 4] = {
      12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
  };
  const uint8_t in_row2[3 * 4] = {
      24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
  };
  const uint8_t in_row3[3 * 4] = {
      36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
  };

  // Output is either a 3x4 image or 4x3 image.
  uint8_t out[3 * 4 * 4];
  IntSize outSize(3, 4);
  IntSize outSizeSwap(4, 3);
  int32_t outStride = 3 * 4;
  int32_t outStrideSwap = 4 * 4;
  IntRect dirty;

  auto func = ReorientRow(Orientation());
  dirty = func(in_row0, 0, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 0, 3, 1));
  dirty = func(in_row1, 1, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 1, 3, 1));
  dirty = func(in_row2, 2, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 2, 3, 1));
  dirty = func(in_row3, 3, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 3, 3, 1));

  // clang-format off
  const uint8_t check_identity[3 * 4 * 4] = {
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11,
      12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
      24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
      36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
  };
  // clang-format on
  EXPECT_TRUE(ArrayEqual(out, check_identity));

  func = ReorientRow(Orientation(Angle::D90));
  dirty = func(in_row0, 0, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(3, 0, 1, 3));
  dirty = func(in_row1, 1, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(2, 0, 1, 3));
  dirty = func(in_row2, 2, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(1, 0, 1, 3));
  dirty = func(in_row3, 3, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(0, 0, 1, 3));

  // clang-format off
  const uint8_t check_d90[3 * 4 * 4] = {
      36, 37, 38, 39, 24, 25, 26, 27, 12, 13, 14, 15, 0,  1,  2,  3,
      40, 41, 42, 43, 28, 29, 30, 31, 16, 17, 18, 19, 4,  5,  6,  7,
      44, 45, 46, 47, 32, 33, 34, 35, 20, 21, 22, 23, 8,  9,  10, 11,
  };
  // clang-format on
  EXPECT_TRUE(ArrayEqual(out, check_d90));

  func = ReorientRow(Orientation(Angle::D180));
  dirty = func(in_row0, 0, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 3, 3, 1));
  dirty = func(in_row1, 1, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 2, 3, 1));
  dirty = func(in_row2, 2, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 1, 3, 1));
  dirty = func(in_row3, 3, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 0, 3, 1));

  // clang-format off
  const uint8_t check_d180[3 * 4 * 4] = {
      44, 45, 46, 47, 40, 41, 42, 43, 36, 37, 38, 39,
      32, 33, 34, 35, 28, 29, 30, 31, 24, 25, 26, 27,
      20, 21, 22, 23, 16, 17, 18, 19, 12, 13, 14, 15,
      8,  9,  10, 11, 4,  5,  6,  7,  0,  1,  2,  3,
  };
  // clang-format on
  EXPECT_TRUE(ArrayEqual(out, check_d180));

  func = ReorientRow(Orientation(Angle::D270));
  dirty = func(in_row0, 0, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(0, 0, 1, 3));
  dirty = func(in_row1, 1, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(1, 0, 1, 3));
  dirty = func(in_row2, 2, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(2, 0, 1, 3));
  dirty = func(in_row3, 3, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(3, 0, 1, 3));

  // clang-format off
  const uint8_t check_d270[3 * 4 * 4] = {
      8,  9,  10, 11, 20, 21, 22, 23, 32, 33, 34, 35, 44, 45, 46, 47,
      4,  5,  6,  7,  16, 17, 18, 19, 28, 29, 30, 31, 40, 41, 42, 43,
      0,  1,  2,  3,  12, 13, 14, 15, 24, 25, 26, 27, 36, 37, 38, 39,
  };
  // clang-format on
  EXPECT_TRUE(ArrayEqual(out, check_d270));

  func = ReorientRow(Orientation(Angle::D0, Flip::Horizontal));
  dirty = func(in_row0, 0, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 0, 3, 1));
  dirty = func(in_row1, 1, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 1, 3, 1));
  dirty = func(in_row2, 2, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 2, 3, 1));
  dirty = func(in_row3, 3, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 3, 3, 1));

  // clang-format off
  const uint8_t check_d0_flip[3 * 4 * 4] = {
      8,  9,  10, 11, 4,  5,  6,  7,  0,  1,  2,  3,
      20, 21, 22, 23, 16, 17, 18, 19, 12, 13, 14, 15,
      32, 33, 34, 35, 28, 29, 30, 31, 24, 25, 26, 27,
      44, 45, 46, 47, 40, 41, 42, 43, 36, 37, 38, 39,
  };
  // clang-format on
  EXPECT_TRUE(ArrayEqual(out, check_d0_flip));

  func = ReorientRow(Orientation(Angle::D90, Flip::Horizontal));
  dirty = func(in_row0, 0, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(0, 0, 1, 3));
  dirty = func(in_row1, 1, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(1, 0, 1, 3));
  dirty = func(in_row2, 2, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(2, 0, 1, 3));
  dirty = func(in_row3, 3, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(3, 0, 1, 3));

  // clang-format off
  const uint8_t check_d90_flip[3 * 4 * 4] = {
      0,  1,  2,  3,  12, 13, 14, 15, 24, 25, 26, 27, 36, 37, 38, 39,
      4,  5,  6,  7,  16, 17, 18, 19, 28, 29, 30, 31, 40, 41, 42, 43,
      8,  9,  10, 11, 20, 21, 22, 23, 32, 33, 34, 35, 44, 45, 46, 47,
  };
  // clang-format on
  EXPECT_TRUE(ArrayEqual(out, check_d90_flip));

  func = ReorientRow(Orientation(Angle::D180, Flip::Horizontal));
  dirty = func(in_row0, 0, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 3, 3, 1));
  dirty = func(in_row1, 1, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 2, 3, 1));
  dirty = func(in_row2, 2, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 1, 3, 1));
  dirty = func(in_row3, 3, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 0, 3, 1));

  // clang-format off
  const uint8_t check_d180_flip[3 * 4 * 4] = {
      36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
      24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
      12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11,
  };
  // clang-format on
  EXPECT_TRUE(ArrayEqual(out, check_d180_flip));

  func = ReorientRow(Orientation(Angle::D270, Flip::Horizontal));
  dirty = func(in_row0, 0, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(3, 0, 1, 3));
  dirty = func(in_row1, 1, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(2, 0, 1, 3));
  dirty = func(in_row2, 2, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(1, 0, 1, 3));
  dirty = func(in_row3, 3, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(0, 0, 1, 3));

  // clang-format off
  const uint8_t check_d270_flip[3 * 4 * 4] = {
      44, 45, 46, 47, 32, 33, 34, 35, 20, 21, 22, 23, 8,  9,  10, 11,
      40, 41, 42, 43, 28, 29, 30, 31, 16, 17, 18, 19, 4,  5,  6,  7,
      36, 37, 38, 39, 24, 25, 26, 27, 12, 13, 14, 15, 0,  1,  2,  3,
  };
  // clang-format on
  EXPECT_TRUE(ArrayEqual(out, check_d270_flip));

  func = ReorientRow(
      Orientation(Angle::D0, Flip::Horizontal, /* aFlipFirst */ true));
  dirty = func(in_row0, 0, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 0, 3, 1));
  dirty = func(in_row1, 1, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 1, 3, 1));
  dirty = func(in_row2, 2, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 2, 3, 1));
  dirty = func(in_row3, 3, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 3, 3, 1));

  // No rotation, so flipping before and after are the same.
  EXPECT_TRUE(ArrayEqual(out, check_d0_flip));

  func = ReorientRow(
      Orientation(Angle::D90, Flip::Horizontal, /* aFlipFirst */ true));
  dirty = func(in_row0, 0, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(3, 0, 1, 3));
  dirty = func(in_row1, 1, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(2, 0, 1, 3));
  dirty = func(in_row2, 2, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(1, 0, 1, 3));
  dirty = func(in_row3, 3, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(0, 0, 1, 3));

  // Flip, rotate 90 degrees is the same as rotate 270 degrees, flip.
  EXPECT_TRUE(ArrayEqual(out, check_d270_flip));

  func = ReorientRow(
      Orientation(Angle::D180, Flip::Horizontal, /* aFlipFirst */ true));
  dirty = func(in_row0, 0, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 3, 3, 1));
  dirty = func(in_row1, 1, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 2, 3, 1));
  dirty = func(in_row2, 2, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 1, 3, 1));
  dirty = func(in_row3, 3, out, outSize, outStride);
  EXPECT_EQ(dirty, IntRect(0, 0, 3, 1));

  // Flip, rotate 180 degrees is the same as rotate 180 degrees, flip.
  EXPECT_TRUE(ArrayEqual(out, check_d180_flip));

  func = ReorientRow(
      Orientation(Angle::D270, Flip::Horizontal, /* aFlipFirst */ true));
  dirty = func(in_row0, 0, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(0, 0, 1, 3));
  dirty = func(in_row1, 1, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(1, 0, 1, 3));
  dirty = func(in_row2, 2, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(2, 0, 1, 3));
  dirty = func(in_row3, 3, out, outSizeSwap, outStrideSwap);
  EXPECT_EQ(dirty, IntRect(3, 0, 1, 3));

  // Flip, rotate 270 degrees is the same as rotate 90 degrees, flip.
  EXPECT_TRUE(ArrayEqual(out, check_d90_flip));
}

TEST(Moz2D, PremultiplyRowSweep)
{
  CheckBGRARowSweep(SwizzleOp::Premultiply, SurfaceFormat::B8G8R8A8);
  CheckBGRARowSweep(SwizzleOp::Premultiply, SurfaceFormat::R8G8B8A8);
  CheckBGRARowSweep(SwizzleOp::Premultiply, SurfaceFormat::A8R8G8B8);
}

TEST(Moz2D, PremultiplyDataSweep)
{
  CheckBGRADataSweep(SwizzleOp::Premultiply, SurfaceFormat::B8G8R8A8);
  CheckBGRADataSweep(SwizzleOp::Premultiply, SurfaceFormat::R8G8B8A8);
}

TEST(Moz2D, PremultiplyYFlipDataSweep)
{
  CheckBGRADataSweep(SwizzleOp::PremultiplyYFlip, SurfaceFormat::B8G8R8A8);
  CheckBGRADataSweep(SwizzleOp::PremultiplyYFlip, SurfaceFormat::R8G8B8A8);
}

TEST(Moz2D, PremultiplyYFlipInplaceDataSweep)
{
  CheckBGRADataSweep(SwizzleOp::PremultiplyYFlipInplace,
                     SurfaceFormat::B8G8R8A8);
  CheckBGRADataSweep(SwizzleOp::PremultiplyYFlipInplace,
                     SurfaceFormat::R8G8B8A8);
}

TEST(Moz2D, UnpremultiplyRowSweep)
{
  CheckBGRARowSweep(SwizzleOp::Unpremultiply, SurfaceFormat::B8G8R8A8);
  CheckBGRARowSweep(SwizzleOp::Unpremultiply, SurfaceFormat::R8G8B8A8);
  CheckBGRARowSweep(SwizzleOp::Unpremultiply, SurfaceFormat::A8R8G8B8);
}

TEST(Moz2D, UnpremultiplyDataSweep)
{
  CheckBGRADataSweep(SwizzleOp::Unpremultiply, SurfaceFormat::B8G8R8A8);
  CheckBGRADataSweep(SwizzleOp::Unpremultiply, SurfaceFormat::R8G8B8A8);
}

TEST(Moz2D, SwizzleDataSweep)
{
  CheckBGRADataSweep(SwizzleOp::Copy, SurfaceFormat::B8G8R8X8);
  CheckBGRADataSweep(SwizzleOp::Copy, SurfaceFormat::R8G8B8A8);
  CheckBGRADataSweep(SwizzleOp::Copy, SurfaceFormat::R8G8B8X8);
}

TEST(Moz2D, SwizzleYFlipDataSweep)
{
  CheckBGRADataSweep(SwizzleOp::YFlip, SurfaceFormat::B8G8R8X8);
  CheckBGRADataSweep(SwizzleOp::YFlip, SurfaceFormat::R8G8B8A8);
  CheckBGRADataSweep(SwizzleOp::YFlip, SurfaceFormat::R8G8B8X8);
}

TEST(Moz2D, SwizzleYFlipInplaceDataSweep)
{
  CheckBGRADataSweep(SwizzleOp::YFlipInplace, SurfaceFormat::B8G8R8X8);
  CheckBGRADataSweep(SwizzleOp::YFlipInplace, SurfaceFormat::R8G8B8A8);
  CheckBGRADataSweep(SwizzleOp::YFlipInplace, SurfaceFormat::R8G8B8X8);
}

TEST(Moz2D, SwizzleRowSweep)
{
  CheckBGRARowSweep(SwizzleOp::Copy, SurfaceFormat::B8G8R8A8);
  CheckBGRARowSweep(SwizzleOp::Copy, SurfaceFormat::R8G8B8A8);
  CheckBGRARowSweep(SwizzleOp::Copy, SurfaceFormat::R8G8B8X8);
}

TEST(Moz2D, UnpackRowRGB24Sweep)
{
  CheckUnpackRowSweep(SurfaceFormat::R8G8B8X8);
  CheckUnpackRowSweep(SurfaceFormat::B8G8R8X8);
  CheckUnpackRowSweep(SurfaceFormat::X8R8G8B8);
}

class Moz2D_SwizzleBench : public ::testing::Test {
 public:
  static constexpr int32_t kWidth = 256;
  static constexpr int32_t kGap = 0;
  static constexpr int32_t kStride = kWidth * 4 + kGap;
  static constexpr int32_t kHeight = 4;
  static constexpr int32_t kBufLen = kStride * kHeight;
  static constexpr int32_t kRepeat = 256;

  Moz2D_SwizzleBench() = default;
  ~Moz2D_SwizzleBench() = default;

  void SetUp() final {
    mSrc = MakeUniqueFallible<uint8_t[]>(kBufLen);
    if (!mSrc) {
      return;
    }
    mDst = MakeUniqueFallible<uint8_t[]>(kBufLen);
    if (!mDst) {
      mSrc.reset();
      return;
    }
    memset(mSrc.get(), 0x2A, kBufLen);
    memset(mDst.get(), 0x0, kBufLen);
  }

  bool SwizzleRow(SwizzleOp aOp, SurfaceFormat aSrcFormat,
                  SurfaceFormat aDstFormat, SwizzleArch aArch) {
    if (!mSrc || !mDst) {
      return false;
    }

    SwizzleRowFn func = RowFnFor(aOp, aSrcFormat, aDstFormat, aArch);
    if (!func) {
      return false;
    }

    int32_t srcStride = BytesPerPixel(aSrcFormat) * kWidth + kGap;
    int32_t dstStride = BytesPerPixel(aDstFormat) * kWidth + kGap;
    MOZ_ASSERT(srcStride <= kStride);
    MOZ_ASSERT(dstStride <= kStride);

    for (int32_t i = 0; i < kRepeat; ++i) {
      uint8_t* src = mSrc.get();
      uint8_t* dst = mDst.get();
      for (int32_t k = 0; k < kHeight; ++k) {
        func(src, dst, kWidth);
        src += srcStride;
        dst += dstStride;
      }
    }
    return true;
  }

  bool SwizzleData(SwizzleOp aOp, SurfaceFormat aSrcFormat,
                   SurfaceFormat aDstFormat, SwizzleArch aArch) {
    if (!mSrc || !mDst) {
      return false;
    }

    int32_t srcStride = BytesPerPixel(aSrcFormat) * kWidth + kGap;
    int32_t dstStride = BytesPerPixel(aDstFormat) * kWidth + kGap;
    MOZ_ASSERT(srcStride <= kStride);
    MOZ_ASSERT(dstStride <= kStride);

    bool success = true;
    uint8_t* src = mSrc.get();
    uint8_t* dst = mDst.get();

    for (int32_t i = 0; i < kRepeat && success; ++i) {
      switch (aOp) {
        case SwizzleOp::Copy:
          success = ::SwizzleData(src, srcStride, aSrcFormat, dst, dstStride,
                                  aDstFormat, IntSize(kWidth, kHeight), aArch);
          break;
        case SwizzleOp::Premultiply:
          success =
              PremultiplyData(src, srcStride, aSrcFormat, dst, dstStride,
                              aDstFormat, IntSize(kWidth, kHeight), aArch);
          break;
        case SwizzleOp::Unpremultiply:
          success =
              UnpremultiplyData(src, srcStride, aSrcFormat, dst, dstStride,
                                aDstFormat, IntSize(kWidth, kHeight), aArch);
          break;
        default:
          MOZ_ASSERT_UNREACHABLE("Unhandled SwizzleOp!");
          return false;
      }
    }

    return success;
  }

  void TearDown() final {
    mSrc.reset();
    mDst.reset();
  }

 private:
  UniquePtr<uint8_t[]> mSrc;
  UniquePtr<uint8_t[]> mDst;
};

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Unpack_RGB_BGRX_Fallback,
                  [this]() -> bool {
                    return SwizzleRow(SwizzleOp::Copy, SurfaceFormat::R8G8B8,
                                      SurfaceFormat::B8G8R8X8,
                                      SwizzleArch::eFallback);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Unpack_RGB_BGRX_Generic,
                  [this]() -> bool {
                    return SwizzleRow(SwizzleOp::Copy, SurfaceFormat::R8G8B8,
                                      SurfaceFormat::B8G8R8X8,
                                      SwizzleArch::eGeneric);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Unpack_RGB_BGRX_NEON, [this]() -> bool {
  return SwizzleRow(SwizzleOp::Copy, SurfaceFormat::R8G8B8,
                    SurfaceFormat::B8G8R8X8, SwizzleArch::eNEON);
});

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Unpack_RGB_BGRX_SSSE3, [this]() -> bool {
  return SwizzleRow(SwizzleOp::Copy, SurfaceFormat::R8G8B8,
                    SurfaceFormat::B8G8R8X8, SwizzleArch::eSSSE3);
});

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Unpack_RGB_BGRX_AVX2, [this]() -> bool {
  return SwizzleRow(SwizzleOp::Copy, SurfaceFormat::R8G8B8,
                    SurfaceFormat::B8G8R8X8, SwizzleArch::eAVX2);
});

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Swizzle_RGBA_BGRX_Fallback,
                  [this]() -> bool {
                    return SwizzleData(SwizzleOp::Copy, SurfaceFormat::R8G8B8A8,
                                       SurfaceFormat::B8G8R8X8,
                                       SwizzleArch::eFallback);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Swizzle_RGBA_BGRX_Generic,
                  [this]() -> bool {
                    return SwizzleRow(SwizzleOp::Copy, SurfaceFormat::R8G8B8A8,
                                      SurfaceFormat::B8G8R8X8,
                                      SwizzleArch::eGeneric);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Swizzle_RGBA_BGRX_NEON, [this]() -> bool {
  return SwizzleData(SwizzleOp::Copy, SurfaceFormat::R8G8B8A8,
                     SurfaceFormat::B8G8R8X8, SwizzleArch::eNEON);
});

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Swizzle_RGBA_BGRX_SSE2, [this]() -> bool {
  return SwizzleData(SwizzleOp::Copy, SurfaceFormat::R8G8B8A8,
                     SurfaceFormat::B8G8R8X8, SwizzleArch::eSSE2);
});

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Swizzle_RGBA_BGRX_SSSE3,
                  [this]() -> bool {
                    return SwizzleData(SwizzleOp::Copy, SurfaceFormat::R8G8B8A8,
                                       SurfaceFormat::B8G8R8X8,
                                       SwizzleArch::eSSSE3);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Swizzle_RGBA_BGRX_AVX2, [this]() -> bool {
  return SwizzleData(SwizzleOp::Copy, SurfaceFormat::R8G8B8A8,
                     SurfaceFormat::B8G8R8X8, SwizzleArch::eAVX2);
});

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Swizzle_InvertedCMYK_BGRX_Fallback,
                  [this]() -> bool {
                    return SwizzleRow(
                        SwizzleOp::Copy, SurfaceFormat::InvertedCMYK,
                        SurfaceFormat::B8G8R8X8, SwizzleArch::eFallback);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Swizzle_InvertedCMYK_BGRX_Generic,
                  [this]() -> bool {
                    return SwizzleRow(
                        SwizzleOp::Copy, SurfaceFormat::InvertedCMYK,
                        SurfaceFormat::B8G8R8X8, SwizzleArch::eGeneric);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Swizzle_InvertedCMYK_BGRX_NEON,
                  [this]() -> bool {
                    return SwizzleRow(
                        SwizzleOp::Copy, SurfaceFormat::InvertedCMYK,
                        SurfaceFormat::B8G8R8X8, SwizzleArch::eNEON);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Swizzle_InvertedCMYK_BGRX_SSE2,
                  [this]() -> bool {
                    return SwizzleRow(
                        SwizzleOp::Copy, SurfaceFormat::InvertedCMYK,
                        SurfaceFormat::B8G8R8X8, SwizzleArch::eSSE2);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Swizzle_InvertedCMYK_BGRX_AVX2,
                  [this]() -> bool {
                    return SwizzleRow(
                        SwizzleOp::Copy, SurfaceFormat::InvertedCMYK,
                        SurfaceFormat::B8G8R8X8, SwizzleArch::eAVX2);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Premultiply_RGBA_BGRA_Fallback,
                  [this]() -> bool {
                    return SwizzleData(
                        SwizzleOp::Premultiply, SurfaceFormat::R8G8B8A8,
                        SurfaceFormat::B8G8R8A8, SwizzleArch::eFallback);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Premultiply_RGBA_BGRA_Generic,
                  [this]() -> bool {
                    return SwizzleRow(
                        SwizzleOp::Premultiply, SurfaceFormat::R8G8B8A8,
                        SurfaceFormat::B8G8R8A8, SwizzleArch::eGeneric);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Premultiply_RGBA_BGRA_NEON,
                  [this]() -> bool {
                    return SwizzleData(
                        SwizzleOp::Premultiply, SurfaceFormat::R8G8B8A8,
                        SurfaceFormat::B8G8R8A8, SwizzleArch::eNEON);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Premultiply_RGBA_BGRA_SSE2,
                  [this]() -> bool {
                    return SwizzleData(
                        SwizzleOp::Premultiply, SurfaceFormat::R8G8B8A8,
                        SurfaceFormat::B8G8R8A8, SwizzleArch::eSSE2);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Premultiply_RGBA_BGRA_AVX2,
                  [this]() -> bool {
                    return SwizzleData(
                        SwizzleOp::Premultiply, SurfaceFormat::R8G8B8A8,
                        SurfaceFormat::B8G8R8A8, SwizzleArch::eAVX2);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Unpremultiply_RGBA_BGRA_Fallback,
                  [this]() -> bool {
                    return SwizzleData(
                        SwizzleOp::Unpremultiply, SurfaceFormat::R8G8B8A8,
                        SurfaceFormat::B8G8R8A8, SwizzleArch::eFallback);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Unpremultiply_RGBA_BGRA_Generic,
                  [this]() -> bool {
                    return SwizzleRow(
                        SwizzleOp::Unpremultiply, SurfaceFormat::R8G8B8A8,
                        SurfaceFormat::B8G8R8A8, SwizzleArch::eGeneric);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Unpremultiply_RGBA_BGRA_NEON,
                  [this]() -> bool {
                    return SwizzleData(
                        SwizzleOp::Unpremultiply, SurfaceFormat::R8G8B8A8,
                        SurfaceFormat::B8G8R8A8, SwizzleArch::eNEON);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Unpremultiply_RGBA_BGRA_SSE2,
                  [this]() -> bool {
                    return SwizzleData(
                        SwizzleOp::Unpremultiply, SurfaceFormat::R8G8B8A8,
                        SurfaceFormat::B8G8R8A8, SwizzleArch::eSSE2);
                  });

MOZ_GTEST_BENCH_F(Moz2D_SwizzleBench, Unpremultiply_RGBA_BGRA_AVX2,
                  [this]() -> bool {
                    return SwizzleData(
                        SwizzleOp::Unpremultiply, SurfaceFormat::R8G8B8A8,
                        SurfaceFormat::B8G8R8A8, SwizzleArch::eAVX2);
                  });
