/*
 * Copyright (c) 2016, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <math.h>

#include "config/aom_dsp_rtcd.h"

#include "aom/aomcx.h"
#include "aom_dsp/quantize.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/bitops.h"
#include "aom_ports/mem.h"

#include "av1/common/idct.h"
#include "av1/common/quant_common.h"
#include "av1/common/scan.h"
#include "av1/common/seg_common.h"

#include "av1/encoder/av1_quantize.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/rd.h"

void av1_quantize_skip(intptr_t n_coeffs, tran_low_t *qcoeff_ptr,
                       tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr) {
  memset(qcoeff_ptr, 0, n_coeffs * sizeof(*qcoeff_ptr));
  memset(dqcoeff_ptr, 0, n_coeffs * sizeof(*dqcoeff_ptr));
  *eob_ptr = 0;
}

int av1_quantize_fp_no_qmatrix(const int16_t quant_ptr[2],
                               const int16_t dequant_ptr[2],
                               const int16_t round_ptr[2], int log_scale,
                               const int16_t *scan, int coeff_count,
                               const tran_low_t *coeff_ptr,
                               tran_low_t *qcoeff_ptr,
                               tran_low_t *dqcoeff_ptr) {
  memset(qcoeff_ptr, 0, coeff_count * sizeof(*qcoeff_ptr));
  memset(dqcoeff_ptr, 0, coeff_count * sizeof(*dqcoeff_ptr));
  const int rounding[2] = { ROUND_POWER_OF_TWO(round_ptr[0], log_scale),
                            ROUND_POWER_OF_TWO(round_ptr[1], log_scale) };
  int eob = 0;
  for (int i = 0; i < coeff_count; i++) {
    const int rc = scan[i];
    const int32_t thresh = (int32_t)(dequant_ptr[rc != 0]);
    const int coeff = coeff_ptr[rc];
    const int coeff_sign = AOMSIGN(coeff);
    int64_t abs_coeff = (coeff ^ coeff_sign) - coeff_sign;
    int tmp32 = 0;
    if ((abs_coeff << (1 + log_scale)) >= thresh) {
      abs_coeff = clamp64(abs_coeff + rounding[rc != 0], INT16_MIN, INT16_MAX);
      tmp32 = (int)((abs_coeff * quant_ptr[rc != 0]) >> (16 - log_scale));
      if (tmp32) {
        qcoeff_ptr[rc] = (tmp32 ^ coeff_sign) - coeff_sign;
        const tran_low_t abs_dqcoeff =
            (tmp32 * dequant_ptr[rc != 0]) >> log_scale;
        dqcoeff_ptr[rc] = (abs_dqcoeff ^ coeff_sign) - coeff_sign;
      }
    }
    if (tmp32) eob = i + 1;
  }
  return eob;
}

static void quantize_fp_helper_c(
    const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int16_t *zbin_ptr,
    const int16_t *round_ptr, const int16_t *quant_ptr,
    const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr,
    tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr,
    const int16_t *scan, const int16_t *iscan, const qm_val_t *qm_ptr,
    const qm_val_t *iqm_ptr, int log_scale) {
  int i, eob = -1;
  const int rounding[2] = { ROUND_POWER_OF_TWO(round_ptr[0], log_scale),
                            ROUND_POWER_OF_TWO(round_ptr[1], log_scale) };
  // TODO(jingning) Decide the need of these arguments after the
  // quantization process is completed.
  (void)zbin_ptr;
  (void)quant_shift_ptr;
  (void)iscan;

  memset(qcoeff_ptr, 0, n_coeffs * sizeof(*qcoeff_ptr));
  memset(dqcoeff_ptr, 0, n_coeffs * sizeof(*dqcoeff_ptr));

  if (qm_ptr == NULL && iqm_ptr == NULL) {
    *eob_ptr = av1_quantize_fp_no_qmatrix(quant_ptr, dequant_ptr, round_ptr,
                                          log_scale, scan, (int)n_coeffs,
                                          coeff_ptr, qcoeff_ptr, dqcoeff_ptr);
  } else {
    // Quantization pass: All coefficients with index >= zero_flag are
    // skippable. Note: zero_flag can be zero.
    for (i = 0; i < n_coeffs; i++) {
      const int rc = scan[i];
      const int coeff = coeff_ptr[rc];
      const qm_val_t wt = qm_ptr ? qm_ptr[rc] : (1 << AOM_QM_BITS);
      const qm_val_t iwt = iqm_ptr ? iqm_ptr[rc] : (1 << AOM_QM_BITS);
      const int dequant =
          (dequant_ptr[rc != 0] * iwt + (1 << (AOM_QM_BITS - 1))) >>
          AOM_QM_BITS;
      const int coeff_sign = AOMSIGN(coeff);
      int64_t abs_coeff = (coeff ^ coeff_sign) - coeff_sign;
      int tmp32 = 0;
      if (abs_coeff * wt >=
          (dequant_ptr[rc != 0] << (AOM_QM_BITS - (1 + log_scale)))) {
        abs_coeff += rounding[rc != 0];
        abs_coeff = clamp64(abs_coeff, INT16_MIN, INT16_MAX);
        tmp32 = (int)((abs_coeff * wt * quant_ptr[rc != 0]) >>
                      (16 - log_scale + AOM_QM_BITS));
        qcoeff_ptr[rc] = (tmp32 ^ coeff_sign) - coeff_sign;
        const tran_low_t abs_dqcoeff = (tmp32 * dequant) >> log_scale;
        dqcoeff_ptr[rc] = (abs_dqcoeff ^ coeff_sign) - coeff_sign;
      }

      if (tmp32) eob = i;
    }
    *eob_ptr = eob + 1;
  }
}

#if CONFIG_AV1_HIGHBITDEPTH
static void highbd_quantize_fp_helper_c(
    const tran_low_t *coeff_ptr, intptr_t count, const int16_t *zbin_ptr,
    const int16_t *round_ptr, const int16_t *quant_ptr,
    const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr,
    tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr,
    const int16_t *scan, const int16_t *iscan, const qm_val_t *qm_ptr,
    const qm_val_t *iqm_ptr, int log_scale) {
  int i;
  int eob = -1;
  const int shift = 16 - log_scale;
  // TODO(jingning) Decide the need of these arguments after the
  // quantization process is completed.
  (void)zbin_ptr;
  (void)quant_shift_ptr;
  (void)iscan;

  if (qm_ptr || iqm_ptr) {
    // Quantization pass: All coefficients with index >= zero_flag are
    // skippable. Note: zero_flag can be zero.
    for (i = 0; i < count; i++) {
      const int rc = scan[i];
      const int coeff = coeff_ptr[rc];
      const qm_val_t wt = qm_ptr != NULL ? qm_ptr[rc] : (1 << AOM_QM_BITS);
      const qm_val_t iwt = iqm_ptr != NULL ? iqm_ptr[rc] : (1 << AOM_QM_BITS);
      const int dequant =
          (dequant_ptr[rc != 0] * iwt + (1 << (AOM_QM_BITS - 1))) >>
          AOM_QM_BITS;
      const int coeff_sign = AOMSIGN(coeff);
      const int64_t abs_coeff = (coeff ^ coeff_sign) - coeff_sign;
      int abs_qcoeff = 0;
      if (abs_coeff * wt >=
          (dequant_ptr[rc != 0] << (AOM_QM_BITS - (1 + log_scale)))) {
        const int64_t tmp =
            abs_coeff + ROUND_POWER_OF_TWO(round_ptr[rc != 0], log_scale);
        abs_qcoeff =
            (int)((tmp * quant_ptr[rc != 0] * wt) >> (shift + AOM_QM_BITS));
        qcoeff_ptr[rc] = (tran_low_t)((abs_qcoeff ^ coeff_sign) - coeff_sign);
        const tran_low_t abs_dqcoeff = (abs_qcoeff * dequant) >> log_scale;
        dqcoeff_ptr[rc] = (tran_low_t)((abs_dqcoeff ^ coeff_sign) - coeff_sign);
        if (abs_qcoeff) eob = i;
      } else {
        qcoeff_ptr[rc] = 0;
        dqcoeff_ptr[rc] = 0;
      }
    }
  } else {
    const int log_scaled_round_arr[2] = {
      ROUND_POWER_OF_TWO(round_ptr[0], log_scale),
      ROUND_POWER_OF_TWO(round_ptr[1], log_scale),
    };
    for (i = 0; i < count; i++) {
      const int rc = scan[i];
      const int coeff = coeff_ptr[rc];
      const int rc01 = (rc != 0);
      const int coeff_sign = AOMSIGN(coeff);
      const int abs_coeff = (coeff ^ coeff_sign) - coeff_sign;
      const int log_scaled_round = log_scaled_round_arr[rc01];
      if ((abs_coeff << (1 + log_scale)) >= dequant_ptr[rc01]) {
        const int quant = quant_ptr[rc01];
        const int dequant = dequant_ptr[rc01];
        const int64_t tmp = (int64_t)abs_coeff + log_scaled_round;
        const int abs_qcoeff = (int)((tmp * quant) >> shift);
        qcoeff_ptr[rc] = (tran_low_t)((abs_qcoeff ^ coeff_sign) - coeff_sign);
        const tran_low_t abs_dqcoeff = (abs_qcoeff * dequant) >> log_scale;
        if (abs_qcoeff) eob = i;
        dqcoeff_ptr[rc] = (tran_low_t)((abs_dqcoeff ^ coeff_sign) - coeff_sign);
      } else {
        qcoeff_ptr[rc] = 0;
        dqcoeff_ptr[rc] = 0;
      }
    }
  }
  *eob_ptr = eob + 1;
}
#endif  // CONFIG_AV1_HIGHBITDEPTH

void av1_quantize_fp_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs,
                       const int16_t *zbin_ptr, const int16_t *round_ptr,
                       const int16_t *quant_ptr, const int16_t *quant_shift_ptr,
                       tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
                       const int16_t *dequant_ptr, uint16_t *eob_ptr,
                       const int16_t *scan, const int16_t *iscan) {
  quantize_fp_helper_c(coeff_ptr, n_coeffs, zbin_ptr, round_ptr, quant_ptr,
                       quant_shift_ptr, qcoeff_ptr, dqcoeff_ptr, dequant_ptr,
                       eob_ptr, scan, iscan, NULL, NULL, 0);
}

void av1_quantize_lp_c(const int16_t *coeff_ptr, intptr_t n_coeffs,
                       const int16_t *round_ptr, const int16_t *quant_ptr,
                       int16_t *qcoeff_ptr, int16_t *dqcoeff_ptr,
                       const int16_t *dequant_ptr, uint16_t *eob_ptr,
                       const int16_t *scan, const int16_t *iscan) {
  (void)iscan;
  int eob = -1;

  memset(qcoeff_ptr, 0, n_coeffs * sizeof(*qcoeff_ptr));
  memset(dqcoeff_ptr, 0, n_coeffs * sizeof(*dqcoeff_ptr));

  // Quantization pass: All coefficients with index >= zero_flag are
  // skippable. Note: zero_flag can be zero.
  for (int i = 0; i < n_coeffs; i++) {
    const int rc = scan[i];
    const int coeff = coeff_ptr[rc];
    const int coeff_sign = AOMSIGN(coeff);
    const int abs_coeff = (coeff ^ coeff_sign) - coeff_sign;

    int tmp = clamp(abs_coeff + round_ptr[rc != 0], INT16_MIN, INT16_MAX);
    tmp = (tmp * quant_ptr[rc != 0]) >> 16;

    qcoeff_ptr[rc] = (tmp ^ coeff_sign) - coeff_sign;
    dqcoeff_ptr[rc] = qcoeff_ptr[rc] * dequant_ptr[rc != 0];

    if (tmp) eob = i;
  }
  *eob_ptr = eob + 1;
}

void av1_quantize_fp_32x32_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs,
                             const int16_t *zbin_ptr, const int16_t *round_ptr,
                             const int16_t *quant_ptr,
                             const int16_t *quant_shift_ptr,
                             tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
                             const int16_t *dequant_ptr, uint16_t *eob_ptr,
                             const int16_t *scan, const int16_t *iscan) {
  quantize_fp_helper_c(coeff_ptr, n_coeffs, zbin_ptr, round_ptr, quant_ptr,
                       quant_shift_ptr, qcoeff_ptr, dqcoeff_ptr, dequant_ptr,
                       eob_ptr, scan, iscan, NULL, NULL, 1);
}

void av1_quantize_fp_64x64_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs,
                             const int16_t *zbin_ptr, const int16_t *round_ptr,
                             const int16_t *quant_ptr,
                             const int16_t *quant_shift_ptr,
                             tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
                             const int16_t *dequant_ptr, uint16_t *eob_ptr,
                             const int16_t *scan, const int16_t *iscan) {
  quantize_fp_helper_c(coeff_ptr, n_coeffs, zbin_ptr, round_ptr, quant_ptr,
                       quant_shift_ptr, qcoeff_ptr, dqcoeff_ptr, dequant_ptr,
                       eob_ptr, scan, iscan, NULL, NULL, 2);
}

void av1_quantize_fp_facade(const tran_low_t *coeff_ptr, intptr_t n_coeffs,
                            const MACROBLOCK_PLANE *p, tran_low_t *qcoeff_ptr,
                            tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr,
                            const SCAN_ORDER *sc, const QUANT_PARAM *qparam) {
  const qm_val_t *qm_ptr = qparam->qmatrix;
  const qm_val_t *iqm_ptr = qparam->iqmatrix;
  if (qm_ptr != NULL && iqm_ptr != NULL) {
    quantize_fp_helper_c(coeff_ptr, n_coeffs, p->zbin_QTX, p->round_fp_QTX,
                         p->quant_fp_QTX, p->quant_shift_QTX, qcoeff_ptr,
                         dqcoeff_ptr, p->dequant_QTX, eob_ptr, sc->scan,
                         sc->iscan, qm_ptr, iqm_ptr, qparam->log_scale);
  } else {
    switch (qparam->log_scale) {
      case 0:
        av1_quantize_fp(coeff_ptr, n_coeffs, p->zbin_QTX, p->round_fp_QTX,
                        p->quant_fp_QTX, p->quant_shift_QTX, qcoeff_ptr,
                        dqcoeff_ptr, p->dequant_QTX, eob_ptr, sc->scan,
                        sc->iscan);
        break;
      case 1:
        av1_quantize_fp_32x32(coeff_ptr, n_coeffs, p->zbin_QTX, p->round_fp_QTX,
                              p->quant_fp_QTX, p->quant_shift_QTX, qcoeff_ptr,
                              dqcoeff_ptr, p->dequant_QTX, eob_ptr, sc->scan,
                              sc->iscan);
        break;
      case 2:
        av1_quantize_fp_64x64(coeff_ptr, n_coeffs, p->zbin_QTX, p->round_fp_QTX,
                              p->quant_fp_QTX, p->quant_shift_QTX, qcoeff_ptr,
                              dqcoeff_ptr, p->dequant_QTX, eob_ptr, sc->scan,
                              sc->iscan);
        break;
      default: assert(0);
    }
  }
}

void av1_quantize_b_facade(const tran_low_t *coeff_ptr, intptr_t n_coeffs,
                           const MACROBLOCK_PLANE *p, tran_low_t *qcoeff_ptr,
                           tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr,
                           const SCAN_ORDER *sc, const QUANT_PARAM *qparam) {
  const qm_val_t *qm_ptr = qparam->qmatrix;
  const qm_val_t *iqm_ptr = qparam->iqmatrix;
#if !CONFIG_REALTIME_ONLY
  if (qparam->use_quant_b_adapt) {
    // TODO(sarahparker) These quantize_b optimizations need SIMD
    // implementations
    if (qm_ptr != NULL && iqm_ptr != NULL) {
      aom_quantize_b_adaptive_helper_c(
          coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX, p->quant_QTX,
          p->quant_shift_QTX, qcoeff_ptr, dqcoeff_ptr, p->dequant_QTX, eob_ptr,
          sc->scan, sc->iscan, qm_ptr, iqm_ptr, qparam->log_scale);
    } else {
      switch (qparam->log_scale) {
        case 0:
          aom_quantize_b_adaptive(coeff_ptr, n_coeffs, p->zbin_QTX,
                                  p->round_QTX, p->quant_QTX,
                                  p->quant_shift_QTX, qcoeff_ptr, dqcoeff_ptr,
                                  p->dequant_QTX, eob_ptr, sc->scan, sc->iscan);
          break;
        case 1:
          aom_quantize_b_32x32_adaptive(
              coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX, p->quant_QTX,
              p->quant_shift_QTX, qcoeff_ptr, dqcoeff_ptr, p->dequant_QTX,
              eob_ptr, sc->scan, sc->iscan);
          break;
        case 2:
          aom_quantize_b_64x64_adaptive(
              coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX, p->quant_QTX,
              p->quant_shift_QTX, qcoeff_ptr, dqcoeff_ptr, p->dequant_QTX,
              eob_ptr, sc->scan, sc->iscan);
          break;
        default: assert(0);
      }
    }
    return;
  }
#endif  // !CONFIG_REALTIME_ONLY

  if (qm_ptr != NULL && iqm_ptr != NULL) {
    aom_quantize_b_helper_c(coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX,
                            p->quant_QTX, p->quant_shift_QTX, qcoeff_ptr,
                            dqcoeff_ptr, p->dequant_QTX, eob_ptr, sc->scan,
                            sc->iscan, qm_ptr, iqm_ptr, qparam->log_scale);
  } else {
    switch (qparam->log_scale) {
      case 0:
        aom_quantize_b(coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX,
                       p->quant_QTX, p->quant_shift_QTX, qcoeff_ptr,
                       dqcoeff_ptr, p->dequant_QTX, eob_ptr, sc->scan,
                       sc->iscan);
        break;
      case 1:
        aom_quantize_b_32x32(coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX,
                             p->quant_QTX, p->quant_shift_QTX, qcoeff_ptr,
                             dqcoeff_ptr, p->dequant_QTX, eob_ptr, sc->scan,
                             sc->iscan);
        break;
      case 2:
        aom_quantize_b_64x64(coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX,
                             p->quant_QTX, p->quant_shift_QTX, qcoeff_ptr,
                             dqcoeff_ptr, p->dequant_QTX, eob_ptr, sc->scan,
                             sc->iscan);
        break;
      default: assert(0);
    }
  }
}

static void quantize_dc(const tran_low_t *coeff_ptr, int n_coeffs,
                        int skip_block, const int16_t *round_ptr,
                        const int16_t quant, tran_low_t *qcoeff_ptr,
                        tran_low_t *dqcoeff_ptr, const int16_t dequant_ptr,
                        uint16_t *eob_ptr, const qm_val_t *qm_ptr,
                        const qm_val_t *iqm_ptr, const int log_scale) {
  const int rc = 0;
  const int coeff = coeff_ptr[rc];
  const int coeff_sign = AOMSIGN(coeff);
  const int abs_coeff = (coeff ^ coeff_sign) - coeff_sign;
  int64_t tmp;
  int eob = -1;
  int32_t tmp32;
  int dequant;

  memset(qcoeff_ptr, 0, n_coeffs * sizeof(*qcoeff_ptr));
  memset(dqcoeff_ptr, 0, n_coeffs * sizeof(*dqcoeff_ptr));

  if (!skip_block) {
    const int wt = qm_ptr != NULL ? qm_ptr[rc] : (1 << AOM_QM_BITS);
    const int iwt = iqm_ptr != NULL ? iqm_ptr[rc] : (1 << AOM_QM_BITS);
    tmp = clamp(abs_coeff + ROUND_POWER_OF_TWO(round_ptr[rc != 0], log_scale),
                INT16_MIN, INT16_MAX);
    tmp32 = (int32_t)((tmp * wt * quant) >> (16 - log_scale + AOM_QM_BITS));
    qcoeff_ptr[rc] = (tmp32 ^ coeff_sign) - coeff_sign;
    dequant = (dequant_ptr * iwt + (1 << (AOM_QM_BITS - 1))) >> AOM_QM_BITS;
    const tran_low_t abs_dqcoeff = (tmp32 * dequant) >> log_scale;
    dqcoeff_ptr[rc] = (tran_low_t)((abs_dqcoeff ^ coeff_sign) - coeff_sign);
    if (tmp32) eob = 0;
  }
  *eob_ptr = eob + 1;
}

void av1_quantize_dc_facade(const tran_low_t *coeff_ptr, intptr_t n_coeffs,
                            const MACROBLOCK_PLANE *p, tran_low_t *qcoeff_ptr,
                            tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr,
                            const SCAN_ORDER *sc, const QUANT_PARAM *qparam) {
  // obsolete skip_block
  const int skip_block = 0;
  (void)sc;
  assert(qparam->log_scale >= 0 && qparam->log_scale < (3));
  const qm_val_t *qm_ptr = qparam->qmatrix;
  const qm_val_t *iqm_ptr = qparam->iqmatrix;
  quantize_dc(coeff_ptr, (int)n_coeffs, skip_block, p->round_QTX,
              p->quant_fp_QTX[0], qcoeff_ptr, dqcoeff_ptr, p->dequant_QTX[0],
              eob_ptr, qm_ptr, iqm_ptr, qparam->log_scale);
}

#if CONFIG_AV1_HIGHBITDEPTH
void av1_highbd_quantize_fp_facade(const tran_low_t *coeff_ptr,
                                   intptr_t n_coeffs, const MACROBLOCK_PLANE *p,
                                   tran_low_t *qcoeff_ptr,
                                   tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr,
                                   const SCAN_ORDER *sc,
                                   const QUANT_PARAM *qparam) {
  const qm_val_t *qm_ptr = qparam->qmatrix;
  const qm_val_t *iqm_ptr = qparam->iqmatrix;
  if (qm_ptr != NULL && iqm_ptr != NULL) {
    highbd_quantize_fp_helper_c(
        coeff_ptr, n_coeffs, p->zbin_QTX, p->round_fp_QTX, p->quant_fp_QTX,
        p->quant_shift_QTX, qcoeff_ptr, dqcoeff_ptr, p->dequant_QTX, eob_ptr,
        sc->scan, sc->iscan, qm_ptr, iqm_ptr, qparam->log_scale);
  } else {
    av1_highbd_quantize_fp(coeff_ptr, n_coeffs, p->zbin_QTX, p->round_fp_QTX,
                           p->quant_fp_QTX, p->quant_shift_QTX, qcoeff_ptr,
                           dqcoeff_ptr, p->dequant_QTX, eob_ptr, sc->scan,
                           sc->iscan, qparam->log_scale);
  }
}

void av1_highbd_quantize_b_facade(const tran_low_t *coeff_ptr,
                                  intptr_t n_coeffs, const MACROBLOCK_PLANE *p,
                                  tran_low_t *qcoeff_ptr,
                                  tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr,
                                  const SCAN_ORDER *sc,
                                  const QUANT_PARAM *qparam) {
  const qm_val_t *qm_ptr = qparam->qmatrix;
  const qm_val_t *iqm_ptr = qparam->iqmatrix;
#if !CONFIG_REALTIME_ONLY
  if (qparam->use_quant_b_adapt) {
    if (qm_ptr != NULL && iqm_ptr != NULL) {
      aom_highbd_quantize_b_adaptive_helper_c(
          coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX, p->quant_QTX,
          p->quant_shift_QTX, qcoeff_ptr, dqcoeff_ptr, p->dequant_QTX, eob_ptr,
          sc->scan, sc->iscan, qm_ptr, iqm_ptr, qparam->log_scale);
    } else {
      switch (qparam->log_scale) {
        case 0:
          aom_highbd_quantize_b_adaptive(
              coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX, p->quant_QTX,
              p->quant_shift_QTX, qcoeff_ptr, dqcoeff_ptr, p->dequant_QTX,
              eob_ptr, sc->scan, sc->iscan);
          break;
        case 1:
          aom_highbd_quantize_b_32x32_adaptive(
              coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX, p->quant_QTX,
              p->quant_shift_QTX, qcoeff_ptr, dqcoeff_ptr, p->dequant_QTX,
              eob_ptr, sc->scan, sc->iscan);
          break;
        case 2:
          aom_highbd_quantize_b_64x64_adaptive(
              coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX, p->quant_QTX,
              p->quant_shift_QTX, qcoeff_ptr, dqcoeff_ptr, p->dequant_QTX,
              eob_ptr, sc->scan, sc->iscan);
          break;
        default: assert(0);
      }
    }
    return;
  }
#endif  // !CONFIG_REALTIME_ONLY

  if (qm_ptr != NULL && iqm_ptr != NULL) {
    aom_highbd_quantize_b_helper_c(
        coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX, p->quant_QTX,
        p->quant_shift_QTX, qcoeff_ptr, dqcoeff_ptr, p->dequant_QTX, eob_ptr,
        sc->scan, sc->iscan, qm_ptr, iqm_ptr, qparam->log_scale);
  } else {
    switch (qparam->log_scale) {
      case 0:
        aom_highbd_quantize_b(coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX,
                              p->quant_QTX, p->quant_shift_QTX, qcoeff_ptr,
                              dqcoeff_ptr, p->dequant_QTX, eob_ptr, sc->scan,
                              sc->iscan);
        break;
      case 1:
        aom_highbd_quantize_b_32x32(
            coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX, p->quant_QTX,
            p->quant_shift_QTX, qcoeff_ptr, dqcoeff_ptr, p->dequant_QTX,
            eob_ptr, sc->scan, sc->iscan);
        break;
      case 2:
        aom_highbd_quantize_b_64x64(
            coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX, p->quant_QTX,
            p->quant_shift_QTX, qcoeff_ptr, dqcoeff_ptr, p->dequant_QTX,
            eob_ptr, sc->scan, sc->iscan);
        break;
      default: assert(0);
    }
  }
}

static inline void highbd_quantize_dc(
    const tran_low_t *coeff_ptr, int n_coeffs, int skip_block,
    const int16_t *round_ptr, const int16_t quant, tran_low_t *qcoeff_ptr,
    tran_low_t *dqcoeff_ptr, const int16_t dequant_ptr, uint16_t *eob_ptr,
    const qm_val_t *qm_ptr, const qm_val_t *iqm_ptr, const int log_scale) {
  int eob = -1;

  memset(qcoeff_ptr, 0, n_coeffs * sizeof(*qcoeff_ptr));
  memset(dqcoeff_ptr, 0, n_coeffs * sizeof(*dqcoeff_ptr));

  if (!skip_block) {
    const qm_val_t wt = qm_ptr != NULL ? qm_ptr[0] : (1 << AOM_QM_BITS);
    const qm_val_t iwt = iqm_ptr != NULL ? iqm_ptr[0] : (1 << AOM_QM_BITS);
    const int coeff = coeff_ptr[0];
    const int coeff_sign = AOMSIGN(coeff);
    const int abs_coeff = (coeff ^ coeff_sign) - coeff_sign;
    const int64_t tmp = abs_coeff + ROUND_POWER_OF_TWO(round_ptr[0], log_scale);
    const int64_t tmpw = tmp * wt;
    const int abs_qcoeff =
        (int)((tmpw * quant) >> (16 - log_scale + AOM_QM_BITS));
    qcoeff_ptr[0] = (tran_low_t)((abs_qcoeff ^ coeff_sign) - coeff_sign);
    const int dequant =
        (dequant_ptr * iwt + (1 << (AOM_QM_BITS - 1))) >> AOM_QM_BITS;

    const tran_low_t abs_dqcoeff = (abs_qcoeff * dequant) >> log_scale;
    dqcoeff_ptr[0] = (tran_low_t)((abs_dqcoeff ^ coeff_sign) - coeff_sign);
    if (abs_qcoeff) eob = 0;
  }
  *eob_ptr = eob + 1;
}

void av1_highbd_quantize_dc_facade(const tran_low_t *coeff_ptr,
                                   intptr_t n_coeffs, const MACROBLOCK_PLANE *p,
                                   tran_low_t *qcoeff_ptr,
                                   tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr,
                                   const SCAN_ORDER *sc,
                                   const QUANT_PARAM *qparam) {
  // obsolete skip_block
  const int skip_block = 0;
  const qm_val_t *qm_ptr = qparam->qmatrix;
  const qm_val_t *iqm_ptr = qparam->iqmatrix;
  (void)sc;

  highbd_quantize_dc(coeff_ptr, (int)n_coeffs, skip_block, p->round_QTX,
                     p->quant_fp_QTX[0], qcoeff_ptr, dqcoeff_ptr,
                     p->dequant_QTX[0], eob_ptr, qm_ptr, iqm_ptr,
                     qparam->log_scale);
}

void av1_highbd_quantize_fp_c(const tran_low_t *coeff_ptr, intptr_t count,
                              const int16_t *zbin_ptr, const int16_t *round_ptr,
                              const int16_t *quant_ptr,
                              const int16_t *quant_shift_ptr,
                              tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
                              const int16_t *dequant_ptr, uint16_t *eob_ptr,
                              const int16_t *scan, const int16_t *iscan,
                              int log_scale) {
  highbd_quantize_fp_helper_c(coeff_ptr, count, zbin_ptr, round_ptr, quant_ptr,
                              quant_shift_ptr, qcoeff_ptr, dqcoeff_ptr,
                              dequant_ptr, eob_ptr, scan, iscan, NULL, NULL,
                              log_scale);
}
#endif  // CONFIG_AV1_HIGHBITDEPTH

static void invert_quant(int16_t *quant, int16_t *shift, int d) {
  uint32_t t;
  int l, m;
  t = d;
  l = get_msb(t);
  m = 1 + (1 << (16 + l)) / d;
  *quant = (int16_t)(m - (1 << 16));
  *shift = 1 << (16 - l);
}

static int get_qzbin_factor(int q, aom_bit_depth_t bit_depth) {
  const int quant = av1_dc_quant_QTX(q, 0, bit_depth);
  switch (bit_depth) {
    case AOM_BITS_8: return q == 0 ? 64 : (quant < 148 ? 84 : 80);
    case AOM_BITS_10: return q == 0 ? 64 : (quant < 592 ? 84 : 80);
    case AOM_BITS_12: return q == 0 ? 64 : (quant < 2368 ? 84 : 80);
    default:
      assert(0 && "bit_depth should be AOM_BITS_8, AOM_BITS_10 or AOM_BITS_12");
      return -1;
  }
}

void av1_build_quantizer(aom_bit_depth_t bit_depth, int y_dc_delta_q,
                         int u_dc_delta_q, int u_ac_delta_q, int v_dc_delta_q,
                         int v_ac_delta_q, QUANTS *const quants,
                         Dequants *const deq, int sharpness) {
  int i, q, quant_QTX;
  const int sharpness_adjustment = 16 * (7 - sharpness) / 7;

  for (q = 0; q < QINDEX_RANGE; q++) {
    const int qzbin_factor = get_qzbin_factor(q, bit_depth);
    int qrounding_factor = q == 0 ? 64 : 48;

    for (i = 0; i < 2; ++i) {
      int qrounding_factor_fp = 64;

      if (sharpness != 0 && q != 0) {
        qrounding_factor = 64 - sharpness_adjustment;
        qrounding_factor_fp = 64 - sharpness_adjustment;
      }

      // y quantizer with TX scale
      quant_QTX = i == 0 ? av1_dc_quant_QTX(q, y_dc_delta_q, bit_depth)
                         : av1_ac_quant_QTX(q, 0, bit_depth);
      invert_quant(&quants->y_quant[q][i], &quants->y_quant_shift[q][i],
                   quant_QTX);
      quants->y_quant_fp[q][i] = (1 << 16) / quant_QTX;
      quants->y_round_fp[q][i] = (qrounding_factor_fp * quant_QTX) >> 7;
      quants->y_zbin[q][i] = ROUND_POWER_OF_TWO(qzbin_factor * quant_QTX, 7);
      quants->y_round[q][i] = (qrounding_factor * quant_QTX) >> 7;
      deq->y_dequant_QTX[q][i] = quant_QTX;

      // u quantizer with TX scale
      quant_QTX = i == 0 ? av1_dc_quant_QTX(q, u_dc_delta_q, bit_depth)
                         : av1_ac_quant_QTX(q, u_ac_delta_q, bit_depth);
      invert_quant(&quants->u_quant[q][i], &quants->u_quant_shift[q][i],
                   quant_QTX);
      quants->u_quant_fp[q][i] = (1 << 16) / quant_QTX;
      quants->u_round_fp[q][i] = (qrounding_factor_fp * quant_QTX) >> 7;
      quants->u_zbin[q][i] = ROUND_POWER_OF_TWO(qzbin_factor * quant_QTX, 7);
      quants->u_round[q][i] = (qrounding_factor * quant_QTX) >> 7;
      deq->u_dequant_QTX[q][i] = quant_QTX;

      // v quantizer with TX scale
      quant_QTX = i == 0 ? av1_dc_quant_QTX(q, v_dc_delta_q, bit_depth)
                         : av1_ac_quant_QTX(q, v_ac_delta_q, bit_depth);
      invert_quant(&quants->v_quant[q][i], &quants->v_quant_shift[q][i],
                   quant_QTX);
      quants->v_quant_fp[q][i] = (1 << 16) / quant_QTX;
      quants->v_round_fp[q][i] = (qrounding_factor_fp * quant_QTX) >> 7;
      quants->v_zbin[q][i] = ROUND_POWER_OF_TWO(qzbin_factor * quant_QTX, 7);
      quants->v_round[q][i] = (qrounding_factor * quant_QTX) >> 7;
      deq->v_dequant_QTX[q][i] = quant_QTX;
    }

    for (i = 2; i < 8; i++) {  // 8: SIMD width
      quants->y_quant[q][i] = quants->y_quant[q][1];
      quants->y_quant_fp[q][i] = quants->y_quant_fp[q][1];
      quants->y_round_fp[q][i] = quants->y_round_fp[q][1];
      quants->y_quant_shift[q][i] = quants->y_quant_shift[q][1];
      quants->y_zbin[q][i] = quants->y_zbin[q][1];
      quants->y_round[q][i] = quants->y_round[q][1];
      deq->y_dequant_QTX[q][i] = deq->y_dequant_QTX[q][1];

      quants->u_quant[q][i] = quants->u_quant[q][1];
      quants->u_quant_fp[q][i] = quants->u_quant_fp[q][1];
      quants->u_round_fp[q][i] = quants->u_round_fp[q][1];
      quants->u_quant_shift[q][i] = quants->u_quant_shift[q][1];
      quants->u_zbin[q][i] = quants->u_zbin[q][1];
      quants->u_round[q][i] = quants->u_round[q][1];
      deq->u_dequant_QTX[q][i] = deq->u_dequant_QTX[q][1];

      quants->v_quant[q][i] = quants->v_quant[q][1];
      quants->v_quant_fp[q][i] = quants->v_quant_fp[q][1];
      quants->v_round_fp[q][i] = quants->v_round_fp[q][1];
      quants->v_quant_shift[q][i] = quants->v_quant_shift[q][1];
      quants->v_zbin[q][i] = quants->v_zbin[q][1];
      quants->v_round[q][i] = quants->v_round[q][1];
      deq->v_dequant_QTX[q][i] = deq->v_dequant_QTX[q][1];
    }
  }
}

static inline bool deltaq_params_have_changed(
    const DeltaQuantParams *prev_deltaq_params,
    const CommonQuantParams *quant_params) {
  return (prev_deltaq_params->y_dc_delta_q != quant_params->y_dc_delta_q ||
          prev_deltaq_params->u_dc_delta_q != quant_params->u_dc_delta_q ||
          prev_deltaq_params->v_dc_delta_q != quant_params->v_dc_delta_q ||
          prev_deltaq_params->u_ac_delta_q != quant_params->u_ac_delta_q ||
          prev_deltaq_params->v_ac_delta_q != quant_params->v_ac_delta_q ||
          prev_deltaq_params->sharpness != quant_params->sharpness);
}

void av1_init_quantizer(EncQuantDequantParams *const enc_quant_dequant_params,
                        CommonQuantParams *quant_params,
                        aom_bit_depth_t bit_depth, int sharpness) {
  DeltaQuantParams *const prev_deltaq_params =
      &enc_quant_dequant_params->prev_deltaq_params;
  quant_params->sharpness = sharpness;

  // Re-initialize the quantizer only if any of the dc/ac deltaq parameters
  // change.
  if (!deltaq_params_have_changed(prev_deltaq_params, quant_params)) return;
  QUANTS *const quants = &enc_quant_dequant_params->quants;
  Dequants *const dequants = &enc_quant_dequant_params->dequants;
  av1_build_quantizer(bit_depth, quant_params->y_dc_delta_q,
                      quant_params->u_dc_delta_q, quant_params->u_ac_delta_q,
                      quant_params->v_dc_delta_q, quant_params->v_ac_delta_q,
                      quants, dequants, sharpness);

  // Record the state of deltaq parameters.
  prev_deltaq_params->y_dc_delta_q = quant_params->y_dc_delta_q;
  prev_deltaq_params->u_dc_delta_q = quant_params->u_dc_delta_q;
  prev_deltaq_params->v_dc_delta_q = quant_params->v_dc_delta_q;
  prev_deltaq_params->u_ac_delta_q = quant_params->u_ac_delta_q;
  prev_deltaq_params->v_ac_delta_q = quant_params->v_ac_delta_q;
  prev_deltaq_params->sharpness = sharpness;
}

/*!\brief Update quantize parameters in MACROBLOCK
 *
 * \param[in]  enc_quant_dequant_params This parameter cached the quantize and
 *                                      dequantize parameters for all q
 *                                      indices.
 * \param[in]  qindex                   Quantize index used for the current
 *                                      superblock.
 * \param[out] x                        A superblock data structure for
 *                                      encoder.
 */
