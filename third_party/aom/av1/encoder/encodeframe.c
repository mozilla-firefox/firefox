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

#include <limits.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"
#include "config/av1_rtcd.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_dsp/binary_codes_writer.h"
#include "aom_ports/mem.h"
#include "aom_ports/aom_timer.h"
#include "aom_util/aom_pthread.h"
#if CONFIG_MISMATCH_DEBUG
#include "aom_util/debug_util.h"
#endif  // CONFIG_MISMATCH_DEBUG

#include "av1/common/cfl.h"
#include "av1/common/common.h"
#include "av1/common/common_data.h"
#include "av1/common/entropy.h"
#include "av1/common/entropymode.h"
#include "av1/common/idct.h"
#include "av1/common/mv.h"
#include "av1/common/mvref_common.h"
#include "av1/common/pred_common.h"
#include "av1/common/quant_common.h"
#include "av1/common/reconintra.h"
#include "av1/common/reconinter.h"
#include "av1/common/seg_common.h"
#include "av1/common/tile_common.h"
#include "av1/common/warped_motion.h"

#include "av1/encoder/allintra_vis.h"
#include "av1/encoder/aq_complexity.h"
#include "av1/encoder/aq_cyclicrefresh.h"
#include "av1/encoder/aq_variance.h"
#include "av1/encoder/global_motion_facade.h"
#include "av1/encoder/encodeframe.h"
#include "av1/encoder/encodeframe_utils.h"
#include "av1/encoder/encodemb.h"
#include "av1/encoder/encodemv.h"
#include "av1/encoder/encodetxb.h"
#include "av1/encoder/ethread.h"
#include "av1/encoder/extend.h"
#include "av1/encoder/intra_mode_search_utils.h"
#include "av1/encoder/ml.h"
#include "av1/encoder/motion_search_facade.h"
#include "av1/encoder/partition_strategy.h"
#if !CONFIG_REALTIME_ONLY
#include "av1/encoder/partition_model_weights.h"
#endif
#include "av1/encoder/partition_search.h"
#include "av1/encoder/rd.h"
#include "av1/encoder/rdopt.h"
#include "av1/encoder/reconinter_enc.h"
#include "av1/encoder/segmentation.h"
#include "av1/encoder/tokenize.h"
#include "av1/encoder/tpl_model.h"
#include "av1/encoder/var_based_part.h"

#if CONFIG_TUNE_VMAF
#include "av1/encoder/tune_vmaf.h"
#endif

/*!\cond */
// This is used as a reference when computing the source variance for the
//  purposes of activity masking.
// Eventually this should be replaced by custom no-reference routines,
//  which will be faster.
static const uint8_t AV1_VAR_OFFS[MAX_SB_SIZE] = {
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128
};

#if CONFIG_AV1_HIGHBITDEPTH
static const uint16_t AV1_HIGH_VAR_OFFS_8[MAX_SB_SIZE] = {
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128
};

static const uint16_t AV1_HIGH_VAR_OFFS_10[MAX_SB_SIZE] = {
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4
};

static const uint16_t AV1_HIGH_VAR_OFFS_12[MAX_SB_SIZE] = {
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16
};
#endif  // CONFIG_AV1_HIGHBITDEPTH
/*!\endcond */

// For the given bit depth, returns a constant array used to assist the
// calculation of source block variance, which will then be used to decide
// adaptive quantizers.
static const uint8_t *get_var_offs(int use_hbd, int bd) {
#if CONFIG_AV1_HIGHBITDEPTH
  if (use_hbd) {
    assert(bd == 8 || bd == 10 || bd == 12);
    const int off_index = (bd - 8) >> 1;
    static const uint16_t *high_var_offs[3] = { AV1_HIGH_VAR_OFFS_8,
                                                AV1_HIGH_VAR_OFFS_10,
                                                AV1_HIGH_VAR_OFFS_12 };
    return CONVERT_TO_BYTEPTR(high_var_offs[off_index]);
  }
#else
  (void)use_hbd;
  (void)bd;
  assert(!use_hbd);
#endif
  assert(bd == 8);
  return AV1_VAR_OFFS;
}

void av1_init_rtc_counters(MACROBLOCK *const x) {
  av1_init_cyclic_refresh_counters(x);
  x->cnt_zeromv = 0;
}

void av1_accumulate_rtc_counters(AV1_COMP *cpi, const MACROBLOCK *const x) {
  if (cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ)
    av1_accumulate_cyclic_refresh_counters(cpi->cyclic_refresh, x);
  cpi->rc.cnt_zeromv += x->cnt_zeromv;
  cpi->rc.num_col_blscroll_last_tl0 += x->sb_col_scroll;
  cpi->rc.num_row_blscroll_last_tl0 += x->sb_row_scroll;
}

unsigned int av1_get_perpixel_variance(const AV1_COMP *cpi,
                                       const MACROBLOCKD *xd,
                                       const struct buf_2d *ref,
                                       BLOCK_SIZE bsize, int plane,
                                       int use_hbd) {
  const int subsampling_x = xd->plane[plane].subsampling_x;
  const int subsampling_y = xd->plane[plane].subsampling_y;
  const BLOCK_SIZE plane_bsize =
      get_plane_block_size(bsize, subsampling_x, subsampling_y);
  unsigned int sse;
  const unsigned int var = cpi->ppi->fn_ptr[plane_bsize].vf(
      ref->buf, ref->stride, get_var_offs(use_hbd, xd->bd), 0, &sse);
  return ROUND_POWER_OF_TWO(var, num_pels_log2_lookup[plane_bsize]);
}

unsigned int av1_get_perpixel_variance_facade(const AV1_COMP *cpi,
                                              const MACROBLOCKD *xd,
                                              const struct buf_2d *ref,
                                              BLOCK_SIZE bsize, int plane) {
  const int use_hbd = is_cur_buf_hbd(xd);
  return av1_get_perpixel_variance(cpi, xd, ref, bsize, plane, use_hbd);
}

void av1_setup_src_planes(MACROBLOCK *x, const YV12_BUFFER_CONFIG *src,
                          int mi_row, int mi_col, const int num_planes,
                          BLOCK_SIZE bsize) {
  // Set current frame pointer.
  x->e_mbd.cur_buf = src;

  // We use AOMMIN(num_planes, MAX_MB_PLANE) instead of num_planes to quiet
  // the static analysis warnings.
  for (int i = 0; i < AOMMIN(num_planes, MAX_MB_PLANE); i++) {
    const int is_uv = i > 0;
    setup_pred_plane(
        &x->plane[i].src, bsize, src->buffers[i], src->crop_widths[is_uv],
        src->crop_heights[is_uv], src->strides[is_uv], mi_row, mi_col, NULL,
        x->e_mbd.plane[i].subsampling_x, x->e_mbd.plane[i].subsampling_y);
  }
}

#if !CONFIG_REALTIME_ONLY
/*!\brief Assigns different quantization parameters to each super
 * block based on its TPL weight.
 *
 * \ingroup tpl_modelling
 *
 * \param[in]     cpi         Top level encoder instance structure
 * \param[in,out] td          Thread data structure
 * \param[in,out] x           Macro block level data for this block.
 * \param[in]     tile_info   Tile infromation / identification
 * \param[in]     mi_row      Block row (in "MI_SIZE" units) index
 * \param[in]     mi_col      Block column (in "MI_SIZE" units) index
 * \param[out]    num_planes  Number of image planes (e.g. Y,U,V)
 *
 * \remark No return value but updates macroblock and thread data
 * related to the q / q delta to be used.
 */
static inline void setup_delta_q(AV1_COMP *const cpi, ThreadData *td,
                                 MACROBLOCK *const x,
                                 const TileInfo *const tile_info, int mi_row,
                                 int mi_col, int num_planes) {
  AV1_COMMON *const cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const DeltaQInfo *const delta_q_info = &cm->delta_q_info;
  assert(delta_q_info->delta_q_present_flag);

  const BLOCK_SIZE sb_size = cm->seq_params->sb_size;
  // Delta-q modulation based on variance
  av1_setup_src_planes(x, cpi->source, mi_row, mi_col, num_planes, sb_size);

  const int delta_q_res = delta_q_info->delta_q_res;
  int current_qindex = cm->quant_params.base_qindex;
  if (cpi->use_ducky_encode && cpi->ducky_encode_info.frame_info.qp_mode ==
                                   DUCKY_ENCODE_FRAME_MODE_QINDEX) {
    const int sb_row = mi_row >> cm->seq_params->mib_size_log2;
    const int sb_col = mi_col >> cm->seq_params->mib_size_log2;
    const int sb_cols =
        CEIL_POWER_OF_TWO(cm->mi_params.mi_cols, cm->seq_params->mib_size_log2);
    const int sb_index = sb_row * sb_cols + sb_col;
    current_qindex =
        cpi->ducky_encode_info.frame_info.superblock_encode_qindex[sb_index];
  } else if (cpi->oxcf.q_cfg.deltaq_mode == DELTA_Q_PERCEPTUAL) {
    if (DELTA_Q_PERCEPTUAL_MODULATION == 1) {
      const int block_wavelet_energy_level =
          av1_block_wavelet_energy_level(cpi, x, sb_size);
      x->sb_energy_level = block_wavelet_energy_level;
      current_qindex = av1_compute_q_from_energy_level_deltaq_mode(
          cpi, block_wavelet_energy_level);
    } else {
      const int block_var_level = av1_log_block_var(cpi, x, sb_size);
      x->sb_energy_level = block_var_level;
      current_qindex =
          av1_compute_q_from_energy_level_deltaq_mode(cpi, block_var_level);
    }
  } else if (cpi->oxcf.q_cfg.deltaq_mode == DELTA_Q_OBJECTIVE &&
             cpi->oxcf.algo_cfg.enable_tpl_model) {
    // Setup deltaq based on tpl stats
    current_qindex =
        av1_get_q_for_deltaq_objective(cpi, td, NULL, sb_size, mi_row, mi_col);
  } else if (cpi->oxcf.q_cfg.deltaq_mode == DELTA_Q_PERCEPTUAL_AI) {
    current_qindex = av1_get_sbq_perceptual_ai(cpi, sb_size, mi_row, mi_col);
  } else if (cpi->oxcf.q_cfg.deltaq_mode == DELTA_Q_USER_RATING_BASED) {
    current_qindex = av1_get_sbq_user_rating_based(cpi, mi_row, mi_col);
  } else if (cpi->oxcf.q_cfg.enable_hdr_deltaq) {
    current_qindex = av1_get_q_for_hdr(cpi, x, sb_size, mi_row, mi_col);
  } else if (cpi->oxcf.q_cfg.deltaq_mode == DELTA_Q_VARIANCE_BOOST) {
    current_qindex = av1_get_sbq_variance_boost(cpi, x);
  }

  x->rdmult_cur_qindex = current_qindex;
  MACROBLOCKD *const xd = &x->e_mbd;
  const int adjusted_qindex = av1_adjust_q_from_delta_q_res(
      delta_q_res, xd->current_base_qindex, current_qindex);
  if (cpi->use_ducky_encode) {
    assert(adjusted_qindex == current_qindex);
  }
  current_qindex = adjusted_qindex;

  x->delta_qindex = current_qindex - cm->quant_params.base_qindex;
  x->rdmult_delta_qindex = x->delta_qindex;

  av1_set_offsets(cpi, tile_info, x, mi_row, mi_col, sb_size);
  xd->mi[0]->current_qindex = current_qindex;
  av1_init_plane_quantizers(cpi, x, xd->mi[0]->segment_id, 0);

  // keep track of any non-zero delta-q used
  td->deltaq_used |= (x->delta_qindex != 0);

  if (cpi->oxcf.tool_cfg.enable_deltalf_mode) {
    const int delta_lf_res = delta_q_info->delta_lf_res;
    const int lfmask = ~(delta_lf_res - 1);
    const int delta_lf_from_base =
        ((x->delta_qindex / 4 + delta_lf_res / 2) & lfmask);
    const int8_t delta_lf =
        (int8_t)clamp(delta_lf_from_base, -MAX_LOOP_FILTER, MAX_LOOP_FILTER);
    const int frame_lf_count =
        av1_num_planes(cm) > 1 ? FRAME_LF_COUNT : FRAME_LF_COUNT - 2;
    const int mib_size = cm->seq_params->mib_size;

    // pre-set the delta lf for loop filter. Note that this value is set
    // before mi is assigned for each block in current superblock
    for (int j = 0; j < AOMMIN(mib_size, mi_params->mi_rows - mi_row); j++) {
      for (int k = 0; k < AOMMIN(mib_size, mi_params->mi_cols - mi_col); k++) {
        const int grid_idx = get_mi_grid_idx(mi_params, mi_row + j, mi_col + k);
        mi_params->mi_alloc[grid_idx].delta_lf_from_base = delta_lf;
        for (int lf_id = 0; lf_id < frame_lf_count; ++lf_id) {
          mi_params->mi_alloc[grid_idx].delta_lf[lf_id] = delta_lf;
        }
      }
    }
  }
}

