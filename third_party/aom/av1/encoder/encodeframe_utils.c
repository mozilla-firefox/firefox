/*
 * Copyright (c) 2020, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include "av1/common/common_data.h"
#include "av1/common/quant_common.h"
#include "av1/common/reconintra.h"

#include "av1/encoder/encoder.h"
#include "av1/encoder/encodeframe_utils.h"
#include "av1/encoder/encoder_utils.h"
#include "av1/encoder/rdopt.h"

void av1_set_ssim_rdmult(const AV1_COMP *const cpi, int *errorperbit,
                         const BLOCK_SIZE bsize, const int mi_row,
                         const int mi_col, int *const rdmult) {
  const AV1_COMMON *const cm = &cpi->common;

  const BLOCK_SIZE bsize_base = BLOCK_16X16;
  const int num_mi_w = mi_size_wide[bsize_base];
  const int num_mi_h = mi_size_high[bsize_base];
  const int num_cols = (cm->mi_params.mi_cols + num_mi_w - 1) / num_mi_w;
  const int num_rows = (cm->mi_params.mi_rows + num_mi_h - 1) / num_mi_h;
  const int num_bcols = (mi_size_wide[bsize] + num_mi_w - 1) / num_mi_w;
  const int num_brows = (mi_size_high[bsize] + num_mi_h - 1) / num_mi_h;
  int row, col;
  double num_of_mi = 0.0;
  double geom_mean_of_scale = 1.0;

  // To avoid overflow of 'geom_mean_of_scale', bsize_base must be at least
  // BLOCK_8X8.
  //
  // For bsize=BLOCK_128X128 and bsize_base=BLOCK_8X8, the loop below would
  // iterate 256 times. Considering the maximum value of
  // cpi->ssim_rdmult_scaling_factors (see av1_set_mb_ssim_rdmult_scaling()),
  // geom_mean_of_scale can go up to 4.8323^256, which is within DBL_MAX
  // (maximum value a double data type can hold). If bsize_base is modified to
  // BLOCK_4X4 (minimum possible block size), geom_mean_of_scale can go up
  // to 4.8323^1024 and exceed DBL_MAX, resulting in data overflow.
  assert(bsize_base >= BLOCK_8X8);
  assert(cpi->oxcf.tune_cfg.tuning == AOM_TUNE_SSIM ||
         cpi->oxcf.tune_cfg.tuning == AOM_TUNE_IQ ||
         cpi->oxcf.tune_cfg.tuning == AOM_TUNE_SSIMULACRA2);

  for (row = mi_row / num_mi_w;
       row < num_rows && row < mi_row / num_mi_w + num_brows; ++row) {
    for (col = mi_col / num_mi_h;
         col < num_cols && col < mi_col / num_mi_h + num_bcols; ++col) {
      const int index = row * num_cols + col;
      assert(cpi->ssim_rdmult_scaling_factors[index] != 0.0);
      geom_mean_of_scale *= cpi->ssim_rdmult_scaling_factors[index];
      num_of_mi += 1.0;
    }
  }
  geom_mean_of_scale = pow(geom_mean_of_scale, (1.0 / num_of_mi));

  *rdmult = (int)((double)(*rdmult) * geom_mean_of_scale + 0.5);
  *rdmult = AOMMAX(*rdmult, 0);
  av1_set_error_per_bit(errorperbit, *rdmult);
}

#if CONFIG_SALIENCY_MAP
void av1_set_saliency_map_vmaf_rdmult(const AV1_COMP *const cpi,
                                      int *errorperbit, const BLOCK_SIZE bsize,
                                      const int mi_row, const int mi_col,
                                      int *const rdmult) {
  const AV1_COMMON *const cm = &cpi->common;
  const int num_mi_w = mi_size_wide[bsize];
  const int num_mi_h = mi_size_high[bsize];
  const int num_cols = (cm->mi_params.mi_cols + num_mi_w - 1) / num_mi_w;

  *rdmult =
      (int)(*rdmult * cpi->sm_scaling_factor[(mi_row / num_mi_h) * num_cols +
                                             (mi_col / num_mi_w)]);

  *rdmult = AOMMAX(*rdmult, 0);
  av1_set_error_per_bit(errorperbit, *rdmult);
}
#endif

// TODO(angiebird): Move this function to tpl_model.c
#if !CONFIG_REALTIME_ONLY
int av1_get_cb_rdmult(const AV1_COMP *const cpi, MACROBLOCK *const x,
                      const BLOCK_SIZE bsize, const int mi_row,
                      const int mi_col) {
  const AV1_COMMON *const cm = &cpi->common;
  assert(IMPLIES(cpi->ppi->gf_group.size > 0,
                 cpi->gf_frame_index < cpi->ppi->gf_group.size));
  const int tpl_idx = cpi->gf_frame_index;
  int deltaq_rdmult = set_rdmult(cpi, x, -1);
  if (!av1_tpl_stats_ready(&cpi->ppi->tpl_data, tpl_idx)) return deltaq_rdmult;
  if (cm->superres_scale_denominator != SCALE_NUMERATOR) return deltaq_rdmult;
  if (cpi->oxcf.q_cfg.aq_mode != NO_AQ) return deltaq_rdmult;
  if (x->rb == 0) return deltaq_rdmult;

  TplParams *const tpl_data = &cpi->ppi->tpl_data;
  TplDepFrame *tpl_frame = &tpl_data->tpl_frame[tpl_idx];
  TplDepStats *tpl_stats = tpl_frame->tpl_stats_ptr;

  const int mi_wide = mi_size_wide[bsize];
  const int mi_high = mi_size_high[bsize];

  int tpl_stride = tpl_frame->stride;
  double intra_cost_base = 0;
  double mc_dep_cost_base = 0;
  double cbcmp_base = 0;
  const int step = 1 << tpl_data->tpl_stats_block_mis_log2;

  for (int row = mi_row; row < mi_row + mi_high; row += step) {
    for (int col = mi_col; col < mi_col + mi_wide; col += step) {
      if (row >= cm->mi_params.mi_rows || col >= cm->mi_params.mi_cols)
        continue;

      TplDepStats *this_stats = &tpl_stats[av1_tpl_ptr_pos(
          row, col, tpl_stride, tpl_data->tpl_stats_block_mis_log2)];

      double cbcmp = (double)this_stats->srcrf_dist;
      int64_t mc_dep_delta =
          RDCOST(tpl_frame->base_rdmult, this_stats->mc_dep_rate,
                 this_stats->mc_dep_dist);
      double dist_scaled = (double)(this_stats->recrf_dist << RDDIV_BITS);
      intra_cost_base += log(dist_scaled) * cbcmp;
      mc_dep_cost_base += log(3 * dist_scaled + mc_dep_delta) * cbcmp;
      cbcmp_base += cbcmp;
    }
  }

  if (cbcmp_base == 0) return deltaq_rdmult;

  double rk = exp((intra_cost_base - mc_dep_cost_base) / cbcmp_base);
  deltaq_rdmult = (int)(deltaq_rdmult * (rk / x->rb));

  return AOMMAX(deltaq_rdmult, 1);
}
#endif  // !CONFIG_REALTIME_ONLY

static inline void update_filter_type_count(FRAME_COUNTS *counts,
                                            const MACROBLOCKD *xd,
                                            const MB_MODE_INFO *mbmi) {
  int dir;
  for (dir = 0; dir < 2; ++dir) {
    const int ctx = av1_get_pred_context_switchable_interp(xd, dir);
    InterpFilter filter = av1_extract_interp_filter(mbmi->interp_filters, dir);

    // Only allow the 3 valid SWITCHABLE_FILTERS.
    assert(filter < SWITCHABLE_FILTERS);
    ++counts->switchable_interp[ctx][filter];
  }
}

// This function will copy the best reference mode information from
// MB_MODE_INFO_EXT_FRAME to MB_MODE_INFO_EXT.
static inline void copy_mbmi_ext_frame_to_mbmi_ext(
    MB_MODE_INFO_EXT *mbmi_ext,
    const MB_MODE_INFO_EXT_FRAME *const mbmi_ext_best, uint8_t ref_frame_type) {
  memcpy(mbmi_ext->ref_mv_stack[ref_frame_type], mbmi_ext_best->ref_mv_stack,
         sizeof(mbmi_ext->ref_mv_stack[USABLE_REF_MV_STACK_SIZE]));
  memcpy(mbmi_ext->weight[ref_frame_type], mbmi_ext_best->weight,
         sizeof(mbmi_ext->weight[USABLE_REF_MV_STACK_SIZE]));
  mbmi_ext->mode_context[ref_frame_type] = mbmi_ext_best->mode_context;
  mbmi_ext->ref_mv_count[ref_frame_type] = mbmi_ext_best->ref_mv_count;
  memcpy(mbmi_ext->global_mvs, mbmi_ext_best->global_mvs,
         sizeof(mbmi_ext->global_mvs));
}

void av1_update_state(const AV1_COMP *const cpi, ThreadData *td,
                      const PICK_MODE_CONTEXT *const ctx, int mi_row,
                      int mi_col, BLOCK_SIZE bsize, RUN_TYPE dry_run) {
  int i, x_idx, y;
  const AV1_COMMON *const cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  struct macroblock_plane *const p = x->plane;
  struct macroblockd_plane *const pd = xd->plane;
  const MB_MODE_INFO *const mi = &ctx->mic;
  MB_MODE_INFO *const mi_addr = xd->mi[0];
  const struct segmentation *const seg = &cm->seg;
  assert(bsize < BLOCK_SIZES_ALL);
  const int bw = mi_size_wide[mi->bsize];
  const int bh = mi_size_high[mi->bsize];
  const int mis = mi_params->mi_stride;
  const int mi_width = mi_size_wide[bsize];
  const int mi_height = mi_size_high[bsize];
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;

  assert(mi->bsize == bsize);

  *mi_addr = *mi;
  copy_mbmi_ext_frame_to_mbmi_ext(&x->mbmi_ext, &ctx->mbmi_ext_best,
                                  av1_ref_frame_type(ctx->mic.ref_frame));

  memcpy(txfm_info->blk_skip, ctx->blk_skip,
         sizeof(txfm_info->blk_skip[0]) * ctx->num_4x4_blk);

  txfm_info->skip_txfm = ctx->rd_stats.skip_txfm;

  xd->tx_type_map = ctx->tx_type_map;
  xd->tx_type_map_stride = mi_size_wide[bsize];
  // If not dry_run, copy the transform type data into the frame level buffer.
  // Encoder will fetch tx types when writing bitstream.
  if (!dry_run) {
    const int grid_idx = get_mi_grid_idx(mi_params, mi_row, mi_col);
    uint8_t *const tx_type_map = mi_params->tx_type_map + grid_idx;
    const int mi_stride = mi_params->mi_stride;
    for (int blk_row = 0; blk_row < bh; ++blk_row) {
      av1_copy_array(tx_type_map + blk_row * mi_stride,
                     xd->tx_type_map + blk_row * xd->tx_type_map_stride, bw);
    }
    xd->tx_type_map = tx_type_map;
    xd->tx_type_map_stride = mi_stride;
  }

  // If segmentation in use
  if (seg->enabled) {
    // For in frame complexity AQ copy the segment id from the segment map.
    if (cpi->oxcf.q_cfg.aq_mode == COMPLEXITY_AQ) {
      const uint8_t *const map =
          seg->update_map ? cpi->enc_seg.map : cm->last_frame_seg_map;
      mi_addr->segment_id =
          map ? get_segment_id(mi_params, map, bsize, mi_row, mi_col) : 0;
    }
    // Else for cyclic refresh mode update the segment map, set the segment id
    // and then update the quantizer.
    if (cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ &&
        mi_addr->segment_id != AM_SEGMENT_ID_INACTIVE &&
        !cpi->rc.rtc_external_ratectrl) {
      av1_cyclic_refresh_update_segment(cpi, x, mi_row, mi_col, bsize,
                                        ctx->rd_stats.rate, ctx->rd_stats.dist,
                                        txfm_info->skip_txfm, dry_run);
    }
    if (mi_addr->uv_mode == UV_CFL_PRED && !is_cfl_allowed(xd))
      mi_addr->uv_mode = UV_DC_PRED;

    if (!dry_run && !mi_addr->skip_txfm) {
      int cdf_num;
      const uint8_t spatial_pred = av1_get_spatial_seg_pred(
          cm, xd, &cdf_num, cpi->cyclic_refresh->skip_over4x4);
      const uint8_t coded_id = av1_neg_interleave(
          mi_addr->segment_id, spatial_pred, seg->last_active_segid + 1);
      int64_t spatial_cost = x->mode_costs.spatial_pred_cost[cdf_num][coded_id];
      td->rd_counts.seg_tmp_pred_cost[0] += spatial_cost;

      const int pred_segment_id =
          cm->last_frame_seg_map
              ? get_segment_id(mi_params, cm->last_frame_seg_map, bsize, mi_row,
                               mi_col)
              : 0;
      const int use_tmp_pred = pred_segment_id == mi_addr->segment_id;
      const uint8_t tmp_pred_ctx = av1_get_pred_context_seg_id(xd);
      td->rd_counts.seg_tmp_pred_cost[1] +=
          x->mode_costs.tmp_pred_cost[tmp_pred_ctx][use_tmp_pred];
      if (!use_tmp_pred) {
        td->rd_counts.seg_tmp_pred_cost[1] += spatial_cost;
      }
    }
  }

  // Count zero motion vector.
  if (!dry_run && !frame_is_intra_only(cm)) {
    const MV mv = mi->mv[0].as_mv;
    if (is_inter_block(mi) && mi->ref_frame[0] == LAST_FRAME &&
        abs(mv.row) < 8 && abs(mv.col) < 8) {
      const int ymis = AOMMIN(cm->mi_params.mi_rows - mi_row, bh);
      // Accumulate low_content_frame.
      for (int mi_y = 0; mi_y < ymis; mi_y += 2) x->cnt_zeromv += bw << 1;
    }
  }

  for (i = 0; i < num_planes; ++i) {
    p[i].coeff = ctx->coeff[i];
    p[i].qcoeff = ctx->qcoeff[i];
    p[i].dqcoeff = ctx->dqcoeff[i];
    p[i].eobs = ctx->eobs[i];
    p[i].txb_entropy_ctx = ctx->txb_entropy_ctx[i];
  }
  for (i = 0; i < 2; ++i) pd[i].color_index_map = ctx->color_index_map[i];
  // Restore the coding context of the MB to that that was in place
  // when the mode was picked for it

  const int cols =
      AOMMIN((xd->mb_to_right_edge >> (3 + MI_SIZE_LOG2)) + mi_width, mi_width);
  const int rows = AOMMIN(
      (xd->mb_to_bottom_edge >> (3 + MI_SIZE_LOG2)) + mi_height, mi_height);
  for (y = 0; y < rows; y++) {
    for (x_idx = 0; x_idx < cols; x_idx++) xd->mi[x_idx + y * mis] = mi_addr;
  }

  if (cpi->oxcf.q_cfg.aq_mode)
    av1_init_plane_quantizers(cpi, x, mi_addr->segment_id, 0);

  if (dry_run) return;

#if CONFIG_INTERNAL_STATS
  {
    unsigned int *const mode_chosen_counts =
        (unsigned int *)cpi->mode_chosen_counts;  // Cast const away.
    if (frame_is_intra_only(cm)) {
      static const int kf_mode_index[] = {
        THR_DC /*DC_PRED*/,
        THR_V_PRED /*V_PRED*/,
        THR_H_PRED /*H_PRED*/,
        THR_D45_PRED /*D45_PRED*/,
        THR_D135_PRED /*D135_PRED*/,
        THR_D113_PRED /*D113_PRED*/,
        THR_D157_PRED /*D157_PRED*/,
        THR_D203_PRED /*D203_PRED*/,
        THR_D67_PRED /*D67_PRED*/,
        THR_SMOOTH,   /*SMOOTH_PRED*/
        THR_SMOOTH_V, /*SMOOTH_V_PRED*/
        THR_SMOOTH_H, /*SMOOTH_H_PRED*/
        THR_PAETH /*PAETH_PRED*/,
      };
      ++mode_chosen_counts[kf_mode_index[mi_addr->mode]];
    } else {
      // Note how often each mode chosen as best
      ++mode_chosen_counts[ctx->best_mode_index];
    }
  }
