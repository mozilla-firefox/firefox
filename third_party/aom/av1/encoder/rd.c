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

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

#include "aom_dsp/aom_dsp_common.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/bitops.h"
#include "aom_ports/mem.h"
#include "aom_ports/aom_once.h"

#include "av1/common/common.h"
#include "av1/common/entropy.h"
#include "av1/common/entropymode.h"
#include "av1/common/pred_common.h"
#include "av1/common/quant_common.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"
#include "av1/common/seg_common.h"

#include "av1/encoder/cost.h"
#include "av1/encoder/encodemv.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/nonrd_opt.h"
#include "av1/encoder/ratectrl.h"
#include "av1/encoder/rd.h"
#include "config/aom_config.h"

#define RD_THRESH_POW 1.25

// The baseline rd thresholds for breaking out of the rd loop for
// certain modes are assumed to be based on 8x8 blocks.
// This table is used to correct for block size.
// The factors here are << 2 (2 = x0.5, 32 = x8 etc).
static const uint8_t rd_thresh_block_size_factor[BLOCK_SIZES_ALL] = {
  2, 3, 3, 4, 6, 6, 8, 12, 12, 16, 24, 24, 32, 48, 48, 64, 4, 4, 8, 8, 16, 16
};

static const int use_intra_ext_tx_for_txsize[EXT_TX_SETS_INTRA]
                                            [EXT_TX_SIZES] = {
                                              { 1, 1, 1, 1 },  // unused
                                              { 1, 1, 0, 0 },
                                              { 0, 0, 1, 0 },
                                            };

static const int use_inter_ext_tx_for_txsize[EXT_TX_SETS_INTER]
                                            [EXT_TX_SIZES] = {
                                              { 1, 1, 1, 1 },  // unused
                                              { 1, 1, 0, 0 },
                                              { 0, 0, 1, 0 },
                                              { 0, 1, 1, 1 },
                                            };

static const int av1_ext_tx_set_idx_to_type[2][AOMMAX(EXT_TX_SETS_INTRA,
                                                      EXT_TX_SETS_INTER)] = {
  {
      // Intra
      EXT_TX_SET_DCTONLY,
      EXT_TX_SET_DTT4_IDTX_1DDCT,
      EXT_TX_SET_DTT4_IDTX,
  },
  {
      // Inter
      EXT_TX_SET_DCTONLY,
      EXT_TX_SET_ALL16,
      EXT_TX_SET_DTT9_IDTX_1DDCT,
      EXT_TX_SET_DCT_IDTX,
  },
};

void av1_fill_mode_rates(AV1_COMMON *const cm, ModeCosts *mode_costs,
                         FRAME_CONTEXT *fc) {
  int i, j;

  for (i = 0; i < PARTITION_CONTEXTS; ++i)
    av1_cost_tokens_from_cdf(mode_costs->partition_cost[i],
                             fc->partition_cdf[i], NULL);

  if (cm->current_frame.skip_mode_info.skip_mode_flag) {
    for (i = 0; i < SKIP_MODE_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->skip_mode_cost[i],
                               fc->skip_mode_cdfs[i], NULL);
    }
  }

  for (i = 0; i < SKIP_CONTEXTS; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->skip_txfm_cost[i],
                             fc->skip_txfm_cdfs[i], NULL);
  }

  for (i = 0; i < KF_MODE_CONTEXTS; ++i)
    for (j = 0; j < KF_MODE_CONTEXTS; ++j)
      av1_cost_tokens_from_cdf(mode_costs->y_mode_costs[i][j],
                               fc->kf_y_cdf[i][j], NULL);

  for (i = 0; i < BLOCK_SIZE_GROUPS; ++i)
    av1_cost_tokens_from_cdf(mode_costs->mbmode_cost[i], fc->y_mode_cdf[i],
                             NULL);
  for (i = 0; i < CFL_ALLOWED_TYPES; ++i)
    for (j = 0; j < INTRA_MODES; ++j)
      av1_cost_tokens_from_cdf(mode_costs->intra_uv_mode_cost[i][j],
                               fc->uv_mode_cdf[i][j], NULL);

  av1_cost_tokens_from_cdf(mode_costs->filter_intra_mode_cost,
                           fc->filter_intra_mode_cdf, NULL);
  for (i = 0; i < BLOCK_SIZES_ALL; ++i) {
    if (av1_filter_intra_allowed_bsize(cm, i))
      av1_cost_tokens_from_cdf(mode_costs->filter_intra_cost[i],
                               fc->filter_intra_cdfs[i], NULL);
  }

  for (i = 0; i < SWITCHABLE_FILTER_CONTEXTS; ++i)
    av1_cost_tokens_from_cdf(mode_costs->switchable_interp_costs[i],
                             fc->switchable_interp_cdf[i], NULL);

  for (i = 0; i < PALATTE_BSIZE_CTXS; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->palette_y_size_cost[i],
                             fc->palette_y_size_cdf[i], NULL);
    av1_cost_tokens_from_cdf(mode_costs->palette_uv_size_cost[i],
                             fc->palette_uv_size_cdf[i], NULL);
    for (j = 0; j < PALETTE_Y_MODE_CONTEXTS; ++j) {
      av1_cost_tokens_from_cdf(mode_costs->palette_y_mode_cost[i][j],
                               fc->palette_y_mode_cdf[i][j], NULL);
    }
  }

  for (i = 0; i < PALETTE_UV_MODE_CONTEXTS; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->palette_uv_mode_cost[i],
                             fc->palette_uv_mode_cdf[i], NULL);
  }

  for (i = 0; i < PALETTE_SIZES; ++i) {
    for (j = 0; j < PALETTE_COLOR_INDEX_CONTEXTS; ++j) {
      av1_cost_tokens_from_cdf(mode_costs->palette_y_color_cost[i][j],
                               fc->palette_y_color_index_cdf[i][j], NULL);
      av1_cost_tokens_from_cdf(mode_costs->palette_uv_color_cost[i][j],
                               fc->palette_uv_color_index_cdf[i][j], NULL);
    }
  }

  int sign_cost[CFL_JOINT_SIGNS];
  av1_cost_tokens_from_cdf(sign_cost, fc->cfl_sign_cdf, NULL);
  for (int joint_sign = 0; joint_sign < CFL_JOINT_SIGNS; joint_sign++) {
    int *cost_u = mode_costs->cfl_cost[joint_sign][CFL_PRED_U];
    int *cost_v = mode_costs->cfl_cost[joint_sign][CFL_PRED_V];
    if (CFL_SIGN_U(joint_sign) == CFL_SIGN_ZERO) {
      memset(cost_u, 0, CFL_ALPHABET_SIZE * sizeof(*cost_u));
    } else {
      const aom_cdf_prob *cdf_u = fc->cfl_alpha_cdf[CFL_CONTEXT_U(joint_sign)];
      av1_cost_tokens_from_cdf(cost_u, cdf_u, NULL);
    }
    if (CFL_SIGN_V(joint_sign) == CFL_SIGN_ZERO) {
      memset(cost_v, 0, CFL_ALPHABET_SIZE * sizeof(*cost_v));
    } else {
      const aom_cdf_prob *cdf_v = fc->cfl_alpha_cdf[CFL_CONTEXT_V(joint_sign)];
      av1_cost_tokens_from_cdf(cost_v, cdf_v, NULL);
    }
    for (int u = 0; u < CFL_ALPHABET_SIZE; u++)
      cost_u[u] += sign_cost[joint_sign];
  }

  for (i = 0; i < MAX_TX_CATS; ++i)
    for (j = 0; j < TX_SIZE_CONTEXTS; ++j)
      av1_cost_tokens_from_cdf(mode_costs->tx_size_cost[i][j],
                               fc->tx_size_cdf[i][j], NULL);

  for (i = 0; i < TXFM_PARTITION_CONTEXTS; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->txfm_partition_cost[i],
                             fc->txfm_partition_cdf[i], NULL);
  }

  for (i = TX_4X4; i < EXT_TX_SIZES; ++i) {
    int s;
    for (s = 1; s < EXT_TX_SETS_INTER; ++s) {
      if (use_inter_ext_tx_for_txsize[s][i]) {
        av1_cost_tokens_from_cdf(
            mode_costs->inter_tx_type_costs[s][i], fc->inter_ext_tx_cdf[s][i],
            av1_ext_tx_inv[av1_ext_tx_set_idx_to_type[1][s]]);
      }
    }
    for (s = 1; s < EXT_TX_SETS_INTRA; ++s) {
      if (use_intra_ext_tx_for_txsize[s][i]) {
        for (j = 0; j < INTRA_MODES; ++j) {
          av1_cost_tokens_from_cdf(
              mode_costs->intra_tx_type_costs[s][i][j],
              fc->intra_ext_tx_cdf[s][i][j],
              av1_ext_tx_inv[av1_ext_tx_set_idx_to_type[0][s]]);
        }
      }
    }
  }
  for (i = 0; i < DIRECTIONAL_MODES; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->angle_delta_cost[i],
                             fc->angle_delta_cdf[i], NULL);
  }
  av1_cost_tokens_from_cdf(mode_costs->intrabc_cost, fc->intrabc_cdf, NULL);

  for (i = 0; i < SPATIAL_PREDICTION_PROBS; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->spatial_pred_cost[i],
                             fc->seg.spatial_pred_seg_cdf[i], NULL);
  }

  for (i = 0; i < SEG_TEMPORAL_PRED_CTXS; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->tmp_pred_cost[i], fc->seg.pred_cdf[i],
                             NULL);
  }

  if (!frame_is_intra_only(cm)) {
    for (i = 0; i < COMP_INTER_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->comp_inter_cost[i],
                               fc->comp_inter_cdf[i], NULL);
    }

    for (i = 0; i < REF_CONTEXTS; ++i) {
      for (j = 0; j < SINGLE_REFS - 1; ++j) {
        av1_cost_tokens_from_cdf(mode_costs->single_ref_cost[i][j],
                                 fc->single_ref_cdf[i][j], NULL);
      }
    }

    for (i = 0; i < COMP_REF_TYPE_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->comp_ref_type_cost[i],
                               fc->comp_ref_type_cdf[i], NULL);
    }

    for (i = 0; i < UNI_COMP_REF_CONTEXTS; ++i) {
      for (j = 0; j < UNIDIR_COMP_REFS - 1; ++j) {
        av1_cost_tokens_from_cdf(mode_costs->uni_comp_ref_cost[i][j],
                                 fc->uni_comp_ref_cdf[i][j], NULL);
      }
    }

    for (i = 0; i < REF_CONTEXTS; ++i) {
      for (j = 0; j < FWD_REFS - 1; ++j) {
        av1_cost_tokens_from_cdf(mode_costs->comp_ref_cost[i][j],
                                 fc->comp_ref_cdf[i][j], NULL);
      }
    }

    for (i = 0; i < REF_CONTEXTS; ++i) {
      for (j = 0; j < BWD_REFS - 1; ++j) {
        av1_cost_tokens_from_cdf(mode_costs->comp_bwdref_cost[i][j],
                                 fc->comp_bwdref_cdf[i][j], NULL);
      }
    }

    for (i = 0; i < INTRA_INTER_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->intra_inter_cost[i],
                               fc->intra_inter_cdf[i], NULL);
    }

    for (i = 0; i < NEWMV_MODE_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->newmv_mode_cost[i], fc->newmv_cdf[i],
                               NULL);
    }

    for (i = 0; i < GLOBALMV_MODE_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->zeromv_mode_cost[i],
                               fc->zeromv_cdf[i], NULL);
    }

    for (i = 0; i < REFMV_MODE_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->refmv_mode_cost[i], fc->refmv_cdf[i],
                               NULL);
    }

    for (i = 0; i < DRL_MODE_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->drl_mode_cost0[i], fc->drl_cdf[i],
                               NULL);
    }
    for (i = 0; i < INTER_MODE_CONTEXTS; ++i)
      av1_cost_tokens_from_cdf(mode_costs->inter_compound_mode_cost[i],
                               fc->inter_compound_mode_cdf[i], NULL);
    for (i = 0; i < BLOCK_SIZES_ALL; ++i)
      av1_cost_tokens_from_cdf(mode_costs->compound_type_cost[i],
                               fc->compound_type_cdf[i], NULL);
    for (i = 0; i < BLOCK_SIZES_ALL; ++i) {
      if (av1_is_wedge_used(i)) {
        av1_cost_tokens_from_cdf(mode_costs->wedge_idx_cost[i],
                                 fc->wedge_idx_cdf[i], NULL);
      }
    }
    for (i = 0; i < BLOCK_SIZE_GROUPS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->interintra_cost[i],
                               fc->interintra_cdf[i], NULL);
      av1_cost_tokens_from_cdf(mode_costs->interintra_mode_cost[i],
                               fc->interintra_mode_cdf[i], NULL);
    }
    for (i = 0; i < BLOCK_SIZES_ALL; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->wedge_interintra_cost[i],
                               fc->wedge_interintra_cdf[i], NULL);
    }
    for (i = BLOCK_8X8; i < BLOCK_SIZES_ALL; i++) {
      av1_cost_tokens_from_cdf(mode_costs->motion_mode_cost[i],
                               fc->motion_mode_cdf[i], NULL);
    }
    for (i = BLOCK_8X8; i < BLOCK_SIZES_ALL; i++) {
      av1_cost_tokens_from_cdf(mode_costs->motion_mode_cost1[i],
                               fc->obmc_cdf[i], NULL);
    }
    for (i = 0; i < COMP_INDEX_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->comp_idx_cost[i],
                               fc->compound_index_cdf[i], NULL);
    }
    for (i = 0; i < COMP_GROUP_IDX_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->comp_group_idx_cost[i],
                               fc->comp_group_idx_cdf[i], NULL);
    }
  }
}

