/*
 * Copyright (c) 2017, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <algorithm>
#include <tuple>

#include "gtest/gtest.h"

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"
#include "config/av1_rtcd.h"

#include "aom/aom_codec.h"
#include "aom_dsp/txfm_common.h"
#include "aom_ports/aom_timer.h"
#include "av1/encoder/encoder.h"
#include "av1/common/scan.h"
#include "test/acm_random.h"
#include "test/register_state_check.h"
#include "test/util.h"

namespace {
using libaom_test::ACMRandom;

#define QUAN_PARAM_LIST                                                       \
  const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int16_t *zbin_ptr,    \
      const int16_t *round_ptr, const int16_t *quant_ptr,                     \
      const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr,                 \
      tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, \
      const int16_t *scan, const int16_t *iscan

#define LP_QUANTIZE_PARAM_LIST                                             \
  const int16_t *coeff_ptr, intptr_t n_coeffs, const int16_t *round_ptr,   \
      const int16_t *quant_ptr, int16_t *qcoeff_ptr, int16_t *dqcoeff_ptr, \
      const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan,  \
      const int16_t *iscan

typedef void (*LPQuantizeFunc)(LP_QUANTIZE_PARAM_LIST);
typedef void (*QuantizeFunc)(QUAN_PARAM_LIST);
typedef void (*QuantizeFuncHbd)(QUAN_PARAM_LIST, int log_scale);

#undef LP_QUANTIZE_PARAM_LIST

#define HBD_QUAN_FUNC                                                      \
  fn(coeff_ptr, n_coeffs, zbin_ptr, round_ptr, quant_ptr, quant_shift_ptr, \
     qcoeff_ptr, dqcoeff_ptr, dequant_ptr, eob_ptr, scan, iscan, log_scale)

#define LBD_QUAN_FUNC                                                      \
  fn(coeff_ptr, n_coeffs, zbin_ptr, round_ptr, quant_ptr, quant_shift_ptr, \
     qcoeff_ptr, dqcoeff_ptr, dequant_ptr, eob_ptr, scan, iscan)

template <QuantizeFuncHbd fn>
void highbd_quan16x16_wrapper(QUAN_PARAM_LIST) {
  const int log_scale = 0;
  HBD_QUAN_FUNC;
}

template <QuantizeFuncHbd fn>
void highbd_quan32x32_wrapper(QUAN_PARAM_LIST) {
  const int log_scale = 1;
  HBD_QUAN_FUNC;
}

template <QuantizeFuncHbd fn>
void highbd_quan64x64_wrapper(QUAN_PARAM_LIST) {
  const int log_scale = 2;
  HBD_QUAN_FUNC;
}

enum QuantType { TYPE_B, TYPE_DC, TYPE_FP };

using std::tuple;

template <typename FuncType>
using QuantizeParam =
    tuple<FuncType, FuncType, TX_SIZE, QuantType, aom_bit_depth_t>;

typedef struct {
  QUANTS quant;
  Dequants dequant;
} QuanTable;

const int kTestNum = 1000;

#define GET_TEMPLATE_PARAM(k) std::get<k>(this->GetParam())

template <typename CoeffType, typename FuncType>
class QuantizeTestBase
    : public ::testing::TestWithParam<QuantizeParam<FuncType>> {
 protected:
  QuantizeTestBase()
      : quant_ref_(GET_TEMPLATE_PARAM(0)), quant_(GET_TEMPLATE_PARAM(1)),
        tx_size_(GET_TEMPLATE_PARAM(2)), type_(GET_TEMPLATE_PARAM(3)),
        bd_(GET_TEMPLATE_PARAM(4)) {}

  ~QuantizeTestBase() override = default;

  void SetUp() override {
    qtab_ = reinterpret_cast<QuanTable *>(aom_memalign(32, sizeof(*qtab_)));
    ASSERT_NE(qtab_, nullptr);
    const int n_coeffs = coeff_num();
    coeff_ = reinterpret_cast<CoeffType *>(
        aom_memalign(32, 6 * n_coeffs * sizeof(CoeffType)));
    ASSERT_NE(coeff_, nullptr);
    InitQuantizer();
  }

  void TearDown() override {
    aom_free(qtab_);
    qtab_ = nullptr;
    aom_free(coeff_);
    coeff_ = nullptr;
  }

  void InitQuantizer() {
    av1_build_quantizer(bd_, 0, 0, 0, 0, 0, &qtab_->quant, &qtab_->dequant, 0);
  }

  virtual void RunQuantizeFunc(
      const CoeffType *coeff_ptr, intptr_t n_coeffs, const int16_t *zbin_ptr,
      const int16_t *round_ptr, const int16_t *quant_ptr,
      const int16_t *quant_shift_ptr, CoeffType *qcoeff_ptr,
      CoeffType *qcoeff_ref_ptr, CoeffType *dqcoeff_ptr,
      CoeffType *dqcoeff_ref_ptr, const int16_t *dequant_ptr,
      uint16_t *eob_ref_ptr, uint16_t *eob_ptr, const int16_t *scan,
      const int16_t *iscan) = 0;

  void QuantizeRun(bool is_loop, int q = 0, int test_num = 1) {
    CoeffType *coeff_ptr = coeff_;
    const intptr_t n_coeffs = coeff_num();

    CoeffType *qcoeff_ref = coeff_ptr + n_coeffs;
    CoeffType *dqcoeff_ref = qcoeff_ref + n_coeffs;

    CoeffType *qcoeff = dqcoeff_ref + n_coeffs;
    CoeffType *dqcoeff = qcoeff + n_coeffs;
    uint16_t *eob = (uint16_t *)(dqcoeff + n_coeffs);

    // Testing uses 2-D DCT scan order table
    const SCAN_ORDER *const sc = get_default_scan(tx_size_, DCT_DCT);

    // Testing uses luminance quantization table
    const int16_t *zbin = qtab_->quant.y_zbin[q];

    const int16_t *round = nullptr;
    const int16_t *quant = nullptr;
    if (type_ == TYPE_B) {
      round = qtab_->quant.y_round[q];
      quant = qtab_->quant.y_quant[q];
    } else if (type_ == TYPE_FP) {
      round = qtab_->quant.y_round_fp[q];
      quant = qtab_->quant.y_quant_fp[q];
    }

    const int16_t *quant_shift = qtab_->quant.y_quant_shift[q];
    const int16_t *dequant = qtab_->dequant.y_dequant_QTX[q];

    for (int i = 0; i < test_num; ++i) {
      if (is_loop) FillCoeffRandom();

      memset(qcoeff_ref, 0, 5 * n_coeffs * sizeof(*qcoeff_ref));

      RunQuantizeFunc(coeff_ptr, n_coeffs, zbin, round, quant, quant_shift,
                      qcoeff, qcoeff_ref, dqcoeff, dqcoeff_ref, dequant,
                      &eob[0], &eob[1], sc->scan, sc->iscan);

      for (int j = 0; j < n_coeffs; ++j) {
        ASSERT_EQ(qcoeff_ref[j], qcoeff[j])
            << "Q mismatch on test: " << i << " at position: " << j
            << " Q: " << q << " coeff: " << coeff_ptr[j];
      }

      for (int j = 0; j < n_coeffs; ++j) {
        ASSERT_EQ(dqcoeff_ref[j], dqcoeff[j])
            << "Dq mismatch on test: " << i << " at position: " << j
            << " Q: " << q << " coeff: " << coeff_ptr[j];
      }

      ASSERT_EQ(eob[0], eob[1])
          << "eobs mismatch on test: " << i << " Q: " << q;
    }
  }

  void CompareResults(const CoeffType *buf_ref, const CoeffType *buf, int size,
                      const char *text, int q, int number) {
    int i;
    for (i = 0; i < size; ++i) {
      ASSERT_EQ(buf_ref[i], buf[i]) << text << " mismatch on test: " << number
                                    << " at position: " << i << " Q: " << q;
    }
  }

  int coeff_num() const { return av1_get_max_eob(tx_size_); }

  void FillCoeff(CoeffType c) {
    const int n_coeffs = coeff_num();
    for (int i = 0; i < n_coeffs; ++i) {
      coeff_[i] = c;
    }
  }

  void FillCoeffRandom() {
    const int n_coeffs = coeff_num();
    FillCoeffZero();
    const int num = rnd_.Rand16() % n_coeffs;
    // Randomize the first non zero coeff position.
    const int start = rnd_.Rand16() % n_coeffs;
    const int end = std::min(start + num, n_coeffs);
    for (int i = start; i < end; ++i) {
      coeff_[i] = GetRandomCoeff();
    }
  }

  void FillCoeffRandomRows(int num) {
    FillCoeffZero();
    for (int i = 0; i < num; ++i) {
      coeff_[i] = GetRandomCoeff();
    }
  }

  void FillCoeffZero() { FillCoeff(0); }

  void FillCoeffConstant() {
    CoeffType c = GetRandomCoeff();
    FillCoeff(c);
  }

  void FillDcOnly() {
    FillCoeffZero();
    coeff_[0] = GetRandomCoeff();
  }

  void FillDcLargeNegative() {
    FillCoeffZero();
    // Generate a qcoeff which contains 512/-512 (0x0100/0xFE00) to catch issues
    // like BUG=883 where the constant being compared was incorrectly
    // initialized.
    coeff_[0] = -8191;
  }

  CoeffType GetRandomCoeff() {
    CoeffType coeff;
    if (bd_ == AOM_BITS_8) {
      coeff =
          clamp(static_cast<int16_t>(rnd_.Rand16()), INT16_MIN + 1, INT16_MAX);
    } else {
      CoeffType min = -(1 << (7 + bd_));
      CoeffType max = -min - 1;
      coeff = clamp(static_cast<CoeffType>(rnd_.Rand31()), min, max);
    }
    return coeff;
  }

  ACMRandom rnd_;
  QuanTable *qtab_;
  CoeffType *coeff_;
  FuncType quant_ref_;
  FuncType quant_;
  TX_SIZE tx_size_;
  QuantType type_;
  aom_bit_depth_t bd_;
};

class FullPrecisionQuantizeTest
    : public QuantizeTestBase<tran_low_t, QuantizeFunc> {
  void RunQuantizeFunc(const tran_low_t *coeff_ptr, intptr_t n_coeffs,
                       const int16_t *zbin_ptr, const int16_t *round_ptr,
                       const int16_t *quant_ptr, const int16_t *quant_shift_ptr,
                       tran_low_t *qcoeff_ptr, tran_low_t *qcoeff_ref_ptr,
                       tran_low_t *dqcoeff_ptr, tran_low_t *dqcoeff_ref_ptr,
                       const int16_t *dequant_ptr, uint16_t *eob_ref_ptr,
                       uint16_t *eob_ptr, const int16_t *scan,
                       const int16_t *iscan) override {
    quant_ref_(coeff_ptr, n_coeffs, zbin_ptr, round_ptr, quant_ptr,
               quant_shift_ptr, qcoeff_ref_ptr, dqcoeff_ref_ptr, dequant_ptr,
               eob_ref_ptr, scan, iscan);

    API_REGISTER_STATE_CHECK(quant_(
        coeff_ptr, n_coeffs, zbin_ptr, round_ptr, quant_ptr, quant_shift_ptr,
        qcoeff_ptr, dqcoeff_ptr, dequant_ptr, eob_ptr, scan, iscan));
  }
};

class LowPrecisionQuantizeTest
    : public QuantizeTestBase<int16_t, LPQuantizeFunc> {
  void RunQuantizeFunc(const int16_t *coeff_ptr, intptr_t n_coeffs,
                       const int16_t * /*zbin_ptr*/, const int16_t *round_ptr,
                       const int16_t *quant_ptr,
                       const int16_t * /*quant_shift_ptr*/, int16_t *qcoeff_ptr,
                       int16_t *qcoeff_ref_ptr, int16_t *dqcoeff_ptr,
                       int16_t *dqcoeff_ref_ptr, const int16_t *dequant_ptr,
                       uint16_t *eob_ref_ptr, uint16_t *eob_ptr,
                       const int16_t *scan, const int16_t *iscan) override {
    quant_ref_(coeff_ptr, n_coeffs, round_ptr, quant_ptr, qcoeff_ref_ptr,
               dqcoeff_ref_ptr, dequant_ptr, eob_ref_ptr, scan, iscan);

    API_REGISTER_STATE_CHECK(quant_(coeff_ptr, n_coeffs, round_ptr, quant_ptr,
                                    qcoeff_ptr, dqcoeff_ptr, dequant_ptr,
                                    eob_ptr, scan, iscan));
  }
};

