/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <assert.h>
#include <immintrin.h>  // AVX2

#include "./vpx_dsp_rtcd.h"

/* clang-format off */
DECLARE_ALIGNED(32, static const uint8_t, bilinear_filters_avx2[512]) = {
  16, 0,  16, 0,  16, 0,  16, 0,  16, 0,  16, 0,  16, 0,  16, 0,
  16, 0,  16, 0,  16, 0,  16, 0,  16, 0,  16, 0,  16, 0,  16, 0,
  14, 2,  14, 2,  14, 2,  14, 2,  14, 2,  14, 2,  14, 2,  14, 2,
  14, 2,  14, 2,  14, 2,  14, 2,  14, 2,  14, 2,  14, 2,  14, 2,
  12, 4,  12, 4,  12, 4,  12, 4,  12, 4,  12, 4,  12, 4,  12, 4,
  12, 4,  12, 4,  12, 4,  12, 4,  12, 4,  12, 4,  12, 4,  12, 4,
  10, 6,  10, 6,  10, 6,  10, 6,  10, 6,  10, 6,  10, 6,  10, 6,
  10, 6,  10, 6,  10, 6,  10, 6,  10, 6,  10, 6,  10, 6,  10, 6,
  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
  6,  10, 6,  10, 6,  10, 6,  10, 6,  10, 6,  10, 6,  10, 6,  10,
  6,  10, 6,  10, 6,  10, 6,  10, 6,  10, 6,  10, 6,  10, 6,  10,
  4,  12, 4,  12, 4,  12, 4,  12, 4,  12, 4,  12, 4,  12, 4,  12,
  4,  12, 4,  12, 4,  12, 4,  12, 4,  12, 4,  12, 4,  12, 4,  12,
  2,  14, 2,  14, 2,  14, 2,  14, 2,  14, 2,  14, 2,  14, 2,  14,
  2,  14, 2,  14, 2,  14, 2,  14, 2,  14, 2,  14, 2,  14, 2,  14,
};

DECLARE_ALIGNED(32, static const int8_t, adjacent_sub_avx2[32]) = {
  1, -1,  1, -1,  1, -1,  1, -1,  1, -1,  1, -1,  1, -1,  1, -1,
  1, -1,  1, -1,  1, -1,  1, -1,  1, -1,  1, -1,  1, -1,  1, -1
};
/* clang-format on */

static INLINE void variance_kernel_avx2(const __m256i src, const __m256i ref,
                                        __m256i *const sse,
                                        __m256i *const sum) {
  const __m256i adj_sub = _mm256_load_si256((__m256i const *)adjacent_sub_avx2);

  // unpack into pairs of source and reference values
  const __m256i src_ref0 = _mm256_unpacklo_epi8(src, ref);
  const __m256i src_ref1 = _mm256_unpackhi_epi8(src, ref);

  // subtract adjacent elements using src*1 + ref*-1
  const __m256i diff0 = _mm256_maddubs_epi16(src_ref0, adj_sub);
  const __m256i diff1 = _mm256_maddubs_epi16(src_ref1, adj_sub);
  const __m256i madd0 = _mm256_madd_epi16(diff0, diff0);
  const __m256i madd1 = _mm256_madd_epi16(diff1, diff1);

  // add to the running totals
  *sum = _mm256_add_epi16(*sum, _mm256_add_epi16(diff0, diff1));
  *sse = _mm256_add_epi32(*sse, _mm256_add_epi32(madd0, madd1));
}

static INLINE void variance_final_from_32bit_sum_avx2(__m256i vsse,
                                                      __m128i vsum,
                                                      unsigned int *const sse,
                                                      int *const sum) {
  // extract the low lane and add it to the high lane
  const __m128i sse_reg_128 = _mm_add_epi32(_mm256_castsi256_si128(vsse),
                                            _mm256_extractf128_si256(vsse, 1));

  // unpack sse and sum registers and add
  const __m128i sse_sum_lo = _mm_unpacklo_epi32(sse_reg_128, vsum);
  const __m128i sse_sum_hi = _mm_unpackhi_epi32(sse_reg_128, vsum);
  const __m128i sse_sum = _mm_add_epi32(sse_sum_lo, sse_sum_hi);

  // perform the final summation and extract the results
  const __m128i res = _mm_add_epi32(sse_sum, _mm_srli_si128(sse_sum, 8));
  *((int *)sse) = _mm_cvtsi128_si32(res);
  *((int *)sum) = _mm_extract_epi32(res, 1);
}

static INLINE void variance_final_from_16bit_sum_avx2(__m256i vsse,
                                                      __m256i vsum,
                                                      unsigned int *const sse,
                                                      int *const sum) {
  // extract the low lane and add it to the high lane
  const __m128i sum_reg_128 = _mm_add_epi16(_mm256_castsi256_si128(vsum),
                                            _mm256_extractf128_si256(vsum, 1));
  const __m128i sum_reg_64 =
      _mm_add_epi16(sum_reg_128, _mm_srli_si128(sum_reg_128, 8));
  const __m128i sum_int32 = _mm_cvtepi16_epi32(sum_reg_64);

  variance_final_from_32bit_sum_avx2(vsse, sum_int32, sse, sum);
}

static INLINE __m256i sum_to_32bit_avx2(const __m256i sum) {
  const __m256i sum_lo = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(sum));
  const __m256i sum_hi =
      _mm256_cvtepi16_epi32(_mm256_extractf128_si256(sum, 1));
  return _mm256_add_epi32(sum_lo, sum_hi);
}

static INLINE void variance8_kernel_avx2(
    const uint8_t *const src, const int src_stride, const uint8_t *const ref,
    const int ref_stride, __m256i *const sse, __m256i *const sum) {
  __m128i src0, src1, ref0, ref1;
  __m256i ss, rr, diff;

  // 0 0 0.... 0 s07 s06 s05 s04 s03 s02 s01 s00
  src0 = _mm_loadl_epi64((const __m128i *)(src + 0 * src_stride));

  // 0 0 0.... 0 s17 s16 s15 s14 s13 s12 s11 s10
  src1 = _mm_loadl_epi64((const __m128i *)(src + 1 * src_stride));

  // s17 s16...s11 s10 s07 s06...s01 s00 (8bit)
  src0 = _mm_unpacklo_epi64(src0, src1);

  // s17 s16...s11 s10 s07 s06...s01 s00 (16 bit)
  ss = _mm256_cvtepu8_epi16(src0);

  // 0 0 0.... 0 r07 r06 r05 r04 r03 r02 r01 r00
  ref0 = _mm_loadl_epi64((const __m128i *)(ref + 0 * ref_stride));

  // 0 0 0.... 0 r17 r16 0 r15 0 r14 0 r13 0 r12 0 r11 0 r10
  ref1 = _mm_loadl_epi64((const __m128i *)(ref + 1 * ref_stride));

  // r17 r16...r11 r10 r07 r06...r01 r00 (8 bit)
  ref0 = _mm_unpacklo_epi64(ref0, ref1);

  // r17 r16...r11 r10 r07 r06...r01 r00 (16 bit)
  rr = _mm256_cvtepu8_epi16(ref0);

  diff = _mm256_sub_epi16(ss, rr);
  *sse = _mm256_add_epi32(*sse, _mm256_madd_epi16(diff, diff));
  *sum = _mm256_add_epi16(*sum, diff);
}

static INLINE void variance16_kernel_avx2(
    const uint8_t *const src, const int src_stride, const uint8_t *const ref,
    const int ref_stride, __m256i *const sse, __m256i *const sum) {
  const __m128i s0 = _mm_loadu_si128((__m128i const *)(src + 0 * src_stride));
  const __m128i s1 = _mm_loadu_si128((__m128i const *)(src + 1 * src_stride));
  const __m128i r0 = _mm_loadu_si128((__m128i const *)(ref + 0 * ref_stride));
  const __m128i r1 = _mm_loadu_si128((__m128i const *)(ref + 1 * ref_stride));
  const __m256i s = _mm256_inserti128_si256(_mm256_castsi128_si256(s0), s1, 1);
  const __m256i r = _mm256_inserti128_si256(_mm256_castsi128_si256(r0), r1, 1);
  variance_kernel_avx2(s, r, sse, sum);
}