static void set_q_index(const EncQuantDequantParams *enc_quant_dequant_params,
                        int qindex, MACROBLOCK *x) {
  const QUANTS *const quants = &enc_quant_dequant_params->quants;
  const Dequants *const dequants = &enc_quant_dequant_params->dequants;
  x->qindex = qindex;
  x->seg_skip_block =
      0;  // TODO(angiebird): Find a proper place to init this variable.

  // Y
  x->plane[0].quant_QTX = quants->y_quant[qindex];
  x->plane[0].quant_fp_QTX = quants->y_quant_fp[qindex];
  x->plane[0].round_fp_QTX = quants->y_round_fp[qindex];
  x->plane[0].quant_shift_QTX = quants->y_quant_shift[qindex];
  x->plane[0].zbin_QTX = quants->y_zbin[qindex];
  x->plane[0].round_QTX = quants->y_round[qindex];
  x->plane[0].dequant_QTX = dequants->y_dequant_QTX[qindex];

  // U
  x->plane[1].quant_QTX = quants->u_quant[qindex];
  x->plane[1].quant_fp_QTX = quants->u_quant_fp[qindex];
  x->plane[1].round_fp_QTX = quants->u_round_fp[qindex];
  x->plane[1].quant_shift_QTX = quants->u_quant_shift[qindex];
  x->plane[1].zbin_QTX = quants->u_zbin[qindex];
  x->plane[1].round_QTX = quants->u_round[qindex];
  x->plane[1].dequant_QTX = dequants->u_dequant_QTX[qindex];

  // V
  x->plane[2].quant_QTX = quants->v_quant[qindex];
  x->plane[2].quant_fp_QTX = quants->v_quant_fp[qindex];
  x->plane[2].round_fp_QTX = quants->v_round_fp[qindex];
  x->plane[2].quant_shift_QTX = quants->v_quant_shift[qindex];
  x->plane[2].zbin_QTX = quants->v_zbin[qindex];
  x->plane[2].round_QTX = quants->v_round[qindex];
  x->plane[2].dequant_QTX = dequants->v_dequant_QTX[qindex];
}