#if !CONFIG_REALTIME_ONLY
void av1_fill_lr_rates(ModeCosts *mode_costs, FRAME_CONTEXT *fc) {
  av1_cost_tokens_from_cdf(mode_costs->switchable_restore_cost,
                           fc->switchable_restore_cdf, NULL);
  av1_cost_tokens_from_cdf(mode_costs->wiener_restore_cost,
                           fc->wiener_restore_cdf, NULL);
  av1_cost_tokens_from_cdf(mode_costs->sgrproj_restore_cost,
                           fc->sgrproj_restore_cdf, NULL);
}
#endif  // !CONFIG_REALTIME_ONLY

// Values are now correlated to quantizer.
static int sad_per_bit_lut_8[QINDEX_RANGE];
static int sad_per_bit_lut_10[QINDEX_RANGE];
static int sad_per_bit_lut_12[QINDEX_RANGE];

static void init_me_luts_bd(int *bit16lut, int range,
                            aom_bit_depth_t bit_depth) {
  int i;
  // Initialize the sad lut tables using a formulaic calculation for now.
  // This is to make it easier to resolve the impact of experimental changes
  // to the quantizer tables.
  for (i = 0; i < range; i++) {
    const double q = av1_convert_qindex_to_q(i, bit_depth);
    bit16lut[i] = (int)(0.0418 * q + 2.4107);
  }
}

static void init_me_luts(void) {
  init_me_luts_bd(sad_per_bit_lut_8, QINDEX_RANGE, AOM_BITS_8);
  init_me_luts_bd(sad_per_bit_lut_10, QINDEX_RANGE, AOM_BITS_10);
  init_me_luts_bd(sad_per_bit_lut_12, QINDEX_RANGE, AOM_BITS_12);
}

void av1_init_me_luts(void) { aom_once(init_me_luts); }

static const int rd_boost_factor[16] = { 64, 32, 32, 32, 24, 16, 12, 12,
                                         8,  8,  4,  4,  2,  2,  1,  0 };

static const int rd_layer_depth_factor[7] = {
  160, 160, 160, 160, 192, 208, 224
};

// Returns the default rd multiplier for inter frames for a given qindex.
// The function here is a first pass estimate based on data from
// a previous Vizer run
static double def_inter_rd_multiplier(int qindex) {
  return 3.2 + (0.0015 * (double)qindex);
}

// Returns the default rd multiplier for ARF/Golden Frames for a given qindex.
// The function here is a first pass estimate based on data from
// a previous Vizer run
static double def_arf_rd_multiplier(int qindex) {
  return 3.25 + (0.0015 * (double)qindex);
}

// Returns the default rd multiplier for key frames for a given qindex.
// The function here is a first pass estimate based on data from
// a previous Vizer run
static double def_kf_rd_multiplier(int qindex) {
  return 3.3 + (0.0015 * (double)qindex);
}

int av1_compute_rd_mult_based_on_qindex(aom_bit_depth_t bit_depth,
                                        FRAME_UPDATE_TYPE update_type,
                                        int qindex, aom_tune_metric tuning) {
  const int q = av1_dc_quant_QTX(qindex, 0, bit_depth);
  int64_t rdmult = q * q;
  if (update_type == KF_UPDATE) {
    double def_rd_q_mult = def_kf_rd_multiplier(q);
    rdmult = (int64_t)((double)rdmult * def_rd_q_mult);
  } else if ((update_type == GF_UPDATE) || (update_type == ARF_UPDATE)) {
    double def_rd_q_mult = def_arf_rd_multiplier(q);
    rdmult = (int64_t)((double)rdmult * def_rd_q_mult);
  } else {
    double def_rd_q_mult = def_inter_rd_multiplier(q);
    rdmult = (int64_t)((double)rdmult * def_rd_q_mult);
  }

  if (tuning == AOM_TUNE_IQ || tuning == AOM_TUNE_SSIMULACRA2) {
    // Further multiply rdmult (by up to 200/128 = 1.5625) to improve image
    // quality. The most noticeable effect is a mild bias towards choosing
    // larger transform sizes (e.g. one 16x16 transform instead of 4 8x8
    // transforms).
    // For very high qindexes, start progressively reducing the weight towards
    // unity (128/128), as transforms are large enough and making them even
    // larger actually harms subjective quality and SSIMULACRA 2 scores.
    // This weight part of the equation was determined by iteratively increasing
    // weight on CID22 and Daala's subset1, and observing its effects on visual
    // quality and SSIMULACRA 2 scores along the usable (0-100) range.
    // The ramp-down part of the equation was determined by choosing a fixed
    // initial qindex point [qindex 159 = (255 - 159) * 3 / 4] where SSIMULACRA
    // 2 scores for encodes with qindexes greater than 159 scored at or above
    // their equivalents with no rdmult adjustment.
    const int weight = clamp(((255 - qindex) * 3) / 4, 0, 72) + 128;
    rdmult = (int64_t)((double)rdmult * weight / 128.0);
  }

  switch (bit_depth) {
    case AOM_BITS_8: break;
    case AOM_BITS_10: rdmult = ROUND_POWER_OF_TWO(rdmult, 4); break;
    case AOM_BITS_12: rdmult = ROUND_POWER_OF_TWO(rdmult, 8); break;
    default:
      assert(0 && "bit_depth should be AOM_BITS_8, AOM_BITS_10 or AOM_BITS_12");
      return -1;
  }
  return rdmult > 0 ? (int)AOMMIN(rdmult, INT_MAX) : 1;
}

int av1_compute_rd_mult(const int qindex, const aom_bit_depth_t bit_depth,
                        const FRAME_UPDATE_TYPE update_type,
                        const int layer_depth, const int boost_index,
                        const FRAME_TYPE frame_type,
                        const int use_fixed_qp_offsets,
                        const int is_stat_consumption_stage,
                        const aom_tune_metric tuning) {
  int64_t rdmult = av1_compute_rd_mult_based_on_qindex(bit_depth, update_type,
                                                       qindex, tuning);
  if (is_stat_consumption_stage && !use_fixed_qp_offsets &&
      (frame_type != KEY_FRAME)) {
    // Layer depth adjustment
    rdmult = (rdmult * rd_layer_depth_factor[layer_depth]) >> 7;
    // ARF boost adjustment
    rdmult += ((rdmult * rd_boost_factor[boost_index]) >> 7);
  }
  return rdmult > 0 ? (int)AOMMIN(rdmult, INT_MAX) : 1;
}