static void init_ref_frame_space(AV1_COMP *cpi, ThreadData *td, int mi_row,
                                 int mi_col) {
  const AV1_COMMON *cm = &cpi->common;
  const GF_GROUP *const gf_group = &cpi->ppi->gf_group;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  MACROBLOCK *x = &td->mb;
  const int frame_idx = cpi->gf_frame_index;
  TplParams *const tpl_data = &cpi->ppi->tpl_data;
  const uint8_t block_mis_log2 = tpl_data->tpl_stats_block_mis_log2;

  av1_zero(x->tpl_keep_ref_frame);

  if (!av1_tpl_stats_ready(tpl_data, frame_idx)) return;
  if (!is_frame_tpl_eligible(gf_group, cpi->gf_frame_index)) return;
  if (cpi->oxcf.q_cfg.aq_mode != NO_AQ) return;

  const int is_overlay =
      cpi->ppi->gf_group.update_type[frame_idx] == OVERLAY_UPDATE;
  if (is_overlay) {
    memset(x->tpl_keep_ref_frame, 1, sizeof(x->tpl_keep_ref_frame));
    return;
  }

  TplDepFrame *tpl_frame = &tpl_data->tpl_frame[frame_idx];
  TplDepStats *tpl_stats = tpl_frame->tpl_stats_ptr;
  const int tpl_stride = tpl_frame->stride;
  int64_t inter_cost[INTER_REFS_PER_FRAME] = { 0 };
  const int step = 1 << block_mis_log2;
  const BLOCK_SIZE sb_size = cm->seq_params->sb_size;

  const int mi_row_end =
      AOMMIN(mi_size_high[sb_size] + mi_row, mi_params->mi_rows);
  const int mi_cols_sr = av1_pixels_to_mi(cm->superres_upscaled_width);
  const int mi_col_sr =
      coded_to_superres_mi(mi_col, cm->superres_scale_denominator);
  const int mi_col_end_sr =
      AOMMIN(coded_to_superres_mi(mi_col + mi_size_wide[sb_size],
                                  cm->superres_scale_denominator),
             mi_cols_sr);
  const int row_step = step;
  const int col_step_sr =
      coded_to_superres_mi(step, cm->superres_scale_denominator);
  for (int row = mi_row; row < mi_row_end; row += row_step) {
    for (int col = mi_col_sr; col < mi_col_end_sr; col += col_step_sr) {
      const TplDepStats *this_stats =
          &tpl_stats[av1_tpl_ptr_pos(row, col, tpl_stride, block_mis_log2)];
      int64_t tpl_pred_error[INTER_REFS_PER_FRAME] = { 0 };
      // Find the winner ref frame idx for the current block
      int64_t best_inter_cost = this_stats->pred_error[0];
      int best_rf_idx = 0;
      for (int idx = 1; idx < INTER_REFS_PER_FRAME; ++idx) {
        if ((this_stats->pred_error[idx] < best_inter_cost) &&
            (this_stats->pred_error[idx] != 0)) {
          best_inter_cost = this_stats->pred_error[idx];
          best_rf_idx = idx;
        }
      }
      // tpl_pred_error is the pred_error reduction of best_ref w.r.t.
      // LAST_FRAME.
      tpl_pred_error[best_rf_idx] = this_stats->pred_error[best_rf_idx] -
                                    this_stats->pred_error[LAST_FRAME - 1];

      for (int rf_idx = 1; rf_idx < INTER_REFS_PER_FRAME; ++rf_idx)
        inter_cost[rf_idx] += tpl_pred_error[rf_idx];
    }
  }

  int rank_index[INTER_REFS_PER_FRAME - 1];
  for (int idx = 0; idx < INTER_REFS_PER_FRAME - 1; ++idx) {
    rank_index[idx] = idx + 1;
    for (int i = idx; i > 0; --i) {
      if (inter_cost[rank_index[i - 1]] > inter_cost[rank_index[i]]) {
        const int tmp = rank_index[i - 1];
        rank_index[i - 1] = rank_index[i];
        rank_index[i] = tmp;
      }
    }
  }

  x->tpl_keep_ref_frame[INTRA_FRAME] = 1;
  x->tpl_keep_ref_frame[LAST_FRAME] = 1;

  int cutoff_ref = 0;
  for (int idx = 0; idx < INTER_REFS_PER_FRAME - 1; ++idx) {
    x->tpl_keep_ref_frame[rank_index[idx] + LAST_FRAME] = 1;
    if (idx > 2) {
      if (!cutoff_ref) {
        // If the predictive coding gains are smaller than the previous more
        // relevant frame over certain amount, discard this frame and all the
        // frames afterwards.
        if (llabs(inter_cost[rank_index[idx]]) <
                llabs(inter_cost[rank_index[idx - 1]]) / 8 ||
            inter_cost[rank_index[idx]] == 0)
          cutoff_ref = 1;
      }

      if (cutoff_ref) x->tpl_keep_ref_frame[rank_index[idx] + LAST_FRAME] = 0;
    }
  }
}

static inline void adjust_rdmult_tpl_model(AV1_COMP *cpi, MACROBLOCK *x,
                                           int mi_row, int mi_col) {
  const BLOCK_SIZE sb_size = cpi->common.seq_params->sb_size;
  const int orig_rdmult = cpi->rd.RDMULT;

  assert(IMPLIES(cpi->ppi->gf_group.size > 0,
                 cpi->gf_frame_index < cpi->ppi->gf_group.size));
  const int gf_group_index = cpi->gf_frame_index;
  if (cpi->oxcf.algo_cfg.enable_tpl_model && cpi->oxcf.q_cfg.aq_mode == NO_AQ &&
      cpi->oxcf.q_cfg.deltaq_mode == NO_DELTA_Q && gf_group_index > 0 &&
      cpi->ppi->gf_group.update_type[gf_group_index] == ARF_UPDATE) {
    const int dr =
        av1_get_rdmult_delta(cpi, sb_size, mi_row, mi_col, orig_rdmult);
    x->rdmult = dr;
  }
}
#endif  // !CONFIG_REALTIME_ONLY

#if CONFIG_RT_ML_PARTITIONING
// Get a prediction(stored in x->est_pred) for the whole superblock.
static void get_estimated_pred(AV1_COMP *cpi, const TileInfo *const tile,
                               MACROBLOCK *x, int mi_row, int mi_col) {
  AV1_COMMON *const cm = &cpi->common;
  const int is_key_frame = frame_is_intra_only(cm);
  MACROBLOCKD *xd = &x->e_mbd;

  // TODO(kyslov) Extend to 128x128
  assert(cm->seq_params->sb_size == BLOCK_64X64);

  av1_set_offsets(cpi, tile, x, mi_row, mi_col, BLOCK_64X64);

  if (!is_key_frame) {
    MB_MODE_INFO *mi = xd->mi[0];
    const YV12_BUFFER_CONFIG *yv12 = get_ref_frame_yv12_buf(cm, LAST_FRAME);

    assert(yv12 != NULL);

    av1_setup_pre_planes(xd, 0, yv12, mi_row, mi_col,
                         get_ref_scale_factors(cm, LAST_FRAME), 1);
    mi->ref_frame[0] = LAST_FRAME;
    mi->ref_frame[1] = NONE;
    mi->bsize = BLOCK_64X64;
    mi->mv[0].as_int = 0;
    mi->interp_filters = av1_broadcast_interp_filter(BILINEAR);

    set_ref_ptrs(cm, xd, mi->ref_frame[0], mi->ref_frame[1]);

    xd->plane[0].dst.buf = x->est_pred;
    xd->plane[0].dst.stride = 64;
    av1_enc_build_inter_predictor_y(xd, mi_row, mi_col);
  } else {
#if CONFIG_AV1_HIGHBITDEPTH
    switch (xd->bd) {
      case 8: memset(x->est_pred, 128, 64 * 64 * sizeof(x->est_pred[0])); break;
      case 10:
        memset(x->est_pred, 128 * 4, 64 * 64 * sizeof(x->est_pred[0]));
        break;
      case 12:
        memset(x->est_pred, 128 * 16, 64 * 64 * sizeof(x->est_pred[0]));
        break;
    }
#else
    memset(x->est_pred, 128, 64 * 64 * sizeof(x->est_pred[0]));
#endif  // CONFIG_VP9_HIGHBITDEPTH
  }
}
#endif  // CONFIG_RT_ML_PARTITIONING

#define AVG_CDF_WEIGHT_LEFT 3
#define AVG_CDF_WEIGHT_TOP_RIGHT 1

/*!\brief Encode a superblock (minimal RD search involved)
 *
 * \ingroup partition_search
 * Encodes the superblock by a pre-determined partition pattern, only minor
 * rd-based searches are allowed to adjust the initial pattern. It is only used
 * by realtime encoding.
 */
static inline void encode_nonrd_sb(AV1_COMP *cpi, ThreadData *td,
                                   TileDataEnc *tile_data, TokenExtra **tp,
                                   const int mi_row, const int mi_col,
                                   const int seg_skip) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  const SPEED_FEATURES *const sf = &cpi->sf;
  const TileInfo *const tile_info = &tile_data->tile_info;
  MB_MODE_INFO **mi = cm->mi_params.mi_grid_base +
                      get_mi_grid_idx(&cm->mi_params, mi_row, mi_col);
  const BLOCK_SIZE sb_size = cm->seq_params->sb_size;
  PC_TREE *const pc_root = td->pc_root;

#if CONFIG_RT_ML_PARTITIONING
  if (sf->part_sf.partition_search_type == ML_BASED_PARTITION) {
    RD_STATS dummy_rdc;
    get_estimated_pred(cpi, tile_info, x, mi_row, mi_col);
    av1_nonrd_pick_partition(cpi, td, tile_data, tp, mi_row, mi_col,
                             BLOCK_64X64, &dummy_rdc, 1, INT64_MAX, pc_root);
    return;
  }
#endif
  // Set the partition
  if (sf->part_sf.partition_search_type == FIXED_PARTITION || seg_skip ||
      (sf->rt_sf.use_fast_fixed_part && x->sb_force_fixed_part == 1 &&
       (!frame_is_intra_only(cm) &&
        (!cpi->ppi->use_svc ||
         !cpi->svc.layer_context[cpi->svc.temporal_layer_id].is_key_frame)))) {
    // set a fixed-size partition
    av1_set_offsets(cpi, tile_info, x, mi_row, mi_col, sb_size);
    BLOCK_SIZE bsize_select = sf->part_sf.fixed_partition_size;
    if (sf->rt_sf.use_fast_fixed_part &&
        x->content_state_sb.source_sad_nonrd < kLowSad) {
      bsize_select = cm->seq_params->sb_size;
    }
    if (cpi->sf.rt_sf.skip_encoding_non_reference_slide_change &&
        cpi->rc.high_source_sad && cpi->ppi->rtc_ref.non_reference_frame) {
      bsize_select = cm->seq_params->sb_size;
      x->force_zeromv_skip_for_sb = 1;
    }
    const BLOCK_SIZE bsize = seg_skip ? sb_size : bsize_select;
    if (x->content_state_sb.source_sad_nonrd > kZeroSad)
      x->force_color_check_block_level = 1;
    av1_set_fixed_partitioning(cpi, tile_info, mi, mi_row, mi_col, bsize);
  } else if (sf->part_sf.partition_search_type == VAR_BASED_PARTITION) {
    // set a variance-based partition
    av1_set_offsets(cpi, tile_info, x, mi_row, mi_col, sb_size);
    av1_choose_var_based_partitioning(cpi, tile_info, td, x, mi_row, mi_col);
  }
  assert(sf->part_sf.partition_search_type == FIXED_PARTITION || seg_skip ||
         sf->part_sf.partition_search_type == VAR_BASED_PARTITION);
  set_cb_offsets(td->mb.cb_offset, 0, 0);

  // Initialize the flag to skip cdef to 1.
  if (sf->rt_sf.skip_cdef_sb) {
    const int block64_in_sb = (sb_size == BLOCK_128X128) ? 2 : 1;
    // If 128x128 block is used, we need to set the flag for all 4 64x64 sub
    // "blocks".
    for (int r = 0; r < block64_in_sb; ++r) {
      for (int c = 0; c < block64_in_sb; ++c) {
        const int idx_in_sb =
            r * MI_SIZE_64X64 * cm->mi_params.mi_stride + c * MI_SIZE_64X64;
        if (mi[idx_in_sb]) mi[idx_in_sb]->cdef_strength = 1;
      }
    }
  }

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, nonrd_use_partition_time);
#endif
  av1_nonrd_use_partition(cpi, td, tile_data, mi, tp, mi_row, mi_col, sb_size,
                          pc_root);
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, nonrd_use_partition_time);
#endif
}

// This function initializes the stats for encode_rd_sb.
static inline void init_encode_rd_sb(AV1_COMP *cpi, ThreadData *td,
                                     const TileDataEnc *tile_data,
                                     SIMPLE_MOTION_DATA_TREE *sms_root,
                                     RD_STATS *rd_cost, int mi_row, int mi_col,
                                     int gather_tpl_data) {
  const AV1_COMMON *cm = &cpi->common;
  const TileInfo *tile_info = &tile_data->tile_info;
  MACROBLOCK *x = &td->mb;

  const SPEED_FEATURES *sf = &cpi->sf;
  const int use_simple_motion_search =
      (sf->part_sf.simple_motion_search_split ||
       sf->part_sf.simple_motion_search_prune_rect ||
       sf->part_sf.simple_motion_search_early_term_none ||
       sf->part_sf.ml_early_term_after_part_split_level) &&
      !frame_is_intra_only(cm);
  if (use_simple_motion_search) {
    av1_init_simple_motion_search_mvs_for_sb(cpi, tile_info, x, sms_root,
                                             mi_row, mi_col);
  }

#if !CONFIG_REALTIME_ONLY
  if (!(has_no_stats_stage(cpi) && cpi->oxcf.mode == REALTIME &&
        cpi->oxcf.gf_cfg.lag_in_frames == 0)) {
    init_ref_frame_space(cpi, td, mi_row, mi_col);
    x->sb_energy_level = 0;
    x->part_search_info.cnn_output_valid = 0;
    if (gather_tpl_data) {
      if (cm->delta_q_info.delta_q_present_flag) {
        const int num_planes = av1_num_planes(cm);
        const BLOCK_SIZE sb_size = cm->seq_params->sb_size;
        setup_delta_q(cpi, td, x, tile_info, mi_row, mi_col, num_planes);
        av1_tpl_rdmult_setup_sb(cpi, x, sb_size, mi_row, mi_col);
      }

      // TODO(jingning): revisit this function.
      if (cpi->oxcf.algo_cfg.enable_tpl_model && (0)) {
        adjust_rdmult_tpl_model(cpi, x, mi_row, mi_col);
      }
    }
  }
#else
  (void)tile_info;
  (void)mi_row;
  (void)mi_col;
  (void)gather_tpl_data;
#endif

  x->reuse_inter_pred = false;
  x->txfm_search_params.mode_eval_type = DEFAULT_EVAL;
  reset_mb_rd_record(x->txfm_search_info.mb_rd_record);
  av1_zero(x->picked_ref_frames_mask);
  av1_invalid_rd_stats(rd_cost);
}