/*!\brief Update quantize matrix in MACROBLOCKD based on segment id
 *
 * \param[in]  quant_params  Quantize parameters used by encoder and decoder
 * \param[in]  segment_id    Segment id.
 * \param[out] xd            A superblock data structure used by encoder and
 * decoder.
 */
static void set_qmatrix(const CommonQuantParams *quant_params, int segment_id,
                        MACROBLOCKD *xd) {
  const int use_qmatrix = av1_use_qmatrix(quant_params, xd, segment_id);
  const int qmlevel_y =
      use_qmatrix ? quant_params->qmatrix_level_y : NUM_QM_LEVELS - 1;
  const int qmlevel_u =
      use_qmatrix ? quant_params->qmatrix_level_u : NUM_QM_LEVELS - 1;
  const int qmlevel_v =
      use_qmatrix ? quant_params->qmatrix_level_v : NUM_QM_LEVELS - 1;
  const int qmlevel_ls[MAX_MB_PLANE] = { qmlevel_y, qmlevel_u, qmlevel_v };
  for (int i = 0; i < MAX_MB_PLANE; ++i) {
    const int qmlevel = qmlevel_ls[i];
    memcpy(&xd->plane[i].seg_qmatrix[segment_id],
           quant_params->gqmatrix[qmlevel][i],
           sizeof(quant_params->gqmatrix[qmlevel][i]));
    memcpy(&xd->plane[i].seg_iqmatrix[segment_id],
           quant_params->giqmatrix[qmlevel][i],
           sizeof(quant_params->giqmatrix[qmlevel][i]));
  }
}