int av1_get_deltaq_offset(aom_bit_depth_t bit_depth, int qindex, double beta) {
  assert(beta > 0.0);
  int q = av1_dc_quant_QTX(qindex, 0, bit_depth);
  int newq = (int)rint(q / sqrt(beta));
  int orig_qindex = qindex;
  if (newq == q) {
    return 0;
  }
  if (newq < q) {
    while (qindex > 0) {
      qindex--;
      q = av1_dc_quant_QTX(qindex, 0, bit_depth);
      if (newq >= q) {
        break;
      }
    }
  } else {
    while (qindex < MAXQ) {
      qindex++;
      q = av1_dc_quant_QTX(qindex, 0, bit_depth);
      if (newq <= q) {
        break;
      }
    }
  }
  return qindex - orig_qindex;
}

int av1_adjust_q_from_delta_q_res(int delta_q_res, int prev_qindex,
                                  int curr_qindex) {
  curr_qindex = clamp(curr_qindex, delta_q_res, 256 - delta_q_res);
  const int sign_deltaq_index = curr_qindex - prev_qindex >= 0 ? 1 : -1;
  const int deltaq_deadzone = delta_q_res / 4;
  const int qmask = ~(delta_q_res - 1);
  int abs_deltaq_index = abs(curr_qindex - prev_qindex);
  abs_deltaq_index = (abs_deltaq_index + deltaq_deadzone) & qmask;
  int adjust_qindex = prev_qindex + sign_deltaq_index * abs_deltaq_index;
  adjust_qindex = AOMMAX(adjust_qindex, MINQ + 1);
  return adjust_qindex;
}

#if !CONFIG_REALTIME_ONLY
int av1_get_adaptive_rdmult(const AV1_COMP *cpi, double beta) {
  assert(beta > 0.0);
  const AV1_COMMON *cm = &cpi->common;

  const GF_GROUP *const gf_group = &cpi->ppi->gf_group;
  const int boost_index = AOMMIN(15, (cpi->ppi->p_rc.gfu_boost / 100));
  const int layer_depth = AOMMIN(gf_group->layer_depth[cpi->gf_frame_index], 6);
  const FRAME_TYPE frame_type = cm->current_frame.frame_type;

  const int qindex_rdmult = cm->quant_params.base_qindex;
  return (int)(av1_compute_rd_mult(
                   qindex_rdmult, cm->seq_params->bit_depth,
                   cpi->ppi->gf_group.update_type[cpi->gf_frame_index],
                   layer_depth, boost_index, frame_type,
                   cpi->oxcf.q_cfg.use_fixed_qp_offsets,
                   is_stat_consumption_stage(cpi), cpi->oxcf.tune_cfg.tuning) /
               beta);
}
#endif  // !CONFIG_REALTIME_ONLY

static int compute_rd_thresh_factor(int qindex, aom_bit_depth_t bit_depth) {
  double q;
  switch (bit_depth) {
    case AOM_BITS_8: q = av1_dc_quant_QTX(qindex, 0, AOM_BITS_8) / 4.0; break;
    case AOM_BITS_10:
      q = av1_dc_quant_QTX(qindex, 0, AOM_BITS_10) / 16.0;
      break;
    case AOM_BITS_12:
      q = av1_dc_quant_QTX(qindex, 0, AOM_BITS_12) / 64.0;
      break;
    default:
      assert(0 && "bit_depth should be AOM_BITS_8, AOM_BITS_10 or AOM_BITS_12");
      return -1;
  }
  // TODO(debargha): Adjust the function below.
  return AOMMAX((int)(pow(q, RD_THRESH_POW) * 5.12), 8);
}

void av1_set_sad_per_bit(const AV1_COMP *cpi, int *sadperbit, int qindex) {
  switch (cpi->common.seq_params->bit_depth) {
    case AOM_BITS_8: *sadperbit = sad_per_bit_lut_8[qindex]; break;
    case AOM_BITS_10: *sadperbit = sad_per_bit_lut_10[qindex]; break;
    case AOM_BITS_12: *sadperbit = sad_per_bit_lut_12[qindex]; break;
    default:
      assert(0 && "bit_depth should be AOM_BITS_8, AOM_BITS_10 or AOM_BITS_12");
  }
}

static void set_block_thresholds(const AV1_COMMON *cm, RD_OPT *rd,
                                 int use_nonrd_pick_mode) {
  int i, bsize, segment_id;
  THR_MODES mode_indices[RTC_REFS * RTC_MODES] = { 0 };
  int num_modes_count = use_nonrd_pick_mode ? 0 : MAX_MODES;

  if (use_nonrd_pick_mode) {
    for (int r_idx = 0; r_idx < RTC_REFS; r_idx++) {
      const MV_REFERENCE_FRAME ref = real_time_ref_combos[r_idx][0];
      if (ref != INTRA_FRAME) {
        for (i = 0; i < RTC_INTER_MODES; i++)
          mode_indices[num_modes_count++] =
              mode_idx[ref][mode_offset(inter_mode_list[i])];
      } else {
        for (i = 0; i < RTC_INTRA_MODES; i++)
          mode_indices[num_modes_count++] =
              mode_idx[ref][mode_offset(intra_mode_list[i])];
      }
    }
  }

  for (segment_id = 0; segment_id < MAX_SEGMENTS; ++segment_id) {
    const int qindex = clamp(
        av1_get_qindex(&cm->seg, segment_id, cm->quant_params.base_qindex) +
            cm->quant_params.y_dc_delta_q,
        0, MAXQ);
    const int q = compute_rd_thresh_factor(qindex, cm->seq_params->bit_depth);

    for (bsize = 0; bsize < BLOCK_SIZES_ALL; ++bsize) {
      // Threshold here seems unnecessarily harsh but fine given actual
      // range of values used for cpi->sf.thresh_mult[].
      const int t = q * rd_thresh_block_size_factor[bsize];
      const int thresh_max = INT_MAX / t;

      for (i = 0; i < num_modes_count; ++i) {
        const int mode_index = use_nonrd_pick_mode ? mode_indices[i] : i;
        rd->threshes[segment_id][bsize][mode_index] =
            rd->thresh_mult[mode_index] < thresh_max
                ? rd->thresh_mult[mode_index] * t / 4
                : INT_MAX;
      }
    }
  }
}

void av1_fill_coeff_costs(CoeffCosts *coeff_costs, FRAME_CONTEXT *fc,
                          const int num_planes) {
  const int nplanes = AOMMIN(num_planes, PLANE_TYPES);
  for (int eob_multi_size = 0; eob_multi_size < 7; ++eob_multi_size) {
    for (int plane = 0; plane < nplanes; ++plane) {
      LV_MAP_EOB_COST *pcost = &coeff_costs->eob_costs[eob_multi_size][plane];

      for (int ctx = 0; ctx < 2; ++ctx) {
        aom_cdf_prob *pcdf;
        switch (eob_multi_size) {
          case 0: pcdf = fc->eob_flag_cdf16[plane][ctx]; break;
          case 1: pcdf = fc->eob_flag_cdf32[plane][ctx]; break;
          case 2: pcdf = fc->eob_flag_cdf64[plane][ctx]; break;
          case 3: pcdf = fc->eob_flag_cdf128[plane][ctx]; break;
          case 4: pcdf = fc->eob_flag_cdf256[plane][ctx]; break;
          case 5: pcdf = fc->eob_flag_cdf512[plane][ctx]; break;
          case 6:
          default: pcdf = fc->eob_flag_cdf1024[plane][ctx]; break;
        }
        av1_cost_tokens_from_cdf(pcost->eob_cost[ctx], pcdf, NULL);
      }
    }
  }
  for (int tx_size = 0; tx_size < TX_SIZES; ++tx_size) {
    for (int plane = 0; plane < nplanes; ++plane) {
      LV_MAP_COEFF_COST *pcost = &coeff_costs->coeff_costs[tx_size][plane];

      for (int ctx = 0; ctx < TXB_SKIP_CONTEXTS; ++ctx)
        av1_cost_tokens_from_cdf(pcost->txb_skip_cost[ctx],
                                 fc->txb_skip_cdf[tx_size][ctx], NULL);

      for (int ctx = 0; ctx < SIG_COEF_CONTEXTS_EOB; ++ctx)
        av1_cost_tokens_from_cdf(pcost->base_eob_cost[ctx],
                                 fc->coeff_base_eob_cdf[tx_size][plane][ctx],
                                 NULL);
      for (int ctx = 0; ctx < SIG_COEF_CONTEXTS; ++ctx)
        av1_cost_tokens_from_cdf(pcost->base_cost[ctx],
                                 fc->coeff_base_cdf[tx_size][plane][ctx], NULL);

      for (int ctx = 0; ctx < SIG_COEF_CONTEXTS; ++ctx) {
        pcost->base_cost[ctx][4] = 0;
        pcost->base_cost[ctx][5] = pcost->base_cost[ctx][1] +
                                   av1_cost_literal(1) -
                                   pcost->base_cost[ctx][0];
        pcost->base_cost[ctx][6] =
            pcost->base_cost[ctx][2] - pcost->base_cost[ctx][1];
        pcost->base_cost[ctx][7] =
            pcost->base_cost[ctx][3] - pcost->base_cost[ctx][2];
      }

      for (int ctx = 0; ctx < EOB_COEF_CONTEXTS; ++ctx)
        av1_cost_tokens_from_cdf(pcost->eob_extra_cost[ctx],
                                 fc->eob_extra_cdf[tx_size][plane][ctx], NULL);

      for (int ctx = 0; ctx < DC_SIGN_CONTEXTS; ++ctx)
        av1_cost_tokens_from_cdf(pcost->dc_sign_cost[ctx],
                                 fc->dc_sign_cdf[plane][ctx], NULL);

      for (int ctx = 0; ctx < LEVEL_CONTEXTS; ++ctx) {
        int br_rate[BR_CDF_SIZE];
        int prev_cost = 0;
        int i, j;
        av1_cost_tokens_from_cdf(
            br_rate, fc->coeff_br_cdf[AOMMIN(tx_size, TX_32X32)][plane][ctx],
            NULL);
        // printf("br_rate: ");
        // for(j = 0; j < BR_CDF_SIZE; j++)
        //  printf("%4d ", br_rate[j]);
        // printf("\n");
        for (i = 0; i < COEFF_BASE_RANGE; i += BR_CDF_SIZE - 1) {
          for (j = 0; j < BR_CDF_SIZE - 1; j++) {
            pcost->lps_cost[ctx][i + j] = prev_cost + br_rate[j];
          }
          prev_cost += br_rate[j];
        }
        pcost->lps_cost[ctx][i] = prev_cost;
        // printf("lps_cost: %d %d %2d : ", tx_size, plane, ctx);
        // for (i = 0; i <= COEFF_BASE_RANGE; i++)
        //  printf("%5d ", pcost->lps_cost[ctx][i]);
        // printf("\n");
      }
      for (int ctx = 0; ctx < LEVEL_CONTEXTS; ++ctx) {
        pcost->lps_cost[ctx][0 + COEFF_BASE_RANGE + 1] =
            pcost->lps_cost[ctx][0];
        for (int i = 1; i <= COEFF_BASE_RANGE; ++i) {
          pcost->lps_cost[ctx][i + COEFF_BASE_RANGE + 1] =
              pcost->lps_cost[ctx][i] - pcost->lps_cost[ctx][i - 1];
        }
      }
    }
  }
}