#endif
  if (!frame_is_intra_only(cm)) {
    if (is_inter_block(mi) && cm->features.interp_filter == SWITCHABLE) {
      // When the frame interp filter is SWITCHABLE, several cases that always
      // use the default type (EIGHTTAP_REGULAR) are described in
      // av1_is_interp_needed(). Here, we should keep the counts for all
      // applicable blocks, so the frame filter resetting decision in
      // fix_interp_filter() is made correctly.
      update_filter_type_count(td->counts, xd, mi_addr);
    }
  }

  const int x_mis = AOMMIN(bw, mi_params->mi_cols - mi_col);
  const int y_mis = AOMMIN(bh, mi_params->mi_rows - mi_row);
  if (cm->seq_params->order_hint_info.enable_ref_frame_mvs)
    av1_copy_frame_mvs(cm, mi, mi_row, mi_col, x_mis, y_mis);
}

void av1_update_inter_mode_stats(FRAME_CONTEXT *fc, FRAME_COUNTS *counts,
                                 PREDICTION_MODE mode, int16_t mode_context) {
  (void)counts;

  int16_t mode_ctx = mode_context & NEWMV_CTX_MASK;
  if (mode == NEWMV) {
#if CONFIG_ENTROPY_STATS
    ++counts->newmv_mode[mode_ctx][0];
#endif
    update_cdf(fc->newmv_cdf[mode_ctx], 0, 2);
    return;
  }

#if CONFIG_ENTROPY_STATS
  ++counts->newmv_mode[mode_ctx][1];
#endif
  update_cdf(fc->newmv_cdf[mode_ctx], 1, 2);

  mode_ctx = (mode_context >> GLOBALMV_OFFSET) & GLOBALMV_CTX_MASK;
  if (mode == GLOBALMV) {
#if CONFIG_ENTROPY_STATS
    ++counts->zeromv_mode[mode_ctx][0];
#endif
    update_cdf(fc->zeromv_cdf[mode_ctx], 0, 2);
    return;
  }

#if CONFIG_ENTROPY_STATS
  ++counts->zeromv_mode[mode_ctx][1];
#endif
  update_cdf(fc->zeromv_cdf[mode_ctx], 1, 2);

  mode_ctx = (mode_context >> REFMV_OFFSET) & REFMV_CTX_MASK;
#if CONFIG_ENTROPY_STATS
  ++counts->refmv_mode[mode_ctx][mode != NEARESTMV];
#endif
  update_cdf(fc->refmv_cdf[mode_ctx], mode != NEARESTMV, 2);
}

static void update_palette_cdf(MACROBLOCKD *xd, const MB_MODE_INFO *const mbmi,
                               FRAME_COUNTS *counts) {
  FRAME_CONTEXT *fc = xd->tile_ctx;
  const BLOCK_SIZE bsize = mbmi->bsize;
  const PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
  const int palette_bsize_ctx = av1_get_palette_bsize_ctx(bsize);

  (void)counts;

  if (mbmi->mode == DC_PRED) {
    const int n = pmi->palette_size[0];
    const int palette_mode_ctx = av1_get_palette_mode_ctx(xd);

#if CONFIG_ENTROPY_STATS
    ++counts->palette_y_mode[palette_bsize_ctx][palette_mode_ctx][n > 0];
#endif
    update_cdf(fc->palette_y_mode_cdf[palette_bsize_ctx][palette_mode_ctx],
               n > 0, 2);
    if (n > 0) {
#if CONFIG_ENTROPY_STATS
      ++counts->palette_y_size[palette_bsize_ctx][n - PALETTE_MIN_SIZE];
#endif
      update_cdf(fc->palette_y_size_cdf[palette_bsize_ctx],
                 n - PALETTE_MIN_SIZE, PALETTE_SIZES);
    }
  }

  if (mbmi->uv_mode == UV_DC_PRED) {
    const int n = pmi->palette_size[1];
    const int palette_uv_mode_ctx = (pmi->palette_size[0] > 0);

#if CONFIG_ENTROPY_STATS
    ++counts->palette_uv_mode[palette_uv_mode_ctx][n > 0];
#endif
    update_cdf(fc->palette_uv_mode_cdf[palette_uv_mode_ctx], n > 0, 2);

    if (n > 0) {
#if CONFIG_ENTROPY_STATS
      ++counts->palette_uv_size[palette_bsize_ctx][n - PALETTE_MIN_SIZE];
#endif
      update_cdf(fc->palette_uv_size_cdf[palette_bsize_ctx],
                 n - PALETTE_MIN_SIZE, PALETTE_SIZES);
    }
  }
}

