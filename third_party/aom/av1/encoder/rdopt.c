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
#include <math.h>
#include <stdbool.h>

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"
#include "config/av1_rtcd.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_dsp/blend.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/aom_timer.h"
#include "aom_ports/mem.h"

#include "av1/common/av1_common_int.h"
#include "av1/common/cfl.h"
#include "av1/common/blockd.h"
#include "av1/common/common.h"
#include "av1/common/common_data.h"
#include "av1/common/entropy.h"
#include "av1/common/entropymode.h"
#include "av1/common/idct.h"
#include "av1/common/mvref_common.h"
#include "av1/common/obmc.h"
#include "av1/common/pred_common.h"
#include "av1/common/quant_common.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"
#include "av1/common/scan.h"
#include "av1/common/seg_common.h"
#include "av1/common/txb_common.h"
#include "av1/common/warped_motion.h"

#include "av1/encoder/aq_variance.h"
#include "av1/encoder/av1_quantize.h"
#include "av1/encoder/cost.h"
#include "av1/encoder/compound_type.h"
#include "av1/encoder/encodemb.h"
#include "av1/encoder/encodemv.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/encodetxb.h"
#include "av1/encoder/hybrid_fwd_txfm.h"
#include "av1/encoder/interp_search.h"
#include "av1/encoder/intra_mode_search.h"
#include "av1/encoder/intra_mode_search_utils.h"
#include "av1/encoder/mcomp.h"
#include "av1/encoder/ml.h"
#include "av1/encoder/mode_prune_model_weights.h"
#include "av1/encoder/model_rd.h"
#include "av1/encoder/motion_search_facade.h"
#include "av1/encoder/palette.h"
#include "av1/encoder/pustats.h"
#include "av1/encoder/random.h"
#include "av1/encoder/ratectrl.h"
#include "av1/encoder/rd.h"
#include "av1/encoder/rdopt.h"
#include "av1/encoder/reconinter_enc.h"
#include "av1/encoder/tokenize.h"
#include "av1/encoder/tpl_model.h"
#include "av1/encoder/tx_search.h"
#include "av1/encoder/var_based_part.h"

#define LAST_NEW_MV_INDEX 6

// Mode_threshold multiplication factor table for prune_inter_modes_if_skippable
// The values are kept in Q12 format and equation used to derive is
// (2.5 - ((float)x->qindex / MAXQ) * 1.5)
#define MODE_THRESH_QBITS 12
static const int mode_threshold_mul_factor[QINDEX_RANGE] = {
  10240, 10216, 10192, 10168, 10144, 10120, 10095, 10071, 10047, 10023, 9999,
  9975,  9951,  9927,  9903,  9879,  9854,  9830,  9806,  9782,  9758,  9734,
  9710,  9686,  9662,  9638,  9614,  9589,  9565,  9541,  9517,  9493,  9469,
  9445,  9421,  9397,  9373,  9349,  9324,  9300,  9276,  9252,  9228,  9204,
  9180,  9156,  9132,  9108,  9083,  9059,  9035,  9011,  8987,  8963,  8939,
  8915,  8891,  8867,  8843,  8818,  8794,  8770,  8746,  8722,  8698,  8674,
  8650,  8626,  8602,  8578,  8553,  8529,  8505,  8481,  8457,  8433,  8409,
  8385,  8361,  8337,  8312,  8288,  8264,  8240,  8216,  8192,  8168,  8144,
  8120,  8096,  8072,  8047,  8023,  7999,  7975,  7951,  7927,  7903,  7879,
  7855,  7831,  7806,  7782,  7758,  7734,  7710,  7686,  7662,  7638,  7614,
  7590,  7566,  7541,  7517,  7493,  7469,  7445,  7421,  7397,  7373,  7349,
  7325,  7301,  7276,  7252,  7228,  7204,  7180,  7156,  7132,  7108,  7084,
  7060,  7035,  7011,  6987,  6963,  6939,  6915,  6891,  6867,  6843,  6819,
  6795,  6770,  6746,  6722,  6698,  6674,  6650,  6626,  6602,  6578,  6554,
  6530,  6505,  6481,  6457,  6433,  6409,  6385,  6361,  6337,  6313,  6289,
  6264,  6240,  6216,  6192,  6168,  6144,  6120,  6096,  6072,  6048,  6024,
  5999,  5975,  5951,  5927,  5903,  5879,  5855,  5831,  5807,  5783,  5758,
  5734,  5710,  5686,  5662,  5638,  5614,  5590,  5566,  5542,  5518,  5493,
  5469,  5445,  5421,  5397,  5373,  5349,  5325,  5301,  5277,  5253,  5228,
  5204,  5180,  5156,  5132,  5108,  5084,  5060,  5036,  5012,  4987,  4963,
  4939,  4915,  4891,  4867,  4843,  4819,  4795,  4771,  4747,  4722,  4698,
  4674,  4650,  4626,  4602,  4578,  4554,  4530,  4506,  4482,  4457,  4433,
  4409,  4385,  4361,  4337,  4313,  4289,  4265,  4241,  4216,  4192,  4168,
  4144,  4120,  4096
};

static const THR_MODES av1_default_mode_order[MAX_MODES] = {
  THR_NEARESTMV,
  THR_NEARESTL2,
  THR_NEARESTL3,
  THR_NEARESTB,
  THR_NEARESTA2,
  THR_NEARESTA,
  THR_NEARESTG,

  THR_NEWMV,
  THR_NEWL2,
  THR_NEWL3,
  THR_NEWB,
  THR_NEWA2,
  THR_NEWA,
  THR_NEWG,

  THR_NEARMV,
  THR_NEARL2,
  THR_NEARL3,
  THR_NEARB,
  THR_NEARA2,
  THR_NEARA,
  THR_NEARG,

  THR_GLOBALMV,
  THR_GLOBALL2,
  THR_GLOBALL3,
  THR_GLOBALB,
  THR_GLOBALA2,
  THR_GLOBALA,
  THR_GLOBALG,

  THR_COMP_NEAREST_NEARESTLA,
  THR_COMP_NEAREST_NEARESTL2A,
  THR_COMP_NEAREST_NEARESTL3A,
  THR_COMP_NEAREST_NEARESTGA,
  THR_COMP_NEAREST_NEARESTLB,
  THR_COMP_NEAREST_NEARESTL2B,
  THR_COMP_NEAREST_NEARESTL3B,
  THR_COMP_NEAREST_NEARESTGB,
  THR_COMP_NEAREST_NEARESTLA2,
  THR_COMP_NEAREST_NEARESTL2A2,
  THR_COMP_NEAREST_NEARESTL3A2,
  THR_COMP_NEAREST_NEARESTGA2,
  THR_COMP_NEAREST_NEARESTLL2,
  THR_COMP_NEAREST_NEARESTLL3,
  THR_COMP_NEAREST_NEARESTLG,
  THR_COMP_NEAREST_NEARESTBA,

  THR_COMP_NEAR_NEARLB,
  THR_COMP_NEW_NEWLB,
  THR_COMP_NEW_NEARESTLB,
  THR_COMP_NEAREST_NEWLB,
  THR_COMP_NEW_NEARLB,
  THR_COMP_NEAR_NEWLB,
  THR_COMP_GLOBAL_GLOBALLB,

  THR_COMP_NEAR_NEARLA,
  THR_COMP_NEW_NEWLA,
  THR_COMP_NEW_NEARESTLA,
  THR_COMP_NEAREST_NEWLA,
  THR_COMP_NEW_NEARLA,
  THR_COMP_NEAR_NEWLA,
  THR_COMP_GLOBAL_GLOBALLA,

  THR_COMP_NEAR_NEARL2A,
  THR_COMP_NEW_NEWL2A,
  THR_COMP_NEW_NEARESTL2A,
  THR_COMP_NEAREST_NEWL2A,
  THR_COMP_NEW_NEARL2A,
  THR_COMP_NEAR_NEWL2A,
  THR_COMP_GLOBAL_GLOBALL2A,

  THR_COMP_NEAR_NEARL3A,
  THR_COMP_NEW_NEWL3A,
  THR_COMP_NEW_NEARESTL3A,
  THR_COMP_NEAREST_NEWL3A,
  THR_COMP_NEW_NEARL3A,
  THR_COMP_NEAR_NEWL3A,
  THR_COMP_GLOBAL_GLOBALL3A,

  THR_COMP_NEAR_NEARGA,
  THR_COMP_NEW_NEWGA,
  THR_COMP_NEW_NEARESTGA,
  THR_COMP_NEAREST_NEWGA,
  THR_COMP_NEW_NEARGA,
  THR_COMP_NEAR_NEWGA,
  THR_COMP_GLOBAL_GLOBALGA,

  THR_COMP_NEAR_NEARL2B,
  THR_COMP_NEW_NEWL2B,
  THR_COMP_NEW_NEARESTL2B,
  THR_COMP_NEAREST_NEWL2B,
  THR_COMP_NEW_NEARL2B,
  THR_COMP_NEAR_NEWL2B,
  THR_COMP_GLOBAL_GLOBALL2B,

  THR_COMP_NEAR_NEARL3B,
  THR_COMP_NEW_NEWL3B,
  THR_COMP_NEW_NEARESTL3B,
  THR_COMP_NEAREST_NEWL3B,
  THR_COMP_NEW_NEARL3B,
  THR_COMP_NEAR_NEWL3B,
  THR_COMP_GLOBAL_GLOBALL3B,

  THR_COMP_NEAR_NEARGB,
  THR_COMP_NEW_NEWGB,
  THR_COMP_NEW_NEARESTGB,
  THR_COMP_NEAREST_NEWGB,
  THR_COMP_NEW_NEARGB,
  THR_COMP_NEAR_NEWGB,
  THR_COMP_GLOBAL_GLOBALGB,

  THR_COMP_NEAR_NEARLA2,
  THR_COMP_NEW_NEWLA2,
  THR_COMP_NEW_NEARESTLA2,
  THR_COMP_NEAREST_NEWLA2,
  THR_COMP_NEW_NEARLA2,
  THR_COMP_NEAR_NEWLA2,
  THR_COMP_GLOBAL_GLOBALLA2,

  THR_COMP_NEAR_NEARL2A2,
  THR_COMP_NEW_NEWL2A2,
  THR_COMP_NEW_NEARESTL2A2,
  THR_COMP_NEAREST_NEWL2A2,
  THR_COMP_NEW_NEARL2A2,
  THR_COMP_NEAR_NEWL2A2,
  THR_COMP_GLOBAL_GLOBALL2A2,

  THR_COMP_NEAR_NEARL3A2,
  THR_COMP_NEW_NEWL3A2,
  THR_COMP_NEW_NEARESTL3A2,
  THR_COMP_NEAREST_NEWL3A2,
  THR_COMP_NEW_NEARL3A2,
  THR_COMP_NEAR_NEWL3A2,
  THR_COMP_GLOBAL_GLOBALL3A2,

  THR_COMP_NEAR_NEARGA2,
  THR_COMP_NEW_NEWGA2,
  THR_COMP_NEW_NEARESTGA2,
  THR_COMP_NEAREST_NEWGA2,
  THR_COMP_NEW_NEARGA2,
  THR_COMP_NEAR_NEWGA2,
  THR_COMP_GLOBAL_GLOBALGA2,

  THR_COMP_NEAR_NEARLL2,
  THR_COMP_NEW_NEWLL2,
  THR_COMP_NEW_NEARESTLL2,
  THR_COMP_NEAREST_NEWLL2,
  THR_COMP_NEW_NEARLL2,
  THR_COMP_NEAR_NEWLL2,
  THR_COMP_GLOBAL_GLOBALLL2,

  THR_COMP_NEAR_NEARLL3,
  THR_COMP_NEW_NEWLL3,
  THR_COMP_NEW_NEARESTLL3,
  THR_COMP_NEAREST_NEWLL3,
  THR_COMP_NEW_NEARLL3,
  THR_COMP_NEAR_NEWLL3,
  THR_COMP_GLOBAL_GLOBALLL3,

  THR_COMP_NEAR_NEARLG,
  THR_COMP_NEW_NEWLG,
  THR_COMP_NEW_NEARESTLG,
  THR_COMP_NEAREST_NEWLG,
  THR_COMP_NEW_NEARLG,
  THR_COMP_NEAR_NEWLG,
  THR_COMP_GLOBAL_GLOBALLG,

  THR_COMP_NEAR_NEARBA,
  THR_COMP_NEW_NEWBA,
  THR_COMP_NEW_NEARESTBA,
  THR_COMP_NEAREST_NEWBA,
  THR_COMP_NEW_NEARBA,
  THR_COMP_NEAR_NEWBA,
  THR_COMP_GLOBAL_GLOBALBA,

  THR_DC,
  THR_PAETH,
  THR_SMOOTH,
  THR_SMOOTH_V,
  THR_SMOOTH_H,
  THR_H_PRED,
  THR_V_PRED,
  THR_D135_PRED,
  THR_D203_PRED,
  THR_D157_PRED,
  THR_D67_PRED,
  THR_D113_PRED,
  THR_D45_PRED,
};

/*!\cond */
typedef struct SingleInterModeState {
  int64_t rd;
  MV_REFERENCE_FRAME ref_frame;
  int valid;
} SingleInterModeState;

typedef struct InterModeSearchState {
  int64_t best_rd;
  int64_t best_skip_rd[2];
  MB_MODE_INFO best_mbmode;
  int best_rate_y;
  int best_rate_uv;
  int best_mode_skippable;
  int best_skip2;
  THR_MODES best_mode_index;
  int num_available_refs;
  int64_t dist_refs[REF_FRAMES];
  int dist_order_refs[REF_FRAMES];
  int64_t mode_threshold[MAX_MODES];
  int64_t best_intra_rd;
  unsigned int best_pred_sse;

  /*!
   * \brief Keep track of best intra rd for use in compound mode.
   */
  int64_t best_pred_rd[REFERENCE_MODES];
  // Save a set of single_newmv for each checked ref_mv.
  int_mv single_newmv[MAX_REF_MV_SEARCH][REF_FRAMES];
  int single_newmv_rate[MAX_REF_MV_SEARCH][REF_FRAMES];
  int single_newmv_valid[MAX_REF_MV_SEARCH][REF_FRAMES];
  int64_t modelled_rd[MB_MODE_COUNT][MAX_REF_MV_SEARCH][REF_FRAMES];
  // The rd of simple translation in single inter modes
  int64_t simple_rd[MB_MODE_COUNT][MAX_REF_MV_SEARCH][REF_FRAMES];
  int64_t best_single_rd[REF_FRAMES];
  PREDICTION_MODE best_single_mode[REF_FRAMES];

  // Single search results by [directions][modes][reference frames]
  SingleInterModeState single_state[2][SINGLE_INTER_MODE_NUM][FWD_REFS];
  int single_state_cnt[2][SINGLE_INTER_MODE_NUM];
  SingleInterModeState single_state_modelled[2][SINGLE_INTER_MODE_NUM]
                                            [FWD_REFS];
  int single_state_modelled_cnt[2][SINGLE_INTER_MODE_NUM];
  MV_REFERENCE_FRAME single_rd_order[2][SINGLE_INTER_MODE_NUM][FWD_REFS];
  IntraModeSearchState intra_search_state;
  RD_STATS best_y_rdcost;
} InterModeSearchState;
/*!\endcond */

void av1_inter_mode_data_init(TileDataEnc *tile_data) {
  for (int i = 0; i < BLOCK_SIZES_ALL; ++i) {
    InterModeRdModel *md = &tile_data->inter_mode_rd_models[i];
    md->ready = 0;
    md->num = 0;
    md->dist_sum = 0;
    md->ld_sum = 0;
    md->sse_sum = 0;
    md->sse_sse_sum = 0;
    md->sse_ld_sum = 0;
  }
}

static int get_est_rate_dist(const TileDataEnc *tile_data, BLOCK_SIZE bsize,
                             int64_t sse, int *est_residue_cost,
                             int64_t *est_dist) {
  const InterModeRdModel *md = &tile_data->inter_mode_rd_models[bsize];
  if (md->ready) {
    if (sse < md->dist_mean) {
      *est_residue_cost = 0;
      *est_dist = sse;
    } else {
      *est_dist = (int64_t)round(md->dist_mean);
      const double est_ld = md->a * sse + md->b;
      // Clamp estimated rate cost by INT_MAX / 2.
      // TODO(angiebird@google.com): find better solution than clamping.
      if (fabs(est_ld) < 1e-2) {
        *est_residue_cost = INT_MAX / 2;
      } else {
        double est_residue_cost_dbl = ((sse - md->dist_mean) / est_ld);
        if (est_residue_cost_dbl < 0) {
          *est_residue_cost = 0;
        } else {
          *est_residue_cost =
              (int)AOMMIN((int64_t)round(est_residue_cost_dbl), INT_MAX / 2);
        }
      }
      if (*est_residue_cost <= 0) {
        *est_residue_cost = 0;
        *est_dist = sse;
      }
    }
    return 1;
  }
  return 0;
}

void av1_inter_mode_data_fit(TileDataEnc *tile_data, int rdmult) {
  for (int bsize = 0; bsize < BLOCK_SIZES_ALL; ++bsize) {
    const int block_idx = inter_mode_data_block_idx(bsize);
    InterModeRdModel *md = &tile_data->inter_mode_rd_models[bsize];
    if (block_idx == -1) continue;
    if ((md->ready == 0 && md->num < 200) || (md->ready == 1 && md->num < 64)) {
      continue;
    } else {
      if (md->ready == 0) {
        md->dist_mean = md->dist_sum / md->num;
        md->ld_mean = md->ld_sum / md->num;
        md->sse_mean = md->sse_sum / md->num;
        md->sse_sse_mean = md->sse_sse_sum / md->num;
        md->sse_ld_mean = md->sse_ld_sum / md->num;
      } else {
        const double factor = 3;
        md->dist_mean =
            (md->dist_mean * factor + (md->dist_sum / md->num)) / (factor + 1);
        md->ld_mean =
            (md->ld_mean * factor + (md->ld_sum / md->num)) / (factor + 1);
        md->sse_mean =
            (md->sse_mean * factor + (md->sse_sum / md->num)) / (factor + 1);
        md->sse_sse_mean =
            (md->sse_sse_mean * factor + (md->sse_sse_sum / md->num)) /
            (factor + 1);
        md->sse_ld_mean =
            (md->sse_ld_mean * factor + (md->sse_ld_sum / md->num)) /
            (factor + 1);
      }

      const double my = md->ld_mean;
      const double mx = md->sse_mean;
      const double dx = sqrt(md->sse_sse_mean);
      const double dxy = md->sse_ld_mean;

      md->a = (dxy - mx * my) / (dx * dx - mx * mx);
      md->b = my - md->a * mx;
      md->ready = 1;

      md->num = 0;
      md->dist_sum = 0;
      md->ld_sum = 0;
      md->sse_sum = 0;
      md->sse_sse_sum = 0;
      md->sse_ld_sum = 0;
    }
    (void)rdmult;
  }
}

static inline void inter_mode_data_push(TileDataEnc *tile_data,
                                        BLOCK_SIZE bsize, int64_t sse,
                                        int64_t dist, int residue_cost) {
  if (residue_cost == 0 || sse == dist) return;
  const int block_idx = inter_mode_data_block_idx(bsize);
  if (block_idx == -1) return;
  InterModeRdModel *rd_model = &tile_data->inter_mode_rd_models[bsize];
  if (rd_model->num < INTER_MODE_RD_DATA_OVERALL_SIZE) {
    const double ld = (sse - dist) * 1. / residue_cost;
    ++rd_model->num;
    rd_model->dist_sum += dist;
    rd_model->ld_sum += ld;
    rd_model->sse_sum += sse;
    rd_model->sse_sse_sum += (double)sse * (double)sse;
    rd_model->sse_ld_sum += sse * ld;
  }
}

static inline void inter_modes_info_push(InterModesInfo *inter_modes_info,
                                         int mode_rate, int64_t sse, int64_t rd,
                                         RD_STATS *rd_cost, RD_STATS *rd_cost_y,
                                         RD_STATS *rd_cost_uv,
                                         const MB_MODE_INFO *mbmi) {
  const int num = inter_modes_info->num;
  assert(num < MAX_INTER_MODES);
  inter_modes_info->mbmi_arr[num] = *mbmi;
  inter_modes_info->mode_rate_arr[num] = mode_rate;
  inter_modes_info->sse_arr[num] = sse;
  inter_modes_info->est_rd_arr[num] = rd;
  inter_modes_info->rd_cost_arr[num] = *rd_cost;
  inter_modes_info->rd_cost_y_arr[num] = *rd_cost_y;
  inter_modes_info->rd_cost_uv_arr[num] = *rd_cost_uv;
  ++inter_modes_info->num;
}

static int compare_rd_idx_pair(const void *a, const void *b) {
  if (((RdIdxPair *)a)->rd == ((RdIdxPair *)b)->rd) {
    // To avoid inconsistency in qsort() ordering when two elements are equal,
    // using idx as tie breaker. Refer aomedia:2928
    if (((RdIdxPair *)a)->idx == ((RdIdxPair *)b)->idx)
      return 0;
    else if (((RdIdxPair *)a)->idx > ((RdIdxPair *)b)->idx)
      return 1;
    else
      return -1;
  } else if (((const RdIdxPair *)a)->rd > ((const RdIdxPair *)b)->rd) {
    return 1;
  } else {
    return -1;
  }
}

static inline void inter_modes_info_sort(const InterModesInfo *inter_modes_info,
                                         RdIdxPair *rd_idx_pair_arr) {
  if (inter_modes_info->num == 0) {
    return;
  }
  for (int i = 0; i < inter_modes_info->num; ++i) {
    rd_idx_pair_arr[i].idx = i;
    rd_idx_pair_arr[i].rd = inter_modes_info->est_rd_arr[i];
  }
  qsort(rd_idx_pair_arr, inter_modes_info->num, sizeof(rd_idx_pair_arr[0]),
        compare_rd_idx_pair);
}

// Similar to get_horver_correlation, but also takes into account first
// row/column, when computing horizontal/vertical correlation.
void av1_get_horver_correlation_full_c(const int16_t *diff, int stride,
                                       int width, int height, float *hcorr,
                                       float *vcorr) {
  // The following notation is used:
  // x - current pixel
  // y - left neighbor pixel
  // z - top neighbor pixel
  int64_t x_sum = 0, x2_sum = 0, xy_sum = 0, xz_sum = 0;
  int64_t x_firstrow = 0, x_finalrow = 0, x_firstcol = 0, x_finalcol = 0;
  int64_t x2_firstrow = 0, x2_finalrow = 0, x2_firstcol = 0, x2_finalcol = 0;

  // First, process horizontal correlation on just the first row
  x_sum += diff[0];
  x2_sum += diff[0] * diff[0];
  x_firstrow += diff[0];
  x2_firstrow += diff[0] * diff[0];
  for (int j = 1; j < width; ++j) {
    const int16_t x = diff[j];
    const int16_t y = diff[j - 1];
    x_sum += x;
    x_firstrow += x;
    x2_sum += x * x;
    x2_firstrow += x * x;
    xy_sum += x * y;
  }

  // Process vertical correlation in the first column
  x_firstcol += diff[0];
  x2_firstcol += diff[0] * diff[0];
  for (int i = 1; i < height; ++i) {
    const int16_t x = diff[i * stride];
    const int16_t z = diff[(i - 1) * stride];
    x_sum += x;
    x_firstcol += x;
    x2_sum += x * x;
    x2_firstcol += x * x;
    xz_sum += x * z;
  }

  // Now process horiz and vert correlation through the rest unit
  for (int i = 1; i < height; ++i) {
    for (int j = 1; j < width; ++j) {
      const int16_t x = diff[i * stride + j];
      const int16_t y = diff[i * stride + j - 1];
      const int16_t z = diff[(i - 1) * stride + j];
      x_sum += x;
      x2_sum += x * x;
      xy_sum += x * y;
      xz_sum += x * z;
    }
  }

  for (int j = 0; j < width; ++j) {
    x_finalrow += diff[(height - 1) * stride + j];
    x2_finalrow +=
        diff[(height - 1) * stride + j] * diff[(height - 1) * stride + j];
  }
  for (int i = 0; i < height; ++i) {
    x_finalcol += diff[i * stride + width - 1];
    x2_finalcol += diff[i * stride + width - 1] * diff[i * stride + width - 1];
  }

  int64_t xhor_sum = x_sum - x_finalcol;
  int64_t xver_sum = x_sum - x_finalrow;
  int64_t y_sum = x_sum - x_firstcol;
  int64_t z_sum = x_sum - x_firstrow;
  int64_t x2hor_sum = x2_sum - x2_finalcol;
  int64_t x2ver_sum = x2_sum - x2_finalrow;
  int64_t y2_sum = x2_sum - x2_firstcol;
  int64_t z2_sum = x2_sum - x2_firstrow;

  const float num_hor = (float)(height * (width - 1));
  const float num_ver = (float)((height - 1) * width);

  const float xhor_var_n = x2hor_sum - (xhor_sum * xhor_sum) / num_hor;
  const float xver_var_n = x2ver_sum - (xver_sum * xver_sum) / num_ver;

  const float y_var_n = y2_sum - (y_sum * y_sum) / num_hor;
  const float z_var_n = z2_sum - (z_sum * z_sum) / num_ver;

  const float xy_var_n = xy_sum - (xhor_sum * y_sum) / num_hor;
  const float xz_var_n = xz_sum - (xver_sum * z_sum) / num_ver;

  if (xhor_var_n > 0 && y_var_n > 0) {
    *hcorr = xy_var_n / sqrtf(xhor_var_n * y_var_n);
    *hcorr = *hcorr < 0 ? 0 : *hcorr;
  } else {
    *hcorr = 1.0;
  }
  if (xver_var_n > 0 && z_var_n > 0) {
    *vcorr = xz_var_n / sqrtf(xver_var_n * z_var_n);
    *vcorr = *vcorr < 0 ? 0 : *vcorr;
  } else {
    *vcorr = 1.0;
  }
}

static int64_t get_sse(const AV1_COMP *cpi, const MACROBLOCK *x,
                       int64_t *sse_y) {
  const AV1_COMMON *cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  const MACROBLOCKD *xd = &x->e_mbd;
  const MB_MODE_INFO *mbmi = xd->mi[0];
  int64_t total_sse = 0;
  for (int plane = 0; plane < num_planes; ++plane) {
    if (plane && !xd->is_chroma_ref) break;
    const struct macroblock_plane *const p = &x->plane[plane];
    const struct macroblockd_plane *const pd = &xd->plane[plane];
    const BLOCK_SIZE bs =
        get_plane_block_size(mbmi->bsize, pd->subsampling_x, pd->subsampling_y);
    unsigned int sse;

    cpi->ppi->fn_ptr[bs].vf(p->src.buf, p->src.stride, pd->dst.buf,
                            pd->dst.stride, &sse);
    total_sse += sse;
    if (!plane && sse_y) *sse_y = sse;
  }
  total_sse <<= 4;
  return total_sse;
}

int64_t av1_block_error_c(const tran_low_t *coeff, const tran_low_t *dqcoeff,
                          intptr_t block_size, int64_t *ssz) {
  int i;
  int64_t error = 0, sqcoeff = 0;

  for (i = 0; i < block_size; i++) {
    const int diff = coeff[i] - dqcoeff[i];
    error += diff * diff;
    sqcoeff += coeff[i] * coeff[i];
  }

  *ssz = sqcoeff;
  return error;
}

int64_t av1_block_error_lp_c(const int16_t *coeff, const int16_t *dqcoeff,
                             intptr_t block_size) {
  int64_t error = 0;

  for (int i = 0; i < block_size; i++) {
    const int diff = coeff[i] - dqcoeff[i];
    error += diff * diff;
  }

  return error;
}

#if CONFIG_AV1_HIGHBITDEPTH
int64_t av1_highbd_block_error_c(const tran_low_t *coeff,
                                 const tran_low_t *dqcoeff, intptr_t block_size,
                                 int64_t *ssz, int bd) {
  int i;
  int64_t error = 0, sqcoeff = 0;
  int shift = 2 * (bd - 8);
  int rounding = (1 << shift) >> 1;

  for (i = 0; i < block_size; i++) {
    const int64_t diff = coeff[i] - dqcoeff[i];
    error += diff * diff;
    sqcoeff += (int64_t)coeff[i] * (int64_t)coeff[i];
  }
  error = (error + rounding) >> shift;
  sqcoeff = (sqcoeff + rounding) >> shift;

  *ssz = sqcoeff;
  return error;
}
#endif

static int conditional_skipintra(PREDICTION_MODE mode,
                                 PREDICTION_MODE best_intra_mode) {
  if (mode == D113_PRED && best_intra_mode != V_PRED &&
      best_intra_mode != D135_PRED)
    return 1;
  if (mode == D67_PRED && best_intra_mode != V_PRED &&
      best_intra_mode != D45_PRED)
    return 1;
  if (mode == D203_PRED && best_intra_mode != H_PRED &&
      best_intra_mode != D45_PRED)
    return 1;
  if (mode == D157_PRED && best_intra_mode != H_PRED &&
      best_intra_mode != D135_PRED)
    return 1;
  return 0;
}

static int cost_mv_ref(const ModeCosts *const mode_costs, PREDICTION_MODE mode,
                       int16_t mode_context) {
  if (is_inter_compound_mode(mode)) {
    return mode_costs
        ->inter_compound_mode_cost[mode_context][INTER_COMPOUND_OFFSET(mode)];
  }

  int mode_cost = 0;
  int16_t mode_ctx = mode_context & NEWMV_CTX_MASK;

  assert(is_inter_mode(mode));

  if (mode == NEWMV) {
    mode_cost = mode_costs->newmv_mode_cost[mode_ctx][0];
    return mode_cost;
  } else {
    mode_cost = mode_costs->newmv_mode_cost[mode_ctx][1];
    mode_ctx = (mode_context >> GLOBALMV_OFFSET) & GLOBALMV_CTX_MASK;

    if (mode == GLOBALMV) {
      mode_cost += mode_costs->zeromv_mode_cost[mode_ctx][0];
      return mode_cost;
    } else {
      mode_cost += mode_costs->zeromv_mode_cost[mode_ctx][1];
      mode_ctx = (mode_context >> REFMV_OFFSET) & REFMV_CTX_MASK;
      mode_cost += mode_costs->refmv_mode_cost[mode_ctx][mode != NEARESTMV];
      return mode_cost;
    }
  }
}

static inline PREDICTION_MODE get_single_mode(PREDICTION_MODE this_mode,
                                              int ref_idx) {
  return ref_idx ? compound_ref1_mode(this_mode)
                 : compound_ref0_mode(this_mode);
}

static inline void estimate_ref_frame_costs(
    const AV1_COMMON *cm, const MACROBLOCKD *xd, const ModeCosts *mode_costs,
    int segment_id, unsigned int *ref_costs_single,
    unsigned int (*ref_costs_comp)[REF_FRAMES]) {
  int seg_ref_active =
      segfeature_active(&cm->seg, segment_id, SEG_LVL_REF_FRAME);
  if (seg_ref_active) {
    memset(ref_costs_single, 0, REF_FRAMES * sizeof(*ref_costs_single));
    int ref_frame;
    for (ref_frame = 0; ref_frame < REF_FRAMES; ++ref_frame)
      memset(ref_costs_comp[ref_frame], 0,
             REF_FRAMES * sizeof((*ref_costs_comp)[0]));
  } else {
    int intra_inter_ctx = av1_get_intra_inter_context(xd);
    ref_costs_single[INTRA_FRAME] =
        mode_costs->intra_inter_cost[intra_inter_ctx][0];
    unsigned int base_cost = mode_costs->intra_inter_cost[intra_inter_ctx][1];

    for (int i = LAST_FRAME; i <= ALTREF_FRAME; ++i)
      ref_costs_single[i] = base_cost;

    const int ctx_p1 = av1_get_pred_context_single_ref_p1(xd);
    const int ctx_p2 = av1_get_pred_context_single_ref_p2(xd);
    const int ctx_p3 = av1_get_pred_context_single_ref_p3(xd);
    const int ctx_p4 = av1_get_pred_context_single_ref_p4(xd);
    const int ctx_p5 = av1_get_pred_context_single_ref_p5(xd);
    const int ctx_p6 = av1_get_pred_context_single_ref_p6(xd);

    // Determine cost of a single ref frame, where frame types are represented
    // by a tree:
    // Level 0: add cost whether this ref is a forward or backward ref
    ref_costs_single[LAST_FRAME] += mode_costs->single_ref_cost[ctx_p1][0][0];
    ref_costs_single[LAST2_FRAME] += mode_costs->single_ref_cost[ctx_p1][0][0];
    ref_costs_single[LAST3_FRAME] += mode_costs->single_ref_cost[ctx_p1][0][0];
    ref_costs_single[GOLDEN_FRAME] += mode_costs->single_ref_cost[ctx_p1][0][0];
    ref_costs_single[BWDREF_FRAME] += mode_costs->single_ref_cost[ctx_p1][0][1];
    ref_costs_single[ALTREF2_FRAME] +=
        mode_costs->single_ref_cost[ctx_p1][0][1];
    ref_costs_single[ALTREF_FRAME] += mode_costs->single_ref_cost[ctx_p1][0][1];

    // Level 1: if this ref is forward ref,
    // add cost whether it is last/last2 or last3/golden
    ref_costs_single[LAST_FRAME] += mode_costs->single_ref_cost[ctx_p3][2][0];
    ref_costs_single[LAST2_FRAME] += mode_costs->single_ref_cost[ctx_p3][2][0];
    ref_costs_single[LAST3_FRAME] += mode_costs->single_ref_cost[ctx_p3][2][1];
    ref_costs_single[GOLDEN_FRAME] += mode_costs->single_ref_cost[ctx_p3][2][1];

    // Level 1: if this ref is backward ref
    // then add cost whether this ref is altref or backward ref
    ref_costs_single[BWDREF_FRAME] += mode_costs->single_ref_cost[ctx_p2][1][0];
    ref_costs_single[ALTREF2_FRAME] +=
        mode_costs->single_ref_cost[ctx_p2][1][0];
    ref_costs_single[ALTREF_FRAME] += mode_costs->single_ref_cost[ctx_p2][1][1];

    // Level 2: further add cost whether this ref is last or last2
    ref_costs_single[LAST_FRAME] += mode_costs->single_ref_cost[ctx_p4][3][0];
    ref_costs_single[LAST2_FRAME] += mode_costs->single_ref_cost[ctx_p4][3][1];

    // Level 2: last3 or golden
    ref_costs_single[LAST3_FRAME] += mode_costs->single_ref_cost[ctx_p5][4][0];
    ref_costs_single[GOLDEN_FRAME] += mode_costs->single_ref_cost[ctx_p5][4][1];

    // Level 2: bwdref or altref2
    ref_costs_single[BWDREF_FRAME] += mode_costs->single_ref_cost[ctx_p6][5][0];
    ref_costs_single[ALTREF2_FRAME] +=
        mode_costs->single_ref_cost[ctx_p6][5][1];

    if (cm->current_frame.reference_mode != SINGLE_REFERENCE) {
      // Similar to single ref, determine cost of compound ref frames.
      // cost_compound_refs = cost_first_ref + cost_second_ref
      const int bwdref_comp_ctx_p = av1_get_pred_context_comp_bwdref_p(xd);
      const int bwdref_comp_ctx_p1 = av1_get_pred_context_comp_bwdref_p1(xd);
      const int ref_comp_ctx_p = av1_get_pred_context_comp_ref_p(xd);
      const int ref_comp_ctx_p1 = av1_get_pred_context_comp_ref_p1(xd);
      const int ref_comp_ctx_p2 = av1_get_pred_context_comp_ref_p2(xd);

      const int comp_ref_type_ctx = av1_get_comp_reference_type_context(xd);
      unsigned int ref_bicomp_costs[REF_FRAMES] = { 0 };

      ref_bicomp_costs[LAST_FRAME] = ref_bicomp_costs[LAST2_FRAME] =
          ref_bicomp_costs[LAST3_FRAME] = ref_bicomp_costs[GOLDEN_FRAME] =
              base_cost + mode_costs->comp_ref_type_cost[comp_ref_type_ctx][1];
      ref_bicomp_costs[BWDREF_FRAME] = ref_bicomp_costs[ALTREF2_FRAME] = 0;
      ref_bicomp_costs[ALTREF_FRAME] = 0;

      // cost of first ref frame
      ref_bicomp_costs[LAST_FRAME] +=
          mode_costs->comp_ref_cost[ref_comp_ctx_p][0][0];
      ref_bicomp_costs[LAST2_FRAME] +=
          mode_costs->comp_ref_cost[ref_comp_ctx_p][0][0];
      ref_bicomp_costs[LAST3_FRAME] +=
          mode_costs->comp_ref_cost[ref_comp_ctx_p][0][1];
      ref_bicomp_costs[GOLDEN_FRAME] +=
          mode_costs->comp_ref_cost[ref_comp_ctx_p][0][1];

      ref_bicomp_costs[LAST_FRAME] +=
          mode_costs->comp_ref_cost[ref_comp_ctx_p1][1][0];
      ref_bicomp_costs[LAST2_FRAME] +=
          mode_costs->comp_ref_cost[ref_comp_ctx_p1][1][1];

      ref_bicomp_costs[LAST3_FRAME] +=
          mode_costs->comp_ref_cost[ref_comp_ctx_p2][2][0];
      ref_bicomp_costs[GOLDEN_FRAME] +=
          mode_costs->comp_ref_cost[ref_comp_ctx_p2][2][1];

      // cost of second ref frame
      ref_bicomp_costs[BWDREF_FRAME] +=
          mode_costs->comp_bwdref_cost[bwdref_comp_ctx_p][0][0];
      ref_bicomp_costs[ALTREF2_FRAME] +=
          mode_costs->comp_bwdref_cost[bwdref_comp_ctx_p][0][0];
      ref_bicomp_costs[ALTREF_FRAME] +=
          mode_costs->comp_bwdref_cost[bwdref_comp_ctx_p][0][1];

      ref_bicomp_costs[BWDREF_FRAME] +=
          mode_costs->comp_bwdref_cost[bwdref_comp_ctx_p1][1][0];
      ref_bicomp_costs[ALTREF2_FRAME] +=
          mode_costs->comp_bwdref_cost[bwdref_comp_ctx_p1][1][1];

      // cost: if one ref frame is forward ref, the other ref is backward ref
      int ref0, ref1;
      for (ref0 = LAST_FRAME; ref0 <= GOLDEN_FRAME; ++ref0) {
        for (ref1 = BWDREF_FRAME; ref1 <= ALTREF_FRAME; ++ref1) {
          ref_costs_comp[ref0][ref1] =
              ref_bicomp_costs[ref0] + ref_bicomp_costs[ref1];
        }
      }

      // cost: if both ref frames are the same side.
      const int uni_comp_ref_ctx_p = av1_get_pred_context_uni_comp_ref_p(xd);
      const int uni_comp_ref_ctx_p1 = av1_get_pred_context_uni_comp_ref_p1(xd);
      const int uni_comp_ref_ctx_p2 = av1_get_pred_context_uni_comp_ref_p2(xd);
      ref_costs_comp[LAST_FRAME][LAST2_FRAME] =
          base_cost + mode_costs->comp_ref_type_cost[comp_ref_type_ctx][0] +
          mode_costs->uni_comp_ref_cost[uni_comp_ref_ctx_p][0][0] +
          mode_costs->uni_comp_ref_cost[uni_comp_ref_ctx_p1][1][0];
      ref_costs_comp[LAST_FRAME][LAST3_FRAME] =
          base_cost + mode_costs->comp_ref_type_cost[comp_ref_type_ctx][0] +
          mode_costs->uni_comp_ref_cost[uni_comp_ref_ctx_p][0][0] +
          mode_costs->uni_comp_ref_cost[uni_comp_ref_ctx_p1][1][1] +
          mode_costs->uni_comp_ref_cost[uni_comp_ref_ctx_p2][2][0];
      ref_costs_comp[LAST_FRAME][GOLDEN_FRAME] =
          base_cost + mode_costs->comp_ref_type_cost[comp_ref_type_ctx][0] +
          mode_costs->uni_comp_ref_cost[uni_comp_ref_ctx_p][0][0] +
          mode_costs->uni_comp_ref_cost[uni_comp_ref_ctx_p1][1][1] +
          mode_costs->uni_comp_ref_cost[uni_comp_ref_ctx_p2][2][1];
      ref_costs_comp[BWDREF_FRAME][ALTREF_FRAME] =
          base_cost + mode_costs->comp_ref_type_cost[comp_ref_type_ctx][0] +
          mode_costs->uni_comp_ref_cost[uni_comp_ref_ctx_p][0][1];
    } else {
      int ref0, ref1;
      for (ref0 = LAST_FRAME; ref0 <= GOLDEN_FRAME; ++ref0) {
        for (ref1 = BWDREF_FRAME; ref1 <= ALTREF_FRAME; ++ref1)
          ref_costs_comp[ref0][ref1] = 512;
      }
      ref_costs_comp[LAST_FRAME][LAST2_FRAME] = 512;
      ref_costs_comp[LAST_FRAME][LAST3_FRAME] = 512;
      ref_costs_comp[LAST_FRAME][GOLDEN_FRAME] = 512;
      ref_costs_comp[BWDREF_FRAME][ALTREF_FRAME] = 512;
    }
  }
}