static INLINE void variance32_kernel_avx2(const uint8_t *const src,
                                          const uint8_t *const ref,
                                          __m256i *const sse,
                                          __m256i *const sum) {
  const __m256i s = _mm256_loadu_si256((__m256i const *)(src));
  const __m256i r = _mm256_loadu_si256((__m256i const *)(ref));
  variance_kernel_avx2(s, r, sse, sum);
}

static INLINE void variance8_avx2(const uint8_t *src, const int src_stride,
                                  const uint8_t *ref, const int ref_stride,
                                  const int h, __m256i *const vsse,
                                  __m256i *const vsum) {
  int i;
  *vsum = _mm256_setzero_si256();
  *vsse = _mm256_setzero_si256();

  for (i = 0; i < h; i += 2) {
    variance8_kernel_avx2(src, src_stride, ref, ref_stride, vsse, vsum);
    src += 2 * src_stride;
    ref += 2 * ref_stride;
  }
}

static INLINE void variance16_avx2(const uint8_t *src, const int src_stride,
                                   const uint8_t *ref, const int ref_stride,
                                   const int h, __m256i *const vsse,
                                   __m256i *const vsum) {
  int i;
  *vsum = _mm256_setzero_si256();
  *vsse = _mm256_setzero_si256();

  for (i = 0; i < h; i += 2) {
    variance16_kernel_avx2(src, src_stride, ref, ref_stride, vsse, vsum);
    src += 2 * src_stride;
    ref += 2 * ref_stride;
  }
}

static INLINE void variance32_avx2(const uint8_t *src, const int src_stride,
                                   const uint8_t *ref, const int ref_stride,
                                   const int h, __m256i *const vsse,
                                   __m256i *const vsum) {
  int i;
  *vsum = _mm256_setzero_si256();
  *vsse = _mm256_setzero_si256();

  for (i = 0; i < h; i++) {
    variance32_kernel_avx2(src, ref, vsse, vsum);
    src += src_stride;
    ref += ref_stride;
  }
}

static INLINE void variance64_avx2(const uint8_t *src, const int src_stride,
                                   const uint8_t *ref, const int ref_stride,
                                   const int h, __m256i *const vsse,
                                   __m256i *const vsum) {
  int i;
  *vsum = _mm256_setzero_si256();

  for (i = 0; i < h; i++) {
    variance32_kernel_avx2(src + 0, ref + 0, vsse, vsum);
    variance32_kernel_avx2(src + 32, ref + 32, vsse, vsum);
    src += src_stride;
    ref += ref_stride;
  }
}

void vpx_get16x16var_avx2(const uint8_t *src_ptr, int src_stride,
                          const uint8_t *ref_ptr, int ref_stride,
                          unsigned int *sse, int *sum) {
  __m256i vsse, vsum;
  variance16_avx2(src_ptr, src_stride, ref_ptr, ref_stride, 16, &vsse, &vsum);
  variance_final_from_16bit_sum_avx2(vsse, vsum, sse, sum);
}

#define FILTER_SRC(filter)                               \
  /* filter the source */                                \
  exp_src_lo = _mm256_maddubs_epi16(exp_src_lo, filter); \
  exp_src_hi = _mm256_maddubs_epi16(exp_src_hi, filter); \
                                                         \
  /* add 8 to source */                                  \
  exp_src_lo = _mm256_add_epi16(exp_src_lo, pw8);        \
  exp_src_hi = _mm256_add_epi16(exp_src_hi, pw8);        \
                                                         \
  /* divide source by 16 */                              \
  exp_src_lo = _mm256_srai_epi16(exp_src_lo, 4);         \
  exp_src_hi = _mm256_srai_epi16(exp_src_hi, 4);

#define CALC_SUM_SSE_INSIDE_LOOP                          \
  /* expand each byte to 2 bytes */                       \
  exp_dst_lo = _mm256_unpacklo_epi8(dst_reg, zero_reg);   \
  exp_dst_hi = _mm256_unpackhi_epi8(dst_reg, zero_reg);   \
  /* source - dest */                                     \
  exp_src_lo = _mm256_sub_epi16(exp_src_lo, exp_dst_lo);  \
  exp_src_hi = _mm256_sub_epi16(exp_src_hi, exp_dst_hi);  \
  /* caculate sum */                                      \
  *sum_reg = _mm256_add_epi16(*sum_reg, exp_src_lo);      \
  exp_src_lo = _mm256_madd_epi16(exp_src_lo, exp_src_lo); \
  *sum_reg = _mm256_add_epi16(*sum_reg, exp_src_hi);      \
  exp_src_hi = _mm256_madd_epi16(exp_src_hi, exp_src_hi); \
  /* calculate sse */                                     \
  *sse_reg = _mm256_add_epi32(*sse_reg, exp_src_lo);      \
  *sse_reg = _mm256_add_epi32(*sse_reg, exp_src_hi);

// final calculation to sum and sse
#define CALC_SUM_AND_SSE                                                   \
  res_cmp = _mm256_cmpgt_epi16(zero_reg, sum_reg);                         \
  sse_reg_hi = _mm256_srli_si256(sse_reg, 8);                              \
  sum_reg_lo = _mm256_unpacklo_epi16(sum_reg, res_cmp);                    \
  sum_reg_hi = _mm256_unpackhi_epi16(sum_reg, res_cmp);                    \
  sse_reg = _mm256_add_epi32(sse_reg, sse_reg_hi);                         \
  sum_reg = _mm256_add_epi32(sum_reg_lo, sum_reg_hi);                      \
                                                                           \
  sse_reg_hi = _mm256_srli_si256(sse_reg, 4);                              \
  sum_reg_hi = _mm256_srli_si256(sum_reg, 8);                              \
                                                                           \
  sse_reg = _mm256_add_epi32(sse_reg, sse_reg_hi);                         \
  sum_reg = _mm256_add_epi32(sum_reg, sum_reg_hi);                         \
  *((int *)sse) = _mm_cvtsi128_si32(_mm256_castsi256_si128(sse_reg)) +     \
                  _mm_cvtsi128_si32(_mm256_extractf128_si256(sse_reg, 1)); \
  sum_reg_hi = _mm256_srli_si256(sum_reg, 4);                              \
  sum_reg = _mm256_add_epi32(sum_reg, sum_reg_hi);                         \
  sum = _mm_cvtsi128_si32(_mm256_castsi256_si128(sum_reg)) +               \
        _mm_cvtsi128_si32(_mm256_extractf128_si256(sum_reg, 1));

static INLINE void spv32_x0_y0(const uint8_t *src, int src_stride,
                               const uint8_t *dst, int dst_stride,
                               const uint8_t *second_pred, int second_stride,
                               int do_sec, int height, __m256i *sum_reg,
                               __m256i *sse_reg) {
  const __m256i zero_reg = _mm256_setzero_si256();
  __m256i exp_src_lo, exp_src_hi, exp_dst_lo, exp_dst_hi;
  int i;
  for (i = 0; i < height; i++) {
    const __m256i dst_reg = _mm256_loadu_si256((__m256i const *)dst);
    const __m256i src_reg = _mm256_loadu_si256((__m256i const *)src);
    if (do_sec) {
      const __m256i sec_reg = _mm256_loadu_si256((__m256i const *)second_pred);
      const __m256i avg_reg = _mm256_avg_epu8(src_reg, sec_reg);
      exp_src_lo = _mm256_unpacklo_epi8(avg_reg, zero_reg);
      exp_src_hi = _mm256_unpackhi_epi8(avg_reg, zero_reg);
      second_pred += second_stride;
    } else {
      exp_src_lo = _mm256_unpacklo_epi8(src_reg, zero_reg);
      exp_src_hi = _mm256_unpackhi_epi8(src_reg, zero_reg);
    }
    CALC_SUM_SSE_INSIDE_LOOP
    src += src_stride;
    dst += dst_stride;
  }
}