void av1_sum_intra_stats(const AV1_COMMON *const cm, FRAME_COUNTS *counts,
                         MACROBLOCKD *xd, const MB_MODE_INFO *const mbmi,
                         const MB_MODE_INFO *above_mi,
                         const MB_MODE_INFO *left_mi, const int intraonly) {
  FRAME_CONTEXT *fc = xd->tile_ctx;
  const PREDICTION_MODE y_mode = mbmi->mode;
  (void)counts;
  const BLOCK_SIZE bsize = mbmi->bsize;

  if (intraonly) {
#if CONFIG_ENTROPY_STATS
    const PREDICTION_MODE above = av1_above_block_mode(above_mi);
    const PREDICTION_MODE left = av1_left_block_mode(left_mi);
    const int above_ctx = intra_mode_context[above];
    const int left_ctx = intra_mode_context[left];
    ++counts->kf_y_mode[above_ctx][left_ctx][y_mode];
#endif  // CONFIG_ENTROPY_STATS
    update_cdf(get_y_mode_cdf(fc, above_mi, left_mi), y_mode, INTRA_MODES);
  } else {
#if CONFIG_ENTROPY_STATS
    ++counts->y_mode[size_group_lookup[bsize]][y_mode];
#endif  // CONFIG_ENTROPY_STATS
    update_cdf(fc->y_mode_cdf[size_group_lookup[bsize]], y_mode, INTRA_MODES);
  }

  if (av1_filter_intra_allowed(cm, mbmi)) {
    const int use_filter_intra_mode =
        mbmi->filter_intra_mode_info.use_filter_intra;
#if CONFIG_ENTROPY_STATS
    ++counts->filter_intra[mbmi->bsize][use_filter_intra_mode];
    if (use_filter_intra_mode) {
      ++counts
            ->filter_intra_mode[mbmi->filter_intra_mode_info.filter_intra_mode];
    }
#endif  // CONFIG_ENTROPY_STATS
    update_cdf(fc->filter_intra_cdfs[mbmi->bsize], use_filter_intra_mode, 2);
    if (use_filter_intra_mode) {
      update_cdf(fc->filter_intra_mode_cdf,
                 mbmi->filter_intra_mode_info.filter_intra_mode,
                 FILTER_INTRA_MODES);
    }
  }
  if (av1_is_directional_mode(mbmi->mode) && av1_use_angle_delta(bsize)) {
#if CONFIG_ENTROPY_STATS
    ++counts->angle_delta[mbmi->mode - V_PRED]
                         [mbmi->angle_delta[PLANE_TYPE_Y] + MAX_ANGLE_DELTA];
#endif
    update_cdf(fc->angle_delta_cdf[mbmi->mode - V_PRED],
               mbmi->angle_delta[PLANE_TYPE_Y] + MAX_ANGLE_DELTA,
               2 * MAX_ANGLE_DELTA + 1);
  }

  if (!xd->is_chroma_ref) return;

  const UV_PREDICTION_MODE uv_mode = mbmi->uv_mode;
  const CFL_ALLOWED_TYPE cfl_allowed = is_cfl_allowed(xd);
#if CONFIG_ENTROPY_STATS
  ++counts->uv_mode[cfl_allowed][y_mode][uv_mode];
#endif  // CONFIG_ENTROPY_STATS
  update_cdf(fc->uv_mode_cdf[cfl_allowed][y_mode], uv_mode,
             UV_INTRA_MODES - !cfl_allowed);
  if (uv_mode == UV_CFL_PRED) {
    const int8_t joint_sign = mbmi->cfl_alpha_signs;
    const uint8_t idx = mbmi->cfl_alpha_idx;

#if CONFIG_ENTROPY_STATS
    ++counts->cfl_sign[joint_sign];
#endif
    update_cdf(fc->cfl_sign_cdf, joint_sign, CFL_JOINT_SIGNS);
    if (CFL_SIGN_U(joint_sign) != CFL_SIGN_ZERO) {
      aom_cdf_prob *cdf_u = fc->cfl_alpha_cdf[CFL_CONTEXT_U(joint_sign)];

#if CONFIG_ENTROPY_STATS
      ++counts->cfl_alpha[CFL_CONTEXT_U(joint_sign)][CFL_IDX_U(idx)];
#endif
      update_cdf(cdf_u, CFL_IDX_U(idx), CFL_ALPHABET_SIZE);
    }
    if (CFL_SIGN_V(joint_sign) != CFL_SIGN_ZERO) {
      aom_cdf_prob *cdf_v = fc->cfl_alpha_cdf[CFL_CONTEXT_V(joint_sign)];

#if CONFIG_ENTROPY_STATS
      ++counts->cfl_alpha[CFL_CONTEXT_V(joint_sign)][CFL_IDX_V(idx)];
#endif
      update_cdf(cdf_v, CFL_IDX_V(idx), CFL_ALPHABET_SIZE);
    }
  }
  const PREDICTION_MODE intra_mode = get_uv_mode(uv_mode);
  if (av1_is_directional_mode(intra_mode) && av1_use_angle_delta(bsize)) {
#if CONFIG_ENTROPY_STATS
    ++counts->angle_delta[intra_mode - V_PRED]
                         [mbmi->angle_delta[PLANE_TYPE_UV] + MAX_ANGLE_DELTA];
#endif
    update_cdf(fc->angle_delta_cdf[intra_mode - V_PRED],
               mbmi->angle_delta[PLANE_TYPE_UV] + MAX_ANGLE_DELTA,
               2 * MAX_ANGLE_DELTA + 1);
  }
  if (av1_allow_palette(cm->features.allow_screen_content_tools, bsize)) {
    update_palette_cdf(xd, mbmi, counts);
  }
}

void av1_restore_context(MACROBLOCK *x, const RD_SEARCH_MACROBLOCK_CONTEXT *ctx,
                         int mi_row, int mi_col, BLOCK_SIZE bsize,
                         const int num_planes) {
  MACROBLOCKD *xd = &x->e_mbd;
  int p;
  const int num_4x4_blocks_wide = mi_size_wide[bsize];
  const int num_4x4_blocks_high = mi_size_high[bsize];
  int mi_width = mi_size_wide[bsize];
  int mi_height = mi_size_high[bsize];
  for (p = 0; p < num_planes; p++) {
    int tx_col = mi_col;
    int tx_row = mi_row & MAX_MIB_MASK;
    memcpy(
        xd->above_entropy_context[p] + (tx_col >> xd->plane[p].subsampling_x),
        ctx->a + num_4x4_blocks_wide * p,
        (sizeof(ENTROPY_CONTEXT) * num_4x4_blocks_wide) >>
            xd->plane[p].subsampling_x);
    memcpy(xd->left_entropy_context[p] + (tx_row >> xd->plane[p].subsampling_y),
           ctx->l + num_4x4_blocks_high * p,
           (sizeof(ENTROPY_CONTEXT) * num_4x4_blocks_high) >>
               xd->plane[p].subsampling_y);
  }
  memcpy(xd->above_partition_context + mi_col, ctx->sa,
         sizeof(*xd->above_partition_context) * mi_width);
  memcpy(xd->left_partition_context + (mi_row & MAX_MIB_MASK), ctx->sl,
         sizeof(xd->left_partition_context[0]) * mi_height);
  xd->above_txfm_context = ctx->p_ta;
  xd->left_txfm_context = ctx->p_tl;
  memcpy(xd->above_txfm_context, ctx->ta,
         sizeof(*xd->above_txfm_context) * mi_width);
  memcpy(xd->left_txfm_context, ctx->tl,
         sizeof(*xd->left_txfm_context) * mi_height);
}

void av1_save_context(const MACROBLOCK *x, RD_SEARCH_MACROBLOCK_CONTEXT *ctx,
                      int mi_row, int mi_col, BLOCK_SIZE bsize,
                      const int num_planes) {
  const MACROBLOCKD *xd = &x->e_mbd;
  int p;
  int mi_width = mi_size_wide[bsize];
  int mi_height = mi_size_high[bsize];

  // buffer the above/left context information of the block in search.
  for (p = 0; p < num_planes; ++p) {
    int tx_col = mi_col;
    int tx_row = mi_row & MAX_MIB_MASK;
    memcpy(
        ctx->a + mi_width * p,
        xd->above_entropy_context[p] + (tx_col >> xd->plane[p].subsampling_x),
        (sizeof(ENTROPY_CONTEXT) * mi_width) >> xd->plane[p].subsampling_x);
    memcpy(ctx->l + mi_height * p,
           xd->left_entropy_context[p] + (tx_row >> xd->plane[p].subsampling_y),
           (sizeof(ENTROPY_CONTEXT) * mi_height) >> xd->plane[p].subsampling_y);
  }
  memcpy(ctx->sa, xd->above_partition_context + mi_col,
         sizeof(*xd->above_partition_context) * mi_width);
  memcpy(ctx->sl, xd->left_partition_context + (mi_row & MAX_MIB_MASK),
         sizeof(xd->left_partition_context[0]) * mi_height);
  memcpy(ctx->ta, xd->above_txfm_context,
         sizeof(*xd->above_txfm_context) * mi_width);
  memcpy(ctx->tl, xd->left_txfm_context,
         sizeof(*xd->left_txfm_context) * mi_height);
  ctx->p_ta = xd->above_txfm_context;
  ctx->p_tl = xd->left_txfm_context;
}

static void set_partial_sb_partition(const AV1_COMMON *const cm,
                                     MB_MODE_INFO *mi, int bh_in, int bw_in,
                                     int mi_rows_remaining,
                                     int mi_cols_remaining, BLOCK_SIZE bsize,
                                     MB_MODE_INFO **mib) {
  int bh = bh_in;
  int r, c;
  for (r = 0; r < cm->seq_params->mib_size; r += bh) {
    int bw = bw_in;
    for (c = 0; c < cm->seq_params->mib_size; c += bw) {
      const int grid_index = get_mi_grid_idx(&cm->mi_params, r, c);
      const int mi_index = get_alloc_mi_idx(&cm->mi_params, r, c);
      mib[grid_index] = mi + mi_index;
      mib[grid_index]->bsize = find_partition_size(
          bsize, mi_rows_remaining - r, mi_cols_remaining - c, &bh, &bw);
    }
  }
}

// This function attempts to set all mode info entries in a given superblock
// to the same block partition size.
// However, at the bottom and right borders of the image the requested size
// may not be allowed in which case this code attempts to choose the largest
// allowable partition.
void av1_set_fixed_partitioning(AV1_COMP *cpi, const TileInfo *const tile,
                                MB_MODE_INFO **mib, int mi_row, int mi_col,
                                BLOCK_SIZE bsize) {
  AV1_COMMON *const cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int mi_rows_remaining = tile->mi_row_end - mi_row;
  const int mi_cols_remaining = tile->mi_col_end - mi_col;
  MB_MODE_INFO *const mi_upper_left =
      mi_params->mi_alloc + get_alloc_mi_idx(mi_params, mi_row, mi_col);
  int bh = mi_size_high[bsize];
  int bw = mi_size_wide[bsize];

  assert(bsize >= mi_params->mi_alloc_bsize &&
         "Attempted to use bsize < mi_params->mi_alloc_bsize");
  assert((mi_rows_remaining > 0) && (mi_cols_remaining > 0));

  // Apply the requested partition size to the SB if it is all "in image"
  if ((mi_cols_remaining >= cm->seq_params->mib_size) &&
      (mi_rows_remaining >= cm->seq_params->mib_size)) {
    for (int block_row = 0; block_row < cm->seq_params->mib_size;
         block_row += bh) {
      for (int block_col = 0; block_col < cm->seq_params->mib_size;
           block_col += bw) {
        const int grid_index = get_mi_grid_idx(mi_params, block_row, block_col);
        const int mi_index = get_alloc_mi_idx(mi_params, block_row, block_col);
        mib[grid_index] = mi_upper_left + mi_index;
        mib[grid_index]->bsize = bsize;
      }
    }
  } else {
    // Else this is a partial SB.
    set_partial_sb_partition(cm, mi_upper_left, bh, bw, mi_rows_remaining,
                             mi_cols_remaining, bsize, mib);
  }
}

int av1_is_leaf_split_partition(AV1_COMMON *cm, int mi_row, int mi_col,
                                BLOCK_SIZE bsize) {
  const int bs = mi_size_wide[bsize];
  const int hbs = bs / 2;
  assert(bsize >= BLOCK_8X8);
  const BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);

  for (int i = 0; i < 4; i++) {
    int x_idx = (i & 1) * hbs;
    int y_idx = (i >> 1) * hbs;
    if ((mi_row + y_idx >= cm->mi_params.mi_rows) ||
        (mi_col + x_idx >= cm->mi_params.mi_cols))
      return 0;
    if (get_partition(cm, mi_row + y_idx, mi_col + x_idx, subsize) !=
            PARTITION_NONE &&
        subsize != BLOCK_8X8)
      return 0;
  }
  return 1;
}