static inline void store_coding_context(
#if CONFIG_INTERNAL_STATS
    MACROBLOCK *x, PICK_MODE_CONTEXT *ctx, int mode_index,
#else
    MACROBLOCK *x, PICK_MODE_CONTEXT *ctx,
#endif  // CONFIG_INTERNAL_STATS
    int skippable) {
  MACROBLOCKD *const xd = &x->e_mbd;

  // Take a snapshot of the coding context so it can be
  // restored if we decide to encode this way
  ctx->rd_stats.skip_txfm = x->txfm_search_info.skip_txfm;
  ctx->skippable = skippable;
#if CONFIG_INTERNAL_STATS
  ctx->best_mode_index = mode_index;
#endif  // CONFIG_INTERNAL_STATS
  ctx->mic = *xd->mi[0];
  av1_copy_mbmi_ext_to_mbmi_ext_frame(&ctx->mbmi_ext_best, &x->mbmi_ext,
                                      av1_ref_frame_type(xd->mi[0]->ref_frame));
}

static inline void setup_buffer_ref_mvs_inter(
    const AV1_COMP *const cpi, MACROBLOCK *x, MV_REFERENCE_FRAME ref_frame,
    BLOCK_SIZE block_size, struct buf_2d yv12_mb[REF_FRAMES][MAX_MB_PLANE]) {
  const AV1_COMMON *cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  const YV12_BUFFER_CONFIG *scaled_ref_frame =
      av1_get_scaled_ref_frame(cpi, ref_frame);
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  MB_MODE_INFO_EXT *const mbmi_ext = &x->mbmi_ext;
  const struct scale_factors *const sf =
      get_ref_scale_factors_const(cm, ref_frame);
  const YV12_BUFFER_CONFIG *yv12 = get_ref_frame_yv12_buf(cm, ref_frame);
  assert(yv12 != NULL);

  if (scaled_ref_frame) {
    // Setup pred block based on scaled reference, because av1_mv_pred() doesn't
    // support scaling.
    av1_setup_pred_block(xd, yv12_mb[ref_frame], scaled_ref_frame, NULL, NULL,
                         num_planes);
  } else {
    av1_setup_pred_block(xd, yv12_mb[ref_frame], yv12, sf, sf, num_planes);
  }

  // Gets an initial list of candidate vectors from neighbours and orders them
  av1_find_mv_refs(cm, xd, mbmi, ref_frame, mbmi_ext->ref_mv_count,
                   xd->ref_mv_stack, xd->weight, NULL, mbmi_ext->global_mvs,
                   mbmi_ext->mode_context);
  // TODO(Ravi): Populate mbmi_ext->ref_mv_stack[ref_frame][4] and
  // mbmi_ext->weight[ref_frame][4] inside av1_find_mv_refs.
  av1_copy_usable_ref_mv_stack_and_weight(xd, mbmi_ext, ref_frame);
  // Further refinement that is encode side only to test the top few candidates
  // in full and choose the best as the center point for subsequent searches.
  // The current implementation doesn't support scaling.
  av1_mv_pred(cpi, x, yv12_mb[ref_frame][0].buf, yv12_mb[ref_frame][0].stride,
              ref_frame, block_size);

  // Go back to unscaled reference.
  if (scaled_ref_frame) {
    // We had temporarily setup pred block based on scaled reference above. Go
    // back to unscaled reference now, for subsequent use.
    av1_setup_pred_block(xd, yv12_mb[ref_frame], yv12, sf, sf, num_planes);
  }
}

#define LEFT_TOP_MARGIN ((AOM_BORDER_IN_PIXELS - AOM_INTERP_EXTEND) << 3)
#define RIGHT_BOTTOM_MARGIN ((AOM_BORDER_IN_PIXELS - AOM_INTERP_EXTEND) << 3)

// TODO(jingning): this mv clamping function should be block size dependent.
static inline void clamp_mv2(MV *mv, const MACROBLOCKD *xd) {
  const SubpelMvLimits mv_limits = { xd->mb_to_left_edge - LEFT_TOP_MARGIN,
                                     xd->mb_to_right_edge + RIGHT_BOTTOM_MARGIN,
                                     xd->mb_to_top_edge - LEFT_TOP_MARGIN,
                                     xd->mb_to_bottom_edge +
                                         RIGHT_BOTTOM_MARGIN };
  clamp_mv(mv, &mv_limits);
}

/* If the current mode shares the same mv with other modes with higher cost,
 * skip this mode. */
static int skip_repeated_mv(const AV1_COMMON *const cm,
                            const MACROBLOCK *const x,
                            PREDICTION_MODE this_mode,
                            const MV_REFERENCE_FRAME ref_frames[2],
                            InterModeSearchState *search_state) {
  const int is_comp_pred = ref_frames[1] > INTRA_FRAME;
  const uint8_t ref_frame_type = av1_ref_frame_type(ref_frames);
  const MB_MODE_INFO_EXT *const mbmi_ext = &x->mbmi_ext;
  const int ref_mv_count = mbmi_ext->ref_mv_count[ref_frame_type];
  PREDICTION_MODE compare_mode = MB_MODE_COUNT;
  if (!is_comp_pred) {
    if (this_mode == NEARMV) {
      if (ref_mv_count == 0) {
        // NEARMV has the same motion vector as NEARESTMV
        compare_mode = NEARESTMV;
      }
      if (ref_mv_count == 1 &&
          cm->global_motion[ref_frames[0]].wmtype <= TRANSLATION) {
        // NEARMV has the same motion vector as GLOBALMV
        compare_mode = GLOBALMV;
      }
    }
    if (this_mode == GLOBALMV) {
      if (ref_mv_count == 0 &&
          cm->global_motion[ref_frames[0]].wmtype <= TRANSLATION) {
        // GLOBALMV has the same motion vector as NEARESTMV
        compare_mode = NEARESTMV;
      }
      if (ref_mv_count == 1) {
        // GLOBALMV has the same motion vector as NEARMV
        compare_mode = NEARMV;
      }
    }

    if (compare_mode != MB_MODE_COUNT) {
      // Use modelled_rd to check whether compare mode was searched
      if (search_state->modelled_rd[compare_mode][0][ref_frames[0]] !=
          INT64_MAX) {
        const int16_t mode_ctx =
            av1_mode_context_analyzer(mbmi_ext->mode_context, ref_frames);
        const int compare_cost =
            cost_mv_ref(&x->mode_costs, compare_mode, mode_ctx);
        const int this_cost = cost_mv_ref(&x->mode_costs, this_mode, mode_ctx);

        // Only skip if the mode cost is larger than compare mode cost
        if (this_cost > compare_cost) {
          search_state->modelled_rd[this_mode][0][ref_frames[0]] =
              search_state->modelled_rd[compare_mode][0][ref_frames[0]];
          return 1;
        }
      }
    }
  }
  return 0;
}

static inline int clamp_and_check_mv(int_mv *out_mv, int_mv in_mv,
                                     const AV1_COMMON *cm,
                                     const MACROBLOCK *x) {
  const MACROBLOCKD *const xd = &x->e_mbd;
  *out_mv = in_mv;
  lower_mv_precision(&out_mv->as_mv, cm->features.allow_high_precision_mv,
                     cm->features.cur_frame_force_integer_mv);
  clamp_mv2(&out_mv->as_mv, xd);
  return av1_is_fullmv_in_range(&x->mv_limits,
                                get_fullmv_from_mv(&out_mv->as_mv));
}

// To use single newmv directly for compound modes, need to clamp the mv to the
// valid mv range. Without this, encoder would generate out of range mv, and
// this is seen in 8k encoding.
static inline void clamp_mv_in_range(MACROBLOCK *const x, int_mv *mv,
                                     int ref_idx) {
  const int_mv ref_mv = av1_get_ref_mv(x, ref_idx);
  SubpelMvLimits mv_limits;

  av1_set_subpel_mv_search_range(&mv_limits, &x->mv_limits, &ref_mv.as_mv);
  clamp_mv(&mv->as_mv, &mv_limits);
}

static int64_t handle_newmv(const AV1_COMP *const cpi, MACROBLOCK *const x,
                            const BLOCK_SIZE bsize, int_mv *cur_mv,
                            int *const rate_mv, HandleInterModeArgs *const args,
                            inter_mode_info *mode_info) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int is_comp_pred = has_second_ref(mbmi);
  const PREDICTION_MODE this_mode = mbmi->mode;
  const int refs[2] = { mbmi->ref_frame[0],
                        mbmi->ref_frame[1] < 0 ? 0 : mbmi->ref_frame[1] };
  const int ref_mv_idx = mbmi->ref_mv_idx;

  if (is_comp_pred) {
    const int valid_mv0 = args->single_newmv_valid[ref_mv_idx][refs[0]];
    const int valid_mv1 = args->single_newmv_valid[ref_mv_idx][refs[1]];
    if (this_mode == NEW_NEWMV) {
      if (valid_mv0) {
        cur_mv[0].as_int = args->single_newmv[ref_mv_idx][refs[0]].as_int;
        clamp_mv_in_range(x, &cur_mv[0], 0);
      }
      if (valid_mv1) {
        cur_mv[1].as_int = args->single_newmv[ref_mv_idx][refs[1]].as_int;
        clamp_mv_in_range(x, &cur_mv[1], 1);
      }
      *rate_mv = 0;
      for (int i = 0; i < 2; ++i) {
        const int_mv ref_mv = av1_get_ref_mv(x, i);
        *rate_mv += av1_mv_bit_cost(&cur_mv[i].as_mv, &ref_mv.as_mv,
                                    x->mv_costs->nmv_joint_cost,
                                    x->mv_costs->mv_cost_stack, MV_COST_WEIGHT);
      }
    } else if (this_mode == NEAREST_NEWMV || this_mode == NEAR_NEWMV) {
      if (valid_mv1) {
        cur_mv[1].as_int = args->single_newmv[ref_mv_idx][refs[1]].as_int;
        clamp_mv_in_range(x, &cur_mv[1], 1);
      }
      const int_mv ref_mv = av1_get_ref_mv(x, 1);
      *rate_mv = av1_mv_bit_cost(&cur_mv[1].as_mv, &ref_mv.as_mv,
                                 x->mv_costs->nmv_joint_cost,
                                 x->mv_costs->mv_cost_stack, MV_COST_WEIGHT);
    } else {
      assert(this_mode == NEW_NEARESTMV || this_mode == NEW_NEARMV);
      if (valid_mv0) {
        cur_mv[0].as_int = args->single_newmv[ref_mv_idx][refs[0]].as_int;
        clamp_mv_in_range(x, &cur_mv[0], 0);
      }
      const int_mv ref_mv = av1_get_ref_mv(x, 0);
      *rate_mv = av1_mv_bit_cost(&cur_mv[0].as_mv, &ref_mv.as_mv,
                                 x->mv_costs->nmv_joint_cost,
                                 x->mv_costs->mv_cost_stack, MV_COST_WEIGHT);
    }
  } else {
    // Single ref case.
    const int ref_idx = 0;
    int search_range = INT_MAX;

    if (cpi->sf.mv_sf.reduce_search_range && mbmi->ref_mv_idx > 0) {
      const MV ref_mv = av1_get_ref_mv(x, ref_idx).as_mv;
      int min_mv_diff = INT_MAX;
      int best_match = -1;
      MV prev_ref_mv[2] = { { 0 } };
      for (int idx = 0; idx < mbmi->ref_mv_idx; ++idx) {
        prev_ref_mv[idx] = av1_get_ref_mv_from_stack(ref_idx, mbmi->ref_frame,
                                                     idx, &x->mbmi_ext)
                               .as_mv;
        const int ref_mv_diff = AOMMAX(abs(ref_mv.row - prev_ref_mv[idx].row),
                                       abs(ref_mv.col - prev_ref_mv[idx].col));

        if (min_mv_diff > ref_mv_diff) {
          min_mv_diff = ref_mv_diff;
          best_match = idx;
        }
      }

      if (min_mv_diff < (16 << 3)) {
        if (args->single_newmv_valid[best_match][refs[0]]) {
          search_range = min_mv_diff;
          search_range +=
              AOMMAX(abs(args->single_newmv[best_match][refs[0]].as_mv.row -
                         prev_ref_mv[best_match].row),
                     abs(args->single_newmv[best_match][refs[0]].as_mv.col -
                         prev_ref_mv[best_match].col));
          // Get full pixel search range.
          search_range = (search_range + 4) >> 3;
        }
      }
    }

    int_mv best_mv;
    av1_single_motion_search(cpi, x, bsize, ref_idx, rate_mv, search_range,
                             mode_info, &best_mv, args);
    if (best_mv.as_int == INVALID_MV) return INT64_MAX;

    args->single_newmv[ref_mv_idx][refs[0]] = best_mv;
    args->single_newmv_rate[ref_mv_idx][refs[0]] = *rate_mv;
    args->single_newmv_valid[ref_mv_idx][refs[0]] = 1;
    cur_mv[0].as_int = best_mv.as_int;

    // Return after single_newmv is set.
    if (mode_info[mbmi->ref_mv_idx].skip) return INT64_MAX;
  }

  return 0;
}

static inline void update_mode_start_end_index(
    const AV1_COMP *const cpi, const MB_MODE_INFO *const mbmi,
    int *mode_index_start, int *mode_index_end, int last_motion_mode_allowed,
    int interintra_allowed, int eval_motion_mode) {
  *mode_index_start = (int)SIMPLE_TRANSLATION;
  *mode_index_end = (int)last_motion_mode_allowed + interintra_allowed;
  if (cpi->sf.winner_mode_sf.motion_mode_for_winner_cand) {
    if (!eval_motion_mode) {
      *mode_index_end = (int)SIMPLE_TRANSLATION;
    } else {
      // Set the start index appropriately to process motion modes other than
      // simple translation
      *mode_index_start = 1;
    }
  }
  if (cpi->sf.inter_sf.extra_prune_warped && mbmi->bsize > BLOCK_16X16)
    *mode_index_end = SIMPLE_TRANSLATION;
}

/*!\brief AV1 motion mode search
 *
 * \ingroup inter_mode_search
 * Function to search over and determine the motion mode. It will update
 * mbmi->motion_mode to one of SIMPLE_TRANSLATION, OBMC_CAUSAL, or
 * WARPED_CAUSAL and determine any necessary side information for the selected
 * motion mode. It will also perform the full transform search, unless the
 * input parameter do_tx_search indicates to do an estimation of the RD rather
 * than an RD corresponding to a full transform search. It will return the
 * RD for the final motion_mode.
 * Do the RD search for a given inter mode and compute all information relevant
 * to the input mode. It will compute the best MV,
 * compound parameters (if the mode is a compound mode) and interpolation filter
 * parameters.
 *
 * \param[in]     cpi               Top-level encoder structure.
 * \param[in]     tile_data         Pointer to struct holding adaptive
 *                                  data/contexts/models for the tile during
 *                                  encoding.
 * \param[in]     x                 Pointer to struct holding all the data for
 *                                  the current macroblock.
 * \param[in]     bsize             Current block size.
 * \param[in,out] rd_stats          Struct to keep track of the overall RD
 *                                  information.
 * \param[in,out] rd_stats_y        Struct to keep track of the RD information
 *                                  for only the Y plane.
 * \param[in,out] rd_stats_uv       Struct to keep track of the RD information
 *                                  for only the UV planes.
 * \param[in]     args              HandleInterModeArgs struct holding
 *                                  miscellaneous arguments for inter mode
 *                                  search. See the documentation for this
 *                                  struct for a description of each member.
 * \param[in]     ref_best_rd       Best RD found so far for this block.
 *                                  It is used for early termination of this
 *                                  search if the RD exceeds this value.
 * \param[in,out] ref_skip_rd       A length 2 array, where skip_rd[0] is the
 *                                  best total RD for a skip mode so far, and
 *                                  skip_rd[1] is the best RD for a skip mode so
 *                                  far in luma. This is used as a speed feature
 *                                  to skip the transform search if the computed
 *                                  skip RD for the current mode is not better
 *                                  than the best skip_rd so far.
 * \param[in,out] rate_mv           The rate associated with the motion vectors.
 *                                  This will be modified if a motion search is
 *                                  done in the motion mode search.
 * \param[in,out] orig_dst          A prediction buffer to hold a computed
 *                                  prediction. This will eventually hold the
 *                                  final prediction, and the tmp_dst info will
 *                                  be copied here.
 * \param[in,out] best_est_rd       Estimated RD for motion mode search if
 *                                  do_tx_search (see below) is 0.
 * \param[in]     do_tx_search      Parameter to indicate whether or not to do
 *                                  a full transform search. This will compute
 *                                  an estimated RD for the modes without the
 *                                  transform search and later perform the full
 *                                  transform search on the best candidates.
 * \param[in]     inter_modes_info  InterModesInfo struct to hold inter mode
 *                                  information to perform a full transform
 *                                  search only on winning candidates searched
 *                                  with an estimate for transform coding RD.
 * \param[in]     eval_motion_mode  Boolean whether or not to evaluate motion
 *                                  motion modes other than SIMPLE_TRANSLATION.
 * \param[out]    yrd               Stores the rdcost corresponding to encoding
 *                                  the luma plane.
 * \return Returns INT64_MAX if the determined motion mode is invalid and the
 * current motion mode being tested should be skipped. It returns 0 if the
 * motion mode search is a success.
 */
static int64_t motion_mode_rd(
    const AV1_COMP *const cpi, TileDataEnc *tile_data, MACROBLOCK *const x,
    BLOCK_SIZE bsize, RD_STATS *rd_stats, RD_STATS *rd_stats_y,
    RD_STATS *rd_stats_uv, HandleInterModeArgs *const args, int64_t ref_best_rd,
    int64_t *ref_skip_rd, int *rate_mv, const BUFFER_SET *orig_dst,
    int64_t *best_est_rd, int do_tx_search, InterModesInfo *inter_modes_info,
    int eval_motion_mode, int64_t *yrd) {
  const AV1_COMMON *const cm = &cpi->common;
  const FeatureFlags *const features = &cm->features;
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const int is_comp_pred = has_second_ref(mbmi);
  const PREDICTION_MODE this_mode = mbmi->mode;
  const int rate2_nocoeff = rd_stats->rate;
  int best_xskip_txfm = 0;
  RD_STATS best_rd_stats, best_rd_stats_y, best_rd_stats_uv;
  uint8_t best_blk_skip[MAX_MIB_SIZE * MAX_MIB_SIZE];
  uint8_t best_tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  const int rate_mv0 = *rate_mv;
  const int interintra_allowed = cm->seq_params->enable_interintra_compound &&
                                 is_interintra_allowed(mbmi) &&
                                 mbmi->compound_idx;
  WARP_SAMPLE_INFO *const warp_sample_info =
      &x->warp_sample_info[mbmi->ref_frame[0]];
  int *pts0 = warp_sample_info->pts;
  int *pts_inref0 = warp_sample_info->pts_inref;

  assert(mbmi->ref_frame[1] != INTRA_FRAME);
  const MV_REFERENCE_FRAME ref_frame_1 = mbmi->ref_frame[1];
  av1_invalid_rd_stats(&best_rd_stats);
  mbmi->num_proj_ref = 1;  // assume num_proj_ref >=1
  MOTION_MODE last_motion_mode_allowed = SIMPLE_TRANSLATION;
  *yrd = INT64_MAX;
  if (features->switchable_motion_mode) {
    // Determine which motion modes to search if more than SIMPLE_TRANSLATION
    // is allowed.
    last_motion_mode_allowed = motion_mode_allowed(
        xd->global_motion, xd, mbmi, features->allow_warped_motion);
  }

  if (last_motion_mode_allowed == WARPED_CAUSAL) {
    // Collect projection samples used in least squares approximation of
    // the warped motion parameters if WARPED_CAUSAL is going to be searched.
    if (warp_sample_info->num < 0) {
      warp_sample_info->num = av1_findSamples(cm, xd, pts0, pts_inref0);
    }
    mbmi->num_proj_ref = warp_sample_info->num;
  }
  const int total_samples = mbmi->num_proj_ref;
  if (total_samples == 0) {
    // Do not search WARPED_CAUSAL if there are no samples to use to determine
    // warped parameters.
    last_motion_mode_allowed = OBMC_CAUSAL;
  }

  const MB_MODE_INFO base_mbmi = *mbmi;
  MB_MODE_INFO best_mbmi;
  const int interp_filter = features->interp_filter;
  const int switchable_rate =
      av1_is_interp_needed(xd)
          ? av1_get_switchable_rate(x, xd, interp_filter,
                                    cm->seq_params->enable_dual_filter)
          : 0;
  int64_t best_rd = INT64_MAX;
  int best_rate_mv = rate_mv0;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  int mode_index_start, mode_index_end;
  const int txfm_rd_gate_level =
      get_txfm_rd_gate_level(cm->seq_params->enable_masked_compound,
                             cpi->sf.inter_sf.txfm_rd_gate_level, bsize,
                             TX_SEARCH_MOTION_MODE, eval_motion_mode);

  // Modify the start and end index according to speed features. For example,
  // if SIMPLE_TRANSLATION has already been searched according to
  // the motion_mode_for_winner_cand speed feature, update the mode_index_start
  // to avoid searching it again.
  update_mode_start_end_index(cpi, mbmi, &mode_index_start, &mode_index_end,
                              last_motion_mode_allowed, interintra_allowed,
                              eval_motion_mode);
  // Main function loop. This loops over all of the possible motion modes and
  // computes RD to determine the best one. This process includes computing
  // any necessary side information for the motion mode and performing the
  // transform search.
  for (int mode_index = mode_index_start; mode_index <= mode_index_end;
       mode_index++) {
    if (args->skip_motion_mode && mode_index) continue;
    int tmp_rate2 = rate2_nocoeff;
    const int is_interintra_mode = mode_index > (int)last_motion_mode_allowed;
    int tmp_rate_mv = rate_mv0;

    *mbmi = base_mbmi;
    if (is_interintra_mode) {
      // Only use SIMPLE_TRANSLATION for interintra
      mbmi->motion_mode = SIMPLE_TRANSLATION;
    } else {
      mbmi->motion_mode = (MOTION_MODE)mode_index;
      assert(mbmi->ref_frame[1] != INTRA_FRAME);
    }

    // Do not search OBMC if the probability of selecting it is below a
    // predetermined threshold for this update_type and block size.
    const FRAME_UPDATE_TYPE update_type =
        get_frame_update_type(&cpi->ppi->gf_group, cpi->gf_frame_index);
    int use_actual_frame_probs = 1;
    int prune_obmc;
#if CONFIG_FPMT_TEST
    use_actual_frame_probs =
        (cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE) ? 0 : 1;
    if (!use_actual_frame_probs) {
      prune_obmc = cpi->ppi->temp_frame_probs.obmc_probs[update_type][bsize] <
                   cpi->sf.inter_sf.prune_obmc_prob_thresh;
    }
#endif
    if (use_actual_frame_probs) {
      prune_obmc = cpi->ppi->frame_probs.obmc_probs[update_type][bsize] <
                   cpi->sf.inter_sf.prune_obmc_prob_thresh;
    }
    if ((!cpi->oxcf.motion_mode_cfg.enable_obmc || prune_obmc) &&
        mbmi->motion_mode == OBMC_CAUSAL)
      continue;

    if (mbmi->motion_mode == SIMPLE_TRANSLATION && !is_interintra_mode) {
      // SIMPLE_TRANSLATION mode: no need to recalculate.
      // The prediction is calculated before motion_mode_rd() is called in
      // handle_inter_mode()
    } else if (mbmi->motion_mode == OBMC_CAUSAL) {
      const uint32_t cur_mv = mbmi->mv[0].as_int;
      // OBMC_CAUSAL not allowed for compound prediction
      assert(!is_comp_pred);
      if (have_newmv_in_inter_mode(this_mode)) {
        av1_single_motion_search(cpi, x, bsize, 0, &tmp_rate_mv, INT_MAX, NULL,
                                 &mbmi->mv[0], NULL);
        tmp_rate2 = rate2_nocoeff - rate_mv0 + tmp_rate_mv;
      }
      if ((mbmi->mv[0].as_int != cur_mv) || eval_motion_mode) {
        // Build the predictor according to the current motion vector if it has
        // not already been built
        av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, orig_dst, bsize,
                                      0, av1_num_planes(cm) - 1);
      }
      // Build the inter predictor by blending the predictor corresponding to
      // this MV, and the neighboring blocks using the OBMC model
      av1_build_obmc_inter_prediction(
          cm, xd, args->above_pred_buf, args->above_pred_stride,
          args->left_pred_buf, args->left_pred_stride);
#if !CONFIG_REALTIME_ONLY
    } else if (mbmi->motion_mode == WARPED_CAUSAL) {
      int pts[SAMPLES_ARRAY_SIZE], pts_inref[SAMPLES_ARRAY_SIZE];
      mbmi->motion_mode = WARPED_CAUSAL;
      mbmi->wm_params.wmtype = DEFAULT_WMTYPE;
      mbmi->interp_filters =
          av1_broadcast_interp_filter(av1_unswitchable_filter(interp_filter));

      memcpy(pts, pts0, total_samples * 2 * sizeof(*pts0));
      memcpy(pts_inref, pts_inref0, total_samples * 2 * sizeof(*pts_inref0));
      // Select the samples according to motion vector difference
      if (mbmi->num_proj_ref > 1) {
        mbmi->num_proj_ref = av1_selectSamples(
            &mbmi->mv[0].as_mv, pts, pts_inref, mbmi->num_proj_ref, bsize);
      }

      // Compute the warped motion parameters with a least squares fit
      //  using the collected samples
      if (!av1_find_projection(mbmi->num_proj_ref, pts, pts_inref, bsize,
                               mbmi->mv[0].as_mv.row, mbmi->mv[0].as_mv.col,
                               &mbmi->wm_params, mi_row, mi_col)) {
        assert(!is_comp_pred);
        if (have_newmv_in_inter_mode(this_mode)) {
          // Refine MV for NEWMV mode
          const int_mv mv0 = mbmi->mv[0];
          const WarpedMotionParams wm_params0 = mbmi->wm_params;
          const int num_proj_ref0 = mbmi->num_proj_ref;

          const int_mv ref_mv = av1_get_ref_mv(x, 0);
          SUBPEL_MOTION_SEARCH_PARAMS ms_params;
          av1_make_default_subpel_ms_params(&ms_params, cpi, x, bsize,
                                            &ref_mv.as_mv, NULL);

          // Refine MV in a small range.
          av1_refine_warped_mv(xd, cm, &ms_params, bsize, pts0, pts_inref0,
                               total_samples, cpi->sf.mv_sf.warp_search_method,
                               cpi->sf.mv_sf.warp_search_iters);

          if (mv0.as_int != mbmi->mv[0].as_int) {
            // Keep the refined MV and WM parameters.
            tmp_rate_mv = av1_mv_bit_cost(
                &mbmi->mv[0].as_mv, &ref_mv.as_mv, x->mv_costs->nmv_joint_cost,
                x->mv_costs->mv_cost_stack, MV_COST_WEIGHT);
            tmp_rate2 = rate2_nocoeff - rate_mv0 + tmp_rate_mv;
          } else {
            // Restore the old MV and WM parameters.
            mbmi->mv[0] = mv0;
            mbmi->wm_params = wm_params0;
            mbmi->num_proj_ref = num_proj_ref0;
          }
        }

        // Build the warped predictor
        av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize, 0,
                                      av1_num_planes(cm) - 1);
      } else {
        continue;
      }
#endif  // !CONFIG_REALTIME_ONLY
    } else if (is_interintra_mode) {
      const int ret =
          av1_handle_inter_intra_mode(cpi, x, bsize, mbmi, args, ref_best_rd,
                                      &tmp_rate_mv, &tmp_rate2, orig_dst);
      if (ret < 0) continue;
    }

    // If we are searching newmv and the mv is the same as refmv, skip the
    // current mode
    if (!av1_check_newmv_joint_nonzero(cm, x)) continue;

    // Update rd_stats for the current motion mode
    txfm_info->skip_txfm = 0;
    rd_stats->dist = 0;
    rd_stats->sse = 0;
    rd_stats->skip_txfm = 1;
    rd_stats->rate = tmp_rate2;
    const ModeCosts *mode_costs = &x->mode_costs;
    if (mbmi->motion_mode != WARPED_CAUSAL) rd_stats->rate += switchable_rate;
    if (interintra_allowed) {
      rd_stats->rate +=
          mode_costs->interintra_cost[size_group_lookup[bsize]]
                                     [mbmi->ref_frame[1] == INTRA_FRAME];
    }
    if ((last_motion_mode_allowed > SIMPLE_TRANSLATION) &&
        (mbmi->ref_frame[1] != INTRA_FRAME)) {
      if (last_motion_mode_allowed == WARPED_CAUSAL) {
        rd_stats->rate +=
            mode_costs->motion_mode_cost[bsize][mbmi->motion_mode];
      } else {
        rd_stats->rate +=
            mode_costs->motion_mode_cost1[bsize][mbmi->motion_mode];
      }
    }

    int64_t this_yrd = INT64_MAX;

    if (!do_tx_search) {
      // Avoid doing a transform search here to speed up the overall mode
      // search. It will be done later in the mode search if the current
      // motion mode seems promising.
      int64_t curr_sse = -1;
      int64_t sse_y = -1;
      int est_residue_cost = 0;
      int64_t est_dist = 0;
      int64_t est_rd = 0;
      if (cpi->sf.inter_sf.inter_mode_rd_model_estimation == 1) {
        curr_sse = get_sse(cpi, x, &sse_y);
        const int has_est_rd = get_est_rate_dist(tile_data, bsize, curr_sse,
                                                 &est_residue_cost, &est_dist);
        (void)has_est_rd;
        assert(has_est_rd);
      } else if (cpi->sf.inter_sf.inter_mode_rd_model_estimation == 2 ||
                 cpi->sf.rt_sf.use_nonrd_pick_mode) {
        model_rd_sb_fn[MODELRD_TYPE_MOTION_MODE_RD](
            cpi, bsize, x, xd, 0, num_planes - 1, &est_residue_cost, &est_dist,
            NULL, &curr_sse, NULL, NULL, NULL);
        sse_y = x->pred_sse[xd->mi[0]->ref_frame[0]];
      }
      est_rd = RDCOST(x->rdmult, rd_stats->rate + est_residue_cost, est_dist);
      if (est_rd * 0.80 > *best_est_rd) {
        mbmi->ref_frame[1] = ref_frame_1;
        continue;
      }
      const int mode_rate = rd_stats->rate;
      rd_stats->rate += est_residue_cost;
      rd_stats->dist = est_dist;
      rd_stats->rdcost = est_rd;
      if (rd_stats->rdcost < *best_est_rd) {
        *best_est_rd = rd_stats->rdcost;
        assert(sse_y >= 0);
        ref_skip_rd[1] = txfm_rd_gate_level
                             ? RDCOST(x->rdmult, mode_rate, (sse_y << 4))
                             : INT64_MAX;
      }
      if (cm->current_frame.reference_mode == SINGLE_REFERENCE) {
        if (!is_comp_pred) {
          assert(curr_sse >= 0);
          inter_modes_info_push(inter_modes_info, mode_rate, curr_sse,
                                rd_stats->rdcost, rd_stats, rd_stats_y,
                                rd_stats_uv, mbmi);
        }
      } else {
        assert(curr_sse >= 0);
        inter_modes_info_push(inter_modes_info, mode_rate, curr_sse,
                              rd_stats->rdcost, rd_stats, rd_stats_y,
                              rd_stats_uv, mbmi);
      }
      mbmi->skip_txfm = 0;
    } else {
      // Perform full transform search
      int64_t skip_rd = INT64_MAX;
      int64_t skip_rdy = INT64_MAX;
      if (txfm_rd_gate_level) {
        // Check if the mode is good enough based on skip RD
        int64_t sse_y = INT64_MAX;
        int64_t curr_sse = get_sse(cpi, x, &sse_y);
        skip_rd = RDCOST(x->rdmult, rd_stats->rate, curr_sse);
        skip_rdy = RDCOST(x->rdmult, rd_stats->rate, (sse_y << 4));
        int eval_txfm = check_txfm_eval(x, bsize, ref_skip_rd[0], skip_rd,
                                        txfm_rd_gate_level, 0);
        if (!eval_txfm) continue;
      }

      // Do transform search
      const int mode_rate = rd_stats->rate;
      if (!av1_txfm_search(cpi, x, bsize, rd_stats, rd_stats_y, rd_stats_uv,
                           rd_stats->rate, ref_best_rd)) {
        if (rd_stats_y->rate == INT_MAX && mode_index == 0) {
          return INT64_MAX;
        }
        continue;
      }
      const int skip_ctx = av1_get_skip_txfm_context(xd);
      const int y_rate =
          rd_stats->skip_txfm
              ? x->mode_costs.skip_txfm_cost[skip_ctx][1]
              : (rd_stats_y->rate + x->mode_costs.skip_txfm_cost[skip_ctx][0]);
      this_yrd = RDCOST(x->rdmult, y_rate + mode_rate, rd_stats_y->dist);

      const int64_t curr_rd = RDCOST(x->rdmult, rd_stats->rate, rd_stats->dist);
      if (curr_rd < ref_best_rd) {
        ref_best_rd = curr_rd;
        ref_skip_rd[0] = skip_rd;
        ref_skip_rd[1] = skip_rdy;
      }
      if (cpi->sf.inter_sf.inter_mode_rd_model_estimation == 1) {
        inter_mode_data_push(
            tile_data, mbmi->bsize, rd_stats->sse, rd_stats->dist,
            rd_stats_y->rate + rd_stats_uv->rate +
                mode_costs->skip_txfm_cost[skip_ctx][mbmi->skip_txfm]);
      }
    }

    if (this_mode == GLOBALMV || this_mode == GLOBAL_GLOBALMV) {
      if (is_nontrans_global_motion(xd, xd->mi[0])) {
        mbmi->interp_filters =
            av1_broadcast_interp_filter(av1_unswitchable_filter(interp_filter));
      }
    }

    const int64_t tmp_rd = RDCOST(x->rdmult, rd_stats->rate, rd_stats->dist);
    if (mode_index == 0) {
      args->simple_rd[this_mode][mbmi->ref_mv_idx][mbmi->ref_frame[0]] = tmp_rd;
    }
    if (mode_index == 0 || tmp_rd < best_rd) {
      // Update best_rd data if this is the best motion mode so far
      best_mbmi = *mbmi;
      best_rd = tmp_rd;
      best_rd_stats = *rd_stats;
      best_rd_stats_y = *rd_stats_y;
      best_rate_mv = tmp_rate_mv;
      *yrd = this_yrd;
      if (num_planes > 1) best_rd_stats_uv = *rd_stats_uv;
      memcpy(best_blk_skip, txfm_info->blk_skip,
             sizeof(txfm_info->blk_skip[0]) * xd->height * xd->width);
      av1_copy_array(best_tx_type_map, xd->tx_type_map, xd->height * xd->width);
      best_xskip_txfm = mbmi->skip_txfm;
    }
  }
  // Update RD and mbmi stats for selected motion mode
  mbmi->ref_frame[1] = ref_frame_1;
  *rate_mv = best_rate_mv;
  if (best_rd == INT64_MAX || !av1_check_newmv_joint_nonzero(cm, x)) {
    av1_invalid_rd_stats(rd_stats);
    restore_dst_buf(xd, *orig_dst, num_planes);
    return INT64_MAX;
  }
  *mbmi = best_mbmi;
  *rd_stats = best_rd_stats;
  *rd_stats_y = best_rd_stats_y;
  if (num_planes > 1) *rd_stats_uv = best_rd_stats_uv;
  memcpy(txfm_info->blk_skip, best_blk_skip,
         sizeof(txfm_info->blk_skip[0]) * xd->height * xd->width);
  av1_copy_array(xd->tx_type_map, best_tx_type_map, xd->height * xd->width);
  txfm_info->skip_txfm = best_xskip_txfm;

  restore_dst_buf(xd, *orig_dst, num_planes);
  return 0;
}