void av1_init_plane_quantizers(const AV1_COMP *cpi, MACROBLOCK *x,
                               int segment_id, const int do_update) {
  const AV1_COMMON *const cm = &cpi->common;
  const CommonQuantParams *const quant_params = &cm->quant_params;
  const GF_GROUP *const gf_group = &cpi->ppi->gf_group;
  const int boost_index = AOMMIN(15, (cpi->ppi->p_rc.gfu_boost / 100));
  const int layer_depth = AOMMIN(gf_group->layer_depth[cpi->gf_frame_index], 6);
  const FRAME_TYPE frame_type = cm->current_frame.frame_type;
  int qindex_rd;

  const int current_qindex = AOMMAX(
      0,
      AOMMIN(QINDEX_RANGE - 1, cm->delta_q_info.delta_q_present_flag
                                   ? quant_params->base_qindex + x->delta_qindex
                                   : quant_params->base_qindex));
  const int qindex = av1_get_qindex(&cm->seg, segment_id, current_qindex);

  if (cpi->oxcf.sb_qp_sweep) {
    const int current_rd_qindex =
        AOMMAX(0, AOMMIN(QINDEX_RANGE - 1, cm->delta_q_info.delta_q_present_flag
                                               ? quant_params->base_qindex +
                                                     x->rdmult_delta_qindex
                                               : quant_params->base_qindex));
    qindex_rd = av1_get_qindex(&cm->seg, segment_id, current_rd_qindex);
  } else {
    qindex_rd = qindex;
  }

  const int qindex_rdmult = qindex_rd + quant_params->y_dc_delta_q;
  const int rdmult = av1_compute_rd_mult(
      qindex_rdmult, cm->seq_params->bit_depth,
      cpi->ppi->gf_group.update_type[cpi->gf_frame_index], layer_depth,
      boost_index, frame_type, cpi->oxcf.q_cfg.use_fixed_qp_offsets,
      is_stat_consumption_stage(cpi), cpi->oxcf.tune_cfg.tuning);

  const int qindex_change = x->qindex != qindex;
  if (qindex_change || do_update) {
    set_q_index(&cpi->enc_quant_dequant_params, qindex, x);
  }

  MACROBLOCKD *const xd = &x->e_mbd;
  if ((segment_id != x->prev_segment_id) ||
      av1_use_qmatrix(quant_params, xd, segment_id)) {
    set_qmatrix(quant_params, segment_id, xd);
  }

  x->seg_skip_block = segfeature_active(&cm->seg, segment_id, SEG_LVL_SKIP);

  av1_set_error_per_bit(&x->errorperbit, rdmult);
  av1_set_sad_per_bit(cpi, &x->sadperbit, qindex_rd);

  x->prev_segment_id = segment_id;
}