void av1_fill_mv_costs(const nmv_context *nmvc, int integer_mv, int usehp,
                       MvCosts *mv_costs) {
  // Avoid accessing 'mv_costs' when it is not allocated.
  if (mv_costs == NULL) return;

  mv_costs->nmv_cost[0] = &mv_costs->nmv_cost_alloc[0][MV_MAX];
  mv_costs->nmv_cost[1] = &mv_costs->nmv_cost_alloc[1][MV_MAX];
  mv_costs->nmv_cost_hp[0] = &mv_costs->nmv_cost_hp_alloc[0][MV_MAX];
  mv_costs->nmv_cost_hp[1] = &mv_costs->nmv_cost_hp_alloc[1][MV_MAX];
  if (integer_mv) {
    mv_costs->mv_cost_stack = (int **)&mv_costs->nmv_cost;
    av1_build_nmv_cost_table(mv_costs->nmv_joint_cost, mv_costs->mv_cost_stack,
                             nmvc, MV_SUBPEL_NONE);
  } else {
    mv_costs->mv_cost_stack =
        usehp ? mv_costs->nmv_cost_hp : mv_costs->nmv_cost;
    av1_build_nmv_cost_table(mv_costs->nmv_joint_cost, mv_costs->mv_cost_stack,
                             nmvc, usehp);
  }
}

void av1_fill_dv_costs(const nmv_context *ndvc, IntraBCMVCosts *dv_costs) {
  dv_costs->dv_costs[0] = &dv_costs->dv_costs_alloc[0][MV_MAX];
  dv_costs->dv_costs[1] = &dv_costs->dv_costs_alloc[1][MV_MAX];
  av1_build_nmv_cost_table(dv_costs->joint_mv, dv_costs->dv_costs, ndvc,
                           MV_SUBPEL_NONE);
}

// Populates speed features based on codec control settings (of type
// COST_UPDATE_TYPE) and expected speed feature settings (of type
// INTERNAL_COST_UPDATE_TYPE) by considering the least frequent cost update.
// The populated/updated speed features are used for cost updates in the
// encoder.
// WARNING: Population of unified cost update frequency needs to be taken care
// accordingly, in case of any modifications/additions to the enum
// COST_UPDATE_TYPE/INTERNAL_COST_UPDATE_TYPE.
static inline void populate_unified_cost_update_freq(
    const CostUpdateFreq cost_upd_freq, SPEED_FEATURES *const sf) {
  INTER_MODE_SPEED_FEATURES *const inter_sf = &sf->inter_sf;
  // Mapping of entropy cost update frequency from the encoder's codec control
  // settings of type COST_UPDATE_TYPE to speed features of type
  // INTERNAL_COST_UPDATE_TYPE.
  static const INTERNAL_COST_UPDATE_TYPE
      map_cost_upd_to_internal_cost_upd[NUM_COST_UPDATE_TYPES] = {
        INTERNAL_COST_UPD_SB, INTERNAL_COST_UPD_SBROW, INTERNAL_COST_UPD_TILE,
        INTERNAL_COST_UPD_OFF
      };

  inter_sf->mv_cost_upd_level =
      AOMMIN(inter_sf->mv_cost_upd_level,
             map_cost_upd_to_internal_cost_upd[cost_upd_freq.mv]);
  inter_sf->coeff_cost_upd_level =
      AOMMIN(inter_sf->coeff_cost_upd_level,
             map_cost_upd_to_internal_cost_upd[cost_upd_freq.coeff]);
  inter_sf->mode_cost_upd_level =
      AOMMIN(inter_sf->mode_cost_upd_level,
             map_cost_upd_to_internal_cost_upd[cost_upd_freq.mode]);
  sf->intra_sf.dv_cost_upd_level =
      AOMMIN(sf->intra_sf.dv_cost_upd_level,
             map_cost_upd_to_internal_cost_upd[cost_upd_freq.dv]);
}

// Checks if entropy costs should be initialized/updated at frame level or not.
static inline int is_frame_level_cost_upd_freq_set(
    const AV1_COMMON *const cm, const INTERNAL_COST_UPDATE_TYPE cost_upd_level,
    const int use_nonrd_pick_mode, const int frames_since_key) {
  const int fill_costs =
      frame_is_intra_only(cm) ||
      (use_nonrd_pick_mode ? frames_since_key < 2
                           : (cm->current_frame.frame_number & 0x07) == 1);
  return ((!use_nonrd_pick_mode && cost_upd_level != INTERNAL_COST_UPD_OFF) ||
          cost_upd_level == INTERNAL_COST_UPD_TILE || fill_costs);
}

// Decide whether we want to update the mode entropy cost for the current frame.
// The logit is currently inherited from selective_disable_cdf_rtc.
static inline int should_force_mode_cost_update(const AV1_COMP *cpi) {
  const REAL_TIME_SPEED_FEATURES *const rt_sf = &cpi->sf.rt_sf;
  if (!rt_sf->frame_level_mode_cost_update) {
    return false;
  }

  if (cpi->oxcf.algo_cfg.cdf_update_mode == 2) {
    return cpi->frames_since_last_update == 1;
  } else if (cpi->oxcf.algo_cfg.cdf_update_mode == 1) {
    if (cpi->svc.number_spatial_layers == 1 &&
        cpi->svc.number_temporal_layers == 1) {
      const AV1_COMMON *const cm = &cpi->common;
      const RATE_CONTROL *const rc = &cpi->rc;

      return frame_is_intra_only(cm) || is_frame_resize_pending(cpi) ||
             rc->high_source_sad || rc->frames_since_key < 10 ||
             cpi->cyclic_refresh->counter_encode_maxq_scene_change < 10 ||
             cm->current_frame.frame_number % 8 == 0;
    } else if (cpi->svc.number_temporal_layers > 1) {
      return cpi->svc.temporal_layer_id != cpi->svc.number_temporal_layers - 1;
    }
  }

  return false;
}

void av1_initialize_rd_consts(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &cpi->td.mb;
  SPEED_FEATURES *const sf = &cpi->sf;
  RD_OPT *const rd = &cpi->rd;
  int use_nonrd_pick_mode = cpi->sf.rt_sf.use_nonrd_pick_mode;
  int frames_since_key = cpi->rc.frames_since_key;

  const GF_GROUP *const gf_group = &cpi->ppi->gf_group;
  const int boost_index = AOMMIN(15, (cpi->ppi->p_rc.gfu_boost / 100));
  const int layer_depth = AOMMIN(gf_group->layer_depth[cpi->gf_frame_index], 6);
  const FRAME_TYPE frame_type = cm->current_frame.frame_type;

  const int qindex_rdmult =
      cm->quant_params.base_qindex + cm->quant_params.y_dc_delta_q;
  rd->RDMULT = av1_compute_rd_mult(
      qindex_rdmult, cm->seq_params->bit_depth,
      cpi->ppi->gf_group.update_type[cpi->gf_frame_index], layer_depth,
      boost_index, frame_type, cpi->oxcf.q_cfg.use_fixed_qp_offsets,
      is_stat_consumption_stage(cpi), cpi->oxcf.tune_cfg.tuning);
#if CONFIG_RD_COMMAND
  if (cpi->oxcf.pass == 2) {
    const RD_COMMAND *rd_command = &cpi->rd_command;
    if (rd_command->option_ls[rd_command->frame_index] ==
        RD_OPTION_SET_Q_RDMULT) {
      rd->RDMULT = rd_command->rdmult_ls[rd_command->frame_index];
    }
  }
#endif  // CONFIG_RD_COMMAND

  av1_set_error_per_bit(&x->errorperbit, rd->RDMULT);

  set_block_thresholds(cm, rd, cpi->sf.rt_sf.use_nonrd_pick_mode);

  populate_unified_cost_update_freq(cpi->oxcf.cost_upd_freq, sf);
  const INTER_MODE_SPEED_FEATURES *const inter_sf = &cpi->sf.inter_sf;
  // Frame level mv cost update
  if (is_frame_level_cost_upd_freq_set(cm, inter_sf->mv_cost_upd_level,
                                       use_nonrd_pick_mode, frames_since_key))
    av1_fill_mv_costs(&cm->fc->nmvc, cm->features.cur_frame_force_integer_mv,
                      cm->features.allow_high_precision_mv, x->mv_costs);

  // Frame level coefficient cost update
  if (is_frame_level_cost_upd_freq_set(cm, inter_sf->coeff_cost_upd_level,
                                       use_nonrd_pick_mode, frames_since_key))
    av1_fill_coeff_costs(&x->coeff_costs, cm->fc, av1_num_planes(cm));

  // Frame level mode cost update
  if (should_force_mode_cost_update(cpi) ||
      is_frame_level_cost_upd_freq_set(cm, inter_sf->mode_cost_upd_level,
                                       use_nonrd_pick_mode, frames_since_key))
    av1_fill_mode_rates(cm, &x->mode_costs, cm->fc);

  // Frame level dv cost update
  if (av1_need_dv_costs(cpi)) {
    if (cpi->td.dv_costs_alloc == NULL) {
      CHECK_MEM_ERROR(
          cm, cpi->td.dv_costs_alloc,
          (IntraBCMVCosts *)aom_malloc(sizeof(*cpi->td.dv_costs_alloc)));
      cpi->td.mb.dv_costs = cpi->td.dv_costs_alloc;
    }
    av1_fill_dv_costs(&cm->fc->ndvc, x->dv_costs);
  }
}