#if !CONFIG_REALTIME_ONLY
int av1_get_rdmult_delta(AV1_COMP *cpi, BLOCK_SIZE bsize, int mi_row,
                         int mi_col, int orig_rdmult) {
  AV1_COMMON *const cm = &cpi->common;
  const GF_GROUP *const gf_group = &cpi->ppi->gf_group;
  assert(IMPLIES(cpi->ppi->gf_group.size > 0,
                 cpi->gf_frame_index < cpi->ppi->gf_group.size));
  const int tpl_idx = cpi->gf_frame_index;
  TplParams *const tpl_data = &cpi->ppi->tpl_data;
  const uint8_t block_mis_log2 = tpl_data->tpl_stats_block_mis_log2;
  int64_t intra_cost = 0;
  int64_t mc_dep_cost = 0;
  const int mi_wide = mi_size_wide[bsize];
  const int mi_high = mi_size_high[bsize];

  TplDepFrame *tpl_frame = &tpl_data->tpl_frame[tpl_idx];
  TplDepStats *tpl_stats = tpl_frame->tpl_stats_ptr;
  int tpl_stride = tpl_frame->stride;

  if (!av1_tpl_stats_ready(&cpi->ppi->tpl_data, cpi->gf_frame_index)) {
    return orig_rdmult;
  }
  if (!is_frame_tpl_eligible(gf_group, cpi->gf_frame_index)) {
    return orig_rdmult;
  }

#ifndef NDEBUG
  int mi_count = 0;
#endif
  const int mi_col_sr =
      coded_to_superres_mi(mi_col, cm->superres_scale_denominator);
  const int mi_col_end_sr =
      coded_to_superres_mi(mi_col + mi_wide, cm->superres_scale_denominator);
  const int mi_cols_sr = av1_pixels_to_mi(cm->superres_upscaled_width);
  const int step = 1 << block_mis_log2;
  const int row_step = step;
  const int col_step_sr =
      coded_to_superres_mi(step, cm->superres_scale_denominator);
  for (int row = mi_row; row < mi_row + mi_high; row += row_step) {
    for (int col = mi_col_sr; col < mi_col_end_sr; col += col_step_sr) {
      if (row >= cm->mi_params.mi_rows || col >= mi_cols_sr) continue;
      TplDepStats *this_stats =
          &tpl_stats[av1_tpl_ptr_pos(row, col, tpl_stride, block_mis_log2)];
      int64_t mc_dep_delta =
          RDCOST(tpl_frame->base_rdmult, this_stats->mc_dep_rate,
                 this_stats->mc_dep_dist);
      intra_cost += this_stats->recrf_dist << RDDIV_BITS;
      mc_dep_cost += (this_stats->recrf_dist << RDDIV_BITS) + mc_dep_delta;
#ifndef NDEBUG
      mi_count++;
#endif
    }
  }
  assert(mi_count <= MAX_TPL_BLK_IN_SB * MAX_TPL_BLK_IN_SB);

  double beta = 1.0;
  if (mc_dep_cost > 0 && intra_cost > 0) {
    const double r0 = cpi->rd.r0;
    const double rk = (double)intra_cost / mc_dep_cost;
    beta = (r0 / rk);
  }

  int rdmult = av1_get_adaptive_rdmult(cpi, beta);

  rdmult = AOMMIN(rdmult, orig_rdmult * 3 / 2);
  rdmult = AOMMAX(rdmult, orig_rdmult * 1 / 2);

  rdmult = AOMMAX(1, rdmult);

  return rdmult;
}

// Checks to see if a super block is on a horizontal image edge.
// In most cases this is the "real" edge unless there are formatting
// bars embedded in the stream.
int av1_active_h_edge(const AV1_COMP *cpi, int mi_row, int mi_step) {
  int top_edge = 0;
  int bottom_edge = cpi->common.mi_params.mi_rows;
  int is_active_h_edge = 0;

  // For two pass account for any formatting bars detected.
  if (is_stat_consumption_stage_twopass(cpi)) {
    const AV1_COMMON *const cm = &cpi->common;
    const FIRSTPASS_STATS *const this_frame_stats = read_one_frame_stats(
        &cpi->ppi->twopass, cm->current_frame.display_order_hint);
    if (this_frame_stats == NULL) return AOM_CODEC_ERROR;

    // The inactive region is specified in MBs not mi units.
    // The image edge is in the following MB row.
    top_edge += (int)(this_frame_stats->inactive_zone_rows * 4);

    bottom_edge -= (int)(this_frame_stats->inactive_zone_rows * 4);
    bottom_edge = AOMMAX(top_edge, bottom_edge);
  }

  if (((top_edge >= mi_row) && (top_edge < (mi_row + mi_step))) ||
      ((bottom_edge >= mi_row) && (bottom_edge < (mi_row + mi_step)))) {
    is_active_h_edge = 1;
  }
  return is_active_h_edge;
}

// Checks to see if a super block is on a vertical image edge.
// In most cases this is the "real" edge unless there are formatting
// bars embedded in the stream.
int av1_active_v_edge(const AV1_COMP *cpi, int mi_col, int mi_step) {
  int left_edge = 0;
  int right_edge = cpi->common.mi_params.mi_cols;
  int is_active_v_edge = 0;

  // For two pass account for any formatting bars detected.
  if (is_stat_consumption_stage_twopass(cpi)) {
    const AV1_COMMON *const cm = &cpi->common;
    const FIRSTPASS_STATS *const this_frame_stats = read_one_frame_stats(
        &cpi->ppi->twopass, cm->current_frame.display_order_hint);
    if (this_frame_stats == NULL) return AOM_CODEC_ERROR;

    // The inactive region is specified in MBs not mi units.
    // The image edge is in the following MB row.
    left_edge += (int)(this_frame_stats->inactive_zone_cols * 4);

    right_edge -= (int)(this_frame_stats->inactive_zone_cols * 4);
    right_edge = AOMMAX(left_edge, right_edge);
  }

  if (((left_edge >= mi_col) && (left_edge < (mi_col + mi_step))) ||
      ((right_edge >= mi_col) && (right_edge < (mi_col + mi_step)))) {
    is_active_v_edge = 1;
  }
  return is_active_v_edge;
}

void av1_get_tpl_stats_sb(AV1_COMP *cpi, BLOCK_SIZE bsize, int mi_row,
                          int mi_col, SuperBlockEnc *sb_enc) {
  sb_enc->tpl_data_count = 0;

  if (!cpi->oxcf.algo_cfg.enable_tpl_model) return;
  if (cpi->common.current_frame.frame_type == KEY_FRAME) return;
  const FRAME_UPDATE_TYPE update_type =
      get_frame_update_type(&cpi->ppi->gf_group, cpi->gf_frame_index);
  if (update_type == INTNL_OVERLAY_UPDATE || update_type == OVERLAY_UPDATE)
    return;
  assert(IMPLIES(cpi->ppi->gf_group.size > 0,
                 cpi->gf_frame_index < cpi->ppi->gf_group.size));

  AV1_COMMON *const cm = &cpi->common;
  const int gf_group_index = cpi->gf_frame_index;
  TplParams *const tpl_data = &cpi->ppi->tpl_data;
  if (!av1_tpl_stats_ready(tpl_data, gf_group_index)) return;
  const int mi_wide = mi_size_wide[bsize];
  const int mi_high = mi_size_high[bsize];

  TplDepFrame *tpl_frame = &tpl_data->tpl_frame[gf_group_index];
  TplDepStats *tpl_stats = tpl_frame->tpl_stats_ptr;
  int tpl_stride = tpl_frame->stride;

  int mi_count = 0;
  int count = 0;
  const int mi_col_sr =
      coded_to_superres_mi(mi_col, cm->superres_scale_denominator);
  const int mi_col_end_sr =
      coded_to_superres_mi(mi_col + mi_wide, cm->superres_scale_denominator);
  // mi_cols_sr is mi_cols at superres case.
  const int mi_cols_sr = av1_pixels_to_mi(cm->superres_upscaled_width);

  // TPL store unit size is not the same as the motion estimation unit size.
  // Here always use motion estimation size to avoid getting repetitive inter/
  // intra cost.
  const BLOCK_SIZE tpl_bsize = convert_length_to_bsize(tpl_data->tpl_bsize_1d);
  assert(mi_size_wide[tpl_bsize] == mi_size_high[tpl_bsize]);
  const int row_step = mi_size_high[tpl_bsize];
  const int col_step_sr = coded_to_superres_mi(mi_size_wide[tpl_bsize],
                                               cm->superres_scale_denominator);

  // Stride is only based on SB size, and we fill in values for every 16x16
  // block in a SB.
  sb_enc->tpl_stride = (mi_col_end_sr - mi_col_sr) / col_step_sr;

  for (int row = mi_row; row < mi_row + mi_high; row += row_step) {
    for (int col = mi_col_sr; col < mi_col_end_sr; col += col_step_sr) {
      assert(count < MAX_TPL_BLK_IN_SB * MAX_TPL_BLK_IN_SB);
      // Handle partial SB, so that no invalid values are used later.
      if (row >= cm->mi_params.mi_rows || col >= mi_cols_sr) {
        sb_enc->tpl_inter_cost[count] = INT64_MAX;
        sb_enc->tpl_intra_cost[count] = INT64_MAX;
        for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
          sb_enc->tpl_mv[count][i].as_int = INVALID_MV;
        }
        count++;
        continue;
      }

      TplDepStats *this_stats = &tpl_stats[av1_tpl_ptr_pos(
          row, col, tpl_stride, tpl_data->tpl_stats_block_mis_log2)];
      sb_enc->tpl_inter_cost[count] = this_stats->inter_cost
                                      << TPL_DEP_COST_SCALE_LOG2;
      sb_enc->tpl_intra_cost[count] = this_stats->intra_cost
                                      << TPL_DEP_COST_SCALE_LOG2;
      memcpy(sb_enc->tpl_mv[count], this_stats->mv, sizeof(this_stats->mv));
      mi_count++;
      count++;
    }
  }

  assert(mi_count <= MAX_TPL_BLK_IN_SB * MAX_TPL_BLK_IN_SB);
  sb_enc->tpl_data_count = mi_count;
}