// (x == 0, y == 4) or (x == 4, y == 0).  sstep determines the direction.
static INLINE void spv32_half_zero(const uint8_t *src, int src_stride,
                                   const uint8_t *dst, int dst_stride,
                                   const uint8_t *second_pred,
                                   int second_stride, int do_sec, int height,
                                   __m256i *sum_reg, __m256i *sse_reg,
                                   int sstep) {
  const __m256i zero_reg = _mm256_setzero_si256();
  __m256i exp_src_lo, exp_src_hi, exp_dst_lo, exp_dst_hi;
  int i;
  for (i = 0; i < height; i++) {
    const __m256i dst_reg = _mm256_loadu_si256((__m256i const *)dst);
    const __m256i src_0 = _mm256_loadu_si256((__m256i const *)src);
    const __m256i src_1 = _mm256_loadu_si256((__m256i const *)(src + sstep));
    const __m256i src_avg = _mm256_avg_epu8(src_0, src_1);
    if (do_sec) {
      const __m256i sec_reg = _mm256_loadu_si256((__m256i const *)second_pred);
      const __m256i avg_reg = _mm256_avg_epu8(src_avg, sec_reg);
      exp_src_lo = _mm256_unpacklo_epi8(avg_reg, zero_reg);
      exp_src_hi = _mm256_unpackhi_epi8(avg_reg, zero_reg);
      second_pred += second_stride;
    } else {
      exp_src_lo = _mm256_unpacklo_epi8(src_avg, zero_reg);
      exp_src_hi = _mm256_unpackhi_epi8(src_avg, zero_reg);
    }
    CALC_SUM_SSE_INSIDE_LOOP
    src += src_stride;
    dst += dst_stride;
  }
}

static INLINE void spv32_x0_y4(const uint8_t *src, int src_stride,
                               const uint8_t *dst, int dst_stride,
                               const uint8_t *second_pred, int second_stride,
                               int do_sec, int height, __m256i *sum_reg,
                               __m256i *sse_reg) {
  spv32_half_zero(src, src_stride, dst, dst_stride, second_pred, second_stride,
                  do_sec, height, sum_reg, sse_reg, src_stride);
}

static INLINE void spv32_x4_y0(const uint8_t *src, int src_stride,
                               const uint8_t *dst, int dst_stride,
                               const uint8_t *second_pred, int second_stride,
                               int do_sec, int height, __m256i *sum_reg,
                               __m256i *sse_reg) {
  spv32_half_zero(src, src_stride, dst, dst_stride, second_pred, second_stride,
                  do_sec, height, sum_reg, sse_reg, 1);
}

static INLINE void spv32_x4_y4(const uint8_t *src, int src_stride,
                               const uint8_t *dst, int dst_stride,
                               const uint8_t *second_pred, int second_stride,
                               int do_sec, int height, __m256i *sum_reg,
                               __m256i *sse_reg) {
  const __m256i zero_reg = _mm256_setzero_si256();
  const __m256i src_a = _mm256_loadu_si256((__m256i const *)src);
  const __m256i src_b = _mm256_loadu_si256((__m256i const *)(src + 1));
  __m256i prev_src_avg = _mm256_avg_epu8(src_a, src_b);
  __m256i exp_src_lo, exp_src_hi, exp_dst_lo, exp_dst_hi;
  int i;
  src += src_stride;
  for (i = 0; i < height; i++) {
    const __m256i dst_reg = _mm256_loadu_si256((__m256i const *)dst);
    const __m256i src_0 = _mm256_loadu_si256((__m256i const *)(src));
    const __m256i src_1 = _mm256_loadu_si256((__m256i const *)(src + 1));
    const __m256i src_avg = _mm256_avg_epu8(src_0, src_1);
    const __m256i current_avg = _mm256_avg_epu8(prev_src_avg, src_avg);
    prev_src_avg = src_avg;

    if (do_sec) {
      const __m256i sec_reg = _mm256_loadu_si256((__m256i const *)second_pred);
      const __m256i avg_reg = _mm256_avg_epu8(current_avg, sec_reg);
      exp_src_lo = _mm256_unpacklo_epi8(avg_reg, zero_reg);
      exp_src_hi = _mm256_unpackhi_epi8(avg_reg, zero_reg);
      second_pred += second_stride;
    } else {
      exp_src_lo = _mm256_unpacklo_epi8(current_avg, zero_reg);
      exp_src_hi = _mm256_unpackhi_epi8(current_avg, zero_reg);
    }
    // save current source average
    CALC_SUM_SSE_INSIDE_LOOP
    dst += dst_stride;
    src += src_stride;
  }
}

// (x == 0, y == bil) or (x == 4, y == bil).  sstep determines the direction.
static INLINE void spv32_bilin_zero(const uint8_t *src, int src_stride,
                                    const uint8_t *dst, int dst_stride,
                                    const uint8_t *second_pred,
                                    int second_stride, int do_sec, int height,
                                    __m256i *sum_reg, __m256i *sse_reg,
                                    int offset, int sstep) {
  const __m256i zero_reg = _mm256_setzero_si256();
  const __m256i pw8 = _mm256_set1_epi16(8);
  const __m256i filter = _mm256_load_si256(
      (__m256i const *)(bilinear_filters_avx2 + (offset << 5)));
  __m256i exp_src_lo, exp_src_hi, exp_dst_lo, exp_dst_hi;
  int i;
  for (i = 0; i < height; i++) {
    const __m256i dst_reg = _mm256_loadu_si256((__m256i const *)dst);
    const __m256i src_0 = _mm256_loadu_si256((__m256i const *)src);
    const __m256i src_1 = _mm256_loadu_si256((__m256i const *)(src + sstep));
    exp_src_lo = _mm256_unpacklo_epi8(src_0, src_1);
    exp_src_hi = _mm256_unpackhi_epi8(src_0, src_1);

    FILTER_SRC(filter)
    if (do_sec) {
      const __m256i sec_reg = _mm256_loadu_si256((__m256i const *)second_pred);
      const __m256i exp_src = _mm256_packus_epi16(exp_src_lo, exp_src_hi);
      const __m256i avg_reg = _mm256_avg_epu8(exp_src, sec_reg);
      second_pred += second_stride;
      exp_src_lo = _mm256_unpacklo_epi8(avg_reg, zero_reg);
      exp_src_hi = _mm256_unpackhi_epi8(avg_reg, zero_reg);
    }
    CALC_SUM_SSE_INSIDE_LOOP
    src += src_stride;
    dst += dst_stride;
  }
}

static INLINE void spv32_x0_yb(const uint8_t *src, int src_stride,
                               const uint8_t *dst, int dst_stride,
                               const uint8_t *second_pred, int second_stride,
                               int do_sec, int height, __m256i *sum_reg,
                               __m256i *sse_reg, int y_offset) {
  spv32_bilin_zero(src, src_stride, dst, dst_stride, second_pred, second_stride,
                   do_sec, height, sum_reg, sse_reg, y_offset, src_stride);
}

static INLINE void spv32_xb_y0(const uint8_t *src, int src_stride,
                               const uint8_t *dst, int dst_stride,
                               const uint8_t *second_pred, int second_stride,
                               int do_sec, int height, __m256i *sum_reg,
                               __m256i *sse_reg, int x_offset) {
  spv32_bilin_zero(src, src_stride, dst, dst_stride, second_pred, second_stride,
                   do_sec, height, sum_reg, sse_reg, x_offset, 1);
}