static void model_rd_norm(int xsq_q10, int *r_q10, int *d_q10) {
  // NOTE: The tables below must be of the same size.

  // The functions described below are sampled at the four most significant
  // bits of x^2 + 8 / 256.

  // Normalized rate:
  // This table models the rate for a Laplacian source with given variance
  // when quantized with a uniform quantizer with given stepsize. The
  // closed form expression is:
  // Rn(x) = H(sqrt(r)) + sqrt(r)*[1 + H(r)/(1 - r)],
  // where r = exp(-sqrt(2) * x) and x = qpstep / sqrt(variance),
  // and H(x) is the binary entropy function.
  static const int rate_tab_q10[] = {
    65536, 6086, 5574, 5275, 5063, 4899, 4764, 4651, 4553, 4389, 4255, 4142,
    4044,  3958, 3881, 3811, 3748, 3635, 3538, 3453, 3376, 3307, 3244, 3186,
    3133,  3037, 2952, 2877, 2809, 2747, 2690, 2638, 2589, 2501, 2423, 2353,
    2290,  2232, 2179, 2130, 2084, 2001, 1928, 1862, 1802, 1748, 1698, 1651,
    1608,  1530, 1460, 1398, 1342, 1290, 1243, 1199, 1159, 1086, 1021, 963,
    911,   864,  821,  781,  745,  680,  623,  574,  530,  490,  455,  424,
    395,   345,  304,  269,  239,  213,  190,  171,  154,  126,  104,  87,
    73,    61,   52,   44,   38,   28,   21,   16,   12,   10,   8,    6,
    5,     3,    2,    1,    1,    1,    0,    0,
  };
  // Normalized distortion:
  // This table models the normalized distortion for a Laplacian source
  // with given variance when quantized with a uniform quantizer
  // with given stepsize. The closed form expression is:
  // Dn(x) = 1 - 1/sqrt(2) * x / sinh(x/sqrt(2))
  // where x = qpstep / sqrt(variance).
  // Note the actual distortion is Dn * variance.
  static const int dist_tab_q10[] = {
    0,    0,    1,    1,    1,    2,    2,    2,    3,    3,    4,    5,
    5,    6,    7,    7,    8,    9,    11,   12,   13,   15,   16,   17,
    18,   21,   24,   26,   29,   31,   34,   36,   39,   44,   49,   54,
    59,   64,   69,   73,   78,   88,   97,   106,  115,  124,  133,  142,
    151,  167,  184,  200,  215,  231,  245,  260,  274,  301,  327,  351,
    375,  397,  418,  439,  458,  495,  528,  559,  587,  613,  637,  659,
    680,  717,  749,  777,  801,  823,  842,  859,  874,  899,  919,  936,
    949,  960,  969,  977,  983,  994,  1001, 1006, 1010, 1013, 1015, 1017,
    1018, 1020, 1022, 1022, 1023, 1023, 1023, 1024,
  };
  static const int xsq_iq_q10[] = {
    0,      4,      8,      12,     16,     20,     24,     28,     32,
    40,     48,     56,     64,     72,     80,     88,     96,     112,
    128,    144,    160,    176,    192,    208,    224,    256,    288,
    320,    352,    384,    416,    448,    480,    544,    608,    672,
    736,    800,    864,    928,    992,    1120,   1248,   1376,   1504,
    1632,   1760,   1888,   2016,   2272,   2528,   2784,   3040,   3296,
    3552,   3808,   4064,   4576,   5088,   5600,   6112,   6624,   7136,
    7648,   8160,   9184,   10208,  11232,  12256,  13280,  14304,  15328,
    16352,  18400,  20448,  22496,  24544,  26592,  28640,  30688,  32736,
    36832,  40928,  45024,  49120,  53216,  57312,  61408,  65504,  73696,
    81888,  90080,  98272,  106464, 114656, 122848, 131040, 147424, 163808,
    180192, 196576, 212960, 229344, 245728,
  };
  const int tmp = (xsq_q10 >> 2) + 8;
  const int k = get_msb(tmp) - 3;
  const int xq = (k << 3) + ((tmp >> k) & 0x7);
  const int one_q10 = 1 << 10;
  const int a_q10 = ((xsq_q10 - xsq_iq_q10[xq]) << 10) >> (2 + k);
  const int b_q10 = one_q10 - a_q10;
  *r_q10 = (rate_tab_q10[xq] * b_q10 + rate_tab_q10[xq + 1] * a_q10) >> 10;
  *d_q10 = (dist_tab_q10[xq] * b_q10 + dist_tab_q10[xq + 1] * a_q10) >> 10;
}

void av1_model_rd_from_var_lapndz(int64_t var, unsigned int n_log2,
                                  unsigned int qstep, int *rate,
                                  int64_t *dist) {
  // This function models the rate and distortion for a Laplacian
  // source with given variance when quantized with a uniform quantizer
  // with given stepsize. The closed form expressions are in:
  // Hang and Chen, "Source Model for transform video coder and its
  // application - Part I: Fundamental Theory", IEEE Trans. Circ.
  // Sys. for Video Tech., April 1997.
  if (var == 0) {
    *rate = 0;
    *dist = 0;
  } else {
    int d_q10, r_q10;
    static const uint32_t MAX_XSQ_Q10 = 245727;
    const uint64_t xsq_q10_64 =
        (((uint64_t)qstep * qstep << (n_log2 + 10)) + (var >> 1)) / var;
    const int xsq_q10 = (int)AOMMIN(xsq_q10_64, MAX_XSQ_Q10);
    model_rd_norm(xsq_q10, &r_q10, &d_q10);
    *rate = ROUND_POWER_OF_TWO(r_q10 << n_log2, 10 - AV1_PROB_COST_SHIFT);
    *dist = (var * (int64_t)d_q10 + 512) >> 10;
  }
}

static double interp_cubic(const double *p, double x) {
  return p[1] + 0.5 * x *
                    (p[2] - p[0] +
                     x * (2.0 * p[0] - 5.0 * p[1] + 4.0 * p[2] - p[3] +
                          x * (3.0 * (p[1] - p[2]) + p[3] - p[0])));
}

/*
static double interp_bicubic(const double *p, int p_stride, double x,
                             double y) {
  double q[4];
  q[0] = interp_cubic(p, x);
  q[1] = interp_cubic(p + p_stride, x);
  q[2] = interp_cubic(p + 2 * p_stride, x);
  q[3] = interp_cubic(p + 3 * p_stride, x);
  return interp_cubic(q, y);
}
*/

static const uint8_t bsize_curvfit_model_cat_lookup[BLOCK_SIZES_ALL] = {
  0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 1, 1, 2, 2, 3, 3
};

static int sse_norm_curvfit_model_cat_lookup(double sse_norm) {
  return (sse_norm > 16.0);
}

static const double interp_rgrid_curv[4][65] = {
  {
      0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
      0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
      0.000000,    118.257702,  120.210658,  121.434853,  122.100487,
      122.377758,  122.436865,  72.290102,   96.974289,   101.652727,
      126.830141,  140.417377,  157.644879,  184.315291,  215.823873,
      262.300169,  335.919859,  420.624173,  519.185032,  619.854243,
      726.053595,  827.663369,  933.127475,  1037.988755, 1138.839609,
      1233.342933, 1333.508064, 1428.760126, 1533.396364, 1616.952052,
      1744.539319, 1803.413586, 1951.466618, 1994.227838, 2086.031680,
      2148.635443, 2239.068450, 2222.590637, 2338.859809, 2402.929011,
      2418.727875, 2435.342670, 2471.159469, 2523.187446, 2591.183827,
      2674.905840, 2774.110714, 2888.555675, 3017.997952, 3162.194773,
      3320.903365, 3493.880956, 3680.884773, 3881.672045, 4096.000000,
  },
  {
      0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
      0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
      0.000000,    13.087244,   15.919735,   25.930313,   24.412411,
      28.567417,   29.924194,   30.857010,   32.742979,   36.382570,
      39.210386,   42.265690,   47.378572,   57.014850,   82.740067,
      137.346562,  219.968084,  316.781856,  415.643773,  516.706538,
      614.914364,  714.303763,  815.512135,  911.210485,  1008.501528,
      1109.787854, 1213.772279, 1322.922561, 1414.752579, 1510.505641,
      1615.741888, 1697.989032, 1780.123933, 1847.453790, 1913.742309,
      1960.828122, 2047.500168, 2085.454095, 2129.230668, 2158.171824,
      2182.231724, 2217.684864, 2269.589211, 2337.264824, 2420.618694,
      2519.557814, 2633.989178, 2763.819779, 2908.956609, 3069.306660,
      3244.776927, 3435.274401, 3640.706076, 3860.978945, 4096.000000,
  },
  {
      0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
      0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
      0.000000,    4.656893,    5.123633,    5.594132,    6.162376,
      6.918433,    7.768444,    8.739415,    10.105862,   11.477328,
      13.236604,   15.421030,   19.093623,   25.801871,   46.724612,
      98.841054,   181.113466,  272.586364,  359.499769,  445.546343,
      525.944439,  605.188743,  681.793483,  756.668359,  838.486885,
      926.950356,  1015.482542, 1113.353926, 1204.897193, 1288.871992,
      1373.464145, 1455.746628, 1527.796460, 1588.475066, 1658.144771,
      1710.302500, 1807.563351, 1863.197608, 1927.281616, 1964.450872,
      2022.719898, 2100.041145, 2185.205712, 2280.993936, 2387.616216,
      2505.282950, 2634.204540, 2774.591385, 2926.653884, 3090.602436,
      3266.647443, 3454.999303, 3655.868416, 3869.465182, 4096.000000,
  },
  {
      0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
      0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
      0.000000,    0.337370,    0.391916,    0.468839,    0.566334,
      0.762564,    1.069225,    1.384361,    1.787581,    2.293948,
      3.251909,    4.412991,    8.050068,    11.606073,   27.668092,
      65.227758,   128.463938,  202.097653,  262.715851,  312.464873,
      355.601398,  400.609054,  447.201352,  495.761568,  552.871938,
      619.067625,  691.984883,  773.753288,  860.628503,  946.262808,
      1019.805896, 1106.061360, 1178.422145, 1244.852258, 1302.173987,
      1399.650266, 1548.092912, 1545.928652, 1670.817500, 1694.523823,
      1779.195362, 1882.155494, 1990.662097, 2108.325181, 2235.456119,
      2372.366287, 2519.367059, 2676.769812, 2844.885918, 3024.026754,
      3214.503695, 3416.628115, 3630.711389, 3857.064892, 4096.000000,
  },
};