#if !CONFIG_REALTIME_ONLY
static void sb_qp_sweep_init_quantizers(AV1_COMP *cpi, ThreadData *td,
                                        const TileDataEnc *tile_data,
                                        SIMPLE_MOTION_DATA_TREE *sms_tree,
                                        RD_STATS *rd_cost, int mi_row,
                                        int mi_col, int delta_qp_ofs) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  const BLOCK_SIZE sb_size = cm->seq_params->sb_size;
  const TileInfo *tile_info = &tile_data->tile_info;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const DeltaQInfo *const delta_q_info = &cm->delta_q_info;
  assert(delta_q_info->delta_q_present_flag);
  const int delta_q_res = delta_q_info->delta_q_res;

  const SPEED_FEATURES *sf = &cpi->sf;
  const int use_simple_motion_search =
      (sf->part_sf.simple_motion_search_split ||
       sf->part_sf.simple_motion_search_prune_rect ||
       sf->part_sf.simple_motion_search_early_term_none ||
       sf->part_sf.ml_early_term_after_part_split_level) &&
      !frame_is_intra_only(cm);
  if (use_simple_motion_search) {
    av1_init_simple_motion_search_mvs_for_sb(cpi, tile_info, x, sms_tree,
                                             mi_row, mi_col);
  }

  int current_qindex = x->rdmult_cur_qindex + delta_qp_ofs;

  MACROBLOCKD *const xd = &x->e_mbd;
  current_qindex = av1_adjust_q_from_delta_q_res(
      delta_q_res, xd->current_base_qindex, current_qindex);

  x->delta_qindex = current_qindex - cm->quant_params.base_qindex;

  av1_set_offsets(cpi, tile_info, x, mi_row, mi_col, sb_size);
  xd->mi[0]->current_qindex = current_qindex;
  av1_init_plane_quantizers(cpi, x, xd->mi[0]->segment_id, 0);

  // keep track of any non-zero delta-q used
  td->deltaq_used |= (x->delta_qindex != 0);

  if (cpi->oxcf.tool_cfg.enable_deltalf_mode) {
    const int delta_lf_res = delta_q_info->delta_lf_res;
    const int lfmask = ~(delta_lf_res - 1);
    const int delta_lf_from_base =
        ((x->delta_qindex / 4 + delta_lf_res / 2) & lfmask);
    const int8_t delta_lf =
        (int8_t)clamp(delta_lf_from_base, -MAX_LOOP_FILTER, MAX_LOOP_FILTER);
    const int frame_lf_count =
        av1_num_planes(cm) > 1 ? FRAME_LF_COUNT : FRAME_LF_COUNT - 2;
    const int mib_size = cm->seq_params->mib_size;

    // pre-set the delta lf for loop filter. Note that this value is set
    // before mi is assigned for each block in current superblock
    for (int j = 0; j < AOMMIN(mib_size, mi_params->mi_rows - mi_row); j++) {
      for (int k = 0; k < AOMMIN(mib_size, mi_params->mi_cols - mi_col); k++) {
        const int grid_idx = get_mi_grid_idx(mi_params, mi_row + j, mi_col + k);
        mi_params->mi_alloc[grid_idx].delta_lf_from_base = delta_lf;
        for (int lf_id = 0; lf_id < frame_lf_count; ++lf_id) {
          mi_params->mi_alloc[grid_idx].delta_lf[lf_id] = delta_lf;
        }
      }
    }
  }

  x->reuse_inter_pred = false;
  x->txfm_search_params.mode_eval_type = DEFAULT_EVAL;
  reset_mb_rd_record(x->txfm_search_info.mb_rd_record);
  av1_zero(x->picked_ref_frames_mask);
  av1_invalid_rd_stats(rd_cost);
}

static int sb_qp_sweep(AV1_COMP *const cpi, ThreadData *td,
                       TileDataEnc *tile_data, TokenExtra **tp, int mi_row,
                       int mi_col, BLOCK_SIZE bsize,
                       SIMPLE_MOTION_DATA_TREE *sms_tree,
                       SB_FIRST_PASS_STATS *sb_org_stats) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  RD_STATS rdc_winner, cur_rdc;
  av1_invalid_rd_stats(&rdc_winner);

  int best_qindex = td->mb.rdmult_delta_qindex;
  const int start = cm->current_frame.frame_type == KEY_FRAME ? -20 : -12;
  const int end = cm->current_frame.frame_type == KEY_FRAME ? 20 : 12;
  const int step = cm->delta_q_info.delta_q_res;

  for (int sweep_qp_delta = start; sweep_qp_delta <= end;
       sweep_qp_delta += step) {
    sb_qp_sweep_init_quantizers(cpi, td, tile_data, sms_tree, &cur_rdc, mi_row,
                                mi_col, sweep_qp_delta);

    const int alloc_mi_idx = get_alloc_mi_idx(&cm->mi_params, mi_row, mi_col);
    const int backup_current_qindex =
        cm->mi_params.mi_alloc[alloc_mi_idx].current_qindex;

    av1_reset_mbmi(&cm->mi_params, bsize, mi_row, mi_col);
    av1_restore_sb_state(sb_org_stats, cpi, td, tile_data, mi_row, mi_col);
    cm->mi_params.mi_alloc[alloc_mi_idx].current_qindex = backup_current_qindex;

    td->pc_root = av1_alloc_pc_tree_node(bsize);
    if (!td->pc_root)
      aom_internal_error(x->e_mbd.error_info, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate PC_TREE");
    av1_rd_pick_partition(cpi, td, tile_data, tp, mi_row, mi_col, bsize,
                          &cur_rdc, cur_rdc, td->pc_root, sms_tree, NULL,
                          SB_DRY_PASS, NULL);

    if ((rdc_winner.rdcost > cur_rdc.rdcost) ||
        (abs(sweep_qp_delta) < abs(best_qindex - x->rdmult_delta_qindex) &&
         rdc_winner.rdcost == cur_rdc.rdcost)) {
      rdc_winner = cur_rdc;
      best_qindex = x->rdmult_delta_qindex + sweep_qp_delta;
    }
  }

  return best_qindex;
}
#endif  //! CONFIG_REALTIME_ONLY

/*!\brief Encode a superblock (RD-search-based)
 *
 * \ingroup partition_search
 * Conducts partition search for a superblock, based on rate-distortion costs,
 * from scratch or adjusting from a pre-calculated partition pattern.
 */
static inline void encode_rd_sb(AV1_COMP *cpi, ThreadData *td,
                                TileDataEnc *tile_data, TokenExtra **tp,
                                const int mi_row, const int mi_col,
                                const int seg_skip) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  const SPEED_FEATURES *const sf = &cpi->sf;
  const TileInfo *const tile_info = &tile_data->tile_info;
  MB_MODE_INFO **mi = cm->mi_params.mi_grid_base +
                      get_mi_grid_idx(&cm->mi_params, mi_row, mi_col);
  const BLOCK_SIZE sb_size = cm->seq_params->sb_size;
  const int num_planes = av1_num_planes(cm);
  int dummy_rate;
  int64_t dummy_dist;
  RD_STATS dummy_rdc;
  SIMPLE_MOTION_DATA_TREE *const sms_root = td->sms_root;

#if CONFIG_REALTIME_ONLY
  (void)seg_skip;
#endif  // CONFIG_REALTIME_ONLY

  init_encode_rd_sb(cpi, td, tile_data, sms_root, &dummy_rdc, mi_row, mi_col,
                    1);

  // Encode the superblock
  if (sf->part_sf.partition_search_type == VAR_BASED_PARTITION) {
    // partition search starting from a variance-based partition
    av1_set_offsets(cpi, tile_info, x, mi_row, mi_col, sb_size);
    av1_choose_var_based_partitioning(cpi, tile_info, td, x, mi_row, mi_col);

#if CONFIG_COLLECT_COMPONENT_TIMING
    start_timing(cpi, rd_use_partition_time);
#endif
    td->pc_root = av1_alloc_pc_tree_node(sb_size);
    if (!td->pc_root)
      aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate PC_TREE");
    av1_rd_use_partition(cpi, td, tile_data, mi, tp, mi_row, mi_col, sb_size,
                         &dummy_rate, &dummy_dist, 1, td->pc_root);
    av1_free_pc_tree_recursive(td->pc_root, num_planes, 0, 0,
                               sf->part_sf.partition_search_type);
    td->pc_root = NULL;
#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(cpi, rd_use_partition_time);
#endif
  }
#if !CONFIG_REALTIME_ONLY
  else if (sf->part_sf.partition_search_type == FIXED_PARTITION || seg_skip) {
    // partition search by adjusting a fixed-size partition
    av1_set_offsets(cpi, tile_info, x, mi_row, mi_col, sb_size);
    const BLOCK_SIZE bsize =
        seg_skip ? sb_size : sf->part_sf.fixed_partition_size;
    av1_set_fixed_partitioning(cpi, tile_info, mi, mi_row, mi_col, bsize);
    td->pc_root = av1_alloc_pc_tree_node(sb_size);
    if (!td->pc_root)
      aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate PC_TREE");
    av1_rd_use_partition(cpi, td, tile_data, mi, tp, mi_row, mi_col, sb_size,
                         &dummy_rate, &dummy_dist, 1, td->pc_root);
    av1_free_pc_tree_recursive(td->pc_root, num_planes, 0, 0,
                               sf->part_sf.partition_search_type);
    td->pc_root = NULL;
  } else {
    // The most exhaustive recursive partition search
    SuperBlockEnc *sb_enc = &x->sb_enc;
    // No stats for overlay frames. Exclude key frame.
    av1_get_tpl_stats_sb(cpi, sb_size, mi_row, mi_col, sb_enc);

    // Reset the tree for simple motion search data
    av1_reset_simple_motion_tree_partition(sms_root, sb_size);

#if CONFIG_COLLECT_COMPONENT_TIMING
    start_timing(cpi, rd_pick_partition_time);
#endif

    // Estimate the maximum square partition block size, which will be used
    // as the starting block size for partitioning the sb
    set_max_min_partition_size(sb_enc, cpi, x, sf, sb_size, mi_row, mi_col);

    // The superblock can be searched only once, or twice consecutively for
    // better quality. Note that the meaning of passes here is different from
    // the general concept of 1-pass/2-pass encoders.
    const int num_passes =
        cpi->oxcf.unit_test_cfg.sb_multipass_unit_test ? 2 : 1;

    if (cpi->oxcf.sb_qp_sweep &&
        !(has_no_stats_stage(cpi) && cpi->oxcf.mode == REALTIME &&
          cpi->oxcf.gf_cfg.lag_in_frames == 0) &&
        cm->delta_q_info.delta_q_present_flag) {
      AOM_CHECK_MEM_ERROR(
          x->e_mbd.error_info, td->mb.sb_stats_cache,
          (SB_FIRST_PASS_STATS *)aom_malloc(sizeof(*td->mb.sb_stats_cache)));
      av1_backup_sb_state(td->mb.sb_stats_cache, cpi, td, tile_data, mi_row,
                          mi_col);
      assert(x->rdmult_delta_qindex == x->delta_qindex);

      const int best_qp_diff =
          sb_qp_sweep(cpi, td, tile_data, tp, mi_row, mi_col, sb_size, sms_root,
                      td->mb.sb_stats_cache) -
          x->rdmult_delta_qindex;

      sb_qp_sweep_init_quantizers(cpi, td, tile_data, sms_root, &dummy_rdc,
                                  mi_row, mi_col, best_qp_diff);

      const int alloc_mi_idx = get_alloc_mi_idx(&cm->mi_params, mi_row, mi_col);
      const int backup_current_qindex =
          cm->mi_params.mi_alloc[alloc_mi_idx].current_qindex;

      av1_reset_mbmi(&cm->mi_params, sb_size, mi_row, mi_col);
      av1_restore_sb_state(td->mb.sb_stats_cache, cpi, td, tile_data, mi_row,
                           mi_col);

      cm->mi_params.mi_alloc[alloc_mi_idx].current_qindex =
          backup_current_qindex;
      aom_free(td->mb.sb_stats_cache);
      td->mb.sb_stats_cache = NULL;
    }
    if (num_passes == 1) {
#if CONFIG_PARTITION_SEARCH_ORDER
      if (cpi->ext_part_controller.ready && !frame_is_intra_only(cm)) {
        av1_reset_part_sf(&cpi->sf.part_sf);
        av1_reset_sf_for_ext_part(cpi);
        RD_STATS this_rdc;
        av1_rd_partition_search(cpi, td, tile_data, tp, sms_root, mi_row,
                                mi_col, sb_size, &this_rdc);
      } else {
        td->pc_root = av1_alloc_pc_tree_node(sb_size);
        if (!td->pc_root)
          aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                             "Failed to allocate PC_TREE");
        av1_rd_pick_partition(cpi, td, tile_data, tp, mi_row, mi_col, sb_size,
                              &dummy_rdc, dummy_rdc, td->pc_root, sms_root,
                              NULL, SB_SINGLE_PASS, NULL);
      }
#else
      td->pc_root = av1_alloc_pc_tree_node(sb_size);
      if (!td->pc_root)
        aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                           "Failed to allocate PC_TREE");
      av1_rd_pick_partition(cpi, td, tile_data, tp, mi_row, mi_col, sb_size,
                            &dummy_rdc, dummy_rdc, td->pc_root, sms_root, NULL,
                            SB_SINGLE_PASS, NULL);