void av1_frame_init_quantizer(AV1_COMP *cpi) {
  MACROBLOCK *const x = &cpi->td.mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  x->prev_segment_id = -1;
  av1_init_plane_quantizers(cpi, x, xd->mi[0]->segment_id, 1);
}

static int adjust_hdr_cb_deltaq(int base_qindex) {
  double baseQp = base_qindex / QP_SCALE_FACTOR;
  const double chromaQp = CHROMA_QP_SCALE * baseQp + CHROMA_QP_OFFSET;
  const double dcbQP = CHROMA_CB_QP_SCALE * chromaQp * QP_SCALE_FACTOR;
  int dqpCb = (int)(dcbQP + (dcbQP < 0 ? -0.5 : 0.5));
  dqpCb = AOMMIN(0, dqpCb);
  dqpCb = (int)CLIP(dqpCb, -12 * QP_SCALE_FACTOR, 12 * QP_SCALE_FACTOR);
  return dqpCb;
}

static int adjust_hdr_cr_deltaq(int base_qindex) {
  double baseQp = base_qindex / QP_SCALE_FACTOR;
  const double chromaQp = CHROMA_QP_SCALE * baseQp + CHROMA_QP_OFFSET;
  const double dcrQP = CHROMA_CR_QP_SCALE * chromaQp * QP_SCALE_FACTOR;
  int dqpCr = (int)(dcrQP + (dcrQP < 0 ? -0.5 : 0.5));
  dqpCr = AOMMIN(0, dqpCr);
  dqpCr = (int)CLIP(dqpCr, -12 * QP_SCALE_FACTOR, 12 * QP_SCALE_FACTOR);
  return dqpCr;
}