static int64_t skip_mode_rd(RD_STATS *rd_stats, const AV1_COMP *const cpi,
                            MACROBLOCK *const x, BLOCK_SIZE bsize,
                            const BUFFER_SET *const orig_dst, int64_t best_rd) {
  assert(bsize < BLOCK_SIZES_ALL);
  const AV1_COMMON *cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  int64_t total_sse = 0;
  int64_t this_rd = INT64_MAX;
  const int skip_mode_ctx = av1_get_skip_mode_context(xd);
  rd_stats->rate = x->mode_costs.skip_mode_cost[skip_mode_ctx][1];

  for (int plane = 0; plane < num_planes; ++plane) {
    // Call av1_enc_build_inter_predictor() for one plane at a time.
    av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, orig_dst, bsize,
                                  plane, plane);
    const struct macroblockd_plane *const pd = &xd->plane[plane];
    const BLOCK_SIZE plane_bsize =
        get_plane_block_size(bsize, pd->subsampling_x, pd->subsampling_y);

    av1_subtract_plane(x, plane_bsize, plane);

    int64_t sse =
        av1_pixel_diff_dist(x, plane, 0, 0, plane_bsize, plane_bsize, NULL);
    if (is_cur_buf_hbd(xd)) sse = ROUND_POWER_OF_TWO(sse, (xd->bd - 8) * 2);
    sse <<= 4;
    total_sse += sse;
    // When current rd cost is more than the best rd, skip evaluation of
    // remaining planes.
    this_rd = RDCOST(x->rdmult, rd_stats->rate, total_sse);
    if (this_rd > best_rd) break;
  }

  rd_stats->dist = rd_stats->sse = total_sse;
  rd_stats->rdcost = this_rd;

  restore_dst_buf(xd, *orig_dst, num_planes);
  return 0;
}

// Check NEARESTMV, NEARMV, GLOBALMV ref mvs for duplicate and skip the relevant
// mode
// Note(rachelbarker): This speed feature currently does not interact correctly
// with global motion. The issue is that, when global motion is used, GLOBALMV
// produces a different prediction to NEARESTMV/NEARMV even if the motion
// vectors are the same. Thus GLOBALMV should not be pruned in this case.
static inline int check_repeat_ref_mv(const MB_MODE_INFO_EXT *mbmi_ext,
                                      int ref_idx,
                                      const MV_REFERENCE_FRAME *ref_frame,
                                      PREDICTION_MODE single_mode) {
  const uint8_t ref_frame_type = av1_ref_frame_type(ref_frame);
  const int ref_mv_count = mbmi_ext->ref_mv_count[ref_frame_type];
  assert(single_mode != NEWMV);
  if (single_mode == NEARESTMV) {
    return 0;
  } else if (single_mode == NEARMV) {
    // when ref_mv_count = 0, NEARESTMV and NEARMV are same as GLOBALMV
    // when ref_mv_count = 1, NEARMV is same as GLOBALMV
    if (ref_mv_count < 2) return 1;
  } else if (single_mode == GLOBALMV) {
    // when ref_mv_count == 0, GLOBALMV is same as NEARESTMV
    if (ref_mv_count == 0) return 1;
    // when ref_mv_count == 1, NEARMV is same as GLOBALMV
    else if (ref_mv_count == 1)
      return 0;

    int stack_size = AOMMIN(USABLE_REF_MV_STACK_SIZE, ref_mv_count);
    // Check GLOBALMV is matching with any mv in ref_mv_stack
    for (int ref_mv_idx = 0; ref_mv_idx < stack_size; ref_mv_idx++) {
      int_mv this_mv;

      if (ref_idx == 0)
        this_mv = mbmi_ext->ref_mv_stack[ref_frame_type][ref_mv_idx].this_mv;
      else
        this_mv = mbmi_ext->ref_mv_stack[ref_frame_type][ref_mv_idx].comp_mv;

      if (this_mv.as_int == mbmi_ext->global_mvs[ref_frame[ref_idx]].as_int)
        return 1;
    }
  }
  return 0;
}

static inline int get_this_mv(int_mv *this_mv, PREDICTION_MODE this_mode,
                              int ref_idx, int ref_mv_idx,
                              int skip_repeated_ref_mv,
                              const MV_REFERENCE_FRAME *ref_frame,
                              const MB_MODE_INFO_EXT *mbmi_ext) {
  const PREDICTION_MODE single_mode = get_single_mode(this_mode, ref_idx);
  assert(is_inter_singleref_mode(single_mode));
  if (single_mode == NEWMV) {
    this_mv->as_int = INVALID_MV;
  } else if (single_mode == GLOBALMV) {
    if (skip_repeated_ref_mv &&
        check_repeat_ref_mv(mbmi_ext, ref_idx, ref_frame, single_mode))
      return 0;
    *this_mv = mbmi_ext->global_mvs[ref_frame[ref_idx]];
  } else {
    assert(single_mode == NEARMV || single_mode == NEARESTMV);
    const uint8_t ref_frame_type = av1_ref_frame_type(ref_frame);
    const int ref_mv_offset = single_mode == NEARESTMV ? 0 : ref_mv_idx + 1;
    if (ref_mv_offset < mbmi_ext->ref_mv_count[ref_frame_type]) {
      assert(ref_mv_offset >= 0);
      if (ref_idx == 0) {
        *this_mv =
            mbmi_ext->ref_mv_stack[ref_frame_type][ref_mv_offset].this_mv;
      } else {
        *this_mv =
            mbmi_ext->ref_mv_stack[ref_frame_type][ref_mv_offset].comp_mv;
      }
    } else {
      if (skip_repeated_ref_mv &&
          check_repeat_ref_mv(mbmi_ext, ref_idx, ref_frame, single_mode))
        return 0;
      *this_mv = mbmi_ext->global_mvs[ref_frame[ref_idx]];
    }
  }
  return 1;
}

// Skip NEARESTMV and NEARMV modes based on refmv weight computed in ref mv list
// population
static inline int skip_nearest_near_mv_using_refmv_weight(
    const MACROBLOCK *const x, const PREDICTION_MODE this_mode,
    const int8_t ref_frame_type, PREDICTION_MODE best_mode) {
  if (this_mode != NEARESTMV && this_mode != NEARMV) return 0;
  // Do not skip the mode if the current block has not yet obtained a valid
  // inter mode.
  if (!is_inter_mode(best_mode)) return 0;

  const MACROBLOCKD *xd = &x->e_mbd;
  // Do not skip the mode if both the top and left neighboring blocks are not
  // available.
  if (!xd->left_available || !xd->up_available) return 0;
  const MB_MODE_INFO_EXT *const mbmi_ext = &x->mbmi_ext;
  const uint16_t *const ref_mv_weight = mbmi_ext->weight[ref_frame_type];
  const int ref_mv_count =
      AOMMIN(MAX_REF_MV_SEARCH, mbmi_ext->ref_mv_count[ref_frame_type]);

  if (ref_mv_count == 0) return 0;
  // If ref mv list has at least one nearest candidate do not prune NEARESTMV
  if (this_mode == NEARESTMV && ref_mv_weight[0] >= REF_CAT_LEVEL) return 0;

  // Count number of ref mvs populated from nearest candidates
  int nearest_refmv_count = 0;
  for (int ref_mv_idx = 0; ref_mv_idx < ref_mv_count; ref_mv_idx++) {
    if (ref_mv_weight[ref_mv_idx] >= REF_CAT_LEVEL) nearest_refmv_count++;
  }

  // nearest_refmv_count indicates the closeness of block motion characteristics
  // with respect to its spatial neighbor. Smaller value of nearest_refmv_count
  // w.r.t to ref_mv_count means less correlation with its spatial neighbors.
  // Hence less possibility for NEARESTMV and NEARMV modes becoming the best
  // mode since these modes work well for blocks that shares similar motion
  // characteristics with its neighbor. Thus, NEARMV mode is pruned when
  // nearest_refmv_count is relatively smaller than ref_mv_count and NEARESTMV
  // mode is pruned if none of the ref mvs are populated from nearest candidate.
  const int prune_thresh = 1 + (ref_mv_count >= 2);
  if (nearest_refmv_count < prune_thresh) return 1;
  return 0;
}

// This function update the non-new mv for the current prediction mode
static inline int build_cur_mv(int_mv *cur_mv, PREDICTION_MODE this_mode,
                               const AV1_COMMON *cm, const MACROBLOCK *x,
                               int skip_repeated_ref_mv) {
  const MACROBLOCKD *xd = &x->e_mbd;
  const MB_MODE_INFO *mbmi = xd->mi[0];
  const int is_comp_pred = has_second_ref(mbmi);

  int ret = 1;
  for (int i = 0; i < is_comp_pred + 1; ++i) {
    int_mv this_mv;
    this_mv.as_int = INVALID_MV;
    ret = get_this_mv(&this_mv, this_mode, i, mbmi->ref_mv_idx,
                      skip_repeated_ref_mv, mbmi->ref_frame, &x->mbmi_ext);
    if (!ret) return 0;
    const PREDICTION_MODE single_mode = get_single_mode(this_mode, i);
    if (single_mode == NEWMV) {
      const uint8_t ref_frame_type = av1_ref_frame_type(mbmi->ref_frame);
      cur_mv[i] =
          (i == 0) ? x->mbmi_ext.ref_mv_stack[ref_frame_type][mbmi->ref_mv_idx]
                         .this_mv
                   : x->mbmi_ext.ref_mv_stack[ref_frame_type][mbmi->ref_mv_idx]
                         .comp_mv;
    } else {
      ret &= clamp_and_check_mv(cur_mv + i, this_mv, cm, x);
    }
  }
  return ret;
}

static inline int get_drl_cost(const MB_MODE_INFO *mbmi,
                               const MB_MODE_INFO_EXT *mbmi_ext,
                               const int (*const drl_mode_cost0)[2],
                               int8_t ref_frame_type) {
  int cost = 0;
  if (mbmi->mode == NEWMV || mbmi->mode == NEW_NEWMV) {
    for (int idx = 0; idx < 2; ++idx) {
      if (mbmi_ext->ref_mv_count[ref_frame_type] > idx + 1) {
        uint8_t drl_ctx = av1_drl_ctx(mbmi_ext->weight[ref_frame_type], idx);
        cost += drl_mode_cost0[drl_ctx][mbmi->ref_mv_idx != idx];
        if (mbmi->ref_mv_idx == idx) return cost;
      }
    }
    return cost;
  }

  if (have_nearmv_in_inter_mode(mbmi->mode)) {
    for (int idx = 1; idx < 3; ++idx) {
      if (mbmi_ext->ref_mv_count[ref_frame_type] > idx + 1) {
        uint8_t drl_ctx = av1_drl_ctx(mbmi_ext->weight[ref_frame_type], idx);
        cost += drl_mode_cost0[drl_ctx][mbmi->ref_mv_idx != (idx - 1)];
        if (mbmi->ref_mv_idx == (idx - 1)) return cost;
      }
    }
    return cost;
  }
  return cost;
}

static inline int is_single_newmv_valid(const HandleInterModeArgs *const args,
                                        const MB_MODE_INFO *const mbmi,
                                        PREDICTION_MODE this_mode) {
  for (int ref_idx = 0; ref_idx < 2; ++ref_idx) {
    const PREDICTION_MODE single_mode = get_single_mode(this_mode, ref_idx);
    const MV_REFERENCE_FRAME ref = mbmi->ref_frame[ref_idx];
    if (single_mode == NEWMV &&
        args->single_newmv_valid[mbmi->ref_mv_idx][ref] == 0) {
      return 0;
    }
  }
  return 1;
}

static int get_drl_refmv_count(const MACROBLOCK *const x,
                               const MV_REFERENCE_FRAME *ref_frame,
                               PREDICTION_MODE mode) {
  const MB_MODE_INFO_EXT *const mbmi_ext = &x->mbmi_ext;
  const int8_t ref_frame_type = av1_ref_frame_type(ref_frame);
  const int has_nearmv = have_nearmv_in_inter_mode(mode) ? 1 : 0;
  const int ref_mv_count = mbmi_ext->ref_mv_count[ref_frame_type];
  const int only_newmv = (mode == NEWMV || mode == NEW_NEWMV);
  const int has_drl =
      (has_nearmv && ref_mv_count > 2) || (only_newmv && ref_mv_count > 1);
  const int ref_set =
      has_drl ? AOMMIN(MAX_REF_MV_SEARCH, ref_mv_count - has_nearmv) : 1;

  return ref_set;
}

// Checks if particular ref_mv_idx should be pruned.
static int prune_ref_mv_idx_using_qindex(const int reduce_inter_modes,
                                         const int qindex,
                                         const int ref_mv_idx) {
  if (reduce_inter_modes >= 3) return 1;
  // Q-index logic based pruning is enabled only for
  // reduce_inter_modes = 2.
  assert(reduce_inter_modes == 2);
  // When reduce_inter_modes=2, pruning happens as below based on q index.
  // For q index range between 0 and 85: prune if ref_mv_idx >= 1.
  // For q index range between 86 and 170: prune if ref_mv_idx == 2.
  // For q index range between 171 and 255: no pruning.
  const int min_prune_ref_mv_idx = (qindex * 3 / QINDEX_RANGE) + 1;
  return (ref_mv_idx >= min_prune_ref_mv_idx);
}

// Whether this reference motion vector can be skipped, based on initial
// heuristics.
static bool ref_mv_idx_early_breakout(
    const SPEED_FEATURES *const sf,
    const RefFrameDistanceInfo *const ref_frame_dist_info, MACROBLOCK *x,
    const HandleInterModeArgs *const args, int64_t ref_best_rd,
    int ref_mv_idx) {
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const MB_MODE_INFO_EXT *const mbmi_ext = &x->mbmi_ext;
  const int8_t ref_frame_type = av1_ref_frame_type(mbmi->ref_frame);
  const int is_comp_pred = has_second_ref(mbmi);
  if (sf->inter_sf.reduce_inter_modes && ref_mv_idx > 0) {
    if (mbmi->ref_frame[0] == LAST2_FRAME ||
        mbmi->ref_frame[0] == LAST3_FRAME ||
        mbmi->ref_frame[1] == LAST2_FRAME ||
        mbmi->ref_frame[1] == LAST3_FRAME) {
      const int has_nearmv = have_nearmv_in_inter_mode(mbmi->mode) ? 1 : 0;
      if (mbmi_ext->weight[ref_frame_type][ref_mv_idx + has_nearmv] <
          REF_CAT_LEVEL) {
        return true;
      }
    }
    // TODO(any): Experiment with reduce_inter_modes for compound prediction
    if (sf->inter_sf.reduce_inter_modes >= 2 && !is_comp_pred &&
        have_newmv_in_inter_mode(mbmi->mode)) {
      if (mbmi->ref_frame[0] != ref_frame_dist_info->nearest_past_ref &&
          mbmi->ref_frame[0] != ref_frame_dist_info->nearest_future_ref) {
        const int has_nearmv = have_nearmv_in_inter_mode(mbmi->mode) ? 1 : 0;
        const int do_prune = prune_ref_mv_idx_using_qindex(
            sf->inter_sf.reduce_inter_modes, x->qindex, ref_mv_idx);
        if (do_prune &&
            (mbmi_ext->weight[ref_frame_type][ref_mv_idx + has_nearmv] <
             REF_CAT_LEVEL)) {
          return true;
        }
      }
    }
  }

  mbmi->ref_mv_idx = ref_mv_idx;
  if (is_comp_pred && (!is_single_newmv_valid(args, mbmi, mbmi->mode))) {
    return true;
  }
  size_t est_rd_rate = args->ref_frame_cost + args->single_comp_cost;
  const int drl_cost = get_drl_cost(
      mbmi, mbmi_ext, x->mode_costs.drl_mode_cost0, ref_frame_type);
  est_rd_rate += drl_cost;
  if (RDCOST(x->rdmult, est_rd_rate, 0) > ref_best_rd &&
      mbmi->mode != NEARESTMV && mbmi->mode != NEAREST_NEARESTMV) {
    return true;
  }
  return false;
}

// Compute the estimated RD cost for the motion vector with simple translation.
static int64_t simple_translation_pred_rd(AV1_COMP *const cpi, MACROBLOCK *x,
                                          RD_STATS *rd_stats,
                                          HandleInterModeArgs *args,
                                          int ref_mv_idx, int64_t ref_best_rd,
                                          BLOCK_SIZE bsize) {
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  MB_MODE_INFO_EXT *const mbmi_ext = &x->mbmi_ext;
  const int8_t ref_frame_type = av1_ref_frame_type(mbmi->ref_frame);
  const AV1_COMMON *cm = &cpi->common;
  const int is_comp_pred = has_second_ref(mbmi);
  const ModeCosts *mode_costs = &x->mode_costs;

  struct macroblockd_plane *p = xd->plane;
  const BUFFER_SET orig_dst = {
    { p[0].dst.buf, p[1].dst.buf, p[2].dst.buf },
    { p[0].dst.stride, p[1].dst.stride, p[2].dst.stride },
  };
  av1_init_rd_stats(rd_stats);

  mbmi->interinter_comp.type = COMPOUND_AVERAGE;
  mbmi->comp_group_idx = 0;
  mbmi->compound_idx = 1;
  if (mbmi->ref_frame[1] == INTRA_FRAME) {
    mbmi->ref_frame[1] = NONE_FRAME;
  }
  int16_t mode_ctx =
      av1_mode_context_analyzer(mbmi_ext->mode_context, mbmi->ref_frame);

  mbmi->num_proj_ref = 0;
  mbmi->motion_mode = SIMPLE_TRANSLATION;
  mbmi->ref_mv_idx = ref_mv_idx;

  rd_stats->rate += args->ref_frame_cost + args->single_comp_cost;
  const int drl_cost =
      get_drl_cost(mbmi, mbmi_ext, mode_costs->drl_mode_cost0, ref_frame_type);
  rd_stats->rate += drl_cost;

  int_mv cur_mv[2];
  if (!build_cur_mv(cur_mv, mbmi->mode, cm, x, 0)) {
    return INT64_MAX;
  }
  assert(have_nearmv_in_inter_mode(mbmi->mode));
  for (int i = 0; i < is_comp_pred + 1; ++i) {
    mbmi->mv[i].as_int = cur_mv[i].as_int;
  }
  const int ref_mv_cost = cost_mv_ref(mode_costs, mbmi->mode, mode_ctx);
  rd_stats->rate += ref_mv_cost;

  if (RDCOST(x->rdmult, rd_stats->rate, 0) > ref_best_rd) {
    return INT64_MAX;
  }

  mbmi->motion_mode = SIMPLE_TRANSLATION;
  mbmi->num_proj_ref = 0;
  if (is_comp_pred) {
    // Only compound_average
    mbmi->interinter_comp.type = COMPOUND_AVERAGE;
    mbmi->comp_group_idx = 0;
    mbmi->compound_idx = 1;
  }
  set_default_interp_filters(mbmi, cm->features.interp_filter);

  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, &orig_dst, bsize,
                                AOM_PLANE_Y, AOM_PLANE_Y);
  int est_rate;
  int64_t est_dist;
  model_rd_sb_fn[MODELRD_CURVFIT](cpi, bsize, x, xd, 0, 0, &est_rate, &est_dist,
                                  NULL, NULL, NULL, NULL, NULL);
  return RDCOST(x->rdmult, rd_stats->rate + est_rate, est_dist);
}

// Represents a set of integers, from 0 to sizeof(int) * 8, as bits in
// an integer. 0 for the i-th bit means that integer is excluded, 1 means
// it is included.
static inline void mask_set_bit(int *mask, int index) { *mask |= (1 << index); }

static inline bool mask_check_bit(int mask, int index) {
  return (mask >> index) & 0x1;
}

// Before performing the full MV search in handle_inter_mode, do a simple
// translation search and see if we can eliminate any motion vectors.
// Returns an integer where, if the i-th bit is set, it means that the i-th
// motion vector should be searched. This is only set for NEAR_MV.
static int ref_mv_idx_to_search(AV1_COMP *const cpi, MACROBLOCK *x,
                                RD_STATS *rd_stats,
                                HandleInterModeArgs *const args,
                                int64_t ref_best_rd, BLOCK_SIZE bsize,
                                const int ref_set) {
  // If the number of ref mv count is equal to 1, do not prune the same. It
  // is better to evaluate the same than to prune it.
  if (ref_set == 1) return 1;
  AV1_COMMON *const cm = &cpi->common;
  const MACROBLOCKD *const xd = &x->e_mbd;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const PREDICTION_MODE this_mode = mbmi->mode;

  // Only search indices if they have some chance of being good.
  int good_indices = 0;
  for (int i = 0; i < ref_set; ++i) {
    if (ref_mv_idx_early_breakout(&cpi->sf, &cpi->ref_frame_dist_info, x, args,
                                  ref_best_rd, i)) {
      continue;
    }
    mask_set_bit(&good_indices, i);
  }

  // Only prune in NEARMV mode, if the speed feature is set, and the block size
  // is large enough. If these conditions are not met, return all good indices
  // found so far.
  if (!cpi->sf.inter_sf.prune_mode_search_simple_translation)
    return good_indices;
  if (!have_nearmv_in_inter_mode(this_mode)) return good_indices;
  if (num_pels_log2_lookup[bsize] <= 6) return good_indices;
  // Do not prune when there is internal resizing. TODO(elliottk) fix this
  // so b/2384 can be resolved.
  if (av1_is_scaled(get_ref_scale_factors(cm, mbmi->ref_frame[0])) ||
      (mbmi->ref_frame[1] > 0 &&
       av1_is_scaled(get_ref_scale_factors(cm, mbmi->ref_frame[1])))) {
    return good_indices;
  }

  // Calculate the RD cost for the motion vectors using simple translation.
  int64_t idx_rdcost[] = { INT64_MAX, INT64_MAX, INT64_MAX };
  for (int ref_mv_idx = 0; ref_mv_idx < ref_set; ++ref_mv_idx) {
    // If this index is bad, ignore it.
    if (!mask_check_bit(good_indices, ref_mv_idx)) {
      continue;
    }
    idx_rdcost[ref_mv_idx] = simple_translation_pred_rd(
        cpi, x, rd_stats, args, ref_mv_idx, ref_best_rd, bsize);
  }
  // Find the index with the best RD cost.
  int best_idx = 0;
  for (int i = 1; i < MAX_REF_MV_SEARCH; ++i) {
    if (idx_rdcost[i] < idx_rdcost[best_idx]) {
      best_idx = i;
    }
  }
  // Only include indices that are good and within a % of the best.
  const double dth = has_second_ref(mbmi) ? 1.05 : 1.001;
  // If the simple translation cost is not within this multiple of the
  // best RD, skip it. Note that the cutoff is derived experimentally.
  const double ref_dth = 5;
  int result = 0;
  for (int i = 0; i < ref_set; ++i) {
    if (mask_check_bit(good_indices, i) &&
        (1.0 * idx_rdcost[i]) / idx_rdcost[best_idx] < dth &&
        (1.0 * idx_rdcost[i]) / ref_best_rd < ref_dth) {
      mask_set_bit(&result, i);
    }
  }
  return result;
}

/*!\brief Motion mode information for inter mode search speedup.
 *
 * Used in a speed feature to search motion modes other than
 * SIMPLE_TRANSLATION only on winning candidates.
 */
typedef struct motion_mode_candidate {
  /*!
   * Mode info for the motion mode candidate.
   */
  MB_MODE_INFO mbmi;
  /*!
   * Rate describing the cost of the motion vectors for this candidate.
   */
  int rate_mv;
  /*!
   * Rate before motion mode search and transform coding is applied.
   */
  int rate2_nocoeff;
  /*!
   * An integer value 0 or 1 which indicates whether or not to skip the motion
   * mode search and default to SIMPLE_TRANSLATION as a speed feature for this
   * candidate.
   */
  int skip_motion_mode;
  /*!
   * Total RD cost for this candidate.
   */
  int64_t rd_cost;
} motion_mode_candidate;

/*!\cond */
typedef struct motion_mode_best_st_candidate {
  motion_mode_candidate motion_mode_cand[MAX_WINNER_MOTION_MODES];
  int num_motion_mode_cand;
} motion_mode_best_st_candidate;

// Checks if the current reference frame matches with neighbouring block's
// (top/left) reference frames
static inline int ref_match_found_in_nb_blocks(MB_MODE_INFO *cur_mbmi,
                                               MB_MODE_INFO *nb_mbmi) {
  MV_REFERENCE_FRAME nb_ref_frames[2] = { nb_mbmi->ref_frame[0],
                                          nb_mbmi->ref_frame[1] };
  MV_REFERENCE_FRAME cur_ref_frames[2] = { cur_mbmi->ref_frame[0],
                                           cur_mbmi->ref_frame[1] };
  const int is_cur_comp_pred = has_second_ref(cur_mbmi);
  int match_found = 0;

  for (int i = 0; i < (is_cur_comp_pred + 1); i++) {
    if ((cur_ref_frames[i] == nb_ref_frames[0]) ||
        (cur_ref_frames[i] == nb_ref_frames[1]))
      match_found = 1;
  }
  return match_found;
}

static inline int find_ref_match_in_above_nbs(const int total_mi_cols,
                                              MACROBLOCKD *xd) {
  if (!xd->up_available) return 1;
  const int mi_col = xd->mi_col;
  MB_MODE_INFO **cur_mbmi = xd->mi;
  // prev_row_mi points into the mi array, starting at the beginning of the
  // previous row.
  MB_MODE_INFO **prev_row_mi = xd->mi - mi_col - 1 * xd->mi_stride;
  const int end_col = AOMMIN(mi_col + xd->width, total_mi_cols);
  uint8_t mi_step;
  for (int above_mi_col = mi_col; above_mi_col < end_col;
       above_mi_col += mi_step) {
    MB_MODE_INFO **above_mi = prev_row_mi + above_mi_col;
    mi_step = mi_size_wide[above_mi[0]->bsize];
    int match_found = 0;
    if (is_inter_block(*above_mi))
      match_found = ref_match_found_in_nb_blocks(*cur_mbmi, *above_mi);
    if (match_found) return 1;
  }
  return 0;
}

static inline int find_ref_match_in_left_nbs(const int total_mi_rows,
                                             MACROBLOCKD *xd) {
  if (!xd->left_available) return 1;
  const int mi_row = xd->mi_row;
  MB_MODE_INFO **cur_mbmi = xd->mi;
  // prev_col_mi points into the mi array, starting at the top of the
  // previous column
  MB_MODE_INFO **prev_col_mi = xd->mi - 1 - mi_row * xd->mi_stride;
  const int end_row = AOMMIN(mi_row + xd->height, total_mi_rows);
  uint8_t mi_step;
  for (int left_mi_row = mi_row; left_mi_row < end_row;
       left_mi_row += mi_step) {
    MB_MODE_INFO **left_mi = prev_col_mi + left_mi_row * xd->mi_stride;
    mi_step = mi_size_high[left_mi[0]->bsize];
    int match_found = 0;
    if (is_inter_block(*left_mi))
      match_found = ref_match_found_in_nb_blocks(*cur_mbmi, *left_mi);
    if (match_found) return 1;
  }
  return 0;
}
/*!\endcond */

/*! \brief Struct used to hold TPL data to
 * narrow down parts of the inter mode search.
 */
typedef struct {
  /*!
   * The best inter cost out of all of the reference frames.
   */
  int64_t best_inter_cost;
  /*!
   * The inter cost for each reference frame.
   */
  int64_t ref_inter_cost[INTER_REFS_PER_FRAME];
} PruneInfoFromTpl;

#if !CONFIG_REALTIME_ONLY
// TODO(Remya): Check if get_tpl_stats_b() can be reused
static inline void get_block_level_tpl_stats(
    AV1_COMP *cpi, BLOCK_SIZE bsize, int mi_row, int mi_col, int *valid_refs,
    PruneInfoFromTpl *inter_cost_info_from_tpl) {
  AV1_COMMON *const cm = &cpi->common;

  assert(IMPLIES(cpi->ppi->gf_group.size > 0,
                 cpi->gf_frame_index < cpi->ppi->gf_group.size));
  const int tpl_idx = cpi->gf_frame_index;
  TplParams *const tpl_data = &cpi->ppi->tpl_data;
  if (!av1_tpl_stats_ready(tpl_data, tpl_idx)) return;
  const TplDepFrame *tpl_frame = &tpl_data->tpl_frame[tpl_idx];
  const TplDepStats *tpl_stats = tpl_frame->tpl_stats_ptr;
  const int mi_wide = mi_size_wide[bsize];
  const int mi_high = mi_size_high[bsize];
  const int tpl_stride = tpl_frame->stride;
  const int step = 1 << tpl_data->tpl_stats_block_mis_log2;
  const int mi_col_sr =
      coded_to_superres_mi(mi_col, cm->superres_scale_denominator);
  const int mi_col_end_sr =
      coded_to_superres_mi(mi_col + mi_wide, cm->superres_scale_denominator);
  const int mi_cols_sr = av1_pixels_to_mi(cm->superres_upscaled_width);

  const int row_step = step;
  const int col_step_sr =
      coded_to_superres_mi(step, cm->superres_scale_denominator);
  for (int row = mi_row; row < AOMMIN(mi_row + mi_high, cm->mi_params.mi_rows);
       row += row_step) {
    for (int col = mi_col_sr; col < AOMMIN(mi_col_end_sr, mi_cols_sr);
         col += col_step_sr) {
      const TplDepStats *this_stats = &tpl_stats[av1_tpl_ptr_pos(
          row, col, tpl_stride, tpl_data->tpl_stats_block_mis_log2)];

      // Sums up the inter cost of corresponding ref frames
      for (int ref_idx = 0; ref_idx < INTER_REFS_PER_FRAME; ref_idx++) {
        inter_cost_info_from_tpl->ref_inter_cost[ref_idx] +=
            this_stats->pred_error[ref_idx];
      }
    }
  }

  // Computes the best inter cost (minimum inter_cost)
  int64_t best_inter_cost = INT64_MAX;
  for (int ref_idx = 0; ref_idx < INTER_REFS_PER_FRAME; ref_idx++) {
    const int64_t cur_inter_cost =
        inter_cost_info_from_tpl->ref_inter_cost[ref_idx];
    // For invalid ref frames, cur_inter_cost = 0 and has to be handled while
    // calculating the minimum inter_cost
    if (cur_inter_cost != 0 && (cur_inter_cost < best_inter_cost) &&
        valid_refs[ref_idx])
      best_inter_cost = cur_inter_cost;
  }
  inter_cost_info_from_tpl->best_inter_cost = best_inter_cost;
}
#endif

static inline int prune_modes_based_on_tpl_stats(
    PruneInfoFromTpl *inter_cost_info_from_tpl, const int *refs, int ref_mv_idx,
    const PREDICTION_MODE this_mode, int prune_mode_level) {
  const int have_newmv = have_newmv_in_inter_mode(this_mode);
  if ((prune_mode_level < 2) && have_newmv) return 0;

  const int64_t best_inter_cost = inter_cost_info_from_tpl->best_inter_cost;
  if (best_inter_cost == INT64_MAX) return 0;

  const int prune_level = prune_mode_level - 1;
  int64_t cur_inter_cost;

  const int is_globalmv =
      (this_mode == GLOBALMV) || (this_mode == GLOBAL_GLOBALMV);
  const int prune_index = is_globalmv ? MAX_REF_MV_SEARCH : ref_mv_idx;

  // Thresholds used for pruning:
  // Lower value indicates aggressive pruning and higher value indicates
  // conservative pruning which is set based on ref_mv_idx and speed feature.
  // 'prune_index' 0, 1, 2 corresponds to ref_mv indices 0, 1 and 2. prune_index
  // 3 corresponds to GLOBALMV/GLOBAL_GLOBALMV
  static const int tpl_inter_mode_prune_mul_factor[3][MAX_REF_MV_SEARCH + 1] = {
    { 6, 6, 6, 4 }, { 6, 4, 4, 4 }, { 5, 4, 4, 4 }
  };

  const int is_comp_pred = (refs[1] > INTRA_FRAME);
  if (!is_comp_pred) {
    cur_inter_cost = inter_cost_info_from_tpl->ref_inter_cost[refs[0] - 1];
  } else {
    const int64_t inter_cost_ref0 =
        inter_cost_info_from_tpl->ref_inter_cost[refs[0] - 1];
    const int64_t inter_cost_ref1 =
        inter_cost_info_from_tpl->ref_inter_cost[refs[1] - 1];
    // Choose maximum inter_cost among inter_cost_ref0 and inter_cost_ref1 for
    // more aggressive pruning
    cur_inter_cost = AOMMAX(inter_cost_ref0, inter_cost_ref1);
  }

  // Prune the mode if cur_inter_cost is greater than threshold times
  // best_inter_cost
  if (cur_inter_cost >
      ((tpl_inter_mode_prune_mul_factor[prune_level][prune_index] *
        best_inter_cost) >>
       2))
    return 1;
  return 0;
}

/*!\brief High level function to select parameters for compound mode.
 *
 * \ingroup inter_mode_search
 * The main search functionality is done in the call to av1_compound_type_rd().
 *
 * \param[in]     cpi               Top-level encoder structure.
 * \param[in]     x                 Pointer to struct holding all the data for
 *                                  the current macroblock.
 * \param[in]     args              HandleInterModeArgs struct holding
 *                                  miscellaneous arguments for inter mode
 *                                  search. See the documentation for this
 *                                  struct for a description of each member.
 * \param[in]     ref_best_rd       Best RD found so far for this block.
 *                                  It is used for early termination of this
 *                                  search if the RD exceeds this value.
 * \param[in,out] cur_mv            Current motion vector.
 * \param[in]     bsize             Current block size.
 * \param[in,out] compmode_interinter_cost  RD of the selected interinter
                                    compound mode.
 * \param[in,out] rd_buffers        CompoundTypeRdBuffers struct to hold all
 *                                  allocated buffers for the compound
 *                                  predictors and masks in the compound type
 *                                  search.
 * \param[in,out] orig_dst          A prediction buffer to hold a computed
 *                                  prediction. This will eventually hold the
 *                                  final prediction, and the tmp_dst info will
 *                                  be copied here.
 * \param[in]     tmp_dst           A temporary prediction buffer to hold a
 *                                  computed prediction.
 * \param[in,out] rate_mv           The rate associated with the motion vectors.
 *                                  This will be modified if a motion search is
 *                                  done in the motion mode search.
 * \param[in,out] rd_stats          Struct to keep track of the overall RD
 *                                  information.
 * \param[in,out] skip_rd           An array of length 2 where skip_rd[0] is the
 *                                  best total RD for a skip mode so far, and
 *                                  skip_rd[1] is the best RD for a skip mode so
 *                                  far in luma. This is used as a speed feature
 *                                  to skip the transform search if the computed
 *                                  skip RD for the current mode is not better
 *                                  than the best skip_rd so far.
 * \param[in,out] skip_build_pred   Indicates whether or not to build the inter
 *                                  predictor. If this is 0, the inter predictor
 *                                  has already been built and thus we can avoid
 *                                  repeating computation.
 * \return Returns 1 if this mode is worse than one already seen and 0 if it is
 * a viable candidate.
 */
static int process_compound_inter_mode(
    AV1_COMP *const cpi, MACROBLOCK *x, HandleInterModeArgs *args,
    int64_t ref_best_rd, int_mv *cur_mv, BLOCK_SIZE bsize,
    int *compmode_interinter_cost, const CompoundTypeRdBuffers *rd_buffers,
    const BUFFER_SET *orig_dst, const BUFFER_SET *tmp_dst, int *rate_mv,
    RD_STATS *rd_stats, int64_t *skip_rd, int *skip_build_pred) {
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const AV1_COMMON *cm = &cpi->common;
  const int masked_compound_used = is_any_masked_compound_used(bsize) &&
                                   cm->seq_params->enable_masked_compound;
  int mode_search_mask = (1 << COMPOUND_AVERAGE) | (1 << COMPOUND_DISTWTD) |
                         (1 << COMPOUND_WEDGE) | (1 << COMPOUND_DIFFWTD);

  const int num_planes = av1_num_planes(cm);
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  int is_luma_interp_done = 0;
  set_default_interp_filters(mbmi, cm->features.interp_filter);

  int64_t best_rd_compound;
  int64_t rd_thresh;
  const int comp_type_rd_shift = COMP_TYPE_RD_THRESH_SHIFT;
  const int comp_type_rd_scale = COMP_TYPE_RD_THRESH_SCALE;
  rd_thresh = get_rd_thresh_from_best_rd(ref_best_rd, (1 << comp_type_rd_shift),
                                         comp_type_rd_scale);
  // Select compound type and any parameters related to that type
  // (for example, the mask parameters if it is a masked mode) and compute
  // the RD
  *compmode_interinter_cost = av1_compound_type_rd(
      cpi, x, args, bsize, cur_mv, mode_search_mask, masked_compound_used,
      orig_dst, tmp_dst, rd_buffers, rate_mv, &best_rd_compound, rd_stats,
      ref_best_rd, skip_rd[1], &is_luma_interp_done, rd_thresh);
  if (ref_best_rd < INT64_MAX &&
      (best_rd_compound >> comp_type_rd_shift) * comp_type_rd_scale >
          ref_best_rd) {
    restore_dst_buf(xd, *orig_dst, num_planes);
    return 1;
  }

  // Build only uv predictor for COMPOUND_AVERAGE.
  // Note there is no need to call av1_enc_build_inter_predictor
  // for luma if COMPOUND_AVERAGE is selected because it is the first
  // candidate in av1_compound_type_rd, which means it used the dst_buf
  // rather than the tmp_buf.
  if (mbmi->interinter_comp.type == COMPOUND_AVERAGE && is_luma_interp_done) {
    if (num_planes > 1) {
      av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, orig_dst, bsize,
                                    AOM_PLANE_U, num_planes - 1);
    }
    *skip_build_pred = 1;
  }
  return 0;
}

// Speed feature to prune out MVs that are similar to previous MVs if they
// don't achieve the best RD advantage.
static int prune_ref_mv_idx_search(int ref_mv_idx, int best_ref_mv_idx,
                                   int_mv save_mv[MAX_REF_MV_SEARCH - 1][2],
                                   MB_MODE_INFO *mbmi, int pruning_factor) {
  int i;
  const int is_comp_pred = has_second_ref(mbmi);
  const int thr = (1 + is_comp_pred) << (pruning_factor + 1);

  // Skip the evaluation if an MV match is found.
  if (ref_mv_idx > 0) {
    for (int idx = 0; idx < ref_mv_idx; ++idx) {
      if (save_mv[idx][0].as_int == INVALID_MV) continue;

      int mv_diff = 0;
      for (i = 0; i < 1 + is_comp_pred; ++i) {
        mv_diff += abs(save_mv[idx][i].as_mv.row - mbmi->mv[i].as_mv.row) +
                   abs(save_mv[idx][i].as_mv.col - mbmi->mv[i].as_mv.col);
      }

      // If this mode is not the best one, and current MV is similar to
      // previous stored MV, terminate this ref_mv_idx evaluation.
      if (best_ref_mv_idx == -1 && mv_diff <= thr) return 1;
    }
  }

  if (ref_mv_idx < MAX_REF_MV_SEARCH - 1) {
    for (i = 0; i < is_comp_pred + 1; ++i)
      save_mv[ref_mv_idx][i].as_int = mbmi->mv[i].as_int;
  }

  return 0;
}