#endif  // CONFIG_PARTITION_SEARCH_ORDER
    } else {
      // First pass
      AOM_CHECK_MEM_ERROR(
          x->e_mbd.error_info, td->mb.sb_fp_stats,
          (SB_FIRST_PASS_STATS *)aom_malloc(sizeof(*td->mb.sb_fp_stats)));
      av1_backup_sb_state(td->mb.sb_fp_stats, cpi, td, tile_data, mi_row,
                          mi_col);
      td->pc_root = av1_alloc_pc_tree_node(sb_size);
      if (!td->pc_root)
        aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                           "Failed to allocate PC_TREE");
      av1_rd_pick_partition(cpi, td, tile_data, tp, mi_row, mi_col, sb_size,
                            &dummy_rdc, dummy_rdc, td->pc_root, sms_root, NULL,
                            SB_DRY_PASS, NULL);

      // Second pass
      init_encode_rd_sb(cpi, td, tile_data, sms_root, &dummy_rdc, mi_row,
                        mi_col, 0);
      av1_reset_mbmi(&cm->mi_params, sb_size, mi_row, mi_col);
      av1_reset_simple_motion_tree_partition(sms_root, sb_size);

      av1_restore_sb_state(td->mb.sb_fp_stats, cpi, td, tile_data, mi_row,
                           mi_col);

      td->pc_root = av1_alloc_pc_tree_node(sb_size);
      if (!td->pc_root)
        aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                           "Failed to allocate PC_TREE");
      av1_rd_pick_partition(cpi, td, tile_data, tp, mi_row, mi_col, sb_size,
                            &dummy_rdc, dummy_rdc, td->pc_root, sms_root, NULL,
                            SB_WET_PASS, NULL);
      aom_free(td->mb.sb_fp_stats);
      td->mb.sb_fp_stats = NULL;
    }

    // Reset to 0 so that it wouldn't be used elsewhere mistakenly.
    sb_enc->tpl_data_count = 0;
#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(cpi, rd_pick_partition_time);
#endif
  }
#endif  // !CONFIG_REALTIME_ONLY

  // Update the inter rd model
  // TODO(angiebird): Let inter_mode_rd_model_estimation support multi-tile.
  if (cpi->sf.inter_sf.inter_mode_rd_model_estimation == 1 &&
      cm->tiles.cols == 1 && cm->tiles.rows == 1) {
    av1_inter_mode_data_fit(tile_data, x->rdmult);
  }
}

// Check if the cost update of symbols mode, coeff and dv are tile or off.
static inline int is_mode_coeff_dv_upd_freq_tile_or_off(
    const AV1_COMP *const cpi) {
  const INTER_MODE_SPEED_FEATURES *const inter_sf = &cpi->sf.inter_sf;

  return (inter_sf->coeff_cost_upd_level <= INTERNAL_COST_UPD_TILE &&
          inter_sf->mode_cost_upd_level <= INTERNAL_COST_UPD_TILE &&
          cpi->sf.intra_sf.dv_cost_upd_level <= INTERNAL_COST_UPD_TILE);
}

// When row-mt is enabled and cost update frequencies are set to off/tile,
// processing of current SB can start even before processing of top-right SB
// is finished. This function checks if it is sufficient to wait for top SB
// to finish processing before current SB starts processing.
static inline int delay_wait_for_top_right_sb(const AV1_COMP *const cpi) {
  const MODE mode = cpi->oxcf.mode;
  if (mode == GOOD) return 0;

  if (mode == ALLINTRA)
    return is_mode_coeff_dv_upd_freq_tile_or_off(cpi);
  else if (mode == REALTIME)
    return (is_mode_coeff_dv_upd_freq_tile_or_off(cpi) &&
            cpi->sf.inter_sf.mv_cost_upd_level <= INTERNAL_COST_UPD_TILE);
  else
    return 0;
}

/*!\brief Calculate source SAD at superblock level using 64x64 block source SAD
 *
 * \ingroup partition_search
 * \callgraph
 * \callergraph
 */
static inline uint64_t get_sb_source_sad(const AV1_COMP *cpi, int mi_row,
                                         int mi_col) {
  if (cpi->src_sad_blk_64x64 == NULL) return UINT64_MAX;

  const AV1_COMMON *const cm = &cpi->common;
  const int blk_64x64_in_mis = (cm->seq_params->sb_size == BLOCK_128X128)
                                   ? (cm->seq_params->mib_size >> 1)
                                   : cm->seq_params->mib_size;
  const int num_blk_64x64_cols =
      (cm->mi_params.mi_cols + blk_64x64_in_mis - 1) / blk_64x64_in_mis;
  const int num_blk_64x64_rows =
      (cm->mi_params.mi_rows + blk_64x64_in_mis - 1) / blk_64x64_in_mis;
  const int blk_64x64_col_index = mi_col / blk_64x64_in_mis;
  const int blk_64x64_row_index = mi_row / blk_64x64_in_mis;
  uint64_t curr_sb_sad = UINT64_MAX;
  // Avoid the border as sad_blk_64x64 may not be set for the border
  // in the scene detection.
  if ((blk_64x64_row_index >= num_blk_64x64_rows - 1) ||
      (blk_64x64_col_index >= num_blk_64x64_cols - 1)) {
    return curr_sb_sad;
  }
  const uint64_t *const src_sad_blk_64x64_data =
      &cpi->src_sad_blk_64x64[blk_64x64_col_index +
                              blk_64x64_row_index * num_blk_64x64_cols];
  if (cm->seq_params->sb_size == BLOCK_128X128) {
    // Calculate SB source SAD by accumulating source SAD of 64x64 blocks in the
    // superblock
    curr_sb_sad = src_sad_blk_64x64_data[0] + src_sad_blk_64x64_data[1] +
                  src_sad_blk_64x64_data[num_blk_64x64_cols] +
                  src_sad_blk_64x64_data[num_blk_64x64_cols + 1];
  } else if (cm->seq_params->sb_size == BLOCK_64X64) {
    curr_sb_sad = src_sad_blk_64x64_data[0];
  }
  return curr_sb_sad;
}

/*!\brief Determine whether grading content can be skipped based on sad stat
 *
 * \ingroup partition_search
 * \callgraph
 * \callergraph
 */
static inline bool is_calc_src_content_needed(AV1_COMP *cpi,
                                              MACROBLOCK *const x, int mi_row,
                                              int mi_col) {
  if (cpi->svc.spatial_layer_id < cpi->svc.number_spatial_layers - 1)
    return true;
  const uint64_t curr_sb_sad = get_sb_source_sad(cpi, mi_row, mi_col);
  if (curr_sb_sad == UINT64_MAX) return true;
  if (curr_sb_sad == 0) {
    x->content_state_sb.source_sad_nonrd = kZeroSad;
    return false;
  }
  AV1_COMMON *const cm = &cpi->common;
  bool do_calc_src_content = true;

  if (cpi->oxcf.speed < 9) return do_calc_src_content;

  // TODO(yunqing): Tune/validate the thresholds for 128x128 SB size.
  if (AOMMIN(cm->width, cm->height) < 360) {
    // Derive Average 64x64 block source SAD from SB source SAD
    const uint64_t avg_64x64_blk_sad =
        (cm->seq_params->sb_size == BLOCK_128X128) ? ((curr_sb_sad + 2) >> 2)
                                                   : curr_sb_sad;

    // The threshold is determined based on kLowSad and kHighSad threshold and
    // test results.
    uint64_t thresh_low = 15000;
    uint64_t thresh_high = 40000;

    if (cpi->sf.rt_sf.increase_source_sad_thresh) {
      thresh_low = thresh_low << 1;
      thresh_high = thresh_high << 1;
    }

    if (avg_64x64_blk_sad > thresh_low && avg_64x64_blk_sad < thresh_high) {
      do_calc_src_content = false;
      // Note: set x->content_state_sb.source_sad_rd as well if this is extended
      // to RTC rd path.
      x->content_state_sb.source_sad_nonrd = kMedSad;
    }
  }

  return do_calc_src_content;
}

/*!\brief Determine whether grading content is needed based on sf and frame stat
 *
 * \ingroup partition_search
 * \callgraph
 * \callergraph
 */
// TODO(any): consolidate sfs to make interface cleaner
static inline void grade_source_content_sb(AV1_COMP *cpi, MACROBLOCK *const x,
                                           TileDataEnc *tile_data, int mi_row,
                                           int mi_col) {
  AV1_COMMON *const cm = &cpi->common;
  if (cm->current_frame.frame_type == KEY_FRAME ||
      (cpi->ppi->use_svc &&
       cpi->svc.layer_context[cpi->svc.temporal_layer_id].is_key_frame)) {
    assert(x->content_state_sb.source_sad_nonrd == kMedSad);
    assert(x->content_state_sb.source_sad_rd == kMedSad);
    return;
  }
  bool calc_src_content = false;

  if (cpi->sf.rt_sf.source_metrics_sb_nonrd) {
    if (!cpi->sf.rt_sf.check_scene_detection || cpi->rc.frame_source_sad > 0) {
      calc_src_content = is_calc_src_content_needed(cpi, x, mi_row, mi_col);
    } else {
      x->content_state_sb.source_sad_nonrd = kZeroSad;
    }
  } else if ((cpi->sf.rt_sf.var_part_based_on_qidx >= 1) &&
             (cm->width * cm->height <= 352 * 288)) {
    if (cpi->rc.frame_source_sad > 0)
      calc_src_content = true;
    else
      x->content_state_sb.source_sad_rd = kZeroSad;
  }
  if (calc_src_content)
    av1_source_content_sb(cpi, x, tile_data, mi_row, mi_col);
}

/*!\brief Encode a superblock row by breaking it into superblocks
 *
 * \ingroup partition_search
 * \callgraph
 * \callergraph
 * Do partition and mode search for an sb row: one row of superblocks filling up
 * the width of the current tile.
 */
static inline void encode_sb_row(AV1_COMP *cpi, ThreadData *td,
                                 TileDataEnc *tile_data, int mi_row,
                                 TokenExtra **tp) {
  AV1_COMMON *const cm = &cpi->common;
  const TileInfo *const tile_info = &tile_data->tile_info;
  MultiThreadInfo *const mt_info = &cpi->mt_info;
  AV1EncRowMultiThreadInfo *const enc_row_mt = &mt_info->enc_row_mt;
  AV1EncRowMultiThreadSync *const row_mt_sync = &tile_data->row_mt_sync;
  bool row_mt_enabled = mt_info->row_mt_enabled;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  const int sb_cols_in_tile = av1_get_sb_cols_in_tile(cm, tile_info);
  const BLOCK_SIZE sb_size = cm->seq_params->sb_size;
  const int mib_size = cm->seq_params->mib_size;
  const int mib_size_log2 = cm->seq_params->mib_size_log2;
  const int sb_row = (mi_row - tile_info->mi_row_start) >> mib_size_log2;
  const int use_nonrd_mode = cpi->sf.rt_sf.use_nonrd_pick_mode;

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, encode_sb_row_time);
#endif

  // Initialize the left context for the new SB row
  av1_zero_left_context(xd);

  // Reset delta for quantizer and loof filters at the beginning of every tile
  if (mi_row == tile_info->mi_row_start || row_mt_enabled) {
    if (cm->delta_q_info.delta_q_present_flag)
      xd->current_base_qindex = cm->quant_params.base_qindex;
    if (cm->delta_q_info.delta_lf_present_flag) {
      av1_reset_loop_filter_delta(xd, av1_num_planes(cm));
    }
  }

  reset_thresh_freq_fact(x);

  // Code each SB in the row
  for (int mi_col = tile_info->mi_col_start, sb_col_in_tile = 0;
       mi_col < tile_info->mi_col_end; mi_col += mib_size, sb_col_in_tile++) {
    // In realtime/allintra mode and when frequency of cost updates is off/tile,
    // wait for the top superblock to finish encoding. Otherwise, wait for the
    // top-right superblock to finish encoding.
    enc_row_mt->sync_read_ptr(
        row_mt_sync, sb_row, sb_col_in_tile - delay_wait_for_top_right_sb(cpi));

#if CONFIG_MULTITHREAD
    if (row_mt_enabled) {
      pthread_mutex_lock(enc_row_mt->mutex_);
      const bool row_mt_exit = enc_row_mt->row_mt_exit;
      pthread_mutex_unlock(enc_row_mt->mutex_);
      // Exit in case any worker has encountered an error.
      if (row_mt_exit) return;
    }
#endif

    const int update_cdf = tile_data->allow_update_cdf && row_mt_enabled;
    if (update_cdf && (tile_info->mi_row_start != mi_row)) {
      if ((tile_info->mi_col_start == mi_col)) {
        // restore frame context at the 1st column sb
        memcpy(xd->tile_ctx, x->row_ctx, sizeof(*xd->tile_ctx));
      } else {
        // update context
        int wt_left = AVG_CDF_WEIGHT_LEFT;
        int wt_tr = AVG_CDF_WEIGHT_TOP_RIGHT;
        if (tile_info->mi_col_end > (mi_col + mib_size))
          av1_avg_cdf_symbols(xd->tile_ctx, x->row_ctx + sb_col_in_tile,
                              wt_left, wt_tr);
        else
          av1_avg_cdf_symbols(xd->tile_ctx, x->row_ctx + sb_col_in_tile - 1,
                              wt_left, wt_tr);
      }
    }

    // Update the rate cost tables for some symbols
    av1_set_cost_upd_freq(cpi, td, tile_info, mi_row, mi_col);

    // Reset color coding related parameters
    av1_zero(x->color_sensitivity_sb);
    av1_zero(x->color_sensitivity_sb_g);
    av1_zero(x->color_sensitivity_sb_alt);
    av1_zero(x->color_sensitivity);
    x->content_state_sb.source_sad_nonrd = kMedSad;
    x->content_state_sb.source_sad_rd = kMedSad;
    x->content_state_sb.lighting_change = 0;
    x->content_state_sb.low_sumdiff = 0;
    x->force_zeromv_skip_for_sb = 0;
    x->sb_me_block = 0;
    x->sb_me_partition = 0;
    x->sb_me_mv.as_int = 0;
    x->sb_col_scroll = 0;
    x->sb_row_scroll = 0;
    x->sb_force_fixed_part = 1;
    x->color_palette_thresh = 64;
    x->force_color_check_block_level = 0;
    x->nonrd_prune_ref_frame_search =
        cpi->sf.rt_sf.nonrd_prune_ref_frame_search;

    if (cpi->oxcf.mode == ALLINTRA) {
      x->intra_sb_rdmult_modifier = 128;
    }

    xd->cur_frame_force_integer_mv = cm->features.cur_frame_force_integer_mv;
    x->source_variance = UINT_MAX;
    td->mb.cb_coef_buff = av1_get_cb_coeff_buffer(cpi, mi_row, mi_col);

    // Get segment id and skip flag
    const struct segmentation *const seg = &cm->seg;
    int seg_skip = 0;
    if (seg->enabled) {
      const uint8_t *const map =
          seg->update_map ? cpi->enc_seg.map : cm->last_frame_seg_map;
      const uint8_t segment_id =
          map ? get_segment_id(&cm->mi_params, map, sb_size, mi_row, mi_col)
              : 0;
      seg_skip = segfeature_active(seg, segment_id, SEG_LVL_SKIP);
    }

    produce_gradients_for_sb(cpi, x, sb_size, mi_row, mi_col);

    init_src_var_info_of_4x4_sub_blocks(cpi, x->src_var_info_of_4x4_sub_blocks,
                                        sb_size);

    // Grade the temporal variation of the sb, the grade will be used to decide
    // fast mode search strategy for coding blocks
    if (!seg_skip) grade_source_content_sb(cpi, x, tile_data, mi_row, mi_col);

    // encode the superblock
    if (use_nonrd_mode) {
      encode_nonrd_sb(cpi, td, tile_data, tp, mi_row, mi_col, seg_skip);
    } else {
      encode_rd_sb(cpi, td, tile_data, tp, mi_row, mi_col, seg_skip);
    }

    // Update the top-right context in row_mt coding
    if (update_cdf && (tile_info->mi_row_end > (mi_row + mib_size))) {
      if (sb_cols_in_tile == 1)
        memcpy(x->row_ctx, xd->tile_ctx, sizeof(*xd->tile_ctx));
      else if (sb_col_in_tile >= 1)
        memcpy(x->row_ctx + sb_col_in_tile - 1, xd->tile_ctx,
               sizeof(*xd->tile_ctx));
    }
    enc_row_mt->sync_write_ptr(row_mt_sync, sb_row, sb_col_in_tile,
                               sb_cols_in_tile);
  }