static const double interp_dgrid_curv[3][65] = {
  {
      16.000000, 15.962891, 15.925174, 15.886888, 15.848074, 15.808770,
      15.769015, 15.728850, 15.688313, 15.647445, 15.606284, 15.564870,
      15.525918, 15.483820, 15.373330, 15.126844, 14.637442, 14.184387,
      13.560070, 12.880717, 12.165995, 11.378144, 10.438769, 9.130790,
      7.487633,  5.688649,  4.267515,  3.196300,  2.434201,  1.834064,
      1.369920,  1.035921,  0.775279,  0.574895,  0.427232,  0.314123,
      0.233236,  0.171440,  0.128188,  0.092762,  0.067569,  0.049324,
      0.036330,  0.027008,  0.019853,  0.015539,  0.011093,  0.008733,
      0.007624,  0.008105,  0.005427,  0.004065,  0.003427,  0.002848,
      0.002328,  0.001865,  0.001457,  0.001103,  0.000801,  0.000550,
      0.000348,  0.000193,  0.000085,  0.000021,  0.000000,
  },
  {
      16.000000, 15.996116, 15.984769, 15.966413, 15.941505, 15.910501,
      15.873856, 15.832026, 15.785466, 15.734633, 15.679981, 15.621967,
      15.560961, 15.460157, 15.288367, 15.052462, 14.466922, 13.921212,
      13.073692, 12.222005, 11.237799, 9.985848,  8.898823,  7.423519,
      5.995325,  4.773152,  3.744032,  2.938217,  2.294526,  1.762412,
      1.327145,  1.020728,  0.765535,  0.570548,  0.425833,  0.313825,
      0.232959,  0.171324,  0.128174,  0.092750,  0.067558,  0.049319,
      0.036330,  0.027008,  0.019853,  0.015539,  0.011093,  0.008733,
      0.007624,  0.008105,  0.005427,  0.004065,  0.003427,  0.002848,
      0.002328,  0.001865,  0.001457,  0.001103,  0.000801,  0.000550,
      0.000348,  0.000193,  0.000085,  0.000021,  -0.000000,
  },
};

void av1_model_rd_curvfit(BLOCK_SIZE bsize, double sse_norm, double xqr,
                          double *rate_f, double *distbysse_f) {
  const double x_start = -15.5;
  const double x_end = 16.5;
  const double x_step = 0.5;
  const double epsilon = 1e-6;
  const int rcat = bsize_curvfit_model_cat_lookup[bsize];
  const int dcat = sse_norm_curvfit_model_cat_lookup(sse_norm);
  (void)x_end;

  xqr = AOMMAX(xqr, x_start + x_step + epsilon);
  xqr = AOMMIN(xqr, x_end - x_step - epsilon);
  const double x = (xqr - x_start) / x_step;
  const int xi = (int)floor(x);
  const double xo = x - xi;

  assert(xi > 0);

  const double *prate = &interp_rgrid_curv[rcat][(xi - 1)];
  *rate_f = interp_cubic(prate, xo);
  const double *pdist = &interp_dgrid_curv[dcat][(xi - 1)];
  *distbysse_f = interp_cubic(pdist, xo);
}

static void get_entropy_contexts_plane(BLOCK_SIZE plane_bsize,
                                       const struct macroblockd_plane *pd,
                                       ENTROPY_CONTEXT t_above[MAX_MIB_SIZE],
                                       ENTROPY_CONTEXT t_left[MAX_MIB_SIZE]) {
  const int num_4x4_w = mi_size_wide[plane_bsize];
  const int num_4x4_h = mi_size_high[plane_bsize];
  const ENTROPY_CONTEXT *const above = pd->above_entropy_context;
  const ENTROPY_CONTEXT *const left = pd->left_entropy_context;

  memcpy(t_above, above, sizeof(ENTROPY_CONTEXT) * num_4x4_w);
  memcpy(t_left, left, sizeof(ENTROPY_CONTEXT) * num_4x4_h);
}

void av1_get_entropy_contexts(BLOCK_SIZE plane_bsize,
                              const struct macroblockd_plane *pd,
                              ENTROPY_CONTEXT t_above[MAX_MIB_SIZE],
                              ENTROPY_CONTEXT t_left[MAX_MIB_SIZE]) {
  assert(plane_bsize < BLOCK_SIZES_ALL);
  get_entropy_contexts_plane(plane_bsize, pd, t_above, t_left);
}

// Special clamping used in the encoder when calculating a prediction
//
// Logically, all pixel fetches used for prediction are clamped against the
// edges of the frame. But doing this directly is slow, so instead we allocate
// a finite border around the frame and fill it with copies of the outermost
// pixels.
//
// Since this border is finite, we need to clamp the motion vector before
// prediction in order to avoid out-of-bounds reads. At the same time, this
// clamp must not change the prediction result.
//
// We can balance both of these concerns by calculating how far we would have
// to go in each direction before the extended prediction region (the current
// block + AOM_INTERP_EXTEND many pixels around the block) would be mapped
// so that it touches the frame only at one row or column. This is a special
// point because any more extreme MV will always lead to the same prediction.
// So it is safe to clamp at that point.
//
// In the worst case, this requires a border of
//   max_block_width + 2*AOM_INTERP_EXTEND = 128 + 2*4 = 136 pixels
// around the frame edges.
static inline void enc_clamp_mv(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                                MV *mv) {
  int bw = xd->width << MI_SIZE_LOG2;
  int bh = xd->height << MI_SIZE_LOG2;

  int px_to_left_edge = xd->mi_col << MI_SIZE_LOG2;
  int px_to_right_edge = (cm->mi_params.mi_cols - xd->mi_col) << MI_SIZE_LOG2;
  int px_to_top_edge = xd->mi_row << MI_SIZE_LOG2;
  int px_to_bottom_edge = (cm->mi_params.mi_rows - xd->mi_row) << MI_SIZE_LOG2;

  const SubpelMvLimits mv_limits = {
    .col_min = -GET_MV_SUBPEL(px_to_left_edge + bw + AOM_INTERP_EXTEND),
    .col_max = GET_MV_SUBPEL(px_to_right_edge + AOM_INTERP_EXTEND),
    .row_min = -GET_MV_SUBPEL(px_to_top_edge + bh + AOM_INTERP_EXTEND),
    .row_max = GET_MV_SUBPEL(px_to_bottom_edge + AOM_INTERP_EXTEND)
  };
  clamp_mv(mv, &mv_limits);
}

void av1_mv_pred(const AV1_COMP *cpi, MACROBLOCK *x, uint8_t *ref_y_buffer,
                 int ref_y_stride, int ref_frame, BLOCK_SIZE block_size) {
  const MV_REFERENCE_FRAME ref_frames[2] = { ref_frame, NONE_FRAME };
  const int_mv ref_mv =
      av1_get_ref_mv_from_stack(0, ref_frames, 0, &x->mbmi_ext);
  const int_mv ref_mv1 =
      av1_get_ref_mv_from_stack(0, ref_frames, 1, &x->mbmi_ext);
  MV pred_mv[MAX_MV_REF_CANDIDATES + 1];
  int num_mv_refs = 0;
  pred_mv[num_mv_refs++] = ref_mv.as_mv;
  if (ref_mv.as_int != ref_mv1.as_int) {
    pred_mv[num_mv_refs++] = ref_mv1.as_mv;
  }

  assert(num_mv_refs <= (int)(sizeof(pred_mv) / sizeof(pred_mv[0])));

  const uint8_t *const src_y_ptr = x->plane[0].src.buf;
  int zero_seen = 0;
  int best_sad = INT_MAX;
  int max_mv = 0;
  // Get the sad for each candidate reference mv.
  for (int i = 0; i < num_mv_refs; ++i) {
    MV *this_mv = &pred_mv[i];
    enc_clamp_mv(&cpi->common, &x->e_mbd, this_mv);

    const int fp_row = (this_mv->row + 3 + (this_mv->row >= 0)) >> 3;
    const int fp_col = (this_mv->col + 3 + (this_mv->col >= 0)) >> 3;
    max_mv = AOMMAX(max_mv, AOMMAX(abs(this_mv->row), abs(this_mv->col)) >> 3);

    if (fp_row == 0 && fp_col == 0 && zero_seen) continue;
    zero_seen |= (fp_row == 0 && fp_col == 0);

    const uint8_t *const ref_y_ptr =
        &ref_y_buffer[ref_y_stride * fp_row + fp_col];
    // Find sad for current vector.
    const int this_sad = cpi->ppi->fn_ptr[block_size].sdf(
        src_y_ptr, x->plane[0].src.stride, ref_y_ptr, ref_y_stride);
    // Note if it is the best so far.
    if (this_sad < best_sad) {
      best_sad = this_sad;
    }
    if (i == 0)
      x->pred_mv0_sad[ref_frame] = this_sad;
    else if (i == 1)
      x->pred_mv1_sad[ref_frame] = this_sad;
  }

  // Note the index of the mv that worked best in the reference list.
  x->max_mv_context[ref_frame] = max_mv;
  x->pred_mv_sad[ref_frame] = best_sad;
}

