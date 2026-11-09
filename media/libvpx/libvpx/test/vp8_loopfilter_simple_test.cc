/*
 *  Copyright (c) 2026 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// Compares the C and LoongArch LSX versions of the simple loop filter on
// random and near-flat input; the whole buffer must match byte for byte.

#include <string.h>

#include <array>
#include <tuple>

#include "gtest/gtest.h"

#include "./vpx_config.h"
#include "./vp8_rtcd.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "vpx_ports/mem.h"

namespace {

using libvpx_test::ACMRandom;

#if HAVE_LSX

typedef void (*LoopFilterSimple)(unsigned char *y_ptr, int y_stride,
                                 const unsigned char *blimit);

const int kRows = 64;
const int kCols = 64;
const int kBufferSize = kRows * kCols;
// Two input distributions; see FillBuffer().
const int kUniform = 0;
const int kBlocky = 1;
const int kNumModes = 2;

// Fills the scratch area, addressed with 'stride' bytes per row. kUniform is
// pure noise. kBlocky is a near-flat image with 4x4 blocks and a small step at
// each block edge: that is what the filter meets in coded video, and it drives
// the filter into the small, unsaturated value range that uniform noise almost
// never reaches (noise saturates the average the filter is built around).
void FillBuffer(ACMRandom *rnd, int stride, int mode, unsigned char *buf) {
  if (mode == kUniform) {
    for (int i = 0; i < kBufferSize; ++i) buf[i] = (unsigned char)rnd->Rand8();
    return;
  }
  // Kept away from 0 and 255 so the block gains stay inside the byte range.
  const int base = 48 + rnd->Rand8() % 160;
  for (int y = 0; y * stride < kBufferSize; ++y) {
    for (int x = 0; x < stride; ++x) {
      const int v =
          base + ((x >> 2) & 3) + ((y >> 2) & 3) + (int)(rnd->Rand8() % 3) - 1;
      buf[y * stride + x] = (unsigned char)v;
    }
  }
}

class SimpleEdgeTest : public ::testing::TestWithParam<
                           std::tuple<LoopFilterSimple, LoopFilterSimple> > {
 public:
  void TearDown() override { libvpx_test::ClearSystemState(); }
};

TEST_P(SimpleEdgeTest, MatchC) {
  LoopFilterSimple ref = std::get<0>(GetParam());
  LoopFilterSimple opt = std::get<1>(GetParam());
  // Guards against an instantiation that lists the same function twice, which
  // would leave the comparison below comparing a buffer with itself.
  ASSERT_NE(ref, opt);

  ACMRandom rnd(ACMRandom::DeterministicSeed());
  const std::array<int, 8> kStrides = { 16, 17, 24, 32, 33, 40, 48, 63 };
  // 193 is the largest blimit the codec produces (2 * (63 + 2) + 63), 194 and
  // above leave that range, and 255 is the value that overflows saturated byte
  // arithmetic. Every (stride, offset, mode) combination sweeps this list
  // before falling back to random values.
  const std::array<unsigned char, 20> kBlimits = { 0,   1,   2,   3,   4,
                                                   5,   8,   16,  32,  63,
                                                   64,  127, 128, 129, 160,
                                                   192, 194, 200, 250, 255 };
  const int kNumBlimits = (int)kBlimits.size();
  const int kNumRandom = 100;

  DECLARE_ALIGNED(16, unsigned char, buf_ref[64 * 64]);
  DECLARE_ALIGNED(16, unsigned char, buf_opt[64 * 64]);

  for (const int stride : kStrides) {
    for (int offset = 0; offset < 4; ++offset) {
      for (int mode = 0; mode < kNumModes; ++mode) {
        for (int iter = 0; iter < kNumBlimits + kNumRandom; ++iter) {
          const unsigned char blimit =
              iter < kNumBlimits ? kBlimits[iter] : (unsigned char)rnd.Rand8();
          FillBuffer(&rnd, stride, mode, buf_ref);
          memcpy(buf_opt, buf_ref, sizeof(buf_ref));
          ref(buf_ref + 2 * stride + 8 + offset, stride, &blimit);
          opt(buf_opt + 2 * stride + 8 + offset, stride, &blimit);
          ASSERT_EQ(0, memcmp(buf_ref, buf_opt, sizeof(buf_ref)))
              << "stride=" << stride << " offset=" << offset << " mode=" << mode
              << " iter=" << iter << " blimit=" << (int)blimit;
        }
      }
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    LSX, SimpleEdgeTest,
    ::testing::Values(
        std::make_tuple(&vp8_loop_filter_simple_vertical_edge_c,
                        &vp8_loop_filter_simple_vertical_edge_lsx),
        std::make_tuple(&vp8_loop_filter_simple_horizontal_edge_c,
                        &vp8_loop_filter_simple_horizontal_edge_lsx),
        std::make_tuple(&vp8_loop_filter_bvs_c, &vp8_loop_filter_bvs_lsx),
        std::make_tuple(&vp8_loop_filter_bhs_c, &vp8_loop_filter_bhs_lsx)));

#endif  // HAVE_LSX

}  // namespace
