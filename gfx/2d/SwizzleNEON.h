/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SWIZZLE_NEON_H_
#define MOZILLA_GFX_SWIZZLE_NEON_H_

#ifdef MOZILLA_GFX_SWIZZLE_GENERIC_H_
#  error "SwizzleNEON.h must be included before SwizzleGeneric.h"
#endif

#include <arm_neon.h>

#include "SwizzleGenericDecls.h"

namespace mozilla::gfx {

#ifdef __aarch64__
using mozneon = xsimd::neon64;
#else
using mozneon = xsimd::neon;
#endif

extern const uint32_t sUnpremultiplyTable_NEON[256];

template <class Arch>
  requires std::same_as<Arch, xsimd::neon> || std::same_as<Arch, xsimd::neon64>
static MOZ_ALWAYS_INLINE xsimd::batch<uint8_t, Arch> LoadRemainder_SIMD(
    const uint8_t* aSrc, size_t aLength) {
  const uint32_t* src32 = reinterpret_cast<const uint32_t*>(aSrc);
  uint32x4_t dst32;
  if (aLength >= 2) {
    // Load first 2 pixels
    dst32 = vcombine_u32(vld1_u32(src32), vdup_n_u32(0));
    // Load third pixel
    if (aLength >= 3) {
      dst32 = vld1q_lane_u32(src32 + 2, dst32, 2);
    }
  } else {
    // Load single pixel
    dst32 = vld1q_lane_u32(src32, vdupq_n_u32(0), 0);
  }
  return vreinterpretq_u8_u32(dst32);
}

template <class Arch>
  requires std::same_as<Arch, xsimd::neon> || std::same_as<Arch, xsimd::neon64>
static MOZ_ALWAYS_INLINE void StoreRemainder_SIMD(
    uint8_t* aDst, size_t aLength, const xsimd::batch<uint8_t, Arch>& aSrc) {
  uint32_t* dst32 = reinterpret_cast<uint32_t*>(aDst);
  uint32x4_t src32 = vreinterpretq_u32_u8(aSrc);
  if (aLength >= 2) {
    // Store first 2 pixels
    vst1_u32(dst32, vget_low_u32(src32));
    // Store third pixel
    if (aLength >= 3) {
      vst1q_lane_u32(dst32 + 2, src32, 2);
    }
  } else {
    // Store single pixel
    vst1q_lane_u32(dst32, src32, 0);
  }
}

// This matches the original specialization, which is cheaper than what xsimd
// and clang generate.
template <class Arch>
  requires std::same_as<Arch, xsimd::neon> || std::same_as<Arch, xsimd::neon64>
static MOZ_ALWAYS_INLINE xsimd::batch<uint16_t, Arch> ExtractAlpha_SIMD(
    const xsimd::batch<uint8_t, Arch>& aSrc,
    const xsimd::batch<uint16_t, Arch>& aGreenAlpha) {
  return vtrnq_u16(aGreenAlpha, aGreenAlpha).val[1];
}

// arm devices perform better with this than the xsimd::shuffle the generic
// version uses, because there is no xsimd::swizzle vectorization for neon.
template <class Arch>
  requires std::same_as<Arch, xsimd::neon>
static MOZ_ALWAYS_INLINE xsimd::batch<uint8_t, Arch> SwapRB8_SIMD(
    const xsimd::batch<uint8_t, Arch>& aPx) {
  // Swap R and B, leaving G and A alone:
  // (((px >> 16) | (px << 16)) & 0x00FF00FF) | (px & ~0x00FF00FF)
  uint16x8_t px16 = vreinterpretq_u16_u8(aPx);
  return vreinterpretq_u8_u16(
      vbslq_u16(vdupq_n_u16(0x00FF), vrev32q_u16(px16), px16));
}

template <class Arch>
  requires std::same_as<Arch, xsimd::neon> || std::same_as<Arch, xsimd::neon64>
static MOZ_ALWAYS_INLINE xsimd::batch<uint32_t, Arch> UnpremultiplyLookup_SIMD(
    const xsimd::batch<uint8_t, Arch>& aSrc,
    const xsimd::batch<uint16_t, Arch>& aGa) {
  // Extract the alphas for the 4 pixels from the now isolated words.
  alignas(Arch::alignment())
      uint16_t alphaBuf[xsimd::batch<uint16_t, Arch>::size];
  aGa.store_aligned(alphaBuf);

  // Load all of the interleaved low and high portions of the reciprocals
  // and combine them a single vector as lo1 hi1 lo2 hi2 lo3 hi3 lo4 hi4
  return vld1q_lane_u32(
      &sUnpremultiplyTable_NEON[alphaBuf[7]],
      vld1q_lane_u32(
          &sUnpremultiplyTable_NEON[alphaBuf[5]],
          vld1q_lane_u32(&sUnpremultiplyTable_NEON[alphaBuf[3]],
                         vld1q_lane_u32(&sUnpremultiplyTable_NEON[alphaBuf[1]],
                                        vdupq_n_u32(0), 0),
                         1),
          2),
      3);
}

template <class Arch>
  requires std::same_as<Arch, xsimd::neon> || std::same_as<Arch, xsimd::neon64>
static MOZ_ALWAYS_INLINE xsimd::batch<uint8_t, Arch> UnpremultiplyReverse_SIMD(
    const xsimd::batch<uint8_t, Arch>& aSrc,
    const xsimd::batch<uint32_t, Arch>& aRecip,
    const xsimd::batch<uint16_t, Arch>& aRb,
    const xsimd::batch<uint16_t, Arch>& aGa) {
  // Transpose the interleaved low/high portions so that we produce
  // two separate duplicated vectors for the low and high portions respectively:
  // lo1 lo1 lo2 lo2 lo3 lo3 lo4 lo4 and hi1 hi1 hi2 hi2 hi3 hi3 hi4 hi4
  uint16x8_t recip16 = vreinterpretq_u16_u32(aRecip);
  uint16x8x2_t q1234lohi = vtrnq_u16(recip16, recip16);

  // VQDMULH is a signed multiply that doubles (*2) the result, then takes the
  // high word.  To work around the signedness and the doubling, the low
  // portion of the reciprocal only stores the lower 15 bits, which fits in a
  // signed 16 bit integer. The high 9 bit portion is effectively also doubled
  // by 2 as a side-effect of being shifted for storage. Thus the output scale
  // of doing a normal multiply by the high portion and the VQDMULH by the low
  // portion are both doubled and can be safely added together. The resulting
  // sum just needs to be halved (via VHADD) to thus cancel out the doubling.
  // All this combines to produce a reciprocal multiply of the form:
  // rb = ((rb * hi) + ((rb * lo * 2) >> 16)) / 2
  auto rb = vhaddq_u16(vmulq_u16(aRb, q1234lohi.val[1]),
                       vreinterpretq_u16_s16(vqdmulhq_s16(
                           vreinterpretq_s16_u16(aRb),
                           vreinterpretq_s16_u16(q1234lohi.val[0]))));

  // ga = ((ga * hi) + ((ga * lo * 2) >> 16)) / 2
  auto ga = vhaddq_u16(vmulq_u16(aGa, q1234lohi.val[1]),
                       vreinterpretq_u16_s16(vqdmulhq_s16(
                           vreinterpretq_s16_u16(aGa),
                           vreinterpretq_s16_u16(q1234lohi.val[0]))));

  // Combine to the final pixel with ((rb | (ga << 8)) & ~0xFF000000) | (aSrc &
  // 0xFF000000), which inserts back in the original alpha value unchanged.
  return vreinterpretq_u8_u16(
      vbslq_u16(vreinterpretq_u16_u32(vdupq_n_u32(0xFF000000)),
                vreinterpretq_u16_u8(aSrc), vsliq_n_u16(rb, ga, 8)));
}

template <class Arch>
  requires std::same_as<Arch, xsimd::neon> || std::same_as<Arch, xsimd::neon64>
static MOZ_ALWAYS_INLINE xsimd::batch<uint8_t, Arch> LoadRemainderRGB_SIMD(
    const uint8_t* aSrc, size_t aLength) {
  // Load aLength (1-4) packed RGB pixels (3-12 bytes) without reading past the
  // 3*aLength valid source bytes. Bytes beyond the loaded pixels are unused by
  // the expand swizzle and dropped by StoreRemainder_SIMD.
  uint32x4_t px = vdupq_n_u32(0);
  if (aLength >= 2) {
    // Bytes 0-3: pixel 0 (RGB) and pixel 1's R.
    px = vld1q_lane_u32(reinterpret_cast<const uint32_t*>(aSrc), px, 0);
    if (aLength >= 3) {
      // Bytes 4-7: pixel 1's GB and pixel 2's RG.
      px = vld1q_lane_u32(reinterpret_cast<const uint32_t*>(aSrc + 4), px, 1);
      if (aLength >= 4) {
        // Bytes 8-11: pixel 2's B and pixel 3 (RGB).
        px = vld1q_lane_u32(reinterpret_cast<const uint32_t*>(aSrc + 8), px, 2);
      } else {
        // 3 pixels = 9 bytes; byte 8 is pixel 2's B.
        return vld1q_lane_u8(aSrc + 8, vreinterpretq_u8_u32(px), 8);
      }
    } else {
      // 2 pixels = 6 bytes; bytes 4-5 are pixel 1's GB.
      return vreinterpretq_u8_u16(
          vld1q_lane_u16(reinterpret_cast<const uint16_t*>(aSrc + 4),
                         vreinterpretq_u16_u32(px), 2));
    }
    return vreinterpretq_u8_u32(px);
  }
  // 1 pixel = 3 bytes: bytes 0-1 then byte 2.
  uint8x16_t p8 = vreinterpretq_u8_u16(vld1q_lane_u16(
      reinterpret_cast<const uint16_t*>(aSrc), vreinterpretq_u16_u32(px), 0));
  return vld1q_lane_u8(aSrc + 2, p8, 2);
}

// arm devices perform better with the original specialization as aarch64,
// because there is no xsimd::swizzle vectorization.
template <class Arch, bool aSwapRB>
  requires std::same_as<Arch, xsimd::neon>
static MOZ_ALWAYS_INLINE xsimd::batch<uint8_t, Arch> UnpackBatchRGB24_SIMD(
    const xsimd::batch<uint8_t, Arch>& aSrc) {
  uint8x16_t alpha = vreinterpretq_u8_u32(vdupq_n_u32(0xFF000000));
  uint8x8_t masklo;
  uint8x8_t maskhi;
  if constexpr (aSwapRB) {
    static const uint8_t masklo_data[] = {2, 1, 0, 0, 5, 4, 3, 0};
    static const uint8_t maskhi_data[] = {4, 3, 2, 0, 7, 6, 5, 0};
    masklo = vld1_u8(masklo_data);
    maskhi = vld1_u8(maskhi_data);
  } else {
    static const uint8_t masklo_data[] = {0, 1, 2, 0, 3, 4, 5, 0};
    static const uint8_t maskhi_data[] = {2, 3, 4, 0, 5, 6, 7, 0};
    masklo = vld1_u8(masklo_data);
    maskhi = vld1_u8(maskhi_data);
  }

  // G2R2B1G1 R1B0G0R0 -> X1R1G1B1 X0R0G0B0
  uint8x8_t pxlo = vtbl1_u8(vget_low_u8(aSrc), masklo);
  // B3G3R3B2 G2R2B1G1 -> X3R3G3B3 X2R2G2B2
  uint8x8_t pxhi =
      vtbl1_u8(vext_u8(vget_low_u8(aSrc), vget_high_u8(aSrc), 4), maskhi);
  return vorrq_u8(vcombine_u8(pxlo, pxhi), alpha);
}

}  // namespace mozilla::gfx

#endif /* MOZILLA_GFX_SWIZZLE_NEON_H_ */