#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, encode_sb_row_time);
#endif
}

static inline void init_encode_frame_mb_context(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCK *const x = &cpi->td.mb;
  MACROBLOCKD *const xd = &x->e_mbd;

  // Copy data over into macro block data structures.
  av1_setup_src_planes(x, cpi->source, 0, 0, num_planes,
                       cm->seq_params->sb_size);

  av1_setup_block_planes(xd, cm->seq_params->subsampling_x,
                         cm->seq_params->subsampling_y, num_planes);
}

void av1_alloc_tile_data(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  AV1EncRowMultiThreadInfo *const enc_row_mt = &cpi->mt_info.enc_row_mt;
  const int tile_cols = cm->tiles.cols;
  const int tile_rows = cm->tiles.rows;

  av1_row_mt_mem_dealloc(cpi);

  aom_free(cpi->tile_data);
  cpi->allocated_tiles = 0;
  enc_row_mt->allocated_tile_cols = 0;
  enc_row_mt->allocated_tile_rows = 0;

  CHECK_MEM_ERROR(
      cm, cpi->tile_data,
      aom_memalign(32, tile_cols * tile_rows * sizeof(*cpi->tile_data)));

  cpi->allocated_tiles = tile_cols * tile_rows;
  enc_row_mt->allocated_tile_cols = tile_cols;
  enc_row_mt->allocated_tile_rows = tile_rows;
  for (int tile_row = 0; tile_row < tile_rows; ++tile_row) {
    for (int tile_col = 0; tile_col < tile_cols; ++tile_col) {
      const int tile_index = tile_row * tile_cols + tile_col;
      TileDataEnc *const this_tile = &cpi->tile_data[tile_index];
      av1_zero(this_tile->row_mt_sync);
      this_tile->row_ctx = NULL;
    }
  }
}

void av1_init_tile_data(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  const int tile_cols = cm->tiles.cols;
  const int tile_rows = cm->tiles.rows;
  int tile_col, tile_row;
  TokenInfo *const token_info = &cpi->token_info;
  TokenExtra *pre_tok = token_info->tile_tok[0][0];
  TokenList *tplist = token_info->tplist[0][0];
  unsigned int tile_tok = 0;
  int tplist_count = 0;

  if (!is_stat_generation_stage(cpi) &&
      cm->features.allow_screen_content_tools) {
    // Number of tokens for which token info needs to be allocated.
    unsigned int tokens_required =
        get_token_alloc(cm->mi_params.mb_rows, cm->mi_params.mb_cols,
                        MAX_SB_SIZE_LOG2, num_planes);
    // Allocate/reallocate memory for token related info if the number of tokens
    // required is more than the number of tokens already allocated. This could
    // occur in case of the following:
    // 1) If the memory is not yet allocated
    // 2) If the frame dimensions have changed
    const bool realloc_tokens = tokens_required > token_info->tokens_allocated;
    if (realloc_tokens) {
      free_token_info(token_info);
      alloc_token_info(cm, token_info, tokens_required);
      pre_tok = token_info->tile_tok[0][0];
      tplist = token_info->tplist[0][0];
    }
  }

  for (tile_row = 0; tile_row < tile_rows; ++tile_row) {
    for (tile_col = 0; tile_col < tile_cols; ++tile_col) {
      TileDataEnc *const tile_data =
          &cpi->tile_data[tile_row * tile_cols + tile_col];
      TileInfo *const tile_info = &tile_data->tile_info;
      av1_tile_init(tile_info, cm, tile_row, tile_col);
      tile_data->firstpass_top_mv = kZeroMv;
      tile_data->abs_sum_level = 0;

      if (is_token_info_allocated(token_info)) {
        token_info->tile_tok[tile_row][tile_col] = pre_tok + tile_tok;
        pre_tok = token_info->tile_tok[tile_row][tile_col];
        tile_tok = allocated_tokens(
            tile_info, cm->seq_params->mib_size_log2 + MI_SIZE_LOG2,
            num_planes);
        token_info->tplist[tile_row][tile_col] = tplist + tplist_count;
        tplist = token_info->tplist[tile_row][tile_col];
        tplist_count = av1_get_sb_rows_in_tile(cm, tile_info);
      }
      tile_data->allow_update_cdf = !cm->tiles.large_scale;
      tile_data->allow_update_cdf = tile_data->allow_update_cdf &&
                                    !cm->features.disable_cdf_update &&
                                    !delay_wait_for_top_right_sb(cpi);
      tile_data->tctx = *cm->fc;
    }
  }
}

// Populate the start palette token info prior to encoding an SB row.
static inline void get_token_start(AV1_COMP *cpi, const TileInfo *tile_info,
                                   int tile_row, int tile_col, int mi_row,
                                   TokenExtra **tp) {
  const TokenInfo *token_info = &cpi->token_info;
  if (!is_token_info_allocated(token_info)) return;

  const AV1_COMMON *cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  TokenList *const tplist = cpi->token_info.tplist[tile_row][tile_col];
  const int sb_row_in_tile =
      (mi_row - tile_info->mi_row_start) >> cm->seq_params->mib_size_log2;

  get_start_tok(cpi, tile_row, tile_col, mi_row, tp,
                cm->seq_params->mib_size_log2 + MI_SIZE_LOG2, num_planes);
  assert(tplist != NULL);
  tplist[sb_row_in_tile].start = *tp;
}

// Populate the token count after encoding an SB row.
static inline void populate_token_count(AV1_COMP *cpi,
                                        const TileInfo *tile_info, int tile_row,
                                        int tile_col, int mi_row,
                                        TokenExtra *tok) {
  const TokenInfo *token_info = &cpi->token_info;
  if (!is_token_info_allocated(token_info)) return;

  const AV1_COMMON *cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  TokenList *const tplist = token_info->tplist[tile_row][tile_col];
  const int sb_row_in_tile =
      (mi_row - tile_info->mi_row_start) >> cm->seq_params->mib_size_log2;
  const int tile_mb_cols =
      (tile_info->mi_col_end - tile_info->mi_col_start + 2) >> 2;
  const int num_mb_rows_in_sb =
      ((1 << (cm->seq_params->mib_size_log2 + MI_SIZE_LOG2)) + 8) >> 4;
  tplist[sb_row_in_tile].count =
      (unsigned int)(tok - tplist[sb_row_in_tile].start);

  assert((unsigned int)(tok - tplist[sb_row_in_tile].start) <=
         get_token_alloc(num_mb_rows_in_sb, tile_mb_cols,
                         cm->seq_params->mib_size_log2 + MI_SIZE_LOG2,
                         num_planes));

  (void)num_planes;
  (void)tile_mb_cols;
  (void)num_mb_rows_in_sb;
}

/*!\brief Encode a superblock row
 *
 * \ingroup partition_search
 */
void av1_encode_sb_row(AV1_COMP *cpi, ThreadData *td, int tile_row,
                       int tile_col, int mi_row) {
  AV1_COMMON *const cm = &cpi->common;
  const int tile_cols = cm->tiles.cols;
  TileDataEnc *this_tile = &cpi->tile_data[tile_row * tile_cols + tile_col];
  const TileInfo *const tile_info = &this_tile->tile_info;
  TokenExtra *tok = NULL;

  get_token_start(cpi, tile_info, tile_row, tile_col, mi_row, &tok);

  encode_sb_row(cpi, td, this_tile, mi_row, &tok);

  populate_token_count(cpi, tile_info, tile_row, tile_col, mi_row, tok);
}

/*!\brief Encode a tile
 *
 * \ingroup partition_search
 */
void av1_encode_tile(AV1_COMP *cpi, ThreadData *td, int tile_row,
                     int tile_col) {
  AV1_COMMON *const cm = &cpi->common;
  TileDataEnc *const this_tile =
      &cpi->tile_data[tile_row * cm->tiles.cols + tile_col];
  const TileInfo *const tile_info = &this_tile->tile_info;

  if (!cpi->sf.rt_sf.use_nonrd_pick_mode) av1_inter_mode_data_init(this_tile);

  av1_zero_above_context(cm, &td->mb.e_mbd, tile_info->mi_col_start,
                         tile_info->mi_col_end, tile_row);
  av1_init_above_context(&cm->above_contexts, av1_num_planes(cm), tile_row,
                         &td->mb.e_mbd);

#if !CONFIG_REALTIME_ONLY
  if (cpi->oxcf.intra_mode_cfg.enable_cfl_intra)
    cfl_init(&td->mb.e_mbd.cfl, cm->seq_params);
#endif

  if (td->mb.txfm_search_info.mb_rd_record != NULL) {
    av1_crc32c_calculator_init(
        &td->mb.txfm_search_info.mb_rd_record->crc_calculator);
  }

  for (int mi_row = tile_info->mi_row_start; mi_row < tile_info->mi_row_end;
       mi_row += cm->seq_params->mib_size) {
    av1_encode_sb_row(cpi, td, tile_row, tile_col, mi_row);
  }
  this_tile->abs_sum_level = td->abs_sum_level;
}

/*!\brief Break one frame into tiles and encode the tiles
 *
 * \ingroup partition_search
 *
 * \param[in]    cpi    Top-level encoder structure
 */
static inline void encode_tiles(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  const int tile_cols = cm->tiles.cols;
  const int tile_rows = cm->tiles.rows;
  int tile_col, tile_row;

  MACROBLOCK *const mb = &cpi->td.mb;
  assert(IMPLIES(cpi->tile_data == NULL,
                 cpi->allocated_tiles < tile_cols * tile_rows));
  if (cpi->allocated_tiles < tile_cols * tile_rows) av1_alloc_tile_data(cpi);

  av1_init_tile_data(cpi);
  av1_alloc_mb_data(cpi, mb);

  for (tile_row = 0; tile_row < tile_rows; ++tile_row) {
    for (tile_col = 0; tile_col < tile_cols; ++tile_col) {
      TileDataEnc *const this_tile =
          &cpi->tile_data[tile_row * cm->tiles.cols + tile_col];
      cpi->td.intrabc_used = 0;
      cpi->td.deltaq_used = 0;
      cpi->td.abs_sum_level = 0;
      cpi->td.rd_counts.seg_tmp_pred_cost[0] = 0;
      cpi->td.rd_counts.seg_tmp_pred_cost[1] = 0;
      cpi->td.mb.e_mbd.tile_ctx = &this_tile->tctx;
      cpi->td.mb.tile_pb_ctx = &this_tile->tctx;
      av1_init_rtc_counters(&cpi->td.mb);
      cpi->td.mb.palette_pixels = 0;
      av1_encode_tile(cpi, &cpi->td, tile_row, tile_col);
      if (!frame_is_intra_only(&cpi->common))
        av1_accumulate_rtc_counters(cpi, &cpi->td.mb);
      cpi->palette_pixel_num += cpi->td.mb.palette_pixels;
      cpi->intrabc_used |= cpi->td.intrabc_used;
      cpi->deltaq_used |= cpi->td.deltaq_used;
    }
  }

  av1_dealloc_mb_data(mb, av1_num_planes(cm));
}

// Set the relative distance of a reference frame w.r.t. current frame
static inline void set_rel_frame_dist(
    const AV1_COMMON *const cm, RefFrameDistanceInfo *const ref_frame_dist_info,
    const int ref_frame_flags) {
  MV_REFERENCE_FRAME ref_frame;
  int min_past_dist = INT32_MAX, min_future_dist = INT32_MAX;
  ref_frame_dist_info->nearest_past_ref = NONE_FRAME;
  ref_frame_dist_info->nearest_future_ref = NONE_FRAME;
  for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ++ref_frame) {
    ref_frame_dist_info->ref_relative_dist[ref_frame - LAST_FRAME] = 0;
    if (ref_frame_flags & av1_ref_frame_flag_list[ref_frame]) {
      int dist = av1_encoder_get_relative_dist(
          cm->cur_frame->ref_display_order_hint[ref_frame - LAST_FRAME],
          cm->current_frame.display_order_hint);
      ref_frame_dist_info->ref_relative_dist[ref_frame - LAST_FRAME] = dist;
      // Get the nearest ref_frame in the past
      if (abs(dist) < min_past_dist && dist < 0) {
        ref_frame_dist_info->nearest_past_ref = ref_frame;
        min_past_dist = abs(dist);
      }
      // Get the nearest ref_frame in the future
      if (dist < min_future_dist && dist > 0) {
        ref_frame_dist_info->nearest_future_ref = ref_frame;
        min_future_dist = dist;
      }
    }
  }
}