static INLINE void spv32_x4_yb(const uint8_t *src, int src_stride,
                               const uint8_t *dst, int dst_stride,
                               const uint8_t *second_pred, int second_stride,
                               int do_sec, int height, __m256i *sum_reg,
                               __m256i *sse_reg, int y_offset) {
  const __m256i zero_reg = _mm256_setzero_si256();
  const __m256i pw8 = _mm256_set1_epi16(8);
  const __m256i filter = _mm256_load_si256(
      (__m256i const *)(bilinear_filters_avx2 + (y_offset << 5)));
  const __m256i src_a = _mm256_loadu_si256((__m256i const *)src);
  const __m256i src_b = _mm256_loadu_si256((__m256i const *)(src + 1));
  __m256i prev_src_avg = _mm256_avg_epu8(src_a, src_b);
  __m256i exp_src_lo, exp_src_hi, exp_dst_lo, exp_dst_hi;
  int i;
  src += src_stride;
  for (i = 0; i < height; i++) {
    const __m256i dst_reg = _mm256_loadu_si256((__m256i const *)dst);
    const __m256i src_0 = _mm256_loadu_si256((__m256i const *)src);
    const __m256i src_1 = _mm256_loadu_si256((__m256i const *)(src + 1));
    const __m256i src_avg = _mm256_avg_epu8(src_0, src_1);
    exp_src_lo = _mm256_unpacklo_epi8(prev_src_avg, src_avg);
    exp_src_hi = _mm256_unpackhi_epi8(prev_src_avg, src_avg);
    prev_src_avg = src_avg;

    FILTER_SRC(filter)
    if (do_sec) {
      const __m256i sec_reg = _mm256_loadu_si256((__m256i const *)second_pred);
      const __m256i exp_src_avg = _mm256_packus_epi16(exp_src_lo, exp_src_hi);
      const __m256i avg_reg = _mm256_avg_epu8(exp_src_avg, sec_reg);
      exp_src_lo = _mm256_unpacklo_epi8(avg_reg, zero_reg);
      exp_src_hi = _mm256_unpackhi_epi8(avg_reg, zero_reg);
      second_pred += second_stride;
    }
    CALC_SUM_SSE_INSIDE_LOOP
    dst += dst_stride;
    src += src_stride;
  }
}

static INLINE void spv32_xb_y4(const uint8_t *src, int src_stride,
                               const uint8_t *dst, int dst_stride,
                               const uint8_t *second_pred, int second_stride,
                               int do_sec, int height, __m256i *sum_reg,
                               __m256i *sse_reg, int x_offset) {
  const __m256i zero_reg = _mm256_setzero_si256();
  const __m256i pw8 = _mm256_set1_epi16(8);
  const __m256i filter = _mm256_load_si256(
      (__m256i const *)(bilinear_filters_avx2 + (x_offset << 5)));
  const __m256i src_a = _mm256_loadu_si256((__m256i const *)src);
  const __m256i src_b = _mm256_loadu_si256((__m256i const *)(src + 1));
  __m256i exp_src_lo, exp_src_hi, exp_dst_lo, exp_dst_hi;
  __m256i src_reg, src_pack;
  int i;
  exp_src_lo = _mm256_unpacklo_epi8(src_a, src_b);
  exp_src_hi = _mm256_unpackhi_epi8(src_a, src_b);
  FILTER_SRC(filter)
  // convert each 16 bit to 8 bit to each low and high lane source
  src_pack = _mm256_packus_epi16(exp_src_lo, exp_src_hi);

  src += src_stride;
  for (i = 0; i < height; i++) {
    const __m256i dst_reg = _mm256_loadu_si256((__m256i const *)dst);
    const __m256i src_0 = _mm256_loadu_si256((__m256i const *)src);
    const __m256i src_1 = _mm256_loadu_si256((__m256i const *)(src + 1));
    exp_src_lo = _mm256_unpacklo_epi8(src_0, src_1);
    exp_src_hi = _mm256_unpackhi_epi8(src_0, src_1);

    FILTER_SRC(filter)

    src_reg = _mm256_packus_epi16(exp_src_lo, exp_src_hi);
    // average between previous pack to the current
    src_pack = _mm256_avg_epu8(src_pack, src_reg);

    if (do_sec) {
      const __m256i sec_reg = _mm256_loadu_si256((__m256i const *)second_pred);
      const __m256i avg_pack = _mm256_avg_epu8(src_pack, sec_reg);
      exp_src_lo = _mm256_unpacklo_epi8(avg_pack, zero_reg);
      exp_src_hi = _mm256_unpackhi_epi8(avg_pack, zero_reg);
      second_pred += second_stride;
    } else {
      exp_src_lo = _mm256_unpacklo_epi8(src_pack, zero_reg);
      exp_src_hi = _mm256_unpackhi_epi8(src_pack, zero_reg);
    }
    CALC_SUM_SSE_INSIDE_LOOP
    src_pack = src_reg;
    dst += dst_stride;
    src += src_stride;
  }
}

static INLINE void spv32_xb_yb(const uint8_t *src, int src_stride,
                               const uint8_t *dst, int dst_stride,
                               const uint8_t *second_pred, int second_stride,
                               int do_sec, int height, __m256i *sum_reg,
                               __m256i *sse_reg, int x_offset, int y_offset) {
  const __m256i zero_reg = _mm256_setzero_si256();
  const __m256i pw8 = _mm256_set1_epi16(8);
  const __m256i xfilter = _mm256_load_si256(
      (__m256i const *)(bilinear_filters_avx2 + (x_offset << 5)));
  const __m256i yfilter = _mm256_load_si256(
      (__m256i const *)(bilinear_filters_avx2 + (y_offset << 5)));
  const __m256i src_a = _mm256_loadu_si256((__m256i const *)src);
  const __m256i src_b = _mm256_loadu_si256((__m256i const *)(src + 1));
  __m256i exp_src_lo, exp_src_hi, exp_dst_lo, exp_dst_hi;
  __m256i prev_src_pack, src_pack;
  int i;
  exp_src_lo = _mm256_unpacklo_epi8(src_a, src_b);
  exp_src_hi = _mm256_unpackhi_epi8(src_a, src_b);
  FILTER_SRC(xfilter)
  // convert each 16 bit to 8 bit to each low and high lane source
  prev_src_pack = _mm256_packus_epi16(exp_src_lo, exp_src_hi);
  src += src_stride;

  for (i = 0; i < height; i++) {
    const __m256i dst_reg = _mm256_loadu_si256((__m256i const *)dst);
    const __m256i src_0 = _mm256_loadu_si256((__m256i const *)src);
    const __m256i src_1 = _mm256_loadu_si256((__m256i const *)(src + 1));
    exp_src_lo = _mm256_unpacklo_epi8(src_0, src_1);
    exp_src_hi = _mm256_unpackhi_epi8(src_0, src_1);

    FILTER_SRC(xfilter)
    src_pack = _mm256_packus_epi16(exp_src_lo, exp_src_hi);

    // merge previous pack to current pack source
    exp_src_lo = _mm256_unpacklo_epi8(prev_src_pack, src_pack);
    exp_src_hi = _mm256_unpackhi_epi8(prev_src_pack, src_pack);

    FILTER_SRC(yfilter)
    if (do_sec) {
      const __m256i sec_reg = _mm256_loadu_si256((__m256i const *)second_pred);
      const __m256i exp_src = _mm256_packus_epi16(exp_src_lo, exp_src_hi);
      const __m256i avg_reg = _mm256_avg_epu8(exp_src, sec_reg);
      exp_src_lo = _mm256_unpacklo_epi8(avg_reg, zero_reg);
      exp_src_hi = _mm256_unpackhi_epi8(avg_reg, zero_reg);
      second_pred += second_stride;
    }

    prev_src_pack = src_pack;

    CALC_SUM_SSE_INSIDE_LOOP
    dst += dst_stride;
    src += src_stride;
  }
}

