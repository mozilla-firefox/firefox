/*
 *  Copyright (c) 2026 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <assert.h>
#include <immintrin.h>

#include "./vpx_dsp_rtcd.h"

static INLINE uint64_t reduce_add_epi64_avx2(__m256i acc) {
  const __m128i lo = _mm256_castsi256_si128(acc);
  const __m128i hi = _mm256_extracti128_si256(acc, 1);
  __m128i sum = _mm_add_epi64(lo, hi);
  sum = _mm_add_epi64(sum, _mm_srli_si128(sum, 8));
#if VPX_ARCH_X86_64
  return (uint64_t)_mm_cvtsi128_si64(sum);
#else
  uint64_t tmp;
  _mm_storel_epi64((__m128i *)&tmp, sum);
  return tmp;
#endif
}

static INLINE __m256i widen_add_u32_to_u64_avx2(__m256i acc64, __m256i sum32) {
  const __m256i zero = _mm256_setzero_si256();
  acc64 = _mm256_add_epi64(acc64, _mm256_unpacklo_epi32(sum32, zero));
  return _mm256_add_epi64(acc64, _mm256_unpackhi_epi32(sum32, zero));
}

uint64_t vpx_sum_squares_2d_i16_avx2(const int16_t *src, int stride, int size) {
  if (size == 4) {
    const __m128i r0 = _mm_loadl_epi64((const __m128i *)(src + 0 * stride));
    const __m128i r1 = _mm_loadl_epi64((const __m128i *)(src + 1 * stride));
    const __m128i r2 = _mm_loadl_epi64((const __m128i *)(src + 2 * stride));
    const __m128i r3 = _mm_loadl_epi64((const __m128i *)(src + 3 * stride));
    const __m128i s01 = _mm_unpacklo_epi64(r0, r1);
    const __m128i s23 = _mm_unpacklo_epi64(r2, r3);
    const __m128i sq01 = _mm_madd_epi16(s01, s01);
    const __m128i sq23 = _mm_madd_epi16(s23, s23);
    const __m128i zero = _mm_setzero_si128();
    __m128i acc64 = _mm_add_epi64(_mm_unpacklo_epi32(sq01, zero),
                                  _mm_unpackhi_epi32(sq01, zero));
    acc64 = _mm_add_epi64(acc64, _mm_unpacklo_epi32(sq23, zero));
    acc64 = _mm_add_epi64(acc64, _mm_unpackhi_epi32(sq23, zero));
    acc64 = _mm_add_epi64(acc64, _mm_srli_si128(acc64, 8));
#if VPX_ARCH_X86_64
    return (uint64_t)_mm_cvtsi128_si64(acc64);
#else
    uint64_t tmp;
    _mm_storel_epi64((__m128i *)&tmp, acc64);
    return tmp;
#endif
  } else if (size == 8) {
    const __m128i r0 = _mm_loadu_si128((const __m128i *)(src + 0 * stride));
    const __m128i r1 = _mm_loadu_si128((const __m128i *)(src + 1 * stride));
    const __m128i r2 = _mm_loadu_si128((const __m128i *)(src + 2 * stride));
    const __m128i r3 = _mm_loadu_si128((const __m128i *)(src + 3 * stride));
    const __m128i r4 = _mm_loadu_si128((const __m128i *)(src + 4 * stride));
    const __m128i r5 = _mm_loadu_si128((const __m128i *)(src + 5 * stride));
    const __m128i r6 = _mm_loadu_si128((const __m128i *)(src + 6 * stride));
    const __m128i r7 = _mm_loadu_si128((const __m128i *)(src + 7 * stride));

    const __m256i s01 =
        _mm256_inserti128_si256(_mm256_castsi128_si256(r0), r1, 1);
    const __m256i s23 =
        _mm256_inserti128_si256(_mm256_castsi128_si256(r2), r3, 1);
    const __m256i s45 =
        _mm256_inserti128_si256(_mm256_castsi128_si256(r4), r5, 1);
    const __m256i s67 =
        _mm256_inserti128_si256(_mm256_castsi128_si256(r6), r7, 1);

    const __m256i sq01 = _mm256_madd_epi16(s01, s01);
    const __m256i sq23 = _mm256_madd_epi16(s23, s23);
    const __m256i sq45 = _mm256_madd_epi16(s45, s45);
    const __m256i sq67 = _mm256_madd_epi16(s67, s67);

    __m256i acc64 = _mm256_setzero_si256();
    acc64 = widen_add_u32_to_u64_avx2(acc64, _mm256_add_epi32(sq01, sq23));
    acc64 = widen_add_u32_to_u64_avx2(acc64, _mm256_add_epi32(sq45, sq67));
    return reduce_add_epi64_avx2(acc64);
  } else {
    int r = size;
    __m256i acc64 = _mm256_setzero_si256();
    assert(size % 16 == 0);

    do {
      int c = 0;
      do {
        const int16_t *const b = src + c;
        const __m256i v0 =
            _mm256_loadu_si256((const __m256i *)(b + 0 * stride));
        const __m256i v1 =
            _mm256_loadu_si256((const __m256i *)(b + 1 * stride));
        const __m256i v2 =
            _mm256_loadu_si256((const __m256i *)(b + 2 * stride));
        const __m256i v3 =
            _mm256_loadu_si256((const __m256i *)(b + 3 * stride));

        const __m256i sq0 = _mm256_madd_epi16(v0, v0);
        const __m256i sq1 = _mm256_madd_epi16(v1, v1);
        const __m256i sq2 = _mm256_madd_epi16(v2, v2);
        const __m256i sq3 = _mm256_madd_epi16(v3, v3);

        acc64 = widen_add_u32_to_u64_avx2(acc64, _mm256_add_epi32(sq0, sq1));
        acc64 = widen_add_u32_to_u64_avx2(acc64, _mm256_add_epi32(sq2, sq3));
        c += 16;
      } while (c < size);

      src += 4 * stride;
      r -= 4;
    } while (r > 0);

    return reduce_add_epi64_avx2(acc64);
  }
}