// analysis_type 0: Use mc_dep_cost and intra_cost
// analysis_type 1: Use count of best inter predictor chosen
// analysis_type 2: Use cost reduction from intra to inter for best inter
//                  predictor chosen
int av1_get_q_for_deltaq_objective(AV1_COMP *const cpi, ThreadData *td,
                                   int64_t *delta_dist, BLOCK_SIZE bsize,
                                   int mi_row, int mi_col) {
  AV1_COMMON *const cm = &cpi->common;
  assert(IMPLIES(cpi->ppi->gf_group.size > 0,
                 cpi->gf_frame_index < cpi->ppi->gf_group.size));
  const int tpl_idx = cpi->gf_frame_index;
  TplParams *const tpl_data = &cpi->ppi->tpl_data;
  const uint8_t block_mis_log2 = tpl_data->tpl_stats_block_mis_log2;
  double intra_cost = 0;
  double mc_dep_reg = 0;
  double mc_dep_cost = 0;
  double cbcmp_base = 1;
  double srcrf_dist = 0;
  double srcrf_sse = 0;
  double srcrf_rate = 0;
  const int mi_wide = mi_size_wide[bsize];
  const int mi_high = mi_size_high[bsize];
  const int base_qindex = cm->quant_params.base_qindex;

  if (tpl_idx >= MAX_TPL_FRAME_IDX) return base_qindex;

  TplDepFrame *tpl_frame = &tpl_data->tpl_frame[tpl_idx];
  TplDepStats *tpl_stats = tpl_frame->tpl_stats_ptr;
  int tpl_stride = tpl_frame->stride;
  if (!tpl_frame->is_valid) return base_qindex;

#ifndef NDEBUG
  int mi_count = 0;
#endif
  const int mi_col_sr =
      coded_to_superres_mi(mi_col, cm->superres_scale_denominator);
  const int mi_col_end_sr =
      coded_to_superres_mi(mi_col + mi_wide, cm->superres_scale_denominator);
  const int mi_cols_sr = av1_pixels_to_mi(cm->superres_upscaled_width);
  const int step = 1 << block_mis_log2;
  const int row_step = step;
  const int col_step_sr =
      coded_to_superres_mi(step, cm->superres_scale_denominator);
  for (int row = mi_row; row < mi_row + mi_high; row += row_step) {
    for (int col = mi_col_sr; col < mi_col_end_sr; col += col_step_sr) {
      if (row >= cm->mi_params.mi_rows || col >= mi_cols_sr) continue;
      TplDepStats *this_stats =
          &tpl_stats[av1_tpl_ptr_pos(row, col, tpl_stride, block_mis_log2)];
      double cbcmp = (double)this_stats->srcrf_dist;
      int64_t mc_dep_delta =
          RDCOST(tpl_frame->base_rdmult, this_stats->mc_dep_rate,
                 this_stats->mc_dep_dist);
      double dist_scaled = (double)(this_stats->recrf_dist << RDDIV_BITS);
      intra_cost += log(dist_scaled) * cbcmp;
      mc_dep_cost += log(dist_scaled + mc_dep_delta) * cbcmp;
      mc_dep_reg += log(3 * dist_scaled + mc_dep_delta) * cbcmp;
      srcrf_dist += (double)(this_stats->srcrf_dist << RDDIV_BITS);
      srcrf_sse += (double)(this_stats->srcrf_sse << RDDIV_BITS);
      srcrf_rate += (double)(this_stats->srcrf_rate << TPL_DEP_COST_SCALE_LOG2);
#ifndef NDEBUG
      mi_count++;
#endif
      cbcmp_base += cbcmp;
    }
  }
  assert(mi_count <= MAX_TPL_BLK_IN_SB * MAX_TPL_BLK_IN_SB);

  int offset = 0;
  double beta = 1.0;
  double rk;
  if (mc_dep_cost > 0 && intra_cost > 0) {
    const double r0 = cpi->rd.r0;
    rk = exp((intra_cost - mc_dep_cost) / cbcmp_base);
    td->mb.rb = exp((intra_cost - mc_dep_reg) / cbcmp_base);
    beta = (r0 / rk);
    assert(beta > 0.0);
  } else {
    return base_qindex;
  }
  offset = av1_get_deltaq_offset(cm->seq_params->bit_depth, base_qindex, beta);

  const DeltaQInfo *const delta_q_info = &cm->delta_q_info;
  offset = AOMMIN(offset, delta_q_info->delta_q_res * 9 - 1);
  offset = AOMMAX(offset, -delta_q_info->delta_q_res * 9 + 1);
  int qindex = cm->quant_params.base_qindex + offset;
  qindex = AOMMIN(qindex, MAXQ);
  qindex = AOMMAX(qindex, MINQ);

  int frm_qstep = av1_dc_quant_QTX(base_qindex, 0, cm->seq_params->bit_depth);
  int sbs_qstep =
      av1_dc_quant_QTX(base_qindex, offset, cm->seq_params->bit_depth);

  if (delta_dist) {
    double sbs_dist = srcrf_dist * pow((double)sbs_qstep / frm_qstep, 2.0);
    double sbs_rate = srcrf_rate * ((double)frm_qstep / sbs_qstep);
    sbs_dist = AOMMIN(sbs_dist, srcrf_sse);
    *delta_dist = (int64_t)((sbs_dist - srcrf_dist) / rk);
    *delta_dist += RDCOST(tpl_frame->base_rdmult, 4 * 256, 0);
    *delta_dist += RDCOST(tpl_frame->base_rdmult, sbs_rate - srcrf_rate, 0);
  }
  return qindex;
}

#if !DISABLE_HDR_LUMA_DELTAQ
// offset table defined in Table3 of T-REC-H.Sup15 document.
static const int hdr_thres[HDR_QP_LEVELS + 1] = { 0,   301, 367, 434, 501, 567,
                                                  634, 701, 767, 834, 1024 };

static const int hdr10_qp_offset[HDR_QP_LEVELS] = { 3,  2,  1,  0,  -1,
                                                    -2, -3, -4, -5, -6 };
#endif

int av1_get_q_for_hdr(AV1_COMP *const cpi, MACROBLOCK *const x,
                      BLOCK_SIZE bsize, int mi_row, int mi_col) {
  AV1_COMMON *const cm = &cpi->common;
  assert(cm->seq_params->bit_depth == AOM_BITS_10);

#if DISABLE_HDR_LUMA_DELTAQ
  (void)x;
  (void)bsize;
  (void)mi_row;
  (void)mi_col;
  return cm->quant_params.base_qindex;
#else
  // calculate pixel average
  const int block_luma_avg = av1_log_block_avg(cpi, x, bsize, mi_row, mi_col);
  // adjust offset based on average of the pixel block
  int offset = 0;
  for (int i = 0; i < HDR_QP_LEVELS; i++) {
    if (block_luma_avg >= hdr_thres[i] && block_luma_avg < hdr_thres[i + 1]) {
      offset = (int)(hdr10_qp_offset[i] * QP_SCALE_FACTOR);
      break;
    }
  }

  const DeltaQInfo *const delta_q_info = &cm->delta_q_info;
  offset = AOMMIN(offset, delta_q_info->delta_q_res * 9 - 1);
  offset = AOMMAX(offset, -delta_q_info->delta_q_res * 9 + 1);
  int qindex = cm->quant_params.base_qindex + offset;
  qindex = AOMMIN(qindex, MAXQ);
  qindex = AOMMAX(qindex, MINQ);

  return qindex;
#endif
}
#endif  // !CONFIG_REALTIME_ONLY

void av1_reset_simple_motion_tree_partition(SIMPLE_MOTION_DATA_TREE *sms_tree,
                                            BLOCK_SIZE bsize) {
  if (sms_tree == NULL) return;
  sms_tree->partitioning = PARTITION_NONE;

  if (bsize >= BLOCK_8X8) {
    BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);
    for (int idx = 0; idx < 4; ++idx)
      av1_reset_simple_motion_tree_partition(sms_tree->split[idx], subsize);
  }
}

// Record the ref frames that have been selected by square partition blocks.
void av1_update_picked_ref_frames_mask(MACROBLOCK *const x, int ref_type,
                                       BLOCK_SIZE bsize, int mib_size,
                                       int mi_row, int mi_col) {
  assert(mi_size_wide[bsize] == mi_size_high[bsize]);
  const int sb_size_mask = mib_size - 1;
  const int mi_row_in_sb = mi_row & sb_size_mask;
  const int mi_col_in_sb = mi_col & sb_size_mask;
  const int mi_size = mi_size_wide[bsize];
  for (int i = mi_row_in_sb; i < mi_row_in_sb + mi_size; ++i) {
    for (int j = mi_col_in_sb; j < mi_col_in_sb + mi_size; ++j) {
      x->picked_ref_frames_mask[i * 32 + j] |= 1 << ref_type;
    }
  }
}

static void avg_cdf_symbol(aom_cdf_prob *cdf_ptr_left, aom_cdf_prob *cdf_ptr_tr,
                           int num_cdfs, int cdf_stride, int nsymbs,
                           int wt_left, int wt_tr) {
  for (int i = 0; i < num_cdfs; i++) {
    for (int j = 0; j <= nsymbs; j++) {
      cdf_ptr_left[i * cdf_stride + j] =
          (aom_cdf_prob)(((int)cdf_ptr_left[i * cdf_stride + j] * wt_left +
                          (int)cdf_ptr_tr[i * cdf_stride + j] * wt_tr +
                          ((wt_left + wt_tr) / 2)) /
                         (wt_left + wt_tr));
      assert(cdf_ptr_left[i * cdf_stride + j] >= 0 &&
             cdf_ptr_left[i * cdf_stride + j] < CDF_PROB_TOP);
    }
  }
}

#define AVERAGE_CDF(cname_left, cname_tr, nsymbs) \
  AVG_CDF_STRIDE(cname_left, cname_tr, nsymbs, CDF_SIZE(nsymbs))

#define AVG_CDF_STRIDE(cname_left, cname_tr, nsymbs, cdf_stride)           \
  do {                                                                     \
    aom_cdf_prob *cdf_ptr_left = (aom_cdf_prob *)cname_left;               \
    aom_cdf_prob *cdf_ptr_tr = (aom_cdf_prob *)cname_tr;                   \
    int array_size = (int)sizeof(cname_left) / sizeof(aom_cdf_prob);       \
    int num_cdfs = array_size / cdf_stride;                                \
    avg_cdf_symbol(cdf_ptr_left, cdf_ptr_tr, num_cdfs, cdf_stride, nsymbs, \
                   wt_left, wt_tr);                                        \
  } while (0)

static void avg_nmv(nmv_context *nmv_left, nmv_context *nmv_tr, int wt_left,
                    int wt_tr) {
  AVERAGE_CDF(nmv_left->joints_cdf, nmv_tr->joints_cdf, 4);
  for (int i = 0; i < 2; i++) {
    AVERAGE_CDF(nmv_left->comps[i].classes_cdf, nmv_tr->comps[i].classes_cdf,
                MV_CLASSES);
    AVERAGE_CDF(nmv_left->comps[i].class0_fp_cdf,
                nmv_tr->comps[i].class0_fp_cdf, MV_FP_SIZE);
    AVERAGE_CDF(nmv_left->comps[i].fp_cdf, nmv_tr->comps[i].fp_cdf, MV_FP_SIZE);
    AVERAGE_CDF(nmv_left->comps[i].sign_cdf, nmv_tr->comps[i].sign_cdf, 2);
    AVERAGE_CDF(nmv_left->comps[i].class0_hp_cdf,
                nmv_tr->comps[i].class0_hp_cdf, 2);
    AVERAGE_CDF(nmv_left->comps[i].hp_cdf, nmv_tr->comps[i].hp_cdf, 2);
    AVERAGE_CDF(nmv_left->comps[i].class0_cdf, nmv_tr->comps[i].class0_cdf,
                CLASS0_SIZE);
    AVERAGE_CDF(nmv_left->comps[i].bits_cdf, nmv_tr->comps[i].bits_cdf, 2);
  }
}