static INLINE int sub_pix_var32xh(const uint8_t *src, int src_stride,
                                  int x_offset, int y_offset,
                                  const uint8_t *dst, int dst_stride,
                                  const uint8_t *second_pred, int second_stride,
                                  int do_sec, int height, unsigned int *sse) {
  const __m256i zero_reg = _mm256_setzero_si256();
  __m256i sum_reg = _mm256_setzero_si256();
  __m256i sse_reg = _mm256_setzero_si256();
  __m256i sse_reg_hi, res_cmp, sum_reg_lo, sum_reg_hi;
  int sum;
  // sum_reg accumulates 2 * height signed diffs in [-255, 255] per int16_t
  // lane, so height <= 64 keeps each lane within [-32640, 32640] < INT16_MAX.
  assert(height >= 1 && height <= 64);
  assert(x_offset >= 0 && x_offset < 8);
  assert(y_offset >= 0 && y_offset < 8);
  // x_offset = 0 and y_offset = 0
  if (x_offset == 0) {
    if (y_offset == 0) {
      spv32_x0_y0(src, src_stride, dst, dst_stride, second_pred, second_stride,
                  do_sec, height, &sum_reg, &sse_reg);
      // x_offset = 0 and y_offset = 4
    } else if (y_offset == 4) {
      spv32_x0_y4(src, src_stride, dst, dst_stride, second_pred, second_stride,
                  do_sec, height, &sum_reg, &sse_reg);
      // x_offset = 0 and y_offset = bilin interpolation
    } else {
      spv32_x0_yb(src, src_stride, dst, dst_stride, second_pred, second_stride,
                  do_sec, height, &sum_reg, &sse_reg, y_offset);
    }
    // x_offset = 4  and y_offset = 0
  } else if (x_offset == 4) {
    if (y_offset == 0) {
      spv32_x4_y0(src, src_stride, dst, dst_stride, second_pred, second_stride,
                  do_sec, height, &sum_reg, &sse_reg);
      // x_offset = 4  and y_offset = 4
    } else if (y_offset == 4) {
      spv32_x4_y4(src, src_stride, dst, dst_stride, second_pred, second_stride,
                  do_sec, height, &sum_reg, &sse_reg);
      // x_offset = 4  and y_offset = bilin interpolation
    } else {
      spv32_x4_yb(src, src_stride, dst, dst_stride, second_pred, second_stride,
                  do_sec, height, &sum_reg, &sse_reg, y_offset);
    }
    // x_offset = bilin interpolation and y_offset = 0
  } else {
    if (y_offset == 0) {
      spv32_xb_y0(src, src_stride, dst, dst_stride, second_pred, second_stride,
                  do_sec, height, &sum_reg, &sse_reg, x_offset);
      // x_offset = bilin interpolation and y_offset = 4
    } else if (y_offset == 4) {
      spv32_xb_y4(src, src_stride, dst, dst_stride, second_pred, second_stride,
                  do_sec, height, &sum_reg, &sse_reg, x_offset);
      // x_offset = bilin interpolation and y_offset = bilin interpolation
    } else {
      spv32_xb_yb(src, src_stride, dst, dst_stride, second_pred, second_stride,
                  do_sec, height, &sum_reg, &sse_reg, x_offset, y_offset);
    }
  }
  CALC_SUM_AND_SSE
  return sum;
}

static int sub_pixel_variance32xh_avx2(const uint8_t *src, int src_stride,
                                       int x_offset, int y_offset,
                                       const uint8_t *dst, int dst_stride,
                                       int height, unsigned int *sse) {
  return sub_pix_var32xh(src, src_stride, x_offset, y_offset, dst, dst_stride,
                         NULL, 0, 0, height, sse);
}

static int sub_pixel_avg_variance32xh_avx2(const uint8_t *src, int src_stride,
                                           int x_offset, int y_offset,
                                           const uint8_t *dst, int dst_stride,
                                           const uint8_t *second_pred,
                                           int second_stride, int height,
                                           unsigned int *sse) {
  return sub_pix_var32xh(src, src_stride, x_offset, y_offset, dst, dst_stride,
                         second_pred, second_stride, 1, height, sse);
}

// Load two 16-byte rows into the low and high 128-bit lanes of a YMM register.
static INLINE __m256i load_two_rows16(const uint8_t *src, int stride) {
  const __m128i row0 = _mm_loadu_si128((const __m128i *)src);
  const __m128i row1 = _mm_loadu_si128((const __m128i *)(src + stride));
  return _mm256_inserti128_si256(_mm256_castsi128_si256(row0), row1, 1);
}

// Broadcast a 128-bit row into both lanes without relying on the
// _mm256_broadcastsi128_si256 intrinsic on older toolchains.
static INLINE __m256i dup_row16(const __m128i row) {
  return _mm256_inserti128_si256(_mm256_castsi128_si256(row), row, 1);
}

// Bilinearly interpolate packed uint8_t pairs (src_a, src_b) using 16-scale
// taps in *filter_ptr (t0 + t1 == 16; max maddubs = 255 * 16 = 4080).
static INLINE __m256i bilin_filter16_avx2(const __m256i src_a,
                                          const __m256i src_b,
                                          const __m256i *const filter_ptr) {
  const __m256i filter = *filter_ptr;
  const __m256i round = _mm256_set1_epi16(8);
  __m256i low =
      _mm256_maddubs_epi16(_mm256_unpacklo_epi8(src_a, src_b), filter);
  __m256i high =
      _mm256_maddubs_epi16(_mm256_unpackhi_epi8(src_a, src_b), filter);
  low = _mm256_srai_epi16(_mm256_add_epi16(low, round), 4);
  high = _mm256_srai_epi16(_mm256_add_epi16(high, round), 4);
  return _mm256_packus_epi16(low, high);
}

// Horizontally filter row 0 (16 bytes) and replicate it into both YMM lanes.
// Takes x_offset instead of a __m256i parameter to avoid 32-bit MSVC C2719.
static INLINE __m256i filter_row0_16_dup(const uint8_t *src, int x_offset) {
  const __m128i filter = _mm_load_si128(
      (const __m128i *)(bilinear_filters_avx2 + (x_offset << 5)));
  const __m128i round = _mm_set1_epi16(8);
  const __m128i src_a = _mm_loadu_si128((const __m128i *)src);
  const __m128i src_b = _mm_loadu_si128((const __m128i *)(src + 1));
  __m128i low = _mm_maddubs_epi16(_mm_unpacklo_epi8(src_a, src_b), filter);
  __m128i high = _mm_maddubs_epi16(_mm_unpackhi_epi8(src_a, src_b), filter);
  low = _mm_srai_epi16(_mm_add_epi16(low, round), 4);
  high = _mm_srai_epi16(_mm_add_epi16(high, round), 4);
  return dup_row16(_mm_packus_epi16(low, high));
}

// Optionally average two packed 16-byte predicted rows with `second_pred`
// (contiguous 16xH layout) and accumulate into 8x uint32_t `sse` and 16x
// int16_t `sum`.
static INLINE void spv16_accum_avx2(const __m256i pred, const uint8_t *dst,
                                    int dst_stride, const uint8_t *second_pred,
                                    int do_sec, __m256i *const sse,
                                    __m256i *const sum) {
  const __m256i dst_reg = load_two_rows16(dst, dst_stride);
  const __m256i final_pred =
      do_sec ? _mm256_avg_epu8(pred,
                               _mm256_loadu_si256((const __m256i *)second_pred))
             : pred;
  variance_kernel_avx2(final_pred, dst_reg, sse, sum);
}