void av1_setup_pred_block(const MACROBLOCKD *xd,
                          struct buf_2d dst[MAX_MB_PLANE],
                          const YV12_BUFFER_CONFIG *src,
                          const struct scale_factors *scale,
                          const struct scale_factors *scale_uv,
                          const int num_planes) {
  dst[0].buf = src->y_buffer;
  dst[0].stride = src->y_stride;
  dst[1].buf = src->u_buffer;
  dst[2].buf = src->v_buffer;
  dst[1].stride = dst[2].stride = src->uv_stride;

  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  for (int i = 0; i < num_planes; ++i) {
    setup_pred_plane(dst + i, xd->mi[0]->bsize, dst[i].buf,
                     i ? src->uv_crop_width : src->y_crop_width,
                     i ? src->uv_crop_height : src->y_crop_height,
                     dst[i].stride, mi_row, mi_col, i ? scale_uv : scale,
                     xd->plane[i].subsampling_x, xd->plane[i].subsampling_y);
  }
}

YV12_BUFFER_CONFIG *av1_get_scaled_ref_frame(const AV1_COMP *cpi,
                                             int ref_frame) {
  assert(ref_frame >= LAST_FRAME && ref_frame <= ALTREF_FRAME);
  RefCntBuffer *const scaled_buf = cpi->scaled_ref_buf[ref_frame - 1];
  const RefCntBuffer *const ref_buf =
      get_ref_frame_buf(&cpi->common, ref_frame);
  return (scaled_buf != ref_buf && scaled_buf != NULL) ? &scaled_buf->buf
                                                       : NULL;
}

int av1_get_switchable_rate(const MACROBLOCK *x, const MACROBLOCKD *xd,
                            InterpFilter interp_filter, int dual_filter) {
  if (interp_filter == SWITCHABLE) {
    const MB_MODE_INFO *const mbmi = xd->mi[0];
    int inter_filter_cost = 0;
    for (int dir = 0; dir < 2; ++dir) {
      if (dir && !dual_filter) break;
      const int ctx = av1_get_pred_context_switchable_interp(xd, dir);
      const InterpFilter filter =
          av1_extract_interp_filter(mbmi->interp_filters, dir);
      inter_filter_cost += x->mode_costs.switchable_interp_costs[ctx][filter];
    }
    return SWITCHABLE_INTERP_RATE_FACTOR * inter_filter_cost;
  } else {
    return 0;
  }
}