TEST_P(FullPrecisionQuantizeTest, ZeroInput) {
  FillCoeffZero();
  QuantizeRun(false);
}

TEST_P(FullPrecisionQuantizeTest, LargeNegativeInput) {
  FillDcLargeNegative();
  QuantizeRun(false, 0, 1);
}

TEST_P(FullPrecisionQuantizeTest, DcOnlyInput) {
  FillDcOnly();
  QuantizeRun(false, 0, 1);
}

TEST_P(FullPrecisionQuantizeTest, RandomInput) {
  QuantizeRun(true, 0, kTestNum);
}

TEST_P(FullPrecisionQuantizeTest, MultipleQ) {
  for (int q = 0; q < QINDEX_RANGE; ++q) {
    QuantizeRun(true, q, kTestNum);
  }
}

// Force the coeff to be half the value of the dequant.  This exposes a
// mismatch found in av1_quantize_fp_sse2().
TEST_P(FullPrecisionQuantizeTest, CoeffHalfDequant) {
  FillCoeff(16);
  QuantizeRun(false, 25, 1);
}

TEST_P(FullPrecisionQuantizeTest, DISABLED_Speed) {
  tran_low_t *coeff_ptr = coeff_;
  const intptr_t n_coeffs = coeff_num();

  tran_low_t *qcoeff_ref = coeff_ptr + n_coeffs;
  tran_low_t *dqcoeff_ref = qcoeff_ref + n_coeffs;

  tran_low_t *qcoeff = dqcoeff_ref + n_coeffs;
  tran_low_t *dqcoeff = qcoeff + n_coeffs;
  uint16_t *eob = (uint16_t *)(dqcoeff + n_coeffs);

  // Testing uses 2-D DCT scan order table
  const SCAN_ORDER *const sc = get_default_scan(tx_size_, DCT_DCT);

  // Testing uses luminance quantization table
  const int q = 22;
  const int16_t *zbin = qtab_->quant.y_zbin[q];
  const int16_t *round_fp = qtab_->quant.y_round_fp[q];
  const int16_t *quant_fp = qtab_->quant.y_quant_fp[q];
  const int16_t *quant_shift = qtab_->quant.y_quant_shift[q];
  const int16_t *dequant = qtab_->dequant.y_dequant_QTX[q];
  const int kNumTests = 5000000;
  aom_usec_timer timer, simd_timer;
  int rows = tx_size_high[tx_size_];
  int cols = tx_size_wide[tx_size_];
  rows = AOMMIN(32, rows);
  cols = AOMMIN(32, cols);
  for (int cnt = 0; cnt <= rows; cnt++) {
    FillCoeffRandomRows(cnt * cols);

    aom_usec_timer_start(&timer);
    for (int n = 0; n < kNumTests; ++n) {
      quant_ref_(coeff_ptr, n_coeffs, zbin, round_fp, quant_fp, quant_shift,
                 qcoeff, dqcoeff, dequant, eob, sc->scan, sc->iscan);
    }
    aom_usec_timer_mark(&timer);

    aom_usec_timer_start(&simd_timer);
    for (int n = 0; n < kNumTests; ++n) {
      quant_(coeff_ptr, n_coeffs, zbin, round_fp, quant_fp, quant_shift, qcoeff,
             dqcoeff, dequant, eob, sc->scan, sc->iscan);
    }
    aom_usec_timer_mark(&simd_timer);

    const int elapsed_time = static_cast<int>(aom_usec_timer_elapsed(&timer));
    const int simd_elapsed_time =
        static_cast<int>(aom_usec_timer_elapsed(&simd_timer));
    printf("c_time = %d \t simd_time = %d \t Gain = %f \n", elapsed_time,
           simd_elapsed_time, ((float)elapsed_time / simd_elapsed_time));
  }
}