// Compute sum and SSE for a 16xH block (even height in [2, 32]) by processing
// two rows per YMM register and carrying the previous row across 128-bit lanes
// so at most height + 1 rows of `src` are ever read.
// Dynamic range for H <= 32:
//   - per-pixel diff in [-255, 255]
//   - `sum` has 16x int16_t lanes, each summing <= H diffs ([-8160, 8160]),
//     reduced across 4 lanes in variance_final_from_16bit_sum_avx2 to
//     [-32640, 32640] < INT16_MAX
//   - `sse` has 8x uint32_t lanes, each summing <= 2 * H squared diffs
//     (<= 4,161,600 << UINT32_MAX)
static INLINE int sub_pix_var16xh_avx2(const uint8_t *src, int src_stride,
                                       int x_offset, int y_offset,
                                       const uint8_t *dst, int dst_stride,
                                       const uint8_t *second_pred, int do_sec,
                                       int height, uint32_t *sse) {
  __m256i sse_reg = _mm256_setzero_si256();
  __m256i sum_reg = _mm256_setzero_si256();
  int i, sum;
  assert(height >= 2 && (height & 1) == 0 && height <= 32);
  assert(x_offset >= 0 && x_offset < 8);
  assert(y_offset >= 0 && y_offset < 8);

  if (y_offset == 0) {
    if (x_offset == 0) {
      for (i = 0; i < height; i += 2) {
        const __m256i pred = load_two_rows16(src, src_stride);
        spv16_accum_avx2(pred, dst, dst_stride, second_pred, do_sec, &sse_reg,
                         &sum_reg);
        src += 2 * src_stride;
        dst += 2 * dst_stride;
        if (do_sec) second_pred += 32;
      }
    } else if (x_offset == 4) {
      for (i = 0; i < height; i += 2) {
        const __m256i pred =
            _mm256_avg_epu8(load_two_rows16(src, src_stride),
                            load_two_rows16(src + 1, src_stride));
        spv16_accum_avx2(pred, dst, dst_stride, second_pred, do_sec, &sse_reg,
                         &sum_reg);
        src += 2 * src_stride;
        dst += 2 * dst_stride;
        if (do_sec) second_pred += 32;
      }
    } else {
      const __m256i xfilter = _mm256_load_si256(
          (const __m256i *)(bilinear_filters_avx2 + (x_offset << 5)));
      for (i = 0; i < height; i += 2) {
        const __m256i pred =
            bilin_filter16_avx2(load_two_rows16(src, src_stride),
                                load_two_rows16(src + 1, src_stride), &xfilter);
        spv16_accum_avx2(pred, dst, dst_stride, second_pred, do_sec, &sse_reg,
                         &sum_reg);
        src += 2 * src_stride;
        dst += 2 * dst_stride;
        if (do_sec) second_pred += 32;
      }
    }
  } else if (y_offset == 4) {
    if (x_offset == 0) {
      __m256i prev = dup_row16(_mm_loadu_si128((const __m128i *)src));
      src += src_stride;
      for (i = 0; i < height; i += 2) {
        const __m256i curr = load_two_rows16(src, src_stride);
        const __m256i top = _mm256_permute2x128_si256(prev, curr, 0x21);
        prev = curr;
        spv16_accum_avx2(_mm256_avg_epu8(top, curr), dst, dst_stride,
                         second_pred, do_sec, &sse_reg, &sum_reg);
        src += 2 * src_stride;
        dst += 2 * dst_stride;
        if (do_sec) second_pred += 32;
      }
    } else if (x_offset == 4) {
      __m256i prev =
          dup_row16(_mm_avg_epu8(_mm_loadu_si128((const __m128i *)src),
                                 _mm_loadu_si128((const __m128i *)(src + 1))));
      src += src_stride;
      for (i = 0; i < height; i += 2) {
        const __m256i curr =
            _mm256_avg_epu8(load_two_rows16(src, src_stride),
                            load_two_rows16(src + 1, src_stride));
        const __m256i top = _mm256_permute2x128_si256(prev, curr, 0x21);
        prev = curr;
        spv16_accum_avx2(_mm256_avg_epu8(top, curr), dst, dst_stride,
                         second_pred, do_sec, &sse_reg, &sum_reg);
        src += 2 * src_stride;
        dst += 2 * dst_stride;
        if (do_sec) second_pred += 32;
      }
    } else {
      const __m256i xfilter = _mm256_load_si256(
          (const __m256i *)(bilinear_filters_avx2 + (x_offset << 5)));
      __m256i prev = filter_row0_16_dup(src, x_offset);
      src += src_stride;
      for (i = 0; i < height; i += 2) {
        const __m256i curr =
            bilin_filter16_avx2(load_two_rows16(src, src_stride),
                                load_two_rows16(src + 1, src_stride), &xfilter);
        const __m256i top = _mm256_permute2x128_si256(prev, curr, 0x21);
        prev = curr;
        spv16_accum_avx2(_mm256_avg_epu8(top, curr), dst, dst_stride,
                         second_pred, do_sec, &sse_reg, &sum_reg);
        src += 2 * src_stride;
        dst += 2 * dst_stride;
        if (do_sec) second_pred += 32;
      }
    }
  } else {
    const __m256i yfilter = _mm256_load_si256(
        (const __m256i *)(bilinear_filters_avx2 + (y_offset << 5)));
    if (x_offset == 0) {
      __m256i prev = dup_row16(_mm_loadu_si128((const __m128i *)src));
      src += src_stride;
      for (i = 0; i < height; i += 2) {
        const __m256i curr = load_two_rows16(src, src_stride);
        const __m256i top = _mm256_permute2x128_si256(prev, curr, 0x21);
        prev = curr;
        spv16_accum_avx2(bilin_filter16_avx2(top, curr, &yfilter), dst,
                         dst_stride, second_pred, do_sec, &sse_reg, &sum_reg);
        src += 2 * src_stride;
        dst += 2 * dst_stride;
        if (do_sec) second_pred += 32;
      }
    } else if (x_offset == 4) {
      __m256i prev =
          dup_row16(_mm_avg_epu8(_mm_loadu_si128((const __m128i *)src),
                                 _mm_loadu_si128((const __m128i *)(src + 1))));
      src += src_stride;
      for (i = 0; i < height; i += 2) {
        const __m256i curr =
            _mm256_avg_epu8(load_two_rows16(src, src_stride),
                            load_two_rows16(src + 1, src_stride));
        const __m256i top = _mm256_permute2x128_si256(prev, curr, 0x21);
        prev = curr;
        spv16_accum_avx2(bilin_filter16_avx2(top, curr, &yfilter), dst,
                         dst_stride, second_pred, do_sec, &sse_reg, &sum_reg);
        src += 2 * src_stride;
        dst += 2 * dst_stride;
        if (do_sec) second_pred += 32;
      }
    } else {
      const __m256i xfilter = _mm256_load_si256(
          (const __m256i *)(bilinear_filters_avx2 + (x_offset << 5)));
      __m256i prev = filter_row0_16_dup(src, x_offset);
      src += src_stride;
      for (i = 0; i < height; i += 2) {
        const __m256i curr =
            bilin_filter16_avx2(load_two_rows16(src, src_stride),
                                load_two_rows16(src + 1, src_stride), &xfilter);
        const __m256i top = _mm256_permute2x128_si256(prev, curr, 0x21);
        prev = curr;
        spv16_accum_avx2(bilin_filter16_avx2(top, curr, &yfilter), dst,
                         dst_stride, second_pred, do_sec, &sse_reg, &sum_reg);
        src += 2 * src_stride;
        dst += 2 * dst_stride;
        if (do_sec) second_pred += 32;
      }
    }
  }
  variance_final_from_16bit_sum_avx2(sse_reg, sum_reg, sse, &sum);
  return sum;
}

typedef void (*get_var_avx2)(const uint8_t *src_ptr, int src_stride,
                             const uint8_t *ref_ptr, int ref_stride,
                             unsigned int *sse, int *sum);

unsigned int vpx_variance8x4_avx2(const uint8_t *src_ptr, int src_stride,
                                  const uint8_t *ref_ptr, int ref_stride,
                                  unsigned int *sse) {
  __m256i vsse, vsum;
  int sum;
  variance8_avx2(src_ptr, src_stride, ref_ptr, ref_stride, 4, &vsse, &vsum);
  variance_final_from_16bit_sum_avx2(vsse, vsum, sse, &sum);
  return *sse - ((sum * sum) >> 5);
}

unsigned int vpx_variance8x8_avx2(const uint8_t *src_ptr, int src_stride,
                                  const uint8_t *ref_ptr, int ref_stride,
                                  unsigned int *sse) {
  __m256i vsse, vsum;
  int sum;
  variance8_avx2(src_ptr, src_stride, ref_ptr, ref_stride, 8, &vsse, &vsum);
  variance_final_from_16bit_sum_avx2(vsse, vsum, sse, &sum);
  return *sse - ((sum * sum) >> 6);
}

unsigned int vpx_variance8x16_avx2(const uint8_t *src_ptr, int src_stride,
                                   const uint8_t *ref_ptr, int ref_stride,
                                   unsigned int *sse) {
  __m256i vsse, vsum;
  int sum;
  variance8_avx2(src_ptr, src_stride, ref_ptr, ref_stride, 16, &vsse, &vsum);
  variance_final_from_16bit_sum_avx2(vsse, vsum, sse, &sum);
  return *sse - ((sum * sum) >> 7);
}