static inline int refs_are_one_sided(const AV1_COMMON *cm) {
  assert(!frame_is_intra_only(cm));

  int one_sided_refs = 1;
  const int cur_display_order_hint = cm->current_frame.display_order_hint;
  for (int ref = LAST_FRAME; ref <= ALTREF_FRAME; ++ref) {
    const RefCntBuffer *const buf = get_ref_frame_buf(cm, ref);
    if (buf == NULL) continue;
    if (av1_encoder_get_relative_dist(buf->display_order_hint,
                                      cur_display_order_hint) > 0) {
      one_sided_refs = 0;  // bwd reference
      break;
    }
  }
  return one_sided_refs;
}

static inline void get_skip_mode_ref_offsets(const AV1_COMMON *cm,
                                             int ref_order_hint[2]) {
  const SkipModeInfo *const skip_mode_info = &cm->current_frame.skip_mode_info;
  ref_order_hint[0] = ref_order_hint[1] = 0;
  if (!skip_mode_info->skip_mode_allowed) return;

  const RefCntBuffer *const buf_0 =
      get_ref_frame_buf(cm, LAST_FRAME + skip_mode_info->ref_frame_idx_0);
  const RefCntBuffer *const buf_1 =
      get_ref_frame_buf(cm, LAST_FRAME + skip_mode_info->ref_frame_idx_1);
  assert(buf_0 != NULL && buf_1 != NULL);

  ref_order_hint[0] = buf_0->order_hint;
  ref_order_hint[1] = buf_1->order_hint;
}

static int check_skip_mode_enabled(AV1_COMP *const cpi) {
  AV1_COMMON *const cm = &cpi->common;

  av1_setup_skip_mode_allowed(cm);
  if (!cm->current_frame.skip_mode_info.skip_mode_allowed) return 0;

  // Turn off skip mode if the temporal distances of the reference pair to the
  // current frame are different by more than 1 frame.
  const int cur_offset = (int)cm->current_frame.order_hint;
  int ref_offset[2];
  get_skip_mode_ref_offsets(cm, ref_offset);
  const int cur_to_ref0 = get_relative_dist(&cm->seq_params->order_hint_info,
                                            cur_offset, ref_offset[0]);
  const int cur_to_ref1 = abs(get_relative_dist(
      &cm->seq_params->order_hint_info, cur_offset, ref_offset[1]));
  if (abs(cur_to_ref0 - cur_to_ref1) > 1) return 0;

  // High Latency: Turn off skip mode if all refs are fwd.
  if (cpi->all_one_sided_refs && cpi->oxcf.gf_cfg.lag_in_frames > 0) return 0;

  const int ref_frame[2] = {
    cm->current_frame.skip_mode_info.ref_frame_idx_0 + LAST_FRAME,
    cm->current_frame.skip_mode_info.ref_frame_idx_1 + LAST_FRAME
  };
  if (!(cpi->ref_frame_flags & av1_ref_frame_flag_list[ref_frame[0]]) ||
      !(cpi->ref_frame_flags & av1_ref_frame_flag_list[ref_frame[1]]))
    return 0;

  return 1;
}

static inline void set_default_interp_skip_flags(
    const AV1_COMMON *cm, InterpSearchFlags *interp_search_flags) {
  const int num_planes = av1_num_planes(cm);
  interp_search_flags->default_interp_skip_flags =
      (num_planes == 1) ? INTERP_SKIP_LUMA_EVAL_CHROMA
                        : INTERP_SKIP_LUMA_SKIP_CHROMA;
}

static inline void setup_prune_ref_frame_mask(AV1_COMP *cpi) {
  if ((!cpi->oxcf.ref_frm_cfg.enable_onesided_comp ||
       cpi->sf.inter_sf.disable_onesided_comp) &&
      cpi->all_one_sided_refs) {
    // Disable all compound references
    cpi->prune_ref_frame_mask = (1 << MODE_CTX_REF_FRAMES) - (1 << REF_FRAMES);
  } else if (!cpi->sf.rt_sf.use_nonrd_pick_mode &&
             cpi->sf.inter_sf.selective_ref_frame >= 2) {
    AV1_COMMON *const cm = &cpi->common;
    const int cur_frame_display_order_hint =
        cm->current_frame.display_order_hint;
    unsigned int *ref_display_order_hint =
        cm->cur_frame->ref_display_order_hint;
    const int arf2_dist = av1_encoder_get_relative_dist(
        ref_display_order_hint[ALTREF2_FRAME - LAST_FRAME],
        cur_frame_display_order_hint);
    const int bwd_dist = av1_encoder_get_relative_dist(
        ref_display_order_hint[BWDREF_FRAME - LAST_FRAME],
        cur_frame_display_order_hint);

    for (int ref_idx = REF_FRAMES; ref_idx < MODE_CTX_REF_FRAMES; ++ref_idx) {
      MV_REFERENCE_FRAME rf[2];
      av1_set_ref_frame(rf, ref_idx);
      if (!(cpi->ref_frame_flags & av1_ref_frame_flag_list[rf[0]]) ||
          !(cpi->ref_frame_flags & av1_ref_frame_flag_list[rf[1]])) {
        continue;
      }

      if (!cpi->all_one_sided_refs) {
        int ref_dist[2];
        for (int i = 0; i < 2; ++i) {
          ref_dist[i] = av1_encoder_get_relative_dist(
              ref_display_order_hint[rf[i] - LAST_FRAME],
              cur_frame_display_order_hint);
        }

        // One-sided compound is used only when all reference frames are
        // one-sided.
        if ((ref_dist[0] > 0) == (ref_dist[1] > 0)) {
          cpi->prune_ref_frame_mask |= 1 << ref_idx;
        }
      }

      if (cpi->sf.inter_sf.selective_ref_frame >= 4 &&
          (rf[0] == ALTREF2_FRAME || rf[1] == ALTREF2_FRAME) &&
          (cpi->ref_frame_flags & av1_ref_frame_flag_list[BWDREF_FRAME])) {
        // Check if both ALTREF2_FRAME and BWDREF_FRAME are future references.
        if (arf2_dist > 0 && bwd_dist > 0 && bwd_dist <= arf2_dist) {
          // Drop ALTREF2_FRAME as a reference if BWDREF_FRAME is a closer
          // reference to the current frame than ALTREF2_FRAME
          cpi->prune_ref_frame_mask |= 1 << ref_idx;
        }
      }
    }
  }
}

static int allow_deltaq_mode(AV1_COMP *cpi) {
#if !CONFIG_REALTIME_ONLY
  AV1_COMMON *const cm = &cpi->common;
  BLOCK_SIZE sb_size = cm->seq_params->sb_size;
  int sbs_wide = mi_size_wide[sb_size];
  int sbs_high = mi_size_high[sb_size];

  int64_t delta_rdcost = 0;
  for (int mi_row = 0; mi_row < cm->mi_params.mi_rows; mi_row += sbs_high) {
    for (int mi_col = 0; mi_col < cm->mi_params.mi_cols; mi_col += sbs_wide) {
      int64_t this_delta_rdcost = 0;
      av1_get_q_for_deltaq_objective(cpi, &cpi->td, &this_delta_rdcost, sb_size,
                                     mi_row, mi_col);
      delta_rdcost += this_delta_rdcost;
    }
  }
  return delta_rdcost < 0;
#else
  (void)cpi;
  return 1;
#endif  // !CONFIG_REALTIME_ONLY
}

#define FORCE_ZMV_SKIP_128X128_BLK_DIFF 10000
#define FORCE_ZMV_SKIP_MAX_PER_PIXEL_DIFF 4

// Populates block level thresholds for force zeromv-skip decision
static void populate_thresh_to_force_zeromv_skip(AV1_COMP *cpi) {
  if (cpi->sf.rt_sf.part_early_exit_zeromv == 0) return;

  // Threshold for forcing zeromv-skip decision is as below:
  // For 128x128 blocks, threshold is 10000 and per pixel threshold is 0.6103.
  // For 64x64 blocks, threshold is 5000 and per pixel threshold is 1.221
  // allowing slightly higher error for smaller blocks.
  // Per Pixel Threshold of 64x64 block        Area of 64x64 block         1  1
  // ------------------------------------=sqrt(---------------------)=sqrt(-)=-
  // Per Pixel Threshold of 128x128 block      Area of 128x128 block       4  2
  // Thus, per pixel thresholds for blocks of size 32x32, 16x16,...  can be
  // chosen as 2.442, 4.884,.... As the per pixel error tends to be higher for
  // small blocks, the same is clipped to 4.
  const unsigned int thresh_exit_128x128_part = FORCE_ZMV_SKIP_128X128_BLK_DIFF;
  const int num_128x128_pix =
      block_size_wide[BLOCK_128X128] * block_size_high[BLOCK_128X128];

  for (BLOCK_SIZE bsize = BLOCK_4X4; bsize < BLOCK_SIZES_ALL; bsize++) {
    const int num_block_pix = block_size_wide[bsize] * block_size_high[bsize];

    // Calculate the threshold for zeromv-skip decision based on area of the
    // partition
    unsigned int thresh_exit_part_blk =
        (unsigned int)(thresh_exit_128x128_part *
                           sqrt((double)num_block_pix / num_128x128_pix) +
                       0.5);
    thresh_exit_part_blk = AOMMIN(
        thresh_exit_part_blk,
        (unsigned int)(FORCE_ZMV_SKIP_MAX_PER_PIXEL_DIFF * num_block_pix));
    cpi->zeromv_skip_thresh_exit_part[bsize] = thresh_exit_part_blk;
  }
}

static void free_block_hash_buffers(uint32_t *block_hash_values[2][2],
                                    int8_t *is_block_same[2][3]) {
  for (int k = 0; k < 2; ++k) {
    for (int j = 0; j < 2; ++j) {
      aom_free(block_hash_values[k][j]);
    }

    for (int j = 0; j < 3; ++j) {
      aom_free(is_block_same[k][j]);
    }
  }
}

/*!\brief Determines delta_q_res value for Variance Boost modulation.
 */
static int aom_get_variance_boost_delta_q_res(int qindex) {
  // Signaling delta_q changes across superblocks comes with inherent syntax
  // element overhead, which adds up to total payload size. This overhead
  // becomes proportionally bigger the higher the base qindex (i.e. lower
  // quality, smaller file size), so a balance needs to be struck.
  // - Smaller delta_q_res: more granular delta_q control, more bits spent
  // signaling deltas.
  // - Larger delta_q_res: coarser delta_q control, less bits spent signaling
  // deltas.
  //
  // At the same time, SB qindex fluctuations become larger the higher
  // the base qindex (between lowest and highest-variance regions):
  // - For QP 5: up to 8 qindexes
  // - For QP 60: up to 52 qindexes
  //
  // With these factors in mind, it was found that the best strategy that
  // maximizes quality per bitrate is by having very finely-grained delta_q
  // values for the lowest picture qindexes (to preserve tiny qindex SB deltas),
  // and progressively making them coarser as base qindex increases (to reduce
  // total signaling overhead).
  int delta_q_res = 1;

  if (qindex >= 160) {
    delta_q_res = 8;
  } else if (qindex >= 120) {
    delta_q_res = 4;
  } else if (qindex >= 80) {
    delta_q_res = 2;
  } else {
    delta_q_res = 1;
  }

  return delta_q_res;
}

/*!\brief Encoder setup(only for the current frame), encoding, and recontruction
 * for a single frame
 *
 * \ingroup high_level_algo
 */