// TODO(crbug.com/aomedia/2796)
TEST_P(LowPrecisionQuantizeTest, ZeroInput) {
  FillCoeffZero();
  QuantizeRun(false);
}

TEST_P(LowPrecisionQuantizeTest, LargeNegativeInput) {
  FillDcLargeNegative();
  QuantizeRun(false, 0, 1);
}

TEST_P(LowPrecisionQuantizeTest, DcOnlyInput) {
  FillDcOnly();
  QuantizeRun(false, 0, 1);
}

TEST_P(LowPrecisionQuantizeTest, RandomInput) {
  QuantizeRun(true, 0, kTestNum);
}

TEST_P(LowPrecisionQuantizeTest, MultipleQ) {
  for (int q = 0; q < QINDEX_RANGE; ++q) {
    QuantizeRun(true, q, kTestNum);
  }
}

// Force the coeff to be half the value of the dequant.  This exposes a
// mismatch found in av1_quantize_fp_sse2().
TEST_P(LowPrecisionQuantizeTest, CoeffHalfDequant) {
  FillCoeff(16);
  QuantizeRun(false, 25, 1);
}

TEST_P(LowPrecisionQuantizeTest, DISABLED_Speed) {
  int16_t *coeff_ptr = coeff_;
  const intptr_t n_coeffs = coeff_num();

  int16_t *qcoeff_ref = coeff_ptr + n_coeffs;
  int16_t *dqcoeff_ref = qcoeff_ref + n_coeffs;

  int16_t *qcoeff = dqcoeff_ref + n_coeffs;
  int16_t *dqcoeff = qcoeff + n_coeffs;
  uint16_t *eob = (uint16_t *)(dqcoeff + n_coeffs);

  // Testing uses 2-D DCT scan order table
  const SCAN_ORDER *const sc = get_default_scan(tx_size_, DCT_DCT);

  // Testing uses luminance quantization table
  const int q = 22;
  const int16_t *round_fp = qtab_->quant.y_round_fp[q];
  const int16_t *quant_fp = qtab_->quant.y_quant_fp[q];
  const int16_t *dequant = qtab_->dequant.y_dequant_QTX[q];
  const int kNumTests = 5000000;
  aom_usec_timer timer, simd_timer;
  int rows = tx_size_high[tx_size_];
  int cols = tx_size_wide[tx_size_];
  rows = AOMMIN(32, rows);
  cols = AOMMIN(32, cols);
  for (int cnt = 0; cnt <= rows; cnt++) {
    FillCoeffRandomRows(cnt * cols);

    aom_usec_timer_start(&timer);
    for (int n = 0; n < kNumTests; ++n) {
      quant_ref_(coeff_ptr, n_coeffs, round_fp, quant_fp, qcoeff, dqcoeff,
                 dequant, eob, sc->scan, sc->iscan);
    }
    aom_usec_timer_mark(&timer);

    aom_usec_timer_start(&simd_timer);
    for (int n = 0; n < kNumTests; ++n) {
      quant_(coeff_ptr, n_coeffs, round_fp, quant_fp, qcoeff, dqcoeff, dequant,
             eob, sc->scan, sc->iscan);
    }
    aom_usec_timer_mark(&simd_timer);

    const int elapsed_time = static_cast<int>(aom_usec_timer_elapsed(&timer));
    const int simd_elapsed_time =
        static_cast<int>(aom_usec_timer_elapsed(&simd_timer));
    printf("c_time = %d \t simd_time = %d \t Gain = %f \n", elapsed_time,
           simd_elapsed_time, ((float)elapsed_time / simd_elapsed_time));
  }
}