unsigned int vpx_variance16x8_avx2(const uint8_t *src_ptr, int src_stride,
                                   const uint8_t *ref_ptr, int ref_stride,
                                   unsigned int *sse) {
  int sum;
  __m256i vsse, vsum;
  variance16_avx2(src_ptr, src_stride, ref_ptr, ref_stride, 8, &vsse, &vsum);
  variance_final_from_16bit_sum_avx2(vsse, vsum, sse, &sum);
  return *sse - (uint32_t)(((int64_t)sum * sum) >> 7);
}

unsigned int vpx_variance16x16_avx2(const uint8_t *src_ptr, int src_stride,
                                    const uint8_t *ref_ptr, int ref_stride,
                                    unsigned int *sse) {
  int sum;
  __m256i vsse, vsum;
  variance16_avx2(src_ptr, src_stride, ref_ptr, ref_stride, 16, &vsse, &vsum);
  variance_final_from_16bit_sum_avx2(vsse, vsum, sse, &sum);
  return *sse - (uint32_t)(((int64_t)sum * sum) >> 8);
}

unsigned int vpx_variance16x32_avx2(const uint8_t *src_ptr, int src_stride,
                                    const uint8_t *ref_ptr, int ref_stride,
                                    unsigned int *sse) {
  int sum;
  __m256i vsse, vsum;
  variance16_avx2(src_ptr, src_stride, ref_ptr, ref_stride, 32, &vsse, &vsum);
  variance_final_from_16bit_sum_avx2(vsse, vsum, sse, &sum);
  return *sse - (uint32_t)(((int64_t)sum * sum) >> 9);
}

unsigned int vpx_variance32x16_avx2(const uint8_t *src_ptr, int src_stride,
                                    const uint8_t *ref_ptr, int ref_stride,
                                    unsigned int *sse) {
  int sum;
  __m256i vsse, vsum;
  variance32_avx2(src_ptr, src_stride, ref_ptr, ref_stride, 16, &vsse, &vsum);
  variance_final_from_16bit_sum_avx2(vsse, vsum, sse, &sum);
  return *sse - (uint32_t)(((int64_t)sum * sum) >> 9);
}

unsigned int vpx_variance32x32_avx2(const uint8_t *src_ptr, int src_stride,
                                    const uint8_t *ref_ptr, int ref_stride,
                                    unsigned int *sse) {
  int sum;
  __m256i vsse, vsum;
  __m128i vsum_128;
  variance32_avx2(src_ptr, src_stride, ref_ptr, ref_stride, 32, &vsse, &vsum);
  vsum_128 = _mm_add_epi16(_mm256_castsi256_si128(vsum),
                           _mm256_extractf128_si256(vsum, 1));
  vsum_128 = _mm_add_epi32(_mm_cvtepi16_epi32(vsum_128),
                           _mm_cvtepi16_epi32(_mm_srli_si128(vsum_128, 8)));
  variance_final_from_32bit_sum_avx2(vsse, vsum_128, sse, &sum);
  return *sse - (uint32_t)(((int64_t)sum * sum) >> 10);
}

unsigned int vpx_variance32x64_avx2(const uint8_t *src_ptr, int src_stride,
                                    const uint8_t *ref_ptr, int ref_stride,
                                    unsigned int *sse) {
  int sum;
  __m256i vsse, vsum;
  __m128i vsum_128;
  variance32_avx2(src_ptr, src_stride, ref_ptr, ref_stride, 64, &vsse, &vsum);
  vsum = sum_to_32bit_avx2(vsum);
  vsum_128 = _mm_add_epi32(_mm256_castsi256_si128(vsum),
                           _mm256_extractf128_si256(vsum, 1));
  variance_final_from_32bit_sum_avx2(vsse, vsum_128, sse, &sum);
  return *sse - (uint32_t)(((int64_t)sum * sum) >> 11);
}

unsigned int vpx_variance64x32_avx2(const uint8_t *src_ptr, int src_stride,
                                    const uint8_t *ref_ptr, int ref_stride,
                                    unsigned int *sse) {
  __m256i vsse = _mm256_setzero_si256();
  __m256i vsum = _mm256_setzero_si256();
  __m128i vsum_128;
  int sum;
  variance64_avx2(src_ptr, src_stride, ref_ptr, ref_stride, 32, &vsse, &vsum);
  vsum = sum_to_32bit_avx2(vsum);
  vsum_128 = _mm_add_epi32(_mm256_castsi256_si128(vsum),
                           _mm256_extractf128_si256(vsum, 1));
  variance_final_from_32bit_sum_avx2(vsse, vsum_128, sse, &sum);
  return *sse - (uint32_t)(((int64_t)sum * sum) >> 11);
}

unsigned int vpx_variance64x64_avx2(const uint8_t *src_ptr, int src_stride,
                                    const uint8_t *ref_ptr, int ref_stride,
                                    unsigned int *sse) {
  __m256i vsse = _mm256_setzero_si256();
  __m256i vsum = _mm256_setzero_si256();
  __m128i vsum_128;
  int sum;
  int i = 0;

  for (i = 0; i < 2; i++) {
    __m256i vsum16;
    variance64_avx2(src_ptr + 32 * i * src_stride, src_stride,
                    ref_ptr + 32 * i * ref_stride, ref_stride, 32, &vsse,
                    &vsum16);
    vsum = _mm256_add_epi32(vsum, sum_to_32bit_avx2(vsum16));
  }
  vsum_128 = _mm_add_epi32(_mm256_castsi256_si128(vsum),
                           _mm256_extractf128_si256(vsum, 1));
  variance_final_from_32bit_sum_avx2(vsse, vsum_128, sse, &sum);
  return *sse - (unsigned int)(((int64_t)sum * sum) >> 12);
}

unsigned int vpx_mse16x8_avx2(const uint8_t *src_ptr, int src_stride,
                              const uint8_t *ref_ptr, int ref_stride,
                              unsigned int *sse) {
  int sum;
  __m256i vsse, vsum;
  variance16_avx2(src_ptr, src_stride, ref_ptr, ref_stride, 8, &vsse, &vsum);
  variance_final_from_16bit_sum_avx2(vsse, vsum, sse, &sum);
  return *sse;
}

unsigned int vpx_mse16x16_avx2(const uint8_t *src_ptr, int src_stride,
                               const uint8_t *ref_ptr, int ref_stride,
                               unsigned int *sse) {
  int sum;
  __m256i vsse, vsum;
  variance16_avx2(src_ptr, src_stride, ref_ptr, ref_stride, 16, &vsse, &vsum);
  variance_final_from_16bit_sum_avx2(vsse, vsum, sse, &sum);
  return *sse;
}

unsigned int vpx_sub_pixel_variance64x64_avx2(
    const uint8_t *src_ptr, int src_stride, int x_offset, int y_offset,
    const uint8_t *ref_ptr, int ref_stride, unsigned int *sse) {
  unsigned int sse1;
  const int se1 = sub_pixel_variance32xh_avx2(
      src_ptr, src_stride, x_offset, y_offset, ref_ptr, ref_stride, 64, &sse1);
  unsigned int sse2;
  const int se2 =
      sub_pixel_variance32xh_avx2(src_ptr + 32, src_stride, x_offset, y_offset,
                                  ref_ptr + 32, ref_stride, 64, &sse2);
  const int se = se1 + se2;
  *sse = sse1 + sse2;
  return *sse - (uint32_t)(((int64_t)se * se) >> 12);
}

unsigned int vpx_sub_pixel_variance32x32_avx2(
    const uint8_t *src_ptr, int src_stride, int x_offset, int y_offset,
    const uint8_t *ref_ptr, int ref_stride, unsigned int *sse) {
  const int se = sub_pixel_variance32xh_avx2(
      src_ptr, src_stride, x_offset, y_offset, ref_ptr, ref_stride, 32, sse);
  return *sse - (uint32_t)(((int64_t)se * se) >> 10);
}