/*!\brief Prunes ZeroMV Search Using Best NEWMV's SSE
 *
 * \ingroup inter_mode_search
 *
 * Compares the sse of zero mv and the best sse found in single new_mv. If the
 * sse of the zero_mv is higher, returns 1 to signal zero_mv can be skipped.
 * Else returns 0.
 *
 * Note that the sse of here comes from single_motion_search. So it is
 * interpolated with the filter in motion search, not the actual interpolation
 * filter used in encoding.
 *
 * \param[in]     fn_ptr            A table of function pointers to compute SSE.
 * \param[in]     x                 Pointer to struct holding all the data for
 *                                  the current macroblock.
 * \param[in]     bsize             The current block_size.
 * \param[in]     args              The args to handle_inter_mode, used to track
 *                                  the best SSE.
 * \param[in]    prune_zero_mv_with_sse  The argument holds speed feature
 *                                       prune_zero_mv_with_sse value
 * \return Returns 1 if zero_mv is pruned, 0 otherwise.
 */
static inline int prune_zero_mv_with_sse(const aom_variance_fn_ptr_t *fn_ptr,
                                         const MACROBLOCK *x, BLOCK_SIZE bsize,
                                         const HandleInterModeArgs *args,
                                         int prune_zero_mv_with_sse) {
  const MACROBLOCKD *xd = &x->e_mbd;
  const MB_MODE_INFO *mbmi = xd->mi[0];

  const int is_comp_pred = has_second_ref(mbmi);
  const MV_REFERENCE_FRAME *refs = mbmi->ref_frame;

  for (int idx = 0; idx < 1 + is_comp_pred; idx++) {
    if (xd->global_motion[refs[idx]].wmtype != IDENTITY) {
      // Pruning logic only works for IDENTITY type models
      // Note: In theory we could apply similar logic for TRANSLATION
      // type models, but we do not code these due to a spec bug
      // (see comments in gm_get_motion_vector() in av1/common/mv.h)
      assert(xd->global_motion[refs[idx]].wmtype != TRANSLATION);
      return 0;
    }

    // Don't prune if we have invalid data
    assert(mbmi->mv[idx].as_int == 0);
    if (args->best_single_sse_in_refs[refs[idx]] == INT32_MAX) {
      return 0;
    }
  }

  // Sum up the sse of ZEROMV and best NEWMV
  unsigned int this_sse_sum = 0;
  unsigned int best_sse_sum = 0;
  for (int idx = 0; idx < 1 + is_comp_pred; idx++) {
    const struct macroblock_plane *const p = &x->plane[AOM_PLANE_Y];
    const struct macroblockd_plane *pd = xd->plane;
    const struct buf_2d *src_buf = &p->src;
    const struct buf_2d *ref_buf = &pd->pre[idx];
    const uint8_t *src = src_buf->buf;
    const uint8_t *ref = ref_buf->buf;
    const int src_stride = src_buf->stride;
    const int ref_stride = ref_buf->stride;

    unsigned int this_sse;
    fn_ptr[bsize].vf(ref, ref_stride, src, src_stride, &this_sse);
    this_sse_sum += this_sse;

    const unsigned int best_sse = args->best_single_sse_in_refs[refs[idx]];
    best_sse_sum += best_sse;
  }

  const double mul = prune_zero_mv_with_sse > 1 ? 1.00 : 1.25;
  if ((double)this_sse_sum > (mul * (double)best_sse_sum)) {
    return 1;
  }

  return 0;
}

/*!\brief Searches for interpolation filter in realtime mode during winner eval
 *
 * \ingroup inter_mode_search
 *
 * Does a simple interpolation filter search during winner mode evaluation. This
 * is currently only used by realtime mode as \ref
 * av1_interpolation_filter_search is not called during realtime encoding.
 *
 * This function only searches over two possible filters. EIGHTTAP_REGULAR is
 * always search. For lowres clips (<= 240p), MULTITAP_SHARP is also search. For
 * higher  res slips (>240p), EIGHTTAP_SMOOTH is also searched.
 *  *
 * \param[in]     cpi               Pointer to the compressor. Used for feature
 *                                  flags.
 * \param[in,out] x                 Pointer to macroblock. This is primarily
 *                                  used to access the buffers.
 * \param[in]     mi_row            The current row in mi unit (4X4 pixels).
 * \param[in]     mi_col            The current col in mi unit (4X4 pixels).
 * \param[in]     bsize             The current block_size.
 * \return Returns true if a predictor is built in xd->dst, false otherwise.
 */
static inline bool fast_interp_search(const AV1_COMP *cpi, MACROBLOCK *x,
                                      int mi_row, int mi_col,
                                      BLOCK_SIZE bsize) {
  static const InterpFilters filters_ref_set[3] = {
    { EIGHTTAP_REGULAR, EIGHTTAP_REGULAR },
    { EIGHTTAP_SMOOTH, EIGHTTAP_SMOOTH },
    { MULTITAP_SHARP, MULTITAP_SHARP }
  };

  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mi = xd->mi[0];
  int64_t best_cost = INT64_MAX;
  int best_filter_index = -1;
  // dst_bufs[0] sores the new predictor, and dist_bifs[1] stores the best
  const int num_planes = av1_num_planes(cm);
  const int is_240p_or_lesser = AOMMIN(cm->width, cm->height) <= 240;
  assert(is_inter_mode(mi->mode));
  assert(mi->motion_mode == SIMPLE_TRANSLATION);
  assert(!is_inter_compound_mode(mi->mode));

  if (!av1_is_interp_needed(xd)) {
    return false;
  }

  struct macroblockd_plane *pd = xd->plane;
  const BUFFER_SET orig_dst = {
    { pd[0].dst.buf, pd[1].dst.buf, pd[2].dst.buf },
    { pd[0].dst.stride, pd[1].dst.stride, pd[2].dst.stride },
  };
  uint8_t *const tmp_buf = get_buf_by_bd(xd, x->tmp_pred_bufs[0]);
  const BUFFER_SET tmp_dst = { { tmp_buf, tmp_buf + 1 * MAX_SB_SQUARE,
                                 tmp_buf + 2 * MAX_SB_SQUARE },
                               { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE } };
  const BUFFER_SET *dst_bufs[2] = { &orig_dst, &tmp_dst };

  for (int i = 0; i < 3; ++i) {
    if (is_240p_or_lesser) {
      if (filters_ref_set[i].x_filter == EIGHTTAP_SMOOTH) {
        continue;
      }
    } else {
      if (filters_ref_set[i].x_filter == MULTITAP_SHARP) {
        continue;
      }
    }
    int64_t cost;
    RD_STATS tmp_rd = { 0 };

    mi->interp_filters.as_filters = filters_ref_set[i];
    av1_enc_build_inter_predictor_y(xd, mi_row, mi_col);

    model_rd_sb_fn[cpi->sf.rt_sf.use_simple_rd_model
                       ? MODELRD_LEGACY
                       : MODELRD_TYPE_INTERP_FILTER](
        cpi, bsize, x, xd, AOM_PLANE_Y, AOM_PLANE_Y, &tmp_rd.rate, &tmp_rd.dist,
        &tmp_rd.skip_txfm, &tmp_rd.sse, NULL, NULL, NULL);

    tmp_rd.rate += av1_get_switchable_rate(x, xd, cm->features.interp_filter,
                                           cm->seq_params->enable_dual_filter);
    cost = RDCOST(x->rdmult, tmp_rd.rate, tmp_rd.dist);
    if (cost < best_cost) {
      best_filter_index = i;
      best_cost = cost;
      swap_dst_buf(xd, dst_bufs, num_planes);
    }
  }
  assert(best_filter_index >= 0);

  mi->interp_filters.as_filters = filters_ref_set[best_filter_index];

  const bool is_best_pred_in_orig = &orig_dst == dst_bufs[1];

  if (is_best_pred_in_orig) {
    swap_dst_buf(xd, dst_bufs, num_planes);
  } else {
    // Note that xd->pd's bufers are kept in sync with dst_bufs[0]. So if
    // is_best_pred_in_orig is false, that means the current buffer is the
    // original one.
    assert(&orig_dst == dst_bufs[0]);
    assert(xd->plane[AOM_PLANE_Y].dst.buf == orig_dst.plane[AOM_PLANE_Y]);
    const int width = block_size_wide[bsize];
    const int height = block_size_high[bsize];
#if CONFIG_AV1_HIGHBITDEPTH
    const bool is_hbd = is_cur_buf_hbd(xd);
    if (is_hbd) {
      aom_highbd_convolve_copy(CONVERT_TO_SHORTPTR(tmp_dst.plane[AOM_PLANE_Y]),
                               tmp_dst.stride[AOM_PLANE_Y],
                               CONVERT_TO_SHORTPTR(orig_dst.plane[AOM_PLANE_Y]),
                               orig_dst.stride[AOM_PLANE_Y], width, height);
    } else {
      aom_convolve_copy(tmp_dst.plane[AOM_PLANE_Y], tmp_dst.stride[AOM_PLANE_Y],
                        orig_dst.plane[AOM_PLANE_Y],
                        orig_dst.stride[AOM_PLANE_Y], width, height);
    }
#else
    aom_convolve_copy(tmp_dst.plane[AOM_PLANE_Y], tmp_dst.stride[AOM_PLANE_Y],
                      orig_dst.plane[AOM_PLANE_Y], orig_dst.stride[AOM_PLANE_Y],
                      width, height);
#endif
  }

  // Build the YUV predictor.
  if (num_planes > 1) {
    av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize,
                                  AOM_PLANE_U, AOM_PLANE_V);
  }

  return true;
}

/*!\brief AV1 inter mode RD computation
 *
 * \ingroup inter_mode_search
 * Do the RD search for a given inter mode and compute all information relevant
 * to the input mode. It will compute the best MV,
 * compound parameters (if the mode is a compound mode) and interpolation filter
 * parameters.
 *
 * \param[in]     cpi               Top-level encoder structure.
 * \param[in]     tile_data         Pointer to struct holding adaptive
 *                                  data/contexts/models for the tile during
 *                                  encoding.
 * \param[in]     x                 Pointer to structure holding all the data
 *                                  for the current macroblock.
 * \param[in]     bsize             Current block size.
 * \param[in,out] rd_stats          Struct to keep track of the overall RD
 *                                  information.
 * \param[in,out] rd_stats_y        Struct to keep track of the RD information
 *                                  for only the Y plane.
 * \param[in,out] rd_stats_uv       Struct to keep track of the RD information
 *                                  for only the UV planes.
 * \param[in]     args              HandleInterModeArgs struct holding
 *                                  miscellaneous arguments for inter mode
 *                                  search. See the documentation for this
 *                                  struct for a description of each member.
 * \param[in]     ref_best_rd       Best RD found so far for this block.
 *                                  It is used for early termination of this
 *                                  search if the RD exceeds this value.
 * \param[in]     tmp_buf           Temporary buffer used to hold predictors
 *                                  built in this search.
 * \param[in,out] rd_buffers        CompoundTypeRdBuffers struct to hold all
 *                                  allocated buffers for the compound
 *                                  predictors and masks in the compound type
 *                                  search.
 * \param[in,out] best_est_rd       Estimated RD for motion mode search if
 *                                  do_tx_search (see below) is 0.
 * \param[in]     do_tx_search      Parameter to indicate whether or not to do
 *                                  a full transform search. This will compute
 *                                  an estimated RD for the modes without the
 *                                  transform search and later perform the full
 *                                  transform search on the best candidates.
 * \param[in,out] inter_modes_info  InterModesInfo struct to hold inter mode
 *                                  information to perform a full transform
 *                                  search only on winning candidates searched
 *                                  with an estimate for transform coding RD.
 * \param[in,out] motion_mode_cand  A motion_mode_candidate struct to store
 *                                  motion mode information used in a speed
 *                                  feature to search motion modes other than
 *                                  SIMPLE_TRANSLATION only on winning
 *                                  candidates.
 * \param[in,out] skip_rd           A length 2 array, where skip_rd[0] is the
 *                                  best total RD for a skip mode so far, and
 *                                  skip_rd[1] is the best RD for a skip mode so
 *                                  far in luma. This is used as a speed feature
 *                                  to skip the transform search if the computed
 *                                  skip RD for the current mode is not better
 *                                  than the best skip_rd so far.
 * \param[in]     inter_cost_info_from_tpl A PruneInfoFromTpl struct used to
 *                                         narrow down the search based on data
 *                                         collected in the TPL model.
 * \param[out]    yrd               Stores the rdcost corresponding to encoding
 *                                  the luma plane.
 *
 * \return The RD cost for the mode being searched.
 */
static int64_t handle_inter_mode(
    AV1_COMP *const cpi, TileDataEnc *tile_data, MACROBLOCK *x,
    BLOCK_SIZE bsize, RD_STATS *rd_stats, RD_STATS *rd_stats_y,
    RD_STATS *rd_stats_uv, HandleInterModeArgs *args, int64_t ref_best_rd,
    uint8_t *const tmp_buf, const CompoundTypeRdBuffers *rd_buffers,
    int64_t *best_est_rd, const int do_tx_search,
    InterModesInfo *inter_modes_info, motion_mode_candidate *motion_mode_cand,
    int64_t *skip_rd, PruneInfoFromTpl *inter_cost_info_from_tpl,
    int64_t *yrd) {
  const AV1_COMMON *cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  MB_MODE_INFO_EXT *const mbmi_ext = &x->mbmi_ext;
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  const int is_comp_pred = has_second_ref(mbmi);
  const PREDICTION_MODE this_mode = mbmi->mode;

#if CONFIG_REALTIME_ONLY
  const int prune_modes_based_on_tpl = 0;
#else   // CONFIG_REALTIME_ONLY
  const TplParams *const tpl_data = &cpi->ppi->tpl_data;
  const int prune_modes_based_on_tpl =
      cpi->sf.inter_sf.prune_inter_modes_based_on_tpl &&
      av1_tpl_stats_ready(tpl_data, cpi->gf_frame_index);
#endif  // CONFIG_REALTIME_ONLY
  int i;
  // Reference frames for this mode
  const int refs[2] = { mbmi->ref_frame[0],
                        (mbmi->ref_frame[1] < 0 ? 0 : mbmi->ref_frame[1]) };
  int rate_mv = 0;
  int64_t rd = INT64_MAX;
  // Do first prediction into the destination buffer. Do the next
  // prediction into a temporary buffer. Then keep track of which one
  // of these currently holds the best predictor, and use the other
  // one for future predictions. In the end, copy from tmp_buf to
  // dst if necessary.
  struct macroblockd_plane *pd = xd->plane;
  const BUFFER_SET orig_dst = {
    { pd[0].dst.buf, pd[1].dst.buf, pd[2].dst.buf },
    { pd[0].dst.stride, pd[1].dst.stride, pd[2].dst.stride },
  };
  const BUFFER_SET tmp_dst = { { tmp_buf, tmp_buf + 1 * MAX_SB_SQUARE,
                                 tmp_buf + 2 * MAX_SB_SQUARE },
                               { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE } };

  int64_t ret_val = INT64_MAX;
  const int8_t ref_frame_type = av1_ref_frame_type(mbmi->ref_frame);
  RD_STATS best_rd_stats, best_rd_stats_y, best_rd_stats_uv;
  int64_t best_rd = INT64_MAX;
  uint8_t best_blk_skip[MAX_MIB_SIZE * MAX_MIB_SIZE];
  uint8_t best_tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  int64_t best_yrd = INT64_MAX;
  MB_MODE_INFO best_mbmi = *mbmi;
  int best_xskip_txfm = 0;
  int64_t newmv_ret_val = INT64_MAX;
  inter_mode_info mode_info[MAX_REF_MV_SEARCH];

  // Do not prune the mode based on inter cost from tpl if the current ref frame
  // is the winner ref in neighbouring blocks.
  int ref_match_found_in_above_nb = 0;
  int ref_match_found_in_left_nb = 0;
  if (prune_modes_based_on_tpl) {
    ref_match_found_in_above_nb =
        find_ref_match_in_above_nbs(cm->mi_params.mi_cols, xd);
    ref_match_found_in_left_nb =
        find_ref_match_in_left_nbs(cm->mi_params.mi_rows, xd);
  }

  // First, perform a simple translation search for each of the indices. If
  // an index performs well, it will be fully searched in the main loop
  // of this function.
  const int ref_set = get_drl_refmv_count(x, mbmi->ref_frame, this_mode);
  // Save MV results from first 2 ref_mv_idx.
  int_mv save_mv[MAX_REF_MV_SEARCH - 1][2];
  int best_ref_mv_idx = -1;
  const int idx_mask =
      ref_mv_idx_to_search(cpi, x, rd_stats, args, ref_best_rd, bsize, ref_set);
  const int16_t mode_ctx =
      av1_mode_context_analyzer(mbmi_ext->mode_context, mbmi->ref_frame);
  const ModeCosts *mode_costs = &x->mode_costs;
  const int ref_mv_cost = cost_mv_ref(mode_costs, this_mode, mode_ctx);
  const int base_rate =
      args->ref_frame_cost + args->single_comp_cost + ref_mv_cost;

  for (i = 0; i < MAX_REF_MV_SEARCH - 1; ++i) {
    save_mv[i][0].as_int = INVALID_MV;
    save_mv[i][1].as_int = INVALID_MV;
  }
  args->start_mv_cnt = 0;

  // Main loop of this function. This will  iterate over all of the ref mvs
  // in the dynamic reference list and do the following:
  //    1.) Get the current MV. Create newmv MV if necessary
  //    2.) Search compound type and parameters if applicable
  //    3.) Do interpolation filter search
  //    4.) Build the inter predictor
  //    5.) Pick the motion mode (SIMPLE_TRANSLATION, OBMC_CAUSAL,
  //        WARPED_CAUSAL)
  //    6.) Update stats if best so far
  for (int ref_mv_idx = 0; ref_mv_idx < ref_set; ++ref_mv_idx) {
    mbmi->ref_mv_idx = ref_mv_idx;

    mode_info[ref_mv_idx].full_search_mv.as_int = INVALID_MV;
    mode_info[ref_mv_idx].full_mv_bestsme = INT_MAX;
    const int drl_cost = get_drl_cost(
        mbmi, mbmi_ext, mode_costs->drl_mode_cost0, ref_frame_type);
    mode_info[ref_mv_idx].drl_cost = drl_cost;
    mode_info[ref_mv_idx].skip = 0;

    if (!mask_check_bit(idx_mask, ref_mv_idx)) {
      // MV did not perform well in simple translation search. Skip it.
      continue;
    }
    if (prune_modes_based_on_tpl && !ref_match_found_in_above_nb &&
        !ref_match_found_in_left_nb && (ref_best_rd != INT64_MAX)) {
      // Skip mode if TPL model indicates it will not be beneficial.
      if (prune_modes_based_on_tpl_stats(
              inter_cost_info_from_tpl, refs, ref_mv_idx, this_mode,
              cpi->sf.inter_sf.prune_inter_modes_based_on_tpl))
        continue;
    }
    av1_init_rd_stats(rd_stats);

    // Initialize compound mode data
    mbmi->interinter_comp.type = COMPOUND_AVERAGE;
    mbmi->comp_group_idx = 0;
    mbmi->compound_idx = 1;
    if (mbmi->ref_frame[1] == INTRA_FRAME) mbmi->ref_frame[1] = NONE_FRAME;

    mbmi->num_proj_ref = 0;
    mbmi->motion_mode = SIMPLE_TRANSLATION;

    // Compute cost for signalling this DRL index
    rd_stats->rate = base_rate;
    rd_stats->rate += drl_cost;

    int rs = 0;
    int compmode_interinter_cost = 0;

    int_mv cur_mv[2];

    // TODO(Cherma): Extend this speed feature to support compound mode
    int skip_repeated_ref_mv =
        is_comp_pred ? 0 : cpi->sf.inter_sf.skip_repeated_ref_mv;
    // Generate the current mv according to the prediction mode
    if (!build_cur_mv(cur_mv, this_mode, cm, x, skip_repeated_ref_mv)) {
      continue;
    }

    // The above call to build_cur_mv does not handle NEWMV modes. Build
    // the mv here if we have NEWMV for any predictors.
    if (have_newmv_in_inter_mode(this_mode)) {
#if CONFIG_COLLECT_COMPONENT_TIMING
      start_timing(cpi, handle_newmv_time);
#endif
      newmv_ret_val =
          handle_newmv(cpi, x, bsize, cur_mv, &rate_mv, args, mode_info);
#if CONFIG_COLLECT_COMPONENT_TIMING
      end_timing(cpi, handle_newmv_time);
#endif

      if (newmv_ret_val != 0) continue;

      if (is_inter_singleref_mode(this_mode) &&
          cur_mv[0].as_int != INVALID_MV) {
        const MV_REFERENCE_FRAME ref = refs[0];
        const unsigned int this_sse = x->pred_sse[ref];
        if (this_sse < args->best_single_sse_in_refs[ref]) {
          args->best_single_sse_in_refs[ref] = this_sse;
        }

        if (cpi->sf.rt_sf.skip_newmv_mode_based_on_sse) {
          const int th_idx = cpi->sf.rt_sf.skip_newmv_mode_based_on_sse - 1;
          const int pix_idx = num_pels_log2_lookup[bsize] - 4;
          const double scale_factor[3][11] = {
            { 0.7, 0.7, 0.7, 0.7, 0.7, 0.8, 0.8, 0.9, 0.9, 0.9, 0.9 },
            { 0.7, 0.7, 0.7, 0.7, 0.8, 0.8, 1, 1, 1, 1, 1 },
            { 0.7, 0.7, 0.7, 0.7, 1, 1, 1, 1, 1, 1, 1 }
          };
          assert(pix_idx >= 0);
          assert(th_idx <= 2);
          if (args->best_pred_sse < scale_factor[th_idx][pix_idx] * this_sse)
            continue;
        }
      }

      rd_stats->rate += rate_mv;
    }
    // Copy the motion vector for this mode into mbmi struct
    for (i = 0; i < is_comp_pred + 1; ++i) {
      mbmi->mv[i].as_int = cur_mv[i].as_int;
    }

    if (RDCOST(x->rdmult, rd_stats->rate, 0) > ref_best_rd &&
        mbmi->mode != NEARESTMV && mbmi->mode != NEAREST_NEARESTMV) {
      continue;
    }

    // Skip the rest of the search if prune_ref_mv_idx_search speed feature
    // is enabled, and the current MV is similar to a previous one.
    if (cpi->sf.inter_sf.prune_ref_mv_idx_search && is_comp_pred &&
        prune_ref_mv_idx_search(ref_mv_idx, best_ref_mv_idx, save_mv, mbmi,
                                cpi->sf.inter_sf.prune_ref_mv_idx_search))
      continue;

    if (cpi->sf.gm_sf.prune_zero_mv_with_sse &&
        (this_mode == GLOBALMV || this_mode == GLOBAL_GLOBALMV)) {
      if (prune_zero_mv_with_sse(cpi->ppi->fn_ptr, x, bsize, args,
                                 cpi->sf.gm_sf.prune_zero_mv_with_sse)) {
        continue;
      }
    }

    int skip_build_pred = 0;
    const int mi_row = xd->mi_row;
    const int mi_col = xd->mi_col;

    // Handle a compound predictor, continue if it is determined this
    // cannot be the best compound mode
    if (is_comp_pred) {
#if CONFIG_COLLECT_COMPONENT_TIMING
      start_timing(cpi, compound_type_rd_time);
#endif
      const int not_best_mode = process_compound_inter_mode(
          cpi, x, args, ref_best_rd, cur_mv, bsize, &compmode_interinter_cost,
          rd_buffers, &orig_dst, &tmp_dst, &rate_mv, rd_stats, skip_rd,
          &skip_build_pred);
#if CONFIG_COLLECT_COMPONENT_TIMING
      end_timing(cpi, compound_type_rd_time);
#endif
      if (not_best_mode) continue;
    }

    if (!args->skip_ifs) {
#if CONFIG_COLLECT_COMPONENT_TIMING
      start_timing(cpi, interpolation_filter_search_time);
#endif
      // Determine the interpolation filter for this mode
      ret_val = av1_interpolation_filter_search(
          x, cpi, tile_data, bsize, &tmp_dst, &orig_dst, &rd, &rs,
          &skip_build_pred, args, ref_best_rd);
#if CONFIG_COLLECT_COMPONENT_TIMING
      end_timing(cpi, interpolation_filter_search_time);
#endif
      if (args->modelled_rd != NULL && !is_comp_pred) {
        args->modelled_rd[this_mode][ref_mv_idx][refs[0]] = rd;
      }
      if (ret_val != 0) {
        restore_dst_buf(xd, orig_dst, num_planes);
        continue;
      } else if (cpi->sf.inter_sf.model_based_post_interp_filter_breakout &&
                 ref_best_rd != INT64_MAX && (rd >> 3) * 3 > ref_best_rd) {
        restore_dst_buf(xd, orig_dst, num_planes);
        continue;
      }

      // Compute modelled RD if enabled
      if (args->modelled_rd != NULL) {
        if (is_comp_pred) {
          const int mode0 = compound_ref0_mode(this_mode);
          const int mode1 = compound_ref1_mode(this_mode);
          const int64_t mrd =
              AOMMIN(args->modelled_rd[mode0][ref_mv_idx][refs[0]],
                     args->modelled_rd[mode1][ref_mv_idx][refs[1]]);
          if ((rd >> 3) * 6 > mrd && ref_best_rd < INT64_MAX) {
            restore_dst_buf(xd, orig_dst, num_planes);
            continue;
          }
        }
      }
    }

    rd_stats->rate += compmode_interinter_cost;
    if (skip_build_pred != 1) {
      // Build this inter predictor if it has not been previously built
      av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, &orig_dst, bsize, 0,
                                    av1_num_planes(cm) - 1);
    }

#if CONFIG_COLLECT_COMPONENT_TIMING
    start_timing(cpi, motion_mode_rd_time);
#endif
    int rate2_nocoeff = rd_stats->rate;
    // Determine the motion mode. This will be one of SIMPLE_TRANSLATION,
    // OBMC_CAUSAL or WARPED_CAUSAL
    int64_t this_yrd;
    ret_val = motion_mode_rd(cpi, tile_data, x, bsize, rd_stats, rd_stats_y,
                             rd_stats_uv, args, ref_best_rd, skip_rd, &rate_mv,
                             &orig_dst, best_est_rd, do_tx_search,
                             inter_modes_info, 0, &this_yrd);
#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(cpi, motion_mode_rd_time);
#endif
    assert(
        IMPLIES(!av1_check_newmv_joint_nonzero(cm, x), ret_val == INT64_MAX));

    if (ret_val != INT64_MAX) {
      int64_t tmp_rd = RDCOST(x->rdmult, rd_stats->rate, rd_stats->dist);
      const THR_MODES mode_enum = get_prediction_mode_idx(
          mbmi->mode, mbmi->ref_frame[0], mbmi->ref_frame[1]);
      // Collect mode stats for multiwinner mode processing
      store_winner_mode_stats(&cpi->common, x, mbmi, rd_stats, rd_stats_y,
                              rd_stats_uv, mode_enum, NULL, bsize, tmp_rd,
                              cpi->sf.winner_mode_sf.multi_winner_mode_type,
                              do_tx_search);
      if (tmp_rd < best_rd) {
        best_yrd = this_yrd;
        // Update the best rd stats if we found the best mode so far
        best_rd_stats = *rd_stats;
        best_rd_stats_y = *rd_stats_y;
        best_rd_stats_uv = *rd_stats_uv;
        best_rd = tmp_rd;
        best_mbmi = *mbmi;
        best_xskip_txfm = txfm_info->skip_txfm;
        memcpy(best_blk_skip, txfm_info->blk_skip,
               sizeof(best_blk_skip[0]) * xd->height * xd->width);
        av1_copy_array(best_tx_type_map, xd->tx_type_map,
                       xd->height * xd->width);
        motion_mode_cand->rate_mv = rate_mv;
        motion_mode_cand->rate2_nocoeff = rate2_nocoeff;
      }

      if (tmp_rd < ref_best_rd) {
        ref_best_rd = tmp_rd;
        best_ref_mv_idx = ref_mv_idx;
      }
    }
    restore_dst_buf(xd, orig_dst, num_planes);
  }

  if (best_rd == INT64_MAX) return INT64_MAX;

  // re-instate status of the best choice
  *rd_stats = best_rd_stats;
  *rd_stats_y = best_rd_stats_y;
  *rd_stats_uv = best_rd_stats_uv;
  *yrd = best_yrd;
  *mbmi = best_mbmi;
  txfm_info->skip_txfm = best_xskip_txfm;
  assert(IMPLIES(mbmi->comp_group_idx == 1,
                 mbmi->interinter_comp.type != COMPOUND_AVERAGE));
  memcpy(txfm_info->blk_skip, best_blk_skip,
         sizeof(best_blk_skip[0]) * xd->height * xd->width);
  av1_copy_array(xd->tx_type_map, best_tx_type_map, xd->height * xd->width);

  rd_stats->rdcost = RDCOST(x->rdmult, rd_stats->rate, rd_stats->dist);

  return rd_stats->rdcost;
}

/*!\brief Search for the best intrabc predictor
 *
 * \ingroup intra_mode_search
 * \callergraph
 * This function performs a motion search to find the best intrabc predictor.
 *
 * \returns Returns the best overall rdcost (including the non-intrabc modes
 * search before this function).
 */
static int64_t rd_pick_intrabc_mode_sb(const AV1_COMP *cpi, MACROBLOCK *x,
                                       PICK_MODE_CONTEXT *ctx,
                                       RD_STATS *rd_stats, BLOCK_SIZE bsize,
                                       int64_t best_rd) {
  const AV1_COMMON *const cm = &cpi->common;
  if (!av1_allow_intrabc(cm) || !cpi->oxcf.kf_cfg.enable_intrabc ||
      !cpi->sf.mv_sf.use_intrabc || cpi->sf.rt_sf.use_nonrd_pick_mode)
    return INT64_MAX;
  const int num_planes = av1_num_planes(cm);

  MACROBLOCKD *const xd = &x->e_mbd;
  const TileInfo *tile = &xd->tile;
  MB_MODE_INFO *mbmi = xd->mi[0];
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;

  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int w = block_size_wide[bsize];
  const int h = block_size_high[bsize];
  const int sb_row = mi_row >> cm->seq_params->mib_size_log2;
  const int sb_col = mi_col >> cm->seq_params->mib_size_log2;

  MB_MODE_INFO_EXT *const mbmi_ext = &x->mbmi_ext;
  const MV_REFERENCE_FRAME ref_frame = INTRA_FRAME;
  av1_find_mv_refs(cm, xd, mbmi, ref_frame, mbmi_ext->ref_mv_count,
                   xd->ref_mv_stack, xd->weight, NULL, mbmi_ext->global_mvs,
                   mbmi_ext->mode_context);
  // TODO(Ravi): Populate mbmi_ext->ref_mv_stack[ref_frame][4] and
  // mbmi_ext->weight[ref_frame][4] inside av1_find_mv_refs.
  av1_copy_usable_ref_mv_stack_and_weight(xd, mbmi_ext, ref_frame);
  int_mv nearestmv, nearmv;
  av1_find_best_ref_mvs_from_stack(0, mbmi_ext, ref_frame, &nearestmv, &nearmv,
                                   0);

  if (nearestmv.as_int == INVALID_MV) {
    nearestmv.as_int = 0;
  }
  if (nearmv.as_int == INVALID_MV) {
    nearmv.as_int = 0;
  }

  int_mv dv_ref = nearestmv.as_int == 0 ? nearmv : nearestmv;
  if (dv_ref.as_int == 0) {
    av1_find_ref_dv(&dv_ref, tile, cm->seq_params->mib_size, mi_row);
  }
  // Ref DV should not have sub-pel.
  assert((dv_ref.as_mv.col & 7) == 0);
  assert((dv_ref.as_mv.row & 7) == 0);
  mbmi_ext->ref_mv_stack[INTRA_FRAME][0].this_mv = dv_ref;

  struct buf_2d yv12_mb[MAX_MB_PLANE];
  av1_setup_pred_block(xd, yv12_mb, xd->cur_buf, NULL, NULL, num_planes);
  for (int i = 0; i < num_planes; ++i) {
    xd->plane[i].pre[0] = yv12_mb[i];
  }

  enum IntrabcMotionDirection {
    IBC_MOTION_ABOVE,
    IBC_MOTION_LEFT,
    IBC_MOTION_DIRECTIONS
  };

  MB_MODE_INFO best_mbmi = *mbmi;
  RD_STATS best_rdstats = *rd_stats;
  uint8_t best_blk_skip[MAX_MIB_SIZE * MAX_MIB_SIZE] = { 0 };
  uint8_t best_tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  av1_copy_array(best_tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);

  FULLPEL_MOTION_SEARCH_PARAMS fullms_params;
  const SEARCH_METHODS search_method =
      av1_get_default_mv_search_method(x, &cpi->sf.mv_sf, bsize);
  const search_site_config *lookahead_search_sites =
      cpi->mv_search_params.search_site_cfg[SS_CFG_LOOKAHEAD];
  const FULLPEL_MV start_mv = get_fullmv_from_mv(&dv_ref.as_mv);
  av1_make_default_fullpel_ms_params(&fullms_params, cpi, x, bsize,
                                     &dv_ref.as_mv, start_mv,
                                     lookahead_search_sites, search_method,
                                     /*fine_search_interval=*/0);
  const IntraBCMVCosts *const dv_costs = x->dv_costs;
  av1_set_ms_to_intra_mode(&fullms_params, dv_costs);

  for (enum IntrabcMotionDirection dir = IBC_MOTION_ABOVE;
       dir < IBC_MOTION_DIRECTIONS; ++dir) {
    switch (dir) {
      case IBC_MOTION_ABOVE:
        fullms_params.mv_limits.col_min =
            (tile->mi_col_start - mi_col) * MI_SIZE;
        fullms_params.mv_limits.col_max =
            (tile->mi_col_end - mi_col) * MI_SIZE - w;
        fullms_params.mv_limits.row_min =
            (tile->mi_row_start - mi_row) * MI_SIZE;
        fullms_params.mv_limits.row_max =
            (sb_row * cm->seq_params->mib_size - mi_row) * MI_SIZE - h;
        break;
      case IBC_MOTION_LEFT:
        fullms_params.mv_limits.col_min =
            (tile->mi_col_start - mi_col) * MI_SIZE;
        fullms_params.mv_limits.col_max =
            (sb_col * cm->seq_params->mib_size - mi_col) * MI_SIZE - w;
        // TODO(aconverse@google.com): Minimize the overlap between above and
        // left areas.
        fullms_params.mv_limits.row_min =
            (tile->mi_row_start - mi_row) * MI_SIZE;
        int bottom_coded_mi_edge =
            AOMMIN((sb_row + 1) * cm->seq_params->mib_size, tile->mi_row_end);
        fullms_params.mv_limits.row_max =
            (bottom_coded_mi_edge - mi_row) * MI_SIZE - h;
        break;
      default: assert(0);
    }
    assert(fullms_params.mv_limits.col_min >= fullms_params.mv_limits.col_min);
    assert(fullms_params.mv_limits.col_max <= fullms_params.mv_limits.col_max);
    assert(fullms_params.mv_limits.row_min >= fullms_params.mv_limits.row_min);
    assert(fullms_params.mv_limits.row_max <= fullms_params.mv_limits.row_max);

    av1_set_mv_search_range(&fullms_params.mv_limits, &dv_ref.as_mv);

    if (fullms_params.mv_limits.col_max < fullms_params.mv_limits.col_min ||
        fullms_params.mv_limits.row_max < fullms_params.mv_limits.row_min) {
      continue;
    }

    const int step_param = cpi->mv_search_params.mv_step_param;
    IntraBCHashInfo *intrabc_hash_info = &x->intrabc_hash_info;
    int_mv best_mv, best_hash_mv;
    FULLPEL_MV_STATS best_mv_stats;

    int bestsme =
        av1_full_pixel_search(start_mv, &fullms_params, step_param, NULL,
                              &best_mv.as_fullmv, &best_mv_stats, NULL);
    const int hashsme = av1_intrabc_hash_search(
        cpi, xd, &fullms_params, intrabc_hash_info, &best_hash_mv.as_fullmv);
    if (hashsme < bestsme) {
      best_mv = best_hash_mv;
      bestsme = hashsme;
    }

    if (bestsme == INT_MAX) continue;
    const MV dv = get_mv_from_fullmv(&best_mv.as_fullmv);
    if (!av1_is_fullmv_in_range(&fullms_params.mv_limits,
                                get_fullmv_from_mv(&dv)))
      continue;
    if (!av1_is_dv_valid(dv, cm, xd, mi_row, mi_col, bsize,
                         cm->seq_params->mib_size_log2))
      continue;

    // DV should not have sub-pel.
    assert((dv.col & 7) == 0);
    assert((dv.row & 7) == 0);
    memset(&mbmi->palette_mode_info, 0, sizeof(mbmi->palette_mode_info));
    mbmi->filter_intra_mode_info.use_filter_intra = 0;
    mbmi->use_intrabc = 1;
    mbmi->mode = DC_PRED;
    mbmi->uv_mode = UV_DC_PRED;
    mbmi->motion_mode = SIMPLE_TRANSLATION;
    mbmi->mv[0].as_mv = dv;
    mbmi->interp_filters = av1_broadcast_interp_filter(BILINEAR);
    mbmi->skip_txfm = 0;
    av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize, 0,
                                  av1_num_planes(cm) - 1);

    // TODO(aconverse@google.com): The full motion field defining discount
    // in MV_COST_WEIGHT is too large. Explore other values.
    const int rate_mv = av1_mv_bit_cost(&dv, &dv_ref.as_mv, dv_costs->joint_mv,
                                        dv_costs->dv_costs, MV_COST_WEIGHT_SUB);
    const int rate_mode = x->mode_costs.intrabc_cost[1];
    RD_STATS rd_stats_yuv, rd_stats_y, rd_stats_uv;
    if (!av1_txfm_search(cpi, x, bsize, &rd_stats_yuv, &rd_stats_y,
                         &rd_stats_uv, rate_mode + rate_mv, INT64_MAX))
      continue;
    rd_stats_yuv.rdcost =
        RDCOST(x->rdmult, rd_stats_yuv.rate, rd_stats_yuv.dist);
    if (rd_stats_yuv.rdcost < best_rd) {
      best_rd = rd_stats_yuv.rdcost;
      best_mbmi = *mbmi;
      best_rdstats = rd_stats_yuv;
      memcpy(best_blk_skip, txfm_info->blk_skip,
             sizeof(txfm_info->blk_skip[0]) * xd->height * xd->width);
      av1_copy_array(best_tx_type_map, xd->tx_type_map, xd->height * xd->width);
    }
  }
  *mbmi = best_mbmi;
  *rd_stats = best_rdstats;
  memcpy(txfm_info->blk_skip, best_blk_skip,
         sizeof(txfm_info->blk_skip[0]) * xd->height * xd->width);
  av1_copy_array(xd->tx_type_map, best_tx_type_map, ctx->num_4x4_blk);