using std::make_tuple;

#if HAVE_AVX2

const QuantizeParam<LPQuantizeFunc> kLPQParamArrayAvx2[] = {
  make_tuple(&av1_quantize_lp_c, &av1_quantize_lp_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_lp_c, &av1_quantize_lp_avx2,
             static_cast<TX_SIZE>(TX_8X8), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_lp_c, &av1_quantize_lp_avx2,
             static_cast<TX_SIZE>(TX_4X4), TYPE_FP, AOM_BITS_8)
};

INSTANTIATE_TEST_SUITE_P(AVX2, LowPrecisionQuantizeTest,
                         ::testing::ValuesIn(kLPQParamArrayAvx2));

const QuantizeParam<QuantizeFunc> kQParamArrayAvx2[] = {
  make_tuple(&av1_quantize_fp_c, &av1_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_fp_c, &av1_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_4X16), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_fp_c, &av1_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_16X4), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_fp_c, &av1_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_32X8), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_fp_c, &av1_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_8X32), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_fp_32x32_c, &av1_quantize_fp_32x32_avx2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_fp_32x32_c, &av1_quantize_fp_32x32_avx2,
             static_cast<TX_SIZE>(TX_16X64), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_fp_32x32_c, &av1_quantize_fp_32x32_avx2,
             static_cast<TX_SIZE>(TX_64X16), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_fp_64x64_c, &av1_quantize_fp_64x64_avx2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_FP, AOM_BITS_8),