void av1_set_quantizer(AV1_COMMON *const cm, int min_qmlevel, int max_qmlevel,
                       int q, int enable_chroma_deltaq, int enable_hdr_deltaq,
                       bool is_allintra, aom_tune_metric tuning) {
  // quantizer has to be reinitialized with av1_init_quantizer() if any
  // delta_q changes.
  CommonQuantParams *quant_params = &cm->quant_params;
  quant_params->base_qindex = AOMMAX(cm->delta_q_info.delta_q_present_flag, q);
  quant_params->y_dc_delta_q = 0;

  if (enable_chroma_deltaq) {
    if (is_allintra &&
        (tuning == AOM_TUNE_IQ || tuning == AOM_TUNE_SSIMULACRA2)) {
      int chroma_dc_delta_q = 0;
      int chroma_ac_delta_q = 0;

      if (cm->seq_params->subsampling_x == 1 &&
          cm->seq_params->subsampling_y == 1) {
        // 4:2:0 subsampling: Constant chroma delta_q decrease (i.e. improved
        // chroma quality relative to luma) with gradual ramp-down for very low
        // qindexes.
        // Lowering chroma delta_q by 16 was found to improve SSIMULACRA 2
        // BD-Rate by 1.5-2% on Daala's subset1, as well as reducing chroma
        // artifacts (smudging, discoloration) during subjective quality
        // evaluations.
        // The ramp-down of chroma increase was determined by generating the
        // convex hull of SSIMULACRA 2 scores (for all boosts from 0-16), and
        // finding a linear equation that fits the convex hull.
        chroma_dc_delta_q = -clamp((quant_params->base_qindex / 2) - 14, 0, 16);
        chroma_ac_delta_q = chroma_dc_delta_q;
      } else if (cm->seq_params->subsampling_x == 1 &&
                 cm->seq_params->subsampling_y == 0) {
        // 4:2:2 subsampling: Constant chroma AC delta_q increase (i.e. improved
        // luma quality relative to chroma) with gradual ramp-down for very low
        // qindexes.
        // SSIMULACRA 2 appears to have some issues correctly scoring 4:2:2
        // material. Solely optimizing for maximum scores suggests a chroma AC
        // delta_q of 12 is the most efficient. However, visual inspection on
        // difficult-to-encode material resulted in chroma quality degrading too
        // much relative to luma, and chroma channels ending up being too small
        // compared to equivalent 4:4:4 or 4:2:0 encodes.
        // A chroma AC delta_q of 6 was selected because encoded chroma channels
        // have a much closer size to 4:4:4 and 4:2:0 encodes, and have more
        // favorable visual quality characteristics.
        // The ramp-down of chroma decrease was put into place to match 4:2:0
        // and 4:4:4 behavior. There were no special considerations on
        // SSIMULACRA 2 scores.
        chroma_dc_delta_q = 0;
        chroma_ac_delta_q = clamp((quant_params->base_qindex / 2), 0, 6);
      } else if (cm->seq_params->subsampling_x == 0 &&
                 cm->seq_params->subsampling_y == 0) {
        // 4:4:4 subsampling: Constant chroma AC delta_q increase (i.e. improved
        // luma quality relative to chroma) with gradual ramp-down for very low
        // qindexes.
        // Raising chroma AC delta_q by 24 was found to improve SSIMULACRA 2
        // BD-Rate by 2.5-3% on Daala's subset1, as well as providing a more
        // balanced bit allocation between the (relatively-starved) luma and
        // chroma channels.
        // Raising chroma DC delta_q appears to be harmful, both for SSIMULACRA
        // 2 scores and subjective quality (harshens blocking artifacts).
        // The ramp-down of chroma decrease was put into place so (lossy) QP 0
        // encodes still score within 0.1 SSIMULACRA 2 points of the equivalent
        // with no chroma delta_q (with a small efficiency improvement), while
        // encodes in the SSIMULACRA 2 <=90 range yield full benefits from this
        // adjustment.
        chroma_dc_delta_q = 0;
        chroma_ac_delta_q = clamp((quant_params->base_qindex / 2), 0, 24);
      }

      // TODO: bug https://crbug.com/aomedia/375221136 - find chroma_delta_q
      // values for 4:2:2 subsampling mode.
      quant_params->u_dc_delta_q = chroma_dc_delta_q;
      quant_params->u_ac_delta_q = chroma_ac_delta_q;
      quant_params->v_dc_delta_q = chroma_dc_delta_q;
      quant_params->v_ac_delta_q = chroma_ac_delta_q;
    } else {
      // TODO(aomedia:2717): need to design better delta
      quant_params->u_dc_delta_q = 2;
      quant_params->u_ac_delta_q = 2;
      quant_params->v_dc_delta_q = 2;
      quant_params->v_ac_delta_q = 2;
    }
  } else {
    quant_params->u_dc_delta_q = 0;
    quant_params->u_ac_delta_q = 0;
    quant_params->v_dc_delta_q = 0;
    quant_params->v_ac_delta_q = 0;
  }

  // following section 8.3.2 in T-REC-H.Sup15 document
  // to apply to AV1 qindex in the range of [0, 255]
  if (enable_hdr_deltaq) {
    int dqpCb = adjust_hdr_cb_deltaq(quant_params->base_qindex);
    int dqpCr = adjust_hdr_cr_deltaq(quant_params->base_qindex);
    quant_params->u_dc_delta_q = quant_params->u_ac_delta_q = dqpCb;
    quant_params->v_dc_delta_q = quant_params->v_ac_delta_q = dqpCr;
    if (dqpCb != dqpCr) {
      cm->seq_params->separate_uv_delta_q = 1;
    }
  }

  // Select the best luma and chroma QM formulas based on encoding mode and
  // tuning
  int (*get_luma_qmlevel)(int, int, int);
  int (*get_chroma_qmlevel)(int, int, int);

  if (is_allintra) {
    if (tuning == AOM_TUNE_IQ || tuning == AOM_TUNE_SSIMULACRA2) {
      if (tuning == AOM_TUNE_SSIMULACRA2) {
        // Use luma QM formula specifically tailored for tune SSIMULACRA2
        get_luma_qmlevel = aom_get_qmlevel_luma_ssimulacra2;
      } else {
        get_luma_qmlevel = aom_get_qmlevel_allintra;
      }

      if (cm->seq_params->subsampling_x == 0 &&
          cm->seq_params->subsampling_y == 0) {
        // 4:4:4 subsampling mode has 4x the number of chroma coefficients
        // compared to 4:2:0 (2x on each dimension). This means the encoder
        // should use lower chroma QM levels that more closely match the scaling
        // of an equivalent 4:2:0 chroma QM.
        get_chroma_qmlevel = aom_get_qmlevel_444_chroma;
      } else {
        // For all other chroma subsampling modes, use the all intra QM formula
        get_chroma_qmlevel = aom_get_qmlevel_allintra;
      }
    } else {
      get_luma_qmlevel = aom_get_qmlevel_allintra;
      get_chroma_qmlevel = aom_get_qmlevel_allintra;
    }
  } else {
    get_luma_qmlevel = aom_get_qmlevel;
    get_chroma_qmlevel = aom_get_qmlevel;
  }

  quant_params->qmatrix_level_y =
      get_luma_qmlevel(quant_params->base_qindex, min_qmlevel, max_qmlevel);
  quant_params->qmatrix_level_u =
      get_chroma_qmlevel(quant_params->base_qindex + quant_params->u_ac_delta_q,
                         min_qmlevel, max_qmlevel);

  if (cm->seq_params->separate_uv_delta_q) {
    quant_params->qmatrix_level_v = get_chroma_qmlevel(
        quant_params->base_qindex + quant_params->v_ac_delta_q, min_qmlevel,
        max_qmlevel);
  } else {
    quant_params->qmatrix_level_v = quant_params->qmatrix_level_u;
  }
}

// Table that converts 0-63 Q-range values passed in outside to the Qindex
// range used internally.
static const int quantizer_to_qindex[] = {
  0,   4,   8,   12,  16,  20,  24,  28,  32,  36,  40,  44,  48,
  52,  56,  60,  64,  68,  72,  76,  80,  84,  88,  92,  96,  100,
  104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 148, 152,
  156, 160, 164, 168, 172, 176, 180, 184, 188, 192, 196, 200, 204,
  208, 212, 216, 220, 224, 228, 232, 236, 240, 244, 249, 255,
};

int av1_quantizer_to_qindex(int quantizer) {
  return quantizer_to_qindex[quantizer];
}

int av1_qindex_to_quantizer(int qindex) {
  int quantizer;

  for (quantizer = 0; quantizer < 64; ++quantizer)
    if (quantizer_to_qindex[quantizer] >= qindex) return quantizer;

  return 63;
}