#if CONFIG_RD_DEBUG
  mbmi->rd_stats = *rd_stats;
#endif
  return best_rd;
}

// TODO(chiyotsai@google.com): We are using struct $struct_name instead of their
// typedef here because Doxygen doesn't know about the typedefs yet. So using
// the typedef will prevent doxygen from finding this function and generating
// the callgraph. Once documents for AV1_COMP and MACROBLOCK are added to
// doxygen, we can revert back to using the typedefs.
void av1_rd_pick_intra_mode_sb(const struct AV1_COMP *cpi, struct macroblock *x,
                               struct RD_STATS *rd_cost, BLOCK_SIZE bsize,
                               PICK_MODE_CONTEXT *ctx, int64_t best_rd) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int num_planes = av1_num_planes(cm);
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  int rate_y = 0, rate_uv = 0, rate_y_tokenonly = 0, rate_uv_tokenonly = 0;
  uint8_t y_skip_txfm = 0, uv_skip_txfm = 0;
  int64_t dist_y = 0, dist_uv = 0;

  ctx->rd_stats.skip_txfm = 0;
  mbmi->ref_frame[0] = INTRA_FRAME;
  mbmi->ref_frame[1] = NONE_FRAME;
  mbmi->use_intrabc = 0;
  mbmi->mv[0].as_int = 0;
  mbmi->skip_mode = 0;

  const int64_t intra_yrd =
      av1_rd_pick_intra_sby_mode(cpi, x, &rate_y, &rate_y_tokenonly, &dist_y,
                                 &y_skip_txfm, bsize, best_rd, ctx);

  // Initialize default mode evaluation params
  set_mode_eval_params(cpi, x, DEFAULT_EVAL);

  if (intra_yrd < best_rd) {
    // Search intra modes for uv planes if needed
    if (num_planes > 1) {
      // Set up the tx variables for reproducing the y predictions in case we
      // need it for chroma-from-luma.
      if (xd->is_chroma_ref && store_cfl_required_rdo(cm, x)) {
        memcpy(txfm_info->blk_skip, ctx->blk_skip,
               sizeof(txfm_info->blk_skip[0]) * ctx->num_4x4_blk);
        av1_copy_array(xd->tx_type_map, ctx->tx_type_map, ctx->num_4x4_blk);
      }
      const TX_SIZE max_uv_tx_size = av1_get_tx_size(AOM_PLANE_U, xd);
      av1_rd_pick_intra_sbuv_mode(cpi, x, &rate_uv, &rate_uv_tokenonly,
                                  &dist_uv, &uv_skip_txfm, bsize,
                                  max_uv_tx_size);
    }

    // Intra block is always coded as non-skip
    rd_cost->rate =
        rate_y + rate_uv +
        x->mode_costs.skip_txfm_cost[av1_get_skip_txfm_context(xd)][0];
    rd_cost->dist = dist_y + dist_uv;
    rd_cost->rdcost = RDCOST(x->rdmult, rd_cost->rate, rd_cost->dist);
    rd_cost->skip_txfm = 0;
  } else {
    rd_cost->rate = INT_MAX;
  }

  if (rd_cost->rate != INT_MAX && rd_cost->rdcost < best_rd)
    best_rd = rd_cost->rdcost;
  if (rd_pick_intrabc_mode_sb(cpi, x, ctx, rd_cost, bsize, best_rd) < best_rd) {
    ctx->rd_stats.skip_txfm = mbmi->skip_txfm;
    memcpy(ctx->blk_skip, txfm_info->blk_skip,
           sizeof(txfm_info->blk_skip[0]) * ctx->num_4x4_blk);
    assert(rd_cost->rate != INT_MAX);
  }
  if (rd_cost->rate == INT_MAX) return;

  ctx->mic = *xd->mi[0];
  av1_copy_mbmi_ext_to_mbmi_ext_frame(&ctx->mbmi_ext_best, &x->mbmi_ext,
                                      av1_ref_frame_type(xd->mi[0]->ref_frame));
  av1_copy_array(ctx->tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
}

static inline void calc_target_weighted_pred(
    const AV1_COMMON *cm, const MACROBLOCK *x, const MACROBLOCKD *xd,
    const uint8_t *above, int above_stride, const uint8_t *left,
    int left_stride);

static inline void rd_pick_skip_mode(
    RD_STATS *rd_cost, InterModeSearchState *search_state,
    const AV1_COMP *const cpi, MACROBLOCK *const x, BLOCK_SIZE bsize,
    struct buf_2d yv12_mb[REF_FRAMES][MAX_MB_PLANE]) {
  const AV1_COMMON *const cm = &cpi->common;
  const SkipModeInfo *const skip_mode_info = &cm->current_frame.skip_mode_info;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];

  x->compound_idx = 1;  // COMPOUND_AVERAGE
  RD_STATS skip_mode_rd_stats;
  av1_invalid_rd_stats(&skip_mode_rd_stats);

  if (skip_mode_info->ref_frame_idx_0 == INVALID_IDX ||
      skip_mode_info->ref_frame_idx_1 == INVALID_IDX) {
    return;
  }

  const MV_REFERENCE_FRAME ref_frame =
      LAST_FRAME + skip_mode_info->ref_frame_idx_0;
  const MV_REFERENCE_FRAME second_ref_frame =
      LAST_FRAME + skip_mode_info->ref_frame_idx_1;
  const PREDICTION_MODE this_mode = NEAREST_NEARESTMV;
  const THR_MODES mode_index =
      get_prediction_mode_idx(this_mode, ref_frame, second_ref_frame);

  if (mode_index == THR_INVALID) {
    return;
  }

  if ((!cpi->oxcf.ref_frm_cfg.enable_onesided_comp ||
       cpi->sf.inter_sf.disable_onesided_comp) &&
      cpi->all_one_sided_refs) {
    return;
  }

  mbmi->mode = this_mode;
  mbmi->uv_mode = UV_DC_PRED;
  mbmi->ref_frame[0] = ref_frame;
  mbmi->ref_frame[1] = second_ref_frame;
  const uint8_t ref_frame_type = av1_ref_frame_type(mbmi->ref_frame);
  if (x->mbmi_ext.ref_mv_count[ref_frame_type] == UINT8_MAX) {
    MB_MODE_INFO_EXT *mbmi_ext = &x->mbmi_ext;
    if (mbmi_ext->ref_mv_count[ref_frame] == UINT8_MAX ||
        mbmi_ext->ref_mv_count[second_ref_frame] == UINT8_MAX) {
      return;
    }
    av1_find_mv_refs(cm, xd, mbmi, ref_frame_type, mbmi_ext->ref_mv_count,
                     xd->ref_mv_stack, xd->weight, NULL, mbmi_ext->global_mvs,
                     mbmi_ext->mode_context);
    // TODO(Ravi): Populate mbmi_ext->ref_mv_stack[ref_frame][4] and
    // mbmi_ext->weight[ref_frame][4] inside av1_find_mv_refs.
    av1_copy_usable_ref_mv_stack_and_weight(xd, mbmi_ext, ref_frame_type);
  }

  assert(this_mode == NEAREST_NEARESTMV);
  if (!build_cur_mv(mbmi->mv, this_mode, cm, x, 0)) {
    return;
  }

  mbmi->filter_intra_mode_info.use_filter_intra = 0;
  mbmi->interintra_mode = (INTERINTRA_MODE)(II_DC_PRED - 1);
  mbmi->comp_group_idx = 0;
  mbmi->compound_idx = x->compound_idx;
  mbmi->interinter_comp.type = COMPOUND_AVERAGE;
  mbmi->motion_mode = SIMPLE_TRANSLATION;
  mbmi->ref_mv_idx = 0;
  mbmi->skip_mode = mbmi->skip_txfm = 1;
  mbmi->palette_mode_info.palette_size[0] = 0;
  mbmi->palette_mode_info.palette_size[1] = 0;

  set_default_interp_filters(mbmi, cm->features.interp_filter);

  set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);
  for (int i = 0; i < num_planes; i++) {
    xd->plane[i].pre[0] = yv12_mb[mbmi->ref_frame[0]][i];
    xd->plane[i].pre[1] = yv12_mb[mbmi->ref_frame[1]][i];
  }

  BUFFER_SET orig_dst;
  for (int i = 0; i < num_planes; i++) {
    orig_dst.plane[i] = xd->plane[i].dst.buf;
    orig_dst.stride[i] = xd->plane[i].dst.stride;
  }

  // Compare the use of skip_mode with the best intra/inter mode obtained.
  const int skip_mode_ctx = av1_get_skip_mode_context(xd);
  int64_t best_intra_inter_mode_cost = INT64_MAX;
  if (rd_cost->dist < INT64_MAX && rd_cost->rate < INT32_MAX) {
    const ModeCosts *mode_costs = &x->mode_costs;
    best_intra_inter_mode_cost = RDCOST(
        x->rdmult, rd_cost->rate + mode_costs->skip_mode_cost[skip_mode_ctx][0],
        rd_cost->dist);
    // Account for non-skip mode rate in total rd stats
    rd_cost->rate += mode_costs->skip_mode_cost[skip_mode_ctx][0];
    av1_rd_cost_update(x->rdmult, rd_cost);
  }

  // Obtain the rdcost for skip_mode.
  skip_mode_rd(&skip_mode_rd_stats, cpi, x, bsize, &orig_dst,
               best_intra_inter_mode_cost);

  if (skip_mode_rd_stats.rdcost <= best_intra_inter_mode_cost &&
      (!xd->lossless[mbmi->segment_id] || skip_mode_rd_stats.dist == 0)) {
    assert(mode_index != THR_INVALID);
    search_state->best_mbmode.skip_mode = 1;
    search_state->best_mbmode = *mbmi;
    memset(search_state->best_mbmode.inter_tx_size,
           search_state->best_mbmode.tx_size,
           sizeof(search_state->best_mbmode.inter_tx_size));
    set_txfm_ctxs(search_state->best_mbmode.tx_size, xd->width, xd->height,
                  search_state->best_mbmode.skip_txfm && is_inter_block(mbmi),
                  xd);
    search_state->best_mode_index = mode_index;

    // Update rd_cost
    rd_cost->rate = skip_mode_rd_stats.rate;
    rd_cost->dist = rd_cost->sse = skip_mode_rd_stats.dist;
    rd_cost->rdcost = skip_mode_rd_stats.rdcost;

    search_state->best_rd = rd_cost->rdcost;
    search_state->best_skip2 = 1;
    search_state->best_mode_skippable = 1;

    x->txfm_search_info.skip_txfm = 1;
  }
}

// Get winner mode stats of given mode index
static inline MB_MODE_INFO *get_winner_mode_stats(
    MACROBLOCK *x, MB_MODE_INFO *best_mbmode, RD_STATS *best_rd_cost,
    int best_rate_y, int best_rate_uv, THR_MODES *best_mode_index,
    RD_STATS **winner_rd_cost, int *winner_rate_y, int *winner_rate_uv,
    THR_MODES *winner_mode_index, MULTI_WINNER_MODE_TYPE multi_winner_mode_type,
    int mode_idx) {
  MB_MODE_INFO *winner_mbmi;
  if (multi_winner_mode_type) {
    assert(mode_idx >= 0 && mode_idx < x->winner_mode_count);
    WinnerModeStats *winner_mode_stat = &x->winner_mode_stats[mode_idx];
    winner_mbmi = &winner_mode_stat->mbmi;

    *winner_rd_cost = &winner_mode_stat->rd_cost;
    *winner_rate_y = winner_mode_stat->rate_y;
    *winner_rate_uv = winner_mode_stat->rate_uv;
    *winner_mode_index = winner_mode_stat->mode_index;
  } else {
    winner_mbmi = best_mbmode;
    *winner_rd_cost = best_rd_cost;
    *winner_rate_y = best_rate_y;
    *winner_rate_uv = best_rate_uv;
    *winner_mode_index = *best_mode_index;
  }
  return winner_mbmi;
}

// speed feature: fast intra/inter transform type search
// Used for speed >= 2
// When this speed feature is on, in rd mode search, only DCT is used.
// After the mode is determined, this function is called, to select
// transform types and get accurate rdcost.
static inline void refine_winner_mode_tx(
    const AV1_COMP *cpi, MACROBLOCK *x, RD_STATS *rd_cost, BLOCK_SIZE bsize,
    PICK_MODE_CONTEXT *ctx, THR_MODES *best_mode_index,
    MB_MODE_INFO *best_mbmode, struct buf_2d yv12_mb[REF_FRAMES][MAX_MB_PLANE],
    int best_rate_y, int best_rate_uv, int *best_skip2, int winner_mode_count) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  TxfmSearchParams *txfm_params = &x->txfm_search_params;
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  int64_t best_rd;
  const int num_planes = av1_num_planes(cm);

  if (!is_winner_mode_processing_enabled(cpi, x, best_mbmode,
                                         rd_cost->skip_txfm))
    return;

  // Set params for winner mode evaluation
  set_mode_eval_params(cpi, x, WINNER_MODE_EVAL);

  // No best mode identified so far
  if (*best_mode_index == THR_INVALID) return;

  best_rd = RDCOST(x->rdmult, rd_cost->rate, rd_cost->dist);
  for (int mode_idx = 0; mode_idx < winner_mode_count; mode_idx++) {
    RD_STATS *winner_rd_stats = NULL;
    int winner_rate_y = 0, winner_rate_uv = 0;
    THR_MODES winner_mode_index = 0;

    // TODO(any): Combine best mode and multi-winner mode processing paths
    // Get winner mode stats for current mode index
    MB_MODE_INFO *winner_mbmi = get_winner_mode_stats(
        x, best_mbmode, rd_cost, best_rate_y, best_rate_uv, best_mode_index,
        &winner_rd_stats, &winner_rate_y, &winner_rate_uv, &winner_mode_index,
        cpi->sf.winner_mode_sf.multi_winner_mode_type, mode_idx);

    if (xd->lossless[winner_mbmi->segment_id] == 0 &&
        winner_mode_index != THR_INVALID &&
        is_winner_mode_processing_enabled(cpi, x, winner_mbmi,
                                          rd_cost->skip_txfm)) {
      RD_STATS rd_stats = *winner_rd_stats;
      int skip_blk = 0;
      RD_STATS rd_stats_y, rd_stats_uv;
      const int skip_ctx = av1_get_skip_txfm_context(xd);

      *mbmi = *winner_mbmi;

      set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);

      // Select prediction reference frames.
      for (int i = 0; i < num_planes; i++) {
        xd->plane[i].pre[0] = yv12_mb[mbmi->ref_frame[0]][i];
        if (has_second_ref(mbmi))
          xd->plane[i].pre[1] = yv12_mb[mbmi->ref_frame[1]][i];
      }

      if (is_inter_mode(mbmi->mode)) {
        const int mi_row = xd->mi_row;
        const int mi_col = xd->mi_col;
        bool is_predictor_built = false;
        const PREDICTION_MODE prediction_mode = mbmi->mode;
        // Do interpolation filter search for realtime mode if applicable.
        if (cpi->sf.winner_mode_sf.winner_mode_ifs &&
            cpi->oxcf.mode == REALTIME &&
            cm->current_frame.reference_mode == SINGLE_REFERENCE &&
            is_inter_mode(prediction_mode) &&
            mbmi->motion_mode == SIMPLE_TRANSLATION &&
            !is_inter_compound_mode(prediction_mode)) {
          is_predictor_built =
              fast_interp_search(cpi, x, mi_row, mi_col, bsize);
        }
        if (!is_predictor_built) {
          av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize, 0,
                                        av1_num_planes(cm) - 1);
        }
        if (mbmi->motion_mode == OBMC_CAUSAL)
          av1_build_obmc_inter_predictors_sb(cm, xd);

        av1_subtract_plane(x, bsize, 0);
        if (txfm_params->tx_mode_search_type == TX_MODE_SELECT &&
            !xd->lossless[mbmi->segment_id]) {
          av1_pick_recursive_tx_size_type_yrd(cpi, x, &rd_stats_y, bsize,
                                              INT64_MAX);
          assert(rd_stats_y.rate != INT_MAX);
        } else {
          av1_pick_uniform_tx_size_type_yrd(cpi, x, &rd_stats_y, bsize,
                                            INT64_MAX);
          memset(mbmi->inter_tx_size, mbmi->tx_size,
                 sizeof(mbmi->inter_tx_size));
          for (int i = 0; i < xd->height * xd->width; ++i)
            set_blk_skip(txfm_info->blk_skip, 0, i, rd_stats_y.skip_txfm);
        }
      } else {
        av1_pick_uniform_tx_size_type_yrd(cpi, x, &rd_stats_y, bsize,
                                          INT64_MAX);
      }

      if (num_planes > 1) {
        av1_txfm_uvrd(cpi, x, &rd_stats_uv, bsize, INT64_MAX);
      } else {
        av1_init_rd_stats(&rd_stats_uv);
      }

      const ModeCosts *mode_costs = &x->mode_costs;
      if (is_inter_mode(mbmi->mode) &&
          RDCOST(x->rdmult,
                 mode_costs->skip_txfm_cost[skip_ctx][0] + rd_stats_y.rate +
                     rd_stats_uv.rate,
                 (rd_stats_y.dist + rd_stats_uv.dist)) >
              RDCOST(x->rdmult, mode_costs->skip_txfm_cost[skip_ctx][1],
                     (rd_stats_y.sse + rd_stats_uv.sse))) {
        skip_blk = 1;
        rd_stats_y.rate = mode_costs->skip_txfm_cost[skip_ctx][1];
        rd_stats_uv.rate = 0;
        rd_stats_y.dist = rd_stats_y.sse;
        rd_stats_uv.dist = rd_stats_uv.sse;
      } else {
        skip_blk = 0;
        rd_stats_y.rate += mode_costs->skip_txfm_cost[skip_ctx][0];
      }
      int this_rate = rd_stats.rate + rd_stats_y.rate + rd_stats_uv.rate -
                      winner_rate_y - winner_rate_uv;
      int64_t this_rd =
          RDCOST(x->rdmult, this_rate, (rd_stats_y.dist + rd_stats_uv.dist));
      if (best_rd > this_rd) {
        *best_mbmode = *mbmi;
        *best_mode_index = winner_mode_index;
        av1_copy_array(ctx->blk_skip, txfm_info->blk_skip, ctx->num_4x4_blk);
        av1_copy_array(ctx->tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
        rd_cost->rate = this_rate;
        rd_cost->dist = rd_stats_y.dist + rd_stats_uv.dist;
        rd_cost->sse = rd_stats_y.sse + rd_stats_uv.sse;
        rd_cost->rdcost = this_rd;
        best_rd = this_rd;
        *best_skip2 = skip_blk;
      }
    }
  }
}

/*!\cond */
typedef struct {
  // Mask for each reference frame, specifying which prediction modes to NOT try
  // during search.
  uint32_t pred_modes[REF_FRAMES];
  // If ref_combo[i][j + 1] is true, do NOT try prediction using combination of
  // reference frames (i, j).
  // Note: indexing with 'j + 1' is due to the fact that 2nd reference can be -1
  // (NONE_FRAME).
  bool ref_combo[REF_FRAMES][REF_FRAMES + 1];
} mode_skip_mask_t;
/*!\endcond */

// Update 'ref_combo' mask to disable given 'ref' in single and compound modes.
static inline void disable_reference(
    MV_REFERENCE_FRAME ref, bool ref_combo[REF_FRAMES][REF_FRAMES + 1]) {
  for (MV_REFERENCE_FRAME ref2 = NONE_FRAME; ref2 < REF_FRAMES; ++ref2) {
    ref_combo[ref][ref2 + 1] = true;
  }
}

// Update 'ref_combo' mask to disable all inter references except ALTREF.
static inline void disable_inter_references_except_altref(
    bool ref_combo[REF_FRAMES][REF_FRAMES + 1]) {
  disable_reference(LAST_FRAME, ref_combo);
  disable_reference(LAST2_FRAME, ref_combo);
  disable_reference(LAST3_FRAME, ref_combo);
  disable_reference(GOLDEN_FRAME, ref_combo);
  disable_reference(BWDREF_FRAME, ref_combo);
  disable_reference(ALTREF2_FRAME, ref_combo);
}

static const MV_REFERENCE_FRAME reduced_ref_combos[][2] = {
  { LAST_FRAME, NONE_FRAME },     { ALTREF_FRAME, NONE_FRAME },
  { LAST_FRAME, ALTREF_FRAME },   { GOLDEN_FRAME, NONE_FRAME },
  { INTRA_FRAME, NONE_FRAME },    { GOLDEN_FRAME, ALTREF_FRAME },
  { LAST_FRAME, GOLDEN_FRAME },   { LAST_FRAME, INTRA_FRAME },
  { LAST_FRAME, BWDREF_FRAME },   { LAST_FRAME, LAST3_FRAME },
  { GOLDEN_FRAME, BWDREF_FRAME }, { GOLDEN_FRAME, INTRA_FRAME },
  { BWDREF_FRAME, NONE_FRAME },   { BWDREF_FRAME, ALTREF_FRAME },
  { ALTREF_FRAME, INTRA_FRAME },  { BWDREF_FRAME, INTRA_FRAME },
};

typedef enum { REF_SET_FULL, REF_SET_REDUCED, REF_SET_REALTIME } REF_SET;

static inline void default_skip_mask(mode_skip_mask_t *mask, REF_SET ref_set) {
  if (ref_set == REF_SET_FULL) {
    // Everything available by default.
    memset(mask, 0, sizeof(*mask));
  } else {
    // All modes available by default.
    memset(mask->pred_modes, 0, sizeof(mask->pred_modes));
    // All references disabled first.
    for (MV_REFERENCE_FRAME ref1 = INTRA_FRAME; ref1 < REF_FRAMES; ++ref1) {
      for (MV_REFERENCE_FRAME ref2 = NONE_FRAME; ref2 < REF_FRAMES; ++ref2) {
        mask->ref_combo[ref1][ref2 + 1] = true;
      }
    }
    const MV_REFERENCE_FRAME(*ref_set_combos)[2];
    int num_ref_combos;

    // Then enable reduced set of references explicitly.
    switch (ref_set) {
      case REF_SET_REDUCED:
        ref_set_combos = reduced_ref_combos;
        num_ref_combos =
            (int)sizeof(reduced_ref_combos) / sizeof(reduced_ref_combos[0]);
        break;
      case REF_SET_REALTIME:
        ref_set_combos = real_time_ref_combos;
        num_ref_combos =
            (int)sizeof(real_time_ref_combos) / sizeof(real_time_ref_combos[0]);
        break;
      default: assert(0); num_ref_combos = 0;
    }

    for (int i = 0; i < num_ref_combos; ++i) {
      const MV_REFERENCE_FRAME *const this_combo = ref_set_combos[i];
      mask->ref_combo[this_combo[0]][this_combo[1] + 1] = false;
    }
  }
}

static inline void init_mode_skip_mask(mode_skip_mask_t *mask,
                                       const AV1_COMP *cpi, MACROBLOCK *x,
                                       BLOCK_SIZE bsize) {
  const AV1_COMMON *const cm = &cpi->common;
  const struct segmentation *const seg = &cm->seg;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  unsigned char segment_id = mbmi->segment_id;
  const SPEED_FEATURES *const sf = &cpi->sf;
  const INTER_MODE_SPEED_FEATURES *const inter_sf = &sf->inter_sf;
  REF_SET ref_set = REF_SET_FULL;

  if (sf->rt_sf.use_real_time_ref_set)
    ref_set = REF_SET_REALTIME;
  else if (cpi->oxcf.ref_frm_cfg.enable_reduced_reference_set)
    ref_set = REF_SET_REDUCED;

  default_skip_mask(mask, ref_set);

  int min_pred_mv_sad = INT_MAX;
  MV_REFERENCE_FRAME ref_frame;
  if (ref_set == REF_SET_REALTIME) {
    // For real-time encoding, we only look at a subset of ref frames. So the
    // threshold for pruning should be computed from this subset as well.
    const int num_rt_refs =
        sizeof(real_time_ref_combos) / sizeof(*real_time_ref_combos);
    for (int r_idx = 0; r_idx < num_rt_refs; r_idx++) {
      const MV_REFERENCE_FRAME ref = real_time_ref_combos[r_idx][0];
      if (ref != INTRA_FRAME) {
        min_pred_mv_sad = AOMMIN(min_pred_mv_sad, x->pred_mv_sad[ref]);
      }
    }
  } else {
    for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ++ref_frame)
      min_pred_mv_sad = AOMMIN(min_pred_mv_sad, x->pred_mv_sad[ref_frame]);
  }

  for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ++ref_frame) {
    if (!(cpi->ref_frame_flags & av1_ref_frame_flag_list[ref_frame])) {
      // Skip checking missing reference in both single and compound reference
      // modes.
      disable_reference(ref_frame, mask->ref_combo);
    } else {
      // Skip fixed mv modes for poor references
      if ((x->pred_mv_sad[ref_frame] >> 2) > min_pred_mv_sad) {
        mask->pred_modes[ref_frame] |= INTER_NEAREST_NEAR_ZERO;
      }
    }
    if (segfeature_active(seg, segment_id, SEG_LVL_REF_FRAME) &&
        get_segdata(seg, segment_id, SEG_LVL_REF_FRAME) != (int)ref_frame) {
      // Reference not used for the segment.
      disable_reference(ref_frame, mask->ref_combo);
    }
  }
  // Note: We use the following drop-out only if the SEG_LVL_REF_FRAME feature
  // is disabled for this segment. This is to prevent the possibility that we
  // end up unable to pick any mode.
  if (!segfeature_active(seg, segment_id, SEG_LVL_REF_FRAME)) {
    // Only consider GLOBALMV/ALTREF_FRAME for alt ref frame,
    // unless ARNR filtering is enabled in which case we want
    // an unfiltered alternative. We allow near/nearest as well
    // because they may result in zero-zero MVs but be cheaper.
    if (cpi->rc.is_src_frame_alt_ref &&
        (cpi->oxcf.algo_cfg.arnr_max_frames == 0)) {
      disable_inter_references_except_altref(mask->ref_combo);

      mask->pred_modes[ALTREF_FRAME] = ~INTER_NEAREST_NEAR_ZERO;
      const MV_REFERENCE_FRAME tmp_ref_frames[2] = { ALTREF_FRAME, NONE_FRAME };
      int_mv near_mv, nearest_mv, global_mv;
      get_this_mv(&nearest_mv, NEARESTMV, 0, 0, 0, tmp_ref_frames,
                  &x->mbmi_ext);
      get_this_mv(&near_mv, NEARMV, 0, 0, 0, tmp_ref_frames, &x->mbmi_ext);
      get_this_mv(&global_mv, GLOBALMV, 0, 0, 0, tmp_ref_frames, &x->mbmi_ext);

      if (near_mv.as_int != global_mv.as_int)
        mask->pred_modes[ALTREF_FRAME] |= (1 << NEARMV);
      if (nearest_mv.as_int != global_mv.as_int)
        mask->pred_modes[ALTREF_FRAME] |= (1 << NEARESTMV);
    }
  }

  if (cpi->rc.is_src_frame_alt_ref) {
    if (inter_sf->alt_ref_search_fp &&
        (cpi->ref_frame_flags & av1_ref_frame_flag_list[ALTREF_FRAME])) {
      mask->pred_modes[ALTREF_FRAME] = 0;
      disable_inter_references_except_altref(mask->ref_combo);
      disable_reference(INTRA_FRAME, mask->ref_combo);
    }
  }

  if (inter_sf->alt_ref_search_fp) {
    if (!cm->show_frame && x->best_pred_mv_sad[0] < INT_MAX) {
      int sad_thresh = x->best_pred_mv_sad[0] + (x->best_pred_mv_sad[0] >> 3);
      // Conservatively skip the modes w.r.t. BWDREF, ALTREF2 and ALTREF, if
      // those are past frames
      MV_REFERENCE_FRAME start_frame =
          inter_sf->alt_ref_search_fp == 1 ? ALTREF2_FRAME : BWDREF_FRAME;
      for (ref_frame = start_frame; ref_frame <= ALTREF_FRAME; ref_frame++) {
        if (cpi->ref_frame_dist_info.ref_relative_dist[ref_frame - LAST_FRAME] <
            0) {
          // Prune inter modes when relative dist of ALTREF2 and ALTREF is close
          // to the relative dist of LAST_FRAME.
          if (inter_sf->alt_ref_search_fp == 1 &&
              (abs(cpi->ref_frame_dist_info
                       .ref_relative_dist[ref_frame - LAST_FRAME]) >
               1.5 * abs(cpi->ref_frame_dist_info
                             .ref_relative_dist[LAST_FRAME - LAST_FRAME]))) {
            continue;
          }
          if (x->pred_mv_sad[ref_frame] > sad_thresh)
            mask->pred_modes[ref_frame] |= INTER_ALL;
        }
      }
    }
  }

  if (sf->rt_sf.prune_inter_modes_wrt_gf_arf_based_on_sad) {
    if (x->best_pred_mv_sad[0] < INT_MAX) {
      int sad_thresh = x->best_pred_mv_sad[0] + (x->best_pred_mv_sad[0] >> 1);
      const int prune_ref_list[2] = { GOLDEN_FRAME, ALTREF_FRAME };

      // Conservatively skip the modes w.r.t. GOLDEN and ALTREF references
      for (int ref_idx = 0; ref_idx < 2; ref_idx++) {
        ref_frame = prune_ref_list[ref_idx];
        if (x->pred_mv_sad[ref_frame] > sad_thresh)
          mask->pred_modes[ref_frame] |= INTER_NEAREST_NEAR_ZERO;
      }
    }
  }

  if (bsize > sf->part_sf.max_intra_bsize) {
    disable_reference(INTRA_FRAME, mask->ref_combo);
  }

  if (!cpi->oxcf.tool_cfg.enable_global_motion) {
    for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ++ref_frame) {
      mask->pred_modes[ref_frame] |= (1 << GLOBALMV);
      mask->pred_modes[ref_frame] |= (1 << GLOBAL_GLOBALMV);
    }
  }

  mask->pred_modes[INTRA_FRAME] |=
      ~(uint32_t)sf->intra_sf.intra_y_mode_mask[max_txsize_lookup[bsize]];

  // Prune reference frames which are not the closest to the current
  // frame and with large pred_mv_sad.
  if (inter_sf->prune_single_ref) {
    assert(inter_sf->prune_single_ref > 0 && inter_sf->prune_single_ref < 3);
    const double prune_threshes[2] = { 1.20, 1.05 };

    for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ++ref_frame) {
      const RefFrameDistanceInfo *const ref_frame_dist_info =
          &cpi->ref_frame_dist_info;
      const int is_closest_ref =
          (ref_frame == ref_frame_dist_info->nearest_past_ref) ||
          (ref_frame == ref_frame_dist_info->nearest_future_ref);

      if (!is_closest_ref) {
        const int dir =
            (ref_frame_dist_info->ref_relative_dist[ref_frame - LAST_FRAME] < 0)
                ? 0
                : 1;
        if (x->best_pred_mv_sad[dir] < INT_MAX &&
            x->pred_mv_sad[ref_frame] >
                prune_threshes[inter_sf->prune_single_ref - 1] *
                    x->best_pred_mv_sad[dir])
          mask->pred_modes[ref_frame] |= INTER_SINGLE_ALL;
      }
    }
  }
}

static inline void init_neighbor_pred_buf(const OBMCBuffer *const obmc_buffer,
                                          HandleInterModeArgs *const args,
                                          int is_hbd) {
  if (is_hbd) {
    const int len = sizeof(uint16_t);
    args->above_pred_buf[0] = CONVERT_TO_BYTEPTR(obmc_buffer->above_pred);
    args->above_pred_buf[1] = CONVERT_TO_BYTEPTR(obmc_buffer->above_pred +
                                                 (MAX_SB_SQUARE >> 1) * len);
    args->above_pred_buf[2] =
        CONVERT_TO_BYTEPTR(obmc_buffer->above_pred + MAX_SB_SQUARE * len);
    args->left_pred_buf[0] = CONVERT_TO_BYTEPTR(obmc_buffer->left_pred);
    args->left_pred_buf[1] =
        CONVERT_TO_BYTEPTR(obmc_buffer->left_pred + (MAX_SB_SQUARE >> 1) * len);
    args->left_pred_buf[2] =
        CONVERT_TO_BYTEPTR(obmc_buffer->left_pred + MAX_SB_SQUARE * len);
  } else {
    args->above_pred_buf[0] = obmc_buffer->above_pred;
    args->above_pred_buf[1] = obmc_buffer->above_pred + (MAX_SB_SQUARE >> 1);
    args->above_pred_buf[2] = obmc_buffer->above_pred + MAX_SB_SQUARE;
    args->left_pred_buf[0] = obmc_buffer->left_pred;
    args->left_pred_buf[1] = obmc_buffer->left_pred + (MAX_SB_SQUARE >> 1);
    args->left_pred_buf[2] = obmc_buffer->left_pred + MAX_SB_SQUARE;
  }
}

static inline int prune_ref_frame(const AV1_COMP *cpi, const MACROBLOCK *x,
                                  MV_REFERENCE_FRAME ref_frame) {
  const AV1_COMMON *const cm = &cpi->common;
  MV_REFERENCE_FRAME rf[2];
  av1_set_ref_frame(rf, ref_frame);

  if ((cpi->prune_ref_frame_mask >> ref_frame) & 1) return 1;

  if (prune_ref_by_selective_ref_frame(cpi, x, rf,
                                       cm->cur_frame->ref_display_order_hint)) {
    return 1;
  }

  return 0;
}

static inline int is_ref_frame_used_by_compound_ref(int ref_frame,
                                                    int skip_ref_frame_mask) {
  for (int r = ALTREF_FRAME + 1; r < MODE_CTX_REF_FRAMES; ++r) {
    if (!(skip_ref_frame_mask & (1 << r))) {
      const MV_REFERENCE_FRAME *rf = ref_frame_map[r - REF_FRAMES];
      if (rf[0] == ref_frame || rf[1] == ref_frame) {
        return 1;
      }
    }
  }
  return 0;
}

static inline int is_ref_frame_used_in_cache(MV_REFERENCE_FRAME ref_frame,
                                             const MB_MODE_INFO *mi_cache) {
  if (!mi_cache) {
    return 0;
  }

  if (ref_frame < REF_FRAMES) {
    return (ref_frame == mi_cache->ref_frame[0] ||
            ref_frame == mi_cache->ref_frame[1]);
  }

  // if we are here, then the current mode is compound.
  MV_REFERENCE_FRAME cached_ref_type = av1_ref_frame_type(mi_cache->ref_frame);
  return ref_frame == cached_ref_type;
}

// Please add/modify parameter setting in this function, making it consistent
// and easy to read and maintain.
static inline void set_params_rd_pick_inter_mode(
    const AV1_COMP *cpi, MACROBLOCK *x, HandleInterModeArgs *args,
    BLOCK_SIZE bsize, mode_skip_mask_t *mode_skip_mask, int skip_ref_frame_mask,
    unsigned int *ref_costs_single, unsigned int (*ref_costs_comp)[REF_FRAMES],
    struct buf_2d (*yv12_mb)[MAX_MB_PLANE]) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  MB_MODE_INFO_EXT *const mbmi_ext = &x->mbmi_ext;
  unsigned char segment_id = mbmi->segment_id;

  init_neighbor_pred_buf(&x->obmc_buffer, args, is_cur_buf_hbd(&x->e_mbd));
  av1_collect_neighbors_ref_counts(xd);
  estimate_ref_frame_costs(cm, xd, &x->mode_costs, segment_id, ref_costs_single,
                           ref_costs_comp);

  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  x->best_pred_mv_sad[0] = INT_MAX;
  x->best_pred_mv_sad[1] = INT_MAX;

  for (MV_REFERENCE_FRAME ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME;
       ++ref_frame) {
    x->pred_mv_sad[ref_frame] = INT_MAX;
    mbmi_ext->mode_context[ref_frame] = 0;
    mbmi_ext->ref_mv_count[ref_frame] = UINT8_MAX;
    if (cpi->ref_frame_flags & av1_ref_frame_flag_list[ref_frame]) {
      // Skip the ref frame if the mask says skip and the ref is not used by
      // compound ref.
      if (skip_ref_frame_mask & (1 << ref_frame) &&
          !is_ref_frame_used_by_compound_ref(ref_frame, skip_ref_frame_mask) &&
          !is_ref_frame_used_in_cache(ref_frame, x->mb_mode_cache)) {
        continue;
      }
      assert(get_ref_frame_yv12_buf(cm, ref_frame) != NULL);
      setup_buffer_ref_mvs_inter(cpi, x, ref_frame, bsize, yv12_mb);
    }
    if (cpi->sf.inter_sf.alt_ref_search_fp ||
        cpi->sf.inter_sf.prune_single_ref ||
        cpi->sf.rt_sf.prune_inter_modes_wrt_gf_arf_based_on_sad) {
      // Store the best pred_mv_sad across all past frames
      if (cpi->ref_frame_dist_info.ref_relative_dist[ref_frame - LAST_FRAME] <
          0)
        x->best_pred_mv_sad[0] =
            AOMMIN(x->best_pred_mv_sad[0], x->pred_mv_sad[ref_frame]);
      else
        // Store the best pred_mv_sad across all future frames
        x->best_pred_mv_sad[1] =
            AOMMIN(x->best_pred_mv_sad[1], x->pred_mv_sad[ref_frame]);
    }
  }

  if (!cpi->sf.rt_sf.use_real_time_ref_set && is_comp_ref_allowed(bsize)) {
    // No second reference on RT ref set, so no need to initialize
    for (MV_REFERENCE_FRAME ref_frame = EXTREF_FRAME;
         ref_frame < MODE_CTX_REF_FRAMES; ++ref_frame) {
      mbmi_ext->mode_context[ref_frame] = 0;
      mbmi_ext->ref_mv_count[ref_frame] = UINT8_MAX;
      const MV_REFERENCE_FRAME *rf = ref_frame_map[ref_frame - REF_FRAMES];
      if (!((cpi->ref_frame_flags & av1_ref_frame_flag_list[rf[0]]) &&
            (cpi->ref_frame_flags & av1_ref_frame_flag_list[rf[1]]))) {
        continue;
      }

      if (skip_ref_frame_mask & (1 << ref_frame) &&
          !is_ref_frame_used_in_cache(ref_frame, x->mb_mode_cache)) {
        continue;
      }
      // Ref mv list population is not required, when compound references are
      // pruned.
      if (prune_ref_frame(cpi, x, ref_frame)) continue;

      av1_find_mv_refs(cm, xd, mbmi, ref_frame, mbmi_ext->ref_mv_count,
                       xd->ref_mv_stack, xd->weight, NULL, mbmi_ext->global_mvs,
                       mbmi_ext->mode_context);
      // TODO(Ravi): Populate mbmi_ext->ref_mv_stack[ref_frame][4] and
      // mbmi_ext->weight[ref_frame][4] inside av1_find_mv_refs.
      av1_copy_usable_ref_mv_stack_and_weight(xd, mbmi_ext, ref_frame);
    }
  }

  av1_count_overlappable_neighbors(cm, xd);
  const FRAME_UPDATE_TYPE update_type =
      get_frame_update_type(&cpi->ppi->gf_group, cpi->gf_frame_index);
  int use_actual_frame_probs = 1;
  int prune_obmc;