#if CONFIG_AV1_HIGHBITDEPTH
  make_tuple(&highbd_quan16x16_wrapper<av1_highbd_quantize_fp_c>,
             &highbd_quan16x16_wrapper<av1_highbd_quantize_fp_avx2>,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AOM_BITS_8),
  make_tuple(&highbd_quan16x16_wrapper<av1_highbd_quantize_fp_c>,
             &highbd_quan16x16_wrapper<av1_highbd_quantize_fp_avx2>,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AOM_BITS_10),
  make_tuple(&highbd_quan16x16_wrapper<av1_highbd_quantize_fp_c>,
             &highbd_quan16x16_wrapper<av1_highbd_quantize_fp_avx2>,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AOM_BITS_12),
  make_tuple(&highbd_quan32x32_wrapper<av1_highbd_quantize_fp_c>,
             &highbd_quan32x32_wrapper<av1_highbd_quantize_fp_avx2>,
             static_cast<TX_SIZE>(TX_32X32), TYPE_FP, AOM_BITS_8),
  make_tuple(&highbd_quan32x32_wrapper<av1_highbd_quantize_fp_c>,
             &highbd_quan32x32_wrapper<av1_highbd_quantize_fp_avx2>,
             static_cast<TX_SIZE>(TX_32X32), TYPE_FP, AOM_BITS_10),
  make_tuple(&highbd_quan32x32_wrapper<av1_highbd_quantize_fp_c>,
             &highbd_quan32x32_wrapper<av1_highbd_quantize_fp_avx2>,
             static_cast<TX_SIZE>(TX_32X32), TYPE_FP, AOM_BITS_12),
  make_tuple(&highbd_quan64x64_wrapper<av1_highbd_quantize_fp_c>,
             &highbd_quan64x64_wrapper<av1_highbd_quantize_fp_avx2>,
             static_cast<TX_SIZE>(TX_64X64), TYPE_FP, AOM_BITS_8),
  make_tuple(&highbd_quan64x64_wrapper<av1_highbd_quantize_fp_c>,
             &highbd_quan64x64_wrapper<av1_highbd_quantize_fp_avx2>,
             static_cast<TX_SIZE>(TX_64X64), TYPE_FP, AOM_BITS_10),
  make_tuple(&highbd_quan64x64_wrapper<av1_highbd_quantize_fp_c>,
             &highbd_quan64x64_wrapper<av1_highbd_quantize_fp_avx2>,
             static_cast<TX_SIZE>(TX_64X64), TYPE_FP, AOM_BITS_12),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_10),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_12),
  make_tuple(&aom_highbd_quantize_b_32x32_c, &aom_highbd_quantize_b_32x32_avx2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_12),
  make_tuple(&aom_highbd_quantize_b_64x64_c, &aom_highbd_quantize_b_64x64_avx2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_12),
