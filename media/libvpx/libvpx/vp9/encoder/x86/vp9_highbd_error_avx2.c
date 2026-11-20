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

#include "./vp9_rtcd.h"
#include "vp9/common/vp9_common.h"

static INLINE int64_t reduce_add_epi64_avx2(__m256i acc) {
  const __m128i lo = _mm256_castsi256_si128(acc);
  const __m128i hi = _mm256_extracti128_si256(acc, 1);
  __m128i sum = _mm_add_epi64(lo, hi);
  sum = _mm_add_epi64(sum, _mm_srli_si128(sum, 8));
#if VPX_ARCH_X86_64
  return _mm_cvtsi128_si64(sum);
#else
  int64_t res;
  _mm_storel_epi64((__m128i *)&res, sum);
  return res;
#endif
}

int64_t vp9_highbd_block_error_avx2(const tran_low_t *coeff,
                                    const tran_low_t *dqcoeff,
                                    intptr_t block_size, int64_t *ssz, int bd) {
  intptr_t i;
  const int shift = 2 * (bd - 8);
  const int rounding = shift > 0 ? 1 << (shift - 1) : 0;
  __m256i err_acc0 = _mm256_setzero_si256();
  __m256i err_acc1 = _mm256_setzero_si256();
  __m256i ssz_acc0 = _mm256_setzero_si256();
  __m256i ssz_acc1 = _mm256_setzero_si256();
  int64_t error, sqcoeff;

  assert(block_size >= 8 && block_size % 8 == 0);

  for (i = 0; i < block_size; i += 8) {
    const __m256i c = _mm256_loadu_si256((const __m256i *)(coeff + i));
    const __m256i dq = _mm256_loadu_si256((const __m256i *)(dqcoeff + i));
    const __m256i diff = _mm256_sub_epi32(c, dq);
    const __m256i c_odd = _mm256_srli_epi64(c, 32);
    const __m256i diff_odd = _mm256_srli_epi64(diff, 32);

    err_acc0 = _mm256_add_epi64(err_acc0, _mm256_mul_epi32(diff, diff));
    err_acc1 = _mm256_add_epi64(err_acc1, _mm256_mul_epi32(diff_odd, diff_odd));
    ssz_acc0 = _mm256_add_epi64(ssz_acc0, _mm256_mul_epi32(c, c));
    ssz_acc1 = _mm256_add_epi64(ssz_acc1, _mm256_mul_epi32(c_odd, c_odd));
  }

  error = reduce_add_epi64_avx2(_mm256_add_epi64(err_acc0, err_acc1));
  sqcoeff = reduce_add_epi64_avx2(_mm256_add_epi64(ssz_acc0, ssz_acc1));

  assert(error >= 0 && sqcoeff >= 0);
  error = (error + rounding) >> shift;
  sqcoeff = (sqcoeff + rounding) >> shift;

  *ssz = sqcoeff;
  return error;
}
