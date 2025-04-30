/*
 * Copyright (c) 2018, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <tuple>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

#include "test/register_state_check.h"
#include "test/acm_random.h"
#include "test/util.h"

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"

#include "aom/aom_integer.h"
#include "aom_ports/aom_timer.h"
#include "av1/encoder/pickrst.h"

#define MAX_WIENER_BLOCK 384
#define MAX_DATA_BLOCK (MAX_WIENER_BLOCK + WIENER_WIN)

// 8-bit-depth tests
namespace wiener_lowbd {

// C implementation of the algorithm implmented by the SIMD code.
// This is a little more efficient than the version in av1_compute_stats_c().
static void compute_stats_win_opt_c(int wiener_win, const uint8_t *dgd,
                                    const uint8_t *src, int16_t *d, int16_t *s,
                                    int h_start, int h_end, int v_start,
                                    int v_end, int dgd_stride, int src_stride,
                                    int64_t *M, int64_t *H,
                                    int use_downsampled_wiener_stats) {
  ASSERT_TRUE(wiener_win == WIENER_WIN || wiener_win == WIENER_WIN_CHROMA);
  (void)d;
  (void)s;
  int i, j, k, l, m, n;
  const int pixel_count = (h_end - h_start) * (v_end - v_start);
  const int wiener_win2 = wiener_win * wiener_win;
  const int wiener_halfwin = (wiener_win >> 1);
  uint8_t avg = find_average(dgd, h_start, h_end, v_start, v_end, dgd_stride);
  int downsample_factor =
      use_downsampled_wiener_stats ? WIENER_STATS_DOWNSAMPLE_FACTOR : 1;

  std::vector<std::vector<int64_t> > M_int(wiener_win,
                                           std::vector<int64_t>(wiener_win, 0));
  std::vector<std::vector<int64_t> > H_int(
      wiener_win * wiener_win, std::vector<int64_t>(wiener_win * 8, 0));
  std::vector<std::vector<int32_t> > sumY(wiener_win,
                                          std::vector<int32_t>(wiener_win, 0));
  int32_t sumX = 0;
  const uint8_t *dgd_win = dgd - wiener_halfwin * dgd_stride - wiener_halfwin;

  // Main loop handles two pixels at a time
  // We can assume that h_start is even, since it will always be aligned to
  // a tile edge + some number of restoration units, and both of those will
  // be 64-pixel aligned.
  // However, at the edge of the image, h_end may be odd, so we need to handle
  // that case correctly.
  assert(h_start % 2 == 0);
  for (i = v_start; i < v_end; i = i + downsample_factor) {
    if (use_downsampled_wiener_stats &&
        (v_end - i < WIENER_STATS_DOWNSAMPLE_FACTOR)) {
      downsample_factor = v_end - i;
    }
    int32_t sumX_row_i32 = 0;
    std::vector<std::vector<int32_t> > sumY_row(
        wiener_win, std::vector<int32_t>(wiener_win, 0));
    std::vector<std::vector<int32_t> > M_row_i32(
        wiener_win, std::vector<int32_t>(wiener_win, 0));
    std::vector<std::vector<int32_t> > H_row_i32(
        wiener_win * wiener_win, std::vector<int32_t>(wiener_win * 8, 0));
    const int h_end_even = h_end & ~1;
    const int has_odd_pixel = h_end & 1;
    for (j = h_start; j < h_end_even; j += 2) {
      const uint8_t X1 = src[i * src_stride + j];
      const uint8_t X2 = src[i * src_stride + j + 1];
      sumX_row_i32 += X1 + X2;

      const uint8_t *dgd_ij = dgd_win + i * dgd_stride + j;
      for (k = 0; k < wiener_win; k++) {
        for (l = 0; l < wiener_win; l++) {
          const uint8_t *dgd_ijkl = dgd_ij + k * dgd_stride + l;
          int32_t *H_int_temp = &H_row_i32[(l * wiener_win + k)][0];
          const uint8_t D1 = dgd_ijkl[0];
          const uint8_t D2 = dgd_ijkl[1];
          sumY_row[k][l] += D1 + D2;
          M_row_i32[l][k] += D1 * X1 + D2 * X2;
          for (m = 0; m < wiener_win; m++) {
            for (n = 0; n < wiener_win; n++) {
              H_int_temp[m * 8 + n] += D1 * dgd_ij[n + dgd_stride * m] +
                                       D2 * dgd_ij[n + dgd_stride * m + 1];
            }
          }
        }
      }
    }
    // If the width is odd, add in the final pixel
    if (has_odd_pixel) {
      const uint8_t X1 = src[i * src_stride + j];
      sumX_row_i32 += X1;

      const uint8_t *dgd_ij = dgd_win + i * dgd_stride + j;
      for (k = 0; k < wiener_win; k++) {
        for (l = 0; l < wiener_win; l++) {
          const uint8_t *dgd_ijkl = dgd_ij + k * dgd_stride + l;
          int32_t *H_int_temp = &H_row_i32[(l * wiener_win + k)][0];
          const uint8_t D1 = dgd_ijkl[0];
          sumY_row[k][l] += D1;
          M_row_i32[l][k] += D1 * X1;
          for (m = 0; m < wiener_win; m++) {
            for (n = 0; n < wiener_win; n++) {
              H_int_temp[m * 8 + n] += D1 * dgd_ij[n + dgd_stride * m];
            }
          }
        }
      }
    }

    sumX += sumX_row_i32 * downsample_factor;
    // Scale M matrix based on the downsampling factor
    for (k = 0; k < wiener_win; ++k) {
      for (l = 0; l < wiener_win; ++l) {
        sumY[k][l] += sumY_row[k][l] * downsample_factor;
        M_int[k][l] += (int64_t)M_row_i32[k][l] * downsample_factor;
      }
    }
    // Scale H matrix based on the downsampling factor
    for (k = 0; k < wiener_win * wiener_win; ++k) {
      for (l = 0; l < wiener_win * 8; ++l) {
        H_int[k][l] += (int64_t)H_row_i32[k][l] * downsample_factor;
      }
    }
  }

  const int64_t avg_square_sum = (int64_t)avg * (int64_t)avg * pixel_count;
  for (k = 0; k < wiener_win; k++) {
    for (l = 0; l < wiener_win; l++) {
      M[l * wiener_win + k] =
          M_int[l][k] + avg_square_sum - (int64_t)avg * (sumX + sumY[k][l]);
      for (m = 0; m < wiener_win; m++) {
        for (n = 0; n < wiener_win; n++) {
          H[(l * wiener_win + k) * wiener_win2 + m * wiener_win + n] =
              H_int[(l * wiener_win + k)][n * 8 + m] + avg_square_sum -
              (int64_t)avg * (sumY[k][l] + sumY[n][m]);
        }
      }
    }
  }
}

static void compute_stats_opt_c(int wiener_win, const uint8_t *dgd,
                                const uint8_t *src, int16_t *d, int16_t *s,
                                int h_start, int h_end, int v_start, int v_end,
                                int dgd_stride, int src_stride, int64_t *M,
                                int64_t *H, int use_downsampled_wiener_stats) {
  if (wiener_win == WIENER_WIN || wiener_win == WIENER_WIN_CHROMA) {
    compute_stats_win_opt_c(wiener_win, dgd, src, d, s, h_start, h_end, v_start,
                            v_end, dgd_stride, src_stride, M, H,
                            use_downsampled_wiener_stats);
  } else {
    av1_compute_stats_c(wiener_win, dgd, src, d, s, h_start, h_end, v_start,
                        v_end, dgd_stride, src_stride, M, H,
                        use_downsampled_wiener_stats);
  }
}

static const int kIterations = 100;
typedef void (*compute_stats_Func)(int wiener_win, const uint8_t *dgd,
                                   const uint8_t *src, int16_t *dgd_avg,
                                   int16_t *src_avg, int h_start, int h_end,
                                   int v_start, int v_end, int dgd_stride,
                                   int src_stride, int64_t *M, int64_t *H,
                                   int use_downsampled_wiener_stats);

////////////////////////////////////////////////////////////////////////////////
// 8 bit
////////////////////////////////////////////////////////////////////////////////

typedef std::tuple<const compute_stats_Func> WienerTestParam;

class WienerTest : public ::testing::TestWithParam<WienerTestParam> {
 public:
  void SetUp() override {
    src_buf = (uint8_t *)aom_memalign(
        32, MAX_DATA_BLOCK * MAX_DATA_BLOCK * sizeof(*src_buf));
    ASSERT_NE(src_buf, nullptr);
    dgd_buf = (uint8_t *)aom_memalign(
        32, MAX_DATA_BLOCK * MAX_DATA_BLOCK * sizeof(*dgd_buf));
    ASSERT_NE(dgd_buf, nullptr);
    const int buf_size =
        sizeof(*buf) * 6 * RESTORATION_UNITSIZE_MAX * RESTORATION_UNITSIZE_MAX;
    buf = (int16_t *)aom_memalign(32, buf_size);
    ASSERT_NE(buf, nullptr);
    memset(buf, 0, buf_size);
    target_func_ = GET_PARAM(0);
  }
  void TearDown() override {
    aom_free(src_buf);
    aom_free(dgd_buf);
    aom_free(buf);
  }
  void RunWienerTest(const int32_t wiener_win, int32_t run_times);
  void RunWienerTest_ExtremeValues(const int32_t wiener_win);

 private:
  compute_stats_Func target_func_;
  libaom_test::ACMRandom rng_;
  uint8_t *src_buf;
  uint8_t *dgd_buf;
  int16_t *buf;
};

void WienerTest::RunWienerTest(const int32_t wiener_win, int32_t run_times) {
  const int32_t wiener_halfwin = wiener_win >> 1;
  const int32_t wiener_win2 = wiener_win * wiener_win;
  DECLARE_ALIGNED(32, int64_t, M_ref[WIENER_WIN2]);
  DECLARE_ALIGNED(32, int64_t, H_ref[WIENER_WIN2 * WIENER_WIN2]);
  DECLARE_ALIGNED(32, int64_t, M_test[WIENER_WIN2]);
  DECLARE_ALIGNED(32, int64_t, H_test[WIENER_WIN2 * WIENER_WIN2]);
  // Note(rachelbarker):
  // The SIMD code requires `h_start` to be even, but can otherwise
  // deal with any values of `h_end`, `v_start`, `v_end`. We cover this
  // entire range, even though (at the time of writing) `h_start` and `v_start`
  // will always be multiples of 64 when called from non-test code.
  // If in future any new requirements are added, these lines will
  // need changing.
  int h_start = (rng_.Rand16() % (MAX_WIENER_BLOCK / 2)) & ~1;
  int h_end = run_times != 1 ? 256 : (rng_.Rand16() % MAX_WIENER_BLOCK);
  if (h_start > h_end) std::swap(h_start, h_end);
  int v_start = rng_.Rand16() % (MAX_WIENER_BLOCK / 2);
  int v_end = run_times != 1 ? 256 : (rng_.Rand16() % MAX_WIENER_BLOCK);
  if (v_start > v_end) std::swap(v_start, v_end);
  const int dgd_stride = h_end;
  const int src_stride = MAX_DATA_BLOCK;
  const int iters = run_times == 1 ? kIterations : 2;
  const int max_value_downsample_stats = 1;
  int16_t *dgd_avg = buf;
  int16_t *src_avg =
      buf + (3 * RESTORATION_UNITSIZE_MAX * RESTORATION_UNITSIZE_MAX);

  for (int iter = 0; iter < iters && !HasFatalFailure(); ++iter) {
    for (int i = 0; i < MAX_DATA_BLOCK * MAX_DATA_BLOCK; ++i) {
      dgd_buf[i] = rng_.Rand8();
      src_buf[i] = rng_.Rand8();
    }
    uint8_t *dgd = dgd_buf + wiener_halfwin * MAX_DATA_BLOCK + wiener_halfwin;
    uint8_t *src = src_buf;
    for (int use_downsampled_stats = 0;
         use_downsampled_stats <= max_value_downsample_stats;
         use_downsampled_stats++) {
      aom_usec_timer timer;
      aom_usec_timer_start(&timer);
      for (int i = 0; i < run_times; ++i) {
        av1_compute_stats_c(wiener_win, dgd, src, dgd_avg, src_avg, h_start,
                            h_end, v_start, v_end, dgd_stride, src_stride,
                            M_ref, H_ref, use_downsampled_stats);
      }
      aom_usec_timer_mark(&timer);
      const double time1 = static_cast<double>(aom_usec_timer_elapsed(&timer));
      aom_usec_timer_start(&timer);
      for (int i = 0; i < run_times; ++i) {
        target_func_(wiener_win, dgd, src, dgd_avg, src_avg, h_start, h_end,
                     v_start, v_end, dgd_stride, src_stride, M_test, H_test,
                     use_downsampled_stats);
      }
      aom_usec_timer_mark(&timer);
      const double time2 = static_cast<double>(aom_usec_timer_elapsed(&timer));
      if (run_times > 10) {
        printf("win %d %3dx%-3d:%7.2f/%7.2fns", wiener_win, h_end, v_end, time1,
               time2);
        printf("(%3.2f)\n", time1 / time2);
      }
      int failed = 0;
      for (int i = 0; i < wiener_win2; ++i) {
        if (M_ref[i] != M_test[i]) {
          failed = 1;
          printf("win %d M iter %d [%4d] ref %6" PRId64 " test %6" PRId64 " \n",
                 wiener_win, iter, i, M_ref[i], M_test[i]);
          break;
        }
      }
      for (int i = 0; i < wiener_win2 * wiener_win2; ++i) {
        if (H_ref[i] != H_test[i]) {
          failed = 1;
          printf("win %d H iter %d [%4d] ref %6" PRId64 " test %6" PRId64 " \n",
                 wiener_win, iter, i, H_ref[i], H_test[i]);
          break;
        }
      }
      ASSERT_EQ(failed, 0);
    }
  }
}

void WienerTest::RunWienerTest_ExtremeValues(const int32_t wiener_win) {
  const int32_t wiener_halfwin = wiener_win >> 1;
  const int32_t wiener_win2 = wiener_win * wiener_win;
  DECLARE_ALIGNED(32, int64_t, M_ref[WIENER_WIN2]);
  DECLARE_ALIGNED(32, int64_t, H_ref[WIENER_WIN2 * WIENER_WIN2]);
  DECLARE_ALIGNED(32, int64_t, M_test[WIENER_WIN2]);
  DECLARE_ALIGNED(32, int64_t, H_test[WIENER_WIN2 * WIENER_WIN2]);
  const int h_start = 16;
  const int h_end = MAX_WIENER_BLOCK;
  const int v_start = 16;
  const int v_end = MAX_WIENER_BLOCK;
  const int dgd_stride = h_end;
  const int src_stride = MAX_DATA_BLOCK;
  const int iters = 1;
  const int max_value_downsample_stats = 1;
  int16_t *dgd_avg = buf;
  int16_t *src_avg =
      buf + (3 * RESTORATION_UNITSIZE_MAX * RESTORATION_UNITSIZE_MAX);

  for (int iter = 0; iter < iters && !HasFatalFailure(); ++iter) {
    // Fill with alternating extreme values to maximize difference with
    // the average.
    for (int i = 0; i < MAX_DATA_BLOCK * MAX_DATA_BLOCK; ++i) {
      dgd_buf[i] = i & 1 ? 255 : 0;
      src_buf[i] = i & 1 ? 255 : 0;
    }
    uint8_t *dgd = dgd_buf + wiener_halfwin * MAX_DATA_BLOCK + wiener_halfwin;
    uint8_t *src = src_buf;
    for (int use_downsampled_stats = 0;
         use_downsampled_stats <= max_value_downsample_stats;
         use_downsampled_stats++) {
      av1_compute_stats_c(wiener_win, dgd, src, dgd_avg, src_avg, h_start,
                          h_end, v_start, v_end, dgd_stride, src_stride, M_ref,
                          H_ref, use_downsampled_stats);

      target_func_(wiener_win, dgd, src, dgd_avg, src_avg, h_start, h_end,
                   v_start, v_end, dgd_stride, src_stride, M_test, H_test,
                   use_downsampled_stats);

      int failed = 0;
      for (int i = 0; i < wiener_win2; ++i) {
        if (M_ref[i] != M_test[i]) {
          failed = 1;
          printf("win %d M iter %d [%4d] ref %6" PRId64 " test %6" PRId64 " \n",
                 wiener_win, iter, i, M_ref[i], M_test[i]);
          break;
        }
      }
      for (int i = 0; i < wiener_win2 * wiener_win2; ++i) {
        if (H_ref[i] != H_test[i]) {
          failed = 1;
          printf("win %d H iter %d [%4d] ref %6" PRId64 " test %6" PRId64 " \n",
                 wiener_win, iter, i, H_ref[i], H_test[i]);
          break;
        }
      }
      ASSERT_EQ(failed, 0);
    }
  }
}

TEST_P(WienerTest, RandomValues) {
  RunWienerTest(WIENER_WIN, 1);
  RunWienerTest(WIENER_WIN_CHROMA, 1);
}

TEST_P(WienerTest, ExtremeValues) {
  RunWienerTest_ExtremeValues(WIENER_WIN);
  RunWienerTest_ExtremeValues(WIENER_WIN_CHROMA);
}

TEST_P(WienerTest, DISABLED_Speed) {
  RunWienerTest(WIENER_WIN, 200);
  RunWienerTest(WIENER_WIN_CHROMA, 200);
}

INSTANTIATE_TEST_SUITE_P(C, WienerTest, ::testing::Values(compute_stats_opt_c));

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(SSE4_1, WienerTest,
                         ::testing::Values(av1_compute_stats_sse4_1));
#endif  // HAVE_SSE4_1

#if HAVE_AVX2

INSTANTIATE_TEST_SUITE_P(AVX2, WienerTest,
                         ::testing::Values(av1_compute_stats_avx2));
#endif  // HAVE_AVX2

#if HAVE_NEON

INSTANTIATE_TEST_SUITE_P(NEON, WienerTest,
                         ::testing::Values(av1_compute_stats_neon));
#endif  // HAVE_NEON

#if HAVE_SVE

INSTANTIATE_TEST_SUITE_P(SVE, WienerTest,
                         ::testing::Values(av1_compute_stats_sve));
#endif  // HAVE_SVE

}  // namespace wiener_lowbd

#if CONFIG_AV1_HIGHBITDEPTH
// High bit-depth tests:
namespace wiener_highbd {

static void compute_stats_highbd_win_opt_c(int wiener_win, const uint8_t *dgd8,
                                           const uint8_t *src8, int h_start,
                                           int h_end, int v_start, int v_end,
                                           int dgd_stride, int src_stride,
                                           int64_t *M, int64_t *H,
                                           aom_bit_depth_t bit_depth) {
  ASSERT_TRUE(wiener_win == WIENER_WIN || wiener_win == WIENER_WIN_CHROMA);
  int i, j, k, l, m, n;
  const int pixel_count = (h_end - h_start) * (v_end - v_start);
  const int wiener_win2 = wiener_win * wiener_win;
  const int wiener_halfwin = (wiener_win >> 1);
  const uint16_t *src = CONVERT_TO_SHORTPTR(src8);
  const uint16_t *dgd = CONVERT_TO_SHORTPTR(dgd8);
  const uint16_t avg =
      find_average_highbd(dgd, h_start, h_end, v_start, v_end, dgd_stride);

  std::vector<std::vector<int64_t> > M_int(wiener_win,
                                           std::vector<int64_t>(wiener_win, 0));
  std::vector<std::vector<int64_t> > H_int(
      wiener_win * wiener_win, std::vector<int64_t>(wiener_win * 8, 0));
  std::vector<std::vector<int32_t> > sumY(wiener_win,
                                          std::vector<int32_t>(wiener_win, 0));

  memset(M, 0, sizeof(*M) * wiener_win2);
  memset(H, 0, sizeof(*H) * wiener_win2 * wiener_win2);

  int64_t sumX = 0;
  const uint16_t *dgd_win = dgd - wiener_halfwin * dgd_stride - wiener_halfwin;

  // Main loop handles two pixels at a time
  // We can assume that h_start is even, since it will always be aligned to
  // a tile edge + some number of restoration units, and both of those will
  // be 64-pixel aligned.
  // However, at the edge of the image, h_end may be odd, so we need to handle
  // that case correctly.
  assert(h_start % 2 == 0);
  for (i = v_start; i < v_end; i++) {
    const int h_end_even = h_end & ~1;
    const int has_odd_pixel = h_end & 1;
    for (j = h_start; j < h_end_even; j += 2) {
      const uint16_t X1 = src[i * src_stride + j];
      const uint16_t X2 = src[i * src_stride + j + 1];
      sumX += X1 + X2;

      const uint16_t *dgd_ij = dgd_win + i * dgd_stride + j;
      for (k = 0; k < wiener_win; k++) {
        for (l = 0; l < wiener_win; l++) {
          const uint16_t *dgd_ijkl = dgd_ij + k * dgd_stride + l;
          int64_t *H_int_temp = &H_int[(l * wiener_win + k)][0];
          const uint16_t D1 = dgd_ijkl[0];
          const uint16_t D2 = dgd_ijkl[1];
          sumY[k][l] += D1 + D2;
          M_int[l][k] += D1 * X1 + D2 * X2;
          for (m = 0; m < wiener_win; m++) {
            for (n = 0; n < wiener_win; n++) {
              H_int_temp[m * 8 + n] += D1 * dgd_ij[n + dgd_stride * m] +
                                       D2 * dgd_ij[n + dgd_stride * m + 1];
            }
          }
        }
      }
    }
    // If the width is odd, add in the final pixel
    if (has_odd_pixel) {
      const uint16_t X1 = src[i * src_stride + j];
      sumX += X1;

      const uint16_t *dgd_ij = dgd_win + i * dgd_stride + j;
      for (k = 0; k < wiener_win; k++) {
        for (l = 0; l < wiener_win; l++) {
          const uint16_t *dgd_ijkl = dgd_ij + k * dgd_stride + l;
          int64_t *H_int_temp = &H_int[(l * wiener_win + k)][0];
          const uint16_t D1 = dgd_ijkl[0];
          sumY[k][l] += D1;
          M_int[l][k] += D1 * X1;
          for (m = 0; m < wiener_win; m++) {
            for (n = 0; n < wiener_win; n++) {
              H_int_temp[m * 8 + n] += D1 * dgd_ij[n + dgd_stride * m];
            }
          }
        }
      }
    }
  }

  uint8_t bit_depth_divider = 1;
  if (bit_depth == AOM_BITS_12)
    bit_depth_divider = 16;
  else if (bit_depth == AOM_BITS_10)
    bit_depth_divider = 4;

  const int64_t avg_square_sum = (int64_t)avg * (int64_t)avg * pixel_count;
  for (k = 0; k < wiener_win; k++) {
    for (l = 0; l < wiener_win; l++) {
      M[l * wiener_win + k] =
          (M_int[l][k] +
           (avg_square_sum - (int64_t)avg * (sumX + sumY[k][l]))) /
          bit_depth_divider;
      for (m = 0; m < wiener_win; m++) {
        for (n = 0; n < wiener_win; n++) {
          H[(l * wiener_win + k) * wiener_win2 + m * wiener_win + n] =
              (H_int[(l * wiener_win + k)][n * 8 + m] +
               (avg_square_sum - (int64_t)avg * (sumY[k][l] + sumY[n][m]))) /
              bit_depth_divider;
        }
      }
    }
  }
}

static void compute_stats_highbd_opt_c(int wiener_win, const uint8_t *dgd,
                                       const uint8_t *src, int16_t *d,
                                       int16_t *s, int h_start, int h_end,
                                       int v_start, int v_end, int dgd_stride,
                                       int src_stride, int64_t *M, int64_t *H,
                                       aom_bit_depth_t bit_depth) {
  if (wiener_win == WIENER_WIN || wiener_win == WIENER_WIN_CHROMA) {
    compute_stats_highbd_win_opt_c(wiener_win, dgd, src, h_start, h_end,
                                   v_start, v_end, dgd_stride, src_stride, M, H,
                                   bit_depth);
  } else {
    av1_compute_stats_highbd_c(wiener_win, dgd, src, d, s, h_start, h_end,
                               v_start, v_end, dgd_stride, src_stride, M, H,
                               bit_depth);
  }
}

static const int kIterations = 100;
typedef void (*compute_stats_Func)(int wiener_win, const uint8_t *dgd,
                                   const uint8_t *src, int16_t *d, int16_t *s,
                                   int h_start, int h_end, int v_start,
                                   int v_end, int dgd_stride, int src_stride,
                                   int64_t *M, int64_t *H,
                                   aom_bit_depth_t bit_depth);

typedef std::tuple<const compute_stats_Func> WienerTestParam;

class WienerTestHighbd : public ::testing::TestWithParam<WienerTestParam> {
 public:
  void SetUp() override {
    src_buf = (uint16_t *)aom_memalign(
        32, MAX_DATA_BLOCK * MAX_DATA_BLOCK * sizeof(*src_buf));
    ASSERT_NE(src_buf, nullptr);
    dgd_buf = (uint16_t *)aom_memalign(
        32, MAX_DATA_BLOCK * MAX_DATA_BLOCK * sizeof(*dgd_buf));
    ASSERT_NE(dgd_buf, nullptr);
    const size_t buf_size =
        sizeof(*buf) * 6 * RESTORATION_UNITSIZE_MAX * RESTORATION_UNITSIZE_MAX;
    buf = (int16_t *)aom_memalign(32, buf_size);
    ASSERT_NE(buf, nullptr);
    memset(buf, 0, buf_size);
    target_func_ = GET_PARAM(0);
  }
  void TearDown() override {
    aom_free(src_buf);
    aom_free(dgd_buf);
    aom_free(buf);
  }
  void RunWienerTest(const int32_t wiener_win, int32_t run_times,
                     aom_bit_depth_t bit_depth);
  void RunWienerTest_ExtremeValues(const int32_t wiener_win,
                                   aom_bit_depth_t bit_depth);

  void RunWienerTest_Overflow32bTest(const int32_t wiener_win,
                                     aom_bit_depth_t bit_depth);

 private:
  compute_stats_Func target_func_;
  libaom_test::ACMRandom rng_;
  uint16_t *src_buf;
  uint16_t *dgd_buf;
  int16_t *buf;
};

void WienerTestHighbd::RunWienerTest(const int32_t wiener_win,
                                     int32_t run_times,
                                     aom_bit_depth_t bit_depth) {
  const int32_t wiener_halfwin = wiener_win >> 1;
  const int32_t wiener_win2 = wiener_win * wiener_win;
  DECLARE_ALIGNED(32, int64_t, M_ref[WIENER_WIN2]);
  DECLARE_ALIGNED(32, int64_t, H_ref[WIENER_WIN2 * WIENER_WIN2]);
  DECLARE_ALIGNED(32, int64_t, M_test[WIENER_WIN2]);
  DECLARE_ALIGNED(32, int64_t, H_test[WIENER_WIN2 * WIENER_WIN2]);
  // Note(rachelbarker):
  // The SIMD code requires `h_start` to be even, but can otherwise
  // deal with any values of `h_end`, `v_start`, `v_end`. We cover this
  // entire range, even though (at the time of writing) `h_start` and `v_start`
  // will always be multiples of 64 when called from non-test code.
  // If in future any new requirements are added, these lines will
  // need changing.
  int h_start = (rng_.Rand16() % (MAX_WIENER_BLOCK / 2)) & ~1;
  int h_end = run_times != 1 ? 256 : (rng_.Rand16() % MAX_WIENER_BLOCK);
  if (h_start > h_end) std::swap(h_start, h_end);
  int v_start = rng_.Rand16() % (MAX_WIENER_BLOCK / 2);
  int v_end = run_times != 1 ? 256 : (rng_.Rand16() % MAX_WIENER_BLOCK);
  if (v_start > v_end) std::swap(v_start, v_end);
  const int dgd_stride = h_end;
  const int src_stride = MAX_DATA_BLOCK;
  const int iters = run_times == 1 ? kIterations : 2;
  int16_t *dgd_avg = buf;
  int16_t *src_avg =
      buf + (3 * RESTORATION_UNITSIZE_MAX * RESTORATION_UNITSIZE_MAX);
  for (int iter = 0; iter < iters && !HasFatalFailure(); ++iter) {
    for (int i = 0; i < MAX_DATA_BLOCK * MAX_DATA_BLOCK; ++i) {
      dgd_buf[i] = rng_.Rand16() % (1 << bit_depth);
      src_buf[i] = rng_.Rand16() % (1 << bit_depth);
    }
    const uint8_t *dgd8 = CONVERT_TO_BYTEPTR(
        dgd_buf + wiener_halfwin * MAX_DATA_BLOCK + wiener_halfwin);
    const uint8_t *src8 = CONVERT_TO_BYTEPTR(src_buf);

    aom_usec_timer timer;
    aom_usec_timer_start(&timer);
    for (int i = 0; i < run_times; ++i) {
      av1_compute_stats_highbd_c(wiener_win, dgd8, src8, dgd_avg, src_avg,
                                 h_start, h_end, v_start, v_end, dgd_stride,
                                 src_stride, M_ref, H_ref, bit_depth);
    }
    aom_usec_timer_mark(&timer);
    const double time1 = static_cast<double>(aom_usec_timer_elapsed(&timer));
    aom_usec_timer_start(&timer);
    for (int i = 0; i < run_times; ++i) {
      target_func_(wiener_win, dgd8, src8, dgd_avg, src_avg, h_start, h_end,
                   v_start, v_end, dgd_stride, src_stride, M_test, H_test,
                   bit_depth);
    }
    aom_usec_timer_mark(&timer);
    const double time2 = static_cast<double>(aom_usec_timer_elapsed(&timer));
    if (run_times > 10) {
      printf("win %d bd %d %3dx%-3d:%7.2f/%7.2fns", wiener_win, bit_depth,
             h_end, v_end, time1, time2);
      printf("(%3.2f)\n", time1 / time2);
    }
    int failed = 0;
    for (int i = 0; i < wiener_win2; ++i) {
      if (M_ref[i] != M_test[i]) {
        failed = 1;
        printf("win %d bd %d M iter %d [%4d] ref %6" PRId64 " test %6" PRId64
               " \n",
               wiener_win, bit_depth, iter, i, M_ref[i], M_test[i]);
        break;
      }
    }
    for (int i = 0; i < wiener_win2 * wiener_win2; ++i) {
      if (H_ref[i] != H_test[i]) {
        failed = 1;
        printf("win %d bd %d H iter %d [%4d] ref %6" PRId64 " test %6" PRId64
               " \n",
               wiener_win, bit_depth, iter, i, H_ref[i], H_test[i]);
        break;
      }
    }
    ASSERT_EQ(failed, 0);
  }
}

void WienerTestHighbd::RunWienerTest_ExtremeValues(const int32_t wiener_win,
                                                   aom_bit_depth_t bit_depth) {
  const int32_t wiener_halfwin = wiener_win >> 1;
  const int32_t wiener_win2 = wiener_win * wiener_win;
  DECLARE_ALIGNED(32, int64_t, M_ref[WIENER_WIN2]);
  DECLARE_ALIGNED(32, int64_t, H_ref[WIENER_WIN2 * WIENER_WIN2]);
  DECLARE_ALIGNED(32, int64_t, M_test[WIENER_WIN2]);
  DECLARE_ALIGNED(32, int64_t, H_test[WIENER_WIN2 * WIENER_WIN2]);
  const int h_start = 16;
  const int h_end = MAX_WIENER_BLOCK;
  const int v_start = 16;
  const int v_end = MAX_WIENER_BLOCK;
  const int dgd_stride = h_end;
  const int src_stride = MAX_DATA_BLOCK;
  const int iters = 1;
  int16_t *dgd_avg = buf;
  int16_t *src_avg =
      buf + (3 * RESTORATION_UNITSIZE_MAX * RESTORATION_UNITSIZE_MAX);
  for (int iter = 0; iter < iters && !HasFatalFailure(); ++iter) {
    // Fill with alternating extreme values to maximize difference with
    // the average.
    for (int i = 0; i < MAX_DATA_BLOCK * MAX_DATA_BLOCK; ++i) {
      dgd_buf[i] = i & 1 ? ((uint16_t)1 << bit_depth) - 1 : 0;
      src_buf[i] = i & 1 ? ((uint16_t)1 << bit_depth) - 1 : 0;
    }
    const uint8_t *dgd8 = CONVERT_TO_BYTEPTR(
        dgd_buf + wiener_halfwin * MAX_DATA_BLOCK + wiener_halfwin);
    const uint8_t *src8 = CONVERT_TO_BYTEPTR(src_buf);

    av1_compute_stats_highbd_c(wiener_win, dgd8, src8, dgd_avg, src_avg,
                               h_start, h_end, v_start, v_end, dgd_stride,
                               src_stride, M_ref, H_ref, bit_depth);

    target_func_(wiener_win, dgd8, src8, dgd_avg, src_avg, h_start, h_end,
                 v_start, v_end, dgd_stride, src_stride, M_test, H_test,
                 bit_depth);

    int failed = 0;
    for (int i = 0; i < wiener_win2; ++i) {
      if (M_ref[i] != M_test[i]) {
        failed = 1;
        printf("win %d bd %d M iter %d [%4d] ref %6" PRId64 " test %6" PRId64
               " \n",
               wiener_win, bit_depth, iter, i, M_ref[i], M_test[i]);
        break;
      }
    }
    for (int i = 0; i < wiener_win2 * wiener_win2; ++i) {
      if (H_ref[i] != H_test[i]) {
        failed = 1;
        printf("win %d bd %d H iter %d [%4d] ref %6" PRId64 " test %6" PRId64
               " \n",
               wiener_win, bit_depth, iter, i, H_ref[i], H_test[i]);
        break;
      }
    }
    ASSERT_EQ(failed, 0);
  }
}

void WienerTestHighbd::RunWienerTest_Overflow32bTest(
    const int32_t wiener_win, aom_bit_depth_t bit_depth) {
  const int32_t wiener_halfwin = wiener_win >> 1;
  const int32_t wiener_win2 = wiener_win * wiener_win;
  DECLARE_ALIGNED(32, int64_t, M_ref[WIENER_WIN2]);
  DECLARE_ALIGNED(32, int64_t, H_ref[WIENER_WIN2 * WIENER_WIN2]);
  DECLARE_ALIGNED(32, int64_t, M_test[WIENER_WIN2]);
  DECLARE_ALIGNED(32, int64_t, H_test[WIENER_WIN2 * WIENER_WIN2]);
  const int h_start = 16;
  const int h_end = MAX_WIENER_BLOCK;
  const int v_start = 16;
  const int v_end = MAX_WIENER_BLOCK;
  const int dgd_stride = h_end;
  const int src_stride = MAX_DATA_BLOCK;
  const int iters = 1;
  int16_t *dgd_avg = buf;
  int16_t *src_avg =
      buf + (3 * RESTORATION_UNITSIZE_MAX * RESTORATION_UNITSIZE_MAX);
  for (int iter = 0; iter < iters && !HasFatalFailure(); ++iter) {
    // Fill src and dgd such that the intermediate values for M and H will at
    // some point overflow a signed 32-bit value.
    for (int i = 0; i < MAX_DATA_BLOCK * MAX_DATA_BLOCK; ++i) {
      dgd_buf[i] = ((uint16_t)1 << bit_depth) - 1;
      src_buf[i] = 0;
    }

    memset(dgd_buf, 0, MAX_DATA_BLOCK * 30 * sizeof(dgd_buf));
    const uint8_t *dgd8 = CONVERT_TO_BYTEPTR(
        dgd_buf + wiener_halfwin * MAX_DATA_BLOCK + wiener_halfwin);
    const uint8_t *src8 = CONVERT_TO_BYTEPTR(src_buf);

    av1_compute_stats_highbd_c(wiener_win, dgd8, src8, dgd_avg, src_avg,
                               h_start, h_end, v_start, v_end, dgd_stride,
                               src_stride, M_ref, H_ref, bit_depth);

    target_func_(wiener_win, dgd8, src8, dgd_avg, src_avg, h_start, h_end,
                 v_start, v_end, dgd_stride, src_stride, M_test, H_test,
                 bit_depth);

    int failed = 0;
    for (int i = 0; i < wiener_win2; ++i) {
      if (M_ref[i] != M_test[i]) {
        failed = 1;
        printf("win %d bd %d M iter %d [%4d] ref %6" PRId64 " test %6" PRId64
               " \n",
               wiener_win, bit_depth, iter, i, M_ref[i], M_test[i]);
        break;
      }
    }
    for (int i = 0; i < wiener_win2 * wiener_win2; ++i) {
      if (H_ref[i] != H_test[i]) {
        failed = 1;
        printf("win %d bd %d H iter %d [%4d] ref %6" PRId64 " test %6" PRId64
               " \n",
               wiener_win, bit_depth, iter, i, H_ref[i], H_test[i]);
        break;
      }
    }
    ASSERT_EQ(failed, 0);
  }
}

TEST_P(WienerTestHighbd, RandomValues) {
  RunWienerTest(WIENER_WIN, 1, AOM_BITS_8);
  RunWienerTest(WIENER_WIN_CHROMA, 1, AOM_BITS_8);
  RunWienerTest(WIENER_WIN, 1, AOM_BITS_10);
  RunWienerTest(WIENER_WIN_CHROMA, 1, AOM_BITS_10);
  RunWienerTest(WIENER_WIN, 1, AOM_BITS_12);
  RunWienerTest(WIENER_WIN_CHROMA, 1, AOM_BITS_12);
}

TEST_P(WienerTestHighbd, ExtremeValues) {
  RunWienerTest_ExtremeValues(WIENER_WIN, AOM_BITS_8);
  RunWienerTest_ExtremeValues(WIENER_WIN_CHROMA, AOM_BITS_8);
  RunWienerTest_ExtremeValues(WIENER_WIN, AOM_BITS_10);
  RunWienerTest_ExtremeValues(WIENER_WIN_CHROMA, AOM_BITS_10);
  RunWienerTest_ExtremeValues(WIENER_WIN, AOM_BITS_12);
  RunWienerTest_ExtremeValues(WIENER_WIN_CHROMA, AOM_BITS_12);
}

TEST_P(WienerTestHighbd, Overflow32bTest) {
  RunWienerTest_Overflow32bTest(WIENER_WIN, AOM_BITS_8);
  RunWienerTest_Overflow32bTest(WIENER_WIN_CHROMA, AOM_BITS_8);
  RunWienerTest_Overflow32bTest(WIENER_WIN, AOM_BITS_10);
  RunWienerTest_Overflow32bTest(WIENER_WIN_CHROMA, AOM_BITS_10);
  RunWienerTest_Overflow32bTest(WIENER_WIN, AOM_BITS_12);
  RunWienerTest_Overflow32bTest(WIENER_WIN_CHROMA, AOM_BITS_12);
}
TEST_P(WienerTestHighbd, DISABLED_Speed) {
  RunWienerTest(WIENER_WIN, 200, AOM_BITS_8);
  RunWienerTest(WIENER_WIN_CHROMA, 200, AOM_BITS_8);
  RunWienerTest(WIENER_WIN, 200, AOM_BITS_10);
  RunWienerTest(WIENER_WIN_CHROMA, 200, AOM_BITS_10);
  RunWienerTest(WIENER_WIN, 200, AOM_BITS_12);
  RunWienerTest(WIENER_WIN_CHROMA, 200, AOM_BITS_12);
}

INSTANTIATE_TEST_SUITE_P(C, WienerTestHighbd,
                         ::testing::Values(compute_stats_highbd_opt_c));

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(SSE4_1, WienerTestHighbd,
                         ::testing::Values(av1_compute_stats_highbd_sse4_1));
#endif  // HAVE_SSE4_1

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, WienerTestHighbd,
                         ::testing::Values(av1_compute_stats_highbd_avx2));
#endif  // HAVE_AVX2

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(NEON, WienerTestHighbd,
                         ::testing::Values(av1_compute_stats_highbd_neon));
#endif  // HAVE_NEON

#if HAVE_SVE
INSTANTIATE_TEST_SUITE_P(SVE, WienerTestHighbd,
                         ::testing::Values(av1_compute_stats_highbd_sve));
#endif  // HAVE_SVE

// A test that reproduces b/274668506: signed integer overflow in
// update_a_sep_sym().
TEST(SearchWienerTest, 10bitSignedIntegerOverflowInUpdateASepSym) {
  constexpr int kWidth = 427;
  constexpr int kHeight = 1;
  std::vector<uint16_t> buffer(3 * kWidth * kHeight);
  // The values in the buffer alternate between 0 and 1023.
  uint16_t value = 0;
  for (size_t i = 0; i < buffer.size(); ++i) {
    buffer[i] = value;
    value = 1023 - value;
  }
  unsigned char *img_data = reinterpret_cast<unsigned char *>(buffer.data());

  aom_image_t img;
  EXPECT_EQ(
      aom_img_wrap(&img, AOM_IMG_FMT_I44416, kWidth, kHeight, 1, img_data),
      &img);
  img.cp = AOM_CICP_CP_UNSPECIFIED;
  img.tc = AOM_CICP_TC_UNSPECIFIED;
  img.mc = AOM_CICP_MC_UNSPECIFIED;
  img.range = AOM_CR_FULL_RANGE;

  aom_codec_iface_t *iface = aom_codec_av1_cx();
  aom_codec_enc_cfg_t cfg;
  EXPECT_EQ(aom_codec_enc_config_default(iface, &cfg, AOM_USAGE_ALL_INTRA),
            AOM_CODEC_OK);
  cfg.rc_end_usage = AOM_Q;
  cfg.g_profile = 1;
  cfg.g_bit_depth = AOM_BITS_10;
  cfg.g_input_bit_depth = 10;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  cfg.g_limit = 1;
  cfg.g_lag_in_frames = 0;
  cfg.kf_mode = AOM_KF_DISABLED;
  cfg.kf_max_dist = 0;
  cfg.g_threads = 61;
  cfg.rc_min_quantizer = 2;
  cfg.rc_max_quantizer = 20;
  aom_codec_ctx_t enc;
  EXPECT_EQ(aom_codec_enc_init(&enc, iface, &cfg, AOM_CODEC_USE_HIGHBITDEPTH),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AOME_SET_CQ_LEVEL, 11), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_ROW_MT, 1), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_TILE_ROWS, 4), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AOME_SET_CPUUSED, 3), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_COLOR_RANGE, AOM_CR_FULL_RANGE),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_SKIP_POSTPROC_FILTERING, 1),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AOME_SET_TUNING, AOM_TUNE_SSIM),
            AOM_CODEC_OK);

  // Encode frame
  EXPECT_EQ(aom_codec_encode(&enc, &img, 0, 1, 0), AOM_CODEC_OK);
  aom_codec_iter_t iter = nullptr;
  const aom_codec_cx_pkt_t *pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x1f0011.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, AOM_FRAME_IS_KEY);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Flush encoder
  EXPECT_EQ(aom_codec_encode(&enc, nullptr, 0, 1, 0), AOM_CODEC_OK);
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  EXPECT_EQ(aom_codec_destroy(&enc), AOM_CODEC_OK);
}

// A test that reproduces b/281219978: signed integer overflow in
// update_b_sep_sym().
TEST(SearchWienerTest, 12bitSignedIntegerOverflowInUpdateBSepSym) {
  constexpr int kWidth = 311;
  constexpr int kHeight = 3;
  static const uint16_t buffer[3 * kWidth * kHeight] = {
    // Y plane:
    0, 0, 0, 2156, 2513, 2211, 4095, 4095, 0, 2538, 0, 0, 0, 0, 4095, 0, 258,
    941, 4095, 907, 0, 0, 2325, 2485, 2408, 4095, 1513, 0, 3644, 2080, 4095,
    4095, 0, 2135, 0, 2461, 4095, 0, 4095, 4095, 0, 1987, 0, 3629, 0, 4095,
    3918, 4095, 0, 4095, 4095, 4095, 0, 1065, 0, 2072, 3597, 102, 0, 534, 0, 0,
    0, 4095, 0, 0, 4095, 0, 4095, 0, 4095, 0, 3611, 0, 1139, 4095, 0, 0, 0, 0,
    0, 4095, 0, 0, 0, 0, 4095, 4095, 4095, 0, 0, 0, 3070, 3224, 0, 0, 4095,
    4051, 4095, 0, 4095, 3712, 0, 1465, 4095, 1699, 4095, 4095, 0, 0, 0, 3885,
    0, 4095, 0, 0, 4095, 1686, 4095, 4095, 4095, 4095, 1330, 0, 0, 0, 4095, 0,
    4095, 4095, 3919, 4095, 781, 2371, 2055, 4095, 912, 3710, 0, 2045, 0, 4095,
    4095, 4095, 1811, 0, 1298, 1115, 0, 3327, 0, 0, 4095, 0, 253, 2386, 4095,
    1791, 3657, 1444, 0, 4095, 1918, 4095, 4095, 0, 4095, 305, 1587, 0, 4095, 0,
    3759, 0, 0, 4095, 2387, 4095, 4095, 0, 0, 4095, 4095, 0, 1015, 4095, 0, 768,
    2598, 1667, 130, 4095, 0, 0, 435, 4095, 3683, 4095, 0, 4095, 4095, 1888,
    2828, 4095, 3349, 0, 4095, 4095, 4095, 4095, 0, 4095, 0, 0, 4095, 0, 2491,
    1598, 0, 0, 383, 3712, 4095, 0, 0, 4095, 760, 4095, 4095, 4095, 2030, 4095,
    0, 0, 3236, 0, 1040, 0, 0, 4095, 0, 0, 4095, 4095, 4095, 0, 0, 1043, 3897,
    2446, 233, 1589, 427, 4095, 4095, 4095, 4095, 0, 1656, 3786, 4095, 0, 840,
    4095, 4095, 1429, 4095, 0, 4095, 2734, 4095, 0, 2431, 1801, 278, 0, 4095, 0,
    4095, 0, 0, 420, 0, 0, 746, 0, 0, 3281, 3006, 4095, 4095, 0, 0, 0, 3605,
    4095, 4095, 0, 4095, 4095, 4095, 4095, 2660, 496, 4095, 0, 0, 0, 0, 4095, 0,
    1317, 4095, 4095, 510, 1919, 0, 3893, 0, 4095, 4095, 4095, 4095, 4095, 2071,
    2006, 0, 3316, 4095, 0, 0, 4095, 852, 2982, 0, 2073, 0, 2728, 1499, 4095,
    852, 361, 3137, 4095, 4095, 1502, 1575, 0, 4095, 0, 0, 0, 0, 1585, 4095, 0,
    4095, 0, 3188, 3244, 4095, 2958, 4095, 4095, 0, 4095, 4095, 4095, 1706,
    2896, 4095, 1788, 730, 1146, 4095, 0, 0, 4095, 0, 0, 0, 2791, 3613, 2175,
    2925, 0, 0, 0, 0, 0, 1279, 4095, 4095, 0, 4095, 0, 0, 2336, 0, 3462, 4095,
    0, 4095, 1997, 2328, 2860, 0, 4095, 4095, 3241, 4095, 4095, 4095, 4095,
    4095, 4095, 118, 0, 4095, 4095, 4095, 0, 3734, 0, 0, 0, 4095, 1952, 4095,
    413, 4095, 1183, 4095, 0, 4095, 0, 0, 4095, 4095, 4095, 3805, 0, 1398, 0,
    4095, 0, 0, 0, 4095, 4095, 4095, 2802, 3658, 4095, 4095, 0, 0, 0, 4095, 0,
    897, 0, 4095, 2163, 0, 0, 0, 4095, 1440, 2487, 4095, 4095, 0, 4095, 4095,
    4095, 2808, 0, 1999, 0, 0, 4095, 4095, 4095, 1563, 124, 2179, 754, 0, 0,
    2407, 2798, 0, 4095, 4095, 0, 0, 1929, 0, 0, 0, 1387, 4095, 4095, 0, 0,
    3911, 562, 4095, 0, 4095, 2639, 2673, 4095, 4095, 0, 0, 4095, 4095, 0, 4095,
    4095, 901, 0, 321, 3961, 4095, 0, 4095, 4095, 4095, 0, 0, 0, 0, 3035, 3713,
    3441, 0, 4095, 0, 0, 854, 1544, 3963, 1968, 4095, 0, 0, 0, 0, 2897, 4095, 0,
    4095, 4095, 0, 235, 1011, 4095, 0, 3452, 4095, 4095, 0, 0, 4095, 4095, 4095,
    4095, 4095, 3312, 0, 3064, 4095, 3981, 4095, 4095, 4095, 4095, 4095, 0, 791,
    3243, 4095, 799, 0, 0, 0, 523, 2117, 3776, 0, 4095, 3311, 0, 543, 4095,
    4095, 4095, 0, 0, 4095, 4095, 4095, 4095, 0, 0, 4095, 4095, 225, 0, 1195,
    3070, 1210, 4095, 0, 4095, 498, 782, 0, 0, 4095, 4095, 4095, 4095, 4095,
    1456, 4095, 3898, 1472, 4095, 4095, 0, 4095, 4026, 0, 0, 2354, 1554, 0,
    4095, 0, 2986, 0, 1053, 1228, 0, 0, 4095, 4095, 0, 0, 4095, 0, 0, 4095, 0,
    0, 0, 606, 0, 4095, 3563, 4095, 2016, 4095, 0, 0, 4095, 0, 4095, 4095, 4095,
    0, 0, 0, 929, 0, 0, 4095, 0, 3069, 4095, 0, 2687, 4095, 4095, 4095, 2015,
    4095, 4095, 4095, 0, 4095, 0, 0, 2860, 3668, 0, 0, 4095, 2523, 2104, 0, 0,
    3063, 4095, 3674, 4095, 0, 2762, 0, 4095, 2582, 3473, 930, 0, 1012, 108, 38,
    4095, 1148, 3568, 4036, 4095, 4095, 0, 1120, 1873, 3028, 4095, 515, 1902,
    4095, 0, 815, 4095, 1548, 0, 1073, 3919, 4095, 2374, 0, 3126, 4095, 2268, 0,
    0, 0, 4095, 425, 4095, 0, 0, 4095, 4095, 2710, 4095, 2067, 4095, 4095, 2201,
    4095, 4095, 0, 4095, 4095, 2933, 0, 417, 2801, 4095, 4095, 3274, 0, 2870,
    4095, 4095, 0, 0, 973, 0, 0, 3129, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 242,
    4095, 0, 4095, 0, 0, 0, 0, 987, 0, 2426, 4045, 2780, 0, 4095, 3762, 3361,
    3095, 4095, 596, 1072, 4071, 4095, 4095, 0, 0, 81, 0, 1001, 1683, 4095,
    4095, 3105, 2673, 0, 3300, 104, 4030, 0, 2615, 4095, 4095, 0, 4095, 1830,
    3917, 4095, 4095, 4095, 0, 4095, 3637, 0, 4095, 4095, 3677, 4095, 4095, 0,
    880, 4095, 4095, 0, 2797, 0, 0, 0, 0, 3225, 4095, 4095, 1925, 2885, 1879, 0,
    0, 4095, 0, 0, 0, 2974, 559, 0, 0, 0, 699, 997, 1491, 423, 4012, 0, 2315,
    4095, 0, 0, 4095, 0, 836, 4095, 0, 4095, 0, 1752, 0, 0, 0, 4095, 4095, 0, 0,
    51, 4095, 350, 0, 2143, 2588, 0, 4095, 0, 4095, 0, 2757, 2370, 4095, 668,
    4095, 0, 4095, 0, 3652, 3890, 0, 4095, 0, 4095, 4095, 4095, 4095, 4095,
    // U plane:
    4095, 4095, 1465, 0, 588, 4095, 0, 4095, 4095, 4095, 0, 2167, 4095, 4095,
    918, 3223, 4095, 4095, 0, 696, 4095, 4095, 0, 0, 594, 4095, 2935, 0, 0, 0,
    2036, 4095, 0, 2492, 4095, 4095, 0, 0, 0, 3883, 0, 4095, 483, 4095, 4095,
    324, 923, 0, 3079, 0, 4095, 4095, 810, 0, 3371, 4095, 4095, 0, 4095, 2756,
    0, 723, 0, 3338, 1084, 0, 4095, 4095, 3764, 0, 4095, 4095, 4095, 2323, 0,
    3693, 682, 0, 0, 909, 4095, 2348, 4095, 4095, 4095, 1509, 4095, 0, 4095,
    4095, 4095, 4095, 3977, 3652, 1580, 637, 4095, 0, 593, 4095, 1199, 1773,
    4095, 4095, 4095, 0, 3447, 0, 0, 4095, 3873, 0, 0, 2094, 0, 1195, 0, 3892,
    4095, 4095, 729, 4095, 0, 0, 4095, 449, 4095, 4095, 2900, 0, 4095, 0, 2114,
    4095, 4095, 4095, 1174, 995, 2933, 360, 0, 1970, 0, 4095, 1208, 0, 4095, 0,
    4095, 0, 4095, 4095, 0, 4095, 0, 0, 0, 1976, 0, 0, 921, 4095, 4095, 192,
    1006, 0, 0, 2725, 4095, 0, 2813, 0, 0, 2375, 4095, 1982, 0, 2725, 4095,
    1225, 3566, 4095, 0, 344, 863, 2747, 0, 4095, 4095, 1928, 4095, 4095, 0,
    3640, 0, 1744, 3191, 4095, 4095, 0, 4095, 4095, 4095, 0, 0, 748, 4095, 0,
    2609, 0, 0, 0, 0, 0, 3508, 4095, 4095, 2463, 0, 4095, 0, 4095, 4095, 4095,
    3175, 419, 2193, 0, 0, 4095, 0, 0, 4095, 4051, 2159, 4095, 4095, 2262, 379,
    4095, 0, 0, 3399, 4095, 4095, 4095, 3769, 2510, 4054, 3336, 730, 3968, 0, 0,
    3354, 0, 1822, 0, 4095, 0, 3847, 3823, 3262, 0, 0, 2936, 0, 4095, 4095,
    2120, 0, 3147, 0, 2838, 3480, 474, 1194, 4095, 4095, 2820, 4095, 0, 4095,
    1882, 4095, 1085, 0, 4095, 2234, 3371, 4095, 0, 4095, 0, 0, 0, 2586, 4095,
    4095, 4095, 4095, 0, 3818, 1401, 2273, 4095, 0, 4095, 0, 3907, 4095, 4095,
    694, 0, 4066, 4095, 0, 0, 4095, 2116, 4095, 4095, 4095, 4095, 4095, 0, 2821,
    29, 0, 0, 663, 1711, 652, 1271, 4095, 4095, 2401, 3726, 4095, 3453, 1803,
    3614, 0, 4095, 3439, 4095, 0, 4095, 0, 816, 0, 0, 4095, 4095, 2635, 0, 1918,
    0, 2663, 381, 0, 0, 3670, 0, 4095, 3065, 965, 4095, 4095, 4095, 2993, 4095,
    4095, 0, 4095, 973, 4095, 0, 4095, 4095, 0, 3071, 0, 2777, 4095, 4095, 0,
    3996, 4095, 1637, 0, 4095, 67, 3784, 0, 0, 4095, 2603, 579, 4095, 4095,
    2854, 4095, 3016, 0, 4095, 0, 0, 4095, 4095, 4095, 4095, 3998, 3023, 4095,
    4095, 0, 0, 0, 4095, 4095, 4095, 4095, 0, 0, 2623, 1308, 55, 4095, 0, 0,
    2554, 2311, 0, 4095, 4095, 4095, 1134, 2112, 0, 4095, 4095, 0, 4095, 0, 645,
    0, 0, 4095, 0, 909, 0, 0, 1719, 4095, 0, 3542, 0, 575, 0, 4095, 4095, 4095,
    3428, 1172, 481, 1521, 4095, 3199, 1265, 4095, 3518, 4017, 4095, 760, 2042,
    3986, 0, 4095, 42, 4095, 0, 4095, 4095, 4095, 4095, 2235, 346, 3865, 0,
    4095, 4095, 4095, 4095, 4095, 4095, 845, 4095, 0, 2826, 4095, 4095, 0, 0,
    335, 1614, 1465, 0, 4095, 4095, 0, 2771, 4095, 0, 2810, 4095, 4095, 0, 1254,
    4095, 2589, 4095, 4095, 2252, 0, 0, 0, 4095, 0, 73, 4095, 4095, 0, 1341, 0,
    0, 0, 0, 4095, 0, 0, 2645, 1985, 492, 914, 3996, 4095, 4095, 4095, 0, 2383,
    2556, 433, 0, 4095, 1094, 4095, 4095, 642, 4095, 1722, 0, 3460, 4095, 4095,
    4095, 4095, 4095, 0, 154, 4095, 92, 4095, 0, 0, 0, 4095, 0, 4095, 4095, 444,
    0, 2925, 0, 0, 0, 0, 1628, 0, 4095, 1731, 2418, 697, 4095, 0, 2513, 4095, 0,
    4095, 4095, 4095, 4095, 4095, 0, 2510, 4095, 3850, 0, 0, 4095, 2480, 4095,
    4095, 2661, 4095, 0, 4095, 0, 0, 4095, 4095, 847, 4095, 4095, 3257, 443, 0,
    67, 0, 0, 0, 4095, 0, 0, 3073, 4095, 0, 4095, 0, 4095, 0, 4095, 1224, 4095,
    4095, 4095, 0, 4095, 958, 0, 4095, 0, 2327, 684, 0, 0, 0, 0, 4095, 4095, 0,
    3693, 795, 4095, 0, 621, 1592, 2314, 4095, 0, 928, 1897, 4095, 4095, 0,
    4095, 0, 0, 4095, 2619, 4095, 0, 4095, 0, 0, 4095, 2485, 4095, 4095, 0, 435,
    4095, 1818, 4095, 4095, 0, 0, 0, 4095, 4095, 4095, 4095, 0, 1671, 4095,
    4095, 0, 2617, 0, 2572, 0, 0, 4095, 3471, 0, 0, 4095, 2719, 3979, 1307, 0,
    0, 0, 0, 1794, 642, 447, 913, 4095, 3927, 0, 2686, 0, 0, 4095, 0, 857, 0,
    4095, 4095, 567, 2385, 0, 0, 4095, 893, 0, 289, 0, 0, 0, 4095, 4095, 2566,
    0, 1913, 0, 2350, 1033, 2764, 0, 4095, 0, 4095, 0, 0, 0, 0, 4095, 3952,
    3969, 0, 3476, 0, 4095, 4095, 393, 0, 2613, 0, 0, 1422, 0, 3359, 491, 3263,
    4095, 4095, 0, 0, 4095, 697, 3601, 4095, 0, 4095, 4095, 0, 4095, 0, 0, 4095,
    0, 4095, 4095, 4095, 2506, 0, 0, 1403, 0, 3836, 3976, 0, 4095, 4095, 4095,
    2497, 4095, 4095, 4095, 4095, 0, 4095, 3317, 4095, 4095, 4095, 0, 0, 1131,
    0, 0, 0, 4095, 0, 0, 4095, 0, 0, 2988, 4095, 4095, 2711, 2487, 1335, 0, 0,
    0, 4095, 261, 4095, 86, 0, 0, 1138, 4095, 0, 0, 4095, 4095, 0, 0, 0, 334, 0,
    2395, 3297, 4095, 1698, 4095, 1791, 1341, 0, 3559, 0, 4095, 0, 2056, 3238,
    3310, 4095, 4095, 779, 2129, 2849, 4095, 2622, 1051, 0, 0, 1282, 4095, 1246,
    0, 0, 3696, 4095, 556, 0, 0, 3463, 2658, 3572, 4095, 3982, 4095, 4095, 0, 0,
    4053, 4095, 4095, 4095, 2162, 2567, 1621, 4095, 4095, 1522, 293, 4095, 0, 0,
    1976, 4095, 3089, 4095, 0, 0, 0, 0, 3650,
    // V plane:
    0, 1892, 4095, 1995, 0, 0, 0, 2208, 1152, 1794, 4095, 4095, 89, 3333, 4095,
    2478, 4095, 2505, 4095, 0, 2664, 4095, 1984, 0, 1144, 4095, 0, 4095, 0,
    4095, 0, 0, 0, 2404, 1727, 4095, 4095, 0, 1326, 2033, 0, 4095, 0, 4095,
    3022, 0, 4095, 0, 1980, 4095, 0, 2284, 4095, 0, 3422, 0, 4095, 2171, 3155,
    4095, 0, 4095, 0, 636, 0, 0, 4095, 3264, 3862, 0, 2164, 0, 0, 3879, 3886, 0,
    225, 0, 0, 4095, 0, 1956, 523, 464, 738, 0, 1545, 0, 2829, 4095, 4095, 4095,
    799, 4095, 358, 4095, 0, 0, 953, 0, 0, 2081, 4095, 1604, 4095, 2086, 0, 954,
    0, 0, 2393, 2413, 4095, 4095, 0, 3583, 4095, 4095, 2995, 4095, 0, 4095,
    4095, 3501, 4095, 247, 4095, 0, 0, 0, 4095, 1303, 3382, 1059, 4095, 0, 543,
    1276, 1801, 0, 0, 0, 2928, 0, 4095, 3931, 70, 0, 0, 3992, 4095, 1278, 1930,
    4095, 0, 4095, 4095, 3894, 0, 0, 0, 0, 4095, 0, 0, 0, 0, 0, 0, 4095, 4095,
    4095, 1098, 4095, 2059, 0, 380, 3166, 0, 4095, 2215, 0, 0, 2846, 0, 0, 2614,
    528, 4095, 0, 4095, 2371, 0, 4095, 0, 0, 0, 0, 4095, 3133, 4095, 4095, 0,
    4095, 1283, 3821, 1772, 0, 0, 4095, 4095, 4095, 890, 3475, 4095, 4095, 133,
    3292, 1819, 4095, 4095, 4095, 0, 0, 4095, 702, 4095, 0, 0, 0, 4095, 0, 2137,
    4095, 4095, 4095, 0, 0, 0, 4095, 4095, 1555, 2435, 2778, 4095, 0, 4095,
    3825, 0, 3736, 3054, 0, 0, 4095, 4095, 4095, 0, 0, 0, 0, 371, 4095, 4095, 0,
    0, 1565, 4095, 2731, 4095, 0, 756, 925, 0, 0, 0, 4095, 775, 1379, 4095,
    1439, 0, 0, 0, 2680, 0, 0, 4095, 1280, 4095, 0, 0, 4095, 4095, 0, 3088, 0,
    4095, 4095, 4095, 0, 0, 1526, 4095, 2314, 4095, 4095, 0, 4095, 288, 0, 205,
    4095, 4095, 4095, 0, 1247, 2014, 0, 1530, 1985, 0, 0, 4095, 3195, 0, 4095,
    4, 2397, 4095, 4095, 4095, 0, 4095, 4095, 4095, 0, 0, 0, 0, 0, 4031, 928,
    4095, 0, 0, 4095, 4095, 4095, 1966, 4095, 2299, 1215, 4095, 0, 4095, 1335,
    0, 4095, 1991, 4095, 0, 4095, 114, 0, 0, 0, 2123, 2639, 4095, 3323, 4095,
    4095, 418, 209, 0, 0, 4095, 4095, 4095, 4095, 963, 0, 0, 0, 4095, 2505, 0,
    3627, 0, 311, 3748, 2047, 4095, 2791, 0, 3643, 1852, 0, 0, 4095, 0, 2179, 0,
    4095, 2678, 0, 0, 0, 2342, 4095, 4095, 0, 0, 4095, 0, 0, 0, 0, 1076, 0, 0,
    4095, 0, 2370, 0, 3530, 0, 0, 0, 0, 0, 4095, 0, 0, 0, 3474, 1201, 0, 379,
    699, 4095, 777, 4095, 0, 4095, 4095, 0, 1213, 1762, 4095, 4095, 4095, 0,
    4095, 1090, 1233, 0, 4095, 0, 4095, 0, 0, 0, 2845, 3385, 2718, 0, 0, 2975,
    3630, 0, 4095, 4095, 4095, 4095, 3261, 243, 0, 4095, 0, 0, 3836, 4095, 4095,
    4095, 963, 0, 0, 2526, 0, 4095, 4000, 4095, 2069, 0, 0, 4095, 0, 4095, 1421,
    0, 4095, 0, 4095, 4095, 0, 4095, 0, 4095, 4095, 1537, 4095, 3201, 0, 0,
    4095, 2719, 4095, 0, 4095, 4095, 4095, 0, 4095, 0, 4095, 2300, 0, 2876, 0,
    4095, 4095, 4095, 3235, 497, 635, 0, 1480, 4095, 0, 3067, 3979, 3741, 0,
    3059, 1214, 4095, 4095, 2197, 0, 4095, 4095, 2734, 0, 4095, 4095, 3364,
    2369, 4095, 303, 4095, 0, 4095, 4095, 3472, 1733, 4095, 4095, 4095, 0, 55,
    0, 10, 1378, 1169, 4095, 0, 0, 688, 3613, 0, 4095, 2832, 867, 4095, 4095,
    3514, 4095, 0, 4095, 4095, 2458, 3506, 0, 1920, 0, 1762, 1178, 2549, 4095,
    3967, 4095, 0, 2975, 1282, 0, 377, 846, 3434, 97, 0, 0, 1616, 3526, 136,
    1888, 0, 147, 334, 4095, 0, 4095, 0, 4095, 1106, 4095, 0, 4095, 3280, 4095,
    4095, 0, 2849, 3528, 0, 4095, 4095, 0, 2306, 0, 3412, 0, 4095, 4095, 4095,
    4048, 2273, 0, 4095, 4095, 4095, 0, 4095, 3031, 4095, 4095, 4095, 0, 3382,
    3812, 2315, 4095, 0, 0, 0, 432, 4095, 3606, 0, 4, 2847, 4095, 0, 4095, 0, 0,
    2616, 4095, 4095, 0, 4095, 0, 3394, 4095, 3976, 3119, 0, 0, 0, 0, 4046,
    4095, 4095, 3331, 4095, 2127, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 4095,
    4095, 4095, 0, 2068, 0, 0, 3882, 2967, 0, 1745, 4095, 2112, 478, 0, 4095, 0,
    199, 4095, 4095, 3542, 4095, 2634, 4095, 4095, 1235, 4095, 4095, 167, 1553,
    0, 4095, 2649, 0, 3383, 0, 4095, 2803, 4095, 0, 4095, 0, 785, 4095, 0, 4095,
    1743, 4095, 0, 3945, 0, 4095, 1894, 4095, 3973, 4095, 0, 0, 4095, 0, 0,
    4095, 318, 4095, 4095, 4095, 0, 261, 4095, 4095, 2125, 2690, 4095, 0, 4095,
    3863, 1740, 4095, 0, 2899, 1509, 0, 0, 0, 2780, 4095, 1897, 2104, 4095,
    1708, 284, 4095, 0, 4095, 3382, 4095, 4095, 483, 0, 0, 0, 3099, 0, 4095, 0,
    926, 4095, 2062, 1931, 2121, 0, 4095, 0, 2485, 1535, 4095, 4095, 3662, 4095,
    2419, 2487, 0, 4095, 4095, 4095, 0, 0, 4095, 0, 0, 2029, 0, 3008, 2338, 0,
    4095, 0, 3854, 0, 4095, 0, 0, 1315, 0, 0, 0, 0, 3492, 0, 1445, 0, 11, 4095,
    0, 0, 873, 0, 4095, 0, 4095, 2654, 3040, 0, 0, 0, 4095, 0, 68, 4095, 0, 0,
    990, 0, 828, 1015, 88, 3606, 0, 2875, 4095, 0, 3117, 411, 0, 0, 2859, 0, 0,
    4095, 3480, 25, 4095, 4095, 4095, 0, 0, 0, 4095, 4095, 4095, 4095, 1724, 0,
    0, 0, 3635, 1063, 3728, 4095, 4095, 2025, 3715, 0, 0, 0, 3722, 0, 1648, 0,
    4095, 3579, 0, 0, 0, 4095, 4095, 0, 4095
  };
  unsigned char *img_data =
      reinterpret_cast<unsigned char *>(const_cast<uint16_t *>(buffer));

  aom_image_t img;
  EXPECT_EQ(
      aom_img_wrap(&img, AOM_IMG_FMT_I44416, kWidth, kHeight, 1, img_data),
      &img);
  img.cp = AOM_CICP_CP_UNSPECIFIED;
  img.tc = AOM_CICP_TC_UNSPECIFIED;
  img.mc = AOM_CICP_MC_UNSPECIFIED;
  img.range = AOM_CR_FULL_RANGE;

  aom_codec_iface_t *iface = aom_codec_av1_cx();
  aom_codec_enc_cfg_t cfg;
  EXPECT_EQ(aom_codec_enc_config_default(iface, &cfg, AOM_USAGE_ALL_INTRA),
            AOM_CODEC_OK);
  cfg.rc_end_usage = AOM_Q;
  cfg.g_profile = 2;
  cfg.g_bit_depth = AOM_BITS_12;
  cfg.g_input_bit_depth = 12;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  cfg.g_limit = 1;
  cfg.g_lag_in_frames = 0;
  cfg.kf_mode = AOM_KF_DISABLED;
  cfg.kf_max_dist = 0;
  cfg.g_threads = 34;
  cfg.rc_min_quantizer = 8;
  cfg.rc_max_quantizer = 20;
  aom_codec_ctx_t enc;
  EXPECT_EQ(aom_codec_enc_init(&enc, iface, &cfg, AOM_CODEC_USE_HIGHBITDEPTH),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AOME_SET_CQ_LEVEL, 14), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_ROW_MT, 1), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_TILE_ROWS, 4), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_TILE_COLUMNS, 4), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AOME_SET_CPUUSED, 0), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_COLOR_RANGE, AOM_CR_FULL_RANGE),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_SKIP_POSTPROC_FILTERING, 1),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AOME_SET_TUNING, AOM_TUNE_SSIM),
            AOM_CODEC_OK);

  // Encode frame
  EXPECT_EQ(aom_codec_encode(&enc, &img, 0, 1, 0), AOM_CODEC_OK);
  aom_codec_iter_t iter = nullptr;
  const aom_codec_cx_pkt_t *pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x1f0011.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, AOM_FRAME_IS_KEY);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Flush encoder
  EXPECT_EQ(aom_codec_encode(&enc, nullptr, 0, 1, 0), AOM_CODEC_OK);
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  EXPECT_EQ(aom_codec_destroy(&enc), AOM_CODEC_OK);
}

// A test that reproduces crbug.com/oss-fuzz/66474: signed integer overflow in
// update_b_sep_sym().
TEST(SearchWienerTest, 12bitSignedIntegerOverflowInUpdateBSepSym2) {
  constexpr int kWidth = 510;
  constexpr int kHeight = 3;
  static const uint16_t buffer[kWidth * kHeight] = {
    // Y plane:
    2136, 4095, 0,    0,    0,    4095, 4095, 0,    4095, 4095, 329,  0,
    4095, 0,    4095, 2587, 0,    0,    0,    4095, 0,    0,    0,    0,
    4095, 0,    4095, 878,  0,    4095, 0,    4095, 1474, 0,    573,  0,
    2401, 0,    1663, 4095, 0,    9,    3381, 0,    1084, 0,    270,  0,
    4095, 4095, 4095, 3992, 4095, 2047, 0,    0,    0,    4095, 41,   0,
    2726, 279,  0,    0,    4095, 0,    0,    1437, 0,    4095, 4095, 0,
    0,    0,    4095, 1683, 183,  3976, 3052, 0,    4095, 0,    0,    0,
    4095, 4095, 1882, 4095, 0,    4095, 83,   4095, 0,    4095, 0,    0,
    4095, 4095, 0,    0,    1637, 4095, 0,    4095, 0,    4095, 4095, 4095,
    0,    4095, 197,  4095, 563,  0,    3696, 3073, 3670, 0,    4095, 4095,
    0,    0,    0,    4095, 0,    0,    0,    0,    4095, 4095, 0,    0,
    0,    3539, 3468, 0,    2856, 3880, 0,    0,    1350, 2358, 4095, 802,
    4051, 0,    4095, 4095, 4095, 1677, 4095, 1135, 0,    4095, 0,    0,
    0,    618,  4095, 4095, 4095, 0,    2080, 4095, 0,    0,    1917, 0,
    0,    4095, 1937, 2835, 4095, 4095, 4095, 4095, 0,    4095, 4095, 3938,
    1707, 0,    0,    0,    4095, 448,  4095, 0,    1000, 2481, 3408, 0,
    0,    4095, 0,    3176, 0,    4095, 0,    4095, 4095, 4095, 0,    160,
    222,  1134, 4095, 4095, 0,    3539, 4095, 569,  3364, 0,    4095, 3687,
    0,    4095, 0,    0,    473,  0,    0,    4095, 298,  0,    3126, 4095,
    3854, 424,  0,    0,    4095, 3893, 0,    0,    175,  2774, 0,    4095,
    0,    2661, 950,  4095, 0,    1553, 0,    4095, 0,    4095, 4095, 2767,
    3630, 799,  255,  0,    4095, 0,    0,    4095, 2375, 0,    0,    0,
    0,    4095, 4095, 0,    0,    0,    1404, 4095, 4095, 4095, 4095, 2317,
    4095, 1227, 2205, 775,  0,    4095, 0,    0,    797,  1125, 736,  1773,
    2996, 4095, 2822, 4095, 4095, 0,    0,    0,    919,  0,    968,  3426,
    2702, 2613, 3647, 0,    0,    4095, 4095, 129,  4095, 0,    0,    4095,
    0,    0,    3632, 0,    3275, 123,  4095, 1566, 0,    0,    0,    1609,
    0,    1466, 4095, 577,  4095, 4095, 0,    4095, 1103, 1103, 4095, 0,
    1909, 0,    4095, 0,    4095, 4095, 227,  0,    4095, 2168, 4095, 374,
    4095, 4095, 4095, 0,    0,    0,    4095, 2066, 4095, 4095, 1475, 0,
    1959, 673,  4095, 0,    4095, 4095, 4095, 1142, 0,    464,  1819, 2033,
    4095, 0,    2212, 4095, 4095, 3961, 0,    4095, 0,    2838, 0,    4095,
    4095, 4095, 4095, 0,    3796, 3379, 2208, 0,    4095, 4095, 1943, 478,
    3573, 4095, 1763, 0,    0,    4095, 4095, 4095, 4095, 2061, 3346, 4095,
    0,    0,    4095, 0,    4095, 4095, 4095, 3738, 4095, 4095, 0,    4095,
    0,    425,  0,    0,    0,    927,  0,    0,    1814, 966,  4095, 0,
    0,    3185, 570,  3883, 2932, 0,    1413, 4095, 4095, 4095, 4095, 2477,
    2270, 4095, 2531, 4095, 1936, 3110, 99,   3936, 4095, 1315, 4095, 0,
    4095, 3564, 4095, 0,    0,    2797, 4095, 0,    1598, 0,    0,    3064,
    3526, 4095, 4095, 0,    3473, 3661, 0,    2388, 0,    4095, 639,  4095,
    0,    4095, 2390, 3715, 4095, 0,    0,    0,    740,  4095, 1432, 0,
    0,    0,    4057, 0,    0,    757,  4095, 4095, 0,    1437, 0,    0,
    4095, 0,    0,    0,    0,    0,    272,  4095, 4095, 4095, 2175, 4058,
    0,    4095, 4095, 4095, 3959, 3535, 0,    4095, 0,    0,    4095, 4095,
    4095, 4095, 0,    0,    4095, 4095, 4095, 3440, 3811, 0,    4095, 4095,
    4095, 4095, 0,    4095, 3193, 3674, 2819, 4095, 4095, 4048, 0,    0,
    4037, 4095, 3110, 4095, 1003, 0,    3650, 4095, 4095, 3154, 0,    1274,
    2192, 4095, 0,    4095, 0,    2814, 981,  370,  1407, 0,    4095, 1518,
    4095, 0,    0,    0,    0,    4095, 1577, 0,    4095, 0,    2607, 4095,
    3583, 0,    0,    4095, 1983, 1498, 4095, 4095, 2645, 4095, 4095, 3480,
    2587, 4095, 0,    0,    0,    0,    4095, 0,    4095, 4095, 0,    284,
    3973, 0,    0,    3677, 2463, 4095, 1338, 0,    4095, 0,    0,    4095,
    212,  2000, 4095, 4095, 0,    4095, 3780, 2039, 4095, 2453, 4095, 2050,
    2660, 1,    3839, 5,    1,    505,  809,  2907, 0,    0,    0,    1421,
    4095, 0,    0,    4095, 4095, 4095, 552,  0,    0,    4095, 3056, 0,
    0,    0,    0,    0,    4095, 0,    3386, 0,    0,    0,    4095, 0,
    0,    3404, 2702, 3534, 4095, 3562, 0,    4095, 4095, 150,  4095, 0,
    0,    3599, 4095, 4095, 0,    0,    0,    4095, 4095, 2093, 4095, 3753,
    3754, 4095, 0,    4095, 2733, 4095, 4095, 0,    0,    4095, 0,    0,
    0,    1496, 4095, 2366, 2936, 2494, 4095, 744,  1173, 4095, 0,    0,
    0,    1966, 4095, 4095, 0,    178,  3254, 4095, 4095, 995,  4095, 2083,
    0,    2639, 4095, 3422, 4095, 4095, 4095, 0,    842,  4095, 4095, 552,
    3681, 4095, 0,    1075, 2631, 554,  0,    0,    4095, 0,    0,    0,
    4095, 4095, 0,    0,    0,    2234, 0,    1098, 4095, 3164, 4095, 0,
    2748, 0,    0,    0,    4095, 4095, 4095, 1724, 891,  3496, 3964, 4095,
    0,    0,    1923, 4095, 4095, 4095, 3118, 0,    0,    0,    4095, 4095,
    0,    0,    3856, 4095, 0,    0,    4095, 4095, 2647, 0,    2089, 4095,
    471,  0,    4095, 0,    0,    0,    4095, 0,    1263, 2969, 289,  0,
    0,    4095, 289,  0,    0,    2965, 0,    0,    3280, 2279, 4091, 5,
    512,  1776, 4,    2046, 3994, 1,    4095, 898,  4095, 0,    0,    0,
    0,    4095, 0,    4095, 4095, 1930, 0,    0,    3725, 4095, 4095, 0,
    2593, 4095, 0,    4095, 984,  0,    4095, 2388, 0,    0,    4095, 4095,
    3341, 4095, 0,    2787, 0,    831,  2978, 4095, 0,    0,    0,    4095,
    1624, 4095, 1054, 1039, 0,    89,   3565, 0,    4095, 468,  0,    4095,
    4095, 0,    4095, 4095, 0,    3907, 0,    0,    0,    0,    0,    0,
    4095, 1898, 2178, 4095, 0,    3708, 2825, 0,    4095, 0,    4095, 4095,
    0,    0,    811,  1078, 0,    4095, 0,    3478, 0,    0,    1127, 0,
    504,  4095, 4095, 2006, 4095, 0,    2666, 1172, 4095, 4095, 4095, 4095,
    4095, 0,    199,  4095, 0,    2355, 2650, 2961, 0,    0,    0,    4095,
    4095, 0,    4095, 0,    4095, 1477, 0,    0,    1946, 0,    3352, 1988,
    0,    0,    2321, 4095, 0,    4095, 3367, 0,    0,    4095, 4095, 1946,
    0,    4034, 0,    0,    4095, 4095, 0,    0,    0,    0,    4095, 973,
    1734, 3966, 4095, 0,    3780, 1242, 0,    4095, 1301, 0,    1513, 4095,
    1079, 4095, 0,    0,    1316, 4095, 4095, 675,  2713, 2006, 4095, 4095,
    0,    0,    4095, 4095, 0,    3542, 4095, 0,    2365, 130,  4095, 2919,
    0,    4095, 3434, 0,    905,  4095, 673,  4095, 4095, 0,    3923, 293,
    4095, 213,  4095, 4095, 1334, 4095, 0,    3317, 0,    0,    0,    4095,
    4095, 4095, 2598, 2010, 0,    0,    3507, 0,    0,    0,    489,  0,
    0,    1782, 2681, 3303, 4095, 4095, 1955, 4095, 4095, 4095, 203,  1973,
    4095, 4020, 0,    4095, 1538, 0,    373,  1934, 4095, 0,    4095, 2244,
    4095, 1936, 4095, 640,  0,    4095, 0,    0,    0,    3653, 4095, 1966,
    4095, 4095, 4095, 4095, 0,    4095, 843,  0,    4095, 4095, 4095, 1646,
    4095, 0,    0,    4095, 4095, 4095, 2164, 0,    0,    0,    2141, 4095,
    0,    903,  4095, 4095, 0,    624,  4095, 792,  0,    0,    0,    0,
    0,    0,    0,    4095, 0,    4095, 4095, 2466, 0,    3631, 0,    4095,
    4095, 4095, 0,    941,  4095, 4095, 1609, 4095, 4095, 0,    0,    2398,
    4095, 4095, 2579, 0,    4020, 3485, 0,    0,    4095, 0,    4095, 0,
    3158, 2355, 0,    4095, 4095, 4095, 0,    0,    4095, 0,    0,    4095,
    475,  2272, 1010, 0,    0,    4095, 0,    0,    4095, 841,  4095, 4095,
    4095, 4095, 0,    4095, 0,    1046, 4095, 1738, 708,  4095, 0,    4095,
    4095, 0,    4095, 4095, 0,    4095, 4095, 0,    0,    0,    4032, 0,
    2679, 0,    1564, 0,    0,    0,    659,  1915, 4095, 3682, 0,    3660,
    4095, 723,  1383, 2499, 1353, 4095, 0,    3898, 2322, 3798, 4095, 0,
    444,  2277, 3729, 4095, 4095, 4095, 3054, 387,  3309, 4048, 3793, 2842,
    2087, 0,    3274, 2454, 518,  0,    4095, 0,    4095, 4095, 3358, 4095,
    2083, 2105, 0,    0,    0,    1125, 2636, 0,    0,    0,    0,    736,
    0,    349,  0,    4095, 2031, 4095, 992,  0,    4095, 3284, 4095, 214,
    3692, 4010, 402,  0,    0,    3776, 4095, 4095, 4095, 4095, 803,  2095,
    3864, 4095, 3323, 0,    0,    361,  1634, 0,    983,  0,    1181, 4095,
    1791, 4095, 367,  792,  4095, 4095, 3315, 3149, 4095, 62,   4095, 1791,
    3708, 2030, 4095, 1237, 0,    4095, 4095, 0,    0,    0,    0,    4095,
    1902, 2257, 4095, 4095, 0,    0,    2929, 4095, 0,    4095, 2356, 4095,
    2877, 1296, 4095, 0,    0,    0,    1310, 1968, 820,  4095, 4095, 4095,
    4095, 4095, 0,    0,    4095, 4095, 4095, 2897, 1787, 2218, 0,    129,
    4095, 4095, 0,    4095, 2331, 4095, 4095, 3192, 4095, 1744, 755,  0,
    1905, 0,    4095, 4095, 4095, 0,    0,    4095, 4095, 4095, 0,    0,
    0,    1467, 266,  1719, 4095, 729,  4095, 4095, 2647, 3543, 3388, 3326,
    4095, 0,    4095, 4095, 4095, 1416, 4095, 2131, 810,  0,    0,    4095,
    4095, 1250, 0,    0,    4095, 2722, 1493, 4095, 0,    4095, 0,    2895,
    0,    3847, 0,    2078, 0,    0,    0,    4095, 4095, 4095, 4095, 0,
    4095, 2651, 4095, 4095, 351,  2675, 4095, 0,    858,  0,    0,    0,
    816,  4095, 0,    4095, 0,    3842, 1990, 593,  0,    0,    3992, 4095,
    4095, 0,    4095, 1314, 4095, 4095, 1864, 2561, 4095, 1339, 0,    4095,
    2201, 4095, 0,    1403, 0,    0,    4095, 4095, 4095, 0,    0,    0,
    0,    0,    0,    577,  4095, 995,  2534, 827,  1431, 4095, 4095, 778,
    1405, 0,    0,    4095, 0,    4095, 1327, 4095, 0,    2725, 3351, 3937,
    741,  0,    2690, 2849, 4095, 4095, 2151, 0,    4095, 0,    4095, 4095,
    4095, 1342, 142,  1920, 1007, 2001
  };
  unsigned char *img_data =
      reinterpret_cast<unsigned char *>(const_cast<uint16_t *>(buffer));

  aom_image_t img;
  EXPECT_EQ(&img, aom_img_wrap(&img, AOM_IMG_FMT_I42016, kWidth, kHeight, 1,
                               img_data));
  img.cp = AOM_CICP_CP_UNSPECIFIED;
  img.tc = AOM_CICP_TC_UNSPECIFIED;
  img.mc = AOM_CICP_MC_UNSPECIFIED;
  img.monochrome = 1;
  img.csp = AOM_CSP_UNKNOWN;
  img.range = AOM_CR_FULL_RANGE;
  img.planes[1] = img.planes[2] = nullptr;
  img.stride[1] = img.stride[2] = 0;

  aom_codec_iface_t *iface = aom_codec_av1_cx();
  aom_codec_enc_cfg_t cfg;
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_enc_config_default(iface, &cfg, AOM_USAGE_GOOD_QUALITY));
  cfg.rc_end_usage = AOM_Q;
  cfg.g_profile = 2;
  cfg.g_bit_depth = AOM_BITS_12;
  cfg.g_input_bit_depth = 12;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  cfg.g_lag_in_frames = 0;
  cfg.g_threads = 53;
  cfg.monochrome = 1;
  cfg.rc_min_quantizer = 22;
  cfg.rc_max_quantizer = 30;
  aom_codec_ctx_t enc;
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_enc_init(&enc, iface, &cfg, AOM_CODEC_USE_HIGHBITDEPTH));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AOME_SET_CQ_LEVEL, 26));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AV1E_SET_TILE_ROWS, 3));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AOME_SET_CPUUSED, 6));
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_control(&enc, AV1E_SET_COLOR_RANGE, AOM_CR_FULL_RANGE));
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_control(&enc, AOME_SET_TUNING, AOM_TUNE_SSIM));

  // Encode frame
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_encode(&enc, &img, 0, 1, 0));
  aom_codec_iter_t iter = nullptr;
  const aom_codec_cx_pkt_t *pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x1f0011.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, AOM_FRAME_IS_KEY);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Encode frame
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_encode(&enc, &img, 0, 1, AOM_EFLAG_FORCE_KF));
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x1f0011.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, AOM_FRAME_IS_KEY);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Encode frame
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_encode(&enc, &img, 0, 1, 0));
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Encode frame
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_encode(&enc, &img, 0, 1, 0));
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Flush encoder
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_encode(&enc, nullptr, 0, 1, 0));
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  EXPECT_EQ(AOM_CODEC_OK, aom_codec_destroy(&enc));
}

// A test that reproduces b/272139363: signed integer overflow in
// update_b_sep_sym().
TEST(SearchWienerTest, 10bitSignedIntegerOverflowInUpdateBSepSym) {
  constexpr int kWidth = 34;
  constexpr int kHeight = 3;
  static const uint16_t buffer[3 * kWidth * kHeight] = {
    // Y plane:
    61, 765, 674, 188, 367, 944, 153, 275, 906, 433, 154, 51, 8, 855, 186, 154,
    392, 0, 634, 3, 690, 1023, 1023, 1023, 1023, 1023, 1023, 8, 1, 64, 426, 0,
    100, 344, 944, 816, 816, 33, 1023, 1023, 1023, 1023, 295, 1023, 1023, 1023,
    1023, 1023, 1023, 1015, 1023, 231, 1020, 254, 439, 439, 894, 439, 150, 1019,
    1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 385, 320, 575,
    682, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 511, 699, 987, 3, 140,
    661, 120, 33, 143, 0, 0, 0, 3, 40, 625, 585, 16, 579, 160, 867,
    // U plane:
    739, 646, 13, 603, 7, 328, 91, 32, 488, 870, 330, 330, 330, 330, 330, 330,
    109, 330, 330, 330, 3, 545, 945, 249, 35, 561, 801, 32, 931, 639, 801, 91,
    1023, 827, 844, 948, 631, 894, 854, 601, 432, 504, 85, 1, 0, 0, 89, 89, 0,
    0, 0, 0, 0, 0, 432, 801, 382, 4, 0, 0, 2, 89, 89, 89, 89, 89, 89, 384, 0, 0,
    0, 0, 0, 0, 0, 1023, 1019, 1, 3, 691, 575, 691, 691, 691, 691, 691, 691,
    691, 691, 691, 691, 691, 84, 527, 4, 485, 8, 682, 698, 340, 1015, 706,
    // V plane:
    49, 10, 28, 1023, 1023, 1023, 0, 32, 32, 872, 114, 1003, 1023, 57, 477, 999,
    1023, 309, 309, 309, 309, 309, 309, 309, 309, 309, 309, 309, 309, 309, 309,
    9, 418, 418, 418, 418, 418, 418, 0, 0, 0, 1023, 4, 5, 0, 0, 1023, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 64, 0, 155, 709, 3, 331, 807, 633, 1023,
    1018, 646, 886, 991, 692, 915, 294, 0, 35, 2, 0, 471, 643, 770, 346, 176,
    32, 329, 322, 302, 61, 765, 674, 188, 367, 944, 153, 275, 906, 433, 154
  };
  unsigned char *img_data =
      reinterpret_cast<unsigned char *>(const_cast<uint16_t *>(buffer));

  aom_image_t img;
  EXPECT_EQ(&img, aom_img_wrap(&img, AOM_IMG_FMT_I44416, kWidth, kHeight, 1,
                               img_data));
  img.cp = AOM_CICP_CP_UNSPECIFIED;
  img.tc = AOM_CICP_TC_UNSPECIFIED;
  img.mc = AOM_CICP_MC_UNSPECIFIED;
  img.range = AOM_CR_FULL_RANGE;

  aom_codec_iface_t *iface = aom_codec_av1_cx();
  aom_codec_enc_cfg_t cfg;
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_enc_config_default(iface, &cfg, AOM_USAGE_ALL_INTRA));
  cfg.rc_end_usage = AOM_Q;
  cfg.g_profile = 1;
  cfg.g_bit_depth = AOM_BITS_10;
  cfg.g_input_bit_depth = 10;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  cfg.g_limit = 1;
  cfg.g_lag_in_frames = 0;
  cfg.kf_mode = AOM_KF_DISABLED;
  cfg.kf_max_dist = 0;
  cfg.rc_min_quantizer = 3;
  cfg.rc_max_quantizer = 54;
  aom_codec_ctx_t enc;
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_enc_init(&enc, iface, &cfg, AOM_CODEC_USE_HIGHBITDEPTH));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AOME_SET_CQ_LEVEL, 28));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AV1E_SET_TILE_COLUMNS, 3));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AOME_SET_CPUUSED, 0));
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_control(&enc, AV1E_SET_COLOR_RANGE, AOM_CR_FULL_RANGE));
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_control(&enc, AV1E_SET_SKIP_POSTPROC_FILTERING, 1));
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_control(&enc, AOME_SET_TUNING, AOM_TUNE_SSIM));

  // Encode frame
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_encode(&enc, &img, 0, 1, 0));
  aom_codec_iter_t iter = nullptr;
  const aom_codec_cx_pkt_t *pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x1f0011.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, AOM_FRAME_IS_KEY);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Flush encoder
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_encode(&enc, nullptr, 0, 1, 0));
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  EXPECT_EQ(AOM_CODEC_OK, aom_codec_destroy(&enc));
}

// A test that reproduces b/319140742: signed integer overflow in
// update_b_sep_sym().
TEST(SearchWienerTest, 10bitSignedIntegerOverflowInUpdateBSepSym2) {
  constexpr int kWidth = 326;
  constexpr int kHeight = 3;
  static const uint16_t buffer[kWidth * kHeight] = {
    // Y plane:
    1023, 1023, 0,    1023, 1023, 0,    623,  0,    0,    1023, 1023, 0,
    0,    0,    0,    523,  1023, 2,    0,    0,    863,  1023, 1023, 409,
    7,    1023, 0,    409,  1023, 0,    579,  1023, 1023, 1023, 0,    0,
    1023, 1023, 446,  1023, 1023, 0,    0,    1023, 0,    0,    829,  1023,
    0,    1023, 939,  0,    0,    23,   1022, 990,  1023, 0,    0,    4,
    0,    299,  0,    0,    1023, 1023, 629,  688,  1023, 1023, 266,  1023,
    865,  0,    413,  0,    267,  0,    0,    69,   1023, 866,  1023, 885,
    0,    762,  330,  382,  0,    1023, 1023, 734,  504,  899,  119,  0,
    378,  1011, 0,    0,    1023, 364,  0,    1023, 1023, 462,  1023, 0,
    504,  1023, 1023, 0,    695,  1023, 57,   1023, 1023, 362,  0,    0,
    0,    0,    1023, 1023, 387,  12,   929,  1023, 0,    194,  1023, 0,
    1023, 505,  0,    1023, 1023, 1023, 1023, 1023, 0,    0,    676,  0,
    6,    683,  70,   0,    0,    1023, 226,  1023, 320,  758,  0,    0,
    648,  1023, 867,  550,  630,  960,  1023, 1023, 1023, 0,    0,    822,
    0,    0,    0,    1023, 1011, 1023, 1023, 0,    0,    15,   30,   0,
    1023, 1023, 0,    0,    0,    84,   954,  1023, 933,  416,  333,  323,
    0,    0,    1023, 355,  1023, 176,  1023, 1023, 886,  87,   1023, 0,
    1023, 1023, 1023, 562,  0,    1023, 1023, 354,  0,    0,    1023, 0,
    86,   0,    0,    1023, 0,    1023, 192,  0,    1023, 0,    1023, 0,
    0,    0,    735,  1023, 1023, 1023, 0,    372,  988,  131,  1023, 1023,
    0,    1023, 1023, 1023, 1023, 970,  1023, 1023, 248,  757,  665,  330,
    223,  273,  0,    274,  1023, 0,    1023, 613,  786,  1023, 792,  0,
    390,  282,  0,    1023, 0,    1023, 0,    1023, 1023, 1023, 614,  993,
    135,  737,  662,  0,    1023, 524,  970,  1023, 0,    906,  1023, 1023,
    959,  1023, 1023, 1023, 1023, 836,  838,  0,    0,    0,    0,    0,
    1023, 917,  492,  290,  1023, 1023, 817,  1023, 0,    0,    588,  410,
    419,  0,    1023, 1023, 178,  0,    0,    563,  775,  977,  1023, 1023,
    0,    1023, 0,    370,  434,  1023, 963,  587,  0,    0,    1023, 1023,
    1023, 1023, 1023, 1023, 619,  0,    1023, 352,  1023, 0,    0,    0,
    133,  557,  36,   1023, 1023, 1023, 0,    469,  1023, 1023, 0,    900,
    59,   841,  1023, 886,  0,    193,  126,  263,  119,  629,  0,    1023,
    0,    1023, 0,    0,    478,  0,    1023, 63,   1023, 0,    0,    0,
    0,    0,    0,    0,    1023, 888,  1023, 905,  646,  0,    0,    1023,
    752,  1023, 1023, 0,    1023, 0,    0,    648,  1023, 0,    0,    838,
    0,    321,  1023, 475,  0,    215,  867,  1023, 0,    1023, 1023, 624,
    417,  1023, 426,  0,    0,    960,  1020, 839,  687,  1023, 161,  1023,
    1023, 1023, 1023, 968,  0,    95,   430,  0,    132,  1023, 1023, 113,
    0,    1023, 1023, 606,  1023, 0,    0,    31,   1023, 1023, 0,    180,
    140,  654,  1023, 1023, 1023, 1023, 1023, 779,  1023, 0,    0,    1023,
    1023, 1023, 0,    1023, 0,    0,    1023, 963,  723,  536,  1023, 0,
    0,    0,    337,  812,  0,    0,    0,    428,  48,   0,    321,  205,
    0,    587,  799,  272,  5,    1023, 322,  0,    761,  0,    749,  1023,
    0,    0,    1023, 1023, 1023, 1023, 242,  402,  98,   0,    1023, 884,
    219,  1023, 0,    1023, 0,    0,    0,    106,  1023, 0,    1023, 414,
    1023, 0,    1023, 619,  0,    0,    973,  854,  82,   1023, 1023, 1023,
    0,    1023, 1023, 0,    0,    588,  433,  0,    0,    961,  0,    0,
    0,    917,  859,  461,  455,  68,   1023, 409,  1023, 821,  1023, 487,
    1023, 0,    717,  0,    613,  0,    0,    840,  932,  782,  1023, 1023,
    576,  1023, 0,    1023, 1023, 187,  876,  162,  0,    1023, 1023, 946,
    873,  0,    0,    953,  0,    537,  0,    0,    1023, 193,  807,  756,
    0,    0,    1023, 732,  1023, 1023, 1023, 0,    0,    1023, 1023, 1023,
    1023, 1023, 119,  0,    0,    90,   1023, 0,    1023, 0,    0,    0,
    1023, 366,  1023, 655,  0,    58,   1023, 1023, 8,    1023, 1023, 24,
    1023, 103,  0,    0,    1023, 919,  1023, 566,  1023, 0,    0,    480,
    1023, 1023, 0,    0,    807,  0,    1023, 0,    273,  412,  632,  1023,
    1023, 1023, 10,   633,  1023, 692,  978,  0,    0,    1023, 1023, 1023,
    25,   494,  215,  0,    148,  1023, 840,  118,  1023, 1023, 999,  1023,
    1023, 1023, 0,    0,    1023, 435,  894,  0,    1023, 1023, 168,  1023,
    1023, 211,  1023, 1023, 656,  1023, 0,    0,    0,    744,  238,  1023,
    0,    196,  907,  0,    0,    0,    838,  726,  1023, 1023, 1023, 0,
    0,    0,    1023, 0,    1023, 1023, 1023, 0,    1023, 0,    0,    0,
    323,  1023, 1023, 0,    1023, 0,    0,    925,  582,  1023, 0,    685,
    1023, 661,  464,  0,    0,    0,    1023, 0,    807,  0,    1023, 1023,
    1023, 100,  0,    1023, 302,  1023, 1023, 1023, 616,  0,    1023, 0,
    0,    377,  1023, 1023, 1023, 0,    1023, 555,  1023, 784,  0,    0,
    1023, 0,    0,    1023, 755,  0,    839,  1023, 0,    0,    0,    1023,
    1023, 1023, 0,    1023, 413,  0,    1023, 1023, 384,  0,    823,  797,
    1023, 0,    1023, 0,    0,    1023, 1023, 1023, 1023, 0,    1023, 39,
    0,    473,  299,  0,    0,    1023, 567,  1023, 1023, 0,    0,    1023,
    650,  1023, 41,   1023, 0,    1023, 0,    1023, 0,    1023, 0,    0,
    444,  1023, 23,   0,    503,  97,   0,    1023, 0,    890,  59,   578,
    0,    201,  1023, 672,  1023, 593,  1023, 599,  213,  1023, 1023, 1023,
    986,  1023, 335,  1023, 457,  0,    888,  1023, 1023, 97,   308,  259,
    813,  1023, 1023, 1023, 0,    1023, 798,  907,  105,  0,    1023, 0,
    1023, 1023, 0,    970,  518,  0,    635,  0,    634,  329,  1023, 430,
    0,    17,   1023, 1023, 1023, 0,    0,    407,  1023, 1023, 0,    1023,
    0,    0,    0,    0,    1023, 1023, 1023, 402,  1023, 0,    0,    101,
    1023, 1023, 1023, 1023, 1023, 1023, 425,  791,  1023, 1023, 961,  0,
    0,    1023, 474,  1023, 1023, 1023, 1023, 468,  1023, 1023, 0,    1023,
    215,  0,    1023, 1023, 334,  463,  286,  1023, 0,    1023, 0,    1023,
    270,  401,  0,    0,    1023, 0,    794,  0,    0,    0,    1023, 0,
    1023, 172,  317,  905,  950,  0
  };
  unsigned char *img_data =
      reinterpret_cast<unsigned char *>(const_cast<uint16_t *>(buffer));

  aom_image_t img;
  EXPECT_EQ(&img, aom_img_wrap(&img, AOM_IMG_FMT_I42016, kWidth, kHeight, 1,
                               img_data));
  img.cp = AOM_CICP_CP_UNSPECIFIED;
  img.tc = AOM_CICP_TC_UNSPECIFIED;
  img.mc = AOM_CICP_MC_UNSPECIFIED;
  img.monochrome = 1;
  img.csp = AOM_CSP_UNKNOWN;
  img.range = AOM_CR_FULL_RANGE;
  img.planes[1] = img.planes[2] = nullptr;
  img.stride[1] = img.stride[2] = 0;

  aom_codec_iface_t *iface = aom_codec_av1_cx();
  aom_codec_enc_cfg_t cfg;
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_enc_config_default(iface, &cfg, AOM_USAGE_GOOD_QUALITY));
  cfg.rc_end_usage = AOM_Q;
  cfg.g_profile = 0;
  cfg.g_bit_depth = AOM_BITS_10;
  cfg.g_input_bit_depth = 10;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  cfg.g_threads = 6;
  cfg.monochrome = 1;
  cfg.rc_min_quantizer = 54;
  cfg.rc_max_quantizer = 62;
  aom_codec_ctx_t enc;
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_enc_init(&enc, iface, &cfg, AOM_CODEC_USE_HIGHBITDEPTH));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AOME_SET_CQ_LEVEL, 58));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AV1E_SET_TILE_ROWS, 1));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AOME_SET_CPUUSED, 6));
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_control(&enc, AV1E_SET_COLOR_RANGE, AOM_CR_FULL_RANGE));
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_control(&enc, AOME_SET_TUNING, AOM_TUNE_SSIM));

  // Encode frame
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_encode(&enc, &img, 0, 1, 0));
  aom_codec_iter_t iter = nullptr;
  const aom_codec_cx_pkt_t *pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_EQ(pkt, nullptr);

  // Flush encoder
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_encode(&enc, nullptr, 0, 1, 0));
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x1f0011.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, AOM_FRAME_IS_KEY);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  EXPECT_EQ(AOM_CODEC_OK, aom_codec_encode(&enc, nullptr, 0, 1, 0));
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  EXPECT_EQ(AOM_CODEC_OK, aom_codec_destroy(&enc));
}

// A test that reproduces b/277121724: signed integer overflow in
// update_b_sep_sym().
TEST(SearchWienerTest, 8bitSignedIntegerOverflowInUpdateBSepSym) {
  constexpr int kWidth = 198;
  constexpr int kHeight = 3;
  // 8-bit YUV 4:2:2
  static const unsigned char buffer[2 * kWidth * kHeight] = {
    // Y plane:
    35, 225, 56, 91, 8, 142, 137, 143, 224, 49, 217, 57, 202, 163, 159, 246,
    232, 134, 135, 14, 76, 101, 239, 88, 186, 159, 118, 23, 114, 20, 108, 41,
    72, 17, 58, 242, 45, 146, 230, 14, 135, 140, 34, 61, 189, 181, 222, 71, 98,
    221, 5, 199, 244, 85, 229, 163, 105, 87, 144, 105, 64, 150, 36, 233, 235, 1,
    179, 190, 50, 222, 176, 109, 166, 18, 80, 129, 45, 9, 218, 144, 234, 10,
    148, 117, 37, 10, 232, 139, 206, 92, 208, 247, 128, 79, 202, 79, 212, 89,
    185, 152, 206, 182, 83, 105, 21, 86, 150, 84, 21, 165, 34, 251, 174, 240,
    172, 155, 254, 85, 98, 25, 96, 78, 230, 253, 36, 19, 247, 155, 112, 216,
    166, 114, 229, 118, 197, 149, 186, 194, 128, 45, 219, 26, 36, 77, 110, 45,
    252, 238, 183, 161, 171, 96, 232, 108, 73, 61, 243, 58, 155, 38, 91, 209,
    187, 206, 16, 165, 236, 145, 69, 126, 102, 10, 4, 43, 191, 106, 193, 240,
    132, 226, 38, 78, 7, 152, 101, 255, 254, 39, 33, 86, 35, 247, 199, 179, 239,
    198, 165, 58, 190, 171, 226, 94, 158, 21, 190, 151, 75, 176, 11, 53, 199,
    87, 91, 1, 226, 20, 117, 96, 75, 192, 101, 200, 125, 106, 233, 176, 63, 204,
    114, 16, 31, 222, 15, 14, 71, 2, 25, 47, 100, 174, 26, 209, 138, 138, 211,
    147, 164, 204, 9, 104, 135, 250, 9, 201, 88, 218, 71, 251, 61, 199, 0, 34,
    59, 115, 228, 161, 100, 132, 50, 4, 117, 100, 191, 126, 53, 28, 193, 42,
    155, 206, 79, 80, 117, 11, 3, 253, 181, 181, 138, 239, 107, 142, 216, 57,
    202, 126, 229, 250, 60, 62, 150, 128, 95, 32, 251, 207, 236, 208, 247, 183,
    59, 19, 117, 40, 106, 87, 140, 57, 109, 190, 51, 105, 226, 116, 156, 3, 35,
    86, 255, 138, 52, 211, 245, 76, 83, 109, 113, 77, 106, 77, 18, 56, 235, 158,
    24, 53, 151, 104, 152, 21, 15, 46, 163, 144, 217, 168, 154, 44, 80, 25, 11,
    37, 100, 235, 145, 154, 113, 0, 140, 153, 80, 64, 19, 121, 185, 144, 43,
    206, 16, 16, 72, 189, 175, 231, 177, 40, 177, 206, 116, 4, 82, 43, 244, 237,
    22, 252, 71, 194, 106, 4, 112, 0, 108, 137, 126, 80, 122, 142, 43, 205, 22,
    209, 217, 165, 32, 208, 100, 70, 3, 120, 159, 203, 7, 233, 152, 37, 96, 212,
    177, 1, 133, 218, 161, 172, 202, 192, 186, 114, 150, 121, 177, 227, 175, 64,
    127, 153, 113, 91, 198, 0, 111, 227, 226, 218, 71, 62, 5, 43, 128, 27, 3,
    82, 5, 10, 68, 153, 215, 181, 138, 246, 224, 170, 1, 241, 191, 181, 151,
    167, 14, 80, 45, 4, 252, 29, 66, 125, 58, 225, 253, 255, 248, 224, 40, 24,
    236, 46, 11, 219, 154, 134, 12, 76, 72, 97, 239, 50, 39, 85, 182, 55, 219,
    19, 109, 81, 119, 125, 206, 159, 239, 67, 193, 180, 132, 80, 127, 2, 169,
    99, 53, 47, 5, 100, 174, 151, 124, 246, 202, 93, 82, 65, 53, 214, 238, 32,
    218, 15, 254, 153, 95, 79, 189, 67, 233, 47, 83, 48, 125, 144, 206, 82, 69,
    186, 112, 134, 244, 96, 21, 143, 187, 248, 8, 224, 161, 227, 185, 236, 6,
    175, 237, 169, 154, 89, 143, 106, 205, 26, 47, 155, 42, 28, 162, 7, 8, 45,
    // U plane:
    55, 165, 203, 139, 152, 208, 36, 177, 61, 49, 129, 211, 140, 71, 253, 250,
    120, 167, 238, 67, 255, 223, 104, 32, 240, 179, 28, 41, 86, 84, 61, 243,
    169, 212, 201, 0, 9, 236, 89, 194, 204, 75, 228, 250, 27, 81, 137, 29, 255,
    131, 194, 241, 76, 133, 186, 135, 212, 197, 150, 145, 203, 96, 86, 231, 91,
    119, 197, 67, 226, 2, 118, 66, 181, 86, 219, 86, 132, 137, 156, 161, 221,
    18, 55, 170, 35, 206, 201, 193, 38, 63, 229, 29, 110, 96, 14, 135, 229, 99,
    106, 108, 167, 110, 50, 32, 144, 113, 48, 29, 57, 29, 20, 199, 145, 245, 9,
    183, 88, 174, 114, 237, 29, 40, 99, 117, 233, 6, 51, 227, 2, 28, 76, 149,
    190, 23, 240, 73, 113, 10, 73, 240, 105, 220, 129, 26, 144, 214, 34, 4, 24,
    219, 24, 156, 198, 214, 244, 143, 106, 255, 204, 93, 2, 88, 107, 211, 241,
    242, 86, 189, 219, 164, 132, 149, 32, 228, 219, 60, 202, 218, 189, 34, 250,
    160, 158, 36, 212, 212, 41, 233, 61, 92, 121, 170, 220, 192, 232, 255, 124,
    249, 231, 55, 196, 219, 196, 62, 238, 187, 76, 33, 138, 67, 82, 159, 169,
    196, 66, 196, 110, 194, 64, 35, 205, 64, 218, 12, 41, 188, 195, 244, 178,
    17, 80, 8, 149, 39, 110, 146, 164, 162, 215, 227, 107, 103, 47, 52, 95, 3,
    181, 90, 255, 80, 83, 206, 66, 153, 112, 72, 109, 235, 69, 105, 57, 75, 145,
    186, 16, 87, 73, 61, 98, 197, 237, 17, 32, 207, 220, 246, 188, 46, 73, 121,
    84, 252, 164, 111, 21, 98, 13, 170, 174, 170, 231, 77, 10, 113, 9, 217, 11,
    // V plane:
    124, 94, 69, 212, 107, 223, 228, 96, 56, 2, 158, 49, 251, 217, 143, 107,
    113, 17, 84, 169, 208, 43, 28, 37, 176, 54, 235, 150, 135, 135, 221, 94, 50,
    131, 251, 78, 38, 254, 129, 200, 207, 55, 111, 110, 144, 109, 228, 65, 70,
    39, 170, 5, 208, 151, 87, 86, 255, 74, 155, 153, 250, 15, 35, 33, 201, 226,
    117, 119, 220, 238, 133, 229, 69, 122, 160, 114, 245, 182, 13, 65, 2, 228,
    205, 174, 128, 248, 4, 139, 178, 227, 204, 243, 249, 253, 119, 253, 107,
    234, 39, 15, 173, 47, 93, 12, 222, 238, 30, 121, 124, 167, 27, 40, 215, 84,
    172, 130, 66, 43, 165, 55, 225, 79, 84, 153, 59, 110, 64, 176, 54, 123, 82,
    128, 189, 150, 52, 202, 102, 133, 199, 197, 253, 180, 221, 127, 144, 124,
    255, 224, 52, 149, 88, 166, 39, 38, 78, 114, 44, 242, 233, 40, 132, 142,
    152, 213, 112, 244, 221, 7, 52, 206, 246, 51, 182, 160, 247, 154, 183, 209,
    81, 70, 56, 186, 63, 182, 2, 82, 202, 178, 233, 52, 198, 241, 175, 38, 165,
    9, 231, 150, 114, 43, 159, 200, 42, 173, 217, 25, 233, 214, 210, 50, 43,
    159, 231, 102, 241, 246, 77, 76, 115, 77, 81, 114, 194, 182, 236, 0, 236,
    198, 197, 180, 176, 148, 48, 177, 106, 180, 150, 158, 237, 130, 242, 109,
    174, 247, 57, 230, 184, 64, 245, 251, 123, 169, 122, 156, 125, 123, 104,
    238, 1, 235, 187, 53, 67, 38, 50, 139, 123, 149, 111, 72, 80, 17, 175, 186,
    98, 153, 247, 97, 218, 141, 38, 0, 171, 254, 180, 81, 233, 71, 156, 48, 14,
    62, 210, 161, 124, 203, 92
  };
  unsigned char *img_data = const_cast<unsigned char *>(buffer);

  aom_image_t img;
  EXPECT_EQ(aom_img_wrap(&img, AOM_IMG_FMT_I422, kWidth, kHeight, 1, img_data),
            &img);
  img.cp = AOM_CICP_CP_UNSPECIFIED;
  img.tc = AOM_CICP_TC_UNSPECIFIED;
  img.mc = AOM_CICP_MC_UNSPECIFIED;
  img.range = AOM_CR_FULL_RANGE;

  aom_codec_iface_t *iface = aom_codec_av1_cx();
  aom_codec_enc_cfg_t cfg;
  EXPECT_EQ(aom_codec_enc_config_default(iface, &cfg, AOM_USAGE_ALL_INTRA),
            AOM_CODEC_OK);
  cfg.rc_end_usage = AOM_Q;
  cfg.g_profile = 2;
  cfg.g_bit_depth = AOM_BITS_8;
  cfg.g_input_bit_depth = 8;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  cfg.g_limit = 1;
  cfg.g_lag_in_frames = 0;
  cfg.kf_mode = AOM_KF_DISABLED;
  cfg.kf_max_dist = 0;
  cfg.g_threads = 43;
  cfg.rc_min_quantizer = 30;
  cfg.rc_max_quantizer = 50;
  aom_codec_ctx_t enc;
  EXPECT_EQ(aom_codec_enc_init(&enc, iface, &cfg, 0), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AOME_SET_CQ_LEVEL, 40), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_ROW_MT, 1), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_TILE_ROWS, 4), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_TILE_COLUMNS, 1), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AOME_SET_CPUUSED, 2), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_COLOR_RANGE, AOM_CR_FULL_RANGE),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_SKIP_POSTPROC_FILTERING, 1),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AOME_SET_TUNING, AOM_TUNE_SSIM),
            AOM_CODEC_OK);

  // Encode frame
  EXPECT_EQ(aom_codec_encode(&enc, &img, 0, 1, 0), AOM_CODEC_OK);
  aom_codec_iter_t iter = nullptr;
  const aom_codec_cx_pkt_t *pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x1f0011.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, AOM_FRAME_IS_KEY);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Flush encoder
  EXPECT_EQ(aom_codec_encode(&enc, nullptr, 0, 1, 0), AOM_CODEC_OK);
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  EXPECT_EQ(aom_codec_destroy(&enc), AOM_CODEC_OK);
}

// A test that reproduces crbug.com/oss-fuzz/68195: signed integer overflow in
// linsolve_wiener().
TEST(SearchWienerTest, 8bitSignedIntegerOverflowInLinsolveWiener) {
  constexpr int kWidth = 4;
  constexpr int kHeight = 3;
  constexpr unsigned char kBuffer[kWidth * kHeight] = {
    // Y plane:
    50, 167, 190, 194, 27, 29, 204, 182, 133, 239, 64, 179,
  };
  unsigned char *img_data = const_cast<unsigned char *>(kBuffer);

  aom_image_t img;
  EXPECT_EQ(&img,
            aom_img_wrap(&img, AOM_IMG_FMT_I420, kWidth, kHeight, 1, img_data));
  img.cp = AOM_CICP_CP_UNSPECIFIED;
  img.tc = AOM_CICP_TC_UNSPECIFIED;
  img.mc = AOM_CICP_MC_UNSPECIFIED;
  img.monochrome = 1;
  img.csp = AOM_CSP_UNKNOWN;
  img.range = AOM_CR_FULL_RANGE;
  img.planes[1] = img.planes[2] = nullptr;
  img.stride[1] = img.stride[2] = 0;

  aom_codec_iface_t *iface = aom_codec_av1_cx();
  aom_codec_enc_cfg_t cfg;
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_enc_config_default(iface, &cfg, AOM_USAGE_GOOD_QUALITY));
  cfg.rc_end_usage = AOM_Q;
  cfg.g_profile = 0;
  cfg.g_bit_depth = AOM_BITS_8;
  cfg.g_input_bit_depth = 8;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  cfg.g_threads = 32;
  cfg.monochrome = 1;
  cfg.rc_min_quantizer = 50;
  cfg.rc_max_quantizer = 57;
  aom_codec_ctx_t enc;
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_enc_init(&enc, iface, &cfg, 0));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AOME_SET_CQ_LEVEL, 53));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AV1E_SET_TILE_ROWS, 1));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AV1E_SET_TILE_COLUMNS, 1));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AOME_SET_CPUUSED, 6));
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_control(&enc, AV1E_SET_COLOR_RANGE, AOM_CR_FULL_RANGE));
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_control(&enc, AOME_SET_TUNING, AOM_TUNE_SSIM));

  // Encode frame
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_encode(&enc, &img, 0, 1, 0));
  aom_codec_iter_t iter = nullptr;
  const aom_codec_cx_pkt_t *pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_EQ(pkt, nullptr);

  // Encode frame
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_encode(&enc, &img, 0, 1, 0));
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Flush encoder
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_encode(&enc, nullptr, 0, 1, 0));
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x1f0011.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, AOM_FRAME_IS_KEY);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Flush encoder
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_encode(&enc, nullptr, 0, 1, 0));
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x0.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, 0u);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Flush encoder
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_encode(&enc, nullptr, 0, 1, 0));
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  EXPECT_EQ(AOM_CODEC_OK, aom_codec_destroy(&enc));
}

// A test that reproduces b/259173819: signed integer overflow in
// linsolve_wiener().
TEST(SearchWienerTest, 10bitSignedIntegerOverflowInLinsolveWiener) {
  constexpr int kWidth = 3;
  constexpr int kHeight = 3;
  static const uint16_t buffer[3 * kWidth * kHeight] = {
    // Y plane:
    81, 81, 1023, 1020, 81, 1023, 81, 128, 0,
    // U plane:
    273, 273, 273, 273, 273, 273, 273, 273, 273,
    // V plane:
    273, 273, 273, 273, 273, 273, 516, 81, 81
  };
  unsigned char *img_data =
      reinterpret_cast<unsigned char *>(const_cast<uint16_t *>(buffer));

  aom_image_t img;
  EXPECT_EQ(
      aom_img_wrap(&img, AOM_IMG_FMT_I44416, kWidth, kHeight, 1, img_data),
      &img);
  img.cp = AOM_CICP_CP_UNSPECIFIED;
  img.tc = AOM_CICP_TC_UNSPECIFIED;
  img.mc = AOM_CICP_MC_UNSPECIFIED;
  img.range = AOM_CR_FULL_RANGE;

  aom_codec_iface_t *iface = aom_codec_av1_cx();
  aom_codec_enc_cfg_t cfg;
  EXPECT_EQ(aom_codec_enc_config_default(iface, &cfg, AOM_USAGE_ALL_INTRA),
            AOM_CODEC_OK);
  cfg.rc_end_usage = AOM_Q;
  cfg.g_profile = 1;
  cfg.g_bit_depth = AOM_BITS_10;
  cfg.g_input_bit_depth = 10;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  cfg.g_limit = 1;
  cfg.g_lag_in_frames = 0;
  cfg.kf_mode = AOM_KF_DISABLED;
  cfg.kf_max_dist = 0;
  cfg.g_threads = 21;
  cfg.rc_min_quantizer = 16;
  cfg.rc_max_quantizer = 54;
  aom_codec_ctx_t enc;
  EXPECT_EQ(aom_codec_enc_init(&enc, iface, &cfg, AOM_CODEC_USE_HIGHBITDEPTH),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AOME_SET_CQ_LEVEL, 35), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_ROW_MT, 1), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_TILE_ROWS, 2), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_TILE_COLUMNS, 5), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AOME_SET_CPUUSED, 1), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_COLOR_RANGE, AOM_CR_FULL_RANGE),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_SKIP_POSTPROC_FILTERING, 1),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AOME_SET_TUNING, AOM_TUNE_SSIM),
            AOM_CODEC_OK);

  // Encode frame
  EXPECT_EQ(aom_codec_encode(&enc, &img, 0, 1, 0), AOM_CODEC_OK);
  aom_codec_iter_t iter = nullptr;
  const aom_codec_cx_pkt_t *pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x1f0011.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, AOM_FRAME_IS_KEY);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Flush encoder
  EXPECT_EQ(aom_codec_encode(&enc, nullptr, 0, 1, 0), AOM_CODEC_OK);
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  EXPECT_EQ(aom_codec_destroy(&enc), AOM_CODEC_OK);
}

// A test that reproduces b/330639949: signed integer overflow in
// linsolve_wiener().
TEST(SearchWienerTest, 12bitSignedIntegerOverflowInLinsolveWiener) {
  constexpr int kWidth = 173;
  constexpr int kHeight = 3;
  // Since the image format is YUV 4:2:0, aom_img_wrap() expects the buffer is
  // allocated with width and height aligned to a multiple of 2. Align the
  // width to a multiple of 2 so that the stride set by aom_img_wrap() is
  // correct. It is not necessary to align the height to a multiple of 2
  // because aom_codec_encode() will only read cfg.g_h rows.
  static constexpr uint16_t kBuffer[(kWidth + 1) * kHeight] = {
    // Y plane:
    // Row:
    0, 0, 369, 0, 4095, 873, 4095, 4095, 0, 571, 4023, 0, 1028, 58, 556, 0, 0,
    1875, 16, 1043, 4095, 0, 1671, 1990, 0, 4095, 2932, 3117, 4095, 0, 0, 0,
    4095, 4095, 4095, 4095, 4095, 4095, 508, 4095, 0, 0, 4095, 4095, 4095, 0,
    4095, 4095, 0, 197, 4095, 1475, 1127, 4095, 0, 1570, 1881, 4095, 1215, 4095,
    0, 0, 1918, 4095, 0, 4095, 3415, 0, 732, 122, 1087, 0, 0, 0, 0, 0, 1012,
    4095, 0, 4095, 4095, 0, 0, 4095, 1931, 4095, 0, 4095, 4095, 4095, 4095, 570,
    4095, 4095, 0, 2954, 0, 0, 0, 1925, 3802, 0, 4095, 55, 0, 4095, 760, 4095,
    0, 3313, 4095, 4095, 4095, 0, 218, 799, 4095, 0, 4095, 2455, 4095, 0, 0,
    611, 4095, 3060, 1669, 0, 0, 4095, 3589, 3903, 0, 3427, 1903, 0, 4095, 3789,
    4095, 4095, 107, 2064, 4095, 2764, 4095, 0, 0, 0, 3498, 0, 0, 1336, 4095,
    4095, 3480, 0, 545, 673, 4095, 0, 4095, 4095, 3175, 4095, 1623, 4095, 0,
    540, 4095, 4095, 14, 429, 0, 0,
    // Row:
    0, 4095, 4095, 0, 1703, 3003, 968, 1313, 4095, 613, 4095, 3918, 112, 4095,
    0, 4095, 2211, 88, 4051, 1203, 2005, 4095, 4095, 0, 2106, 0, 4095, 0, 4095,
    4095, 4095, 0, 3261, 0, 4095, 0, 1184, 4095, 4095, 818, 4095, 0, 4095, 1292,
    4095, 0, 4095, 4095, 0, 4095, 4095, 0, 0, 346, 906, 974, 4095, 4095, 4095,
    4095, 0, 4095, 3225, 2547, 4095, 0, 0, 2705, 2933, 4095, 0, 0, 3579, 0,
    4095, 4095, 4095, 1872, 4095, 298, 2961, 0, 0, 2805, 0, 0, 1210, 3773, 0,
    1208, 3347, 0, 4095, 0, 0, 0, 4034, 4095, 0, 0, 4095, 0, 0, 0, 3302, 0, 0,
    0, 0, 0, 4095, 4095, 0, 2609, 4095, 0, 1831, 4095, 0, 2463, 4095, 4095,
    4095, 4095, 752, 4095, 4095, 41, 1829, 2975, 227, 2505, 2719, 1059, 4071,
    4095, 4095, 3859, 0, 0, 0, 0, 4095, 2423, 4095, 4095, 4095, 4095, 4095,
    1466, 0, 0, 4095, 121, 0, 0, 4095, 0, 0, 3328, 4095, 4095, 0, 1172, 0, 2938,
    0, 4095, 0, 0, 0, 4095, 1821, 0,
    // Row:
    4095, 4095, 4095, 4095, 3487, 4095, 0, 0, 0, 3367, 4095, 4095, 1139, 4095,
    4095, 169, 1300, 1840, 4095, 3508, 4095, 618, 4095, 4095, 4095, 53, 4095,
    4095, 4095, 4095, 4055, 0, 0, 0, 4095, 4095, 0, 0, 0, 0, 1919, 2415, 1485,
    458, 4095, 4095, 3176, 4095, 0, 0, 4095, 4095, 617, 3631, 4095, 4095, 0, 0,
    3983, 4095, 4095, 681, 1685, 4095, 4095, 0, 1783, 25, 4095, 0, 0, 4095,
    4095, 0, 2075, 0, 4095, 4095, 4095, 0, 773, 3407, 0, 4095, 4095, 0, 0, 4095,
    4095, 4095, 4095, 4095, 0, 0, 0, 0, 4095, 0, 1804, 0, 0, 3169, 3576, 502, 0,
    0, 4095, 0, 4095, 0, 4095, 4095, 4095, 0, 4095, 779, 0, 4095, 0, 0, 0, 4095,
    0, 0, 4095, 4095, 4095, 4095, 0, 0, 4095, 4095, 2134, 4095, 4020, 2990,
    3949, 4095, 4095, 4095, 4095, 4095, 0, 4095, 4095, 2829, 4095, 4095, 4095,
    0, 197, 2328, 3745, 0, 3412, 190, 4095, 4095, 4095, 2809, 3953, 0, 4095,
    1502, 2514, 3866, 0, 0, 4095, 4095, 1878, 129, 4095, 0
  };
  unsigned char *img_data =
      reinterpret_cast<unsigned char *>(const_cast<uint16_t *>(kBuffer));

  aom_image_t img;
  EXPECT_EQ(&img, aom_img_wrap(&img, AOM_IMG_FMT_I42016, kWidth, kHeight, 1,
                               img_data));
  img.cp = AOM_CICP_CP_UNSPECIFIED;
  img.tc = AOM_CICP_TC_UNSPECIFIED;
  img.mc = AOM_CICP_MC_UNSPECIFIED;
  img.monochrome = 1;
  img.csp = AOM_CSP_UNKNOWN;
  img.range = AOM_CR_FULL_RANGE;
  img.planes[1] = img.planes[2] = nullptr;
  img.stride[1] = img.stride[2] = 0;

  aom_codec_iface_t *iface = aom_codec_av1_cx();
  aom_codec_enc_cfg_t cfg;
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_enc_config_default(iface, &cfg, AOM_USAGE_GOOD_QUALITY));
  cfg.rc_end_usage = AOM_Q;
  cfg.g_profile = 2;
  cfg.g_bit_depth = AOM_BITS_12;
  cfg.g_input_bit_depth = 12;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  cfg.g_lag_in_frames = 0;
  cfg.g_threads = 18;
  cfg.monochrome = 1;
  cfg.rc_min_quantizer = 0;
  cfg.rc_max_quantizer = 51;
  aom_codec_ctx_t enc;
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_enc_init(&enc, iface, &cfg, AOM_CODEC_USE_HIGHBITDEPTH));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AOME_SET_CQ_LEVEL, 25));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AV1E_SET_TILE_ROWS, 4));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AOME_SET_CPUUSED, 6));
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_control(&enc, AV1E_SET_COLOR_RANGE, AOM_CR_FULL_RANGE));
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_control(&enc, AOME_SET_TUNING, AOM_TUNE_SSIM));

  // Encode frame
  EXPECT_EQ(aom_codec_encode(&enc, &img, 0, 1, 0), AOM_CODEC_OK);
  aom_codec_iter_t iter = nullptr;
  const aom_codec_cx_pkt_t *pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x1f0011.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, AOM_FRAME_IS_KEY);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Encode frame
  EXPECT_EQ(aom_codec_encode(&enc, &img, 0, 1, 0), AOM_CODEC_OK);
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x20000.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, 0u);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Encode frame
  EXPECT_EQ(aom_codec_encode(&enc, &img, 0, 1, 0), AOM_CODEC_OK);
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x20000.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, 0u);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Encode frame
  EXPECT_EQ(aom_codec_encode(&enc, &img, 0, 1, 0), AOM_CODEC_OK);
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x20000.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, 0u);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Flush encoder
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_encode(&enc, nullptr, 0, 1, 0));
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  EXPECT_EQ(AOM_CODEC_OK, aom_codec_destroy(&enc));
}

// A test that reproduces crbug.com/oss-fuzz/384759831: signed integer overflow
// in linsolve_wiener().
TEST(SearchWienerTest, DISABLED_12bitSignedIntegerOverflowInLinsolveWiener2) {
  constexpr int kWidth = 4;
  constexpr int kHeight = 2;
  static constexpr uint16_t kBuffer1[kWidth * kHeight] = {
    // Y plane:
    32, 4095, 2080, 2592, 32, 3104, 4095, 32,
  };
  unsigned char *img_data1 =
      reinterpret_cast<unsigned char *>(const_cast<uint16_t *>(kBuffer1));
  static constexpr uint16_t kBuffer2[kWidth * kHeight] = {
    // Y plane:
    4095, 4095, 2080, 4095, 4095, 32, 4095, 544,
  };
  unsigned char *img_data2 =
      reinterpret_cast<unsigned char *>(const_cast<uint16_t *>(kBuffer2));
  static constexpr uint16_t kBuffer3[kWidth * kHeight] = {
    // Y plane:
    3872, 3872, 3872, 2848, 800, 4095, 32, 3104,
  };
  unsigned char *img_data3 =
      reinterpret_cast<unsigned char *>(const_cast<uint16_t *>(kBuffer3));

  aom_codec_iface_t *iface = aom_codec_av1_cx();
  aom_codec_enc_cfg_t cfg;
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_enc_config_default(iface, &cfg, AOM_USAGE_GOOD_QUALITY));
  cfg.rc_end_usage = AOM_Q;
  cfg.g_profile = 2;
  cfg.g_bit_depth = AOM_BITS_12;
  cfg.g_input_bit_depth = 12;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  cfg.g_lag_in_frames = 0;
  cfg.g_threads = 32;
  cfg.monochrome = 1;
  cfg.rc_min_quantizer = 51;
  cfg.rc_max_quantizer = 55;
  aom_codec_ctx_t enc;
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_enc_init(&enc, iface, &cfg, AOM_CODEC_USE_HIGHBITDEPTH));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AOME_SET_CQ_LEVEL, 53));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AV1E_SET_TILE_ROWS, 3));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AV1E_SET_TILE_COLUMNS, 6));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AOME_SET_CPUUSED, 6));
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_control(&enc, AV1E_SET_COLOR_RANGE, AOM_CR_FULL_RANGE));
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_control(&enc, AOME_SET_TUNING, AOM_TUNE_SSIM));

  aom_image_t img;

  // Encode frame
  EXPECT_EQ(&img, aom_img_wrap(&img, AOM_IMG_FMT_I42016, kWidth, kHeight, 1,
                               img_data1));
  img.monochrome = 1;
  img.planes[1] = img.planes[2] = nullptr;
  img.stride[1] = img.stride[2] = 0;
  EXPECT_EQ(aom_codec_encode(&enc, &img, 0, 1, 0), AOM_CODEC_OK);
  aom_codec_iter_t iter = nullptr;
  const aom_codec_cx_pkt_t *pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x1f0011.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, AOM_FRAME_IS_KEY);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Encode frame
  EXPECT_EQ(&img, aom_img_wrap(&img, AOM_IMG_FMT_I42016, kWidth, kHeight, 1,
                               img_data2));
  img.monochrome = 1;
  img.planes[1] = img.planes[2] = nullptr;
  img.stride[1] = img.stride[2] = 0;
  EXPECT_EQ(aom_codec_encode(&enc, &img, 0, 1, 0), AOM_CODEC_OK);
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x20000.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, 0u);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Encode frame
  EXPECT_EQ(&img, aom_img_wrap(&img, AOM_IMG_FMT_I42016, kWidth, kHeight, 1,
                               img_data3));
  img.monochrome = 1;
  img.planes[1] = img.planes[2] = nullptr;
  img.stride[1] = img.stride[2] = 0;
  EXPECT_EQ(aom_codec_encode(&enc, &img, 0, 1, 0), AOM_CODEC_OK);
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x20000.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, 0u);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Flush encoder
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_encode(&enc, nullptr, 0, 1, 0));
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  EXPECT_EQ(AOM_CODEC_OK, aom_codec_destroy(&enc));
}

// A test that reproduces crbug.com/oss-fuzz/42537236: signed integer overflow
// in linsolve_wiener().
TEST(SearchWienerTest, DISABLED_12bitSignedIntegerOverflowInLinsolveWiener3) {
  constexpr int kWidth = 17;
  constexpr int kHeight = 6;
  // Since the image format is YUV 4:2:0, aom_img_wrap() expects the buffer is
  // allocated with width and height aligned to a multiple of 2. Align the
  // width to a multiple of 2 so that the stride set by aom_img_wrap() is
  // correct.
  static constexpr uint16_t kBuffer1[(kWidth + 1) * kHeight] = {
    // Y plane:
    // Row:
    4095, 2408, 1907, 0, 1119, 0, 4095, 4095, 0, 4095, 2289, 4095, 0, 4095,
    4095, 1545, 4095, 0,
    // Row:
    4095, 3437, 4095, 0, 4095, 4095, 4095, 0, 0, 4095, 0, 0, 1694, 4095, 4095,
    404, 2728, 0,
    // Row:
    3756, 3051, 4095, 0, 0, 0, 841, 0, 0, 324, 0, 0, 0, 756, 4095, 2902, 0, 0,
    // Row:
    0, 0, 4095, 2779, 4095, 4095, 0, 4095, 0, 0, 4095, 4095, 1626, 3491, 4095,
    4095, 1617, 0,
    // Row:
    4095, 4095, 0, 3039, 1218, 159, 4095, 3866, 4095, 438, 0, 0, 4095, 0, 0, 0,
    0, 0,
    // Row:
    1091, 0, 4095, 0, 3587, 0, 4095, 0, 1409, 4095, 4095, 0, 3399, 0, 0, 0, 428,
    0
  };
  unsigned char *img_data1 =
      reinterpret_cast<unsigned char *>(const_cast<uint16_t *>(kBuffer1));
  static constexpr uint16_t kBuffer2[(kWidth + 1) * kHeight] = {
    // Y plane:
    // Row:
    561, 4095, 0, 1627, 4095, 2115, 4095, 4095, 0, 3397, 664, 2409, 0, 1235, 0,
    4095, 0, 0,
    // Row:
    4095, 0, 1665, 0, 4095, 4095, 3541, 0, 4095, 1787, 2584, 4095, 0, 4095,
    4095, 4095, 4095, 0,
    // Row:
    4095, 4095, 4095, 0, 0, 0, 0, 0, 0, 69, 0, 4095, 4095, 882, 4095, 4095,
    4095, 0,
    // Row:
    4095, 3759, 4095, 0, 4095, 4095, 4095, 4095, 420, 0, 4095, 4095, 0, 0, 0, 0,
    0, 0,
    // Row:
    2855, 2411, 4095, 2167, 4095, 2731, 0, 4095, 0, 4095, 0, 4011, 4095, 4095,
    0, 4095, 1964, 0,
    // Row:
    4095, 2879, 2924, 0, 0, 4095, 3770, 4095, 0, 2172, 2825, 1287, 0, 4095,
    4095, 0, 4095, 0
  };
  unsigned char *img_data2 =
      reinterpret_cast<unsigned char *>(const_cast<uint16_t *>(kBuffer2));

  aom_codec_iface_t *iface = aom_codec_av1_cx();
  aom_codec_enc_cfg_t cfg;
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_enc_config_default(iface, &cfg, AOM_USAGE_GOOD_QUALITY));
  cfg.rc_end_usage = AOM_Q;
  cfg.g_profile = 2;
  cfg.g_bit_depth = AOM_BITS_12;
  cfg.g_input_bit_depth = 12;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  cfg.g_lag_in_frames = 0;
  cfg.g_threads = 34;
  cfg.monochrome = 1;
  cfg.rc_min_quantizer = 63;
  cfg.rc_max_quantizer = 63;
  aom_codec_ctx_t enc;
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_enc_init(&enc, iface, &cfg, AOM_CODEC_USE_HIGHBITDEPTH));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AOME_SET_CQ_LEVEL, 63));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AV1E_SET_TILE_ROWS, 5));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AV1E_SET_TILE_COLUMNS, 3));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_control(&enc, AOME_SET_CPUUSED, 6));
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_control(&enc, AV1E_SET_COLOR_RANGE, AOM_CR_FULL_RANGE));
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_control(&enc, AOME_SET_TUNING, AOM_TUNE_SSIM));

  aom_image_t img;

  // Encode frame
  EXPECT_EQ(&img, aom_img_wrap(&img, AOM_IMG_FMT_I42016, kWidth, kHeight, 1,
                               img_data1));
  img.monochrome = 1;
  img.planes[1] = img.planes[2] = nullptr;
  img.stride[1] = img.stride[2] = 0;
  EXPECT_EQ(aom_codec_encode(&enc, &img, 0, 1, 0), AOM_CODEC_OK);
  aom_codec_iter_t iter = nullptr;
  const aom_codec_cx_pkt_t *pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x1f0011.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, AOM_FRAME_IS_KEY);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Encode frame
  EXPECT_EQ(&img, aom_img_wrap(&img, AOM_IMG_FMT_I42016, kWidth, kHeight, 1,
                               img_data2));
  img.monochrome = 1;
  img.planes[1] = img.planes[2] = nullptr;
  img.stride[1] = img.stride[2] = 0;
  EXPECT_EQ(aom_codec_encode(&enc, &img, 0, 1, 0), AOM_CODEC_OK);
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  ASSERT_NE(pkt, nullptr);
  EXPECT_EQ(pkt->kind, AOM_CODEC_CX_FRAME_PKT);
  // pkt->data.frame.flags is 0x20000.
  EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, 0u);
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  // Flush encoder
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_encode(&enc, nullptr, 0, 1, 0));
  iter = nullptr;
  pkt = aom_codec_get_cx_data(&enc, &iter);
  EXPECT_EQ(pkt, nullptr);

  EXPECT_EQ(AOM_CODEC_OK, aom_codec_destroy(&enc));
}

}  // namespace wiener_highbd
#endif  // CONFIG_AV1_HIGHBITDEPTH