#if !CONFIG_REALTIME_ONLY
  make_tuple(&aom_highbd_quantize_b_adaptive_c,
             &aom_highbd_quantize_b_adaptive_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_highbd_quantize_b_adaptive_c,
             &aom_highbd_quantize_b_adaptive_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_10),
  make_tuple(&aom_highbd_quantize_b_adaptive_c,
             &aom_highbd_quantize_b_adaptive_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_12),
  make_tuple(&aom_highbd_quantize_b_32x32_adaptive_c,
             &aom_highbd_quantize_b_32x32_adaptive_avx2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_highbd_quantize_b_32x32_adaptive_c,
             &aom_highbd_quantize_b_32x32_adaptive_avx2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_10),
  make_tuple(&aom_highbd_quantize_b_32x32_adaptive_c,
             &aom_highbd_quantize_b_32x32_adaptive_avx2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_12),
#endif  // !CONFIG_REALTIME_ONLY
#endif  // CONFIG_AV1_HIGHBITDEPTH
#if !CONFIG_REALTIME_ONLY
  make_tuple(&aom_quantize_b_adaptive_c, &aom_quantize_b_adaptive_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_quantize_b_adaptive_c, &aom_quantize_b_adaptive_avx2,
             static_cast<TX_SIZE>(TX_8X8), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_quantize_b_adaptive_c, &aom_quantize_b_adaptive_avx2,
             static_cast<TX_SIZE>(TX_4X4), TYPE_B, AOM_BITS_8),
#endif  // !CONFIG_REALTIME_ONLY
  make_tuple(&aom_quantize_b_c, &aom_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_quantize_b_32x32_c, &aom_quantize_b_32x32_avx2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_quantize_b_64x64_c, &aom_quantize_b_64x64_avx2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_8),
};

INSTANTIATE_TEST_SUITE_P(AVX2, FullPrecisionQuantizeTest,
                         ::testing::ValuesIn(kQParamArrayAvx2));
#endif  // HAVE_AVX2

#if HAVE_SSE2

const QuantizeParam<LPQuantizeFunc> kLPQParamArraySSE2[] = {
  make_tuple(&av1_quantize_lp_c, &av1_quantize_lp_sse2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_lp_c, &av1_quantize_lp_sse2,
             static_cast<TX_SIZE>(TX_8X8), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_lp_c, &av1_quantize_lp_sse2,
             static_cast<TX_SIZE>(TX_4X4), TYPE_FP, AOM_BITS_8)
};

INSTANTIATE_TEST_SUITE_P(SSE2, LowPrecisionQuantizeTest,
                         ::testing::ValuesIn(kLPQParamArraySSE2));