#if CONFIG_FPMT_TEST
  use_actual_frame_probs =
      (cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE) ? 0 : 1;
  if (!use_actual_frame_probs) {
    prune_obmc = cpi->ppi->temp_frame_probs.obmc_probs[update_type][bsize] <
                 cpi->sf.inter_sf.prune_obmc_prob_thresh;
  }
#endif
  if (use_actual_frame_probs) {
    prune_obmc = cpi->ppi->frame_probs.obmc_probs[update_type][bsize] <
                 cpi->sf.inter_sf.prune_obmc_prob_thresh;
  }
  if (cpi->oxcf.motion_mode_cfg.enable_obmc && !prune_obmc) {
    if (check_num_overlappable_neighbors(mbmi) &&
        is_motion_variation_allowed_bsize(bsize)) {
      int dst_width1[MAX_MB_PLANE] = { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE };
      int dst_width2[MAX_MB_PLANE] = { MAX_SB_SIZE >> 1, MAX_SB_SIZE >> 1,
                                       MAX_SB_SIZE >> 1 };
      int dst_height1[MAX_MB_PLANE] = { MAX_SB_SIZE >> 1, MAX_SB_SIZE >> 1,
                                        MAX_SB_SIZE >> 1 };
      int dst_height2[MAX_MB_PLANE] = { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE };
      av1_build_prediction_by_above_preds(cm, xd, args->above_pred_buf,
                                          dst_width1, dst_height1,
                                          args->above_pred_stride);
      av1_build_prediction_by_left_preds(cm, xd, args->left_pred_buf,
                                         dst_width2, dst_height2,
                                         args->left_pred_stride);
      const int num_planes = av1_num_planes(cm);
      av1_setup_dst_planes(xd->plane, bsize, &cm->cur_frame->buf, mi_row,
                           mi_col, 0, num_planes);
      calc_target_weighted_pred(
          cm, x, xd, args->above_pred_buf[0], args->above_pred_stride[0],
          args->left_pred_buf[0], args->left_pred_stride[0]);
    }
  }

  init_mode_skip_mask(mode_skip_mask, cpi, x, bsize);

  // Set params for mode evaluation
  set_mode_eval_params(cpi, x, MODE_EVAL);

  x->comp_rd_stats_idx = 0;

  for (int idx = 0; idx < REF_FRAMES; idx++) {
    args->best_single_sse_in_refs[idx] = INT32_MAX;
  }
}

static inline void init_single_inter_mode_search_state(
    InterModeSearchState *search_state) {
  for (int dir = 0; dir < 2; ++dir) {
    for (int mode = 0; mode < SINGLE_INTER_MODE_NUM; ++mode) {
      for (int ref_frame = 0; ref_frame < FWD_REFS; ++ref_frame) {
        SingleInterModeState *state;

        state = &search_state->single_state[dir][mode][ref_frame];
        state->ref_frame = NONE_FRAME;
        state->rd = INT64_MAX;

        state = &search_state->single_state_modelled[dir][mode][ref_frame];
        state->ref_frame = NONE_FRAME;
        state->rd = INT64_MAX;

        search_state->single_rd_order[dir][mode][ref_frame] = NONE_FRAME;
      }
    }
  }

  for (int ref_frame = 0; ref_frame < REF_FRAMES; ++ref_frame) {
    search_state->best_single_rd[ref_frame] = INT64_MAX;
    search_state->best_single_mode[ref_frame] = PRED_MODE_INVALID;
  }
  av1_zero(search_state->single_state_cnt);
  av1_zero(search_state->single_state_modelled_cnt);
}

static inline void init_inter_mode_search_state(
    InterModeSearchState *search_state, const AV1_COMP *cpi,
    const MACROBLOCK *x, BLOCK_SIZE bsize, int64_t best_rd_so_far) {
  init_intra_mode_search_state(&search_state->intra_search_state);
  av1_invalid_rd_stats(&search_state->best_y_rdcost);

  search_state->best_rd = best_rd_so_far;
  search_state->best_skip_rd[0] = INT64_MAX;
  search_state->best_skip_rd[1] = INT64_MAX;

  av1_zero(search_state->best_mbmode);

  search_state->best_rate_y = INT_MAX;

  search_state->best_rate_uv = INT_MAX;

  search_state->best_mode_skippable = 0;

  search_state->best_skip2 = 0;

  search_state->best_mode_index = THR_INVALID;

  const MACROBLOCKD *const xd = &x->e_mbd;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const unsigned char segment_id = mbmi->segment_id;

  search_state->num_available_refs = 0;
  memset(search_state->dist_refs, -1, sizeof(search_state->dist_refs));
  memset(search_state->dist_order_refs, -1,
         sizeof(search_state->dist_order_refs));

  for (int i = 0; i <= LAST_NEW_MV_INDEX; ++i)
    search_state->mode_threshold[i] = 0;
  const int *const rd_threshes = cpi->rd.threshes[segment_id][bsize];
  for (int i = LAST_NEW_MV_INDEX + 1; i < SINGLE_REF_MODE_END; ++i)
    search_state->mode_threshold[i] =
        ((int64_t)rd_threshes[i] * x->thresh_freq_fact[bsize][i]) >>
        RD_THRESH_FAC_FRAC_BITS;

  search_state->best_intra_rd = INT64_MAX;

  search_state->best_pred_sse = UINT_MAX;

  av1_zero(search_state->single_newmv);
  av1_zero(search_state->single_newmv_rate);
  av1_zero(search_state->single_newmv_valid);
  for (int i = SINGLE_INTER_MODE_START; i < SINGLE_INTER_MODE_END; ++i) {
    for (int j = 0; j < MAX_REF_MV_SEARCH; ++j) {
      for (int ref_frame = 0; ref_frame < REF_FRAMES; ++ref_frame) {
        search_state->modelled_rd[i][j][ref_frame] = INT64_MAX;
        search_state->simple_rd[i][j][ref_frame] = INT64_MAX;
      }
    }
  }

  for (int i = 0; i < REFERENCE_MODES; ++i) {
    search_state->best_pred_rd[i] = INT64_MAX;
  }

  if (cpi->common.current_frame.reference_mode != SINGLE_REFERENCE) {
    for (int i = SINGLE_REF_MODE_END; i < THR_INTER_MODE_END; ++i)
      search_state->mode_threshold[i] =
          ((int64_t)rd_threshes[i] * x->thresh_freq_fact[bsize][i]) >>
          RD_THRESH_FAC_FRAC_BITS;

    for (int i = COMP_INTER_MODE_START; i < COMP_INTER_MODE_END; ++i) {
      for (int j = 0; j < MAX_REF_MV_SEARCH; ++j) {
        for (int ref_frame = 0; ref_frame < REF_FRAMES; ++ref_frame) {
          search_state->modelled_rd[i][j][ref_frame] = INT64_MAX;
          search_state->simple_rd[i][j][ref_frame] = INT64_MAX;
        }
      }
    }

    init_single_inter_mode_search_state(search_state);
  }
}

static bool mask_says_skip(const mode_skip_mask_t *mode_skip_mask,
                           const MV_REFERENCE_FRAME *ref_frame,
                           const PREDICTION_MODE this_mode) {
  if (mode_skip_mask->pred_modes[ref_frame[0]] & (1 << this_mode)) {
    return true;
  }

  return mode_skip_mask->ref_combo[ref_frame[0]][ref_frame[1] + 1];
}

static int inter_mode_compatible_skip(const AV1_COMP *cpi, const MACROBLOCK *x,
                                      BLOCK_SIZE bsize,
                                      PREDICTION_MODE curr_mode,
                                      const MV_REFERENCE_FRAME *ref_frames) {
  const int comp_pred = ref_frames[1] > INTRA_FRAME;
  if (comp_pred) {
    if (!is_comp_ref_allowed(bsize)) return 1;
    if (!(cpi->ref_frame_flags & av1_ref_frame_flag_list[ref_frames[1]])) {
      return 1;
    }

    const AV1_COMMON *const cm = &cpi->common;
    if (frame_is_intra_only(cm)) return 1;

    const CurrentFrame *const current_frame = &cm->current_frame;
    if (current_frame->reference_mode == SINGLE_REFERENCE) return 1;

    const struct segmentation *const seg = &cm->seg;
    const unsigned char segment_id = x->e_mbd.mi[0]->segment_id;
    // Do not allow compound prediction if the segment level reference frame
    // feature is in use as in this case there can only be one reference.
    if (segfeature_active(seg, segment_id, SEG_LVL_REF_FRAME)) return 1;
  }

  if (ref_frames[0] > INTRA_FRAME && ref_frames[1] == INTRA_FRAME) {
    // Mode must be compatible
    if (!is_interintra_allowed_bsize(bsize)) return 1;
    if (!is_interintra_allowed_mode(curr_mode)) return 1;
  }

  return 0;
}

static int fetch_picked_ref_frames_mask(const MACROBLOCK *const x,
                                        BLOCK_SIZE bsize, int mib_size) {
  const int sb_size_mask = mib_size - 1;
  const MACROBLOCKD *const xd = &x->e_mbd;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int mi_row_in_sb = mi_row & sb_size_mask;
  const int mi_col_in_sb = mi_col & sb_size_mask;
  const int mi_w = mi_size_wide[bsize];
  const int mi_h = mi_size_high[bsize];
  int picked_ref_frames_mask = 0;
  for (int i = mi_row_in_sb; i < mi_row_in_sb + mi_h; ++i) {
    for (int j = mi_col_in_sb; j < mi_col_in_sb + mi_w; ++j) {
      picked_ref_frames_mask |= x->picked_ref_frames_mask[i * 32 + j];
    }
  }
  return picked_ref_frames_mask;
}

// Check if reference frame pair of the current block matches with the given
// block.
static inline int match_ref_frame_pair(const MB_MODE_INFO *mbmi,
                                       const MV_REFERENCE_FRAME *ref_frames) {
  return ((ref_frames[0] == mbmi->ref_frame[0]) &&
          (ref_frames[1] == mbmi->ref_frame[1]));
}

// Case 1: return 0, means don't skip this mode
// Case 2: return 1, means skip this mode completely
// Case 3: return 2, means skip compound only, but still try single motion modes
static int inter_mode_search_order_independent_skip(
    const AV1_COMP *cpi, const MACROBLOCK *x, mode_skip_mask_t *mode_skip_mask,
    InterModeSearchState *search_state, int skip_ref_frame_mask,
    PREDICTION_MODE mode, const MV_REFERENCE_FRAME *ref_frame) {
  if (mask_says_skip(mode_skip_mask, ref_frame, mode)) {
    return 1;
  }

  const int ref_type = av1_ref_frame_type(ref_frame);
  if (!cpi->sf.rt_sf.use_real_time_ref_set)
    if (prune_ref_frame(cpi, x, ref_type)) return 1;

  // This is only used in motion vector unit test.
  if (cpi->oxcf.unit_test_cfg.motion_vector_unit_test &&
      ref_frame[0] == INTRA_FRAME)
    return 1;

  const AV1_COMMON *const cm = &cpi->common;
  if (skip_repeated_mv(cm, x, mode, ref_frame, search_state)) {
    return 1;
  }

  // Reuse the prediction mode in cache
  if (x->use_mb_mode_cache) {
    const MB_MODE_INFO *cached_mi = x->mb_mode_cache;
    const PREDICTION_MODE cached_mode = cached_mi->mode;
    const MV_REFERENCE_FRAME *cached_frame = cached_mi->ref_frame;
    const int cached_mode_is_single = cached_frame[1] <= INTRA_FRAME;

    // If the cached mode is intra, then we just need to match the mode.
    if (is_mode_intra(cached_mode) && mode != cached_mode) {
      return 1;
    }

    // If the cached mode is single inter mode, then we match the mode and
    // reference frame.
    if (cached_mode_is_single) {
      if (mode != cached_mode || ref_frame[0] != cached_frame[0]) {
        return 1;
      }
    } else {
      // If the cached mode is compound, then we need to consider several cases.
      const int mode_is_single = ref_frame[1] <= INTRA_FRAME;
      if (mode_is_single) {
        // If the mode is single, we know the modes can't match. But we might
        // still want to search it if compound mode depends on the current mode.
        int skip_motion_mode_only = 0;
        if (cached_mode == NEW_NEARMV || cached_mode == NEW_NEARESTMV) {
          skip_motion_mode_only = (ref_frame[0] == cached_frame[0]);
        } else if (cached_mode == NEAR_NEWMV || cached_mode == NEAREST_NEWMV) {
          skip_motion_mode_only = (ref_frame[0] == cached_frame[1]);
        } else if (cached_mode == NEW_NEWMV) {
          skip_motion_mode_only = (ref_frame[0] == cached_frame[0] ||
                                   ref_frame[0] == cached_frame[1]);
        }

        return 1 + skip_motion_mode_only;
      } else {
        // If both modes are compound, then everything must match.
        if (mode != cached_mode || ref_frame[0] != cached_frame[0] ||
            ref_frame[1] != cached_frame[1]) {
          return 1;
        }
      }
    }
  }

  const MB_MODE_INFO *const mbmi = x->e_mbd.mi[0];
  // If no valid mode has been found so far in PARTITION_NONE when finding a
  // valid partition is required, do not skip mode.
  if (search_state->best_rd == INT64_MAX && mbmi->partition == PARTITION_NONE &&
      x->must_find_valid_partition)
    return 0;

  const SPEED_FEATURES *const sf = &cpi->sf;
  // Prune NEARMV and NEAR_NEARMV based on q index and neighbor's reference
  // frames
  if (sf->inter_sf.prune_nearmv_using_neighbors &&
      (mode == NEAR_NEARMV || mode == NEARMV)) {
    const MACROBLOCKD *const xd = &x->e_mbd;
    if (search_state->best_rd != INT64_MAX && xd->left_available &&
        xd->up_available) {
      const int thresholds[PRUNE_NEARMV_MAX][3] = { { 1, 0, 0 },
                                                    { 1, 1, 0 },
                                                    { 2, 1, 0 } };
      const int qindex_sub_range = x->qindex * 3 / QINDEX_RANGE;

      assert(sf->inter_sf.prune_nearmv_using_neighbors <= PRUNE_NEARMV_MAX &&
             qindex_sub_range < 3);
      const int num_ref_frame_pair_match_thresh =
          thresholds[sf->inter_sf.prune_nearmv_using_neighbors - 1]
                    [qindex_sub_range];

      assert(num_ref_frame_pair_match_thresh <= 2 &&
             num_ref_frame_pair_match_thresh >= 0);
      int num_ref_frame_pair_match = 0;

      num_ref_frame_pair_match = match_ref_frame_pair(xd->left_mbmi, ref_frame);
      num_ref_frame_pair_match +=
          match_ref_frame_pair(xd->above_mbmi, ref_frame);

      // Pruning based on ref frame pair match with neighbors.
      if (num_ref_frame_pair_match < num_ref_frame_pair_match_thresh) return 1;
    }
  }

  int skip_motion_mode = 0;
  if (mbmi->partition != PARTITION_NONE) {
    int skip_ref = skip_ref_frame_mask & (1 << ref_type);
    if (ref_type <= ALTREF_FRAME && skip_ref) {
      // Since the compound ref modes depends on the motion estimation result of
      // two single ref modes (best mv of single ref modes as the start point),
      // if current single ref mode is marked skip, we need to check if it will
      // be used in compound ref modes.
      if (is_ref_frame_used_by_compound_ref(ref_type, skip_ref_frame_mask)) {
        // Found a not skipped compound ref mode which contains current
        // single ref. So this single ref can't be skipped completely
        // Just skip its motion mode search, still try its simple
        // transition mode.
        skip_motion_mode = 1;
        skip_ref = 0;
      }
    }
    // If we are reusing the prediction from cache, and the current frame is
    // required by the cache, then we cannot prune it.
    if (is_ref_frame_used_in_cache(ref_type, x->mb_mode_cache)) {
      skip_ref = 0;
      // If the cache only needs the current reference type for compound
      // prediction, then we can skip motion mode search.
      skip_motion_mode = (ref_type <= ALTREF_FRAME &&
                          x->mb_mode_cache->ref_frame[1] > INTRA_FRAME);
    }
    if (skip_ref) return 1;
  }

  if (ref_frame[0] == INTRA_FRAME) {
    if (mode != DC_PRED) {
      // Disable intra modes other than DC_PRED for blocks with low variance
      // Threshold for intra skipping based on source variance
      // TODO(debargha): Specialize the threshold for super block sizes
      const unsigned int skip_intra_var_thresh = 64;
      if ((sf->rt_sf.mode_search_skip_flags & FLAG_SKIP_INTRA_LOWVAR) &&
          x->source_variance < skip_intra_var_thresh)
        return 1;
    }
  }

  if (skip_motion_mode) return 2;

  return 0;
}

static inline void init_mbmi(MB_MODE_INFO *mbmi, PREDICTION_MODE curr_mode,
                             const MV_REFERENCE_FRAME *ref_frames,
                             const AV1_COMMON *cm) {
  PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
  mbmi->ref_mv_idx = 0;
  mbmi->mode = curr_mode;
  mbmi->uv_mode = UV_DC_PRED;
  mbmi->ref_frame[0] = ref_frames[0];
  mbmi->ref_frame[1] = ref_frames[1];
  pmi->palette_size[0] = 0;
  pmi->palette_size[1] = 0;
  mbmi->filter_intra_mode_info.use_filter_intra = 0;
  mbmi->mv[0].as_int = mbmi->mv[1].as_int = 0;
  mbmi->motion_mode = SIMPLE_TRANSLATION;
  mbmi->interintra_mode = (INTERINTRA_MODE)(II_DC_PRED - 1);
  set_default_interp_filters(mbmi, cm->features.interp_filter);
}

static inline void collect_single_states(MACROBLOCK *x,
                                         InterModeSearchState *search_state,
                                         const MB_MODE_INFO *const mbmi) {
  int i, j;
  const MV_REFERENCE_FRAME ref_frame = mbmi->ref_frame[0];
  const PREDICTION_MODE this_mode = mbmi->mode;
  const int dir = ref_frame <= GOLDEN_FRAME ? 0 : 1;
  const int mode_offset = INTER_OFFSET(this_mode);
  const int ref_set = get_drl_refmv_count(x, mbmi->ref_frame, this_mode);

  // Simple rd
  int64_t simple_rd = search_state->simple_rd[this_mode][0][ref_frame];
  for (int ref_mv_idx = 1; ref_mv_idx < ref_set; ++ref_mv_idx) {
    const int64_t rd =
        search_state->simple_rd[this_mode][ref_mv_idx][ref_frame];
    if (rd < simple_rd) simple_rd = rd;
  }

  // Insertion sort of single_state
  const SingleInterModeState this_state_s = { simple_rd, ref_frame, 1 };
  SingleInterModeState *state_s = search_state->single_state[dir][mode_offset];
  i = search_state->single_state_cnt[dir][mode_offset];
  for (j = i; j > 0 && state_s[j - 1].rd > this_state_s.rd; --j)
    state_s[j] = state_s[j - 1];
  state_s[j] = this_state_s;
  search_state->single_state_cnt[dir][mode_offset]++;

  // Modelled rd
  int64_t modelled_rd = search_state->modelled_rd[this_mode][0][ref_frame];
  for (int ref_mv_idx = 1; ref_mv_idx < ref_set; ++ref_mv_idx) {
    const int64_t rd =
        search_state->modelled_rd[this_mode][ref_mv_idx][ref_frame];
    if (rd < modelled_rd) modelled_rd = rd;
  }

  // Insertion sort of single_state_modelled
  const SingleInterModeState this_state_m = { modelled_rd, ref_frame, 1 };
  SingleInterModeState *state_m =
      search_state->single_state_modelled[dir][mode_offset];
  i = search_state->single_state_modelled_cnt[dir][mode_offset];
  for (j = i; j > 0 && state_m[j - 1].rd > this_state_m.rd; --j)
    state_m[j] = state_m[j - 1];
  state_m[j] = this_state_m;
  search_state->single_state_modelled_cnt[dir][mode_offset]++;
}

static inline void analyze_single_states(const AV1_COMP *cpi,
                                         InterModeSearchState *search_state) {
  const int prune_level = cpi->sf.inter_sf.prune_comp_search_by_single_result;
  assert(prune_level >= 1);
  int i, j, dir, mode;

  for (dir = 0; dir < 2; ++dir) {
    int64_t best_rd;
    SingleInterModeState(*state)[FWD_REFS];
    const int prune_factor = prune_level >= 2 ? 6 : 5;

    // Use the best rd of GLOBALMV or NEWMV to prune the unlikely
    // reference frames for all the modes (NEARESTMV and NEARMV may not
    // have same motion vectors). Always keep the best of each mode
    // because it might form the best possible combination with other mode.
    state = search_state->single_state[dir];
    best_rd = AOMMIN(state[INTER_OFFSET(NEWMV)][0].rd,
                     state[INTER_OFFSET(GLOBALMV)][0].rd);
    for (mode = 0; mode < SINGLE_INTER_MODE_NUM; ++mode) {
      for (i = 1; i < search_state->single_state_cnt[dir][mode]; ++i) {
        if (state[mode][i].rd != INT64_MAX &&
            (state[mode][i].rd >> 3) * prune_factor > best_rd) {
          state[mode][i].valid = 0;
        }
      }
    }

    state = search_state->single_state_modelled[dir];
    best_rd = AOMMIN(state[INTER_OFFSET(NEWMV)][0].rd,
                     state[INTER_OFFSET(GLOBALMV)][0].rd);
    for (mode = 0; mode < SINGLE_INTER_MODE_NUM; ++mode) {
      for (i = 1; i < search_state->single_state_modelled_cnt[dir][mode]; ++i) {
        if (state[mode][i].rd != INT64_MAX &&
            (state[mode][i].rd >> 3) * prune_factor > best_rd) {
          state[mode][i].valid = 0;
        }
      }
    }
  }

  // Ordering by simple rd first, then by modelled rd
  for (dir = 0; dir < 2; ++dir) {
    for (mode = 0; mode < SINGLE_INTER_MODE_NUM; ++mode) {
      const int state_cnt_s = search_state->single_state_cnt[dir][mode];
      const int state_cnt_m =
          search_state->single_state_modelled_cnt[dir][mode];
      SingleInterModeState *state_s = search_state->single_state[dir][mode];
      SingleInterModeState *state_m =
          search_state->single_state_modelled[dir][mode];
      int count = 0;
      const int max_candidates = AOMMAX(state_cnt_s, state_cnt_m);
      for (i = 0; i < state_cnt_s; ++i) {
        if (state_s[i].rd == INT64_MAX) break;
        if (state_s[i].valid) {
          search_state->single_rd_order[dir][mode][count++] =
              state_s[i].ref_frame;
        }
      }
      if (count >= max_candidates) continue;

      for (i = 0; i < state_cnt_m && count < max_candidates; ++i) {
        if (state_m[i].rd == INT64_MAX) break;
        if (!state_m[i].valid) continue;
        const int ref_frame = state_m[i].ref_frame;
        int match = 0;
        // Check if existing already
        for (j = 0; j < count; ++j) {
          if (search_state->single_rd_order[dir][mode][j] == ref_frame) {
            match = 1;
            break;
          }
        }
        if (match) continue;
        // Check if this ref_frame is removed in simple rd
        int valid = 1;
        for (j = 0; j < state_cnt_s; ++j) {
          if (ref_frame == state_s[j].ref_frame) {
            valid = state_s[j].valid;
            break;
          }
        }
        if (valid) {
          search_state->single_rd_order[dir][mode][count++] = ref_frame;
        }
      }
    }
  }
}

static int compound_skip_get_candidates(
    const AV1_COMP *cpi, const InterModeSearchState *search_state,
    const int dir, const PREDICTION_MODE mode) {
  const int mode_offset = INTER_OFFSET(mode);
  const SingleInterModeState *state =
      search_state->single_state[dir][mode_offset];
  const SingleInterModeState *state_modelled =
      search_state->single_state_modelled[dir][mode_offset];

  int max_candidates = 0;
  for (int i = 0; i < FWD_REFS; ++i) {
    if (search_state->single_rd_order[dir][mode_offset][i] == NONE_FRAME) break;
    max_candidates++;
  }

  int candidates = max_candidates;
  if (cpi->sf.inter_sf.prune_comp_search_by_single_result >= 2) {
    candidates = AOMMIN(2, max_candidates);
  }
  if (cpi->sf.inter_sf.prune_comp_search_by_single_result >= 3) {
    if (state[0].rd != INT64_MAX && state_modelled[0].rd != INT64_MAX &&
        state[0].ref_frame == state_modelled[0].ref_frame)
      candidates = 1;
    if (mode == NEARMV || mode == GLOBALMV) candidates = 1;
  }

  if (cpi->sf.inter_sf.prune_comp_search_by_single_result >= 4) {
    // Limit the number of candidates to 1 in each direction for compound
    // prediction
    candidates = AOMMIN(1, candidates);
  }
  return candidates;
}

static int compound_skip_by_single_states(
    const AV1_COMP *cpi, const InterModeSearchState *search_state,
    const PREDICTION_MODE this_mode, const MV_REFERENCE_FRAME ref_frame,
    const MV_REFERENCE_FRAME second_ref_frame, const MACROBLOCK *x) {
  const MV_REFERENCE_FRAME refs[2] = { ref_frame, second_ref_frame };
  const int mode[2] = { compound_ref0_mode(this_mode),
                        compound_ref1_mode(this_mode) };
  const int mode_offset[2] = { INTER_OFFSET(mode[0]), INTER_OFFSET(mode[1]) };
  const int mode_dir[2] = { refs[0] <= GOLDEN_FRAME ? 0 : 1,
                            refs[1] <= GOLDEN_FRAME ? 0 : 1 };
  int ref_searched[2] = { 0, 0 };
  int ref_mv_match[2] = { 1, 1 };
  int i, j;

  for (i = 0; i < 2; ++i) {
    const SingleInterModeState *state =
        search_state->single_state[mode_dir[i]][mode_offset[i]];
    const int state_cnt =
        search_state->single_state_cnt[mode_dir[i]][mode_offset[i]];
    for (j = 0; j < state_cnt; ++j) {
      if (state[j].ref_frame == refs[i]) {
        ref_searched[i] = 1;
        break;
      }
    }
  }

  const int ref_set = get_drl_refmv_count(x, refs, this_mode);
  for (i = 0; i < 2; ++i) {
    if (!ref_searched[i] || (mode[i] != NEARESTMV && mode[i] != NEARMV)) {
      continue;
    }
    const MV_REFERENCE_FRAME single_refs[2] = { refs[i], NONE_FRAME };
    for (int ref_mv_idx = 0; ref_mv_idx < ref_set; ref_mv_idx++) {
      int_mv single_mv;
      int_mv comp_mv;
      get_this_mv(&single_mv, mode[i], 0, ref_mv_idx, 0, single_refs,
                  &x->mbmi_ext);
      get_this_mv(&comp_mv, this_mode, i, ref_mv_idx, 0, refs, &x->mbmi_ext);
      if (single_mv.as_int != comp_mv.as_int) {
        ref_mv_match[i] = 0;
        break;
      }
    }
  }

  for (i = 0; i < 2; ++i) {
    if (!ref_searched[i] || !ref_mv_match[i]) continue;
    const int candidates =
        compound_skip_get_candidates(cpi, search_state, mode_dir[i], mode[i]);
    const MV_REFERENCE_FRAME *ref_order =
        search_state->single_rd_order[mode_dir[i]][mode_offset[i]];
    int match = 0;
    for (j = 0; j < candidates; ++j) {
      if (refs[i] == ref_order[j]) {
        match = 1;
        break;
      }
    }
    if (!match) return 1;
  }

  return 0;
}

// Check if ref frames of current block matches with given block.
static inline void match_ref_frame(const MB_MODE_INFO *const mbmi,
                                   const MV_REFERENCE_FRAME *ref_frames,
                                   int *const is_ref_match) {
  if (is_inter_block(mbmi)) {
    is_ref_match[0] |= ref_frames[0] == mbmi->ref_frame[0];
    is_ref_match[1] |= ref_frames[1] == mbmi->ref_frame[0];
    if (has_second_ref(mbmi)) {
      is_ref_match[0] |= ref_frames[0] == mbmi->ref_frame[1];
      is_ref_match[1] |= ref_frames[1] == mbmi->ref_frame[1];
    }
  }
}

// Prune compound mode using ref frames of neighbor blocks.
static inline int compound_skip_using_neighbor_refs(
    MACROBLOCKD *const xd, const PREDICTION_MODE this_mode,
    const MV_REFERENCE_FRAME *ref_frames, int prune_ext_comp_using_neighbors) {
  // Exclude non-extended compound modes from pruning
  if (this_mode == NEAREST_NEARESTMV || this_mode == NEAR_NEARMV ||
      this_mode == NEW_NEWMV || this_mode == GLOBAL_GLOBALMV)
    return 0;

  if (prune_ext_comp_using_neighbors >= 3) return 1;

  int is_ref_match[2] = { 0 };  // 0 - match for forward refs
                                // 1 - match for backward refs
  // Check if ref frames of this block matches with left neighbor.
  if (xd->left_available)
    match_ref_frame(xd->left_mbmi, ref_frames, is_ref_match);

  // Check if ref frames of this block matches with above neighbor.
  if (xd->up_available)
    match_ref_frame(xd->above_mbmi, ref_frames, is_ref_match);

  // Combine ref frame match with neighbors in forward and backward refs.
  const int track_ref_match = is_ref_match[0] + is_ref_match[1];

  // Pruning based on ref frame match with neighbors.
  if (track_ref_match >= prune_ext_comp_using_neighbors) return 0;
  return 1;
}

// Update best single mode for the given reference frame based on simple rd.
static inline void update_best_single_mode(InterModeSearchState *search_state,
                                           const PREDICTION_MODE this_mode,
                                           const MV_REFERENCE_FRAME ref_frame,
                                           int64_t this_rd) {
  if (this_rd < search_state->best_single_rd[ref_frame]) {
    search_state->best_single_rd[ref_frame] = this_rd;
    search_state->best_single_mode[ref_frame] = this_mode;
  }
}

// Prune compound mode using best single mode for the same reference.
static inline int skip_compound_using_best_single_mode_ref(
    const PREDICTION_MODE this_mode, const MV_REFERENCE_FRAME *ref_frames,
    const PREDICTION_MODE *best_single_mode,
    int prune_comp_using_best_single_mode_ref) {
  // Exclude non-extended compound modes from pruning
  if (this_mode == NEAREST_NEARESTMV || this_mode == NEAR_NEARMV ||
      this_mode == NEW_NEWMV || this_mode == GLOBAL_GLOBALMV)
    return 0;

  assert(this_mode >= NEAREST_NEWMV && this_mode <= NEW_NEARMV);
  const PREDICTION_MODE comp_mode_ref0 = compound_ref0_mode(this_mode);
  // Get ref frame direction corresponding to NEWMV
  // 0 - NEWMV corresponding to forward direction
  // 1 - NEWMV corresponding to backward direction
  const int newmv_dir = comp_mode_ref0 != NEWMV;

  // Avoid pruning the compound mode when ref frame corresponding to NEWMV
  // have NEWMV as single mode winner.
  // Example: For an extended-compound mode,
  // {mode, {fwd_frame, bwd_frame}} = {NEAR_NEWMV, {LAST_FRAME, ALTREF_FRAME}}
  // - Ref frame corresponding to NEWMV is ALTREF_FRAME
  // - Avoid pruning this mode, if best single mode corresponding to ref frame
  //   ALTREF_FRAME is NEWMV
  const PREDICTION_MODE single_mode = best_single_mode[ref_frames[newmv_dir]];
  if (single_mode == NEWMV) return 0;

  // Avoid pruning the compound mode when best single mode is not available
  if (prune_comp_using_best_single_mode_ref == 1)
    if (single_mode == MB_MODE_COUNT) return 0;
  return 1;
}

static int compare_int64(const void *a, const void *b) {
  int64_t a64 = *((int64_t *)a);
  int64_t b64 = *((int64_t *)b);
  if (a64 < b64) {
    return -1;
  } else if (a64 == b64) {
    return 0;
  } else {
    return 1;
  }
}

static inline void update_search_state(
    InterModeSearchState *search_state, RD_STATS *best_rd_stats_dst,
    PICK_MODE_CONTEXT *ctx, const RD_STATS *new_best_rd_stats,
    const RD_STATS *new_best_rd_stats_y, const RD_STATS *new_best_rd_stats_uv,
    THR_MODES new_best_mode, const MACROBLOCK *x, int txfm_search_done) {
  const MACROBLOCKD *xd = &x->e_mbd;
  const MB_MODE_INFO *mbmi = xd->mi[0];
  const int skip_ctx = av1_get_skip_txfm_context(xd);
  const int skip_txfm =
      mbmi->skip_txfm && !is_mode_intra(av1_mode_defs[new_best_mode].mode);
  const TxfmSearchInfo *txfm_info = &x->txfm_search_info;

  search_state->best_rd = new_best_rd_stats->rdcost;
  search_state->best_mode_index = new_best_mode;
  *best_rd_stats_dst = *new_best_rd_stats;
  search_state->best_mbmode = *mbmi;
  search_state->best_skip2 = skip_txfm;
  search_state->best_mode_skippable = new_best_rd_stats->skip_txfm;
  // When !txfm_search_done, new_best_rd_stats won't provide correct rate_y and
  // rate_uv because av1_txfm_search process is replaced by rd estimation.
  // Therefore, we should avoid updating best_rate_y and best_rate_uv here.
  // These two values will be updated when av1_txfm_search is called.
  if (txfm_search_done) {
    search_state->best_rate_y =
        new_best_rd_stats_y->rate +
        x->mode_costs.skip_txfm_cost[skip_ctx]
                                    [new_best_rd_stats->skip_txfm || skip_txfm];
    search_state->best_rate_uv = new_best_rd_stats_uv->rate;
  }
  search_state->best_y_rdcost = *new_best_rd_stats_y;
  memcpy(ctx->blk_skip, txfm_info->blk_skip,
         sizeof(txfm_info->blk_skip[0]) * ctx->num_4x4_blk);
  av1_copy_array(ctx->tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
}

// Find the best RD for a reference frame (among single reference modes)
// and store +10% of it in the 0-th element in ref_frame_rd.
static inline void find_top_ref(int64_t ref_frame_rd[REF_FRAMES]) {
  assert(ref_frame_rd[0] == INT64_MAX);
  int64_t ref_copy[REF_FRAMES - 1];
  memcpy(ref_copy, ref_frame_rd + 1,
         sizeof(ref_frame_rd[0]) * (REF_FRAMES - 1));
  qsort(ref_copy, REF_FRAMES - 1, sizeof(int64_t), compare_int64);

  int64_t cutoff = ref_copy[0];
  // The cut-off is within 10% of the best.
  if (cutoff != INT64_MAX) {
    assert(cutoff < INT64_MAX / 200);
    cutoff = (110 * cutoff) / 100;
  }
  ref_frame_rd[0] = cutoff;
}

// Check if either frame is within the cutoff.
static inline bool in_single_ref_cutoff(int64_t ref_frame_rd[REF_FRAMES],
                                        MV_REFERENCE_FRAME frame1,
                                        MV_REFERENCE_FRAME frame2) {
  assert(frame2 > 0);
  return ref_frame_rd[frame1] <= ref_frame_rd[0] ||
         ref_frame_rd[frame2] <= ref_frame_rd[0];
}

static inline void evaluate_motion_mode_for_winner_candidates(
    const AV1_COMP *const cpi, MACROBLOCK *const x, RD_STATS *const rd_cost,
    HandleInterModeArgs *const args, TileDataEnc *const tile_data,
    PICK_MODE_CONTEXT *const ctx,
    struct buf_2d yv12_mb[REF_FRAMES][MAX_MB_PLANE],
    const motion_mode_best_st_candidate *const best_motion_mode_cands,
    int do_tx_search, const BLOCK_SIZE bsize, int64_t *const best_est_rd,
    InterModeSearchState *const search_state, int64_t *yrd) {
  const AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  InterModesInfo *const inter_modes_info = x->inter_modes_info;
  const int num_best_cand = best_motion_mode_cands->num_motion_mode_cand;

  for (int cand = 0; cand < num_best_cand; cand++) {
    RD_STATS rd_stats;
    RD_STATS rd_stats_y;
    RD_STATS rd_stats_uv;
    av1_init_rd_stats(&rd_stats);
    av1_init_rd_stats(&rd_stats_y);
    av1_init_rd_stats(&rd_stats_uv);
    int rate_mv;

    rate_mv = best_motion_mode_cands->motion_mode_cand[cand].rate_mv;
    args->skip_motion_mode =
        best_motion_mode_cands->motion_mode_cand[cand].skip_motion_mode;
    *mbmi = best_motion_mode_cands->motion_mode_cand[cand].mbmi;
    rd_stats.rate =
        best_motion_mode_cands->motion_mode_cand[cand].rate2_nocoeff;

    // Continue if the best candidate is compound.
    if (!is_inter_singleref_mode(mbmi->mode)) continue;

    x->txfm_search_info.skip_txfm = 0;
    struct macroblockd_plane *pd = xd->plane;
    const BUFFER_SET orig_dst = {
      { pd[0].dst.buf, pd[1].dst.buf, pd[2].dst.buf },
      { pd[0].dst.stride, pd[1].dst.stride, pd[2].dst.stride },
    };

    set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);
    // Initialize motion mode to simple translation
    // Calculation of switchable rate depends on it.
    mbmi->motion_mode = 0;
    const int is_comp_pred = mbmi->ref_frame[1] > INTRA_FRAME;
    for (int i = 0; i < num_planes; i++) {
      xd->plane[i].pre[0] = yv12_mb[mbmi->ref_frame[0]][i];
      if (is_comp_pred) xd->plane[i].pre[1] = yv12_mb[mbmi->ref_frame[1]][i];
    }

    int64_t skip_rd[2] = { search_state->best_skip_rd[0],
                           search_state->best_skip_rd[1] };
    int64_t this_yrd = INT64_MAX;
    int64_t ret_value = motion_mode_rd(
        cpi, tile_data, x, bsize, &rd_stats, &rd_stats_y, &rd_stats_uv, args,
        search_state->best_rd, skip_rd, &rate_mv, &orig_dst, best_est_rd,
        do_tx_search, inter_modes_info, 1, &this_yrd);

    if (ret_value != INT64_MAX) {
      rd_stats.rdcost = RDCOST(x->rdmult, rd_stats.rate, rd_stats.dist);
      const THR_MODES mode_enum = get_prediction_mode_idx(
          mbmi->mode, mbmi->ref_frame[0], mbmi->ref_frame[1]);
      // Collect mode stats for multiwinner mode processing
      store_winner_mode_stats(
          &cpi->common, x, mbmi, &rd_stats, &rd_stats_y, &rd_stats_uv,
          mode_enum, NULL, bsize, rd_stats.rdcost,
          cpi->sf.winner_mode_sf.multi_winner_mode_type, do_tx_search);
      if (rd_stats.rdcost < search_state->best_rd) {
        *yrd = this_yrd;
        update_search_state(search_state, rd_cost, ctx, &rd_stats, &rd_stats_y,
                            &rd_stats_uv, mode_enum, x, do_tx_search);
        if (do_tx_search) search_state->best_skip_rd[0] = skip_rd[0];
      }
    }
  }
}