// In case of row-based multi-threading of encoder, since we always
// keep a top - right sync, we can average the top - right SB's CDFs and
// the left SB's CDFs and use the same for current SB's encoding to
// improve the performance. This function facilitates the averaging
// of CDF and used only when row-mt is enabled in encoder.
void av1_avg_cdf_symbols(FRAME_CONTEXT *ctx_left, FRAME_CONTEXT *ctx_tr,
                         int wt_left, int wt_tr) {
  AVERAGE_CDF(ctx_left->txb_skip_cdf, ctx_tr->txb_skip_cdf, 2);
  AVERAGE_CDF(ctx_left->eob_extra_cdf, ctx_tr->eob_extra_cdf, 2);
  AVERAGE_CDF(ctx_left->dc_sign_cdf, ctx_tr->dc_sign_cdf, 2);
  AVERAGE_CDF(ctx_left->eob_flag_cdf16, ctx_tr->eob_flag_cdf16, 5);
  AVERAGE_CDF(ctx_left->eob_flag_cdf32, ctx_tr->eob_flag_cdf32, 6);
  AVERAGE_CDF(ctx_left->eob_flag_cdf64, ctx_tr->eob_flag_cdf64, 7);
  AVERAGE_CDF(ctx_left->eob_flag_cdf128, ctx_tr->eob_flag_cdf128, 8);
  AVERAGE_CDF(ctx_left->eob_flag_cdf256, ctx_tr->eob_flag_cdf256, 9);
  AVERAGE_CDF(ctx_left->eob_flag_cdf512, ctx_tr->eob_flag_cdf512, 10);
  AVERAGE_CDF(ctx_left->eob_flag_cdf1024, ctx_tr->eob_flag_cdf1024, 11);
  AVERAGE_CDF(ctx_left->coeff_base_eob_cdf, ctx_tr->coeff_base_eob_cdf, 3);
  AVERAGE_CDF(ctx_left->coeff_base_cdf, ctx_tr->coeff_base_cdf, 4);
  AVERAGE_CDF(ctx_left->coeff_br_cdf, ctx_tr->coeff_br_cdf, BR_CDF_SIZE);
  AVERAGE_CDF(ctx_left->newmv_cdf, ctx_tr->newmv_cdf, 2);
  AVERAGE_CDF(ctx_left->zeromv_cdf, ctx_tr->zeromv_cdf, 2);
  AVERAGE_CDF(ctx_left->refmv_cdf, ctx_tr->refmv_cdf, 2);
  AVERAGE_CDF(ctx_left->drl_cdf, ctx_tr->drl_cdf, 2);
  AVERAGE_CDF(ctx_left->inter_compound_mode_cdf,
              ctx_tr->inter_compound_mode_cdf, INTER_COMPOUND_MODES);
  AVERAGE_CDF(ctx_left->compound_type_cdf, ctx_tr->compound_type_cdf,
              MASKED_COMPOUND_TYPES);
  AVERAGE_CDF(ctx_left->wedge_idx_cdf, ctx_tr->wedge_idx_cdf, 16);
  AVERAGE_CDF(ctx_left->interintra_cdf, ctx_tr->interintra_cdf, 2);
  AVERAGE_CDF(ctx_left->wedge_interintra_cdf, ctx_tr->wedge_interintra_cdf, 2);
  AVERAGE_CDF(ctx_left->interintra_mode_cdf, ctx_tr->interintra_mode_cdf,
              INTERINTRA_MODES);
  AVERAGE_CDF(ctx_left->motion_mode_cdf, ctx_tr->motion_mode_cdf, MOTION_MODES);
  AVERAGE_CDF(ctx_left->obmc_cdf, ctx_tr->obmc_cdf, 2);
  AVERAGE_CDF(ctx_left->palette_y_size_cdf, ctx_tr->palette_y_size_cdf,
              PALETTE_SIZES);
  AVERAGE_CDF(ctx_left->palette_uv_size_cdf, ctx_tr->palette_uv_size_cdf,
              PALETTE_SIZES);
  for (int j = 0; j < PALETTE_SIZES; j++) {
    int nsymbs = j + PALETTE_MIN_SIZE;
    AVG_CDF_STRIDE(ctx_left->palette_y_color_index_cdf[j],
                   ctx_tr->palette_y_color_index_cdf[j], nsymbs,
                   CDF_SIZE(PALETTE_COLORS));
    AVG_CDF_STRIDE(ctx_left->palette_uv_color_index_cdf[j],
                   ctx_tr->palette_uv_color_index_cdf[j], nsymbs,
                   CDF_SIZE(PALETTE_COLORS));
  }
  AVERAGE_CDF(ctx_left->palette_y_mode_cdf, ctx_tr->palette_y_mode_cdf, 2);
  AVERAGE_CDF(ctx_left->palette_uv_mode_cdf, ctx_tr->palette_uv_mode_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_inter_cdf, ctx_tr->comp_inter_cdf, 2);
  AVERAGE_CDF(ctx_left->single_ref_cdf, ctx_tr->single_ref_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_ref_type_cdf, ctx_tr->comp_ref_type_cdf, 2);
  AVERAGE_CDF(ctx_left->uni_comp_ref_cdf, ctx_tr->uni_comp_ref_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_ref_cdf, ctx_tr->comp_ref_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_bwdref_cdf, ctx_tr->comp_bwdref_cdf, 2);
  AVERAGE_CDF(ctx_left->txfm_partition_cdf, ctx_tr->txfm_partition_cdf, 2);
  AVERAGE_CDF(ctx_left->compound_index_cdf, ctx_tr->compound_index_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_group_idx_cdf, ctx_tr->comp_group_idx_cdf, 2);
  AVERAGE_CDF(ctx_left->skip_mode_cdfs, ctx_tr->skip_mode_cdfs, 2);
  AVERAGE_CDF(ctx_left->skip_txfm_cdfs, ctx_tr->skip_txfm_cdfs, 2);
  AVERAGE_CDF(ctx_left->intra_inter_cdf, ctx_tr->intra_inter_cdf, 2);
  avg_nmv(&ctx_left->nmvc, &ctx_tr->nmvc, wt_left, wt_tr);
  avg_nmv(&ctx_left->ndvc, &ctx_tr->ndvc, wt_left, wt_tr);
  AVERAGE_CDF(ctx_left->intrabc_cdf, ctx_tr->intrabc_cdf, 2);
  AVERAGE_CDF(ctx_left->seg.pred_cdf, ctx_tr->seg.pred_cdf, 2);
  AVERAGE_CDF(ctx_left->seg.spatial_pred_seg_cdf,
              ctx_tr->seg.spatial_pred_seg_cdf, MAX_SEGMENTS);
  AVERAGE_CDF(ctx_left->filter_intra_cdfs, ctx_tr->filter_intra_cdfs, 2);
  AVERAGE_CDF(ctx_left->filter_intra_mode_cdf, ctx_tr->filter_intra_mode_cdf,
              FILTER_INTRA_MODES);
  AVERAGE_CDF(ctx_left->switchable_restore_cdf, ctx_tr->switchable_restore_cdf,
              RESTORE_SWITCHABLE_TYPES);
  AVERAGE_CDF(ctx_left->wiener_restore_cdf, ctx_tr->wiener_restore_cdf, 2);
  AVERAGE_CDF(ctx_left->sgrproj_restore_cdf, ctx_tr->sgrproj_restore_cdf, 2);
  AVERAGE_CDF(ctx_left->y_mode_cdf, ctx_tr->y_mode_cdf, INTRA_MODES);
  AVG_CDF_STRIDE(ctx_left->uv_mode_cdf[0], ctx_tr->uv_mode_cdf[0],
                 UV_INTRA_MODES - 1, CDF_SIZE(UV_INTRA_MODES));
  AVERAGE_CDF(ctx_left->uv_mode_cdf[1], ctx_tr->uv_mode_cdf[1], UV_INTRA_MODES);
  for (int i = 0; i < PARTITION_CONTEXTS; i++) {
    if (i < 4) {
      AVG_CDF_STRIDE(ctx_left->partition_cdf[i], ctx_tr->partition_cdf[i], 4,
                     CDF_SIZE(10));
    } else if (i < 16) {
      AVERAGE_CDF(ctx_left->partition_cdf[i], ctx_tr->partition_cdf[i], 10);
    } else {
      AVG_CDF_STRIDE(ctx_left->partition_cdf[i], ctx_tr->partition_cdf[i], 8,
                     CDF_SIZE(10));
    }
  }
  AVERAGE_CDF(ctx_left->switchable_interp_cdf, ctx_tr->switchable_interp_cdf,
              SWITCHABLE_FILTERS);
  AVERAGE_CDF(ctx_left->kf_y_cdf, ctx_tr->kf_y_cdf, INTRA_MODES);
  AVERAGE_CDF(ctx_left->angle_delta_cdf, ctx_tr->angle_delta_cdf,
              2 * MAX_ANGLE_DELTA + 1);
  AVG_CDF_STRIDE(ctx_left->tx_size_cdf[0], ctx_tr->tx_size_cdf[0], MAX_TX_DEPTH,
                 CDF_SIZE(MAX_TX_DEPTH + 1));
  AVERAGE_CDF(ctx_left->tx_size_cdf[1], ctx_tr->tx_size_cdf[1],
              MAX_TX_DEPTH + 1);
  AVERAGE_CDF(ctx_left->tx_size_cdf[2], ctx_tr->tx_size_cdf[2],
              MAX_TX_DEPTH + 1);
  AVERAGE_CDF(ctx_left->tx_size_cdf[3], ctx_tr->tx_size_cdf[3],
              MAX_TX_DEPTH + 1);
  AVERAGE_CDF(ctx_left->delta_q_cdf, ctx_tr->delta_q_cdf, DELTA_Q_PROBS + 1);
  AVERAGE_CDF(ctx_left->delta_lf_cdf, ctx_tr->delta_lf_cdf, DELTA_LF_PROBS + 1);
  for (int i = 0; i < FRAME_LF_COUNT; i++) {
    AVERAGE_CDF(ctx_left->delta_lf_multi_cdf[i], ctx_tr->delta_lf_multi_cdf[i],
                DELTA_LF_PROBS + 1);
  }
  AVG_CDF_STRIDE(ctx_left->intra_ext_tx_cdf[1], ctx_tr->intra_ext_tx_cdf[1], 7,
                 CDF_SIZE(TX_TYPES));
  AVG_CDF_STRIDE(ctx_left->intra_ext_tx_cdf[2], ctx_tr->intra_ext_tx_cdf[2], 5,
                 CDF_SIZE(TX_TYPES));
  AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[1], ctx_tr->inter_ext_tx_cdf[1], 16,
                 CDF_SIZE(TX_TYPES));
  AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[2], ctx_tr->inter_ext_tx_cdf[2], 12,
                 CDF_SIZE(TX_TYPES));
  AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[3], ctx_tr->inter_ext_tx_cdf[3], 2,
                 CDF_SIZE(TX_TYPES));
  AVERAGE_CDF(ctx_left->cfl_sign_cdf, ctx_tr->cfl_sign_cdf, CFL_JOINT_SIGNS);
  AVERAGE_CDF(ctx_left->cfl_alpha_cdf, ctx_tr->cfl_alpha_cdf,
              CFL_ALPHABET_SIZE);
}