const QuantizeParam<QuantizeFunc> kQParamArraySSE2[] = {
  make_tuple(&av1_quantize_fp_c, &av1_quantize_fp_sse2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_fp_c, &av1_quantize_fp_sse2,
             static_cast<TX_SIZE>(TX_4X16), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_fp_c, &av1_quantize_fp_sse2,
             static_cast<TX_SIZE>(TX_16X4), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_fp_c, &av1_quantize_fp_sse2,
             static_cast<TX_SIZE>(TX_8X32), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_fp_c, &av1_quantize_fp_sse2,
             static_cast<TX_SIZE>(TX_32X8), TYPE_FP, AOM_BITS_8),
  make_tuple(&aom_quantize_b_c, &aom_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_8),
#if CONFIG_AV1_HIGHBITDEPTH
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_10),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_12),
#if !CONFIG_REALTIME_ONLY
  make_tuple(&aom_highbd_quantize_b_adaptive_c,
             &aom_highbd_quantize_b_adaptive_sse2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_highbd_quantize_b_adaptive_c,
             &aom_highbd_quantize_b_adaptive_sse2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_10),
  make_tuple(&aom_highbd_quantize_b_adaptive_c,
             &aom_highbd_quantize_b_adaptive_sse2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_12),
  make_tuple(&aom_highbd_quantize_b_32x32_c, &aom_highbd_quantize_b_32x32_sse2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_highbd_quantize_b_32x32_c, &aom_highbd_quantize_b_32x32_sse2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_10),
  make_tuple(&aom_highbd_quantize_b_32x32_c, &aom_highbd_quantize_b_32x32_sse2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_12),
  make_tuple(&aom_highbd_quantize_b_32x32_adaptive_c,
             &aom_highbd_quantize_b_32x32_adaptive_sse2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_highbd_quantize_b_32x32_adaptive_c,
             &aom_highbd_quantize_b_32x32_adaptive_sse2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_10),
  make_tuple(&aom_highbd_quantize_b_32x32_adaptive_c,
             &aom_highbd_quantize_b_32x32_adaptive_sse2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_12),
#endif  // !CONFIG_REALTIME_ONLY
  make_tuple(&aom_highbd_quantize_b_64x64_c, &aom_highbd_quantize_b_64x64_sse2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_highbd_quantize_b_64x64_c, &aom_highbd_quantize_b_64x64_sse2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_10),
  make_tuple(&aom_highbd_quantize_b_64x64_c, &aom_highbd_quantize_b_64x64_sse2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_12),
#if !CONFIG_REALTIME_ONLY
  make_tuple(&aom_highbd_quantize_b_64x64_adaptive_c,
             &aom_highbd_quantize_b_64x64_adaptive_sse2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_highbd_quantize_b_64x64_adaptive_c,
             &aom_highbd_quantize_b_64x64_adaptive_sse2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_10),
  make_tuple(&aom_highbd_quantize_b_64x64_adaptive_c,
             &aom_highbd_quantize_b_64x64_adaptive_sse2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_12),
#endif  // !CONFIG_REALTIME_ONLY
#endif  // CONFIG_AV1_HIGHBITDEPTH
#if !CONFIG_REALTIME_ONLY
  make_tuple(&aom_quantize_b_adaptive_c, &aom_quantize_b_adaptive_sse2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_quantize_b_adaptive_c, &aom_quantize_b_adaptive_sse2,
             static_cast<TX_SIZE>(TX_8X8), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_quantize_b_adaptive_c, &aom_quantize_b_adaptive_sse2,
             static_cast<TX_SIZE>(TX_4X4), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_quantize_b_32x32_adaptive_c,
             &aom_quantize_b_32x32_adaptive_sse2,
             static_cast<TX_SIZE>(TX_32X16), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_quantize_b_32x32_adaptive_c,
             &aom_quantize_b_32x32_adaptive_sse2,
             static_cast<TX_SIZE>(TX_16X32), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_quantize_b_32x32_adaptive_c,
             &aom_quantize_b_32x32_adaptive_sse2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_quantize_b_64x64_adaptive_c,
             &aom_quantize_b_64x64_adaptive_sse2,
             static_cast<TX_SIZE>(TX_32X64), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_quantize_b_64x64_adaptive_c,
             &aom_quantize_b_64x64_adaptive_sse2,
             static_cast<TX_SIZE>(TX_64X32), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_quantize_b_64x64_adaptive_c,
             &aom_quantize_b_64x64_adaptive_sse2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_8)
#endif  // !CONFIG_REALTIME_ONLY
};

INSTANTIATE_TEST_SUITE_P(SSE2, FullPrecisionQuantizeTest,
                         ::testing::ValuesIn(kQParamArraySSE2));
#endif

#if HAVE_NEON

const QuantizeParam<LPQuantizeFunc> kLPQParamArrayNEON[] = {
  make_tuple(av1_quantize_lp_c, av1_quantize_lp_neon,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AOM_BITS_8),
  make_tuple(av1_quantize_lp_c, av1_quantize_lp_neon,
             static_cast<TX_SIZE>(TX_8X8), TYPE_FP, AOM_BITS_8),
  make_tuple(av1_quantize_lp_c, av1_quantize_lp_neon,
             static_cast<TX_SIZE>(TX_4X4), TYPE_FP, AOM_BITS_8)
};