/*!\cond */
// Arguments for speed feature pruning of inter mode search
typedef struct {
  int *skip_motion_mode;
  mode_skip_mask_t *mode_skip_mask;
  InterModeSearchState *search_state;
  int skip_ref_frame_mask;
  int reach_first_comp_mode;
  int mode_thresh_mul_fact;
  int num_single_modes_processed;
  int prune_cpd_using_sr_stats_ready;
} InterModeSFArgs;
/*!\endcond */

static int skip_inter_mode(AV1_COMP *cpi, MACROBLOCK *x, const BLOCK_SIZE bsize,
                           int64_t *ref_frame_rd, int midx,
                           InterModeSFArgs *args, int is_low_temp_var) {
  const SPEED_FEATURES *const sf = &cpi->sf;
  MACROBLOCKD *const xd = &x->e_mbd;
  // Get the actual prediction mode we are trying in this iteration
  const THR_MODES mode_enum = av1_default_mode_order[midx];
  const MODE_DEFINITION *mode_def = &av1_mode_defs[mode_enum];
  const PREDICTION_MODE this_mode = mode_def->mode;
  const MV_REFERENCE_FRAME *ref_frames = mode_def->ref_frame;
  const MV_REFERENCE_FRAME ref_frame = ref_frames[0];
  const MV_REFERENCE_FRAME second_ref_frame = ref_frames[1];
  const int comp_pred = second_ref_frame > INTRA_FRAME;

  if (ref_frame == INTRA_FRAME) return 1;

  const FRAME_UPDATE_TYPE update_type =
      get_frame_update_type(&cpi->ppi->gf_group, cpi->gf_frame_index);
  if (sf->inter_sf.skip_arf_compound && update_type == ARF_UPDATE &&
      comp_pred) {
    return 1;
  }

  // This is for real time encoding.
  if (is_low_temp_var && !comp_pred && ref_frame != LAST_FRAME &&
      this_mode != NEARESTMV)
    return 1;

  // Check if this mode should be skipped because it is incompatible with the
  // current frame
  if (inter_mode_compatible_skip(cpi, x, bsize, this_mode, ref_frames))
    return 1;
  const int ret = inter_mode_search_order_independent_skip(
      cpi, x, args->mode_skip_mask, args->search_state,
      args->skip_ref_frame_mask, this_mode, mode_def->ref_frame);
  if (ret == 1) return 1;
  *(args->skip_motion_mode) = (ret == 2);

  // We've reached the first compound prediction mode, get stats from the
  // single reference predictors to help with pruning.
  // Disable this pruning logic if interpolation filter search was skipped for
  // single prediction modes as it can result in aggressive pruning of compound
  // prediction modes due to the absence of modelled_rd populated by
  // av1_interpolation_filter_search().
  // TODO(Remya): Check the impact of the sf
  // 'prune_comp_search_by_single_result' if compound prediction modes are
  // enabled in future for REALTIME encode.
  if (!sf->interp_sf.skip_interp_filter_search &&
      sf->inter_sf.prune_comp_search_by_single_result > 0 && comp_pred &&
      args->reach_first_comp_mode == 0) {
    analyze_single_states(cpi, args->search_state);
    args->reach_first_comp_mode = 1;
  }

  // Prune aggressively when best mode is skippable.
  int mul_fact = args->search_state->best_mode_skippable
                     ? args->mode_thresh_mul_fact
                     : (1 << MODE_THRESH_QBITS);
  int64_t mode_threshold =
      (args->search_state->mode_threshold[mode_enum] * mul_fact) >>
      MODE_THRESH_QBITS;

  if (args->search_state->best_rd < mode_threshold) return 1;

  // Skip this compound mode based on the RD results from the single prediction
  // modes
  if (!sf->interp_sf.skip_interp_filter_search &&
      sf->inter_sf.prune_comp_search_by_single_result > 0 && comp_pred) {
    if (compound_skip_by_single_states(cpi, args->search_state, this_mode,
                                       ref_frame, second_ref_frame, x))
      return 1;
  }

  if (sf->inter_sf.prune_compound_using_single_ref && comp_pred) {
    // After we done with single reference modes, find the 2nd best RD
    // for a reference frame. Only search compound modes that have a reference
    // frame at least as good as the 2nd best.
    if (!args->prune_cpd_using_sr_stats_ready &&
        args->num_single_modes_processed == NUM_SINGLE_REF_MODES) {
      find_top_ref(ref_frame_rd);
      args->prune_cpd_using_sr_stats_ready = 1;
    }
    if (args->prune_cpd_using_sr_stats_ready &&
        !in_single_ref_cutoff(ref_frame_rd, ref_frame, second_ref_frame))
      return 1;
  }

  // Skip NEW_NEARMV and NEAR_NEWMV extended compound modes
  if (sf->inter_sf.skip_ext_comp_nearmv_mode &&
      (this_mode == NEW_NEARMV || this_mode == NEAR_NEWMV)) {
    return 1;
  }

  if (sf->inter_sf.prune_ext_comp_using_neighbors && comp_pred) {
    if (compound_skip_using_neighbor_refs(
            xd, this_mode, ref_frames,
            sf->inter_sf.prune_ext_comp_using_neighbors))
      return 1;
  }

  if (sf->inter_sf.prune_comp_using_best_single_mode_ref && comp_pred) {
    if (skip_compound_using_best_single_mode_ref(
            this_mode, ref_frames, args->search_state->best_single_mode,
            sf->inter_sf.prune_comp_using_best_single_mode_ref))
      return 1;
  }

  if (sf->inter_sf.prune_nearest_near_mv_using_refmv_weight && !comp_pred) {
    const int8_t ref_frame_type = av1_ref_frame_type(ref_frames);
    if (skip_nearest_near_mv_using_refmv_weight(
            x, this_mode, ref_frame_type,
            args->search_state->best_mbmode.mode)) {
      // Ensure the mode is pruned only when the current block has obtained a
      // valid inter mode.
      assert(is_inter_mode(args->search_state->best_mbmode.mode));
      return 1;
    }
  }

  if (sf->rt_sf.prune_inter_modes_with_golden_ref &&
      ref_frame == GOLDEN_FRAME && !comp_pred) {
    const int subgop_size = AOMMIN(cpi->ppi->gf_group.size, FIXED_GF_INTERVAL);
    if (cpi->rc.frames_since_golden > (subgop_size >> 2) &&
        args->search_state->best_mbmode.ref_frame[0] != GOLDEN_FRAME) {
      if ((bsize > BLOCK_16X16 && this_mode == NEWMV) || this_mode == NEARMV)
        return 1;
    }
  }

  return 0;
}

static void record_best_compound(REFERENCE_MODE reference_mode,
                                 RD_STATS *rd_stats, int comp_pred, int rdmult,
                                 InterModeSearchState *search_state,
                                 int compmode_cost) {
  int64_t single_rd, hybrid_rd, single_rate, hybrid_rate;

  if (reference_mode == REFERENCE_MODE_SELECT) {
    single_rate = rd_stats->rate - compmode_cost;
    hybrid_rate = rd_stats->rate;
  } else {
    single_rate = rd_stats->rate;
    hybrid_rate = rd_stats->rate + compmode_cost;
  }

  single_rd = RDCOST(rdmult, single_rate, rd_stats->dist);
  hybrid_rd = RDCOST(rdmult, hybrid_rate, rd_stats->dist);

  if (!comp_pred) {
    if (single_rd < search_state->best_pred_rd[SINGLE_REFERENCE])
      search_state->best_pred_rd[SINGLE_REFERENCE] = single_rd;
  } else {
    if (single_rd < search_state->best_pred_rd[COMPOUND_REFERENCE])
      search_state->best_pred_rd[COMPOUND_REFERENCE] = single_rd;
  }
  if (hybrid_rd < search_state->best_pred_rd[REFERENCE_MODE_SELECT])
    search_state->best_pred_rd[REFERENCE_MODE_SELECT] = hybrid_rd;
}

// Does a transform search over a list of the best inter mode candidates.
// This is called if the original mode search computed an RD estimate
// for the transform search rather than doing a full search.
static void tx_search_best_inter_candidates(
    AV1_COMP *cpi, TileDataEnc *tile_data, MACROBLOCK *x,
    int64_t best_rd_so_far, BLOCK_SIZE bsize,
    struct buf_2d yv12_mb[REF_FRAMES][MAX_MB_PLANE], int mi_row, int mi_col,
    InterModeSearchState *search_state, RD_STATS *rd_cost,
    PICK_MODE_CONTEXT *ctx, int64_t *yrd) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  const ModeCosts *mode_costs = &x->mode_costs;
  const int num_planes = av1_num_planes(cm);
  const int skip_ctx = av1_get_skip_txfm_context(xd);
  MB_MODE_INFO *const mbmi = xd->mi[0];
  InterModesInfo *inter_modes_info = x->inter_modes_info;
  inter_modes_info_sort(inter_modes_info, inter_modes_info->rd_idx_pair_arr);
  search_state->best_rd = best_rd_so_far;
  search_state->best_mode_index = THR_INVALID;
  // Initialize best mode stats for winner mode processing
  x->winner_mode_count = 0;
  store_winner_mode_stats(&cpi->common, x, mbmi, NULL, NULL, NULL, THR_INVALID,
                          NULL, bsize, best_rd_so_far,
                          cpi->sf.winner_mode_sf.multi_winner_mode_type, 0);
  inter_modes_info->num =
      inter_modes_info->num < cpi->sf.rt_sf.num_inter_modes_for_tx_search
          ? inter_modes_info->num
          : cpi->sf.rt_sf.num_inter_modes_for_tx_search;
  const int64_t top_est_rd =
      inter_modes_info->num > 0
          ? inter_modes_info
                ->est_rd_arr[inter_modes_info->rd_idx_pair_arr[0].idx]
          : INT64_MAX;
  *yrd = INT64_MAX;
  int64_t best_rd_in_this_partition = INT64_MAX;
  int num_inter_mode_cands = inter_modes_info->num;
  int newmv_mode_evaled = 0;
  int max_allowed_cands = INT_MAX;
  if (cpi->sf.inter_sf.limit_inter_mode_cands) {
    // The bound on the no. of inter mode candidates, beyond which the
    // candidates are limited if a newmv mode got evaluated, is set as
    // max_allowed_cands + 1.
    const int num_allowed_cands[5] = { INT_MAX, 10, 9, 6, 2 };
    assert(cpi->sf.inter_sf.limit_inter_mode_cands <= 4);
    max_allowed_cands =
        num_allowed_cands[cpi->sf.inter_sf.limit_inter_mode_cands];
  }

  int num_mode_thresh = INT_MAX;
  if (cpi->sf.inter_sf.limit_txfm_eval_per_mode) {
    // Bound the no. of transform searches per prediction mode beyond a
    // threshold.
    const int num_mode_thresh_ary[4] = { INT_MAX, 4, 3, 0 };
    assert(cpi->sf.inter_sf.limit_txfm_eval_per_mode <= 3);
    num_mode_thresh =
        num_mode_thresh_ary[cpi->sf.inter_sf.limit_txfm_eval_per_mode];
  }

  int num_tx_cands = 0;
  int num_tx_search_modes[INTER_MODE_END - INTER_MODE_START] = { 0 };
  // Iterate over best inter mode candidates and perform tx search
  for (int j = 0; j < num_inter_mode_cands; ++j) {
    const int data_idx = inter_modes_info->rd_idx_pair_arr[j].idx;
    *mbmi = inter_modes_info->mbmi_arr[data_idx];
    const PREDICTION_MODE prediction_mode = mbmi->mode;
    int64_t curr_est_rd = inter_modes_info->est_rd_arr[data_idx];
    if (curr_est_rd * 0.80 > top_est_rd) break;

    if (num_tx_cands > num_mode_thresh) {
      if ((prediction_mode != NEARESTMV &&
           num_tx_search_modes[prediction_mode - INTER_MODE_START] >= 1) ||
          (prediction_mode == NEARESTMV &&
           num_tx_search_modes[prediction_mode - INTER_MODE_START] >= 2))
        continue;
    }

    txfm_info->skip_txfm = 0;
    set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);

    // Select prediction reference frames.
    const int is_comp_pred = mbmi->ref_frame[1] > INTRA_FRAME;
    for (int i = 0; i < num_planes; i++) {
      xd->plane[i].pre[0] = yv12_mb[mbmi->ref_frame[0]][i];
      if (is_comp_pred) xd->plane[i].pre[1] = yv12_mb[mbmi->ref_frame[1]][i];
    }

    bool is_predictor_built = false;

    // Initialize RD stats
    RD_STATS rd_stats;
    RD_STATS rd_stats_y;
    RD_STATS rd_stats_uv;
    const int mode_rate = inter_modes_info->mode_rate_arr[data_idx];
    int64_t skip_rd = INT64_MAX;
    const int txfm_rd_gate_level = get_txfm_rd_gate_level(
        cm->seq_params->enable_masked_compound,
        cpi->sf.inter_sf.txfm_rd_gate_level, bsize, TX_SEARCH_DEFAULT,
        /*eval_motion_mode=*/0);
    if (txfm_rd_gate_level) {
      // Check if the mode is good enough based on skip RD
      int64_t curr_sse = inter_modes_info->sse_arr[data_idx];
      skip_rd = RDCOST(x->rdmult, mode_rate, curr_sse);
      int eval_txfm = check_txfm_eval(x, bsize, search_state->best_skip_rd[0],
                                      skip_rd, txfm_rd_gate_level, 0);
      if (!eval_txfm) continue;
    }

    // Build the prediction for this mode
    if (!is_predictor_built) {
      av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize, 0,
                                    av1_num_planes(cm) - 1);
    }
    if (mbmi->motion_mode == OBMC_CAUSAL) {
      av1_build_obmc_inter_predictors_sb(cm, xd);
    }

    num_tx_cands++;
    if (have_newmv_in_inter_mode(prediction_mode)) newmv_mode_evaled = 1;
    num_tx_search_modes[prediction_mode - INTER_MODE_START]++;
    int64_t this_yrd = INT64_MAX;
    // Do the transform search
    if (!av1_txfm_search(cpi, x, bsize, &rd_stats, &rd_stats_y, &rd_stats_uv,
                         mode_rate, search_state->best_rd)) {
      continue;
    } else {
      const int y_rate =
          rd_stats.skip_txfm
              ? mode_costs->skip_txfm_cost[skip_ctx][1]
              : (rd_stats_y.rate + mode_costs->skip_txfm_cost[skip_ctx][0]);
      this_yrd = RDCOST(x->rdmult, y_rate + mode_rate, rd_stats_y.dist);

      if (cpi->sf.inter_sf.inter_mode_rd_model_estimation == 1) {
        inter_mode_data_push(
            tile_data, mbmi->bsize, rd_stats.sse, rd_stats.dist,
            rd_stats_y.rate + rd_stats_uv.rate +
                mode_costs->skip_txfm_cost[skip_ctx][mbmi->skip_txfm]);
      }
    }
    rd_stats.rdcost = RDCOST(x->rdmult, rd_stats.rate, rd_stats.dist);
    if (rd_stats.rdcost < best_rd_in_this_partition) {
      best_rd_in_this_partition = rd_stats.rdcost;
      *yrd = this_yrd;
    }

    const THR_MODES mode_enum = get_prediction_mode_idx(
        prediction_mode, mbmi->ref_frame[0], mbmi->ref_frame[1]);

    // Collect mode stats for multiwinner mode processing
    const int txfm_search_done = 1;
    store_winner_mode_stats(
        &cpi->common, x, mbmi, &rd_stats, &rd_stats_y, &rd_stats_uv, mode_enum,
        NULL, bsize, rd_stats.rdcost,
        cpi->sf.winner_mode_sf.multi_winner_mode_type, txfm_search_done);

    if (rd_stats.rdcost < search_state->best_rd) {
      update_search_state(search_state, rd_cost, ctx, &rd_stats, &rd_stats_y,
                          &rd_stats_uv, mode_enum, x, txfm_search_done);
      search_state->best_skip_rd[0] = skip_rd;
      // Limit the total number of modes to be evaluated if the first is valid
      // and transform skip or compound
      if (cpi->sf.inter_sf.inter_mode_txfm_breakout) {
        if (!j && (search_state->best_mbmode.skip_txfm || rd_stats.skip_txfm)) {
          // Evaluate more candidates at high quantizers where occurrence of
          // transform skip is high.
          const int max_cands_cap[5] = { 2, 3, 5, 7, 9 };
          const int qindex_band = (5 * x->qindex) >> QINDEX_BITS;
          num_inter_mode_cands =
              AOMMIN(max_cands_cap[qindex_band], inter_modes_info->num);
        } else if (!j && has_second_ref(&search_state->best_mbmode)) {
          const int aggr = cpi->sf.inter_sf.inter_mode_txfm_breakout - 1;
          // Evaluate more candidates at low quantizers where occurrence of
          // single reference mode is high.
          const int max_cands_cap_cmp[2][4] = { { 10, 7, 5, 4 },
                                                { 10, 7, 5, 3 } };
          const int qindex_band_cmp = (4 * x->qindex) >> QINDEX_BITS;
          num_inter_mode_cands = AOMMIN(
              max_cands_cap_cmp[aggr][qindex_band_cmp], inter_modes_info->num);
        }
      }
    }
    // If the number of candidates evaluated exceeds max_allowed_cands, break if
    // a newmv mode was evaluated already.
    if ((num_tx_cands > max_allowed_cands) && newmv_mode_evaled) break;
  }
}

// Indicates number of winner simple translation modes to be used
static const unsigned int num_winner_motion_modes[3] = { 0, 10, 3 };

// Adds a motion mode to the candidate list for motion_mode_for_winner_cand
// speed feature. This list consists of modes that have only searched
// SIMPLE_TRANSLATION. The final list will be used to search other motion
// modes after the initial RD search.
static void handle_winner_cand(
    MB_MODE_INFO *const mbmi,
    motion_mode_best_st_candidate *best_motion_mode_cands,
    int max_winner_motion_mode_cand, int64_t this_rd,
    motion_mode_candidate *motion_mode_cand, int skip_motion_mode) {
  // Number of current motion mode candidates in list
  const int num_motion_mode_cand = best_motion_mode_cands->num_motion_mode_cand;
  int valid_motion_mode_cand_loc = num_motion_mode_cand;

  // find the best location to insert new motion mode candidate
  for (int j = 0; j < num_motion_mode_cand; j++) {
    if (this_rd < best_motion_mode_cands->motion_mode_cand[j].rd_cost) {
      valid_motion_mode_cand_loc = j;
      break;
    }
  }

  // Insert motion mode if location is found
  if (valid_motion_mode_cand_loc < max_winner_motion_mode_cand) {
    if (num_motion_mode_cand > 0 &&
        valid_motion_mode_cand_loc < max_winner_motion_mode_cand - 1)
      memmove(
          &best_motion_mode_cands
               ->motion_mode_cand[valid_motion_mode_cand_loc + 1],
          &best_motion_mode_cands->motion_mode_cand[valid_motion_mode_cand_loc],
          (AOMMIN(num_motion_mode_cand, max_winner_motion_mode_cand - 1) -
           valid_motion_mode_cand_loc) *
              sizeof(best_motion_mode_cands->motion_mode_cand[0]));
    motion_mode_cand->mbmi = *mbmi;
    motion_mode_cand->rd_cost = this_rd;
    motion_mode_cand->skip_motion_mode = skip_motion_mode;
    best_motion_mode_cands->motion_mode_cand[valid_motion_mode_cand_loc] =
        *motion_mode_cand;
    best_motion_mode_cands->num_motion_mode_cand =
        AOMMIN(max_winner_motion_mode_cand,
               best_motion_mode_cands->num_motion_mode_cand + 1);
  }
}

/*!\brief Search intra modes in interframes
 *
 * \ingroup intra_mode_search
 *
 * This function searches for the best intra mode when the current frame is an
 * interframe. This function however does *not* handle luma palette mode.
 * Palette mode is currently handled by \ref av1_search_palette_mode.
 *
 * This function will first iterate through the luma mode candidates to find the
 * best luma intra mode. Once the best luma mode it's found, it will then search
 * for the best chroma mode. Because palette mode is currently not handled by
 * here, a cache of uv mode is stored in
 * InterModeSearchState::intra_search_state so it can be reused later by \ref
 * av1_search_palette_mode.
 *
 * \param[in,out] search_state      Struct keep track of the prediction mode
 *                                  search state in interframe.
 *
 * \param[in]     cpi               Top-level encoder structure.
 * \param[in,out] x                 Pointer to struct holding all the data for
 *                                  the current prediction block.
 * \param[out]    rd_cost           Stores the best rd_cost among all the
 *                                  prediction modes searched.
 * \param[in]     bsize             Current block size.
 * \param[in,out] ctx               Structure to hold the number of 4x4 blks to
 *                                  copy the tx_type and txfm_skip arrays.
 *                                  for only the Y plane.
 * \param[in]     sf_args           Stores the list of intra mode candidates
 *                                  to be searched.
 * \param[in]     intra_ref_frame_cost  The entropy cost for signaling that the
 *                                      current ref frame is an intra frame.
 * \param[in]     yrd_threshold     The rdcost threshold for luma intra mode to
 *                                  terminate chroma intra mode search.
 *
 * \remark If a new best mode is found, search_state and rd_costs are updated
 * correspondingly. While x is also modified, it is only used as a temporary
 * buffer, and the final decisions are stored in search_state.
 */
static inline void search_intra_modes_in_interframe(
    InterModeSearchState *search_state, const AV1_COMP *cpi, MACROBLOCK *x,
    RD_STATS *rd_cost, BLOCK_SIZE bsize, PICK_MODE_CONTEXT *ctx,
    const InterModeSFArgs *sf_args, unsigned int intra_ref_frame_cost,
    int64_t yrd_threshold) {
  const AV1_COMMON *const cm = &cpi->common;
  const SPEED_FEATURES *const sf = &cpi->sf;
  const IntraModeCfg *const intra_mode_cfg = &cpi->oxcf.intra_mode_cfg;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  IntraModeSearchState *intra_search_state = &search_state->intra_search_state;

  int is_best_y_mode_intra = 0;
  RD_STATS best_intra_rd_stats_y;
  int64_t best_rd_y = INT64_MAX;
  int best_mode_cost_y = -1;
  MB_MODE_INFO best_mbmi = *xd->mi[0];
  THR_MODES best_mode_enum = THR_INVALID;
  uint8_t best_blk_skip[MAX_MIB_SIZE * MAX_MIB_SIZE];
  uint8_t best_tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  const int num_4x4 = bsize_to_num_blk(bsize);

  // Performs luma search
  int64_t best_model_rd = INT64_MAX;
  int64_t top_intra_model_rd[TOP_INTRA_MODEL_COUNT];
  for (int i = 0; i < TOP_INTRA_MODEL_COUNT; i++) {
    top_intra_model_rd[i] = INT64_MAX;
  }

  if (cpi->oxcf.algo_cfg.sharpness) {
    int bh = mi_size_high[bsize];
    int bw = mi_size_wide[bsize];
    if (bh > 4 || bw > 4) return;
  }

  for (int mode_idx = 0; mode_idx < LUMA_MODE_COUNT; ++mode_idx) {
    if (sf->intra_sf.skip_intra_in_interframe &&
        search_state->intra_search_state.skip_intra_modes)
      break;
    set_y_mode_and_delta_angle(
        mode_idx, mbmi, sf->intra_sf.prune_luma_odd_delta_angles_in_intra);
    assert(mbmi->mode < INTRA_MODE_END);

    // Use intra_y_mode_mask speed feature to skip intra mode evaluation.
    if (sf_args->mode_skip_mask->pred_modes[INTRA_FRAME] & (1 << mbmi->mode))
      continue;

    const THR_MODES mode_enum =
        get_prediction_mode_idx(mbmi->mode, INTRA_FRAME, NONE_FRAME);
    if ((!intra_mode_cfg->enable_smooth_intra ||
         cpi->sf.intra_sf.disable_smooth_intra) &&
        (mbmi->mode == SMOOTH_PRED || mbmi->mode == SMOOTH_H_PRED ||
         mbmi->mode == SMOOTH_V_PRED))
      continue;
    if (!intra_mode_cfg->enable_paeth_intra && mbmi->mode == PAETH_PRED)
      continue;
    if (av1_is_directional_mode(mbmi->mode) &&
        !(av1_use_angle_delta(bsize) && intra_mode_cfg->enable_angle_delta) &&
        mbmi->angle_delta[PLANE_TYPE_Y] != 0)
      continue;
    const PREDICTION_MODE this_mode = mbmi->mode;

    assert(av1_mode_defs[mode_enum].ref_frame[0] == INTRA_FRAME);
    assert(av1_mode_defs[mode_enum].ref_frame[1] == NONE_FRAME);
    init_mbmi(mbmi, this_mode, av1_mode_defs[mode_enum].ref_frame, cm);
    x->txfm_search_info.skip_txfm = 0;

    if (this_mode != DC_PRED) {
      // Only search the oblique modes if the best so far is
      // one of the neighboring directional modes
      if ((sf->rt_sf.mode_search_skip_flags & FLAG_SKIP_INTRA_BESTINTER) &&
          (this_mode >= D45_PRED && this_mode <= PAETH_PRED)) {
        if (search_state->best_mode_index != THR_INVALID &&
            search_state->best_mbmode.ref_frame[0] > INTRA_FRAME)
          continue;
      }
      if (sf->rt_sf.mode_search_skip_flags & FLAG_SKIP_INTRA_DIRMISMATCH) {
        if (conditional_skipintra(
                this_mode, search_state->intra_search_state.best_intra_mode))
          continue;
      }
    }

    RD_STATS intra_rd_stats_y;
    int mode_cost_y;
    int64_t intra_rd_y = INT64_MAX;
    const int is_luma_result_valid = av1_handle_intra_y_mode(
        intra_search_state, cpi, x, bsize, intra_ref_frame_cost, ctx,
        &intra_rd_stats_y, search_state->best_rd, &mode_cost_y, &intra_rd_y,
        &best_model_rd, top_intra_model_rd);
    if (is_luma_result_valid && intra_rd_y < yrd_threshold) {
      is_best_y_mode_intra = 1;
      if (intra_rd_y < best_rd_y) {
        best_intra_rd_stats_y = intra_rd_stats_y;
        best_mode_cost_y = mode_cost_y;
        best_rd_y = intra_rd_y;
        best_mbmi = *mbmi;
        best_mode_enum = mode_enum;
        memcpy(best_blk_skip, x->txfm_search_info.blk_skip,
               sizeof(best_blk_skip[0]) * num_4x4);
        av1_copy_array(best_tx_type_map, xd->tx_type_map, num_4x4);
      }
    }
  }

  if (!is_best_y_mode_intra) {
    return;
  }

  assert(best_rd_y < INT64_MAX);

  // Restores the best luma mode
  *mbmi = best_mbmi;
  memcpy(x->txfm_search_info.blk_skip, best_blk_skip,
         sizeof(best_blk_skip[0]) * num_4x4);
  av1_copy_array(xd->tx_type_map, best_tx_type_map, num_4x4);

  // Performs chroma search
  RD_STATS intra_rd_stats, intra_rd_stats_uv;
  av1_init_rd_stats(&intra_rd_stats);
  av1_init_rd_stats(&intra_rd_stats_uv);
  const int num_planes = av1_num_planes(cm);
  if (num_planes > 1) {
    const int intra_uv_mode_valid = av1_search_intra_uv_modes_in_interframe(
        intra_search_state, cpi, x, bsize, &intra_rd_stats,
        &best_intra_rd_stats_y, &intra_rd_stats_uv, search_state->best_rd);

    if (!intra_uv_mode_valid) {
      return;
    }
  }

  // Merge the luma and chroma rd stats
  assert(best_mode_cost_y >= 0);
  intra_rd_stats.rate = best_intra_rd_stats_y.rate + best_mode_cost_y;
  if (!xd->lossless[mbmi->segment_id] && block_signals_txsize(bsize)) {
    // av1_pick_uniform_tx_size_type_yrd above includes the cost of the tx_size
    // in the tokenonly rate, but for intra blocks, tx_size is always coded
    // (prediction granularity), so we account for it in the full rate,
    // not the tokenonly rate.
    best_intra_rd_stats_y.rate -= tx_size_cost(x, bsize, mbmi->tx_size);
  }

  const ModeCosts *mode_costs = &x->mode_costs;
  const PREDICTION_MODE mode = mbmi->mode;
  if (num_planes > 1 && xd->is_chroma_ref) {
    const int uv_mode_cost =
        mode_costs->intra_uv_mode_cost[is_cfl_allowed(xd)][mode][mbmi->uv_mode];
    intra_rd_stats.rate +=
        intra_rd_stats_uv.rate +
        intra_mode_info_cost_uv(cpi, x, mbmi, bsize, uv_mode_cost);
  }

  // Intra block is always coded as non-skip
  intra_rd_stats.skip_txfm = 0;
  intra_rd_stats.dist = best_intra_rd_stats_y.dist + intra_rd_stats_uv.dist;
  // Add in the cost of the no skip flag.
  const int skip_ctx = av1_get_skip_txfm_context(xd);
  intra_rd_stats.rate += mode_costs->skip_txfm_cost[skip_ctx][0];
  // Calculate the final RD estimate for this mode.
  const int64_t this_rd =
      RDCOST(x->rdmult, intra_rd_stats.rate, intra_rd_stats.dist);
  // Keep record of best intra rd
  if (this_rd < search_state->best_intra_rd) {
    search_state->best_intra_rd = this_rd;
    intra_search_state->best_intra_mode = mode;
  }

  for (int i = 0; i < REFERENCE_MODES; ++i) {
    search_state->best_pred_rd[i] =
        AOMMIN(search_state->best_pred_rd[i], this_rd);
  }

  intra_rd_stats.rdcost = this_rd;

  // Collect mode stats for multiwinner mode processing
  const int txfm_search_done = 1;
  store_winner_mode_stats(
      &cpi->common, x, mbmi, &intra_rd_stats, &best_intra_rd_stats_y,
      &intra_rd_stats_uv, best_mode_enum, NULL, bsize, intra_rd_stats.rdcost,
      cpi->sf.winner_mode_sf.multi_winner_mode_type, txfm_search_done);
  if (intra_rd_stats.rdcost < search_state->best_rd) {
    update_search_state(search_state, rd_cost, ctx, &intra_rd_stats,
                        &best_intra_rd_stats_y, &intra_rd_stats_uv,
                        best_mode_enum, x, txfm_search_done);
  }
}

#if !CONFIG_REALTIME_ONLY
// Prepare inter_cost and intra_cost from TPL stats, which are used as ML
// features in intra mode pruning.
static inline void calculate_cost_from_tpl_data(const AV1_COMP *cpi,
                                                MACROBLOCK *x, BLOCK_SIZE bsize,
                                                int mi_row, int mi_col,
                                                int64_t *inter_cost,
                                                int64_t *intra_cost) {
  const AV1_COMMON *const cm = &cpi->common;
  // Only consider full SB.
  const BLOCK_SIZE sb_size = cm->seq_params->sb_size;
  const int tpl_bsize_1d = cpi->ppi->tpl_data.tpl_bsize_1d;
  const int len = (block_size_wide[sb_size] / tpl_bsize_1d) *
                  (block_size_high[sb_size] / tpl_bsize_1d);
  SuperBlockEnc *sb_enc = &x->sb_enc;
  if (sb_enc->tpl_data_count == len) {
    const BLOCK_SIZE tpl_bsize = convert_length_to_bsize(tpl_bsize_1d);
    const int tpl_stride = sb_enc->tpl_stride;
    const int tplw = mi_size_wide[tpl_bsize];
    const int tplh = mi_size_high[tpl_bsize];
    const int nw = mi_size_wide[bsize] / tplw;
    const int nh = mi_size_high[bsize] / tplh;
    if (nw >= 1 && nh >= 1) {
      const int of_h = mi_row % mi_size_high[sb_size];
      const int of_w = mi_col % mi_size_wide[sb_size];
      const int start = of_h / tplh * tpl_stride + of_w / tplw;

      for (int k = 0; k < nh; k++) {
        for (int l = 0; l < nw; l++) {
          *inter_cost += sb_enc->tpl_inter_cost[start + k * tpl_stride + l];
          *intra_cost += sb_enc->tpl_intra_cost[start + k * tpl_stride + l];
        }
      }
      *inter_cost /= nw * nh;
      *intra_cost /= nw * nh;
    }
  }
}
#endif  // !CONFIG_REALTIME_ONLY

// When the speed feature skip_intra_in_interframe > 0, enable ML model to prune
// intra mode search.
static inline void skip_intra_modes_in_interframe(
    AV1_COMMON *const cm, struct macroblock *x, BLOCK_SIZE bsize,
    InterModeSearchState *search_state, const SPEED_FEATURES *const sf,
    int64_t inter_cost, int64_t intra_cost) {
  MACROBLOCKD *const xd = &x->e_mbd;
  const int comp_pred = search_state->best_mbmode.ref_frame[1] > INTRA_FRAME;
  if (sf->rt_sf.prune_intra_mode_based_on_mv_range &&
      bsize > sf->part_sf.max_intra_bsize && !comp_pred) {
    const MV best_mv = search_state->best_mbmode.mv[0].as_mv;
    const int mv_thresh = 16 << sf->rt_sf.prune_intra_mode_based_on_mv_range;
    if (abs(best_mv.row) < mv_thresh && abs(best_mv.col) < mv_thresh &&
        x->source_variance > 128) {
      search_state->intra_search_state.skip_intra_modes = 1;
      return;
    }
  }

  const unsigned int src_var_thresh_intra_skip = 1;
  const int skip_intra_in_interframe = sf->intra_sf.skip_intra_in_interframe;
  if (!(skip_intra_in_interframe &&
        (x->source_variance > src_var_thresh_intra_skip)))
    return;

  // Prune intra search based on best inter mode being transfrom skip.
  if ((skip_intra_in_interframe >= 2) && search_state->best_mbmode.skip_txfm) {
    const int qindex_thresh[2] = { 200, MAXQ };
    const int ind = (skip_intra_in_interframe >= 3) ? 1 : 0;
    if (!have_newmv_in_inter_mode(search_state->best_mbmode.mode) &&
        (x->qindex <= qindex_thresh[ind])) {
      search_state->intra_search_state.skip_intra_modes = 1;
      return;
    } else if ((skip_intra_in_interframe >= 4) &&
               (inter_cost < 0 || intra_cost < 0)) {
      search_state->intra_search_state.skip_intra_modes = 1;
      return;
    }
  }
  // Use ML model to prune intra search.
  if (inter_cost >= 0 && intra_cost >= 0) {
    const NN_CONFIG *nn_config = (AOMMIN(cm->width, cm->height) <= 480)
                                     ? &av1_intrap_nn_config
                                     : &av1_intrap_hd_nn_config;
    float nn_features[6];
    float scores[2] = { 0.0f };

    nn_features[0] = (float)search_state->best_mbmode.skip_txfm;
    nn_features[1] = (float)mi_size_wide_log2[bsize];
    nn_features[2] = (float)mi_size_high_log2[bsize];
    nn_features[3] = (float)intra_cost;
    nn_features[4] = (float)inter_cost;
    const int ac_q = av1_ac_quant_QTX(x->qindex, 0, xd->bd);
    const int ac_q_max = av1_ac_quant_QTX(255, 0, xd->bd);
    nn_features[5] = (float)(ac_q_max / ac_q);

    av1_nn_predict(nn_features, nn_config, 1, scores);

    // For two parameters, the max prob returned from av1_nn_softmax equals
    // 1.0 / (1.0 + e^(-|diff_score|)). Here use scores directly to avoid the
    // calling of av1_nn_softmax.
    const float thresh[5] = { 1.4f, 1.4f, 1.4f, 1.4f, 1.4f };
    assert(skip_intra_in_interframe <= 5);
    if (scores[1] > scores[0] + thresh[skip_intra_in_interframe - 1]) {
      search_state->intra_search_state.skip_intra_modes = 1;
    }
  }
}

static inline bool skip_interp_filter_search(const AV1_COMP *cpi,
                                             int is_single_pred) {
  const MODE encoding_mode = cpi->oxcf.mode;
  if (encoding_mode == REALTIME) {
    return (cpi->common.current_frame.reference_mode == SINGLE_REFERENCE &&
            (cpi->sf.interp_sf.skip_interp_filter_search ||
             cpi->sf.winner_mode_sf.winner_mode_ifs));
  } else if (encoding_mode == GOOD) {
    // Skip interpolation filter search for single prediction modes.
    return (cpi->sf.interp_sf.skip_interp_filter_search && is_single_pred);
  }
  return false;
}

static inline int get_block_temp_var(const AV1_COMP *cpi, const MACROBLOCK *x,
                                     BLOCK_SIZE bsize) {
  const AV1_COMMON *const cm = &cpi->common;
  const SPEED_FEATURES *const sf = &cpi->sf;

  if (sf->part_sf.partition_search_type != VAR_BASED_PARTITION ||
      !sf->rt_sf.short_circuit_low_temp_var ||
      !sf->rt_sf.prune_inter_modes_using_temp_var) {
    return 0;
  }

  const int mi_row = x->e_mbd.mi_row;
  const int mi_col = x->e_mbd.mi_col;
  int is_low_temp_var = 0;

  if (cm->seq_params->sb_size == BLOCK_64X64)
    is_low_temp_var = av1_get_force_skip_low_temp_var_small_sb(
        &x->part_search_info.variance_low[0], mi_row, mi_col, bsize);
  else
    is_low_temp_var = av1_get_force_skip_low_temp_var(
        &x->part_search_info.variance_low[0], mi_row, mi_col, bsize);

  return is_low_temp_var;
}