static inline void encode_frame_internal(AV1_COMP *cpi) {
  ThreadData *const td = &cpi->td;
  MACROBLOCK *const x = &td->mb;
  AV1_COMMON *const cm = &cpi->common;
  CommonModeInfoParams *const mi_params = &cm->mi_params;
  FeatureFlags *const features = &cm->features;
  MACROBLOCKD *const xd = &x->e_mbd;
  RD_COUNTS *const rdc = &cpi->td.rd_counts;
#if CONFIG_FPMT_TEST
  FrameProbInfo *const temp_frame_probs = &cpi->ppi->temp_frame_probs;
  FrameProbInfo *const temp_frame_probs_simulation =
      &cpi->ppi->temp_frame_probs_simulation;
#endif
  FrameProbInfo *const frame_probs = &cpi->ppi->frame_probs;
  IntraBCHashInfo *const intrabc_hash_info = &x->intrabc_hash_info;
  MultiThreadInfo *const mt_info = &cpi->mt_info;
  AV1EncRowMultiThreadInfo *const enc_row_mt = &mt_info->enc_row_mt;
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  const DELTAQ_MODE deltaq_mode = oxcf->q_cfg.deltaq_mode;
  int i;

  if (!cpi->sf.rt_sf.use_nonrd_pick_mode) {
    mi_params->setup_mi(mi_params);
  }

  set_mi_offsets(mi_params, xd, 0, 0);

  av1_zero(*td->counts);
  av1_zero(rdc->tx_type_used);
  av1_zero(rdc->obmc_used);
  av1_zero(rdc->warped_used);
  av1_zero(rdc->seg_tmp_pred_cost);

  // Reset the flag.
  cpi->intrabc_used = 0;
  // Need to disable intrabc when superres is selected
  if (av1_superres_scaled(cm)) {
    features->allow_intrabc = 0;
  }

  features->allow_intrabc &= (oxcf->kf_cfg.enable_intrabc);

  if (features->allow_warped_motion &&
      cpi->sf.inter_sf.prune_warped_prob_thresh > 0) {
    const FRAME_UPDATE_TYPE update_type =
        get_frame_update_type(&cpi->ppi->gf_group, cpi->gf_frame_index);
    int warped_probability =
#if CONFIG_FPMT_TEST
        cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE
            ? temp_frame_probs->warped_probs[update_type]
            :
#endif  // CONFIG_FPMT_TEST
            frame_probs->warped_probs[update_type];
    if (warped_probability < cpi->sf.inter_sf.prune_warped_prob_thresh)
      features->allow_warped_motion = 0;
  }

  int hash_table_created = 0;
  if (!is_stat_generation_stage(cpi) && av1_use_hash_me(cpi) &&
      !cpi->sf.rt_sf.use_nonrd_pick_mode) {
    // TODO(any): move this outside of the recoding loop to avoid recalculating
    // the hash table.
    // add to hash table
    const int pic_width = cpi->source->y_crop_width;
    const int pic_height = cpi->source->y_crop_height;
    uint32_t *block_hash_values[2][2] = { { NULL } };
    int8_t *is_block_same[2][3] = { { NULL } };
    int k, j;
    bool error = false;

    for (k = 0; k < 2 && !error; ++k) {
      for (j = 0; j < 2; ++j) {
        block_hash_values[k][j] = (uint32_t *)aom_malloc(
            sizeof(*block_hash_values[0][0]) * pic_width * pic_height);
        if (!block_hash_values[k][j]) {
          error = true;
          break;
        }
      }

      for (j = 0; j < 3 && !error; ++j) {
        is_block_same[k][j] = (int8_t *)aom_malloc(
            sizeof(*is_block_same[0][0]) * pic_width * pic_height);
        if (!is_block_same[k][j]) error = true;
      }
    }

    av1_hash_table_init(intrabc_hash_info);
    if (error ||
        !av1_hash_table_create(&intrabc_hash_info->intrabc_hash_table)) {
      free_block_hash_buffers(block_hash_values, is_block_same);
      aom_internal_error(cm->error, AOM_CODEC_MEM_ERROR,
                         "Error allocating intrabc_hash_table and buffers");
    }
    hash_table_created = 1;
    av1_generate_block_2x2_hash_value(intrabc_hash_info, cpi->source,
                                      block_hash_values[0], is_block_same[0]);
    // Hash data generated for screen contents is used for intraBC ME
    const int min_alloc_size = block_size_wide[mi_params->mi_alloc_bsize];
    const int max_sb_size =
        (1 << (cm->seq_params->mib_size_log2 + MI_SIZE_LOG2));
    int src_idx = 0;
    for (int size = 4; size <= max_sb_size; size *= 2, src_idx = !src_idx) {
      const int dst_idx = !src_idx;
      av1_generate_block_hash_value(
          intrabc_hash_info, cpi->source, size, block_hash_values[src_idx],
          block_hash_values[dst_idx], is_block_same[src_idx],
          is_block_same[dst_idx]);
      if (size >= min_alloc_size) {
        if (!av1_add_to_hash_map_by_row_with_precal_data(
                &intrabc_hash_info->intrabc_hash_table,
                block_hash_values[dst_idx], is_block_same[dst_idx][2],
                pic_width, pic_height, size)) {
          error = true;
          break;
        }
      }
    }

    free_block_hash_buffers(block_hash_values, is_block_same);

    if (error) {
      aom_internal_error(cm->error, AOM_CODEC_MEM_ERROR,
                         "Error adding data to intrabc_hash_table");
    }
  }

  const CommonQuantParams *quant_params = &cm->quant_params;
  for (i = 0; i < MAX_SEGMENTS; ++i) {
    const int qindex =
        cm->seg.enabled ? av1_get_qindex(&cm->seg, i, quant_params->base_qindex)
                        : quant_params->base_qindex;
    xd->lossless[i] =
        qindex == 0 && quant_params->y_dc_delta_q == 0 &&
        quant_params->u_dc_delta_q == 0 && quant_params->u_ac_delta_q == 0 &&
        quant_params->v_dc_delta_q == 0 && quant_params->v_ac_delta_q == 0;
    if (xd->lossless[i]) cpi->enc_seg.has_lossless_segment = 1;
    xd->qindex[i] = qindex;
    if (xd->lossless[i]) {
      cpi->optimize_seg_arr[i] = NO_TRELLIS_OPT;
    } else {
      cpi->optimize_seg_arr[i] = cpi->sf.rd_sf.optimize_coefficients;
    }
  }
  features->coded_lossless = is_coded_lossless(cm, xd);
  features->all_lossless = features->coded_lossless && !av1_superres_scaled(cm);

  // Fix delta q resolution for the moment

  cm->delta_q_info.delta_q_res = 0;
  if (cpi->use_ducky_encode) {
    cm->delta_q_info.delta_q_res = DEFAULT_DELTA_Q_RES_DUCKY_ENCODE;
  } else if (cpi->oxcf.q_cfg.aq_mode != CYCLIC_REFRESH_AQ) {
    if (deltaq_mode == DELTA_Q_OBJECTIVE)
      cm->delta_q_info.delta_q_res = DEFAULT_DELTA_Q_RES_OBJECTIVE;
    else if (deltaq_mode == DELTA_Q_PERCEPTUAL)
      cm->delta_q_info.delta_q_res = DEFAULT_DELTA_Q_RES_PERCEPTUAL;
    else if (deltaq_mode == DELTA_Q_PERCEPTUAL_AI)
      cm->delta_q_info.delta_q_res = DEFAULT_DELTA_Q_RES_PERCEPTUAL;
    else if (deltaq_mode == DELTA_Q_USER_RATING_BASED)
      cm->delta_q_info.delta_q_res = DEFAULT_DELTA_Q_RES_PERCEPTUAL;
    else if (deltaq_mode == DELTA_Q_HDR)
      cm->delta_q_info.delta_q_res = DEFAULT_DELTA_Q_RES_PERCEPTUAL;
    else if (deltaq_mode == DELTA_Q_VARIANCE_BOOST)
      cm->delta_q_info.delta_q_res =
          aom_get_variance_boost_delta_q_res(quant_params->base_qindex);
    // Set delta_q_present_flag before it is used for the first time
    cm->delta_q_info.delta_lf_res = DEFAULT_DELTA_LF_RES;
    cm->delta_q_info.delta_q_present_flag = deltaq_mode != NO_DELTA_Q;

    // Turn off cm->delta_q_info.delta_q_present_flag if objective delta_q
    // is used for ineligible frames. That effectively will turn off row_mt
    // usage. Note objective delta_q and tpl eligible frames are only altref
    // frames currently.
    const GF_GROUP *gf_group = &cpi->ppi->gf_group;
    if (cm->delta_q_info.delta_q_present_flag) {
      if (deltaq_mode == DELTA_Q_OBJECTIVE &&
          gf_group->update_type[cpi->gf_frame_index] == LF_UPDATE)
        cm->delta_q_info.delta_q_present_flag = 0;

      if (deltaq_mode == DELTA_Q_OBJECTIVE &&
          cm->delta_q_info.delta_q_present_flag) {
        cm->delta_q_info.delta_q_present_flag &= allow_deltaq_mode(cpi);
      }
    }

    // Reset delta_q_used flag
    cpi->deltaq_used = 0;

    cm->delta_q_info.delta_lf_present_flag =
        cm->delta_q_info.delta_q_present_flag &&
        oxcf->tool_cfg.enable_deltalf_mode;
    cm->delta_q_info.delta_lf_multi = DEFAULT_DELTA_LF_MULTI;

    // update delta_q_present_flag and delta_lf_present_flag based on
    // base_qindex
    cm->delta_q_info.delta_q_present_flag &= quant_params->base_qindex > 0;
    cm->delta_q_info.delta_lf_present_flag &= quant_params->base_qindex > 0;
  } else if (cpi->cyclic_refresh->apply_cyclic_refresh ||
             cpi->svc.number_temporal_layers == 1) {
    cpi->cyclic_refresh->actual_num_seg1_blocks = 0;
    cpi->cyclic_refresh->actual_num_seg2_blocks = 0;
  }
  cpi->rc.cnt_zeromv = 0;

  av1_frame_init_quantizer(cpi);
  init_encode_frame_mb_context(cpi);
  set_default_interp_skip_flags(cm, &cpi->interp_search_flags);

  if (cm->prev_frame && cm->prev_frame->seg.enabled &&
      cpi->svc.number_spatial_layers == 1)
    cm->last_frame_seg_map = cm->prev_frame->seg_map;
  else
    cm->last_frame_seg_map = NULL;
  if (features->allow_intrabc || features->coded_lossless) {
    av1_set_default_ref_deltas(cm->lf.ref_deltas);
    av1_set_default_mode_deltas(cm->lf.mode_deltas);
  } else if (cm->prev_frame) {
    memcpy(cm->lf.ref_deltas, cm->prev_frame->ref_deltas, REF_FRAMES);
    memcpy(cm->lf.mode_deltas, cm->prev_frame->mode_deltas, MAX_MODE_LF_DELTAS);
  }
  memcpy(cm->cur_frame->ref_deltas, cm->lf.ref_deltas, REF_FRAMES);
  memcpy(cm->cur_frame->mode_deltas, cm->lf.mode_deltas, MAX_MODE_LF_DELTAS);

  cpi->all_one_sided_refs =
      frame_is_intra_only(cm) ? 0 : refs_are_one_sided(cm);

  cpi->prune_ref_frame_mask = 0;
  // Figure out which ref frames can be skipped at frame level.
  setup_prune_ref_frame_mask(cpi);

  x->txfm_search_info.txb_split_count = 0;
#if CONFIG_SPEED_STATS
  x->txfm_search_info.tx_search_count = 0;
#endif  // CONFIG_SPEED_STATS

#if !CONFIG_REALTIME_ONLY
#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, av1_compute_global_motion_time);
#endif
  av1_compute_global_motion_facade(cpi);
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, av1_compute_global_motion_time);
#endif
#endif  // !CONFIG_REALTIME_ONLY

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, av1_setup_motion_field_time);
#endif
  av1_calculate_ref_frame_side(cm);
  if (features->allow_ref_frame_mvs) av1_setup_motion_field(cm);
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, av1_setup_motion_field_time);
#endif

  cm->current_frame.skip_mode_info.skip_mode_flag =
      check_skip_mode_enabled(cpi);

  // Initialization of skip mode cost depends on the value of
  // 'skip_mode_flag'. This initialization happens in the function
  // av1_fill_mode_rates(), which is in turn called in
  // av1_initialize_rd_consts(). Thus, av1_initialize_rd_consts()
  // has to be called after 'skip_mode_flag' is initialized.
  av1_initialize_rd_consts(cpi);
  av1_set_sad_per_bit(cpi, &x->sadperbit, quant_params->base_qindex);
  populate_thresh_to_force_zeromv_skip(cpi);

  enc_row_mt->sync_read_ptr = av1_row_mt_sync_read_dummy;
  enc_row_mt->sync_write_ptr = av1_row_mt_sync_write_dummy;
  mt_info->row_mt_enabled = 0;
  mt_info->pack_bs_mt_enabled = AOMMIN(mt_info->num_mod_workers[MOD_PACK_BS],
                                       cm->tiles.cols * cm->tiles.rows) > 1;

  if (oxcf->row_mt && (mt_info->num_workers > 1)) {
    mt_info->row_mt_enabled = 1;
    enc_row_mt->sync_read_ptr = av1_row_mt_sync_read;
    enc_row_mt->sync_write_ptr = av1_row_mt_sync_write;
    av1_encode_tiles_row_mt(cpi);
  } else {
    if (AOMMIN(mt_info->num_workers, cm->tiles.cols * cm->tiles.rows) > 1) {
      av1_encode_tiles_mt(cpi);
    } else {
      // Preallocate the pc_tree for realtime coding to reduce the cost of
      // memory allocation.
      const int use_nonrd_mode = cpi->sf.rt_sf.use_nonrd_pick_mode;
      if (use_nonrd_mode) {
        td->pc_root = av1_alloc_pc_tree_node(cm->seq_params->sb_size);
        if (!td->pc_root)
          aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                             "Failed to allocate PC_TREE");
      } else {
        td->pc_root = NULL;
      }

      encode_tiles(cpi);
      av1_free_pc_tree_recursive(td->pc_root, av1_num_planes(cm), 0, 0,
                                 cpi->sf.part_sf.partition_search_type);
      td->pc_root = NULL;
    }
  }

  // If intrabc is allowed but never selected, reset the allow_intrabc flag.
  if (features->allow_intrabc && !cpi->intrabc_used) {
    features->allow_intrabc = 0;
  }
  if (features->allow_intrabc) {
    cm->delta_q_info.delta_lf_present_flag = 0;
  }

  if (cm->delta_q_info.delta_q_present_flag && cpi->deltaq_used == 0) {
    cm->delta_q_info.delta_q_present_flag = 0;
  }

  // Set the transform size appropriately before bitstream creation
  const MODE_EVAL_TYPE eval_type =
      cpi->sf.winner_mode_sf.enable_winner_mode_for_tx_size_srch
          ? WINNER_MODE_EVAL
          : DEFAULT_EVAL;
  const TX_SIZE_SEARCH_METHOD tx_search_type =
      cpi->winner_mode_params.tx_size_search_methods[eval_type];
  assert(oxcf->txfm_cfg.enable_tx64 || tx_search_type != USE_LARGESTALL);
  features->tx_mode = select_tx_mode(cm, tx_search_type);

  // Retain the frame level probability update conditions for parallel frames.
  // These conditions will be consumed during postencode stage to update the
  // probability.
  if (cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0) {
    cpi->do_update_frame_probs_txtype[cpi->num_frame_recode] =
        cpi->sf.tx_sf.tx_type_search.prune_tx_type_using_stats;
    cpi->do_update_frame_probs_obmc[cpi->num_frame_recode] =
        (cpi->sf.inter_sf.prune_obmc_prob_thresh > 0 &&
         cpi->sf.inter_sf.prune_obmc_prob_thresh < INT_MAX);
    cpi->do_update_frame_probs_warp[cpi->num_frame_recode] =
        (features->allow_warped_motion &&
         cpi->sf.inter_sf.prune_warped_prob_thresh > 0);
    cpi->do_update_frame_probs_interpfilter[cpi->num_frame_recode] =
        (cm->current_frame.frame_type != KEY_FRAME &&
         cpi->sf.interp_sf.adaptive_interp_filter_search == 2 &&
         features->interp_filter == SWITCHABLE);
  }

  if (cpi->sf.tx_sf.tx_type_search.prune_tx_type_using_stats ||
      ((cpi->sf.tx_sf.tx_type_search.fast_inter_tx_type_prob_thresh !=
        INT_MAX) &&
       (cpi->sf.tx_sf.tx_type_search.fast_inter_tx_type_prob_thresh != 0))) {
    const FRAME_UPDATE_TYPE update_type =
        get_frame_update_type(&cpi->ppi->gf_group, cpi->gf_frame_index);
    for (i = 0; i < TX_SIZES_ALL; i++) {
      int sum = 0;
      int j;
      int left = MAX_TX_TYPE_PROB;

      for (j = 0; j < TX_TYPES; j++)
        sum += cpi->td.rd_counts.tx_type_used[i][j];

      for (j = TX_TYPES - 1; j >= 0; j--) {
        int update_txtype_frameprobs = 1;
        const int new_prob =
            sum ? (int)((int64_t)MAX_TX_TYPE_PROB *
                        cpi->td.rd_counts.tx_type_used[i][j] / sum)
                : (j ? 0 : MAX_TX_TYPE_PROB);
#if CONFIG_FPMT_TEST
        if (cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE) {
          if (cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] ==
              0) {
            int prob =
                (temp_frame_probs_simulation->tx_type_probs[update_type][i][j] +
                 new_prob) >>
                1;
            left -= prob;
            if (j == 0) prob += left;
            temp_frame_probs_simulation->tx_type_probs[update_type][i][j] =
                prob;
            // Copy temp_frame_probs_simulation to temp_frame_probs
            for (int update_type_idx = 0; update_type_idx < FRAME_UPDATE_TYPES;
                 update_type_idx++) {
              temp_frame_probs->tx_type_probs[update_type_idx][i][j] =
                  temp_frame_probs_simulation
                      ->tx_type_probs[update_type_idx][i][j];
            }
          }
          update_txtype_frameprobs = 0;
        }
#endif  // CONFIG_FPMT_TEST
        // Track the frame probabilities of parallel encode frames to update
        // during postencode stage.
        if (cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0) {
          update_txtype_frameprobs = 0;
          cpi->frame_new_probs[cpi->num_frame_recode]
              .tx_type_probs[update_type][i][j] = new_prob;
        }
        if (update_txtype_frameprobs) {
          int prob =
              (frame_probs->tx_type_probs[update_type][i][j] + new_prob) >> 1;
          left -= prob;
          if (j == 0) prob += left;
          frame_probs->tx_type_probs[update_type][i][j] = prob;
        }
      }
    }
  }

  if (cm->seg.enabled) {
    cm->seg.temporal_update = 1;
    if (rdc->seg_tmp_pred_cost[0] < rdc->seg_tmp_pred_cost[1])
      cm->seg.temporal_update = 0;
  }

  if (cpi->sf.inter_sf.prune_obmc_prob_thresh > 0 &&
      cpi->sf.inter_sf.prune_obmc_prob_thresh < INT_MAX) {
    const FRAME_UPDATE_TYPE update_type =
        get_frame_update_type(&cpi->ppi->gf_group, cpi->gf_frame_index);

    for (i = 0; i < BLOCK_SIZES_ALL; i++) {
      int sum = 0;
      int update_obmc_frameprobs = 1;
      for (int j = 0; j < 2; j++) sum += cpi->td.rd_counts.obmc_used[i][j];

      const int new_prob =
          sum ? 128 * cpi->td.rd_counts.obmc_used[i][1] / sum : 0;
#if CONFIG_FPMT_TEST
      if (cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE) {
        if (cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] == 0) {
          temp_frame_probs_simulation->obmc_probs[update_type][i] =
              (temp_frame_probs_simulation->obmc_probs[update_type][i] +
               new_prob) >>
              1;
          // Copy temp_frame_probs_simulation to temp_frame_probs
          for (int update_type_idx = 0; update_type_idx < FRAME_UPDATE_TYPES;
               update_type_idx++) {
            temp_frame_probs->obmc_probs[update_type_idx][i] =
                temp_frame_probs_simulation->obmc_probs[update_type_idx][i];
          }
        }
        update_obmc_frameprobs = 0;
      }
#endif  // CONFIG_FPMT_TEST
      // Track the frame probabilities of parallel encode frames to update
      // during postencode stage.
      if (cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0) {
        update_obmc_frameprobs = 0;
        cpi->frame_new_probs[cpi->num_frame_recode].obmc_probs[update_type][i] =
            new_prob;
      }
      if (update_obmc_frameprobs) {
        frame_probs->obmc_probs[update_type][i] =
            (frame_probs->obmc_probs[update_type][i] + new_prob) >> 1;
      }
    }
  }

  if (features->allow_warped_motion &&
      cpi->sf.inter_sf.prune_warped_prob_thresh > 0) {
    const FRAME_UPDATE_TYPE update_type =
        get_frame_update_type(&cpi->ppi->gf_group, cpi->gf_frame_index);
    int update_warp_frameprobs = 1;
    int sum = 0;
    for (i = 0; i < 2; i++) sum += cpi->td.rd_counts.warped_used[i];
    const int new_prob = sum ? 128 * cpi->td.rd_counts.warped_used[1] / sum : 0;
#if CONFIG_FPMT_TEST
    if (cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE) {
      if (cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] == 0) {
        temp_frame_probs_simulation->warped_probs[update_type] =
            (temp_frame_probs_simulation->warped_probs[update_type] +
             new_prob) >>
            1;
        // Copy temp_frame_probs_simulation to temp_frame_probs
        for (int update_type_idx = 0; update_type_idx < FRAME_UPDATE_TYPES;
             update_type_idx++) {
          temp_frame_probs->warped_probs[update_type_idx] =
              temp_frame_probs_simulation->warped_probs[update_type_idx];
        }
      }
      update_warp_frameprobs = 0;
    }