INSTANTIATE_TEST_SUITE_P(NEON, LowPrecisionQuantizeTest,
                         ::testing::ValuesIn(kLPQParamArrayNEON));

const QuantizeParam<QuantizeFunc> kQParamArrayNEON[] = {
  make_tuple(&av1_quantize_fp_c, &av1_quantize_fp_neon,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_fp_c, &av1_quantize_fp_neon,
             static_cast<TX_SIZE>(TX_4X16), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_fp_c, &av1_quantize_fp_neon,
             static_cast<TX_SIZE>(TX_16X4), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_fp_c, &av1_quantize_fp_neon,
             static_cast<TX_SIZE>(TX_8X32), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_fp_c, &av1_quantize_fp_neon,
             static_cast<TX_SIZE>(TX_32X8), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_fp_32x32_c, &av1_quantize_fp_32x32_neon,
             static_cast<TX_SIZE>(TX_32X32), TYPE_FP, AOM_BITS_8),
  make_tuple(&av1_quantize_fp_64x64_c, &av1_quantize_fp_64x64_neon,
             static_cast<TX_SIZE>(TX_64X64), TYPE_FP, AOM_BITS_8),
  make_tuple(&aom_quantize_b_c, &aom_quantize_b_neon,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_quantize_b_32x32_c, &aom_quantize_b_32x32_neon,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_8),
  make_tuple(&aom_quantize_b_64x64_c, &aom_quantize_b_64x64_neon,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_8),

#if CONFIG_AV1_HIGHBITDEPTH
  make_tuple(&highbd_quan16x16_wrapper<av1_highbd_quantize_fp_c>,
             &highbd_quan16x16_wrapper<av1_highbd_quantize_fp_neon>,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AOM_BITS_12),
  make_tuple(&highbd_quan32x32_wrapper<av1_highbd_quantize_fp_c>,
             &highbd_quan32x32_wrapper<av1_highbd_quantize_fp_neon>,
             static_cast<TX_SIZE>(TX_32X32), TYPE_FP, AOM_BITS_12),
  make_tuple(&highbd_quan64x64_wrapper<av1_highbd_quantize_fp_c>,
             &highbd_quan64x64_wrapper<av1_highbd_quantize_fp_neon>,
             static_cast<TX_SIZE>(TX_64X64), TYPE_FP, AOM_BITS_12),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_neon,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_12),
  make_tuple(&aom_highbd_quantize_b_32x32_c, &aom_highbd_quantize_b_32x32_neon,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_12),
  make_tuple(&aom_highbd_quantize_b_64x64_c, &aom_highbd_quantize_b_64x64_neon,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_12),
#if !CONFIG_REALTIME_ONLY
  make_tuple(&aom_highbd_quantize_b_adaptive_c,
             &aom_highbd_quantize_b_adaptive_neon,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_12),
  make_tuple(&aom_highbd_quantize_b_32x32_adaptive_c,
             &aom_highbd_quantize_b_32x32_adaptive_neon,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_12),
  make_tuple(&aom_highbd_quantize_b_64x64_adaptive_c,
             &aom_highbd_quantize_b_64x64_adaptive_neon,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_12),
#endif  // !CONFIG_REALTIME_ONLY
#endif  // CONFIG_AV1_HIGHBITDEPTH
};

INSTANTIATE_TEST_SUITE_P(NEON, FullPrecisionQuantizeTest,
                         ::testing::ValuesIn(kQParamArrayNEON));
#endif

#if HAVE_SSSE3 && AOM_ARCH_X86_64
INSTANTIATE_TEST_SUITE_P(
    SSSE3, FullPrecisionQuantizeTest,
    ::testing::Values(
        make_tuple(&aom_quantize_b_c, &aom_quantize_b_ssse3,
                   static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_8),
        make_tuple(&aom_quantize_b_32x32_c, &aom_quantize_b_32x32_ssse3,
                   static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_8),
        make_tuple(&aom_quantize_b_64x64_c, &aom_quantize_b_64x64_ssse3,
                   static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_8)));

#endif  // HAVE_SSSE3 && AOM_ARCH_X86_64

#if HAVE_AVX
INSTANTIATE_TEST_SUITE_P(
    AVX, FullPrecisionQuantizeTest,
    ::testing::Values(
        make_tuple(&aom_quantize_b_c, &aom_quantize_b_avx,
                   static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_8),
        make_tuple(&aom_quantize_b_32x32_c, &aom_quantize_b_32x32_avx,
                   static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_8)));

#endif  // HAVE_AVX

}  // namespace