void av1_set_rd_speed_thresholds(AV1_COMP *cpi) {
  RD_OPT *const rd = &cpi->rd;

  // Set baseline threshold values.
  av1_zero(rd->thresh_mult);

  rd->thresh_mult[THR_NEARESTMV] = 300;
  rd->thresh_mult[THR_NEARESTL2] = 300;
  rd->thresh_mult[THR_NEARESTL3] = 300;
  rd->thresh_mult[THR_NEARESTB] = 300;
  rd->thresh_mult[THR_NEARESTA2] = 300;
  rd->thresh_mult[THR_NEARESTA] = 300;
  rd->thresh_mult[THR_NEARESTG] = 300;

  rd->thresh_mult[THR_NEWMV] = 1000;
  rd->thresh_mult[THR_NEWL2] = 1000;
  rd->thresh_mult[THR_NEWL3] = 1000;
  rd->thresh_mult[THR_NEWB] = 1000;
  rd->thresh_mult[THR_NEWA2] = 1100;
  rd->thresh_mult[THR_NEWA] = 1000;
  rd->thresh_mult[THR_NEWG] = 1000;

  rd->thresh_mult[THR_NEARMV] = 1000;
  rd->thresh_mult[THR_NEARL2] = 1000;
  rd->thresh_mult[THR_NEARL3] = 1000;
  rd->thresh_mult[THR_NEARB] = 1000;
  rd->thresh_mult[THR_NEARA2] = 1000;
  rd->thresh_mult[THR_NEARA] = 1000;
  rd->thresh_mult[THR_NEARG] = 1000;

  rd->thresh_mult[THR_GLOBALMV] = 2200;
  rd->thresh_mult[THR_GLOBALL2] = 2000;
  rd->thresh_mult[THR_GLOBALL3] = 2000;
  rd->thresh_mult[THR_GLOBALB] = 2400;
  rd->thresh_mult[THR_GLOBALA2] = 2000;
  rd->thresh_mult[THR_GLOBALG] = 2000;
  rd->thresh_mult[THR_GLOBALA] = 2400;

  rd->thresh_mult[THR_COMP_NEAREST_NEARESTLA] = 1100;
  rd->thresh_mult[THR_COMP_NEAREST_NEARESTL2A] = 1000;
  rd->thresh_mult[THR_COMP_NEAREST_NEARESTL3A] = 800;
  rd->thresh_mult[THR_COMP_NEAREST_NEARESTGA] = 900;
  rd->thresh_mult[THR_COMP_NEAREST_NEARESTLB] = 1000;
  rd->thresh_mult[THR_COMP_NEAREST_NEARESTL2B] = 1000;
  rd->thresh_mult[THR_COMP_NEAREST_NEARESTL3B] = 1000;
  rd->thresh_mult[THR_COMP_NEAREST_NEARESTGB] = 1000;
  rd->thresh_mult[THR_COMP_NEAREST_NEARESTLA2] = 1000;
  rd->thresh_mult[THR_COMP_NEAREST_NEARESTL2A2] = 1000;
  rd->thresh_mult[THR_COMP_NEAREST_NEARESTL3A2] = 1000;
  rd->thresh_mult[THR_COMP_NEAREST_NEARESTGA2] = 1000;

  rd->thresh_mult[THR_COMP_NEAREST_NEARESTLL2] = 2000;
  rd->thresh_mult[THR_COMP_NEAREST_NEARESTLL3] = 2000;
  rd->thresh_mult[THR_COMP_NEAREST_NEARESTLG] = 2000;
  rd->thresh_mult[THR_COMP_NEAREST_NEARESTBA] = 2000;

  rd->thresh_mult[THR_COMP_NEAR_NEARLA] = 1200;
  rd->thresh_mult[THR_COMP_NEAREST_NEWLA] = 1500;
  rd->thresh_mult[THR_COMP_NEW_NEARESTLA] = 1500;
  rd->thresh_mult[THR_COMP_NEAR_NEWLA] = 1530;
  rd->thresh_mult[THR_COMP_NEW_NEARLA] = 1870;
  rd->thresh_mult[THR_COMP_NEW_NEWLA] = 2400;
  rd->thresh_mult[THR_COMP_GLOBAL_GLOBALLA] = 2750;

  rd->thresh_mult[THR_COMP_NEAR_NEARL2A] = 1200;
  rd->thresh_mult[THR_COMP_NEAREST_NEWL2A] = 1500;
  rd->thresh_mult[THR_COMP_NEW_NEARESTL2A] = 1500;
  rd->thresh_mult[THR_COMP_NEAR_NEWL2A] = 1870;
  rd->thresh_mult[THR_COMP_NEW_NEARL2A] = 1700;
  rd->thresh_mult[THR_COMP_NEW_NEWL2A] = 1800;
  rd->thresh_mult[THR_COMP_GLOBAL_GLOBALL2A] = 2500;

  rd->thresh_mult[THR_COMP_NEAR_NEARL3A] = 1200;
  rd->thresh_mult[THR_COMP_NEAREST_NEWL3A] = 1500;
  rd->thresh_mult[THR_COMP_NEW_NEARESTL3A] = 1500;
  rd->thresh_mult[THR_COMP_NEAR_NEWL3A] = 1700;
  rd->thresh_mult[THR_COMP_NEW_NEARL3A] = 1700;
  rd->thresh_mult[THR_COMP_NEW_NEWL3A] = 2000;
  rd->thresh_mult[THR_COMP_GLOBAL_GLOBALL3A] = 3000;

  rd->thresh_mult[THR_COMP_NEAR_NEARGA] = 1320;
  rd->thresh_mult[THR_COMP_NEAREST_NEWGA] = 1500;
  rd->thresh_mult[THR_COMP_NEW_NEARESTGA] = 1500;
  rd->thresh_mult[THR_COMP_NEAR_NEWGA] = 2040;
  rd->thresh_mult[THR_COMP_NEW_NEARGA] = 1700;
  rd->thresh_mult[THR_COMP_NEW_NEWGA] = 2000;
  rd->thresh_mult[THR_COMP_GLOBAL_GLOBALGA] = 2250;

  rd->thresh_mult[THR_COMP_NEAR_NEARLB] = 1200;
  rd->thresh_mult[THR_COMP_NEAREST_NEWLB] = 1500;
  rd->thresh_mult[THR_COMP_NEW_NEARESTLB] = 1500;
  rd->thresh_mult[THR_COMP_NEAR_NEWLB] = 1360;
  rd->thresh_mult[THR_COMP_NEW_NEARLB] = 1700;
  rd->thresh_mult[THR_COMP_NEW_NEWLB] = 2400;
  rd->thresh_mult[THR_COMP_GLOBAL_GLOBALLB] = 2250;

  rd->thresh_mult[THR_COMP_NEAR_NEARL2B] = 1200;
  rd->thresh_mult[THR_COMP_NEAREST_NEWL2B] = 1500;
  rd->thresh_mult[THR_COMP_NEW_NEARESTL2B] = 1500;
  rd->thresh_mult[THR_COMP_NEAR_NEWL2B] = 1700;
  rd->thresh_mult[THR_COMP_NEW_NEARL2B] = 1700;
  rd->thresh_mult[THR_COMP_NEW_NEWL2B] = 2000;
  rd->thresh_mult[THR_COMP_GLOBAL_GLOBALL2B] = 2500;

  rd->thresh_mult[THR_COMP_NEAR_NEARL3B] = 1200;
  rd->thresh_mult[THR_COMP_NEAREST_NEWL3B] = 1500;
  rd->thresh_mult[THR_COMP_NEW_NEARESTL3B] = 1500;
  rd->thresh_mult[THR_COMP_NEAR_NEWL3B] = 1870;
  rd->thresh_mult[THR_COMP_NEW_NEARL3B] = 1700;
  rd->thresh_mult[THR_COMP_NEW_NEWL3B] = 2000;
  rd->thresh_mult[THR_COMP_GLOBAL_GLOBALL3B] = 2500;

  rd->thresh_mult[THR_COMP_NEAR_NEARGB] = 1200;
  rd->thresh_mult[THR_COMP_NEAREST_NEWGB] = 1500;
  rd->thresh_mult[THR_COMP_NEW_NEARESTGB] = 1500;
  rd->thresh_mult[THR_COMP_NEAR_NEWGB] = 1700;
  rd->thresh_mult[THR_COMP_NEW_NEARGB] = 1700;
  rd->thresh_mult[THR_COMP_NEW_NEWGB] = 2000;
  rd->thresh_mult[THR_COMP_GLOBAL_GLOBALGB] = 2500;

  rd->thresh_mult[THR_COMP_NEAR_NEARLA2] = 1200;
  rd->thresh_mult[THR_COMP_NEAREST_NEWLA2] = 1800;
  rd->thresh_mult[THR_COMP_NEW_NEARESTLA2] = 1500;
  rd->thresh_mult[THR_COMP_NEAR_NEWLA2] = 1700;
  rd->thresh_mult[THR_COMP_NEW_NEARLA2] = 1700;
  rd->thresh_mult[THR_COMP_NEW_NEWLA2] = 2000;
  rd->thresh_mult[THR_COMP_GLOBAL_GLOBALLA2] = 2500;

  rd->thresh_mult[THR_COMP_NEAR_NEARL2A2] = 1200;
  rd->thresh_mult[THR_COMP_NEAREST_NEWL2A2] = 1500;
  rd->thresh_mult[THR_COMP_NEW_NEARESTL2A2] = 1500;
  rd->thresh_mult[THR_COMP_NEAR_NEWL2A2] = 1700;
  rd->thresh_mult[THR_COMP_NEW_NEARL2A2] = 1700;
  rd->thresh_mult[THR_COMP_NEW_NEWL2A2] = 2000;
  rd->thresh_mult[THR_COMP_GLOBAL_GLOBALL2A2] = 2500;

  rd->thresh_mult[THR_COMP_NEAR_NEARL3A2] = 1440;
  rd->thresh_mult[THR_COMP_NEAREST_NEWL3A2] = 1500;
  rd->thresh_mult[THR_COMP_NEW_NEARESTL3A2] = 1500;
  rd->thresh_mult[THR_COMP_NEAR_NEWL3A2] = 1700;
  rd->thresh_mult[THR_COMP_NEW_NEARL3A2] = 1700;
  rd->thresh_mult[THR_COMP_NEW_NEWL3A2] = 2000;
  rd->thresh_mult[THR_COMP_GLOBAL_GLOBALL3A2] = 2500;

  rd->thresh_mult[THR_COMP_NEAR_NEARGA2] = 1200;
  rd->thresh_mult[THR_COMP_NEAREST_NEWGA2] = 1500;
  rd->thresh_mult[THR_COMP_NEW_NEARESTGA2] = 1500;
  rd->thresh_mult[THR_COMP_NEAR_NEWGA2] = 1700;
  rd->thresh_mult[THR_COMP_NEW_NEARGA2] = 1700;
  rd->thresh_mult[THR_COMP_NEW_NEWGA2] = 2000;
  rd->thresh_mult[THR_COMP_GLOBAL_GLOBALGA2] = 2750;

  rd->thresh_mult[THR_COMP_NEAR_NEARLL2] = 1600;
  rd->thresh_mult[THR_COMP_NEAREST_NEWLL2] = 2000;
  rd->thresh_mult[THR_COMP_NEW_NEARESTLL2] = 2000;
  rd->thresh_mult[THR_COMP_NEAR_NEWLL2] = 2640;
  rd->thresh_mult[THR_COMP_NEW_NEARLL2] = 2200;
  rd->thresh_mult[THR_COMP_NEW_NEWLL2] = 2400;
  rd->thresh_mult[THR_COMP_GLOBAL_GLOBALLL2] = 3200;

  rd->thresh_mult[THR_COMP_NEAR_NEARLL3] = 1600;
  rd->thresh_mult[THR_COMP_NEAREST_NEWLL3] = 2000;
  rd->thresh_mult[THR_COMP_NEW_NEARESTLL3] = 1800;
  rd->thresh_mult[THR_COMP_NEAR_NEWLL3] = 2200;
  rd->thresh_mult[THR_COMP_NEW_NEARLL3] = 2200;
  rd->thresh_mult[THR_COMP_NEW_NEWLL3] = 2400;
  rd->thresh_mult[THR_COMP_GLOBAL_GLOBALLL3] = 3200;

  rd->thresh_mult[THR_COMP_NEAR_NEARLG] = 1760;
  rd->thresh_mult[THR_COMP_NEAREST_NEWLG] = 2400;
  rd->thresh_mult[THR_COMP_NEW_NEARESTLG] = 2000;
  rd->thresh_mult[THR_COMP_NEAR_NEWLG] = 1760;
  rd->thresh_mult[THR_COMP_NEW_NEARLG] = 2640;
  rd->thresh_mult[THR_COMP_NEW_NEWLG] = 2400;
  rd->thresh_mult[THR_COMP_GLOBAL_GLOBALLG] = 3200;

  rd->thresh_mult[THR_COMP_NEAR_NEARBA] = 1600;
  rd->thresh_mult[THR_COMP_NEAREST_NEWBA] = 2000;
  rd->thresh_mult[THR_COMP_NEW_NEARESTBA] = 2000;
  rd->thresh_mult[THR_COMP_NEAR_NEWBA] = 2200;
  rd->thresh_mult[THR_COMP_NEW_NEARBA] = 1980;
  rd->thresh_mult[THR_COMP_NEW_NEWBA] = 2640;
  rd->thresh_mult[THR_COMP_GLOBAL_GLOBALBA] = 3200;

  rd->thresh_mult[THR_DC] = 1000;
  rd->thresh_mult[THR_PAETH] = 1000;
  rd->thresh_mult[THR_SMOOTH] = 2200;
  rd->thresh_mult[THR_SMOOTH_V] = 2000;
  rd->thresh_mult[THR_SMOOTH_H] = 2000;
  rd->thresh_mult[THR_H_PRED] = 2000;
  rd->thresh_mult[THR_V_PRED] = 1800;
  rd->thresh_mult[THR_D135_PRED] = 2500;
  rd->thresh_mult[THR_D203_PRED] = 2000;
  rd->thresh_mult[THR_D157_PRED] = 2500;
  rd->thresh_mult[THR_D67_PRED] = 2000;
  rd->thresh_mult[THR_D113_PRED] = 2500;
  rd->thresh_mult[THR_D45_PRED] = 2500;
}

static inline void update_thr_fact(int (*factor_buf)[MAX_MODES],
                                   THR_MODES best_mode_index,
                                   THR_MODES mode_start, THR_MODES mode_end,
                                   BLOCK_SIZE min_size, BLOCK_SIZE max_size,
                                   int max_rd_thresh_factor) {
  for (THR_MODES mode = mode_start; mode < mode_end; ++mode) {
    for (BLOCK_SIZE bs = min_size; bs <= max_size; ++bs) {
      int *const fact = &factor_buf[bs][mode];
      if (mode == best_mode_index) {
        *fact -= (*fact >> RD_THRESH_LOG_DEC_FACTOR);
      } else {
        *fact = AOMMIN(*fact + RD_THRESH_INC, max_rd_thresh_factor);
      }
    }
  }
}

void av1_update_rd_thresh_fact(
    const AV1_COMMON *const cm, int (*factor_buf)[MAX_MODES],
    int use_adaptive_rd_thresh, BLOCK_SIZE bsize, THR_MODES best_mode_index,
    THR_MODES inter_mode_start, THR_MODES inter_mode_end,
    THR_MODES intra_mode_start, THR_MODES intra_mode_end) {
  assert(use_adaptive_rd_thresh > 0);
  const int max_rd_thresh_factor = use_adaptive_rd_thresh * RD_THRESH_MAX_FACT;

  const int bsize_is_1_to_4 = bsize > cm->seq_params->sb_size;
  BLOCK_SIZE min_size, max_size;
  if (bsize_is_1_to_4) {
    // This part handles block sizes with 1:4 and 4:1 aspect ratios
    // TODO(any): Experiment with threshold update for parent/child blocks
    min_size = bsize;
    max_size = bsize;
  } else {
    min_size = AOMMAX(bsize - 2, BLOCK_4X4);
    max_size = AOMMIN(bsize + 2, (int)cm->seq_params->sb_size);
  }

  update_thr_fact(factor_buf, best_mode_index, inter_mode_start, inter_mode_end,
                  min_size, max_size, max_rd_thresh_factor);
  update_thr_fact(factor_buf, best_mode_index, intra_mode_start, intra_mode_end,
                  min_size, max_size, max_rd_thresh_factor);
}

int av1_get_intra_cost_penalty(int qindex, int qdelta,
                               aom_bit_depth_t bit_depth) {
  const int q = av1_dc_quant_QTX(qindex, qdelta, bit_depth);
  switch (bit_depth) {
    case AOM_BITS_8: return 20 * q;
    case AOM_BITS_10: return 5 * q;
    case AOM_BITS_12: return ROUND_POWER_OF_TWO(5 * q, 2);
    default:
      assert(0 && "bit_depth should be AOM_BITS_8, AOM_BITS_10 or AOM_BITS_12");
      return -1;
  }
}