#endif  // CONFIG_FPMT_TEST
    // Track the frame probabilities of parallel encode frames to update
    // during postencode stage.
    if (cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0) {
      update_warp_frameprobs = 0;
      cpi->frame_new_probs[cpi->num_frame_recode].warped_probs[update_type] =
          new_prob;
    }
    if (update_warp_frameprobs) {
      frame_probs->warped_probs[update_type] =
          (frame_probs->warped_probs[update_type] + new_prob) >> 1;
    }
  }

  if (cm->current_frame.frame_type != KEY_FRAME &&
      cpi->sf.interp_sf.adaptive_interp_filter_search == 2 &&
      features->interp_filter == SWITCHABLE) {
    const FRAME_UPDATE_TYPE update_type =
        get_frame_update_type(&cpi->ppi->gf_group, cpi->gf_frame_index);

    for (i = 0; i < SWITCHABLE_FILTER_CONTEXTS; i++) {
      int sum = 0;
      int j;
      int left = 1536;

      for (j = 0; j < SWITCHABLE_FILTERS; j++) {
        sum += cpi->td.counts->switchable_interp[i][j];
      }

      for (j = SWITCHABLE_FILTERS - 1; j >= 0; j--) {
        int update_interpfilter_frameprobs = 1;
        const int new_prob =
            sum ? 1536 * cpi->td.counts->switchable_interp[i][j] / sum
                : (j ? 0 : 1536);
#if CONFIG_FPMT_TEST
        if (cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE) {
          if (cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] ==
              0) {
            int prob = (temp_frame_probs_simulation
                            ->switchable_interp_probs[update_type][i][j] +
                        new_prob) >>
                       1;
            left -= prob;
            if (j == 0) prob += left;
            temp_frame_probs_simulation
                ->switchable_interp_probs[update_type][i][j] = prob;
            // Copy temp_frame_probs_simulation to temp_frame_probs
            for (int update_type_idx = 0; update_type_idx < FRAME_UPDATE_TYPES;
                 update_type_idx++) {
              temp_frame_probs->switchable_interp_probs[update_type_idx][i][j] =
                  temp_frame_probs_simulation
                      ->switchable_interp_probs[update_type_idx][i][j];
            }
          }
          update_interpfilter_frameprobs = 0;
        }
#endif  // CONFIG_FPMT_TEST
        // Track the frame probabilities of parallel encode frames to update
        // during postencode stage.
        if (cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0) {
          update_interpfilter_frameprobs = 0;
          cpi->frame_new_probs[cpi->num_frame_recode]
              .switchable_interp_probs[update_type][i][j] = new_prob;
        }
        if (update_interpfilter_frameprobs) {
          int prob = (frame_probs->switchable_interp_probs[update_type][i][j] +
                      new_prob) >>
                     1;
          left -= prob;
          if (j == 0) prob += left;
          frame_probs->switchable_interp_probs[update_type][i][j] = prob;
        }
      }
    }
  }
  if (hash_table_created) {
    av1_hash_table_destroy(&intrabc_hash_info->intrabc_hash_table);
  }
}

/*!\brief Setup reference frame buffers and encode a frame
 *
 * \ingroup high_level_algo
 * \callgraph
 * \callergraph
 *
 * \param[in]    cpi    Top-level encoder structure
 */
void av1_encode_frame(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  CurrentFrame *const current_frame = &cm->current_frame;
  FeatureFlags *const features = &cm->features;
  RD_COUNTS *const rdc = &cpi->td.rd_counts;
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  // Indicates whether or not to use a default reduced set for ext-tx
  // rather than the potential full set of 16 transforms
  features->reduced_tx_set_used = oxcf->txfm_cfg.reduced_tx_type_set;

  // Make sure segment_id is no larger than last_active_segid.
  if (cm->seg.enabled && cm->seg.update_map) {
    const int mi_rows = cm->mi_params.mi_rows;
    const int mi_cols = cm->mi_params.mi_cols;
    const int last_active_segid = cm->seg.last_active_segid;
    uint8_t *map = cpi->enc_seg.map;
    for (int mi_row = 0; mi_row < mi_rows; ++mi_row) {
      for (int mi_col = 0; mi_col < mi_cols; ++mi_col) {
        map[mi_col] = AOMMIN(map[mi_col], last_active_segid);
      }
      map += mi_cols;
    }
  }

  av1_setup_frame_buf_refs(cm);
  enforce_max_ref_frames(cpi, &cpi->ref_frame_flags,
                         cm->cur_frame->ref_display_order_hint,
                         cm->current_frame.display_order_hint);
  set_rel_frame_dist(&cpi->common, &cpi->ref_frame_dist_info,
                     cpi->ref_frame_flags);
  av1_setup_frame_sign_bias(cm);

  // If global motion is enabled, then every buffer which is used as either
  // a source or a ref frame should have an image pyramid allocated.
  // Check here so that issues can be caught early in debug mode
#if !defined(NDEBUG) && !CONFIG_REALTIME_ONLY
  if (cpi->alloc_pyramid) {
    assert(cpi->source->y_pyramid);
    for (int ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ++ref_frame) {
      const RefCntBuffer *const buf = get_ref_frame_buf(cm, ref_frame);
      if (buf != NULL) {
        assert(buf->buf.y_pyramid);
      }
    }
  }
#endif  // !defined(NDEBUG) && !CONFIG_REALTIME_ONLY

#if CONFIG_MISMATCH_DEBUG
  mismatch_reset_frame(av1_num_planes(cm));
#endif

  rdc->newmv_or_intra_blocks = 0;
  cpi->palette_pixel_num = 0;

  if (cpi->sf.hl_sf.frame_parameter_update ||
      cpi->sf.rt_sf.use_comp_ref_nonrd) {
    if (frame_is_intra_only(cm))
      current_frame->reference_mode = SINGLE_REFERENCE;
    else
      current_frame->reference_mode = REFERENCE_MODE_SELECT;

    features->interp_filter = SWITCHABLE;
    if (cm->tiles.large_scale) features->interp_filter = EIGHTTAP_REGULAR;

    features->switchable_motion_mode = is_switchable_motion_mode_allowed(
        features->allow_warped_motion, oxcf->motion_mode_cfg.enable_obmc);

    rdc->compound_ref_used_flag = 0;
    rdc->skip_mode_used_flag = 0;

    encode_frame_internal(cpi);

    if (current_frame->reference_mode == REFERENCE_MODE_SELECT) {
      // Use a flag that includes 4x4 blocks
      if (rdc->compound_ref_used_flag == 0) {
        current_frame->reference_mode = SINGLE_REFERENCE;
#if CONFIG_ENTROPY_STATS
        av1_zero(cpi->td.counts->comp_inter);
#endif  // CONFIG_ENTROPY_STATS
      }
    }
    // Re-check on the skip mode status as reference mode may have been
    // changed.
    SkipModeInfo *const skip_mode_info = &current_frame->skip_mode_info;
    if (frame_is_intra_only(cm) ||
        current_frame->reference_mode == SINGLE_REFERENCE) {
      skip_mode_info->skip_mode_allowed = 0;
      skip_mode_info->skip_mode_flag = 0;
    }
    if (skip_mode_info->skip_mode_flag && rdc->skip_mode_used_flag == 0)
      skip_mode_info->skip_mode_flag = 0;

    if (!cm->tiles.large_scale) {
      if (features->tx_mode == TX_MODE_SELECT &&
          cpi->td.mb.txfm_search_info.txb_split_count == 0)
        features->tx_mode = TX_MODE_LARGEST;
    }
  } else {
    // This is needed if real-time speed setting is changed on the fly
    // from one using compound prediction to one using single reference.
    if (current_frame->reference_mode == REFERENCE_MODE_SELECT)
      current_frame->reference_mode = SINGLE_REFERENCE;
    encode_frame_internal(cpi);
  }
}