// TODO(chiyotsai@google.com): See the todo for av1_rd_pick_intra_mode_sb.
void av1_rd_pick_inter_mode(struct AV1_COMP *cpi, struct TileDataEnc *tile_data,
                            struct macroblock *x, struct RD_STATS *rd_cost,
                            BLOCK_SIZE bsize, PICK_MODE_CONTEXT *ctx,
                            int64_t best_rd_so_far) {
  AV1_COMMON *const cm = &cpi->common;
  const FeatureFlags *const features = &cm->features;
  const int num_planes = av1_num_planes(cm);
  const SPEED_FEATURES *const sf = &cpi->sf;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  int i;
  const ModeCosts *mode_costs = &x->mode_costs;
  const int *comp_inter_cost =
      mode_costs->comp_inter_cost[av1_get_reference_mode_context(xd)];

  InterModeSearchState search_state;
  init_inter_mode_search_state(&search_state, cpi, x, bsize, best_rd_so_far);
  INTERINTRA_MODE interintra_modes[REF_FRAMES] = {
    INTERINTRA_MODES, INTERINTRA_MODES, INTERINTRA_MODES, INTERINTRA_MODES,
    INTERINTRA_MODES, INTERINTRA_MODES, INTERINTRA_MODES, INTERINTRA_MODES
  };
  HandleInterModeArgs args = { { NULL },
                               { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE },
                               { NULL },
                               { MAX_SB_SIZE >> 1, MAX_SB_SIZE >> 1,
                                 MAX_SB_SIZE >> 1 },
                               NULL,
                               NULL,
                               NULL,
                               search_state.modelled_rd,
                               INT_MAX,
                               INT_MAX,
                               search_state.simple_rd,
                               0,
                               false,
                               interintra_modes,
                               { { { 0 }, { { 0 } }, { 0 }, 0, 0, 0, 0 } },
                               { { 0, 0 } },
                               { 0 },
                               0,
                               0,
                               -1,
                               -1,
                               -1,
                               { 0 },
                               { 0 },
                               UINT_MAX };
  // Currently, is_low_temp_var is used in real time encoding.
  const int is_low_temp_var = get_block_temp_var(cpi, x, bsize);

  for (i = 0; i < MODE_CTX_REF_FRAMES; ++i) args.cmp_mode[i] = -1;
  // Indicates the appropriate number of simple translation winner modes for
  // exhaustive motion mode evaluation
  const int max_winner_motion_mode_cand =
      num_winner_motion_modes[sf->winner_mode_sf.motion_mode_for_winner_cand];
  assert(max_winner_motion_mode_cand <= MAX_WINNER_MOTION_MODES);
  motion_mode_candidate motion_mode_cand;
  motion_mode_best_st_candidate best_motion_mode_cands;
  // Initializing the number of motion mode candidates to zero.
  best_motion_mode_cands.num_motion_mode_cand = 0;
  for (i = 0; i < MAX_WINNER_MOTION_MODES; ++i)
    best_motion_mode_cands.motion_mode_cand[i].rd_cost = INT64_MAX;

  for (i = 0; i < REF_FRAMES; ++i) x->pred_sse[i] = INT_MAX;

  av1_invalid_rd_stats(rd_cost);

  for (i = 0; i < REF_FRAMES; ++i) {
    x->warp_sample_info[i].num = -1;
  }

  // Ref frames that are selected by square partition blocks.
  int picked_ref_frames_mask = 0;
  if (sf->inter_sf.prune_ref_frame_for_rect_partitions &&
      mbmi->partition != PARTITION_NONE) {
    // prune_ref_frame_for_rect_partitions = 1 implies prune only extended
    // partition blocks. prune_ref_frame_for_rect_partitions >=2
    // implies prune for vert, horiz and extended partition blocks.
    if ((mbmi->partition != PARTITION_VERT &&
         mbmi->partition != PARTITION_HORZ) ||
        sf->inter_sf.prune_ref_frame_for_rect_partitions >= 2) {
      picked_ref_frames_mask =
          fetch_picked_ref_frames_mask(x, bsize, cm->seq_params->mib_size);
    }
  }

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, set_params_rd_pick_inter_mode_time);
#endif
  // Skip ref frames that never selected by square blocks.
  const int skip_ref_frame_mask =
      picked_ref_frames_mask ? ~picked_ref_frames_mask : 0;
  mode_skip_mask_t mode_skip_mask;
  unsigned int ref_costs_single[REF_FRAMES];
  unsigned int ref_costs_comp[REF_FRAMES][REF_FRAMES];
  struct buf_2d yv12_mb[REF_FRAMES][MAX_MB_PLANE];
  // init params, set frame modes, speed features
  set_params_rd_pick_inter_mode(cpi, x, &args, bsize, &mode_skip_mask,
                                skip_ref_frame_mask, ref_costs_single,
                                ref_costs_comp, yv12_mb);
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, set_params_rd_pick_inter_mode_time);
#endif

  int64_t best_est_rd = INT64_MAX;
  const InterModeRdModel *md = &tile_data->inter_mode_rd_models[bsize];
  // If do_tx_search is 0, only estimated RD should be computed.
  // If do_tx_search is 1, all modes have TX search performed.
  const int do_tx_search =
      !((sf->inter_sf.inter_mode_rd_model_estimation == 1 && md->ready) ||
        (sf->inter_sf.inter_mode_rd_model_estimation == 2 &&
         num_pels_log2_lookup[bsize] > 8));
  InterModesInfo *inter_modes_info = x->inter_modes_info;
  inter_modes_info->num = 0;

  // Temporary buffers used by handle_inter_mode().
  uint8_t *const tmp_buf = get_buf_by_bd(xd, x->tmp_pred_bufs[0]);

  // The best RD found for the reference frame, among single reference modes.
  // Note that the 0-th element will contain a cut-off that is later used
  // to determine if we should skip a compound mode.
  int64_t ref_frame_rd[REF_FRAMES] = { INT64_MAX, INT64_MAX, INT64_MAX,
                                       INT64_MAX, INT64_MAX, INT64_MAX,
                                       INT64_MAX, INT64_MAX };

  // Prepared stats used later to check if we could skip intra mode eval.
  int64_t inter_cost = -1;
  int64_t intra_cost = -1;
  // Need to tweak the threshold for hdres speed 0 & 1.
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  // Obtain the relevant tpl stats for pruning inter modes
  PruneInfoFromTpl inter_cost_info_from_tpl;
#if !CONFIG_REALTIME_ONLY
  if (sf->inter_sf.prune_inter_modes_based_on_tpl) {
    // x->tpl_keep_ref_frame[id] = 1 => no pruning in
    // prune_ref_by_selective_ref_frame()
    // x->tpl_keep_ref_frame[id] = 0  => ref frame can be pruned in
    // prune_ref_by_selective_ref_frame()
    // Populating valid_refs[idx] = 1 ensures that
    // 'inter_cost_info_from_tpl.best_inter_cost' does not correspond to a
    // pruned ref frame.
    int valid_refs[INTER_REFS_PER_FRAME];
    for (MV_REFERENCE_FRAME frame = LAST_FRAME; frame < REF_FRAMES; frame++) {
      const MV_REFERENCE_FRAME refs[2] = { frame, NONE_FRAME };
      valid_refs[frame - 1] =
          x->tpl_keep_ref_frame[frame] ||
          !prune_ref_by_selective_ref_frame(
              cpi, x, refs, cm->cur_frame->ref_display_order_hint);
    }
    av1_zero(inter_cost_info_from_tpl);
    get_block_level_tpl_stats(cpi, bsize, mi_row, mi_col, valid_refs,
                              &inter_cost_info_from_tpl);
  }

  const int do_pruning =
      (AOMMIN(cm->width, cm->height) > 480 && cpi->speed <= 1) ? 0 : 1;
  if (do_pruning && sf->intra_sf.skip_intra_in_interframe &&
      cpi->oxcf.algo_cfg.enable_tpl_model)
    calculate_cost_from_tpl_data(cpi, x, bsize, mi_row, mi_col, &inter_cost,
                                 &intra_cost);
#endif  // !CONFIG_REALTIME_ONLY

  // Initialize best mode stats for winner mode processing.
  const int max_winner_mode_count =
      winner_mode_count_allowed[sf->winner_mode_sf.multi_winner_mode_type];
  zero_winner_mode_stats(bsize, max_winner_mode_count, x->winner_mode_stats);
  x->winner_mode_count = 0;
  store_winner_mode_stats(&cpi->common, x, mbmi, NULL, NULL, NULL, THR_INVALID,
                          NULL, bsize, best_rd_so_far,
                          sf->winner_mode_sf.multi_winner_mode_type, 0);

  int mode_thresh_mul_fact = (1 << MODE_THRESH_QBITS);
  if (sf->inter_sf.prune_inter_modes_if_skippable) {
    // Higher multiplication factor values for lower quantizers.
    mode_thresh_mul_fact = mode_threshold_mul_factor[x->qindex];
  }

  // Initialize arguments for mode loop speed features
  InterModeSFArgs sf_args = { &args.skip_motion_mode,
                              &mode_skip_mask,
                              &search_state,
                              skip_ref_frame_mask,
                              0,
                              mode_thresh_mul_fact,
                              0,
                              0 };
  int64_t best_inter_yrd = INT64_MAX;

  // This is the main loop of this function. It loops over all possible inter
  // modes and calls handle_inter_mode() to compute the RD for each.
  // Here midx is just an iterator index that should not be used by itself
  // except to keep track of the number of modes searched. It should be used
  // with av1_default_mode_order to get the enum that defines the mode, which
  // can be used with av1_mode_defs to get the prediction mode and the ref
  // frames.
  // TODO(yunqing, any): Setting mode_start and mode_end outside for-loop brings
  // good speedup for real time case. If we decide to use compound mode in real
  // time, maybe we can modify av1_default_mode_order table.
  THR_MODES mode_start = THR_INTER_MODE_START;
  THR_MODES mode_end = THR_INTER_MODE_END;
  const CurrentFrame *const current_frame = &cm->current_frame;
  if (current_frame->reference_mode == SINGLE_REFERENCE) {
    mode_start = SINGLE_REF_MODE_START;
    mode_end = SINGLE_REF_MODE_END;
  }

  for (THR_MODES midx = mode_start; midx < mode_end; ++midx) {
    // Get the actual prediction mode we are trying in this iteration
    const THR_MODES mode_enum = av1_default_mode_order[midx];
    const MODE_DEFINITION *mode_def = &av1_mode_defs[mode_enum];
    const PREDICTION_MODE this_mode = mode_def->mode;
    const MV_REFERENCE_FRAME *ref_frames = mode_def->ref_frame;

    const MV_REFERENCE_FRAME ref_frame = ref_frames[0];
    const MV_REFERENCE_FRAME second_ref_frame = ref_frames[1];
    const int is_single_pred =
        ref_frame > INTRA_FRAME && second_ref_frame == NONE_FRAME;
    const int comp_pred = second_ref_frame > INTRA_FRAME;

    init_mbmi(mbmi, this_mode, ref_frames, cm);

    txfm_info->skip_txfm = 0;
    sf_args.num_single_modes_processed += is_single_pred;
    set_ref_ptrs(cm, xd, ref_frame, second_ref_frame);
#if CONFIG_COLLECT_COMPONENT_TIMING
    start_timing(cpi, skip_inter_mode_time);
#endif
    // Apply speed features to decide if this inter mode can be skipped
    const int is_skip_inter_mode = skip_inter_mode(
        cpi, x, bsize, ref_frame_rd, midx, &sf_args, is_low_temp_var);
#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(cpi, skip_inter_mode_time);
#endif
    if (is_skip_inter_mode) continue;

    // Select prediction reference frames.
    for (i = 0; i < num_planes; i++) {
      xd->plane[i].pre[0] = yv12_mb[ref_frame][i];
      if (comp_pred) xd->plane[i].pre[1] = yv12_mb[second_ref_frame][i];
    }

    mbmi->angle_delta[PLANE_TYPE_Y] = 0;
    mbmi->angle_delta[PLANE_TYPE_UV] = 0;
    mbmi->filter_intra_mode_info.use_filter_intra = 0;
    mbmi->ref_mv_idx = 0;

    const int64_t ref_best_rd = search_state.best_rd;
    RD_STATS rd_stats, rd_stats_y, rd_stats_uv;
    av1_init_rd_stats(&rd_stats);

    const int ref_frame_cost = comp_pred
                                   ? ref_costs_comp[ref_frame][second_ref_frame]
                                   : ref_costs_single[ref_frame];
    const int compmode_cost =
        is_comp_ref_allowed(mbmi->bsize) ? comp_inter_cost[comp_pred] : 0;
    const int real_compmode_cost =
        cm->current_frame.reference_mode == REFERENCE_MODE_SELECT
            ? compmode_cost
            : 0;
    // Point to variables that are maintained between loop iterations
    args.single_newmv = search_state.single_newmv;
    args.single_newmv_rate = search_state.single_newmv_rate;
    args.single_newmv_valid = search_state.single_newmv_valid;
    args.single_comp_cost = real_compmode_cost;
    args.ref_frame_cost = ref_frame_cost;
    args.best_pred_sse = search_state.best_pred_sse;
    args.skip_ifs = skip_interp_filter_search(cpi, is_single_pred);

    int64_t skip_rd[2] = { search_state.best_skip_rd[0],
                           search_state.best_skip_rd[1] };
    int64_t this_yrd = INT64_MAX;
#if CONFIG_COLLECT_COMPONENT_TIMING
    start_timing(cpi, handle_inter_mode_time);
#endif
    int64_t this_rd = handle_inter_mode(
        cpi, tile_data, x, bsize, &rd_stats, &rd_stats_y, &rd_stats_uv, &args,
        ref_best_rd, tmp_buf, &x->comp_rd_buffer, &best_est_rd, do_tx_search,
        inter_modes_info, &motion_mode_cand, skip_rd, &inter_cost_info_from_tpl,
        &this_yrd);
#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(cpi, handle_inter_mode_time);
#endif
    if (current_frame->reference_mode != SINGLE_REFERENCE) {
      if (!args.skip_ifs &&
          sf->inter_sf.prune_comp_search_by_single_result > 0 &&
          is_inter_singleref_mode(this_mode)) {
        collect_single_states(x, &search_state, mbmi);
      }

      if (sf->inter_sf.prune_comp_using_best_single_mode_ref > 0 &&
          is_inter_singleref_mode(this_mode))
        update_best_single_mode(&search_state, this_mode, ref_frame, this_rd);
    }

    if (this_rd == INT64_MAX) continue;

    if (mbmi->skip_txfm) {
      rd_stats_y.rate = 0;
      rd_stats_uv.rate = 0;
    }

    if (sf->inter_sf.prune_compound_using_single_ref && is_single_pred &&
        this_rd < ref_frame_rd[ref_frame]) {
      ref_frame_rd[ref_frame] = this_rd;
    }

    // Did this mode help, i.e., is it the new best mode
    if (this_rd < search_state.best_rd) {
      assert(IMPLIES(comp_pred,
                     cm->current_frame.reference_mode != SINGLE_REFERENCE));
      search_state.best_pred_sse = x->pred_sse[ref_frame];
      best_inter_yrd = this_yrd;
      update_search_state(&search_state, rd_cost, ctx, &rd_stats, &rd_stats_y,
                          &rd_stats_uv, mode_enum, x, do_tx_search);
      if (do_tx_search) search_state.best_skip_rd[0] = skip_rd[0];
      // skip_rd[0] is the best total rd for a skip mode so far.
      // skip_rd[1] is the best total rd for a skip mode so far in luma.
      // When do_tx_search = 1, both skip_rd[0] and skip_rd[1] are updated.
      // When do_tx_search = 0, skip_rd[1] is updated.
      search_state.best_skip_rd[1] = skip_rd[1];
    }
    if (sf->winner_mode_sf.motion_mode_for_winner_cand) {
      // Add this mode to motion mode candidate list for motion mode search
      // if using motion_mode_for_winner_cand speed feature
      handle_winner_cand(mbmi, &best_motion_mode_cands,
                         max_winner_motion_mode_cand, this_rd,
                         &motion_mode_cand, args.skip_motion_mode);
    }

    /* keep record of best compound/single-only prediction */
    record_best_compound(cm->current_frame.reference_mode, &rd_stats, comp_pred,
                         x->rdmult, &search_state, compmode_cost);
  }

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, evaluate_motion_mode_for_winner_candidates_time);
#endif
  if (sf->winner_mode_sf.motion_mode_for_winner_cand) {
    // For the single ref winner candidates, evaluate other motion modes (non
    // simple translation).
    evaluate_motion_mode_for_winner_candidates(
        cpi, x, rd_cost, &args, tile_data, ctx, yv12_mb,
        &best_motion_mode_cands, do_tx_search, bsize, &best_est_rd,
        &search_state, &best_inter_yrd);
  }
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, evaluate_motion_mode_for_winner_candidates_time);
#endif

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, do_tx_search_time);
#endif
  if (do_tx_search != 1) {
    // A full tx search has not yet been done, do tx search for
    // top mode candidates
    tx_search_best_inter_candidates(cpi, tile_data, x, best_rd_so_far, bsize,
                                    yv12_mb, mi_row, mi_col, &search_state,
                                    rd_cost, ctx, &best_inter_yrd);
  }
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, do_tx_search_time);
#endif

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, handle_intra_mode_time);
#endif
  // Gate intra mode evaluation if best of inter is skip except when source
  // variance is extremely low and also based on max intra bsize.
  skip_intra_modes_in_interframe(cm, x, bsize, &search_state, sf, inter_cost,
                                 intra_cost);

  const unsigned int intra_ref_frame_cost = ref_costs_single[INTRA_FRAME];
  search_intra_modes_in_interframe(&search_state, cpi, x, rd_cost, bsize, ctx,
                                   &sf_args, intra_ref_frame_cost,
                                   best_inter_yrd);
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, handle_intra_mode_time);
#endif

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, refine_winner_mode_tx_time);
#endif
  int winner_mode_count =
      sf->winner_mode_sf.multi_winner_mode_type ? x->winner_mode_count : 1;
  // In effect only when fast tx search speed features are enabled.
  refine_winner_mode_tx(
      cpi, x, rd_cost, bsize, ctx, &search_state.best_mode_index,
      &search_state.best_mbmode, yv12_mb, search_state.best_rate_y,
      search_state.best_rate_uv, &search_state.best_skip2, winner_mode_count);
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, refine_winner_mode_tx_time);
#endif

  // Initialize default mode evaluation params
  set_mode_eval_params(cpi, x, DEFAULT_EVAL);

  // Only try palette mode when the best mode so far is an intra mode.
  const int try_palette =
      cpi->oxcf.tool_cfg.enable_palette &&
      av1_allow_palette(features->allow_screen_content_tools, mbmi->bsize) &&
      !is_inter_mode(search_state.best_mbmode.mode) && rd_cost->rate != INT_MAX;
  RD_STATS this_rd_cost;
  int this_skippable = 0;
  if (try_palette) {
#if CONFIG_COLLECT_COMPONENT_TIMING
    start_timing(cpi, av1_search_palette_mode_time);
#endif
    this_skippable = av1_search_palette_mode(
        &search_state.intra_search_state, cpi, x, bsize, intra_ref_frame_cost,
        ctx, &this_rd_cost, search_state.best_rd);
#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(cpi, av1_search_palette_mode_time);
#endif
    if (this_rd_cost.rdcost < search_state.best_rd) {
      search_state.best_mode_index = THR_DC;
      mbmi->mv[0].as_int = 0;
      rd_cost->rate = this_rd_cost.rate;
      rd_cost->dist = this_rd_cost.dist;
      rd_cost->rdcost = this_rd_cost.rdcost;
      search_state.best_rd = rd_cost->rdcost;
      search_state.best_mbmode = *mbmi;
      search_state.best_skip2 = 0;
      search_state.best_mode_skippable = this_skippable;
      memcpy(ctx->blk_skip, txfm_info->blk_skip,
             sizeof(txfm_info->blk_skip[0]) * ctx->num_4x4_blk);
      av1_copy_array(ctx->tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
    }
  }

  search_state.best_mbmode.skip_mode = 0;
  if (cm->current_frame.skip_mode_info.skip_mode_flag &&
      is_comp_ref_allowed(bsize)) {
    const struct segmentation *const seg = &cm->seg;
    unsigned char segment_id = mbmi->segment_id;
    if (!segfeature_active(seg, segment_id, SEG_LVL_REF_FRAME)) {
      rd_pick_skip_mode(rd_cost, &search_state, cpi, x, bsize, yv12_mb);
    }
  }

  // Make sure that the ref_mv_idx is only nonzero when we're
  // using a mode which can support ref_mv_idx
  if (search_state.best_mbmode.ref_mv_idx != 0 &&
      !(search_state.best_mbmode.mode == NEWMV ||
        search_state.best_mbmode.mode == NEW_NEWMV ||
        have_nearmv_in_inter_mode(search_state.best_mbmode.mode))) {
    search_state.best_mbmode.ref_mv_idx = 0;
  }

  if (search_state.best_mode_index == THR_INVALID ||
      search_state.best_rd >= best_rd_so_far) {
    rd_cost->rate = INT_MAX;
    rd_cost->rdcost = INT64_MAX;
    return;
  }

  const InterpFilter interp_filter = features->interp_filter;
  assert((interp_filter == SWITCHABLE) ||
         (interp_filter ==
          search_state.best_mbmode.interp_filters.as_filters.y_filter) ||
         !is_inter_block(&search_state.best_mbmode));
  assert((interp_filter == SWITCHABLE) ||
         (interp_filter ==
          search_state.best_mbmode.interp_filters.as_filters.x_filter) ||
         !is_inter_block(&search_state.best_mbmode));

  if (!cpi->rc.is_src_frame_alt_ref && sf->inter_sf.adaptive_rd_thresh) {
    av1_update_rd_thresh_fact(
        cm, x->thresh_freq_fact, sf->inter_sf.adaptive_rd_thresh, bsize,
        search_state.best_mode_index, mode_start, mode_end, THR_DC, MAX_MODES);
  }

  // macroblock modes
  *mbmi = search_state.best_mbmode;
  txfm_info->skip_txfm |= search_state.best_skip2;

  // Note: this section is needed since the mode may have been forced to
  // GLOBALMV by the all-zero mode handling of ref-mv.
  if (mbmi->mode == GLOBALMV || mbmi->mode == GLOBAL_GLOBALMV) {
    // Correct the interp filters for GLOBALMV
    if (is_nontrans_global_motion(xd, xd->mi[0])) {
      int_interpfilters filters =
          av1_broadcast_interp_filter(av1_unswitchable_filter(interp_filter));
      assert(mbmi->interp_filters.as_int == filters.as_int);
      (void)filters;
    }
  }

  txfm_info->skip_txfm |= search_state.best_mode_skippable;

  assert(search_state.best_mode_index != THR_INVALID);

#if CONFIG_INTERNAL_STATS
  store_coding_context(x, ctx, search_state.best_mode_index,
                       search_state.best_mode_skippable);
#else
  store_coding_context(x, ctx, search_state.best_mode_skippable);
#endif  // CONFIG_INTERNAL_STATS

  if (mbmi->palette_mode_info.palette_size[1] > 0) {
    assert(try_palette);
    av1_restore_uv_color_map(cpi, x);
  }
}

void av1_rd_pick_inter_mode_sb_seg_skip(const AV1_COMP *cpi,
                                        TileDataEnc *tile_data, MACROBLOCK *x,
                                        int mi_row, int mi_col,
                                        RD_STATS *rd_cost, BLOCK_SIZE bsize,
                                        PICK_MODE_CONTEXT *ctx,
                                        int64_t best_rd_so_far) {
  const AV1_COMMON *const cm = &cpi->common;
  const FeatureFlags *const features = &cm->features;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  unsigned char segment_id = mbmi->segment_id;
  const int comp_pred = 0;
  int i;
  unsigned int ref_costs_single[REF_FRAMES];
  unsigned int ref_costs_comp[REF_FRAMES][REF_FRAMES];
  const ModeCosts *mode_costs = &x->mode_costs;
  const int *comp_inter_cost =
      mode_costs->comp_inter_cost[av1_get_reference_mode_context(xd)];
  InterpFilter best_filter = SWITCHABLE;
  int64_t this_rd = INT64_MAX;
  int rate2 = 0;
  const int64_t distortion2 = 0;
  (void)mi_row;
  (void)mi_col;
  (void)tile_data;

  av1_collect_neighbors_ref_counts(xd);

  estimate_ref_frame_costs(cm, xd, mode_costs, segment_id, ref_costs_single,
                           ref_costs_comp);

  for (i = 0; i < REF_FRAMES; ++i) x->pred_sse[i] = INT_MAX;
  for (i = LAST_FRAME; i < REF_FRAMES; ++i) x->pred_mv_sad[i] = INT_MAX;

  rd_cost->rate = INT_MAX;

  assert(segfeature_active(&cm->seg, segment_id, SEG_LVL_SKIP));

  mbmi->palette_mode_info.palette_size[0] = 0;
  mbmi->palette_mode_info.palette_size[1] = 0;
  mbmi->filter_intra_mode_info.use_filter_intra = 0;
  mbmi->mode = GLOBALMV;
  mbmi->motion_mode = SIMPLE_TRANSLATION;
  mbmi->uv_mode = UV_DC_PRED;
  if (segfeature_active(&cm->seg, segment_id, SEG_LVL_REF_FRAME))
    mbmi->ref_frame[0] = get_segdata(&cm->seg, segment_id, SEG_LVL_REF_FRAME);
  else
    mbmi->ref_frame[0] = LAST_FRAME;
  mbmi->ref_frame[1] = NONE_FRAME;
  mbmi->mv[0].as_int =
      gm_get_motion_vector(&cm->global_motion[mbmi->ref_frame[0]],
                           features->allow_high_precision_mv, bsize, mi_col,
                           mi_row, features->cur_frame_force_integer_mv)
          .as_int;
  mbmi->tx_size = max_txsize_lookup[bsize];
  x->txfm_search_info.skip_txfm = 1;

  mbmi->ref_mv_idx = 0;

  mbmi->motion_mode = SIMPLE_TRANSLATION;
  av1_count_overlappable_neighbors(cm, xd);
  if (is_motion_variation_allowed_bsize(bsize) && !has_second_ref(mbmi)) {
    int pts[SAMPLES_ARRAY_SIZE], pts_inref[SAMPLES_ARRAY_SIZE];
    mbmi->num_proj_ref = av1_findSamples(cm, xd, pts, pts_inref);
    // Select the samples according to motion vector difference
    if (mbmi->num_proj_ref > 1) {
      mbmi->num_proj_ref = av1_selectSamples(&mbmi->mv[0].as_mv, pts, pts_inref,
                                             mbmi->num_proj_ref, bsize);
    }
  }

  const InterpFilter interp_filter = features->interp_filter;
  set_default_interp_filters(mbmi, interp_filter);

  if (interp_filter != SWITCHABLE) {
    best_filter = interp_filter;
  } else {
    best_filter = EIGHTTAP_REGULAR;
    if (av1_is_interp_needed(xd)) {
      int rs;
      int best_rs = INT_MAX;
      for (i = 0; i < SWITCHABLE_FILTERS; ++i) {
        mbmi->interp_filters = av1_broadcast_interp_filter(i);
        rs = av1_get_switchable_rate(x, xd, interp_filter,
                                     cm->seq_params->enable_dual_filter);
        if (rs < best_rs) {
          best_rs = rs;
          best_filter = mbmi->interp_filters.as_filters.y_filter;
        }
      }
    }
  }
  // Set the appropriate filter
  mbmi->interp_filters = av1_broadcast_interp_filter(best_filter);
  rate2 += av1_get_switchable_rate(x, xd, interp_filter,
                                   cm->seq_params->enable_dual_filter);

  if (cm->current_frame.reference_mode == REFERENCE_MODE_SELECT)
    rate2 += comp_inter_cost[comp_pred];

  // Estimate the reference frame signaling cost and add it
  // to the rolling cost variable.
  rate2 += ref_costs_single[LAST_FRAME];
  this_rd = RDCOST(x->rdmult, rate2, distortion2);

  rd_cost->rate = rate2;
  rd_cost->dist = distortion2;
  rd_cost->rdcost = this_rd;

  if (this_rd >= best_rd_so_far) {
    rd_cost->rate = INT_MAX;
    rd_cost->rdcost = INT64_MAX;
    return;
  }

  assert((interp_filter == SWITCHABLE) ||
         (interp_filter == mbmi->interp_filters.as_filters.y_filter));

  if (cpi->sf.inter_sf.adaptive_rd_thresh) {
    av1_update_rd_thresh_fact(cm, x->thresh_freq_fact,
                              cpi->sf.inter_sf.adaptive_rd_thresh, bsize,
                              THR_GLOBALMV, THR_INTER_MODE_START,
                              THR_INTER_MODE_END, THR_DC, MAX_MODES);
  }

#if CONFIG_INTERNAL_STATS
  store_coding_context(x, ctx, THR_GLOBALMV, 0);
#else
  store_coding_context(x, ctx, 0);
#endif  // CONFIG_INTERNAL_STATS
}

/*!\cond */
struct calc_target_weighted_pred_ctxt {
  const OBMCBuffer *obmc_buffer;
  const uint8_t *tmp;
  int tmp_stride;
  int overlap;
};
/*!\endcond */

static inline void calc_target_weighted_pred_above(
    MACROBLOCKD *xd, int rel_mi_row, int rel_mi_col, uint8_t op_mi_size,
    int dir, MB_MODE_INFO *nb_mi, void *fun_ctxt, const int num_planes) {
  (void)nb_mi;
  (void)num_planes;
  (void)rel_mi_row;
  (void)dir;

  struct calc_target_weighted_pred_ctxt *ctxt =
      (struct calc_target_weighted_pred_ctxt *)fun_ctxt;

  const int bw = xd->width << MI_SIZE_LOG2;
  const uint8_t *const mask1d = av1_get_obmc_mask(ctxt->overlap);

  int32_t *wsrc = ctxt->obmc_buffer->wsrc + (rel_mi_col * MI_SIZE);
  int32_t *mask = ctxt->obmc_buffer->mask + (rel_mi_col * MI_SIZE);
  const uint8_t *tmp = ctxt->tmp + rel_mi_col * MI_SIZE;
  const int is_hbd = is_cur_buf_hbd(xd);

  if (!is_hbd) {
    for (int row = 0; row < ctxt->overlap; ++row) {
      const uint8_t m0 = mask1d[row];
      const uint8_t m1 = AOM_BLEND_A64_MAX_ALPHA - m0;
      for (int col = 0; col < op_mi_size * MI_SIZE; ++col) {
        wsrc[col] = m1 * tmp[col];
        mask[col] = m0;
      }
      wsrc += bw;
      mask += bw;
      tmp += ctxt->tmp_stride;
    }
  } else {
    const uint16_t *tmp16 = CONVERT_TO_SHORTPTR(tmp);

    for (int row = 0; row < ctxt->overlap; ++row) {
      const uint8_t m0 = mask1d[row];
      const uint8_t m1 = AOM_BLEND_A64_MAX_ALPHA - m0;
      for (int col = 0; col < op_mi_size * MI_SIZE; ++col) {
        wsrc[col] = m1 * tmp16[col];
        mask[col] = m0;
      }
      wsrc += bw;
      mask += bw;
      tmp16 += ctxt->tmp_stride;
    }
  }
}

static inline void calc_target_weighted_pred_left(
    MACROBLOCKD *xd, int rel_mi_row, int rel_mi_col, uint8_t op_mi_size,
    int dir, MB_MODE_INFO *nb_mi, void *fun_ctxt, const int num_planes) {
  (void)nb_mi;
  (void)num_planes;
  (void)rel_mi_col;
  (void)dir;

  struct calc_target_weighted_pred_ctxt *ctxt =
      (struct calc_target_weighted_pred_ctxt *)fun_ctxt;

  const int bw = xd->width << MI_SIZE_LOG2;
  const uint8_t *const mask1d = av1_get_obmc_mask(ctxt->overlap);

  int32_t *wsrc = ctxt->obmc_buffer->wsrc + (rel_mi_row * MI_SIZE * bw);
  int32_t *mask = ctxt->obmc_buffer->mask + (rel_mi_row * MI_SIZE * bw);
  const uint8_t *tmp = ctxt->tmp + (rel_mi_row * MI_SIZE * ctxt->tmp_stride);
  const int is_hbd = is_cur_buf_hbd(xd);

  if (!is_hbd) {
    for (int row = 0; row < op_mi_size * MI_SIZE; ++row) {
      for (int col = 0; col < ctxt->overlap; ++col) {
        const uint8_t m0 = mask1d[col];
        const uint8_t m1 = AOM_BLEND_A64_MAX_ALPHA - m0;
        wsrc[col] = (wsrc[col] >> AOM_BLEND_A64_ROUND_BITS) * m0 +
                    (tmp[col] << AOM_BLEND_A64_ROUND_BITS) * m1;
        mask[col] = (mask[col] >> AOM_BLEND_A64_ROUND_BITS) * m0;
      }
      wsrc += bw;
      mask += bw;
      tmp += ctxt->tmp_stride;
    }
  } else {
    const uint16_t *tmp16 = CONVERT_TO_SHORTPTR(tmp);

    for (int row = 0; row < op_mi_size * MI_SIZE; ++row) {
      for (int col = 0; col < ctxt->overlap; ++col) {
        const uint8_t m0 = mask1d[col];
        const uint8_t m1 = AOM_BLEND_A64_MAX_ALPHA - m0;
        wsrc[col] = (wsrc[col] >> AOM_BLEND_A64_ROUND_BITS) * m0 +
                    (tmp16[col] << AOM_BLEND_A64_ROUND_BITS) * m1;
        mask[col] = (mask[col] >> AOM_BLEND_A64_ROUND_BITS) * m0;
      }
      wsrc += bw;
      mask += bw;
      tmp16 += ctxt->tmp_stride;
    }
  }
}

// This function has a structure similar to av1_build_obmc_inter_prediction
//
// The OBMC predictor is computed as:
//
//  PObmc(x,y) =
//    AOM_BLEND_A64(Mh(x),
//                  AOM_BLEND_A64(Mv(y), P(x,y), PAbove(x,y)),
//                  PLeft(x, y))
//
// Scaling up by AOM_BLEND_A64_MAX_ALPHA ** 2 and omitting the intermediate
// rounding, this can be written as:
//
//  AOM_BLEND_A64_MAX_ALPHA * AOM_BLEND_A64_MAX_ALPHA * Pobmc(x,y) =
//    Mh(x) * Mv(y) * P(x,y) +
//      Mh(x) * Cv(y) * Pabove(x,y) +
//      AOM_BLEND_A64_MAX_ALPHA * Ch(x) * PLeft(x, y)
//
// Where :
//
//  Cv(y) = AOM_BLEND_A64_MAX_ALPHA - Mv(y)
//  Ch(y) = AOM_BLEND_A64_MAX_ALPHA - Mh(y)
//
// This function computes 'wsrc' and 'mask' as:
//
//  wsrc(x, y) =
//    AOM_BLEND_A64_MAX_ALPHA * AOM_BLEND_A64_MAX_ALPHA * src(x, y) -
//      Mh(x) * Cv(y) * Pabove(x,y) +
//      AOM_BLEND_A64_MAX_ALPHA * Ch(x) * PLeft(x, y)
//
//  mask(x, y) = Mh(x) * Mv(y)
//
// These can then be used to efficiently approximate the error for any
// predictor P in the context of the provided neighbouring predictors by
// computing:
//
//  error(x, y) =
//    wsrc(x, y) - mask(x, y) * P(x, y) / (AOM_BLEND_A64_MAX_ALPHA ** 2)
//
static inline void calc_target_weighted_pred(
    const AV1_COMMON *cm, const MACROBLOCK *x, const MACROBLOCKD *xd,
    const uint8_t *above, int above_stride, const uint8_t *left,
    int left_stride) {
  const BLOCK_SIZE bsize = xd->mi[0]->bsize;
  const int bw = xd->width << MI_SIZE_LOG2;
  const int bh = xd->height << MI_SIZE_LOG2;
  const OBMCBuffer *obmc_buffer = &x->obmc_buffer;
  int32_t *mask_buf = obmc_buffer->mask;
  int32_t *wsrc_buf = obmc_buffer->wsrc;

  const int is_hbd = is_cur_buf_hbd(xd);
  const int src_scale = AOM_BLEND_A64_MAX_ALPHA * AOM_BLEND_A64_MAX_ALPHA;

  // plane 0 should not be sub-sampled
  assert(xd->plane[0].subsampling_x == 0);
  assert(xd->plane[0].subsampling_y == 0);

  av1_zero_array(wsrc_buf, bw * bh);
  for (int i = 0; i < bw * bh; ++i) mask_buf[i] = AOM_BLEND_A64_MAX_ALPHA;

  // handle above row
  if (xd->up_available) {
    const int overlap =
        AOMMIN(block_size_high[bsize], block_size_high[BLOCK_64X64]) >> 1;
    struct calc_target_weighted_pred_ctxt ctxt = { obmc_buffer, above,
                                                   above_stride, overlap };
    foreach_overlappable_nb_above(cm, (MACROBLOCKD *)xd,
                                  max_neighbor_obmc[mi_size_wide_log2[bsize]],
                                  calc_target_weighted_pred_above, &ctxt);
  }

  for (int i = 0; i < bw * bh; ++i) {
    wsrc_buf[i] *= AOM_BLEND_A64_MAX_ALPHA;
    mask_buf[i] *= AOM_BLEND_A64_MAX_ALPHA;
  }

  // handle left column
  if (xd->left_available) {
    const int overlap =
        AOMMIN(block_size_wide[bsize], block_size_wide[BLOCK_64X64]) >> 1;
    struct calc_target_weighted_pred_ctxt ctxt = { obmc_buffer, left,
                                                   left_stride, overlap };
    foreach_overlappable_nb_left(cm, (MACROBLOCKD *)xd,
                                 max_neighbor_obmc[mi_size_high_log2[bsize]],
                                 calc_target_weighted_pred_left, &ctxt);
  }

  if (!is_hbd) {
    const uint8_t *src = x->plane[0].src.buf;

    for (int row = 0; row < bh; ++row) {
      for (int col = 0; col < bw; ++col) {
        wsrc_buf[col] = src[col] * src_scale - wsrc_buf[col];
      }
      wsrc_buf += bw;
      src += x->plane[0].src.stride;
    }
  } else {
    const uint16_t *src = CONVERT_TO_SHORTPTR(x->plane[0].src.buf);

    for (int row = 0; row < bh; ++row) {
      for (int col = 0; col < bw; ++col) {
        wsrc_buf[col] = src[col] * src_scale - wsrc_buf[col];
      }
      wsrc_buf += bw;
      src += x->plane[0].src.stride;
    }
  }
}