// Check neighbor blocks' motion information.
static int check_neighbor_blocks(MB_MODE_INFO **mi, int mi_stride,
                                 const TileInfo *const tile_info, int mi_row,
                                 int mi_col) {
  int is_above_low_motion = 1;
  int is_left_low_motion = 1;
  const int thr = 24;

  // Check above block.
  if (mi_row > tile_info->mi_row_start) {
    const MB_MODE_INFO *above_mbmi = mi[-mi_stride];
    const int_mv above_mv = above_mbmi->mv[0];
    if (above_mbmi->mode >= INTRA_MODE_END &&
        (abs(above_mv.as_mv.row) > thr || abs(above_mv.as_mv.col) > thr))
      is_above_low_motion = 0;
  }

  // Check left block.
  if (mi_col > tile_info->mi_col_start) {
    const MB_MODE_INFO *left_mbmi = mi[-1];
    const int_mv left_mv = left_mbmi->mv[0];
    if (left_mbmi->mode >= INTRA_MODE_END &&
        (abs(left_mv.as_mv.row) > thr || abs(left_mv.as_mv.col) > thr))
      is_left_low_motion = 0;
  }

  return (is_above_low_motion && is_left_low_motion);
}

// Check this block's motion in a fast way.
static int fast_detect_non_zero_motion(AV1_COMP *cpi, const uint8_t *src_y,
                                       int src_ystride,
                                       const uint8_t *last_src_y,
                                       int last_src_ystride, int mi_row,
                                       int mi_col) {
  AV1_COMMON *const cm = &cpi->common;
  const BLOCK_SIZE bsize = cm->seq_params->sb_size;
  unsigned int blk_sad = INT_MAX;
  if (cpi->src_sad_blk_64x64 != NULL) {
    const int sb_size_by_mb = (bsize == BLOCK_128X128)
                                  ? (cm->seq_params->mib_size >> 1)
                                  : cm->seq_params->mib_size;
    const int sb_cols =
        (cm->mi_params.mi_cols + sb_size_by_mb - 1) / sb_size_by_mb;
    const int sbi_col = mi_col / sb_size_by_mb;
    const int sbi_row = mi_row / sb_size_by_mb;
    blk_sad = (unsigned int)cpi->src_sad_blk_64x64[sbi_col + sbi_row * sb_cols];
  } else {
    blk_sad = cpi->ppi->fn_ptr[bsize].sdf(src_y, src_ystride, last_src_y,
                                          last_src_ystride);
  }

  // Search 4 1-away points.
  const uint8_t *const search_pos[4] = {
    last_src_y - last_src_ystride,
    last_src_y - 1,
    last_src_y + 1,
    last_src_y + last_src_ystride,
  };
  unsigned int sad_arr[4];
  cpi->ppi->fn_ptr[bsize].sdx4df(src_y, src_ystride, search_pos,
                                 last_src_ystride, sad_arr);

  blk_sad = (blk_sad * 5) >> 3;
  return (blk_sad < sad_arr[0] && blk_sad < sad_arr[1] &&
          blk_sad < sad_arr[2] && blk_sad < sad_arr[3]);
}

// Grade the temporal variation of the source by comparing the current sb and
// its collocated block in the last frame.
void av1_source_content_sb(AV1_COMP *cpi, MACROBLOCK *x, TileDataEnc *tile_data,
                           int mi_row, int mi_col) {
  if (cpi->last_source->y_width != cpi->source->y_width ||
      cpi->last_source->y_height != cpi->source->y_height)
    return;
#if CONFIG_AV1_HIGHBITDEPTH
  if (x->e_mbd.cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) return;
#endif

  unsigned int tmp_sse;
  unsigned int tmp_variance;
  const BLOCK_SIZE bsize = cpi->common.seq_params->sb_size;
  uint8_t *src_y = cpi->source->y_buffer;
  const int src_ystride = cpi->source->y_stride;
  const int src_offset = src_ystride * (mi_row << 2) + (mi_col << 2);
  uint8_t *last_src_y = cpi->last_source->y_buffer;
  const int last_src_ystride = cpi->last_source->y_stride;
  const int last_src_offset = last_src_ystride * (mi_row << 2) + (mi_col << 2);
  uint64_t avg_source_sse_threshold_verylow = 10000;     // ~1.5*1.5*(64*64)
  uint64_t avg_source_sse_threshold_low[2] = { 100000,   // ~5*5*(64*64)
                                               36000 };  // ~3*3*(64*64)

  uint64_t avg_source_sse_threshold_high = 1000000;  // ~15*15*(64*64)
  if (cpi->sf.rt_sf.increase_source_sad_thresh) {
    avg_source_sse_threshold_high = avg_source_sse_threshold_high << 1;
    avg_source_sse_threshold_low[0] = avg_source_sse_threshold_low[0] << 1;
    avg_source_sse_threshold_verylow = avg_source_sse_threshold_verylow << 1;
  }
  uint64_t sum_sq_thresh = 10000;  // sum = sqrt(thresh / 64*64)) ~1.5
  src_y += src_offset;
  last_src_y += last_src_offset;
  tmp_variance = cpi->ppi->fn_ptr[bsize].vf(src_y, src_ystride, last_src_y,
                                            last_src_ystride, &tmp_sse);
  // rd thresholds
  if (tmp_sse < avg_source_sse_threshold_low[1])
    x->content_state_sb.source_sad_rd = kLowSad;

  // nonrd thresholds
  if (tmp_sse == 0) {
    x->content_state_sb.source_sad_nonrd = kZeroSad;
    return;
  }
  if (tmp_sse < avg_source_sse_threshold_verylow)
    x->content_state_sb.source_sad_nonrd = kVeryLowSad;
  else if (tmp_sse < avg_source_sse_threshold_low[0])
    x->content_state_sb.source_sad_nonrd = kLowSad;
  else if (tmp_sse > avg_source_sse_threshold_high)
    x->content_state_sb.source_sad_nonrd = kHighSad;

  // Detect large lighting change.
  // Note: tmp_sse - tmp_variance = ((sum * sum) >> 12)
  if (tmp_variance < (tmp_sse >> 1) && (tmp_sse - tmp_variance) > sum_sq_thresh)
    x->content_state_sb.lighting_change = 1;
  if ((tmp_sse - tmp_variance) < (sum_sq_thresh >> 1))
    x->content_state_sb.low_sumdiff = 1;

  if (tmp_sse > ((avg_source_sse_threshold_high * 7) >> 3) &&
      !x->content_state_sb.lighting_change && !x->content_state_sb.low_sumdiff)
    x->sb_force_fixed_part = 0;

  if (!cpi->sf.rt_sf.use_rtc_tf || cpi->rc.high_source_sad ||
      cpi->rc.frame_source_sad > 20000 || cpi->svc.number_spatial_layers > 1)
    return;

  // In-place temporal filter. If psnr calculation is enabled, we store the
  // source for that.
  AV1_COMMON *const cm = &cpi->common;
  // Calculate n*mean^2
  const unsigned int nmean2 = tmp_sse - tmp_variance;
  const int ac_q_step = av1_ac_quant_QTX(cm->quant_params.base_qindex, 0,
                                         cm->seq_params->bit_depth);
  const PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  const int avg_q_step = av1_ac_quant_QTX(p_rc->avg_frame_qindex[INTER_FRAME],
                                          0, cm->seq_params->bit_depth);

  const unsigned int threshold =
      (cpi->sf.rt_sf.use_rtc_tf == 1)
          ? (clamp(avg_q_step, 250, 1000)) * ac_q_step
          : 250 * ac_q_step;

  // TODO(yunqing): use a weighted sum instead of averaging in filtering.
  if (tmp_variance <= threshold && nmean2 <= 15) {
    // Check neighbor blocks. If neighbor blocks aren't low-motion blocks,
    // skip temporal filtering for this block.
    MB_MODE_INFO **mi = cm->mi_params.mi_grid_base +
                        get_mi_grid_idx(&cm->mi_params, mi_row, mi_col);
    const TileInfo *const tile_info = &tile_data->tile_info;
    const int is_neighbor_blocks_low_motion = check_neighbor_blocks(
        mi, cm->mi_params.mi_stride, tile_info, mi_row, mi_col);
    if (!is_neighbor_blocks_low_motion) return;

    // Only consider 64x64 SB for now. Need to extend to 128x128 for large SB
    // size.
    // Test several nearby points. If non-zero mv exists, don't do temporal
    // filtering.
    const int is_this_blk_low_motion = fast_detect_non_zero_motion(
        cpi, src_y, src_ystride, last_src_y, last_src_ystride, mi_row, mi_col);

    if (!is_this_blk_low_motion) return;

    const int shift_x[2] = { 0, cpi->source->subsampling_x };
    const int shift_y[2] = { 0, cpi->source->subsampling_y };
    const uint8_t h = block_size_high[bsize];
    const uint8_t w = block_size_wide[bsize];

    for (int plane = 0; plane < av1_num_planes(cm); ++plane) {
      uint8_t *src = cpi->source->buffers[plane];
      const int src_stride = cpi->source->strides[plane != 0];
      uint8_t *last_src = cpi->last_source->buffers[plane];
      const int last_src_stride = cpi->last_source->strides[plane != 0];
      src += src_stride * (mi_row << (2 - shift_y[plane != 0])) +
             (mi_col << (2 - shift_x[plane != 0]));
      last_src += last_src_stride * (mi_row << (2 - shift_y[plane != 0])) +
                  (mi_col << (2 - shift_x[plane != 0]));

      for (int i = 0; i < (h >> shift_y[plane != 0]); ++i) {
        for (int j = 0; j < (w >> shift_x[plane != 0]); ++j) {
          src[j] = (last_src[j] + src[j]) >> 1;
        }
        src += src_stride;
        last_src += last_src_stride;
      }
    }
  }
}

// Memset the mbmis at the current superblock to 0
void av1_reset_mbmi(CommonModeInfoParams *const mi_params, BLOCK_SIZE sb_size,
                    int mi_row, int mi_col) {
  // size of sb in unit of mi (BLOCK_4X4)
  const int sb_size_mi = mi_size_wide[sb_size];
  const int mi_alloc_size_1d = mi_size_wide[mi_params->mi_alloc_bsize];
  // size of sb in unit of allocated mi size
  const int sb_size_alloc_mi = mi_size_wide[sb_size] / mi_alloc_size_1d;
  assert(mi_params->mi_alloc_stride % sb_size_alloc_mi == 0 &&
         "mi is not allocated as a multiple of sb!");
  assert(mi_params->mi_stride % sb_size_mi == 0 &&
         "mi_grid_base is not allocated as a multiple of sb!");

  const int mi_rows = mi_size_high[sb_size];
  for (int cur_mi_row = 0; cur_mi_row < mi_rows; cur_mi_row++) {
    assert(get_mi_grid_idx(mi_params, 0, mi_col + mi_alloc_size_1d) <
           mi_params->mi_stride);
    const int mi_grid_idx =
        get_mi_grid_idx(mi_params, mi_row + cur_mi_row, mi_col);
    const int alloc_mi_idx =
        get_alloc_mi_idx(mi_params, mi_row + cur_mi_row, mi_col);
    memset(&mi_params->mi_grid_base[mi_grid_idx], 0,
           sb_size_mi * sizeof(*mi_params->mi_grid_base));
    memset(&mi_params->tx_type_map[mi_grid_idx], 0,
           sb_size_mi * sizeof(*mi_params->tx_type_map));
    if (cur_mi_row % mi_alloc_size_1d == 0) {
      memset(&mi_params->mi_alloc[alloc_mi_idx], 0,
             sb_size_alloc_mi * sizeof(*mi_params->mi_alloc));
    }
  }
}