unsigned int vpx_sub_pixel_avg_variance64x64_avx2(
    const uint8_t *src_ptr, int src_stride, int x_offset, int y_offset,
    const uint8_t *ref_ptr, int ref_stride, unsigned int *sse,
    const uint8_t *second_pred) {
  unsigned int sse1;
  const int se1 = sub_pixel_avg_variance32xh_avx2(src_ptr, src_stride, x_offset,
                                                  y_offset, ref_ptr, ref_stride,
                                                  second_pred, 64, 64, &sse1);
  unsigned int sse2;
  const int se2 = sub_pixel_avg_variance32xh_avx2(
      src_ptr + 32, src_stride, x_offset, y_offset, ref_ptr + 32, ref_stride,
      second_pred + 32, 64, 64, &sse2);
  const int se = se1 + se2;

  *sse = sse1 + sse2;

  return *sse - (uint32_t)(((int64_t)se * se) >> 12);
}

unsigned int vpx_sub_pixel_avg_variance32x32_avx2(
    const uint8_t *src_ptr, int src_stride, int x_offset, int y_offset,
    const uint8_t *ref_ptr, int ref_stride, unsigned int *sse,
    const uint8_t *second_pred) {
  // Process 32 elements in parallel.
  const int se = sub_pixel_avg_variance32xh_avx2(src_ptr, src_stride, x_offset,
                                                 y_offset, ref_ptr, ref_stride,
                                                 second_pred, 32, 32, sse);
  return *sse - (uint32_t)(((int64_t)se * se) >> 10);
}

uint32_t vpx_sub_pixel_variance64x32_avx2(const uint8_t *src_ptr,
                                          int src_stride, int x_offset,
                                          int y_offset, const uint8_t *ref_ptr,
                                          int ref_stride, uint32_t *sse) {
  uint32_t sse1, sse2;
  const int se1 = sub_pixel_variance32xh_avx2(
      src_ptr, src_stride, x_offset, y_offset, ref_ptr, ref_stride, 32, &sse1);
  const int se2 =
      sub_pixel_variance32xh_avx2(src_ptr + 32, src_stride, x_offset, y_offset,
                                  ref_ptr + 32, ref_stride, 32, &sse2);
  const int se = se1 + se2;
  *sse = sse1 + sse2;
  return *sse - (uint32_t)(((int64_t)se * se) >> 11);
}

uint32_t vpx_sub_pixel_avg_variance64x32_avx2(const uint8_t *src_ptr,
                                              int src_stride, int x_offset,
                                              int y_offset,
                                              const uint8_t *ref_ptr,
                                              int ref_stride, uint32_t *sse,
                                              const uint8_t *second_pred) {
  uint32_t sse1, sse2;
  const int se1 = sub_pixel_avg_variance32xh_avx2(src_ptr, src_stride, x_offset,
                                                  y_offset, ref_ptr, ref_stride,
                                                  second_pred, 64, 32, &sse1);
  const int se2 = sub_pixel_avg_variance32xh_avx2(
      src_ptr + 32, src_stride, x_offset, y_offset, ref_ptr + 32, ref_stride,
      second_pred + 32, 64, 32, &sse2);
  const int se = se1 + se2;
  *sse = sse1 + sse2;
  return *sse - (uint32_t)(((int64_t)se * se) >> 11);
}

#define SUB_PIX_VAR_32XH_AVX2(H, SHIFT)                                        \
  uint32_t vpx_sub_pixel_variance32x##H##_avx2(                                \
      const uint8_t *src_ptr, int src_stride, int x_offset, int y_offset,      \
      const uint8_t *ref_ptr, int ref_stride, uint32_t *sse) {                 \
    const int se = sub_pixel_variance32xh_avx2(                                \
        src_ptr, src_stride, x_offset, y_offset, ref_ptr, ref_stride, H, sse); \
    return *sse - (uint32_t)(((int64_t)se * se) >> (SHIFT));                   \
  }                                                                            \
  uint32_t vpx_sub_pixel_avg_variance32x##H##_avx2(                            \
      const uint8_t *src_ptr, int src_stride, int x_offset, int y_offset,      \
      const uint8_t *ref_ptr, int ref_stride, uint32_t *sse,                   \
      const uint8_t *second_pred) {                                            \
    const int se = sub_pixel_avg_variance32xh_avx2(                            \
        src_ptr, src_stride, x_offset, y_offset, ref_ptr, ref_stride,          \
        second_pred, 32, H, sse);                                              \
    return *sse - (uint32_t)(((int64_t)se * se) >> (SHIFT));                   \
  }

SUB_PIX_VAR_32XH_AVX2(64, 11)
SUB_PIX_VAR_32XH_AVX2(16, 9)
#undef SUB_PIX_VAR_32XH_AVX2

uint32_t vpx_sub_pixel_variance16x32_avx2(const uint8_t *src_ptr,
                                          int src_stride, int x_offset,
                                          int y_offset, const uint8_t *ref_ptr,
                                          int ref_stride, uint32_t *sse) {
  const int se = sub_pix_var16xh_avx2(src_ptr, src_stride, x_offset, y_offset,
                                      ref_ptr, ref_stride, NULL, 0, 32, sse);
  return *sse - (uint32_t)(((int64_t)se * se) >> 9);
}

uint32_t vpx_sub_pixel_avg_variance16x32_avx2(const uint8_t *src_ptr,
                                              int src_stride, int x_offset,
                                              int y_offset,
                                              const uint8_t *ref_ptr,
                                              int ref_stride, uint32_t *sse,
                                              const uint8_t *second_pred) {
  const int se =
      sub_pix_var16xh_avx2(src_ptr, src_stride, x_offset, y_offset, ref_ptr,
                           ref_stride, second_pred, 1, 32, sse);
  return *sse - (uint32_t)(((int64_t)se * se) >> 9);
}

uint32_t vpx_sub_pixel_variance16x16_avx2(const uint8_t *src_ptr,
                                          int src_stride, int x_offset,
                                          int y_offset, const uint8_t *ref_ptr,
                                          int ref_stride, uint32_t *sse) {
  const int se = sub_pix_var16xh_avx2(src_ptr, src_stride, x_offset, y_offset,
                                      ref_ptr, ref_stride, NULL, 0, 16, sse);
  return *sse - (uint32_t)(((int64_t)se * se) >> 8);
}

uint32_t vpx_sub_pixel_avg_variance16x16_avx2(const uint8_t *src_ptr,
                                              int src_stride, int x_offset,
                                              int y_offset,
                                              const uint8_t *ref_ptr,
                                              int ref_stride, uint32_t *sse,
                                              const uint8_t *second_pred) {
  const int se =
      sub_pix_var16xh_avx2(src_ptr, src_stride, x_offset, y_offset, ref_ptr,
                           ref_stride, second_pred, 1, 16, sse);
  return *sse - (uint32_t)(((int64_t)se * se) >> 8);
}

uint32_t vpx_sub_pixel_variance16x8_avx2(const uint8_t *src_ptr, int src_stride,
                                         int x_offset, int y_offset,
                                         const uint8_t *ref_ptr, int ref_stride,
                                         uint32_t *sse) {
  const int se = sub_pix_var16xh_avx2(src_ptr, src_stride, x_offset, y_offset,
                                      ref_ptr, ref_stride, NULL, 0, 8, sse);
  return *sse - (uint32_t)(((int64_t)se * se) >> 7);
}

uint32_t vpx_sub_pixel_avg_variance16x8_avx2(const uint8_t *src_ptr,
                                             int src_stride, int x_offset,
                                             int y_offset,
                                             const uint8_t *ref_ptr,
                                             int ref_stride, uint32_t *sse,
                                             const uint8_t *second_pred) {
  const int se =
      sub_pix_var16xh_avx2(src_ptr, src_stride, x_offset, y_offset, ref_ptr,
                           ref_stride, second_pred, 1, 8, sse);
  return *sse - (uint32_t)(((int64_t)se * se) >> 7);
}