void av1_backup_sb_state(SB_FIRST_PASS_STATS *sb_fp_stats, const AV1_COMP *cpi,
                         ThreadData *td, const TileDataEnc *tile_data,
                         int mi_row, int mi_col) {
  MACROBLOCK *x = &td->mb;
  MACROBLOCKD *xd = &x->e_mbd;
  const TileInfo *tile_info = &tile_data->tile_info;

  const AV1_COMMON *cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  const BLOCK_SIZE sb_size = cm->seq_params->sb_size;

  xd->above_txfm_context =
      cm->above_contexts.txfm[tile_info->tile_row] + mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (mi_row & MAX_MIB_MASK);
  av1_save_context(x, &sb_fp_stats->x_ctx, mi_row, mi_col, sb_size, num_planes);

  sb_fp_stats->rd_count = td->rd_counts;
  sb_fp_stats->split_count = x->txfm_search_info.txb_split_count;

  sb_fp_stats->fc = *td->counts;

  // Don't copy in row_mt case, otherwise run into data race. No behavior change
  // in row_mt case.
  if (cpi->sf.inter_sf.inter_mode_rd_model_estimation == 1) {
    memcpy(sb_fp_stats->inter_mode_rd_models, tile_data->inter_mode_rd_models,
           sizeof(sb_fp_stats->inter_mode_rd_models));
  }

  memcpy(sb_fp_stats->thresh_freq_fact, x->thresh_freq_fact,
         sizeof(sb_fp_stats->thresh_freq_fact));

  const int alloc_mi_idx = get_alloc_mi_idx(&cm->mi_params, mi_row, mi_col);
  sb_fp_stats->current_qindex =
      cm->mi_params.mi_alloc[alloc_mi_idx].current_qindex;

#if CONFIG_INTERNAL_STATS
  memcpy(sb_fp_stats->mode_chosen_counts, cpi->mode_chosen_counts,
         sizeof(sb_fp_stats->mode_chosen_counts));
#endif  // CONFIG_INTERNAL_STATS
}

void av1_restore_sb_state(const SB_FIRST_PASS_STATS *sb_fp_stats, AV1_COMP *cpi,
                          ThreadData *td, TileDataEnc *tile_data, int mi_row,
                          int mi_col) {
  MACROBLOCK *x = &td->mb;

  const AV1_COMMON *cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  const BLOCK_SIZE sb_size = cm->seq_params->sb_size;

  av1_restore_context(x, &sb_fp_stats->x_ctx, mi_row, mi_col, sb_size,
                      num_planes);

  td->rd_counts = sb_fp_stats->rd_count;
  x->txfm_search_info.txb_split_count = sb_fp_stats->split_count;

  *td->counts = sb_fp_stats->fc;

  if (cpi->sf.inter_sf.inter_mode_rd_model_estimation == 1) {
    memcpy(tile_data->inter_mode_rd_models, sb_fp_stats->inter_mode_rd_models,
           sizeof(sb_fp_stats->inter_mode_rd_models));
  }

  memcpy(x->thresh_freq_fact, sb_fp_stats->thresh_freq_fact,
         sizeof(sb_fp_stats->thresh_freq_fact));

  const int alloc_mi_idx = get_alloc_mi_idx(&cm->mi_params, mi_row, mi_col);
  cm->mi_params.mi_alloc[alloc_mi_idx].current_qindex =
      sb_fp_stats->current_qindex;

#if CONFIG_INTERNAL_STATS
  memcpy(cpi->mode_chosen_counts, sb_fp_stats->mode_chosen_counts,
         sizeof(sb_fp_stats->mode_chosen_counts));
#endif  // CONFIG_INTERNAL_STATS
}

/*! Checks whether to skip updating the entropy cost based on tile info.
 *
 * This function contains the common code used to skip the cost update of coeff,
 * mode, mv and dv symbols.
 */
static int skip_cost_update(const SequenceHeader *seq_params,
                            const TileInfo *const tile_info, const int mi_row,
                            const int mi_col,
                            INTERNAL_COST_UPDATE_TYPE upd_level) {
  if (upd_level == INTERNAL_COST_UPD_SB) return 0;
  if (upd_level == INTERNAL_COST_UPD_OFF) return 1;

  // upd_level is at most as frequent as each sb_row in a tile.
  if (mi_col != tile_info->mi_col_start) return 1;

  if (upd_level == INTERNAL_COST_UPD_SBROW_SET) {
    const int mib_size_log2 = seq_params->mib_size_log2;
    const int sb_row = (mi_row - tile_info->mi_row_start) >> mib_size_log2;
    const int sb_size = seq_params->mib_size * MI_SIZE;
    const int tile_height =
        (tile_info->mi_row_end - tile_info->mi_row_start) * MI_SIZE;
    // When upd_level = INTERNAL_COST_UPD_SBROW_SET, the cost update happens
    // once for 2, 4 sb rows for sb size 128, sb size 64 respectively. However,
    // as the update will not be equally spaced in smaller resolutions making
    // it equally spaced by calculating (mv_num_rows_cost_update) the number of
    // rows after which the cost update should happen.
    const int sb_size_update_freq_map[2] = { 2, 4 };
    const int update_freq_sb_rows =
        sb_size_update_freq_map[sb_size != MAX_SB_SIZE];
    const int update_freq_num_rows = sb_size * update_freq_sb_rows;
    // Round-up the division result to next integer.
    const int num_updates_per_tile =
        (tile_height + update_freq_num_rows - 1) / update_freq_num_rows;
    const int num_rows_update_per_tile = num_updates_per_tile * sb_size;
    // Round-up the division result to next integer.
    const int num_sb_rows_per_update =
        (tile_height + num_rows_update_per_tile - 1) / num_rows_update_per_tile;
    if ((sb_row % num_sb_rows_per_update) != 0) return 1;
  }
  return 0;
}

// Checks for skip status of mv cost update.
static int skip_mv_cost_update(AV1_COMP *cpi, const TileInfo *const tile_info,
                               const int mi_row, const int mi_col) {
  const AV1_COMMON *cm = &cpi->common;
  // For intra frames, mv cdfs are not updated during the encode. Hence, the mv
  // cost calculation is skipped in this case.
  if (frame_is_intra_only(cm)) return 1;

  return skip_cost_update(cm->seq_params, tile_info, mi_row, mi_col,
                          cpi->sf.inter_sf.mv_cost_upd_level);
}

// Checks for skip status of dv cost update.
static int skip_dv_cost_update(AV1_COMP *cpi, const TileInfo *const tile_info,
                               const int mi_row, const int mi_col) {
  const AV1_COMMON *cm = &cpi->common;
  // Intrabc is only applicable to intra frames. So skip if intrabc is not
  // allowed.
  if (!av1_allow_intrabc(cm) || is_stat_generation_stage(cpi)) {
    return 1;
  }

  return skip_cost_update(cm->seq_params, tile_info, mi_row, mi_col,
                          cpi->sf.intra_sf.dv_cost_upd_level);
}

// Update the rate costs of some symbols according to the frequency directed
// by speed features
void av1_set_cost_upd_freq(AV1_COMP *cpi, ThreadData *td,
                           const TileInfo *const tile_info, const int mi_row,
                           const int mi_col) {
  AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;

  if (cm->features.disable_cdf_update) {
    return;
  }

  switch (cpi->sf.inter_sf.coeff_cost_upd_level) {
    case INTERNAL_COST_UPD_OFF:
    case INTERNAL_COST_UPD_TILE:  // Tile level
      break;
    case INTERNAL_COST_UPD_SBROW_SET:  // SB row set level in tile
    case INTERNAL_COST_UPD_SBROW:      // SB row level in tile
    case INTERNAL_COST_UPD_SB:         // SB level
      if (skip_cost_update(cm->seq_params, tile_info, mi_row, mi_col,
                           cpi->sf.inter_sf.coeff_cost_upd_level))
        break;
      av1_fill_coeff_costs(&x->coeff_costs, xd->tile_ctx, num_planes);
      break;
    default: assert(0);
  }

  switch (cpi->sf.inter_sf.mode_cost_upd_level) {
    case INTERNAL_COST_UPD_OFF:
    case INTERNAL_COST_UPD_TILE:  // Tile level
      break;
    case INTERNAL_COST_UPD_SBROW_SET:  // SB row set level in tile
    case INTERNAL_COST_UPD_SBROW:      // SB row level in tile
    case INTERNAL_COST_UPD_SB:         // SB level
      if (skip_cost_update(cm->seq_params, tile_info, mi_row, mi_col,
                           cpi->sf.inter_sf.mode_cost_upd_level))
        break;
      av1_fill_mode_rates(cm, &x->mode_costs, xd->tile_ctx);
      break;
    default: assert(0);
  }

  switch (cpi->sf.inter_sf.mv_cost_upd_level) {
    case INTERNAL_COST_UPD_OFF:
    case INTERNAL_COST_UPD_TILE:  // Tile level
      break;
    case INTERNAL_COST_UPD_SBROW_SET:  // SB row set level in tile
    case INTERNAL_COST_UPD_SBROW:      // SB row level in tile
    case INTERNAL_COST_UPD_SB:         // SB level
      // Checks for skip status of mv cost update.
      if (skip_mv_cost_update(cpi, tile_info, mi_row, mi_col)) break;
      av1_fill_mv_costs(&xd->tile_ctx->nmvc,
                        cm->features.cur_frame_force_integer_mv,
                        cm->features.allow_high_precision_mv, x->mv_costs);
      break;
    default: assert(0);
  }

  switch (cpi->sf.intra_sf.dv_cost_upd_level) {
    case INTERNAL_COST_UPD_OFF:
    case INTERNAL_COST_UPD_TILE:  // Tile level
      break;
    case INTERNAL_COST_UPD_SBROW_SET:  // SB row set level in tile
    case INTERNAL_COST_UPD_SBROW:      // SB row level in tile
    case INTERNAL_COST_UPD_SB:         // SB level
      // Checks for skip status of dv cost update.
      if (skip_dv_cost_update(cpi, tile_info, mi_row, mi_col)) break;
      av1_fill_dv_costs(&xd->tile_ctx->ndvc, x->dv_costs);
      break;
    default: assert(0);
  }
}

void av1_dealloc_src_diff_buf(struct macroblock *mb, int num_planes) {
  for (int plane = 0; plane < num_planes; ++plane) {
    aom_free(mb->plane[plane].src_diff);
    mb->plane[plane].src_diff = NULL;
  }
}

void av1_alloc_src_diff_buf(const struct AV1Common *cm, struct macroblock *mb) {
  const int num_planes = av1_num_planes(cm);
#ifndef NDEBUG
  for (int plane = 0; plane < num_planes; ++plane) {
    assert(!mb->plane[plane].src_diff);
  }
#endif
  for (int plane = 0; plane < num_planes; ++plane) {
    const int subsampling_xy =
        plane ? cm->seq_params->subsampling_x + cm->seq_params->subsampling_y
              : 0;
    const int sb_size = MAX_SB_SQUARE >> subsampling_xy;
    CHECK_MEM_ERROR(cm, mb->plane[plane].src_diff,
                    (int16_t *)aom_memalign(
                        32, sizeof(*mb->plane[plane].src_diff) * sb_size));
  }
}
