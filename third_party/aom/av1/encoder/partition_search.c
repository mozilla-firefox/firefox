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

#include <float.h>

#include "config/aom_config.h"

#include "aom_dsp/txfm_common.h"

#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#include "av1/common/enums.h"
#include "av1/common/reconintra.h"

#include "av1/encoder/aq_complexity.h"
#include "av1/encoder/aq_variance.h"
#include "av1/encoder/context_tree.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/encodeframe.h"
#include "av1/encoder/encodeframe_utils.h"
#include "av1/encoder/encodemv.h"
#include "av1/encoder/intra_mode_search_utils.h"
#include "av1/encoder/motion_search_facade.h"
#include "av1/encoder/nonrd_opt.h"
#include "av1/encoder/partition_search.h"
#include "av1/encoder/partition_strategy.h"
#include "av1/encoder/reconinter_enc.h"
#include "av1/encoder/tokenize.h"
#include "av1/encoder/var_based_part.h"
#include "av1/encoder/av1_ml_partition_models.h"

#if CONFIG_TUNE_VMAF
#include "av1/encoder/tune_vmaf.h"
#endif

#define COLLECT_MOTION_SEARCH_FEATURE_SB 0

#if CONFIG_PARTITION_SEARCH_ORDER
void av1_reset_part_sf(PARTITION_SPEED_FEATURES *part_sf) {
  part_sf->partition_search_type = SEARCH_PARTITION;
  part_sf->less_rectangular_check_level = 0;
  part_sf->use_square_partition_only_threshold = BLOCK_128X128;
  part_sf->auto_max_partition_based_on_simple_motion = NOT_IN_USE;
  part_sf->default_max_partition_size = BLOCK_LARGEST;
  part_sf->default_min_partition_size = BLOCK_4X4;
  part_sf->adjust_var_based_rd_partitioning = 0;
  part_sf->max_intra_bsize = BLOCK_LARGEST;
  // This setting only takes effect when partition_search_type is set
  // to FIXED_PARTITION.
  part_sf->fixed_partition_size = BLOCK_16X16;
  // Recode loop tolerance %.
  part_sf->partition_search_breakout_dist_thr = 0;
  part_sf->partition_search_breakout_rate_thr = 0;
  part_sf->prune_ext_partition_types_search_level = 0;
  part_sf->prune_part4_search = 0;
  part_sf->ml_prune_partition = 0;
  part_sf->ml_early_term_after_part_split_level = 0;
  for (int i = 0; i < PARTITION_BLOCK_SIZES; ++i) {
    part_sf->ml_partition_search_breakout_thresh[i] =
        -1;  // -1 means not enabled.
  }
  part_sf->simple_motion_search_prune_agg = SIMPLE_AGG_LVL0;
  part_sf->simple_motion_search_split = 0;
  part_sf->simple_motion_search_prune_rect = 0;
  part_sf->simple_motion_search_early_term_none = 0;
  part_sf->simple_motion_search_reduce_search_steps = 0;
  part_sf->intra_cnn_based_part_prune_level = 0;
  part_sf->ext_partition_eval_thresh = BLOCK_8X8;
  part_sf->rect_partition_eval_thresh = BLOCK_128X128;
  part_sf->ext_part_eval_based_on_cur_best = 0;
  part_sf->prune_ext_part_using_split_info = 0;
  part_sf->prune_rectangular_split_based_on_qidx = 0;
  part_sf->early_term_after_none_split = 0;
  part_sf->ml_predict_breakout_level = 0;
  part_sf->prune_sub_8x8_partition_level = 0;
  part_sf->simple_motion_search_rect_split = 0;
  part_sf->reuse_prev_rd_results_for_part_ab = 0;
  part_sf->reuse_best_prediction_for_part_ab = 0;
  part_sf->use_best_rd_for_pruning = 0;
  part_sf->skip_non_sq_part_based_on_none = 0;
}

// Reset speed features that works for the baseline encoding, but
// blocks the external partition search.
void av1_reset_sf_for_ext_part(AV1_COMP *const cpi) {
  cpi->sf.inter_sf.prune_ref_frame_for_rect_partitions = 0;
}
#endif  // CONFIG_PARTITION_SEARCH_ORDER

#if !CONFIG_REALTIME_ONLY
// If input |features| is NULL, write tpl stats to file for each super block.
// Otherwise, store tpl stats to |features|.
// The tpl stats is computed in the unit of tpl_bsize_1d (16x16).
// When writing to text file:
// The first row contains super block position, super block size,
// tpl unit length, number of units in the super block.
// The second row contains the intra prediction cost for each unit.
// The third row contains the inter prediction cost for each unit.
// The forth row contains the motion compensated dependency cost for each unit.
static void collect_tpl_stats_sb(const AV1_COMP *const cpi,
                                 const BLOCK_SIZE bsize, const int mi_row,
                                 const int mi_col,
                                 aom_partition_features_t *features) {
  const AV1_COMMON *const cm = &cpi->common;
  GF_GROUP *gf_group = &cpi->ppi->gf_group;
  if (gf_group->update_type[cpi->gf_frame_index] == INTNL_OVERLAY_UPDATE ||
      gf_group->update_type[cpi->gf_frame_index] == OVERLAY_UPDATE) {
    return;
  }

  TplParams *const tpl_data = &cpi->ppi->tpl_data;
  TplDepFrame *tpl_frame = &tpl_data->tpl_frame[cpi->gf_frame_index];
  TplDepStats *tpl_stats = tpl_frame->tpl_stats_ptr;
  // If tpl stats is not established, early return
  if (!tpl_data->ready || gf_group->max_layer_depth_allowed == 0) {
    if (features != NULL) features->sb_features.tpl_features.available = 0;
    return;
  }

  const int tpl_stride = tpl_frame->stride;
  const int step = 1 << tpl_data->tpl_stats_block_mis_log2;
  const int mi_width =
      AOMMIN(mi_size_wide[bsize], cm->mi_params.mi_cols - mi_col);
  const int mi_height =
      AOMMIN(mi_size_high[bsize], cm->mi_params.mi_rows - mi_row);
  const int col_steps = (mi_width / step) + ((mi_width % step) > 0);
  const int row_steps = (mi_height / step) + ((mi_height % step) > 0);
  const int num_blocks = col_steps * row_steps;

  if (features == NULL) {
    char filename[256];
    snprintf(filename, sizeof(filename), "%s/tpl_feature_sb%d",
             cpi->oxcf.partition_info_path, cpi->sb_counter);
    FILE *pfile = fopen(filename, "w");
    fprintf(pfile, "%d,%d,%d,%d,%d\n", mi_row, mi_col, bsize,
            tpl_data->tpl_bsize_1d, num_blocks);
    int count = 0;
    for (int row = 0; row < mi_height; row += step) {
      for (int col = 0; col < mi_width; col += step) {
        TplDepStats *this_stats =
            &tpl_stats[av1_tpl_ptr_pos(mi_row + row, mi_col + col, tpl_stride,
                                       tpl_data->tpl_stats_block_mis_log2)];
        fprintf(pfile, "%.0f", (double)this_stats->intra_cost);
        if (count < num_blocks - 1) fprintf(pfile, ",");
        ++count;
      }
    }
    fprintf(pfile, "\n");
    count = 0;
    for (int row = 0; row < mi_height; row += step) {
      for (int col = 0; col < mi_width; col += step) {
        TplDepStats *this_stats =
            &tpl_stats[av1_tpl_ptr_pos(mi_row + row, mi_col + col, tpl_stride,
                                       tpl_data->tpl_stats_block_mis_log2)];
        fprintf(pfile, "%.0f", (double)this_stats->inter_cost);
        if (count < num_blocks - 1) fprintf(pfile, ",");
        ++count;
      }
    }
    fprintf(pfile, "\n");
    count = 0;
    for (int row = 0; row < mi_height; row += step) {
      for (int col = 0; col < mi_width; col += step) {
        TplDepStats *this_stats =
            &tpl_stats[av1_tpl_ptr_pos(mi_row + row, mi_col + col, tpl_stride,
                                       tpl_data->tpl_stats_block_mis_log2)];
        const int64_t mc_dep_delta =
            RDCOST(tpl_frame->base_rdmult, this_stats->mc_dep_rate,
                   this_stats->mc_dep_dist);
        fprintf(pfile, "%.0f", (double)mc_dep_delta);
        if (count < num_blocks - 1) fprintf(pfile, ",");
        ++count;
      }
    }
    fclose(pfile);
  } else {
    features->sb_features.tpl_features.available = 1;
    features->sb_features.tpl_features.tpl_unit_length = tpl_data->tpl_bsize_1d;
    features->sb_features.tpl_features.num_units = num_blocks;
    int count = 0;
    for (int row = 0; row < mi_height; row += step) {
      for (int col = 0; col < mi_width; col += step) {
        TplDepStats *this_stats =
            &tpl_stats[av1_tpl_ptr_pos(mi_row + row, mi_col + col, tpl_stride,
                                       tpl_data->tpl_stats_block_mis_log2)];
        const int64_t mc_dep_delta =
            RDCOST(tpl_frame->base_rdmult, this_stats->mc_dep_rate,
                   this_stats->mc_dep_dist);
        features->sb_features.tpl_features.intra_cost[count] =
            this_stats->intra_cost;
        features->sb_features.tpl_features.inter_cost[count] =
            this_stats->inter_cost;
        features->sb_features.tpl_features.mc_dep_cost[count] = mc_dep_delta;
        ++count;
      }
    }
  }
}
#endif  // !CONFIG_REALTIME_ONLY

static void update_txfm_count(MACROBLOCK *x, MACROBLOCKD *xd,
                              FRAME_COUNTS *counts, TX_SIZE tx_size, int depth,
                              int blk_row, int blk_col,
                              uint8_t allow_update_cdf) {
  MB_MODE_INFO *mbmi = xd->mi[0];
  const BLOCK_SIZE bsize = mbmi->bsize;
  const int max_blocks_high = max_block_high(xd, bsize, 0);
  const int max_blocks_wide = max_block_wide(xd, bsize, 0);
  int ctx = txfm_partition_context(xd->above_txfm_context + blk_col,
                                   xd->left_txfm_context + blk_row, mbmi->bsize,
                                   tx_size);
  const int txb_size_index = av1_get_txb_size_index(bsize, blk_row, blk_col);
  const TX_SIZE plane_tx_size = mbmi->inter_tx_size[txb_size_index];

  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide) return;
  assert(tx_size > TX_4X4);

  if (depth == MAX_VARTX_DEPTH) {
    // Don't add to counts in this case
    mbmi->tx_size = tx_size;
    txfm_partition_update(xd->above_txfm_context + blk_col,
                          xd->left_txfm_context + blk_row, tx_size, tx_size);
    return;
  }

  if (tx_size == plane_tx_size) {
#if CONFIG_ENTROPY_STATS
    ++counts->txfm_partition[ctx][0];
#endif
    if (allow_update_cdf)
      update_cdf(xd->tile_ctx->txfm_partition_cdf[ctx], 0, 2);
    mbmi->tx_size = tx_size;
    txfm_partition_update(xd->above_txfm_context + blk_col,
                          xd->left_txfm_context + blk_row, tx_size, tx_size);
  } else {
    const TX_SIZE sub_txs = sub_tx_size_map[tx_size];
    const int bsw = tx_size_wide_unit[sub_txs];
    const int bsh = tx_size_high_unit[sub_txs];

#if CONFIG_ENTROPY_STATS
    ++counts->txfm_partition[ctx][1];
#endif
    if (allow_update_cdf)
      update_cdf(xd->tile_ctx->txfm_partition_cdf[ctx], 1, 2);
    ++x->txfm_search_info.txb_split_count;

    if (sub_txs == TX_4X4) {
      mbmi->inter_tx_size[txb_size_index] = TX_4X4;
      mbmi->tx_size = TX_4X4;
      txfm_partition_update(xd->above_txfm_context + blk_col,
                            xd->left_txfm_context + blk_row, TX_4X4, tx_size);
      return;
    }

    for (int row = 0; row < tx_size_high_unit[tx_size]; row += bsh) {
      for (int col = 0; col < tx_size_wide_unit[tx_size]; col += bsw) {
        int offsetr = row;
        int offsetc = col;

        update_txfm_count(x, xd, counts, sub_txs, depth + 1, blk_row + offsetr,
                          blk_col + offsetc, allow_update_cdf);
      }
    }
  }
}

static void tx_partition_count_update(const AV1_COMMON *const cm, MACROBLOCK *x,
                                      BLOCK_SIZE plane_bsize,
                                      FRAME_COUNTS *td_counts,
                                      uint8_t allow_update_cdf) {
  MACROBLOCKD *xd = &x->e_mbd;
  const int mi_width = mi_size_wide[plane_bsize];
  const int mi_height = mi_size_high[plane_bsize];
  const TX_SIZE max_tx_size = get_vartx_max_txsize(xd, plane_bsize, 0);
  const int bh = tx_size_high_unit[max_tx_size];
  const int bw = tx_size_wide_unit[max_tx_size];

  xd->above_txfm_context =
      cm->above_contexts.txfm[xd->tile.tile_row] + xd->mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (xd->mi_row & MAX_MIB_MASK);

  for (int idy = 0; idy < mi_height; idy += bh) {
    for (int idx = 0; idx < mi_width; idx += bw) {
      update_txfm_count(x, xd, td_counts, max_tx_size, 0, idy, idx,
                        allow_update_cdf);
    }
  }
}

static void set_txfm_context(MACROBLOCKD *xd, TX_SIZE tx_size, int blk_row,
                             int blk_col) {
  MB_MODE_INFO *mbmi = xd->mi[0];
  const BLOCK_SIZE bsize = mbmi->bsize;
  const int max_blocks_high = max_block_high(xd, bsize, 0);
  const int max_blocks_wide = max_block_wide(xd, bsize, 0);
  const int txb_size_index = av1_get_txb_size_index(bsize, blk_row, blk_col);
  const TX_SIZE plane_tx_size = mbmi->inter_tx_size[txb_size_index];

  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide) return;

  if (tx_size == plane_tx_size) {
    mbmi->tx_size = tx_size;
    txfm_partition_update(xd->above_txfm_context + blk_col,
                          xd->left_txfm_context + blk_row, tx_size, tx_size);

  } else {
    if (tx_size == TX_8X8) {
      mbmi->inter_tx_size[txb_size_index] = TX_4X4;
      mbmi->tx_size = TX_4X4;
      txfm_partition_update(xd->above_txfm_context + blk_col,
                            xd->left_txfm_context + blk_row, TX_4X4, tx_size);
      return;
    }
    const TX_SIZE sub_txs = sub_tx_size_map[tx_size];
    const int bsw = tx_size_wide_unit[sub_txs];
    const int bsh = tx_size_high_unit[sub_txs];
    const int row_end =
        AOMMIN(tx_size_high_unit[tx_size], max_blocks_high - blk_row);
    const int col_end =
        AOMMIN(tx_size_wide_unit[tx_size], max_blocks_wide - blk_col);
    for (int row = 0; row < row_end; row += bsh) {
      const int offsetr = blk_row + row;
      for (int col = 0; col < col_end; col += bsw) {
        const int offsetc = blk_col + col;
        set_txfm_context(xd, sub_txs, offsetr, offsetc);
      }
    }
  }
}

static void tx_partition_set_contexts(const AV1_COMMON *const cm,
                                      MACROBLOCKD *xd, BLOCK_SIZE plane_bsize) {
  const int mi_width = mi_size_wide[plane_bsize];
  const int mi_height = mi_size_high[plane_bsize];
  const TX_SIZE max_tx_size = get_vartx_max_txsize(xd, plane_bsize, 0);
  const int bh = tx_size_high_unit[max_tx_size];
  const int bw = tx_size_wide_unit[max_tx_size];

  xd->above_txfm_context =
      cm->above_contexts.txfm[xd->tile.tile_row] + xd->mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (xd->mi_row & MAX_MIB_MASK);

  for (int idy = 0; idy < mi_height; idy += bh) {
    for (int idx = 0; idx < mi_width; idx += bw) {
      set_txfm_context(xd, max_tx_size, idy, idx);
    }
  }
}

static void update_zeromv_cnt(const AV1_COMP *const cpi,
                              const MB_MODE_INFO *const mi, int mi_row,
                              int mi_col, BLOCK_SIZE bsize) {
  if (mi->ref_frame[0] != LAST_FRAME || !is_inter_block(mi) ||
      mi->segment_id > CR_SEGMENT_ID_BOOST2) {
    return;
  }
  const AV1_COMMON *const cm = &cpi->common;
  const MV mv = mi->mv[0].as_mv;
  const int bw = mi_size_wide[bsize] >> 1;
  const int bh = mi_size_high[bsize] >> 1;
  const int xmis = AOMMIN((cm->mi_params.mi_cols - mi_col) >> 1, bw);
  const int ymis = AOMMIN((cm->mi_params.mi_rows - mi_row) >> 1, bh);
  const int block_index =
      (mi_row >> 1) * (cm->mi_params.mi_cols >> 1) + (mi_col >> 1);
  for (int y = 0; y < ymis; y++) {
    for (int x = 0; x < xmis; x++) {
      // consec_zero_mv is in the scale of 8x8 blocks
      const int map_offset = block_index + y * (cm->mi_params.mi_cols >> 1) + x;
      if (abs(mv.row) < 10 && abs(mv.col) < 10) {
        if (cpi->consec_zero_mv[map_offset] < 255)
          cpi->consec_zero_mv[map_offset]++;
      } else {
        cpi->consec_zero_mv[map_offset] = 0;
      }
    }
  }
}

static void encode_superblock(const AV1_COMP *const cpi, TileDataEnc *tile_data,
                              ThreadData *td, TokenExtra **t, RUN_TYPE dry_run,
                              BLOCK_SIZE bsize, int *rate) {
  const AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO **mi_4x4 = xd->mi;
  MB_MODE_INFO *mbmi = mi_4x4[0];
  const int seg_skip =
      segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP);
  const int mis = cm->mi_params.mi_stride;
  const int mi_width = mi_size_wide[bsize];
  const int mi_height = mi_size_high[bsize];
  const int is_inter = is_inter_block(mbmi);

  // Initialize tx_mode and tx_size_search_method
  TxfmSearchParams *txfm_params = &x->txfm_search_params;
  set_tx_size_search_method(
      cm, &cpi->winner_mode_params, txfm_params,
      cpi->sf.winner_mode_sf.enable_winner_mode_for_tx_size_srch, 1);

  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  if (!is_inter) {
    xd->cfl.store_y = store_cfl_required(cm, xd);
    mbmi->skip_txfm = 1;
    for (int plane = 0; plane < num_planes; ++plane) {
      av1_encode_intra_block_plane(cpi, x, bsize, plane, dry_run,
                                   cpi->optimize_seg_arr[mbmi->segment_id]);
    }

    // If there is at least one lossless segment, force the skip for intra
    // block to be 0, in order to avoid the segment_id to be changed by in
    // write_segment_id().
    if (!cpi->common.seg.segid_preskip && cpi->common.seg.update_map &&
        cpi->enc_seg.has_lossless_segment)
      mbmi->skip_txfm = 0;

    xd->cfl.store_y = 0;
    if (av1_allow_palette(cm->features.allow_screen_content_tools, bsize)) {
      for (int plane = 0; plane < AOMMIN(2, num_planes); ++plane) {
        if (mbmi->palette_mode_info.palette_size[plane] > 0) {
          if (!dry_run) {
            av1_tokenize_color_map(x, plane, t, bsize, mbmi->tx_size,
                                   PALETTE_MAP, tile_data->allow_update_cdf,
                                   td->counts);
          } else if (dry_run == DRY_RUN_COSTCOEFFS) {
            *rate +=
                av1_cost_color_map(x, plane, bsize, mbmi->tx_size, PALETTE_MAP);
          }
        }
      }
    }

    av1_update_intra_mb_txb_context(cpi, td, dry_run, bsize,
                                    tile_data->allow_update_cdf);
  } else {
    int ref;
    const int is_compound = has_second_ref(mbmi);

    set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);
    for (ref = 0; ref < 1 + is_compound; ++ref) {
      const YV12_BUFFER_CONFIG *cfg =
          get_ref_frame_yv12_buf(cm, mbmi->ref_frame[ref]);
      assert(IMPLIES(!is_intrabc_block(mbmi), cfg));
      av1_setup_pre_planes(xd, ref, cfg, mi_row, mi_col,
                           xd->block_ref_scale_factors[ref], num_planes);
    }
    // Predicted sample of inter mode (for Luma plane) cannot be reused if
    // nonrd_check_partition_split speed feature is enabled, Since in such cases
    // the buffer may not contain the predicted sample of best mode.
    const int start_plane =
        (x->reuse_inter_pred && (!cpi->sf.rt_sf.nonrd_check_partition_split) &&
         cm->seq_params->bit_depth == AOM_BITS_8)
            ? 1
            : 0;
    av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize,
                                  start_plane, av1_num_planes(cm) - 1);
    if (mbmi->motion_mode == OBMC_CAUSAL) {
      assert(cpi->oxcf.motion_mode_cfg.enable_obmc);
      av1_build_obmc_inter_predictors_sb(cm, xd);
    }

#if CONFIG_MISMATCH_DEBUG
    if (dry_run == OUTPUT_ENABLED) {
      for (int plane = 0; plane < num_planes; ++plane) {
        const struct macroblockd_plane *pd = &xd->plane[plane];
        int pixel_c, pixel_r;
        mi_to_pixel_loc(&pixel_c, &pixel_r, mi_col, mi_row, 0, 0,
                        pd->subsampling_x, pd->subsampling_y);
        if (!is_chroma_reference(mi_row, mi_col, bsize, pd->subsampling_x,
                                 pd->subsampling_y))
          continue;
        mismatch_record_block_pre(pd->dst.buf, pd->dst.stride,
                                  cm->current_frame.order_hint, plane, pixel_c,
                                  pixel_r, pd->width, pd->height,
                                  xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH);
      }
    }
#else
    (void)num_planes;
#endif

    av1_encode_sb(cpi, x, bsize, dry_run);
    av1_tokenize_sb_vartx(cpi, td, dry_run, bsize, rate,
                          tile_data->allow_update_cdf);
  }

  if (!dry_run) {
    if (av1_allow_intrabc(cm) && is_intrabc_block(mbmi)) td->intrabc_used = 1;
    if (txfm_params->tx_mode_search_type == TX_MODE_SELECT &&
        !xd->lossless[mbmi->segment_id] && mbmi->bsize > BLOCK_4X4 &&
        !(is_inter && (mbmi->skip_txfm || seg_skip))) {
      if (is_inter) {
        tx_partition_count_update(cm, x, bsize, td->counts,
                                  tile_data->allow_update_cdf);
      } else {
        if (mbmi->tx_size != max_txsize_rect_lookup[bsize])
          ++x->txfm_search_info.txb_split_count;
        if (block_signals_txsize(bsize)) {
          const int tx_size_ctx = get_tx_size_context(xd);
          const int32_t tx_size_cat = bsize_to_tx_size_cat(bsize);
          const int depth = tx_size_to_depth(mbmi->tx_size, bsize);
          const int max_depths = bsize_to_max_depth(bsize);

          if (tile_data->allow_update_cdf)
            update_cdf(xd->tile_ctx->tx_size_cdf[tx_size_cat][tx_size_ctx],
                       depth, max_depths + 1);
#if CONFIG_ENTROPY_STATS
          ++td->counts->intra_tx_size[tx_size_cat][tx_size_ctx][depth];
#endif
        }
      }
      assert(IMPLIES(is_rect_tx(mbmi->tx_size), is_rect_tx_allowed(xd, mbmi)));
    } else {
      int i, j;
      TX_SIZE intra_tx_size;
      // The new intra coding scheme requires no change of transform size
      if (is_inter) {
        if (xd->lossless[mbmi->segment_id]) {
          intra_tx_size = TX_4X4;
        } else {
          intra_tx_size =
              tx_size_from_tx_mode(bsize, txfm_params->tx_mode_search_type);
        }
      } else {
        intra_tx_size = mbmi->tx_size;
      }

      const int cols = AOMMIN(cm->mi_params.mi_cols - mi_col, mi_width);
      const int rows = AOMMIN(cm->mi_params.mi_rows - mi_row, mi_height);
      for (j = 0; j < rows; j++) {
        for (i = 0; i < cols; i++) mi_4x4[mis * j + i]->tx_size = intra_tx_size;
      }

      if (intra_tx_size != max_txsize_rect_lookup[bsize])
        ++x->txfm_search_info.txb_split_count;
    }
  }

  if (txfm_params->tx_mode_search_type == TX_MODE_SELECT &&
      block_signals_txsize(mbmi->bsize) && is_inter &&
      !(mbmi->skip_txfm || seg_skip) && !xd->lossless[mbmi->segment_id]) {
    if (dry_run) tx_partition_set_contexts(cm, xd, bsize);
  } else {
    TX_SIZE tx_size = mbmi->tx_size;
    // The new intra coding scheme requires no change of transform size
    if (is_inter) {
      if (xd->lossless[mbmi->segment_id]) {
        tx_size = TX_4X4;
      } else {
        tx_size = tx_size_from_tx_mode(bsize, txfm_params->tx_mode_search_type);
      }
    } else {
      tx_size = (bsize > BLOCK_4X4) ? tx_size : TX_4X4;
    }
    mbmi->tx_size = tx_size;
    set_txfm_ctxs(tx_size, xd->width, xd->height,
                  (mbmi->skip_txfm || seg_skip) && is_inter_block(mbmi), xd);
  }

#if !CONFIG_REALTIME_ONLY
  if (is_inter_block(mbmi) && !xd->is_chroma_ref && is_cfl_allowed(xd)) {
    cfl_store_block(xd, mbmi->bsize, mbmi->tx_size);
  }
#endif
  if (!dry_run) {
    if (cpi->oxcf.pass == AOM_RC_ONE_PASS && cpi->svc.temporal_layer_id == 0 &&
        cpi->sf.rt_sf.use_temporal_noise_estimate &&
        (!cpi->ppi->use_svc ||
         (cpi->ppi->use_svc &&
          !cpi->svc.layer_context[cpi->svc.temporal_layer_id].is_key_frame &&
          cpi->svc.spatial_layer_id == cpi->svc.number_spatial_layers - 1)))
      update_zeromv_cnt(cpi, mbmi, mi_row, mi_col, bsize);
  }
}

static void setup_block_rdmult(const AV1_COMP *const cpi, MACROBLOCK *const x,
                               int mi_row, int mi_col, BLOCK_SIZE bsize,
                               AQ_MODE aq_mode, MB_MODE_INFO *mbmi) {
  x->rdmult = cpi->rd.RDMULT;

  if (aq_mode != NO_AQ) {
    assert(mbmi != NULL);
    if (aq_mode == VARIANCE_AQ) {
      if (cpi->vaq_refresh) {
        const int energy = bsize <= BLOCK_16X16
                               ? x->mb_energy
                               : av1_log_block_var(cpi, x, bsize);
        mbmi->segment_id = energy;
      }
      x->rdmult = set_rdmult(cpi, x, mbmi->segment_id);
    } else if (aq_mode == COMPLEXITY_AQ) {
      x->rdmult = set_rdmult(cpi, x, mbmi->segment_id);
    } else if (aq_mode == CYCLIC_REFRESH_AQ) {
      // If segment is boosted, use rdmult for that segment.
      if (cyclic_refresh_segment_id_boosted(mbmi->segment_id))
        x->rdmult = av1_cyclic_refresh_get_rdmult(cpi->cyclic_refresh);
    }
  }

#if !CONFIG_REALTIME_ONLY
  if (cpi->common.delta_q_info.delta_q_present_flag &&
      !cpi->sf.rt_sf.use_nonrd_pick_mode) {
    x->rdmult = av1_get_cb_rdmult(cpi, x, bsize, mi_row, mi_col);
  }
#endif  // !CONFIG_REALTIME_ONLY

  if (cpi->oxcf.tune_cfg.tuning == AOM_TUNE_SSIM ||
      cpi->oxcf.tune_cfg.tuning == AOM_TUNE_IQ ||
      cpi->oxcf.tune_cfg.tuning == AOM_TUNE_SSIMULACRA2) {
    av1_set_ssim_rdmult(cpi, &x->errorperbit, bsize, mi_row, mi_col,
                        &x->rdmult);
  }
#if CONFIG_SALIENCY_MAP
  else if (cpi->oxcf.tune_cfg.tuning == AOM_TUNE_VMAF_SALIENCY_MAP) {
    av1_set_saliency_map_vmaf_rdmult(cpi, &x->errorperbit,
                                     cpi->common.seq_params->sb_size, mi_row,
                                     mi_col, &x->rdmult);
  }
#endif
#if CONFIG_TUNE_VMAF
  else if (cpi->oxcf.tune_cfg.tuning == AOM_TUNE_VMAF_WITHOUT_PREPROCESSING ||
           cpi->oxcf.tune_cfg.tuning == AOM_TUNE_VMAF_MAX_GAIN ||
           cpi->oxcf.tune_cfg.tuning == AOM_TUNE_VMAF_NEG_MAX_GAIN) {
    av1_set_vmaf_rdmult(cpi, x, bsize, mi_row, mi_col, &x->rdmult);
  }
#endif
#if CONFIG_TUNE_BUTTERAUGLI
  else if (cpi->oxcf.tune_cfg.tuning == AOM_TUNE_BUTTERAUGLI) {
    av1_set_butteraugli_rdmult(cpi, x, bsize, mi_row, mi_col, &x->rdmult);
  }
#endif
  if (cpi->oxcf.mode == ALLINTRA) {
    x->rdmult = (int)(((int64_t)x->rdmult * x->intra_sb_rdmult_modifier) >> 7);
  }

  // Check to make sure that the adjustments above have not caused the
  // rd multiplier to be truncated to 0.
  x->rdmult = (x->rdmult > 0) ? x->rdmult : 1;
}

void av1_set_offsets_without_segment_id(const AV1_COMP *const cpi,
                                        const TileInfo *const tile,
                                        MACROBLOCK *const x, int mi_row,
                                        int mi_col, BLOCK_SIZE bsize) {
  const AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  assert(bsize < BLOCK_SIZES_ALL);
  const int mi_width = mi_size_wide[bsize];
  const int mi_height = mi_size_high[bsize];

  set_mode_info_offsets(&cpi->common.mi_params, &cpi->mbmi_ext_info, x, xd,
                        mi_row, mi_col);

  set_entropy_context(xd, mi_row, mi_col, num_planes);
  xd->above_txfm_context = cm->above_contexts.txfm[tile->tile_row] + mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (mi_row & MAX_MIB_MASK);

  // Set up destination pointers.
  av1_setup_dst_planes(xd->plane, bsize, &cm->cur_frame->buf, mi_row, mi_col, 0,
                       num_planes);

  // Set up limit values for MV components.
  // Mv beyond the range do not produce new/different prediction block.
  av1_set_mv_limits(&cm->mi_params, &x->mv_limits, mi_row, mi_col, mi_height,
                    mi_width, cpi->oxcf.border_in_pixels);

  set_plane_n4(xd, mi_width, mi_height, num_planes);

  // Set up distance of MB to edge of frame in 1/8th pel units.
  assert(!(mi_col & (mi_width - 1)) && !(mi_row & (mi_height - 1)));
  set_mi_row_col(xd, tile, mi_row, mi_height, mi_col, mi_width,
                 cm->mi_params.mi_rows, cm->mi_params.mi_cols);

  // Set up source buffers.
  av1_setup_src_planes(x, cpi->source, mi_row, mi_col, num_planes, bsize);

  // required by av1_append_sub8x8_mvs_for_idx() and av1_find_best_ref_mvs()
  xd->tile = *tile;
}

void av1_set_offsets(const AV1_COMP *const cpi, const TileInfo *const tile,
                     MACROBLOCK *const x, int mi_row, int mi_col,
                     BLOCK_SIZE bsize) {
  const AV1_COMMON *const cm = &cpi->common;
  const struct segmentation *const seg = &cm->seg;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi;

  av1_set_offsets_without_segment_id(cpi, tile, x, mi_row, mi_col, bsize);

  // Setup segment ID.
  mbmi = xd->mi[0];
  mbmi->segment_id = 0;
  if (seg->enabled) {
    if (seg->enabled && !cpi->vaq_refresh) {
      const uint8_t *const map =
          seg->update_map ? cpi->enc_seg.map : cm->last_frame_seg_map;
      mbmi->segment_id =
          map ? get_segment_id(&cm->mi_params, map, bsize, mi_row, mi_col) : 0;
    }
    av1_init_plane_quantizers(cpi, x, mbmi->segment_id, 0);
  }
#ifndef NDEBUG
  x->last_set_offsets_loc.mi_row = mi_row;
  x->last_set_offsets_loc.mi_col = mi_col;
  x->last_set_offsets_loc.bsize = bsize;
#endif  // NDEBUG
}

/*!\brief Hybrid intra mode search.
 *
 * \ingroup intra_mode_search
 * \callgraph
 * \callergraph
 * This is top level function for mode search for intra frames in non-RD
 * optimized case. Depending on speed feature and block size it calls
 * either non-RD or RD optimized intra mode search.
 *
 * \param[in]    cpi            Top-level encoder structure
 * \param[in]    x              Pointer to structure holding all the data for
                                the current macroblock
 * \param[in]    rd_cost        Struct to keep track of the RD information
 * \param[in]    bsize          Current block size
 * \param[in]    ctx            Structure to hold snapshot of coding context
                                during the mode picking process
 *
 * \remark Nothing is returned. Instead, the MB_MODE_INFO struct inside x
 * is modified to store information about the best mode computed
 * in this function. The rd_cost struct is also updated with the RD stats
 * corresponding to the best mode found.
 */

static inline void hybrid_intra_mode_search(AV1_COMP *cpi, MACROBLOCK *const x,
                                            RD_STATS *rd_cost, BLOCK_SIZE bsize,
                                            PICK_MODE_CONTEXT *ctx) {
  int use_rdopt = 0;
  const int hybrid_intra_pickmode = cpi->sf.rt_sf.hybrid_intra_pickmode;
  // Use rd pick for intra mode search based on block size and variance.
  if (hybrid_intra_pickmode && bsize < BLOCK_16X16) {
    unsigned int var_thresh[3] = { 0, 101, 201 };
    assert(hybrid_intra_pickmode <= 3);
    if (x->source_variance >= var_thresh[hybrid_intra_pickmode - 1])
      use_rdopt = 1;
  }

  if (use_rdopt)
    av1_rd_pick_intra_mode_sb(cpi, x, rd_cost, bsize, ctx, INT64_MAX);
  else
    av1_nonrd_pick_intra_mode(cpi, x, rd_cost, bsize, ctx);
}

// For real time/allintra row-mt enabled multi-threaded encoding with cost
// update frequency set to COST_UPD_TILE/COST_UPD_OFF, tile ctxt is not updated
// at superblock level. Thus, it is not required for the encoding of top-right
// superblock be complete for updating tile ctxt. However, when encoding a block
// whose right edge is also the superblock edge, intra and inter mode evaluation
// (ref mv list population) require the encoding of the top-right superblock to
// be complete. So, here, we delay the waiting of threads until the need for the
// data from the top-right superblock region.
static inline void wait_for_top_right_sb(AV1EncRowMultiThreadInfo *enc_row_mt,
                                         AV1EncRowMultiThreadSync *row_mt_sync,
                                         TileInfo *tile_info,
                                         BLOCK_SIZE sb_size,
                                         int sb_mi_size_log2, BLOCK_SIZE bsize,
                                         int mi_row, int mi_col) {
  const int sb_size_in_mi = mi_size_wide[sb_size];
  const int bw_in_mi = mi_size_wide[bsize];
  const int blk_row_in_sb = mi_row & (sb_size_in_mi - 1);
  const int blk_col_in_sb = mi_col & (sb_size_in_mi - 1);
  const int top_right_block_in_sb =
      (blk_row_in_sb == 0) && (blk_col_in_sb + bw_in_mi >= sb_size_in_mi);

  // Don't wait if the block is the not the top-right block in the superblock.
  if (!top_right_block_in_sb) return;

  // Wait for the top-right superblock to finish encoding.
  const int sb_row_in_tile =
      (mi_row - tile_info->mi_row_start) >> sb_mi_size_log2;
  const int sb_col_in_tile =
      (mi_col - tile_info->mi_col_start) >> sb_mi_size_log2;

  enc_row_mt->sync_read_ptr(row_mt_sync, sb_row_in_tile, sb_col_in_tile);
}

/*!\brief Interface for AV1 mode search for an individual coding block
 *
 * \ingroup partition_search
 * \callgraph
 * \callergraph
 * Searches prediction modes, transform, and coefficient coding modes for an
 * individual coding block. This function is the top-level interface that
 * directs the encoder to the proper mode search function, among these
 * implemented for inter/intra + rd/non-rd + non-skip segment/skip segment.
 *
 * \param[in]    cpi            Top-level encoder structure
 * \param[in]    tile_data      Pointer to struct holding adaptive
 *                              data/contexts/models for the tile during
 *                              encoding
 * \param[in]    x              Pointer to structure holding all the data for
 *                              the current macroblock
 * \param[in]    mi_row         Row coordinate of the block in a step size of
 *                              MI_SIZE
 * \param[in]    mi_col         Column coordinate of the block in a step size of
 *                              MI_SIZE
 * \param[in]    rd_cost        Pointer to structure holding rate and distortion
 *                              stats for the current block
 * \param[in]    partition      Partition mode of the parent block
 * \param[in]    bsize          Current block size
 * \param[in]    ctx            Pointer to structure holding coding contexts and
 *                              chosen modes for the current block
 * \param[in]    best_rd        Upper bound of rd cost of a valid partition
 *
 * \remark Nothing is returned. Instead, the chosen modes and contexts necessary
 * for reconstruction are stored in ctx, the rate-distortion stats are stored in
 * rd_cost. If no valid mode leading to rd_cost <= best_rd, the status will be
 * signalled by an INT64_MAX rd_cost->rdcost.
 */
static void pick_sb_modes(AV1_COMP *const cpi, TileDataEnc *tile_data,
                          MACROBLOCK *const x, int mi_row, int mi_col,
                          RD_STATS *rd_cost, PARTITION_TYPE partition,
                          BLOCK_SIZE bsize, PICK_MODE_CONTEXT *ctx,
                          RD_STATS best_rd) {
  if (cpi->sf.part_sf.use_best_rd_for_pruning && best_rd.rdcost < 0) {
    ctx->rd_stats.rdcost = INT64_MAX;
    ctx->rd_stats.skip_txfm = 0;
    av1_invalid_rd_stats(rd_cost);
    return;
  }

  av1_set_offsets(cpi, &tile_data->tile_info, x, mi_row, mi_col, bsize);

  if (cpi->sf.part_sf.reuse_prev_rd_results_for_part_ab &&
      ctx->rd_mode_is_ready) {
    assert(ctx->mic.bsize == bsize);
    assert(ctx->mic.partition == partition);
    rd_cost->rate = ctx->rd_stats.rate;
    rd_cost->dist = ctx->rd_stats.dist;
    rd_cost->rdcost = ctx->rd_stats.rdcost;
    return;
  }

  AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi;
  struct macroblock_plane *const p = x->plane;
  struct macroblockd_plane *const pd = xd->plane;
  const AQ_MODE aq_mode = cpi->oxcf.q_cfg.aq_mode;
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;

  int i;

  // This is only needed for real time/allintra row-mt enabled multi-threaded
  // encoding with cost update frequency set to COST_UPD_TILE/COST_UPD_OFF.
  wait_for_top_right_sb(&cpi->mt_info.enc_row_mt, &tile_data->row_mt_sync,
                        &tile_data->tile_info, cm->seq_params->sb_size,
                        cm->seq_params->mib_size_log2, bsize, mi_row, mi_col);

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, rd_pick_sb_modes_time);
#endif

  mbmi = xd->mi[0];
  mbmi->bsize = bsize;
  mbmi->partition = partition;

#if CONFIG_RD_DEBUG
  mbmi->mi_row = mi_row;
  mbmi->mi_col = mi_col;
#endif

  // Sets up the tx_type_map buffer in MACROBLOCKD.
  xd->tx_type_map = txfm_info->tx_type_map_;
  xd->tx_type_map_stride = mi_size_wide[bsize];

  for (i = 0; i < num_planes; ++i) {
    p[i].coeff = ctx->coeff[i];
    p[i].qcoeff = ctx->qcoeff[i];
    p[i].dqcoeff = ctx->dqcoeff[i];
    p[i].eobs = ctx->eobs[i];
    p[i].txb_entropy_ctx = ctx->txb_entropy_ctx[i];
  }

  for (i = 0; i < 2; ++i) pd[i].color_index_map = ctx->color_index_map[i];

  ctx->skippable = 0;
  // Set to zero to make sure we do not use the previous encoded frame stats
  mbmi->skip_txfm = 0;
  // Reset skip mode flag.
  mbmi->skip_mode = 0;

  x->source_variance = av1_get_perpixel_variance_facade(
      cpi, xd, &x->plane[0].src, bsize, AOM_PLANE_Y);

  // Initialize default mode evaluation params
  set_mode_eval_params(cpi, x, DEFAULT_EVAL);

  // Save rdmult before it might be changed, so it can be restored later.
  const int orig_rdmult = x->rdmult;
  setup_block_rdmult(cpi, x, mi_row, mi_col, bsize, aq_mode, mbmi);
  // Set error per bit for current rdmult
  av1_set_error_per_bit(&x->errorperbit, x->rdmult);
  av1_rd_cost_update(x->rdmult, &best_rd);

  // If set best_rd.rdcost to INT64_MAX, the encoder will not use any previous
  // rdcost information for the following mode search.
  // Disabling the feature could get some coding gain, with encoder slowdown.
  if (!cpi->sf.part_sf.use_best_rd_for_pruning) {
    av1_invalid_rd_stats(&best_rd);
  }

  // Find best coding mode & reconstruct the MB so it is available
  // as a predictor for MBs that follow in the SB
  if (frame_is_intra_only(cm)) {
#if CONFIG_COLLECT_COMPONENT_TIMING
    start_timing(cpi, av1_rd_pick_intra_mode_sb_time);
#endif
    av1_rd_pick_intra_mode_sb(cpi, x, rd_cost, bsize, ctx, best_rd.rdcost);
#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(cpi, av1_rd_pick_intra_mode_sb_time);
#endif
  } else {
#if CONFIG_COLLECT_COMPONENT_TIMING
    start_timing(cpi, av1_rd_pick_inter_mode_sb_time);
#endif
    if (segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP)) {
      av1_rd_pick_inter_mode_sb_seg_skip(cpi, tile_data, x, mi_row, mi_col,
                                         rd_cost, bsize, ctx, best_rd.rdcost);
    } else {
      av1_rd_pick_inter_mode(cpi, tile_data, x, rd_cost, bsize, ctx,
                             best_rd.rdcost);
    }
#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(cpi, av1_rd_pick_inter_mode_sb_time);
#endif
  }

  // Examine the resulting rate and for AQ mode 2 make a segment choice.
  if (rd_cost->rate != INT_MAX && aq_mode == COMPLEXITY_AQ &&
      bsize >= BLOCK_16X16) {
    av1_caq_select_segment(cpi, x, bsize, mi_row, mi_col, rd_cost->rate);
  }

  x->rdmult = orig_rdmult;

  // TODO(jingning) The rate-distortion optimization flow needs to be
  // refactored to provide proper exit/return handle.
  if (rd_cost->rate == INT_MAX) rd_cost->rdcost = INT64_MAX;

  ctx->rd_stats.rate = rd_cost->rate;
  ctx->rd_stats.dist = rd_cost->dist;
  ctx->rd_stats.rdcost = rd_cost->rdcost;

#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, rd_pick_sb_modes_time);
#endif
}

static void update_stats(const AV1_COMMON *const cm, ThreadData *td) {
  MACROBLOCK *x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const MB_MODE_INFO_EXT *const mbmi_ext = &x->mbmi_ext;
  const CurrentFrame *const current_frame = &cm->current_frame;
  const BLOCK_SIZE bsize = mbmi->bsize;
  FRAME_CONTEXT *fc = xd->tile_ctx;
  const int seg_ref_active =
      segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_REF_FRAME);

  if (current_frame->skip_mode_info.skip_mode_flag && !seg_ref_active &&
      is_comp_ref_allowed(bsize)) {
    const int skip_mode_ctx = av1_get_skip_mode_context(xd);
#if CONFIG_ENTROPY_STATS
    td->counts->skip_mode[skip_mode_ctx][mbmi->skip_mode]++;
#endif
    update_cdf(fc->skip_mode_cdfs[skip_mode_ctx], mbmi->skip_mode, 2);
  }

  if (!mbmi->skip_mode && !seg_ref_active) {
    const int skip_ctx = av1_get_skip_txfm_context(xd);
#if CONFIG_ENTROPY_STATS
    td->counts->skip_txfm[skip_ctx][mbmi->skip_txfm]++;
#endif
    update_cdf(fc->skip_txfm_cdfs[skip_ctx], mbmi->skip_txfm, 2);
  }

#if CONFIG_ENTROPY_STATS
  // delta quant applies to both intra and inter
  const int super_block_upper_left =
      ((xd->mi_row & (cm->seq_params->mib_size - 1)) == 0) &&
      ((xd->mi_col & (cm->seq_params->mib_size - 1)) == 0);
  const DeltaQInfo *const delta_q_info = &cm->delta_q_info;
  if (delta_q_info->delta_q_present_flag &&
      (bsize != cm->seq_params->sb_size || !mbmi->skip_txfm) &&
      super_block_upper_left) {
    const int dq = (mbmi->current_qindex - xd->current_base_qindex) /
                   delta_q_info->delta_q_res;
    const int absdq = abs(dq);
    for (int i = 0; i < AOMMIN(absdq, DELTA_Q_SMALL); ++i) {
      td->counts->delta_q[i][1]++;
    }
    if (absdq < DELTA_Q_SMALL) td->counts->delta_q[absdq][0]++;
    if (delta_q_info->delta_lf_present_flag) {
      if (delta_q_info->delta_lf_multi) {
        const int frame_lf_count =
            av1_num_planes(cm) > 1 ? FRAME_LF_COUNT : FRAME_LF_COUNT - 2;
        for (int lf_id = 0; lf_id < frame_lf_count; ++lf_id) {
          const int delta_lf = (mbmi->delta_lf[lf_id] - xd->delta_lf[lf_id]) /
                               delta_q_info->delta_lf_res;
          const int abs_delta_lf = abs(delta_lf);
          for (int i = 0; i < AOMMIN(abs_delta_lf, DELTA_LF_SMALL); ++i) {
            td->counts->delta_lf_multi[lf_id][i][1]++;
          }
          if (abs_delta_lf < DELTA_LF_SMALL)
            td->counts->delta_lf_multi[lf_id][abs_delta_lf][0]++;
        }
      } else {
        const int delta_lf =
            (mbmi->delta_lf_from_base - xd->delta_lf_from_base) /
            delta_q_info->delta_lf_res;
        const int abs_delta_lf = abs(delta_lf);
        for (int i = 0; i < AOMMIN(abs_delta_lf, DELTA_LF_SMALL); ++i) {
          td->counts->delta_lf[i][1]++;
        }
        if (abs_delta_lf < DELTA_LF_SMALL)
          td->counts->delta_lf[abs_delta_lf][0]++;
      }
    }
  }
#endif

  if (!is_inter_block(mbmi)) {
    av1_sum_intra_stats(cm, td->counts, xd, mbmi, xd->above_mbmi, xd->left_mbmi,
                        frame_is_intra_only(cm));
  }

  if (av1_allow_intrabc(cm)) {
    const int is_intrabc = is_intrabc_block(mbmi);
    update_cdf(fc->intrabc_cdf, is_intrabc, 2);
#if CONFIG_ENTROPY_STATS
    ++td->counts->intrabc[is_intrabc];
#endif  // CONFIG_ENTROPY_STATS
    if (is_intrabc) {
      const int8_t ref_frame_type = av1_ref_frame_type(mbmi->ref_frame);
      const int_mv dv_ref = mbmi_ext->ref_mv_stack[ref_frame_type][0].this_mv;
      av1_update_mv_stats(&mbmi->mv[0].as_mv, &dv_ref.as_mv, &fc->ndvc,
                          MV_SUBPEL_NONE);
    }
  }

  if (frame_is_intra_only(cm) || mbmi->skip_mode) return;

  FRAME_COUNTS *const counts = td->counts;
  const int inter_block = is_inter_block(mbmi);

  if (!seg_ref_active) {
#if CONFIG_ENTROPY_STATS
    counts->intra_inter[av1_get_intra_inter_context(xd)][inter_block]++;
#endif
    update_cdf(fc->intra_inter_cdf[av1_get_intra_inter_context(xd)],
               inter_block, 2);
    // If the segment reference feature is enabled we have only a single
    // reference frame allowed for the segment so exclude it from
    // the reference frame counts used to work out probabilities.
    if (inter_block) {
      const MV_REFERENCE_FRAME ref0 = mbmi->ref_frame[0];
      const MV_REFERENCE_FRAME ref1 = mbmi->ref_frame[1];
      if (current_frame->reference_mode == REFERENCE_MODE_SELECT) {
        if (is_comp_ref_allowed(bsize)) {
#if CONFIG_ENTROPY_STATS
          counts->comp_inter[av1_get_reference_mode_context(xd)]
                            [has_second_ref(mbmi)]++;
#endif  // CONFIG_ENTROPY_STATS
          update_cdf(av1_get_reference_mode_cdf(xd), has_second_ref(mbmi), 2);
        }
      }

      if (has_second_ref(mbmi)) {
        const COMP_REFERENCE_TYPE comp_ref_type = has_uni_comp_refs(mbmi)
                                                      ? UNIDIR_COMP_REFERENCE
                                                      : BIDIR_COMP_REFERENCE;
        update_cdf(av1_get_comp_reference_type_cdf(xd), comp_ref_type,
                   COMP_REFERENCE_TYPES);
#if CONFIG_ENTROPY_STATS
        counts->comp_ref_type[av1_get_comp_reference_type_context(xd)]
                             [comp_ref_type]++;
#endif  // CONFIG_ENTROPY_STATS

        if (comp_ref_type == UNIDIR_COMP_REFERENCE) {
          const int bit = (ref0 == BWDREF_FRAME);
          update_cdf(av1_get_pred_cdf_uni_comp_ref_p(xd), bit, 2);
#if CONFIG_ENTROPY_STATS
          counts
              ->uni_comp_ref[av1_get_pred_context_uni_comp_ref_p(xd)][0][bit]++;
#endif  // CONFIG_ENTROPY_STATS
          if (!bit) {
            const int bit1 = (ref1 == LAST3_FRAME || ref1 == GOLDEN_FRAME);
            update_cdf(av1_get_pred_cdf_uni_comp_ref_p1(xd), bit1, 2);
#if CONFIG_ENTROPY_STATS
            counts->uni_comp_ref[av1_get_pred_context_uni_comp_ref_p1(xd)][1]
                                [bit1]++;
#endif  // CONFIG_ENTROPY_STATS
            if (bit1) {
              update_cdf(av1_get_pred_cdf_uni_comp_ref_p2(xd),
                         ref1 == GOLDEN_FRAME, 2);
#if CONFIG_ENTROPY_STATS
              counts->uni_comp_ref[av1_get_pred_context_uni_comp_ref_p2(xd)][2]
                                  [ref1 == GOLDEN_FRAME]++;
#endif  // CONFIG_ENTROPY_STATS
            }
          }
        } else {
          const int bit = (ref0 == GOLDEN_FRAME || ref0 == LAST3_FRAME);
          update_cdf(av1_get_pred_cdf_comp_ref_p(xd), bit, 2);
#if CONFIG_ENTROPY_STATS
          counts->comp_ref[av1_get_pred_context_comp_ref_p(xd)][0][bit]++;
#endif  // CONFIG_ENTROPY_STATS
          if (!bit) {
            update_cdf(av1_get_pred_cdf_comp_ref_p1(xd), ref0 == LAST2_FRAME,
                       2);
#if CONFIG_ENTROPY_STATS
            counts->comp_ref[av1_get_pred_context_comp_ref_p1(xd)][1]
                            [ref0 == LAST2_FRAME]++;
#endif  // CONFIG_ENTROPY_STATS
          } else {
            update_cdf(av1_get_pred_cdf_comp_ref_p2(xd), ref0 == GOLDEN_FRAME,
                       2);
#if CONFIG_ENTROPY_STATS
            counts->comp_ref[av1_get_pred_context_comp_ref_p2(xd)][2]
                            [ref0 == GOLDEN_FRAME]++;
#endif  // CONFIG_ENTROPY_STATS
          }
          update_cdf(av1_get_pred_cdf_comp_bwdref_p(xd), ref1 == ALTREF_FRAME,
                     2);
#if CONFIG_ENTROPY_STATS
          counts->comp_bwdref[av1_get_pred_context_comp_bwdref_p(xd)][0]
                             [ref1 == ALTREF_FRAME]++;
#endif  // CONFIG_ENTROPY_STATS
          if (ref1 != ALTREF_FRAME) {
            update_cdf(av1_get_pred_cdf_comp_bwdref_p1(xd),
                       ref1 == ALTREF2_FRAME, 2);
#if CONFIG_ENTROPY_STATS
            counts->comp_bwdref[av1_get_pred_context_comp_bwdref_p1(xd)][1]
                               [ref1 == ALTREF2_FRAME]++;
#endif  // CONFIG_ENTROPY_STATS
          }
        }
      } else {
        const int bit = (ref0 >= BWDREF_FRAME);
        update_cdf(av1_get_pred_cdf_single_ref_p1(xd), bit, 2);
#if CONFIG_ENTROPY_STATS
        counts->single_ref[av1_get_pred_context_single_ref_p1(xd)][0][bit]++;
#endif  // CONFIG_ENTROPY_STATS
        if (bit) {
          assert(ref0 <= ALTREF_FRAME);
          update_cdf(av1_get_pred_cdf_single_ref_p2(xd), ref0 == ALTREF_FRAME,
                     2);
#if CONFIG_ENTROPY_STATS
          counts->single_ref[av1_get_pred_context_single_ref_p2(xd)][1]
                            [ref0 == ALTREF_FRAME]++;
#endif  // CONFIG_ENTROPY_STATS
          if (ref0 != ALTREF_FRAME) {
            update_cdf(av1_get_pred_cdf_single_ref_p6(xd),
                       ref0 == ALTREF2_FRAME, 2);
#if CONFIG_ENTROPY_STATS
            counts->single_ref[av1_get_pred_context_single_ref_p6(xd)][5]
                              [ref0 == ALTREF2_FRAME]++;
#endif  // CONFIG_ENTROPY_STATS
          }
        } else {
          const int bit1 = !(ref0 == LAST2_FRAME || ref0 == LAST_FRAME);
          update_cdf(av1_get_pred_cdf_single_ref_p3(xd), bit1, 2);
#if CONFIG_ENTROPY_STATS
          counts->single_ref[av1_get_pred_context_single_ref_p3(xd)][2][bit1]++;
#endif  // CONFIG_ENTROPY_STATS
          if (!bit1) {
            update_cdf(av1_get_pred_cdf_single_ref_p4(xd), ref0 != LAST_FRAME,
                       2);
#if CONFIG_ENTROPY_STATS
            counts->single_ref[av1_get_pred_context_single_ref_p4(xd)][3]
                              [ref0 != LAST_FRAME]++;
#endif  // CONFIG_ENTROPY_STATS
          } else {
            update_cdf(av1_get_pred_cdf_single_ref_p5(xd), ref0 != LAST3_FRAME,
                       2);
#if CONFIG_ENTROPY_STATS
            counts->single_ref[av1_get_pred_context_single_ref_p5(xd)][4]
                              [ref0 != LAST3_FRAME]++;
#endif  // CONFIG_ENTROPY_STATS
          }
        }
      }

      if (cm->seq_params->enable_interintra_compound &&
          is_interintra_allowed(mbmi)) {
        const int bsize_group = size_group_lookup[bsize];
        if (mbmi->ref_frame[1] == INTRA_FRAME) {
#if CONFIG_ENTROPY_STATS
          counts->interintra[bsize_group][1]++;
#endif
          update_cdf(fc->interintra_cdf[bsize_group], 1, 2);
#if CONFIG_ENTROPY_STATS
          counts->interintra_mode[bsize_group][mbmi->interintra_mode]++;
#endif
          update_cdf(fc->interintra_mode_cdf[bsize_group],
                     mbmi->interintra_mode, INTERINTRA_MODES);
          if (av1_is_wedge_used(bsize)) {
#if CONFIG_ENTROPY_STATS
            counts->wedge_interintra[bsize][mbmi->use_wedge_interintra]++;
#endif
            update_cdf(fc->wedge_interintra_cdf[bsize],
                       mbmi->use_wedge_interintra, 2);
            if (mbmi->use_wedge_interintra) {
#if CONFIG_ENTROPY_STATS
              counts->wedge_idx[bsize][mbmi->interintra_wedge_index]++;
#endif
              update_cdf(fc->wedge_idx_cdf[bsize], mbmi->interintra_wedge_index,
                         16);
            }
          }
        } else {
#if CONFIG_ENTROPY_STATS
          counts->interintra[bsize_group][0]++;
#endif
          update_cdf(fc->interintra_cdf[bsize_group], 0, 2);
        }
      }

      const MOTION_MODE motion_allowed =
          cm->features.switchable_motion_mode
              ? motion_mode_allowed(xd->global_motion, xd, mbmi,
                                    cm->features.allow_warped_motion)
              : SIMPLE_TRANSLATION;
      if (mbmi->ref_frame[1] != INTRA_FRAME) {
        if (motion_allowed == WARPED_CAUSAL) {
#if CONFIG_ENTROPY_STATS
          counts->motion_mode[bsize][mbmi->motion_mode]++;
#endif
          update_cdf(fc->motion_mode_cdf[bsize], mbmi->motion_mode,
                     MOTION_MODES);
        } else if (motion_allowed == OBMC_CAUSAL) {
#if CONFIG_ENTROPY_STATS
          counts->obmc[bsize][mbmi->motion_mode == OBMC_CAUSAL]++;
#endif
          update_cdf(fc->obmc_cdf[bsize], mbmi->motion_mode == OBMC_CAUSAL, 2);
        }
      }

      if (has_second_ref(mbmi)) {
        assert(current_frame->reference_mode != SINGLE_REFERENCE &&
               is_inter_compound_mode(mbmi->mode) &&
               mbmi->motion_mode == SIMPLE_TRANSLATION);

        const int masked_compound_used = is_any_masked_compound_used(bsize) &&
                                         cm->seq_params->enable_masked_compound;
        if (masked_compound_used) {
          const int comp_group_idx_ctx = get_comp_group_idx_context(xd);
#if CONFIG_ENTROPY_STATS
          ++counts->comp_group_idx[comp_group_idx_ctx][mbmi->comp_group_idx];
#endif
          update_cdf(fc->comp_group_idx_cdf[comp_group_idx_ctx],
                     mbmi->comp_group_idx, 2);
        }

        if (mbmi->comp_group_idx == 0) {
          const int comp_index_ctx = get_comp_index_context(cm, xd);
#if CONFIG_ENTROPY_STATS
          ++counts->compound_index[comp_index_ctx][mbmi->compound_idx];
#endif
          update_cdf(fc->compound_index_cdf[comp_index_ctx], mbmi->compound_idx,
                     2);
        } else {
          assert(masked_compound_used);
          if (is_interinter_compound_used(COMPOUND_WEDGE, bsize)) {
#if CONFIG_ENTROPY_STATS
            ++counts->compound_type[bsize][mbmi->interinter_comp.type -
                                           COMPOUND_WEDGE];
#endif
            update_cdf(fc->compound_type_cdf[bsize],
                       mbmi->interinter_comp.type - COMPOUND_WEDGE,
                       MASKED_COMPOUND_TYPES);
          }
        }
      }
      if (mbmi->interinter_comp.type == COMPOUND_WEDGE) {
        if (is_interinter_compound_used(COMPOUND_WEDGE, bsize)) {
#if CONFIG_ENTROPY_STATS
          counts->wedge_idx[bsize][mbmi->interinter_comp.wedge_index]++;
#endif
          update_cdf(fc->wedge_idx_cdf[bsize],
                     mbmi->interinter_comp.wedge_index, 16);
        }
      }
    }
  }

  if (inter_block && cm->features.interp_filter == SWITCHABLE &&
      av1_is_interp_needed(xd)) {
    update_filter_type_cdf(xd, mbmi, cm->seq_params->enable_dual_filter);
  }
  if (inter_block &&
      !segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP)) {
    const PREDICTION_MODE mode = mbmi->mode;
    const int16_t mode_ctx =
        av1_mode_context_analyzer(mbmi_ext->mode_context, mbmi->ref_frame);
    if (has_second_ref(mbmi)) {
#if CONFIG_ENTROPY_STATS
      ++counts->inter_compound_mode[mode_ctx][INTER_COMPOUND_OFFSET(mode)];
#endif
      update_cdf(fc->inter_compound_mode_cdf[mode_ctx],
                 INTER_COMPOUND_OFFSET(mode), INTER_COMPOUND_MODES);
    } else {
      av1_update_inter_mode_stats(fc, counts, mode, mode_ctx);
    }

    const int new_mv = mbmi->mode == NEWMV || mbmi->mode == NEW_NEWMV;
    if (new_mv) {
      const uint8_t ref_frame_type = av1_ref_frame_type(mbmi->ref_frame);
      for (int idx = 0; idx < 2; ++idx) {
        if (mbmi_ext->ref_mv_count[ref_frame_type] > idx + 1) {
          const uint8_t drl_ctx =
              av1_drl_ctx(mbmi_ext->weight[ref_frame_type], idx);
          update_cdf(fc->drl_cdf[drl_ctx], mbmi->ref_mv_idx != idx, 2);
#if CONFIG_ENTROPY_STATS
          ++counts->drl_mode[drl_ctx][mbmi->ref_mv_idx != idx];
#endif
          if (mbmi->ref_mv_idx == idx) break;
        }
      }
    }

    if (have_nearmv_in_inter_mode(mbmi->mode)) {
      const uint8_t ref_frame_type = av1_ref_frame_type(mbmi->ref_frame);
      for (int idx = 1; idx < 3; ++idx) {
        if (mbmi_ext->ref_mv_count[ref_frame_type] > idx + 1) {
          const uint8_t drl_ctx =
              av1_drl_ctx(mbmi_ext->weight[ref_frame_type], idx);
          update_cdf(fc->drl_cdf[drl_ctx], mbmi->ref_mv_idx != idx - 1, 2);
#if CONFIG_ENTROPY_STATS
          ++counts->drl_mode[drl_ctx][mbmi->ref_mv_idx != idx - 1];
#endif
          if (mbmi->ref_mv_idx == idx - 1) break;
        }
      }
    }
    if (have_newmv_in_inter_mode(mbmi->mode)) {
      const int allow_hp = cm->features.cur_frame_force_integer_mv
                               ? MV_SUBPEL_NONE
                               : cm->features.allow_high_precision_mv;
      if (new_mv) {
        for (int ref = 0; ref < 1 + has_second_ref(mbmi); ++ref) {
          const int_mv ref_mv = av1_get_ref_mv(x, ref);
          av1_update_mv_stats(&mbmi->mv[ref].as_mv, &ref_mv.as_mv, &fc->nmvc,
                              allow_hp);
        }
      } else if (mbmi->mode == NEAREST_NEWMV || mbmi->mode == NEAR_NEWMV) {
        const int ref = 1;
        const int_mv ref_mv = av1_get_ref_mv(x, ref);
        av1_update_mv_stats(&mbmi->mv[ref].as_mv, &ref_mv.as_mv, &fc->nmvc,
                            allow_hp);
      } else if (mbmi->mode == NEW_NEARESTMV || mbmi->mode == NEW_NEARMV) {
        const int ref = 0;
        const int_mv ref_mv = av1_get_ref_mv(x, ref);
        av1_update_mv_stats(&mbmi->mv[ref].as_mv, &ref_mv.as_mv, &fc->nmvc,
                            allow_hp);
      }
    }
  }
}

/*!\brief Reconstructs an individual coding block
 *
 * \ingroup partition_search
 * Reconstructs an individual coding block by applying the chosen modes stored
 * in ctx, also updates mode counts and entropy models.
 *
 * \param[in]    cpi       Top-level encoder structure
 * \param[in]    tile_data Pointer to struct holding adaptive
 *                         data/contexts/models for the tile during encoding
 * \param[in]    td        Pointer to thread data
 * \param[in]    tp        Pointer to the starting token
 * \param[in]    mi_row    Row coordinate of the block in a step size of MI_SIZE
 * \param[in]    mi_col    Column coordinate of the block in a step size of
 *                         MI_SIZE
 * \param[in]    dry_run   A code indicating whether it is part of the final
 *                         pass for reconstructing the superblock
 * \param[in]    bsize     Current block size
 * \param[in]    partition Partition mode of the parent block
 * \param[in]    ctx       Pointer to structure holding coding contexts and the
 *                         chosen modes for the current block
 * \param[in]    rate      Pointer to the total rate for the current block
 *
 * \remark Nothing is returned. Instead, reconstructions (w/o in-loop filters)
 * will be updated in the pixel buffers in td->mb.e_mbd. Also, the chosen modes
 * will be stored in the MB_MODE_INFO buffer td->mb.e_mbd.mi[0].
 */
static void encode_b(const AV1_COMP *const cpi, TileDataEnc *tile_data,
                     ThreadData *td, TokenExtra **tp, int mi_row, int mi_col,
                     RUN_TYPE dry_run, BLOCK_SIZE bsize,
                     PARTITION_TYPE partition, PICK_MODE_CONTEXT *const ctx,
                     int *rate) {
  const AV1_COMMON *const cm = &cpi->common;
  TileInfo *const tile = &tile_data->tile_info;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *xd = &x->e_mbd;
  const int subsampling_x = cm->seq_params->subsampling_x;
  const int subsampling_y = cm->seq_params->subsampling_y;

  av1_set_offsets_without_segment_id(cpi, tile, x, mi_row, mi_col, bsize);
  const int origin_mult = x->rdmult;
  setup_block_rdmult(cpi, x, mi_row, mi_col, bsize, NO_AQ, NULL);
  MB_MODE_INFO *mbmi = xd->mi[0];
  mbmi->partition = partition;
  av1_update_state(cpi, td, ctx, mi_row, mi_col, bsize, dry_run);

  if (!dry_run) {
    set_cb_offsets(x->mbmi_ext_frame->cb_offset, x->cb_offset[PLANE_TYPE_Y],
                   x->cb_offset[PLANE_TYPE_UV]);
    assert(x->cb_offset[PLANE_TYPE_Y] <
           (1 << num_pels_log2_lookup[cpi->common.seq_params->sb_size]));
    assert(x->cb_offset[PLANE_TYPE_UV] <
           ((1 << num_pels_log2_lookup[cpi->common.seq_params->sb_size]) >>
            (subsampling_x + subsampling_y)));
  }

  encode_superblock(cpi, tile_data, td, tp, dry_run, bsize, rate);

  if (!dry_run) {
    update_cb_offsets(x, bsize, subsampling_x, subsampling_y);
    if (bsize == cpi->common.seq_params->sb_size && mbmi->skip_txfm == 1 &&
        cm->delta_q_info.delta_lf_present_flag) {
      const int frame_lf_count =
          av1_num_planes(cm) > 1 ? FRAME_LF_COUNT : FRAME_LF_COUNT - 2;
      for (int lf_id = 0; lf_id < frame_lf_count; ++lf_id)
        mbmi->delta_lf[lf_id] = xd->delta_lf[lf_id];
      mbmi->delta_lf_from_base = xd->delta_lf_from_base;
    }
    if (has_second_ref(mbmi)) {
      if (mbmi->compound_idx == 0 ||
          mbmi->interinter_comp.type == COMPOUND_AVERAGE)
        mbmi->comp_group_idx = 0;
      else
        mbmi->comp_group_idx = 1;
    }

    // delta quant applies to both intra and inter
    const int super_block_upper_left =
        ((mi_row & (cm->seq_params->mib_size - 1)) == 0) &&
        ((mi_col & (cm->seq_params->mib_size - 1)) == 0);
    const DeltaQInfo *const delta_q_info = &cm->delta_q_info;
    if (delta_q_info->delta_q_present_flag &&
        (bsize != cm->seq_params->sb_size || !mbmi->skip_txfm) &&
        super_block_upper_left) {
      xd->current_base_qindex = mbmi->current_qindex;
      if (delta_q_info->delta_lf_present_flag) {
        if (delta_q_info->delta_lf_multi) {
          const int frame_lf_count =
              av1_num_planes(cm) > 1 ? FRAME_LF_COUNT : FRAME_LF_COUNT - 2;
          for (int lf_id = 0; lf_id < frame_lf_count; ++lf_id) {
            xd->delta_lf[lf_id] = mbmi->delta_lf[lf_id];
          }
        } else {
          xd->delta_lf_from_base = mbmi->delta_lf_from_base;
        }
      }
    }

    RD_COUNTS *rdc = &td->rd_counts;
    if (mbmi->skip_mode) {
      assert(!frame_is_intra_only(cm));
      rdc->skip_mode_used_flag = 1;
      if (cm->current_frame.reference_mode == REFERENCE_MODE_SELECT) {
        assert(has_second_ref(mbmi));
        rdc->compound_ref_used_flag = 1;
      }
      set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);
    } else {
      const int seg_ref_active =
          segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_REF_FRAME);
      if (!seg_ref_active) {
        // If the segment reference feature is enabled we have only a single
        // reference frame allowed for the segment so exclude it from
        // the reference frame counts used to work out probabilities.
        if (is_inter_block(mbmi)) {
          av1_collect_neighbors_ref_counts(xd);
          if (cm->current_frame.reference_mode == REFERENCE_MODE_SELECT) {
            if (has_second_ref(mbmi)) {
              // This flag is also updated for 4x4 blocks
              rdc->compound_ref_used_flag = 1;
            }
          }
          set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);
        }
      }
    }

    if (tile_data->allow_update_cdf) update_stats(&cpi->common, td);

    // Gather obmc and warped motion count to update the probability.
    if ((cpi->sf.inter_sf.prune_obmc_prob_thresh > 0 &&
         cpi->sf.inter_sf.prune_obmc_prob_thresh < INT_MAX) ||
        (cm->features.allow_warped_motion &&
         cpi->sf.inter_sf.prune_warped_prob_thresh > 0)) {
      const int inter_block = is_inter_block(mbmi);
      const int seg_ref_active =
          segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_REF_FRAME);
      if (!seg_ref_active && inter_block) {
        const MOTION_MODE motion_allowed =
            cm->features.switchable_motion_mode
                ? motion_mode_allowed(xd->global_motion, xd, mbmi,
                                      cm->features.allow_warped_motion)
                : SIMPLE_TRANSLATION;

        if (mbmi->ref_frame[1] != INTRA_FRAME) {
          if (motion_allowed >= OBMC_CAUSAL) {
            td->rd_counts.obmc_used[bsize][mbmi->motion_mode == OBMC_CAUSAL]++;
          }
          if (motion_allowed == WARPED_CAUSAL) {
            td->rd_counts.warped_used[mbmi->motion_mode == WARPED_CAUSAL]++;
          }
        }
      }
    }
  }
  // TODO(Ravi/Remya): Move this copy function to a better logical place
  // This function will copy the best mode information from block
  // level (x->mbmi_ext) to frame level (cpi->mbmi_ext_info.frame_base). This
  // frame level buffer (cpi->mbmi_ext_info.frame_base) will be used during
  // bitstream preparation.
  av1_copy_mbmi_ext_to_mbmi_ext_frame(x->mbmi_ext_frame, &x->mbmi_ext,
                                      av1_ref_frame_type(xd->mi[0]->ref_frame));
  x->rdmult = origin_mult;
}

/*!\brief Reconstructs a partition (may contain multiple coding blocks)
 *
 * \ingroup partition_search
 * Reconstructs a sub-partition of the superblock by applying the chosen modes
 * and partition trees stored in pc_tree.
 *
 * \param[in]    cpi       Top-level encoder structure
 * \param[in]    td        Pointer to thread data
 * \param[in]    tile_data Pointer to struct holding adaptive
 *                         data/contexts/models for the tile during encoding
 * \param[in]    tp        Pointer to the starting token
 * \param[in]    mi_row    Row coordinate of the block in a step size of MI_SIZE
 * \param[in]    mi_col    Column coordinate of the block in a step size of
 *                         MI_SIZE
 * \param[in]    dry_run   A code indicating whether it is part of the final
 *                         pass for reconstructing the superblock
 * \param[in]    bsize     Current block size
 * \param[in]    pc_tree   Pointer to the PC_TREE node storing the picked
 *                         partitions and mode info for the current block
 * \param[in]    rate      Pointer to the total rate for the current block
 *
 * \remark Nothing is returned. Instead, reconstructions (w/o in-loop filters)
 * will be updated in the pixel buffers in td->mb.e_mbd.
 */
static void encode_sb(const AV1_COMP *const cpi, ThreadData *td,
                      TileDataEnc *tile_data, TokenExtra **tp, int mi_row,
                      int mi_col, RUN_TYPE dry_run, BLOCK_SIZE bsize,
                      PC_TREE *pc_tree, int *rate) {
  assert(bsize < BLOCK_SIZES_ALL);
  const AV1_COMMON *const cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  assert(bsize < BLOCK_SIZES_ALL);
  const int hbs = mi_size_wide[bsize] / 2;
  const int is_partition_root = bsize >= BLOCK_8X8;
  const int ctx = is_partition_root
                      ? partition_plane_context(xd, mi_row, mi_col, bsize)
                      : -1;
  const PARTITION_TYPE partition = pc_tree->partitioning;
  const BLOCK_SIZE subsize = get_partition_subsize(bsize, partition);
#if !CONFIG_REALTIME_ONLY
  int quarter_step = mi_size_wide[bsize] / 4;
  int i;
  BLOCK_SIZE bsize2 = get_partition_subsize(bsize, PARTITION_SPLIT);
#endif

  if (mi_row >= mi_params->mi_rows || mi_col >= mi_params->mi_cols) return;
  if (subsize == BLOCK_INVALID) return;

  if (!dry_run && ctx >= 0) {
    const int has_rows = (mi_row + hbs) < mi_params->mi_rows;
    const int has_cols = (mi_col + hbs) < mi_params->mi_cols;

    if (has_rows && has_cols) {
#if CONFIG_ENTROPY_STATS
      td->counts->partition[ctx][partition]++;
#endif

      if (tile_data->allow_update_cdf) {
        FRAME_CONTEXT *fc = xd->tile_ctx;
        update_cdf(fc->partition_cdf[ctx], partition,
                   partition_cdf_length(bsize));
      }
    }
  }

  switch (partition) {
    case PARTITION_NONE:
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col, dry_run, subsize,
               partition, pc_tree->none, rate);
      break;
    case PARTITION_VERT:
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col, dry_run, subsize,
               partition, pc_tree->vertical[0], rate);
      if (mi_col + hbs < mi_params->mi_cols) {
        encode_b(cpi, tile_data, td, tp, mi_row, mi_col + hbs, dry_run, subsize,
                 partition, pc_tree->vertical[1], rate);
      }
      break;
    case PARTITION_HORZ:
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col, dry_run, subsize,
               partition, pc_tree->horizontal[0], rate);
      if (mi_row + hbs < mi_params->mi_rows) {
        encode_b(cpi, tile_data, td, tp, mi_row + hbs, mi_col, dry_run, subsize,
                 partition, pc_tree->horizontal[1], rate);
      }
      break;
    case PARTITION_SPLIT:
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, dry_run, subsize,
                pc_tree->split[0], rate);
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col + hbs, dry_run, subsize,
                pc_tree->split[1], rate);
      encode_sb(cpi, td, tile_data, tp, mi_row + hbs, mi_col, dry_run, subsize,
                pc_tree->split[2], rate);
      encode_sb(cpi, td, tile_data, tp, mi_row + hbs, mi_col + hbs, dry_run,
                subsize, pc_tree->split[3], rate);
      break;

#if !CONFIG_REALTIME_ONLY
    case PARTITION_HORZ_A:
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col, dry_run, bsize2,
               partition, pc_tree->horizontala[0], rate);
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col + hbs, dry_run, bsize2,
               partition, pc_tree->horizontala[1], rate);
      encode_b(cpi, tile_data, td, tp, mi_row + hbs, mi_col, dry_run, subsize,
               partition, pc_tree->horizontala[2], rate);
      break;
    case PARTITION_HORZ_B:
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col, dry_run, subsize,
               partition, pc_tree->horizontalb[0], rate);
      encode_b(cpi, tile_data, td, tp, mi_row + hbs, mi_col, dry_run, bsize2,
               partition, pc_tree->horizontalb[1], rate);
      encode_b(cpi, tile_data, td, tp, mi_row + hbs, mi_col + hbs, dry_run,
               bsize2, partition, pc_tree->horizontalb[2], rate);
      break;
    case PARTITION_VERT_A:
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col, dry_run, bsize2,
               partition, pc_tree->verticala[0], rate);
      encode_b(cpi, tile_data, td, tp, mi_row + hbs, mi_col, dry_run, bsize2,
               partition, pc_tree->verticala[1], rate);
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col + hbs, dry_run, subsize,
               partition, pc_tree->verticala[2], rate);

      break;
    case PARTITION_VERT_B:
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col, dry_run, subsize,
               partition, pc_tree->verticalb[0], rate);
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col + hbs, dry_run, bsize2,
               partition, pc_tree->verticalb[1], rate);
      encode_b(cpi, tile_data, td, tp, mi_row + hbs, mi_col + hbs, dry_run,
               bsize2, partition, pc_tree->verticalb[2], rate);
      break;
    case PARTITION_HORZ_4:
      for (i = 0; i < SUB_PARTITIONS_PART4; ++i) {
        int this_mi_row = mi_row + i * quarter_step;
        if (i > 0 && this_mi_row >= mi_params->mi_rows) break;

        encode_b(cpi, tile_data, td, tp, this_mi_row, mi_col, dry_run, subsize,
                 partition, pc_tree->horizontal4[i], rate);
      }
      break;
    case PARTITION_VERT_4:
      for (i = 0; i < SUB_PARTITIONS_PART4; ++i) {
        int this_mi_col = mi_col + i * quarter_step;
        if (i > 0 && this_mi_col >= mi_params->mi_cols) break;
        encode_b(cpi, tile_data, td, tp, mi_row, this_mi_col, dry_run, subsize,
                 partition, pc_tree->vertical4[i], rate);
      }
      break;
#endif
    default: assert(0 && "Invalid partition type."); break;
  }

  update_ext_partition_context(xd, mi_row, mi_col, subsize, bsize, partition);
}

static inline int is_adjust_var_based_part_enabled(
    AV1_COMMON *const cm, const PARTITION_SPEED_FEATURES *const part_sf,
    BLOCK_SIZE bsize) {
  if (part_sf->partition_search_type != VAR_BASED_PARTITION) return 0;
  if (part_sf->adjust_var_based_rd_partitioning == 0 ||
      part_sf->adjust_var_based_rd_partitioning > 2)
    return 0;

  if (bsize <= BLOCK_32X32) return 1;
  if (part_sf->adjust_var_based_rd_partitioning == 2) {
    const int is_larger_qindex = cm->quant_params.base_qindex > 190;
    const int is_360p_or_larger = AOMMIN(cm->width, cm->height) >= 360;
    return is_360p_or_larger && is_larger_qindex && bsize == BLOCK_64X64;
  }
  return 0;
}

/*!\brief AV1 block partition search (partition estimation and partial search).
*
* \ingroup partition_search
* Encode the block by applying pre-calculated partition patterns that are
* represented by coding block sizes stored in the mbmi array. Minor partition
* adjustments are tested and applied if they lead to lower rd costs. The
* partition types are limited to a basic set: none, horz, vert, and split.
*
* \param[in]    cpi       Top-level encoder structure
* \param[in]    td        Pointer to thread data
* \param[in]    tile_data Pointer to struct holding adaptive
data/contexts/models for the tile during encoding
* \param[in]    mib       Array representing MB_MODE_INFO pointers for mi
blocks starting from the first pixel of the current
block
* \param[in]    tp        Pointer to the starting token
* \param[in]    mi_row    Row coordinate of the block in a step size of MI_SIZE
* \param[in]    mi_col    Column coordinate of the block in a step size of
MI_SIZE
* \param[in]    bsize     Current block size
* \param[in]    rate      Pointer to the final rate for encoding the current
block
* \param[in]    dist      Pointer to the final distortion of the current block
* \param[in]    do_recon  Whether the reconstruction function needs to be run,
either for finalizing a superblock or providing
reference for future sub-partitions
* \param[in]    pc_tree   Pointer to the PC_TREE node holding the picked
partitions and mode info for the current block
*
* \remark Nothing is returned. The pc_tree struct is modified to store the
* picked partition and modes. The rate and dist are also updated with those
* corresponding to the best partition found.
*/
void av1_rd_use_partition(AV1_COMP *cpi, ThreadData *td, TileDataEnc *tile_data,
                          MB_MODE_INFO **mib, TokenExtra **tp, int mi_row,
                          int mi_col, BLOCK_SIZE bsize, int *rate,
                          int64_t *dist, int do_recon, PC_TREE *pc_tree) {
  AV1_COMMON *const cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int num_planes = av1_num_planes(cm);
  TileInfo *const tile_info = &tile_data->tile_info;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  const ModeCosts *mode_costs = &x->mode_costs;
  const int bs = mi_size_wide[bsize];
  const int hbs = bs / 2;
  const int pl = (bsize >= BLOCK_8X8)
                     ? partition_plane_context(xd, mi_row, mi_col, bsize)
                     : 0;
  const PARTITION_TYPE partition =
      (bsize >= BLOCK_8X8) ? get_partition(cm, mi_row, mi_col, bsize)
                           : PARTITION_NONE;
  const BLOCK_SIZE subsize = get_partition_subsize(bsize, partition);
  RD_SEARCH_MACROBLOCK_CONTEXT x_ctx;
  RD_STATS last_part_rdc, none_rdc, chosen_rdc, invalid_rdc;
  BLOCK_SIZE bs_type = mib[0]->bsize;
  int use_partition_none = 0;
  x->try_merge_partition = 0;

  if (pc_tree->none == NULL) {
    pc_tree->none = av1_alloc_pmc(cpi, bsize, &td->shared_coeff_buf);
    if (!pc_tree->none)
      aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate PICK_MODE_CONTEXT");
  }
  PICK_MODE_CONTEXT *ctx_none = pc_tree->none;

  if (mi_row >= mi_params->mi_rows || mi_col >= mi_params->mi_cols) return;

  assert(mi_size_wide[bsize] == mi_size_high[bsize]);
  // In rt mode, currently the min partition size is BLOCK_8X8.
  assert(bsize >= cpi->sf.part_sf.default_min_partition_size);

  av1_invalid_rd_stats(&last_part_rdc);
  av1_invalid_rd_stats(&none_rdc);
  av1_invalid_rd_stats(&chosen_rdc);
  av1_invalid_rd_stats(&invalid_rdc);

  pc_tree->partitioning = partition;

  xd->above_txfm_context =
      cm->above_contexts.txfm[tile_info->tile_row] + mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (mi_row & MAX_MIB_MASK);
  av1_save_context(x, &x_ctx, mi_row, mi_col, bsize, num_planes);

  if (bsize == BLOCK_16X16 && cpi->vaq_refresh) {
    av1_set_offsets(cpi, tile_info, x, mi_row, mi_col, bsize);
    x->mb_energy = av1_log_block_var(cpi, x, bsize);
  }

  // Save rdmult before it might be changed, so it can be restored later.
  const int orig_rdmult = x->rdmult;
  setup_block_rdmult(cpi, x, mi_row, mi_col, bsize, NO_AQ, NULL);

  if (partition != PARTITION_NONE &&
      is_adjust_var_based_part_enabled(cm, &cpi->sf.part_sf, bsize) &&
      (mi_row + hbs < mi_params->mi_rows &&
       mi_col + hbs < mi_params->mi_cols)) {
    assert(bsize > cpi->sf.part_sf.default_min_partition_size);
    mib[0]->bsize = bsize;
    pc_tree->partitioning = PARTITION_NONE;
    x->try_merge_partition = 1;
    pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &none_rdc, PARTITION_NONE,
                  bsize, ctx_none, invalid_rdc);

    if (none_rdc.rate < INT_MAX) {
      none_rdc.rate += mode_costs->partition_cost[pl][PARTITION_NONE];
      none_rdc.rdcost = RDCOST(x->rdmult, none_rdc.rate, none_rdc.dist);
    }

    // Try to skip split partition evaluation based on none partition
    // characteristics.
    if (none_rdc.rate < INT_MAX && none_rdc.skip_txfm == 1) {
      use_partition_none = 1;
    }

    av1_restore_context(x, &x_ctx, mi_row, mi_col, bsize, num_planes);
    mib[0]->bsize = bs_type;
    pc_tree->partitioning = partition;
  }

  for (int i = 0; i < SUB_PARTITIONS_SPLIT; ++i) {
    pc_tree->split[i] = av1_alloc_pc_tree_node(subsize);
    if (!pc_tree->split[i])
      aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate PC_TREE");
    pc_tree->split[i]->index = i;
  }
  switch (partition) {
    case PARTITION_NONE:
      pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &last_part_rdc,
                    PARTITION_NONE, bsize, ctx_none, invalid_rdc);
      break;
    case PARTITION_HORZ:
      if (use_partition_none) {
        av1_invalid_rd_stats(&last_part_rdc);
        break;
      }

      for (int i = 0; i < SUB_PARTITIONS_RECT; ++i) {
        pc_tree->horizontal[i] =
            av1_alloc_pmc(cpi, subsize, &td->shared_coeff_buf);
        if (!pc_tree->horizontal[i])
          aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                             "Failed to allocate PICK_MODE_CONTEXT");
      }
      pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &last_part_rdc,
                    PARTITION_HORZ, subsize, pc_tree->horizontal[0],
                    invalid_rdc);
      if (last_part_rdc.rate != INT_MAX && bsize >= BLOCK_8X8 &&
          mi_row + hbs < mi_params->mi_rows) {
        RD_STATS tmp_rdc;
        const PICK_MODE_CONTEXT *const ctx_h = pc_tree->horizontal[0];
        av1_init_rd_stats(&tmp_rdc);
        av1_update_state(cpi, td, ctx_h, mi_row, mi_col, subsize, 1);
        encode_superblock(cpi, tile_data, td, tp, DRY_RUN_NORMAL, subsize,
                          NULL);
        pick_sb_modes(cpi, tile_data, x, mi_row + hbs, mi_col, &tmp_rdc,
                      PARTITION_HORZ, subsize, pc_tree->horizontal[1],
                      invalid_rdc);
        if (tmp_rdc.rate == INT_MAX || tmp_rdc.dist == INT64_MAX) {
          av1_invalid_rd_stats(&last_part_rdc);
          break;
        }
        last_part_rdc.rate += tmp_rdc.rate;
        last_part_rdc.dist += tmp_rdc.dist;
        last_part_rdc.rdcost += tmp_rdc.rdcost;
      }
      break;
    case PARTITION_VERT:
      if (use_partition_none) {
        av1_invalid_rd_stats(&last_part_rdc);
        break;
      }

      for (int i = 0; i < SUB_PARTITIONS_RECT; ++i) {
        pc_tree->vertical[i] =
            av1_alloc_pmc(cpi, subsize, &td->shared_coeff_buf);
        if (!pc_tree->vertical[i])
          aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                             "Failed to allocate PICK_MODE_CONTEXT");
      }
      pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &last_part_rdc,
                    PARTITION_VERT, subsize, pc_tree->vertical[0], invalid_rdc);
      if (last_part_rdc.rate != INT_MAX && bsize >= BLOCK_8X8 &&
          mi_col + hbs < mi_params->mi_cols) {
        RD_STATS tmp_rdc;
        const PICK_MODE_CONTEXT *const ctx_v = pc_tree->vertical[0];
        av1_init_rd_stats(&tmp_rdc);
        av1_update_state(cpi, td, ctx_v, mi_row, mi_col, subsize, 1);
        encode_superblock(cpi, tile_data, td, tp, DRY_RUN_NORMAL, subsize,
                          NULL);
        pick_sb_modes(cpi, tile_data, x, mi_row, mi_col + hbs, &tmp_rdc,
                      PARTITION_VERT, subsize,
                      pc_tree->vertical[bsize > BLOCK_8X8], invalid_rdc);
        if (tmp_rdc.rate == INT_MAX || tmp_rdc.dist == INT64_MAX) {
          av1_invalid_rd_stats(&last_part_rdc);
          break;
        }
        last_part_rdc.rate += tmp_rdc.rate;
        last_part_rdc.dist += tmp_rdc.dist;
        last_part_rdc.rdcost += tmp_rdc.rdcost;
      }
      break;
    case PARTITION_SPLIT:
      if (use_partition_none) {
        av1_invalid_rd_stats(&last_part_rdc);
        break;
      }

      last_part_rdc.rate = 0;
      last_part_rdc.dist = 0;
      last_part_rdc.rdcost = 0;
      for (int i = 0; i < SUB_PARTITIONS_SPLIT; i++) {
        int x_idx = (i & 1) * hbs;
        int y_idx = (i >> 1) * hbs;
        int jj = i >> 1, ii = i & 0x01;
        RD_STATS tmp_rdc;
        if ((mi_row + y_idx >= mi_params->mi_rows) ||
            (mi_col + x_idx >= mi_params->mi_cols))
          continue;

        av1_init_rd_stats(&tmp_rdc);
        av1_rd_use_partition(
            cpi, td, tile_data,
            mib + jj * hbs * mi_params->mi_stride + ii * hbs, tp,
            mi_row + y_idx, mi_col + x_idx, subsize, &tmp_rdc.rate,
            &tmp_rdc.dist, i != (SUB_PARTITIONS_SPLIT - 1), pc_tree->split[i]);
        if (tmp_rdc.rate == INT_MAX || tmp_rdc.dist == INT64_MAX) {
          av1_invalid_rd_stats(&last_part_rdc);
          break;
        }
        last_part_rdc.rate += tmp_rdc.rate;
        last_part_rdc.dist += tmp_rdc.dist;
      }
      break;
    case PARTITION_VERT_A:
    case PARTITION_VERT_B:
    case PARTITION_HORZ_A:
    case PARTITION_HORZ_B:
    case PARTITION_HORZ_4:
    case PARTITION_VERT_4:
      assert(0 && "Cannot handle extended partition types");
    default: assert(0); break;
  }

  if (last_part_rdc.rate < INT_MAX) {
    last_part_rdc.rate += mode_costs->partition_cost[pl][partition];
    last_part_rdc.rdcost =
        RDCOST(x->rdmult, last_part_rdc.rate, last_part_rdc.dist);
  }

  if ((cpi->sf.part_sf.partition_search_type == VAR_BASED_PARTITION &&
       cpi->sf.part_sf.adjust_var_based_rd_partitioning > 2) &&
      partition != PARTITION_SPLIT && bsize > BLOCK_8X8 &&
      (mi_row + bs < mi_params->mi_rows ||
       mi_row + hbs == mi_params->mi_rows) &&
      (mi_col + bs < mi_params->mi_cols ||
       mi_col + hbs == mi_params->mi_cols)) {
    BLOCK_SIZE split_subsize = get_partition_subsize(bsize, PARTITION_SPLIT);
    chosen_rdc.rate = 0;
    chosen_rdc.dist = 0;

    av1_restore_context(x, &x_ctx, mi_row, mi_col, bsize, num_planes);
    pc_tree->partitioning = PARTITION_SPLIT;

    // Split partition.
    for (int i = 0; i < SUB_PARTITIONS_SPLIT; i++) {
      int x_idx = (i & 1) * hbs;
      int y_idx = (i >> 1) * hbs;
      RD_STATS tmp_rdc;

      if ((mi_row + y_idx >= mi_params->mi_rows) ||
          (mi_col + x_idx >= mi_params->mi_cols))
        continue;

      av1_save_context(x, &x_ctx, mi_row, mi_col, bsize, num_planes);
      pc_tree->split[i]->partitioning = PARTITION_NONE;
      if (pc_tree->split[i]->none == NULL)
        pc_tree->split[i]->none =
            av1_alloc_pmc(cpi, split_subsize, &td->shared_coeff_buf);
      if (!pc_tree->split[i]->none)
        aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                           "Failed to allocate PICK_MODE_CONTEXT");
      pick_sb_modes(cpi, tile_data, x, mi_row + y_idx, mi_col + x_idx, &tmp_rdc,
                    PARTITION_SPLIT, split_subsize, pc_tree->split[i]->none,
                    invalid_rdc);

      av1_restore_context(x, &x_ctx, mi_row, mi_col, bsize, num_planes);
      if (tmp_rdc.rate == INT_MAX || tmp_rdc.dist == INT64_MAX) {
        av1_invalid_rd_stats(&chosen_rdc);
        break;
      }

      chosen_rdc.rate += tmp_rdc.rate;
      chosen_rdc.dist += tmp_rdc.dist;

      if (i != SUB_PARTITIONS_SPLIT - 1)
        encode_sb(cpi, td, tile_data, tp, mi_row + y_idx, mi_col + x_idx,
                  OUTPUT_ENABLED, split_subsize, pc_tree->split[i], NULL);

      chosen_rdc.rate += mode_costs->partition_cost[pl][PARTITION_NONE];
    }
    if (chosen_rdc.rate < INT_MAX) {
      chosen_rdc.rate += mode_costs->partition_cost[pl][PARTITION_SPLIT];
      chosen_rdc.rdcost = RDCOST(x->rdmult, chosen_rdc.rate, chosen_rdc.dist);
    }
  }

  // If last_part is better set the partitioning to that.
  if (last_part_rdc.rdcost < chosen_rdc.rdcost) {
    mib[0]->bsize = bs_type;
    if (bsize >= BLOCK_8X8) pc_tree->partitioning = partition;

    chosen_rdc = last_part_rdc;
  }
  // If none was better set the partitioning to that.
  if (none_rdc.rdcost < INT64_MAX &&
      none_rdc.rdcost - (none_rdc.rdcost >> 9) < chosen_rdc.rdcost) {
    mib[0]->bsize = bsize;
    if (bsize >= BLOCK_8X8) pc_tree->partitioning = PARTITION_NONE;
    chosen_rdc = none_rdc;
  }

  av1_restore_context(x, &x_ctx, mi_row, mi_col, bsize, num_planes);

  // We must have chosen a partitioning and encoding or we'll fail later on.
  // No other opportunities for success.
  if (bsize == cm->seq_params->sb_size)
    assert(chosen_rdc.rate < INT_MAX && chosen_rdc.dist < INT64_MAX);

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, encode_sb_time);
#endif
  if (do_recon) {
    if (bsize == cm->seq_params->sb_size) {
      // NOTE: To get estimate for rate due to the tokens, use:
      // int rate_coeffs = 0;
      // encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, DRY_RUN_COSTCOEFFS,
      //           bsize, pc_tree, &rate_coeffs);
      set_cb_offsets(x->cb_offset, 0, 0);
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, OUTPUT_ENABLED, bsize,
                pc_tree, NULL);
    } else {
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, DRY_RUN_NORMAL, bsize,
                pc_tree, NULL);
    }
  }
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, encode_sb_time);
#endif

  *rate = chosen_rdc.rate;
  *dist = chosen_rdc.dist;
  x->rdmult = orig_rdmult;
}

static void encode_b_nonrd(const AV1_COMP *const cpi, TileDataEnc *tile_data,
                           ThreadData *td, TokenExtra **tp, int mi_row,
                           int mi_col, RUN_TYPE dry_run, BLOCK_SIZE bsize,
                           PARTITION_TYPE partition,
                           PICK_MODE_CONTEXT *const ctx, int *rate) {
#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing((AV1_COMP *)cpi, encode_b_nonrd_time);
#endif
  const AV1_COMMON *const cm = &cpi->common;
  TileInfo *const tile = &tile_data->tile_info;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *xd = &x->e_mbd;
  av1_set_offsets_without_segment_id(cpi, tile, x, mi_row, mi_col, bsize);
  const int origin_mult = x->rdmult;
  setup_block_rdmult(cpi, x, mi_row, mi_col, bsize, NO_AQ, NULL);
  MB_MODE_INFO *mbmi = xd->mi[0];
  mbmi->partition = partition;
  av1_update_state(cpi, td, ctx, mi_row, mi_col, bsize, dry_run);
  const int subsampling_x = cpi->common.seq_params->subsampling_x;
  const int subsampling_y = cpi->common.seq_params->subsampling_y;
  if (!dry_run) {
    set_cb_offsets(x->mbmi_ext_frame->cb_offset, x->cb_offset[PLANE_TYPE_Y],
                   x->cb_offset[PLANE_TYPE_UV]);
    assert(x->cb_offset[PLANE_TYPE_Y] <
           (1 << num_pels_log2_lookup[cpi->common.seq_params->sb_size]));
    assert(x->cb_offset[PLANE_TYPE_UV] <
           ((1 << num_pels_log2_lookup[cpi->common.seq_params->sb_size]) >>
            (subsampling_x + subsampling_y)));
  }

  encode_superblock(cpi, tile_data, td, tp, dry_run, bsize, rate);
  if (!dry_run) {
    update_cb_offsets(x, bsize, subsampling_x, subsampling_y);
    if (has_second_ref(mbmi)) {
      if (mbmi->compound_idx == 0 ||
          mbmi->interinter_comp.type == COMPOUND_AVERAGE)
        mbmi->comp_group_idx = 0;
      else
        mbmi->comp_group_idx = 1;
      mbmi->compound_idx = 1;
    }
    RD_COUNTS *const rdc = &td->rd_counts;
    if (mbmi->skip_mode) {
      assert(!frame_is_intra_only(cm));
      rdc->skip_mode_used_flag = 1;
      if (cm->current_frame.reference_mode == REFERENCE_MODE_SELECT &&
          has_second_ref(mbmi)) {
        rdc->compound_ref_used_flag = 1;
      }
      set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);
    } else {
      const int seg_ref_active =
          segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_REF_FRAME);
      if (!seg_ref_active) {
        // If the segment reference feature is enabled we have only a single
        // reference frame allowed for the segment so exclude it from
        // the reference frame counts used to work out probabilities.
        if (is_inter_block(mbmi)) {
          av1_collect_neighbors_ref_counts(xd);
          if (cm->current_frame.reference_mode == REFERENCE_MODE_SELECT &&
              has_second_ref(mbmi)) {
            // This flag is also updated for 4x4 blocks
            rdc->compound_ref_used_flag = 1;
          }
          set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);
        }
      }
    }
    if (cpi->oxcf.algo_cfg.loopfilter_control == LOOPFILTER_SELECTIVELY &&
        (mbmi->mode == NEWMV || mbmi->mode < INTRA_MODE_END)) {
      int32_t blocks = mi_size_high[bsize] * mi_size_wide[bsize];
      rdc->newmv_or_intra_blocks += blocks;
    }
    if (tile_data->allow_update_cdf) update_stats(&cpi->common, td);
  }
  if ((cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ ||
       cpi->active_map.enabled) &&
      mbmi->skip_txfm && !cpi->rc.rtc_external_ratectrl && cm->seg.enabled)
    av1_cyclic_reset_segment_skip(cpi, x, mi_row, mi_col, bsize, dry_run);
  // TODO(Ravi/Remya): Move this copy function to a better logical place
  // This function will copy the best mode information from block
  // level (x->mbmi_ext) to frame level (cpi->mbmi_ext_info.frame_base). This
  // frame level buffer (cpi->mbmi_ext_info.frame_base) will be used during
  // bitstream preparation.
  av1_copy_mbmi_ext_to_mbmi_ext_frame(x->mbmi_ext_frame, &x->mbmi_ext,
                                      av1_ref_frame_type(xd->mi[0]->ref_frame));
  x->rdmult = origin_mult;
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing((AV1_COMP *)cpi, encode_b_nonrd_time);
#endif
}

static int get_force_zeromv_skip_flag_for_blk(const AV1_COMP *cpi,
                                              const MACROBLOCK *x,
                                              BLOCK_SIZE bsize) {
  // Force zero MV skip based on SB level decision
  if (x->force_zeromv_skip_for_sb < 2) return x->force_zeromv_skip_for_sb;

  // For blocks of size equal to superblock size, the decision would have been
  // already done at superblock level. Hence zeromv-skip decision is skipped.
  const AV1_COMMON *const cm = &cpi->common;
  if (bsize == cm->seq_params->sb_size) return 0;

  const int num_planes = av1_num_planes(cm);
  const MACROBLOCKD *const xd = &x->e_mbd;
  const unsigned int thresh_exit_part_y =
      cpi->zeromv_skip_thresh_exit_part[bsize];
  const unsigned int thresh_exit_part_uv =
      CALC_CHROMA_THRESH_FOR_ZEROMV_SKIP(thresh_exit_part_y);
  const unsigned int thresh_exit_part[MAX_MB_PLANE] = { thresh_exit_part_y,
                                                        thresh_exit_part_uv,
                                                        thresh_exit_part_uv };
  const YV12_BUFFER_CONFIG *const yv12 = get_ref_frame_yv12_buf(cm, LAST_FRAME);
  const struct scale_factors *const sf =
      get_ref_scale_factors_const(cm, LAST_FRAME);

  struct buf_2d yv12_mb[MAX_MB_PLANE];
  av1_setup_pred_block(xd, yv12_mb, yv12, sf, sf, num_planes);

  for (int plane = 0; plane < num_planes; ++plane) {
    const struct macroblock_plane *const p = &x->plane[plane];
    const struct macroblockd_plane *const pd = &xd->plane[plane];
    const BLOCK_SIZE bs =
        get_plane_block_size(bsize, pd->subsampling_x, pd->subsampling_y);
    const unsigned int plane_sad = cpi->ppi->fn_ptr[bs].sdf(
        p->src.buf, p->src.stride, yv12_mb[plane].buf, yv12_mb[plane].stride);
    assert(plane < MAX_MB_PLANE);
    if (plane_sad >= thresh_exit_part[plane]) return 0;
  }
  return 1;
}

/*!\brief Top level function to pick block mode for non-RD optimized case
 *
 * \ingroup partition_search
 * \callgraph
 * \callergraph
 * Searches prediction modes, transform, and coefficient coding modes for an
 * individual coding block. This function is the top-level function that is
 * used for non-RD optimized mode search (controlled by
 * \c cpi->sf.rt_sf.use_nonrd_pick_mode). Depending on frame type it calls
 * inter/skip/hybrid-intra mode search functions
 *
 * \param[in]    cpi            Top-level encoder structure
 * \param[in]    tile_data      Pointer to struct holding adaptive
 *                              data/contexts/models for the tile during
 *                              encoding
 * \param[in]    x              Pointer to structure holding all the data for
 *                              the current macroblock
 * \param[in]    mi_row         Row coordinate of the block in a step size of
 *                              MI_SIZE
 * \param[in]    mi_col         Column coordinate of the block in a step size of
 *                              MI_SIZE
 * \param[in]    rd_cost        Pointer to structure holding rate and distortion
 *                              stats for the current block
 * \param[in]    bsize          Current block size
 * \param[in]    ctx            Pointer to structure holding coding contexts and
 *                              chosen modes for the current block
 *
 * \remark Nothing is returned. Instead, the chosen modes and contexts necessary
 * for reconstruction are stored in ctx, the rate-distortion stats are stored in
 * rd_cost. If no valid mode leading to rd_cost <= best_rd, the status will be
 * signalled by an INT64_MAX rd_cost->rdcost.
 */
static void pick_sb_modes_nonrd(AV1_COMP *const cpi, TileDataEnc *tile_data,
                                MACROBLOCK *const x, int mi_row, int mi_col,
                                RD_STATS *rd_cost, BLOCK_SIZE bsize,
                                PICK_MODE_CONTEXT *ctx) {
  // For nonrd mode, av1_set_offsets is already called at the superblock level
  // in encode_nonrd_sb when we determine the partitioning.
  if (bsize != cpi->common.seq_params->sb_size ||
      cpi->sf.rt_sf.nonrd_check_partition_split == 1) {
    av1_set_offsets(cpi, &tile_data->tile_info, x, mi_row, mi_col, bsize);
  }
  assert(x->last_set_offsets_loc.mi_row == mi_row &&
         x->last_set_offsets_loc.mi_col == mi_col &&
         x->last_set_offsets_loc.bsize == bsize);
  AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  struct macroblock_plane *const p = x->plane;
  struct macroblockd_plane *const pd = xd->plane;
  const AQ_MODE aq_mode = cpi->oxcf.q_cfg.aq_mode;
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  int i;
  const int seg_skip =
      segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP);

  // This is only needed for real time/allintra row-mt enabled multi-threaded
  // encoding with cost update frequency set to COST_UPD_TILE/COST_UPD_OFF.
  wait_for_top_right_sb(&cpi->mt_info.enc_row_mt, &tile_data->row_mt_sync,
                        &tile_data->tile_info, cm->seq_params->sb_size,
                        cm->seq_params->mib_size_log2, bsize, mi_row, mi_col);

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, pick_sb_modes_nonrd_time);
#endif
  // Sets up the tx_type_map buffer in MACROBLOCKD.
  xd->tx_type_map = txfm_info->tx_type_map_;
  xd->tx_type_map_stride = mi_size_wide[bsize];
  for (i = 0; i < num_planes; ++i) {
    p[i].coeff = ctx->coeff[i];
    p[i].qcoeff = ctx->qcoeff[i];
    p[i].dqcoeff = ctx->dqcoeff[i];
    p[i].eobs = ctx->eobs[i];
    p[i].txb_entropy_ctx = ctx->txb_entropy_ctx[i];
  }
  for (i = 0; i < 2; ++i) pd[i].color_index_map = ctx->color_index_map[i];

  if (!seg_skip) {
    x->force_zeromv_skip_for_blk =
        get_force_zeromv_skip_flag_for_blk(cpi, x, bsize);

    // Source variance may be already compute at superblock level, so no need
    // to recompute, unless bsize < sb_size or source_variance is not yet set.
    if (!x->force_zeromv_skip_for_blk &&
        (x->source_variance == UINT_MAX || bsize < cm->seq_params->sb_size))
      x->source_variance = av1_get_perpixel_variance_facade(
          cpi, xd, &x->plane[0].src, bsize, AOM_PLANE_Y);
  }

  // Save rdmult before it might be changed, so it can be restored later.
  const int orig_rdmult = x->rdmult;
  setup_block_rdmult(cpi, x, mi_row, mi_col, bsize, aq_mode, mbmi);
  // Set error per bit for current rdmult
  av1_set_error_per_bit(&x->errorperbit, x->rdmult);
  // Find best coding mode & reconstruct the MB so it is available
  // as a predictor for MBs that follow in the SB
  if (frame_is_intra_only(cm)) {
#if CONFIG_COLLECT_COMPONENT_TIMING
    start_timing(cpi, hybrid_intra_mode_search_time);
#endif
    hybrid_intra_mode_search(cpi, x, rd_cost, bsize, ctx);
#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(cpi, hybrid_intra_mode_search_time);
#endif
  } else {
#if CONFIG_COLLECT_COMPONENT_TIMING
    start_timing(cpi, nonrd_pick_inter_mode_sb_time);
#endif
    if (seg_skip) {
      x->force_zeromv_skip_for_blk = 1;
      // TODO(marpan): Consider adding a function for nonrd:
      // av1_nonrd_pick_inter_mode_sb_seg_skip(), instead of setting
      // x->force_zeromv_skip flag and entering av1_nonrd_pick_inter_mode_sb().
    }
    av1_nonrd_pick_inter_mode_sb(cpi, tile_data, x, rd_cost, bsize, ctx);
#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(cpi, nonrd_pick_inter_mode_sb_time);
#endif
  }
  if (cpi->sf.rt_sf.skip_cdef_sb) {
    // cdef_strength is initialized to 1 which means skip_cdef, and is updated
    // here. Check to see is skipping cdef is allowed. Never skip on slide/scene
    // change, near a key frame, or when color sensitivity is set. Always allow
    // cdef_skip for seg_skip = 1.
    const int allow_cdef_skipping =
        seg_skip ||
        (cpi->rc.frames_since_key > 10 && !cpi->rc.high_source_sad &&
         !(x->color_sensitivity[COLOR_SENS_IDX(AOM_PLANE_U)] ||
           x->color_sensitivity[COLOR_SENS_IDX(AOM_PLANE_V)]));

    // Find the corresponding 64x64 block. It'll be the 128x128 block if that's
    // the block size.
    const int mi_row_sb = mi_row - mi_row % MI_SIZE_64X64;
    const int mi_col_sb = mi_col - mi_col % MI_SIZE_64X64;
    MB_MODE_INFO **mi_sb =
        cm->mi_params.mi_grid_base +
        get_mi_grid_idx(&cm->mi_params, mi_row_sb, mi_col_sb);
    const int is_720p_or_larger = AOMMIN(cm->width, cm->height) >= 720;
    unsigned int thresh_spatial_var =
        (cpi->oxcf.speed >= 11 && !is_720p_or_larger &&
         cpi->oxcf.tune_cfg.content != AOM_CONTENT_SCREEN)
            ? 400
            : UINT_MAX;
    // For skip_cdef_sb = 1: do not skip if allow_cdef_skipping is false or
    // intra or new mv is picked, with possible conidition on spatial variance.
    // For skip_cdef_sb >= 2: more aggressive mode to always skip unless
    // allow_cdef_skipping is false and source_variance is non-zero.
    if (cpi->sf.rt_sf.skip_cdef_sb >= 2) {
      mi_sb[0]->cdef_strength =
          mi_sb[0]->cdef_strength &&
          (allow_cdef_skipping || x->source_variance == 0);
    } else {
      mi_sb[0]->cdef_strength =
          mi_sb[0]->cdef_strength && allow_cdef_skipping &&
          !(x->source_variance < thresh_spatial_var &&
            (mbmi->mode < INTRA_MODES || mbmi->mode == NEWMV));
    }
    // Store in the pickmode context.
    ctx->mic.cdef_strength = mi_sb[0]->cdef_strength;
  }
  x->rdmult = orig_rdmult;
  ctx->rd_stats.rate = rd_cost->rate;
  ctx->rd_stats.dist = rd_cost->dist;
  ctx->rd_stats.rdcost = rd_cost->rdcost;
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, pick_sb_modes_nonrd_time);
#endif
}

static int try_split_partition(AV1_COMP *const cpi, ThreadData *const td,
                               TileDataEnc *const tile_data,
                               TileInfo *const tile_info, TokenExtra **tp,
                               MACROBLOCK *const x, MACROBLOCKD *const xd,
                               const CommonModeInfoParams *const mi_params,
                               const int mi_row, const int mi_col,
                               const BLOCK_SIZE bsize, const int pl,
                               PC_TREE *pc_tree) {
  AV1_COMMON *const cm = &cpi->common;
  const ModeCosts *mode_costs = &x->mode_costs;
  const int hbs = mi_size_wide[bsize] / 2;
  if (mi_row + mi_size_high[bsize] >= mi_params->mi_rows ||
      mi_col + mi_size_wide[bsize] >= mi_params->mi_cols)
    return 0;
  if (bsize <= BLOCK_8X8 || frame_is_intra_only(cm)) return 0;
  if (x->content_state_sb.source_sad_nonrd <= kLowSad) return 0;

  // Do not try split partition when the source sad is small, or
  // the prediction residual is small.
  const YV12_BUFFER_CONFIG *const yv12 = get_ref_frame_yv12_buf(cm, LAST_FRAME);
  const struct scale_factors *const sf =
      get_ref_scale_factors_const(cm, LAST_FRAME);
  const int num_planes = av1_num_planes(cm);
  av1_setup_src_planes(x, cpi->source, mi_row, mi_col, num_planes, bsize);
  av1_setup_pre_planes(xd, 0, yv12, mi_row, mi_col, sf, num_planes);
  int block_sad = 0;
  for (int plane = 0; plane < num_planes; ++plane) {
    const struct macroblock_plane *const p = &x->plane[plane];
    const struct macroblockd_plane *const pd = &xd->plane[plane];
    const BLOCK_SIZE bs =
        get_plane_block_size(bsize, pd->subsampling_x, pd->subsampling_y);
    const unsigned int plane_sad = cpi->ppi->fn_ptr[bs].sdf(
        p->src.buf, p->src.stride, pd->pre[0].buf, pd->pre[0].stride);
    block_sad += plane_sad;
  }
  const int blk_pix = block_size_wide[bsize] * block_size_high[bsize];
  const int block_avg_sad = block_sad / blk_pix;
  // TODO(chengchen): find a proper threshold. It might change according to
  // q as well.
  const int threshold = 25;
  if (block_avg_sad < threshold) return 0;

  RD_SEARCH_MACROBLOCK_CONTEXT x_ctx;
  RD_STATS split_rdc, none_rdc;
  av1_invalid_rd_stats(&split_rdc);
  av1_invalid_rd_stats(&none_rdc);
  av1_save_context(x, &x_ctx, mi_row, mi_col, bsize, 3);
  xd->above_txfm_context =
      cm->above_contexts.txfm[tile_info->tile_row] + mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (mi_row & MAX_MIB_MASK);

  // Calculate rdcost for none partition
  pc_tree->partitioning = PARTITION_NONE;
  av1_set_offsets(cpi, tile_info, x, mi_row, mi_col, bsize);
  if (!pc_tree->none) {
    pc_tree->none = av1_alloc_pmc(cpi, bsize, &td->shared_coeff_buf);
    if (!pc_tree->none)
      aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate PICK_MODE_CONTEXT");
  } else {
    av1_reset_pmc(pc_tree->none);
  }
  pick_sb_modes_nonrd(cpi, tile_data, x, mi_row, mi_col, &none_rdc, bsize,
                      pc_tree->none);
  none_rdc.rate += mode_costs->partition_cost[pl][PARTITION_NONE];
  none_rdc.rdcost = RDCOST(x->rdmult, none_rdc.rate, none_rdc.dist);
  av1_restore_context(x, &x_ctx, mi_row, mi_col, bsize, 3);

  // Calculate rdcost for split partition
  pc_tree->partitioning = PARTITION_SPLIT;
  const BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);
  av1_init_rd_stats(&split_rdc);
  split_rdc.rate += mode_costs->partition_cost[pl][PARTITION_SPLIT];
  if (subsize >= BLOCK_8X8) {
    split_rdc.rate += (mode_costs->partition_cost[pl][PARTITION_NONE] * 4);
  }
  for (int i = 0; i < SUB_PARTITIONS_SPLIT; ++i) {
    if (!pc_tree->split[i]) {
      pc_tree->split[i] = av1_alloc_pc_tree_node(subsize);
      if (!pc_tree->split[i])
        aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                           "Failed to allocate PC_TREE");
    }
    pc_tree->split[i]->index = i;
  }
  for (int i = 0; i < SUB_PARTITIONS_SPLIT; i++) {
    RD_STATS block_rdc;
    av1_invalid_rd_stats(&block_rdc);
    int x_idx = (i & 1) * hbs;
    int y_idx = (i >> 1) * hbs;
    if ((mi_row + y_idx >= mi_params->mi_rows) ||
        (mi_col + x_idx >= mi_params->mi_cols))
      continue;
    xd->above_txfm_context =
        cm->above_contexts.txfm[tile_info->tile_row] + mi_col + x_idx;
    xd->left_txfm_context =
        xd->left_txfm_context_buffer + ((mi_row + y_idx) & MAX_MIB_MASK);
    if (!pc_tree->split[i]->none) {
      pc_tree->split[i]->none =
          av1_alloc_pmc(cpi, subsize, &td->shared_coeff_buf);
      if (!pc_tree->split[i]->none)
        aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                           "Failed to allocate PICK_MODE_CONTEXT");
    } else {
      av1_reset_pmc(pc_tree->split[i]->none);
    }
    pc_tree->split[i]->partitioning = PARTITION_NONE;
    pick_sb_modes_nonrd(cpi, tile_data, x, mi_row + y_idx, mi_col + x_idx,
                        &block_rdc, subsize, pc_tree->split[i]->none);
    split_rdc.rate += block_rdc.rate;
    split_rdc.dist += block_rdc.dist;
    av1_rd_cost_update(x->rdmult, &split_rdc);
    if (none_rdc.rdcost < split_rdc.rdcost) break;
    if (i != SUB_PARTITIONS_SPLIT - 1)
      encode_b_nonrd(cpi, tile_data, td, tp, mi_row + y_idx, mi_col + x_idx, 1,
                     subsize, PARTITION_NONE, pc_tree->split[i]->none, NULL);
  }
  av1_restore_context(x, &x_ctx, mi_row, mi_col, bsize, 3);
  split_rdc.rdcost = RDCOST(x->rdmult, split_rdc.rate, split_rdc.dist);
  const int split = split_rdc.rdcost < none_rdc.rdcost;

  return split;
}

// Returns if SPLIT partitions should be evaluated
static bool calc_do_split_flag(const AV1_COMP *cpi, const MACROBLOCK *x,
                               const PC_TREE *pc_tree, const RD_STATS *none_rdc,
                               const CommonModeInfoParams *mi_params,
                               int mi_row, int mi_col, int hbs,
                               BLOCK_SIZE bsize, PARTITION_TYPE partition) {
  const AV1_COMMON *const cm = &cpi->common;
  const int is_larger_qindex = cm->quant_params.base_qindex > 100;
  const MACROBLOCKD *const xd = &x->e_mbd;
  bool do_split =
      (cpi->sf.rt_sf.nonrd_check_partition_merge_mode == 3)
          ? (bsize <= BLOCK_32X32 || (is_larger_qindex && bsize <= BLOCK_64X64))
          : true;
  if (cpi->oxcf.tune_cfg.content == AOM_CONTENT_SCREEN ||
      cpi->sf.rt_sf.nonrd_check_partition_merge_mode < 2 ||
      cyclic_refresh_segment_id_boosted(xd->mi[0]->segment_id) ||
      !none_rdc->skip_txfm)
    return do_split;

  const int use_model_yrd_large = get_model_rd_flag(cpi, xd, bsize);

  // When model based skip is not used (i.e.,use_model_yrd_large = 0), skip_txfm
  // would have been populated based on Hadamard transform and skip_txfm flag is
  // more reliable. Hence SPLIT evaluation is disabled at all quantizers for 8x8
  // and 16x16 blocks.
  // When model based skip is used (i.e.,use_model_yrd_large = 1), skip_txfm may
  // not be reliable. Hence SPLIT evaluation is disabled only at lower
  // quantizers for blocks >= 32x32.
  if ((!use_model_yrd_large) || (!is_larger_qindex)) return false;

  // Use residual statistics to decide if SPLIT partition should be evaluated
  // for 32x32 blocks. The pruning logic is avoided for larger block size to
  // avoid the visual artifacts
  if (pc_tree->none->mic.mode == NEWMV && bsize == BLOCK_32X32 && do_split) {
    const BLOCK_SIZE subsize = get_partition_subsize(bsize, partition);
    assert(subsize < BLOCK_SIZES_ALL);
    double min_per_pixel_error = DBL_MAX;
    double max_per_pixel_error = 0.;
    int i;
    for (i = 0; i < SUB_PARTITIONS_SPLIT; i++) {
      const int x_idx = (i & 1) * hbs;
      const int y_idx = (i >> 1) * hbs;
      if ((mi_row + y_idx >= mi_params->mi_rows) ||
          (mi_col + x_idx >= mi_params->mi_cols)) {
        break;
      }

      // Populate the appropriate buffer pointers.
      // Pass scale factors as NULL as the base pointer of the block would have
      // been calculated appropriately.
      struct buf_2d src_split_buf_2d, pred_split_buf_2d;
      const struct buf_2d *src_none_buf_2d = &x->plane[AOM_PLANE_Y].src;
      setup_pred_plane(&src_split_buf_2d, subsize, src_none_buf_2d->buf,
                       src_none_buf_2d->width, src_none_buf_2d->height,
                       src_none_buf_2d->stride, y_idx, x_idx, NULL, 0, 0);
      const struct buf_2d *pred_none_buf_2d = &xd->plane[AOM_PLANE_Y].dst;
      setup_pred_plane(&pred_split_buf_2d, subsize, pred_none_buf_2d->buf,
                       pred_none_buf_2d->width, pred_none_buf_2d->height,
                       pred_none_buf_2d->stride, y_idx, x_idx, NULL, 0, 0);

      unsigned int curr_uint_mse;
      const unsigned int curr_uint_var = cpi->ppi->fn_ptr[subsize].vf(
          src_split_buf_2d.buf, src_split_buf_2d.stride, pred_split_buf_2d.buf,
          pred_split_buf_2d.stride, &curr_uint_mse);
      const double curr_per_pixel_error =
          sqrt((double)curr_uint_var / block_size_wide[subsize] /
               block_size_high[subsize]);
      if (curr_per_pixel_error < min_per_pixel_error)
        min_per_pixel_error = curr_per_pixel_error;
      if (curr_per_pixel_error > max_per_pixel_error)
        max_per_pixel_error = curr_per_pixel_error;
    }

    // Prune based on residual statistics only if all the sub-partitions are
    // valid.
    if (i == SUB_PARTITIONS_SPLIT) {
      if (max_per_pixel_error - min_per_pixel_error <= 1.5) do_split = false;
    }
  }

  return do_split;
}

static void try_merge(AV1_COMP *const cpi, ThreadData *td,
                      TileDataEnc *tile_data, MB_MODE_INFO **mib,
                      TokenExtra **tp, const int mi_row, const int mi_col,
                      const BLOCK_SIZE bsize, PC_TREE *const pc_tree,
                      const PARTITION_TYPE partition, const BLOCK_SIZE subsize,
                      const int pl) {
  AV1_COMMON *const cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  TileInfo *const tile_info = &tile_data->tile_info;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  const ModeCosts *mode_costs = &x->mode_costs;
  const int num_planes = av1_num_planes(cm);
  // Only square blocks from 8x8 to 128x128 are supported
  assert(bsize >= BLOCK_8X8 && bsize <= BLOCK_128X128);
  const int bs = mi_size_wide[bsize];
  const int hbs = bs / 2;
  bool do_split = false;
  RD_SEARCH_MACROBLOCK_CONTEXT x_ctx;
  RD_STATS split_rdc, none_rdc;
  av1_invalid_rd_stats(&split_rdc);
  av1_invalid_rd_stats(&none_rdc);
  av1_save_context(x, &x_ctx, mi_row, mi_col, bsize, num_planes);
  xd->above_txfm_context =
      cm->above_contexts.txfm[tile_info->tile_row] + mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (mi_row & MAX_MIB_MASK);
  pc_tree->partitioning = PARTITION_NONE;
  if (!pc_tree->none) {
    pc_tree->none = av1_alloc_pmc(cpi, bsize, &td->shared_coeff_buf);
    if (!pc_tree->none)
      aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate PICK_MODE_CONTEXT");
  } else {
    av1_reset_pmc(pc_tree->none);
  }
  pick_sb_modes_nonrd(cpi, tile_data, x, mi_row, mi_col, &none_rdc, bsize,
                      pc_tree->none);
  none_rdc.rate += mode_costs->partition_cost[pl][PARTITION_NONE];
  none_rdc.rdcost = RDCOST(x->rdmult, none_rdc.rate, none_rdc.dist);
  av1_restore_context(x, &x_ctx, mi_row, mi_col, bsize, num_planes);

  if (cpi->sf.rt_sf.nonrd_check_partition_merge_mode < 2 ||
      none_rdc.skip_txfm != 1 || pc_tree->none->mic.mode == NEWMV) {
    do_split = calc_do_split_flag(cpi, x, pc_tree, &none_rdc, mi_params, mi_row,
                                  mi_col, hbs, bsize, partition);
    if (do_split) {
      av1_init_rd_stats(&split_rdc);
      split_rdc.rate += mode_costs->partition_cost[pl][PARTITION_SPLIT];
      for (int i = 0; i < SUB_PARTITIONS_SPLIT; i++) {
        RD_STATS block_rdc;
        av1_invalid_rd_stats(&block_rdc);
        int x_idx = (i & 1) * hbs;
        int y_idx = (i >> 1) * hbs;
        if ((mi_row + y_idx >= mi_params->mi_rows) ||
            (mi_col + x_idx >= mi_params->mi_cols))
          continue;
        xd->above_txfm_context =
            cm->above_contexts.txfm[tile_info->tile_row] + mi_col + x_idx;
        xd->left_txfm_context =
            xd->left_txfm_context_buffer + ((mi_row + y_idx) & MAX_MIB_MASK);
        if (!pc_tree->split[i]->none) {
          pc_tree->split[i]->none =
              av1_alloc_pmc(cpi, subsize, &td->shared_coeff_buf);
          if (!pc_tree->split[i]->none)
            aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                               "Failed to allocate PICK_MODE_CONTEXT");
        } else {
          av1_reset_pmc(pc_tree->split[i]->none);
        }
        pc_tree->split[i]->partitioning = PARTITION_NONE;
        pick_sb_modes_nonrd(cpi, tile_data, x, mi_row + y_idx, mi_col + x_idx,
                            &block_rdc, subsize, pc_tree->split[i]->none);
        // TODO(yunqingwang): The rate here did not include the cost of
        // signaling PARTITION_NONE token in the sub-blocks.
        split_rdc.rate += block_rdc.rate;
        split_rdc.dist += block_rdc.dist;

        av1_rd_cost_update(x->rdmult, &split_rdc);

        if (none_rdc.rdcost < split_rdc.rdcost) {
          break;
        }

        if (i != SUB_PARTITIONS_SPLIT - 1)
          encode_b_nonrd(cpi, tile_data, td, tp, mi_row + y_idx, mi_col + x_idx,
                         1, subsize, PARTITION_NONE, pc_tree->split[i]->none,
                         NULL);
      }
      av1_restore_context(x, &x_ctx, mi_row, mi_col, bsize, num_planes);
      split_rdc.rdcost = RDCOST(x->rdmult, split_rdc.rate, split_rdc.dist);
    }
  }

  if (none_rdc.rdcost < split_rdc.rdcost) {
    /* Predicted samples can not be reused for PARTITION_NONE since same
     * buffer is being used to store the reconstructed samples of
     * PARTITION_SPLIT block. */
    if (do_split) x->reuse_inter_pred = false;

    mib[0]->bsize = bsize;
    pc_tree->partitioning = PARTITION_NONE;
    encode_b_nonrd(cpi, tile_data, td, tp, mi_row, mi_col, 0, bsize, partition,
                   pc_tree->none, NULL);
  } else {
    mib[0]->bsize = subsize;
    pc_tree->partitioning = PARTITION_SPLIT;
    /* Predicted samples can not be reused for PARTITION_SPLIT since same
     * buffer is being used to write the reconstructed samples. */
    // TODO(Cherma): Store and reuse predicted samples generated by
    // encode_b_nonrd() in DRY_RUN_NORMAL mode.
    x->reuse_inter_pred = false;

    for (int i = 0; i < SUB_PARTITIONS_SPLIT; i++) {
      int x_idx = (i & 1) * hbs;
      int y_idx = (i >> 1) * hbs;
      if ((mi_row + y_idx >= mi_params->mi_rows) ||
          (mi_col + x_idx >= mi_params->mi_cols))
        continue;

      // Note: We don't reset pc_tree->split[i]->none here because it
      // could contain results from the additional check. Instead, it is
      // reset before we enter the nonrd_check_partition_merge_mode
      // condition.
      if (!pc_tree->split[i]->none) {
        pc_tree->split[i]->none =
            av1_alloc_pmc(cpi, subsize, &td->shared_coeff_buf);
        if (!pc_tree->split[i]->none)
          aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                             "Failed to allocate PICK_MODE_CONTEXT");
      }
      encode_b_nonrd(cpi, tile_data, td, tp, mi_row + y_idx, mi_col + x_idx, 0,
                     subsize, PARTITION_NONE, pc_tree->split[i]->none, NULL);
    }
  }
}

// Evaluate if the sub-partitions can be merged directly into a large partition
// without calculating the RD cost.
static void direct_partition_merging(AV1_COMP *cpi, ThreadData *td,
                                     TileDataEnc *tile_data, MB_MODE_INFO **mib,
                                     int mi_row, int mi_col, BLOCK_SIZE bsize) {
  AV1_COMMON *const cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  TileInfo *const tile_info = &tile_data->tile_info;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  const int bs = mi_size_wide[bsize];
  const int hbs = bs / 2;
  const PARTITION_TYPE partition =
      (bsize >= BLOCK_8X8) ? get_partition(cm, mi_row, mi_col, bsize)
                           : PARTITION_NONE;
  BLOCK_SIZE subsize = get_partition_subsize(bsize, partition);

  MB_MODE_INFO **b0 = mib;
  MB_MODE_INFO **b1 = mib + hbs;
  MB_MODE_INFO **b2 = mib + hbs * mi_params->mi_stride;
  MB_MODE_INFO **b3 = mib + hbs * mi_params->mi_stride + hbs;

  // Check if the following conditions are met. This can be updated
  // later with more support added.
  const int further_split = b0[0]->bsize < subsize || b1[0]->bsize < subsize ||
                            b2[0]->bsize < subsize || b3[0]->bsize < subsize;
  if (further_split) return;

  const int no_skip = !b0[0]->skip_txfm || !b1[0]->skip_txfm ||
                      !b2[0]->skip_txfm || !b3[0]->skip_txfm;
  if (no_skip) return;

  const int compound = (b0[0]->ref_frame[1] != b1[0]->ref_frame[1] ||
                        b0[0]->ref_frame[1] != b2[0]->ref_frame[1] ||
                        b0[0]->ref_frame[1] != b3[0]->ref_frame[1] ||
                        b0[0]->ref_frame[1] > NONE_FRAME);
  if (compound) return;

  // Intra modes aren't considered here.
  const int different_ref = (b0[0]->ref_frame[0] != b1[0]->ref_frame[0] ||
                             b0[0]->ref_frame[0] != b2[0]->ref_frame[0] ||
                             b0[0]->ref_frame[0] != b3[0]->ref_frame[0] ||
                             b0[0]->ref_frame[0] <= INTRA_FRAME);
  if (different_ref) return;

  const int different_mode =
      (b0[0]->mode != b1[0]->mode || b0[0]->mode != b2[0]->mode ||
       b0[0]->mode != b3[0]->mode);
  if (different_mode) return;

  const int unsupported_mode =
      (b0[0]->mode != NEARESTMV && b0[0]->mode != GLOBALMV);
  if (unsupported_mode) return;

  const int different_mv = (b0[0]->mv[0].as_int != b1[0]->mv[0].as_int ||
                            b0[0]->mv[0].as_int != b2[0]->mv[0].as_int ||
                            b0[0]->mv[0].as_int != b3[0]->mv[0].as_int);
  if (different_mv) return;

  const int unsupported_motion_mode =
      (b0[0]->motion_mode != b1[0]->motion_mode ||
       b0[0]->motion_mode != b2[0]->motion_mode ||
       b0[0]->motion_mode != b3[0]->motion_mode ||
       b0[0]->motion_mode != SIMPLE_TRANSLATION);
  if (unsupported_motion_mode) return;

  const int diffent_filter =
      (b0[0]->interp_filters.as_int != b1[0]->interp_filters.as_int ||
       b0[0]->interp_filters.as_int != b2[0]->interp_filters.as_int ||
       b0[0]->interp_filters.as_int != b3[0]->interp_filters.as_int);
  if (diffent_filter) return;

  const int different_seg = (b0[0]->segment_id != b1[0]->segment_id ||
                             b0[0]->segment_id != b2[0]->segment_id ||
                             b0[0]->segment_id != b3[0]->segment_id);
  if (different_seg) return;

  // Evaluate the ref_mv.
  MB_MODE_INFO **this_mi = mib;
  BLOCK_SIZE orig_bsize = this_mi[0]->bsize;
  const PARTITION_TYPE orig_partition = this_mi[0]->partition;

  this_mi[0]->bsize = bsize;
  this_mi[0]->partition = PARTITION_NONE;
  this_mi[0]->skip_txfm = 1;

  // TODO(yunqing): functions called below can be optimized by
  // removing unrelated operations.
  av1_set_offsets_without_segment_id(cpi, &tile_data->tile_info, x, mi_row,
                                     mi_col, bsize);

  const MV_REFERENCE_FRAME ref_frame = this_mi[0]->ref_frame[0];
  int_mv frame_mv[MB_MODE_COUNT][REF_FRAMES];
  struct buf_2d yv12_mb[REF_FRAMES][MAX_MB_PLANE];
  int force_skip_low_temp_var = 0;
  int skip_pred_mv = 0;
  bool use_scaled_ref;

  for (int i = 0; i < MB_MODE_COUNT; ++i) {
    for (int j = 0; j < REF_FRAMES; ++j) {
      frame_mv[i][j].as_int = INVALID_MV;
    }
  }
  av1_copy(x->color_sensitivity, x->color_sensitivity_sb);
  skip_pred_mv = (x->nonrd_prune_ref_frame_search > 2 &&
                  x->color_sensitivity[COLOR_SENS_IDX(AOM_PLANE_U)] != 2 &&
                  x->color_sensitivity[COLOR_SENS_IDX(AOM_PLANE_V)] != 2);

  find_predictors(cpi, x, ref_frame, frame_mv, yv12_mb, bsize,
                  force_skip_low_temp_var, skip_pred_mv, &use_scaled_ref);

  int continue_merging = 1;
  if (frame_mv[NEARESTMV][ref_frame].as_mv.row != b0[0]->mv[0].as_mv.row ||
      frame_mv[NEARESTMV][ref_frame].as_mv.col != b0[0]->mv[0].as_mv.col)
    continue_merging = 0;

  if (!continue_merging) {
    this_mi[0]->bsize = orig_bsize;
    this_mi[0]->partition = orig_partition;

    // TODO(yunqing): Store the results and restore here instead of
    // calling find_predictors() again.
    av1_set_offsets_without_segment_id(cpi, &tile_data->tile_info, x, mi_row,
                                       mi_col, this_mi[0]->bsize);
    find_predictors(cpi, x, ref_frame, frame_mv, yv12_mb, this_mi[0]->bsize,
                    force_skip_low_temp_var, skip_pred_mv, &use_scaled_ref);
  } else {
    struct scale_factors *sf = get_ref_scale_factors(cm, ref_frame);
    const int is_scaled = av1_is_scaled(sf);
    const int is_y_subpel_mv = (abs(this_mi[0]->mv[0].as_mv.row) % 8) ||
                               (abs(this_mi[0]->mv[0].as_mv.col) % 8);
    const int is_uv_subpel_mv = (abs(this_mi[0]->mv[0].as_mv.row) % 16) ||
                                (abs(this_mi[0]->mv[0].as_mv.col) % 16);

    if (cpi->ppi->use_svc || is_scaled || is_y_subpel_mv || is_uv_subpel_mv) {
      const int num_planes = av1_num_planes(cm);
      set_ref_ptrs(cm, xd, ref_frame, this_mi[0]->ref_frame[1]);
      const YV12_BUFFER_CONFIG *cfg = get_ref_frame_yv12_buf(cm, ref_frame);
      av1_setup_pre_planes(xd, 0, cfg, mi_row, mi_col,
                           xd->block_ref_scale_factors[0], num_planes);

      if (!cpi->ppi->use_svc && !is_scaled && !is_y_subpel_mv) {
        assert(is_uv_subpel_mv == 1);
        av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize, 1,
                                      num_planes - 1);
      } else {
        av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize, 0,
                                      num_planes - 1);
      }
    }

    // Copy out mbmi_ext information.
    MB_MODE_INFO_EXT *const mbmi_ext = &x->mbmi_ext;
    MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame = x->mbmi_ext_frame;
    av1_copy_mbmi_ext_to_mbmi_ext_frame(
        mbmi_ext_frame, mbmi_ext, av1_ref_frame_type(this_mi[0]->ref_frame));

    const BLOCK_SIZE this_subsize =
        get_partition_subsize(bsize, this_mi[0]->partition);
    // Update partition contexts.
    update_ext_partition_context(xd, mi_row, mi_col, this_subsize, bsize,
                                 this_mi[0]->partition);

    const int num_planes = av1_num_planes(cm);
    av1_reset_entropy_context(xd, bsize, num_planes);

    // Note: use x->txfm_search_params.tx_mode_search_type instead of
    // cm->features.tx_mode here.
    TX_SIZE tx_size =
        tx_size_from_tx_mode(bsize, x->txfm_search_params.tx_mode_search_type);
    if (xd->lossless[this_mi[0]->segment_id]) tx_size = TX_4X4;
    this_mi[0]->tx_size = tx_size;
    memset(this_mi[0]->inter_tx_size, this_mi[0]->tx_size,
           sizeof(this_mi[0]->inter_tx_size));

    // Update txfm contexts.
    xd->above_txfm_context =
        cm->above_contexts.txfm[tile_info->tile_row] + mi_col;
    xd->left_txfm_context =
        xd->left_txfm_context_buffer + (mi_row & MAX_MIB_MASK);
    set_txfm_ctxs(this_mi[0]->tx_size, xd->width, xd->height,
                  this_mi[0]->skip_txfm && is_inter_block(this_mi[0]), xd);

    // Update mi for this partition block.
    for (int y = 0; y < bs; y++) {
      for (int x_idx = 0; x_idx < bs; x_idx++) {
        this_mi[x_idx + y * mi_params->mi_stride] = this_mi[0];
      }
    }
  }
}

/*!\brief AV1 block partition application (minimal RD search).
*
* \ingroup partition_search
* \callgraph
* \callergraph
* Encode the block by applying pre-calculated partition patterns that are
* represented by coding block sizes stored in the mbmi array. The only
* partition adjustment allowed is merging leaf split nodes if it leads to a
* lower rd cost. The partition types are limited to a basic set: none, horz,
* vert, and split. This function is only used in the real-time mode.
*
* \param[in]    cpi       Top-level encoder structure
* \param[in]    td        Pointer to thread data
* \param[in]    tile_data Pointer to struct holding adaptive
data/contexts/models for the tile during encoding
* \param[in]    mib       Array representing MB_MODE_INFO pointers for mi
blocks starting from the first pixel of the current
block
* \param[in]    tp        Pointer to the starting token
* \param[in]    mi_row    Row coordinate of the block in a step size of MI_SIZE
* \param[in]    mi_col    Column coordinate of the block in a step size of
MI_SIZE
* \param[in]    bsize     Current block size
* \param[in]    pc_tree   Pointer to the PC_TREE node holding the picked
partitions and mode info for the current block
*
* \remark Nothing is returned. The pc_tree struct is modified to store the
* picked partition and modes.
*/
void av1_nonrd_use_partition(AV1_COMP *cpi, ThreadData *td,
                             TileDataEnc *tile_data, MB_MODE_INFO **mib,
                             TokenExtra **tp, int mi_row, int mi_col,
                             BLOCK_SIZE bsize, PC_TREE *pc_tree) {
  AV1_COMMON *const cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  TileInfo *const tile_info = &tile_data->tile_info;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  const ModeCosts *mode_costs = &x->mode_costs;
  // Only square blocks from 8x8 to 128x128 are supported
  assert(bsize >= BLOCK_8X8 && bsize <= BLOCK_128X128);
  const int bs = mi_size_wide[bsize];
  const int hbs = bs / 2;
  PARTITION_TYPE partition = (bsize >= BLOCK_8X8)
                                 ? get_partition(cm, mi_row, mi_col, bsize)
                                 : PARTITION_NONE;
  BLOCK_SIZE subsize = get_partition_subsize(bsize, partition);
  assert(subsize <= BLOCK_LARGEST);
  const int pl = (bsize >= BLOCK_8X8)
                     ? partition_plane_context(xd, mi_row, mi_col, bsize)
                     : 0;

  RD_STATS dummy_cost;
  av1_invalid_rd_stats(&dummy_cost);

  if (mi_row >= mi_params->mi_rows || mi_col >= mi_params->mi_cols) return;

  assert(mi_size_wide[bsize] == mi_size_high[bsize]);

  xd->above_txfm_context =
      cm->above_contexts.txfm[tile_info->tile_row] + mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (mi_row & MAX_MIB_MASK);

  // Initialize default mode evaluation params
  set_mode_eval_params(cpi, x, DEFAULT_EVAL);

  x->reuse_inter_pred = cpi->sf.rt_sf.reuse_inter_pred_nonrd;

  int change_none_to_split = 0;
  if (partition == PARTITION_NONE &&
      cpi->sf.rt_sf.nonrd_check_partition_split == 1) {
    change_none_to_split =
        try_split_partition(cpi, td, tile_data, tile_info, tp, x, xd, mi_params,
                            mi_row, mi_col, bsize, pl, pc_tree);
    if (change_none_to_split) {
      partition = PARTITION_SPLIT;
      subsize = get_partition_subsize(bsize, partition);
      assert(subsize <= BLOCK_LARGEST);
    }
  }

  pc_tree->partitioning = partition;

  switch (partition) {
    case PARTITION_NONE:
      if (!pc_tree->none) {
        pc_tree->none = av1_alloc_pmc(cpi, bsize, &td->shared_coeff_buf);
        if (!pc_tree->none)
          aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                             "Failed to allocate PICK_MODE_CONTEXT");
      } else {
        av1_reset_pmc(pc_tree->none);
      }
      pick_sb_modes_nonrd(cpi, tile_data, x, mi_row, mi_col, &dummy_cost, bsize,
                          pc_tree->none);
      encode_b_nonrd(cpi, tile_data, td, tp, mi_row, mi_col, 0, bsize,
                     partition, pc_tree->none, NULL);
      break;
    case PARTITION_VERT:
      for (int i = 0; i < SUB_PARTITIONS_RECT; ++i) {
        if (!pc_tree->vertical[i]) {
          pc_tree->vertical[i] =
              av1_alloc_pmc(cpi, subsize, &td->shared_coeff_buf);
          if (!pc_tree->vertical[i])
            aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                               "Failed to allocate PICK_MODE_CONTEXT");
        } else {
          av1_reset_pmc(pc_tree->vertical[i]);
        }
      }
      pick_sb_modes_nonrd(cpi, tile_data, x, mi_row, mi_col, &dummy_cost,
                          subsize, pc_tree->vertical[0]);
      encode_b_nonrd(cpi, tile_data, td, tp, mi_row, mi_col, 0, subsize,
                     PARTITION_VERT, pc_tree->vertical[0], NULL);
      if (mi_col + hbs < mi_params->mi_cols && bsize > BLOCK_8X8) {
        pick_sb_modes_nonrd(cpi, tile_data, x, mi_row, mi_col + hbs,
                            &dummy_cost, subsize, pc_tree->vertical[1]);
        encode_b_nonrd(cpi, tile_data, td, tp, mi_row, mi_col + hbs, 0, subsize,
                       PARTITION_VERT, pc_tree->vertical[1], NULL);
      }
      break;
    case PARTITION_HORZ:
      for (int i = 0; i < SUB_PARTITIONS_RECT; ++i) {
        if (!pc_tree->horizontal[i]) {
          pc_tree->horizontal[i] =
              av1_alloc_pmc(cpi, subsize, &td->shared_coeff_buf);
          if (!pc_tree->horizontal[i])
            aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                               "Failed to allocate PICK_MODE_CONTEXT");
        } else {
          av1_reset_pmc(pc_tree->horizontal[i]);
        }
      }
      pick_sb_modes_nonrd(cpi, tile_data, x, mi_row, mi_col, &dummy_cost,
                          subsize, pc_tree->horizontal[0]);
      encode_b_nonrd(cpi, tile_data, td, tp, mi_row, mi_col, 0, subsize,
                     PARTITION_HORZ, pc_tree->horizontal[0], NULL);

      if (mi_row + hbs < mi_params->mi_rows && bsize > BLOCK_8X8) {
        pick_sb_modes_nonrd(cpi, tile_data, x, mi_row + hbs, mi_col,
                            &dummy_cost, subsize, pc_tree->horizontal[1]);
        encode_b_nonrd(cpi, tile_data, td, tp, mi_row + hbs, mi_col, 0, subsize,
                       PARTITION_HORZ, pc_tree->horizontal[1], NULL);
      }
      break;
    case PARTITION_SPLIT:
      for (int i = 0; i < SUB_PARTITIONS_SPLIT; ++i) {
        if (!pc_tree->split[i]) {
          pc_tree->split[i] = av1_alloc_pc_tree_node(subsize);
          if (!pc_tree->split[i])
            aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                               "Failed to allocate PC_TREE");
        }
        pc_tree->split[i]->index = i;
      }
      if (cpi->sf.rt_sf.nonrd_check_partition_merge_mode &&
          av1_is_leaf_split_partition(cm, mi_row, mi_col, bsize) &&
          !frame_is_intra_only(cm) && bsize <= BLOCK_64X64) {
        try_merge(cpi, td, tile_data, mib, tp, mi_row, mi_col, bsize, pc_tree,
                  partition, subsize, pl);
      } else {
        for (int i = 0; i < SUB_PARTITIONS_SPLIT; i++) {
          int x_idx = (i & 1) * hbs;
          int y_idx = (i >> 1) * hbs;
          int jj = i >> 1, ii = i & 0x01;
          if ((mi_row + y_idx >= mi_params->mi_rows) ||
              (mi_col + x_idx >= mi_params->mi_cols))
            continue;
          av1_nonrd_use_partition(
              cpi, td, tile_data,
              mib + jj * hbs * mi_params->mi_stride + ii * hbs, tp,
              mi_row + y_idx, mi_col + x_idx, subsize, pc_tree->split[i]);
        }

        if (!change_none_to_split) {
          // Note: Palette, cfl are not supported.
          if (!frame_is_intra_only(cm) && !tile_data->allow_update_cdf &&
              cpi->sf.rt_sf.partition_direct_merging &&
              mode_costs->partition_cost[pl][PARTITION_NONE] <
                  mode_costs->partition_cost[pl][PARTITION_SPLIT] &&
              (mi_row + bs <= mi_params->mi_rows) &&
              (mi_col + bs <= mi_params->mi_cols)) {
            direct_partition_merging(cpi, td, tile_data, mib, mi_row, mi_col,
                                     bsize);
          }
        }
      }
      break;
    case PARTITION_VERT_A:
    case PARTITION_VERT_B:
    case PARTITION_HORZ_A:
    case PARTITION_HORZ_B:
    case PARTITION_HORZ_4:
    case PARTITION_VERT_4:
      assert(0 && "Cannot handle extended partition types");
    default: assert(0); break;
  }
}

#if !CONFIG_REALTIME_ONLY
// Try searching for an encoding for the given subblock. Returns zero if the
// rdcost is already too high (to tell the caller not to bother searching for
// encodings of further subblocks).
static int rd_try_subblock(AV1_COMP *const cpi, ThreadData *td,
                           TileDataEnc *tile_data, TokenExtra **tp, int is_last,
                           int mi_row, int mi_col, BLOCK_SIZE subsize,
                           RD_STATS best_rdcost, RD_STATS *sum_rdc,
                           PARTITION_TYPE partition,
                           PICK_MODE_CONTEXT *this_ctx) {
  MACROBLOCK *const x = &td->mb;
  const int orig_mult = x->rdmult;
  setup_block_rdmult(cpi, x, mi_row, mi_col, subsize, NO_AQ, NULL);

  av1_rd_cost_update(x->rdmult, &best_rdcost);

  RD_STATS rdcost_remaining;
  av1_rd_stats_subtraction(x->rdmult, &best_rdcost, sum_rdc, &rdcost_remaining);
  RD_STATS this_rdc;
  pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &this_rdc, partition,
                subsize, this_ctx, rdcost_remaining);

  if (this_rdc.rate == INT_MAX) {
    sum_rdc->rdcost = INT64_MAX;
  } else {
    sum_rdc->rate += this_rdc.rate;
    sum_rdc->dist += this_rdc.dist;
    av1_rd_cost_update(x->rdmult, sum_rdc);
  }

  if (sum_rdc->rdcost >= best_rdcost.rdcost) {
    x->rdmult = orig_mult;
    return 0;
  }

  if (!is_last) {
    av1_update_state(cpi, td, this_ctx, mi_row, mi_col, subsize, 1);
    encode_superblock(cpi, tile_data, td, tp, DRY_RUN_NORMAL, subsize, NULL);
  }

  x->rdmult = orig_mult;
  return 1;
}

// Tests an AB partition, and updates the encoder status, the pick mode
// contexts, the best rdcost, and the best partition.
static bool rd_test_partition3(AV1_COMP *const cpi, ThreadData *td,
                               TileDataEnc *tile_data, TokenExtra **tp,
                               PC_TREE *pc_tree, RD_STATS *best_rdc,
                               int64_t *this_rdcost,
                               PICK_MODE_CONTEXT *ctxs[SUB_PARTITIONS_AB],
                               int mi_row, int mi_col, BLOCK_SIZE bsize,
                               PARTITION_TYPE partition,
                               const BLOCK_SIZE ab_subsize[SUB_PARTITIONS_AB],
                               const int ab_mi_pos[SUB_PARTITIONS_AB][2],
                               const MB_MODE_INFO **mode_cache) {
  MACROBLOCK *const x = &td->mb;
  const MACROBLOCKD *const xd = &x->e_mbd;
  const int pl = partition_plane_context(xd, mi_row, mi_col, bsize);
  RD_STATS sum_rdc;
  av1_init_rd_stats(&sum_rdc);
  sum_rdc.rate = x->mode_costs.partition_cost[pl][partition];
  sum_rdc.rdcost = RDCOST(x->rdmult, sum_rdc.rate, 0);
  // Loop over sub-partitions in AB partition type.
  for (int i = 0; i < SUB_PARTITIONS_AB; i++) {
    if (mode_cache && mode_cache[i]) {
      x->use_mb_mode_cache = 1;
      x->mb_mode_cache = mode_cache[i];
    }
    const int mode_search_success =
        rd_try_subblock(cpi, td, tile_data, tp, i == SUB_PARTITIONS_AB - 1,
                        ab_mi_pos[i][0], ab_mi_pos[i][1], ab_subsize[i],
                        *best_rdc, &sum_rdc, partition, ctxs[i]);
    x->use_mb_mode_cache = 0;
    x->mb_mode_cache = NULL;
    if (!mode_search_success) {
      return false;
    }
  }

  av1_rd_cost_update(x->rdmult, &sum_rdc);
  *this_rdcost = sum_rdc.rdcost;
  if (sum_rdc.rdcost >= best_rdc->rdcost) return false;
  sum_rdc.rdcost = RDCOST(x->rdmult, sum_rdc.rate, sum_rdc.dist);
  *this_rdcost = sum_rdc.rdcost;
  if (sum_rdc.rdcost >= best_rdc->rdcost) return false;

  *best_rdc = sum_rdc;
  pc_tree->partitioning = partition;
  return true;
}

#if CONFIG_COLLECT_PARTITION_STATS
static void init_partition_block_timing_stats(
    PartitionTimingStats *part_timing_stats) {
  av1_zero(*part_timing_stats);
}

static inline void start_partition_block_timer(
    PartitionTimingStats *part_timing_stats, PARTITION_TYPE partition_type) {
  assert(!part_timing_stats->timer_is_on);
  part_timing_stats->partition_attempts[partition_type] += 1;
  aom_usec_timer_start(&part_timing_stats->timer);
  part_timing_stats->timer_is_on = 1;
}

static inline void end_partition_block_timer(
    PartitionTimingStats *part_timing_stats, PARTITION_TYPE partition_type,
    int64_t rdcost) {
  if (part_timing_stats->timer_is_on) {
    aom_usec_timer_mark(&part_timing_stats->timer);
    const int64_t time = aom_usec_timer_elapsed(&part_timing_stats->timer);
    part_timing_stats->partition_times[partition_type] += time;
    part_timing_stats->partition_rdcost[partition_type] = rdcost;
    part_timing_stats->timer_is_on = 0;
  }
}
static inline void print_partition_timing_stats_with_rdcost(
    const PartitionTimingStats *part_timing_stats, int mi_row, int mi_col,
    BLOCK_SIZE bsize, FRAME_UPDATE_TYPE frame_update_type, int frame_number,
    const RD_STATS *best_rdc, const char *filename) {
  FILE *f = fopen(filename, "a");
  fprintf(f, "%d,%d,%d,%d,%d,%d,%" PRId64 ",%" PRId64 ",", bsize, frame_number,
          frame_update_type, mi_row, mi_col, best_rdc->rate, best_rdc->dist,
          best_rdc->rdcost);
  for (int idx = 0; idx < EXT_PARTITION_TYPES; idx++) {
    fprintf(f, "%d,", part_timing_stats->partition_decisions[idx]);
  }
  for (int idx = 0; idx < EXT_PARTITION_TYPES; idx++) {
    fprintf(f, "%d,", part_timing_stats->partition_attempts[idx]);
  }
  for (int idx = 0; idx < EXT_PARTITION_TYPES; idx++) {
    fprintf(f, "%" PRId64 ",", part_timing_stats->partition_times[idx]);
  }
  for (int idx = 0; idx < EXT_PARTITION_TYPES; idx++) {
    if (part_timing_stats->partition_rdcost[idx] == INT64_MAX) {
      fprintf(f, "%d,", -1);
    } else {
      fprintf(f, "%" PRId64 ",", part_timing_stats->partition_rdcost[idx]);
    }
  }
  fprintf(f, "\n");
  fclose(f);
}

static inline void print_partition_timing_stats(
    const PartitionTimingStats *part_timing_stats, int intra_only,
    int show_frame, const BLOCK_SIZE bsize, const char *filename) {
  FILE *f = fopen(filename, "a");
  fprintf(f, "%d,%d,%d,", bsize, show_frame, intra_only);
  for (int idx = 0; idx < EXT_PARTITION_TYPES; idx++) {
    fprintf(f, "%d,", part_timing_stats->partition_decisions[idx]);
  }
  for (int idx = 0; idx < EXT_PARTITION_TYPES; idx++) {
    fprintf(f, "%d,", part_timing_stats->partition_attempts[idx]);
  }
  for (int idx = 0; idx < EXT_PARTITION_TYPES; idx++) {
    fprintf(f, "%" PRId64 ",", part_timing_stats->partition_times[idx]);
  }
  fprintf(f, "\n");
  fclose(f);
}

static inline void accumulate_partition_timing_stats(
    FramePartitionTimingStats *fr_part_timing_stats,
    const PartitionTimingStats *part_timing_stats, BLOCK_SIZE bsize) {
  const int bsize_idx = av1_get_bsize_idx_for_part_stats(bsize);
  int *agg_attempts = fr_part_timing_stats->partition_attempts[bsize_idx];
  int *agg_decisions = fr_part_timing_stats->partition_decisions[bsize_idx];
  int64_t *agg_times = fr_part_timing_stats->partition_times[bsize_idx];
  for (int idx = 0; idx < EXT_PARTITION_TYPES; idx++) {
    agg_attempts[idx] += part_timing_stats->partition_attempts[idx];
    agg_decisions[idx] += part_timing_stats->partition_decisions[idx];
    agg_times[idx] += part_timing_stats->partition_times[idx];
  }
}
#endif  // CONFIG_COLLECT_PARTITION_STATS

// Initialize state variables of partition search used in
// av1_rd_pick_partition().
static void init_partition_search_state_params(
    MACROBLOCK *x, AV1_COMP *const cpi, PartitionSearchState *part_search_state,
    int mi_row, int mi_col, BLOCK_SIZE bsize) {
  MACROBLOCKD *const xd = &x->e_mbd;
  const AV1_COMMON *const cm = &cpi->common;
  PartitionBlkParams *blk_params = &part_search_state->part_blk_params;
  const CommonModeInfoParams *const mi_params = &cpi->common.mi_params;

  // Initialization of block size related parameters.
  blk_params->mi_step = mi_size_wide[bsize] / 2;
  blk_params->mi_row = mi_row;
  blk_params->mi_col = mi_col;
  blk_params->mi_row_edge = mi_row + blk_params->mi_step;
  blk_params->mi_col_edge = mi_col + blk_params->mi_step;
  blk_params->width = block_size_wide[bsize];
  blk_params->min_partition_size_1d =
      block_size_wide[x->sb_enc.min_partition_size];
  blk_params->subsize = get_partition_subsize(bsize, PARTITION_SPLIT);
  blk_params->split_bsize2 = blk_params->subsize;
  blk_params->bsize_at_least_8x8 = (bsize >= BLOCK_8X8);
  blk_params->bsize = bsize;

  // Check if the partition corresponds to edge block.
  blk_params->has_rows = (blk_params->mi_row_edge < mi_params->mi_rows);
  blk_params->has_cols = (blk_params->mi_col_edge < mi_params->mi_cols);

  // Update intra partitioning related info.
  part_search_state->intra_part_info = &x->part_search_info;
  // Prepare for segmentation CNN-based partitioning for intra-frame.
  if (frame_is_intra_only(cm) && bsize == BLOCK_64X64) {
    part_search_state->intra_part_info->quad_tree_idx = 0;
    part_search_state->intra_part_info->cnn_output_valid = 0;
  }

  // Set partition plane context index.
  part_search_state->pl_ctx_idx =
      blk_params->bsize_at_least_8x8
          ? partition_plane_context(xd, mi_row, mi_col, bsize)
          : 0;

  // Partition cost buffer update
  ModeCosts *mode_costs = &x->mode_costs;
  part_search_state->partition_cost =
      mode_costs->partition_cost[part_search_state->pl_ctx_idx];

  // Initialize HORZ and VERT win flags as true for all split partitions.
  for (int i = 0; i < SUB_PARTITIONS_SPLIT; i++) {
    part_search_state->split_part_rect_win[i].rect_part_win[HORZ] = true;
    part_search_state->split_part_rect_win[i].rect_part_win[VERT] = true;
  }

  // Initialize the rd cost.
  av1_init_rd_stats(&part_search_state->this_rdc);

  // Initialize RD costs for partition types to 0.
  part_search_state->none_rd = 0;
  av1_zero(part_search_state->split_rd);
  av1_zero(part_search_state->rect_part_rd);

  // Initialize SPLIT partition to be not ready.
  av1_zero(part_search_state->is_split_ctx_is_ready);
  // Initialize HORZ and VERT partitions to be not ready.
  av1_zero(part_search_state->is_rect_ctx_is_ready);

  // Chroma subsampling.
  part_search_state->ss_x = x->e_mbd.plane[1].subsampling_x;
  part_search_state->ss_y = x->e_mbd.plane[1].subsampling_y;

  // Initialize partition search flags to defaults.
  part_search_state->terminate_partition_search = 0;
  part_search_state->do_square_split = blk_params->bsize_at_least_8x8;
  part_search_state->do_rectangular_split =
      cpi->oxcf.part_cfg.enable_rect_partitions &&
      blk_params->bsize_at_least_8x8;
  av1_zero(part_search_state->prune_rect_part);

  // Initialize allowed partition types for the partition block.
  part_search_state->partition_none_allowed =
      av1_blk_has_rows_and_cols(blk_params);
  part_search_state->partition_rect_allowed[HORZ] =
      part_search_state->do_rectangular_split && blk_params->has_cols &&
      get_plane_block_size(get_partition_subsize(bsize, PARTITION_HORZ),
                           part_search_state->ss_x,
                           part_search_state->ss_y) != BLOCK_INVALID;
  part_search_state->partition_rect_allowed[VERT] =
      part_search_state->do_rectangular_split && blk_params->has_rows &&
      get_plane_block_size(get_partition_subsize(bsize, PARTITION_VERT),
                           part_search_state->ss_x,
                           part_search_state->ss_y) != BLOCK_INVALID;

  // Reset the flag indicating whether a partition leading to a rdcost lower
  // than the bound best_rdc has been found.
  part_search_state->found_best_partition = false;

#if CONFIG_COLLECT_PARTITION_STATS
  init_partition_block_timing_stats(&part_search_state->part_timing_stats);
#endif  // CONFIG_COLLECT_PARTITION_STATS
}

// Override partition cost buffer for the edge blocks.
static void set_partition_cost_for_edge_blk(
    AV1_COMMON const *cm, PartitionSearchState *part_search_state) {
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  assert(blk_params.bsize_at_least_8x8 && part_search_state->pl_ctx_idx >= 0);
  const aom_cdf_prob *partition_cdf =
      cm->fc->partition_cdf[part_search_state->pl_ctx_idx];
  const int max_cost = av1_cost_symbol(0);
  for (PARTITION_TYPE i = 0; i < PARTITION_TYPES; ++i)
    part_search_state->tmp_partition_cost[i] = max_cost;
  if (blk_params.has_cols) {
    // At the bottom, the two possibilities are HORZ and SPLIT.
    aom_cdf_prob bot_cdf[2];
    partition_gather_vert_alike(bot_cdf, partition_cdf, blk_params.bsize);
    static const int bot_inv_map[2] = { PARTITION_HORZ, PARTITION_SPLIT };
    av1_cost_tokens_from_cdf(part_search_state->tmp_partition_cost, bot_cdf,
                             bot_inv_map);
  } else if (blk_params.has_rows) {
    // At the right, the two possibilities are VERT and SPLIT.
    aom_cdf_prob rhs_cdf[2];
    partition_gather_horz_alike(rhs_cdf, partition_cdf, blk_params.bsize);
    static const int rhs_inv_map[2] = { PARTITION_VERT, PARTITION_SPLIT };
    av1_cost_tokens_from_cdf(part_search_state->tmp_partition_cost, rhs_cdf,
                             rhs_inv_map);
  } else {
    // At the bottom right, we always split.
    part_search_state->tmp_partition_cost[PARTITION_SPLIT] = 0;
  }
  // Override the partition cost buffer.
  part_search_state->partition_cost = part_search_state->tmp_partition_cost;
}

// Reset the partition search state flags when
// must_find_valid_partition is equal to 1.
static inline void reset_part_limitations(
    AV1_COMP *const cpi, PartitionSearchState *part_search_state) {
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  const int is_rect_part_allowed =
      blk_params.bsize_at_least_8x8 &&
      cpi->oxcf.part_cfg.enable_rect_partitions &&
      (blk_params.width > blk_params.min_partition_size_1d);
  part_search_state->do_square_split =
      blk_params.bsize_at_least_8x8 &&
      (blk_params.width > blk_params.min_partition_size_1d);
  part_search_state->partition_none_allowed =
      av1_blk_has_rows_and_cols(&blk_params) &&
      (blk_params.width >= blk_params.min_partition_size_1d);
  part_search_state->partition_rect_allowed[HORZ] =
      blk_params.has_cols && is_rect_part_allowed &&
      get_plane_block_size(
          get_partition_subsize(blk_params.bsize, PARTITION_HORZ),
          part_search_state->ss_x, part_search_state->ss_y) != BLOCK_INVALID;
  part_search_state->partition_rect_allowed[VERT] =
      blk_params.has_rows && is_rect_part_allowed &&
      get_plane_block_size(
          get_partition_subsize(blk_params.bsize, PARTITION_VERT),
          part_search_state->ss_x, part_search_state->ss_y) != BLOCK_INVALID;
  part_search_state->terminate_partition_search = 0;
}

// Rectangular partitions evaluation at sub-block level.
static void rd_pick_rect_partition(AV1_COMP *const cpi, TileDataEnc *tile_data,
                                   MACROBLOCK *x,
                                   PICK_MODE_CONTEXT *cur_partition_ctx,
                                   PartitionSearchState *part_search_state,
                                   RD_STATS *best_rdc, const int idx,
                                   int mi_row, int mi_col, BLOCK_SIZE bsize,
                                   PARTITION_TYPE partition_type) {
  // Obtain the remainder from the best rd cost
  // for further processing of partition.
  RD_STATS best_remain_rdcost;
  av1_rd_stats_subtraction(x->rdmult, best_rdc, &part_search_state->sum_rdc,
                           &best_remain_rdcost);

  // Obtain the best mode for the partition sub-block.
  pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &part_search_state->this_rdc,
                partition_type, bsize, cur_partition_ctx, best_remain_rdcost);
  av1_rd_cost_update(x->rdmult, &part_search_state->this_rdc);

  // Update the partition rd cost with the current sub-block rd.
  if (part_search_state->this_rdc.rate == INT_MAX) {
    part_search_state->sum_rdc.rdcost = INT64_MAX;
  } else {
    part_search_state->sum_rdc.rate += part_search_state->this_rdc.rate;
    part_search_state->sum_rdc.dist += part_search_state->this_rdc.dist;
    av1_rd_cost_update(x->rdmult, &part_search_state->sum_rdc);
  }
  const RECT_PART_TYPE rect_part =
      partition_type == PARTITION_HORZ ? HORZ : VERT;
  part_search_state->rect_part_rd[rect_part][idx] =
      part_search_state->this_rdc.rdcost;
}

typedef int (*active_edge_info)(const AV1_COMP *cpi, int mi_col, int mi_step);

// Checks if HORZ / VERT partition search is allowed.
static inline int is_rect_part_allowed(
    const AV1_COMP *cpi, const PartitionSearchState *part_search_state,
    const active_edge_info *active_edge, RECT_PART_TYPE rect_part,
    const int mi_pos) {
  const PartitionBlkParams *blk_params = &part_search_state->part_blk_params;
  const int is_part_allowed =
      (!part_search_state->terminate_partition_search &&
       part_search_state->partition_rect_allowed[rect_part] &&
       !part_search_state->prune_rect_part[rect_part] &&
       (part_search_state->do_rectangular_split ||
        active_edge[rect_part](cpi, mi_pos, blk_params->mi_step)));
  return is_part_allowed;
}

static void rectangular_partition_search(
    AV1_COMP *const cpi, ThreadData *td, TileDataEnc *tile_data,
    TokenExtra **tp, MACROBLOCK *x, PC_TREE *pc_tree,
    RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    PartitionSearchState *part_search_state, RD_STATS *best_rdc,
    RD_RECT_PART_WIN_INFO *rect_part_win_info, const RECT_PART_TYPE start_type,
    const RECT_PART_TYPE end_type) {
  const AV1_COMMON *const cm = &cpi->common;
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  RD_STATS *sum_rdc = &part_search_state->sum_rdc;
  const int rect_partition_type[NUM_RECT_PARTS] = { PARTITION_HORZ,
                                                    PARTITION_VERT };

  // mi_pos_rect[NUM_RECT_PARTS][SUB_PARTITIONS_RECT][0]: mi_row postion of
  //                                           HORZ and VERT partition types.
  // mi_pos_rect[NUM_RECT_PARTS][SUB_PARTITIONS_RECT][1]: mi_col postion of
  //                                           HORZ and VERT partition types.
  const int mi_pos_rect[NUM_RECT_PARTS][SUB_PARTITIONS_RECT][2] = {
    { { blk_params.mi_row, blk_params.mi_col },
      { blk_params.mi_row_edge, blk_params.mi_col } },
    { { blk_params.mi_row, blk_params.mi_col },
      { blk_params.mi_row, blk_params.mi_col_edge } }
  };

  // Initialize active edge_type function pointer
  // for HOZR and VERT partition types.
  active_edge_info active_edge_type[NUM_RECT_PARTS] = { av1_active_h_edge,
                                                        av1_active_v_edge };

  // Indicates edge blocks for HORZ and VERT partition types.
  const int is_not_edge_block[NUM_RECT_PARTS] = { blk_params.has_rows,
                                                  blk_params.has_cols };

  // Initialize pc tree context for HORZ and VERT partition types.
  PICK_MODE_CONTEXT **cur_ctx[NUM_RECT_PARTS][SUB_PARTITIONS_RECT] = {
    { &pc_tree->horizontal[0], &pc_tree->horizontal[1] },
    { &pc_tree->vertical[0], &pc_tree->vertical[1] }
  };

  // Loop over rectangular partition types.
  for (RECT_PART_TYPE i = start_type; i <= end_type; i++) {
    assert(IMPLIES(!cpi->oxcf.part_cfg.enable_rect_partitions,
                   !part_search_state->partition_rect_allowed[i]));

    // Check if the HORZ / VERT partition search is to be performed.
    if (!is_rect_part_allowed(cpi, part_search_state, active_edge_type, i,
                              mi_pos_rect[i][0][i]))
      continue;

    // Sub-partition idx.
    int sub_part_idx = 0;
    PARTITION_TYPE partition_type = rect_partition_type[i];
    blk_params.subsize =
        get_partition_subsize(blk_params.bsize, partition_type);
    assert(blk_params.subsize <= BLOCK_LARGEST);
    av1_init_rd_stats(sum_rdc);
    for (int j = 0; j < SUB_PARTITIONS_RECT; j++) {
      if (cur_ctx[i][j][0] == NULL) {
        cur_ctx[i][j][0] =
            av1_alloc_pmc(cpi, blk_params.subsize, &td->shared_coeff_buf);
        if (!cur_ctx[i][j][0])
          aom_internal_error(x->e_mbd.error_info, AOM_CODEC_MEM_ERROR,
                             "Failed to allocate PICK_MODE_CONTEXT");
      }
    }
    sum_rdc->rate = part_search_state->partition_cost[partition_type];
    sum_rdc->rdcost = RDCOST(x->rdmult, sum_rdc->rate, 0);
#if CONFIG_COLLECT_PARTITION_STATS
    PartitionTimingStats *part_timing_stats =
        &part_search_state->part_timing_stats;
    if (best_rdc->rdcost - sum_rdc->rdcost >= 0) {
      start_partition_block_timer(part_timing_stats, partition_type);
    }
#endif

    // First sub-partition evaluation in HORZ / VERT partition type.
    rd_pick_rect_partition(
        cpi, tile_data, x, cur_ctx[i][sub_part_idx][0], part_search_state,
        best_rdc, 0, mi_pos_rect[i][sub_part_idx][0],
        mi_pos_rect[i][sub_part_idx][1], blk_params.subsize, partition_type);

    // Start of second sub-partition evaluation.
    // Evaluate second sub-partition if the first sub-partition cost
    // is less than the best cost and if it is not an edge block.
    if (sum_rdc->rdcost < best_rdc->rdcost && is_not_edge_block[i]) {
      const MB_MODE_INFO *const mbmi = &cur_ctx[i][sub_part_idx][0]->mic;
      const PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
      // Neither palette mode nor cfl predicted.
      if (pmi->palette_size[PLANE_TYPE_Y] == 0 &&
          pmi->palette_size[PLANE_TYPE_UV] == 0) {
        if (mbmi->uv_mode != UV_CFL_PRED)
          part_search_state->is_rect_ctx_is_ready[i] = 1;
      }
      av1_update_state(cpi, td, cur_ctx[i][sub_part_idx][0], blk_params.mi_row,
                       blk_params.mi_col, blk_params.subsize, DRY_RUN_NORMAL);
      encode_superblock(cpi, tile_data, td, tp, DRY_RUN_NORMAL,
                        blk_params.subsize, NULL);

      // Second sub-partition evaluation in HORZ / VERT partition type.
      sub_part_idx = 1;
      rd_pick_rect_partition(
          cpi, tile_data, x, cur_ctx[i][sub_part_idx][0], part_search_state,
          best_rdc, 1, mi_pos_rect[i][sub_part_idx][0],
          mi_pos_rect[i][sub_part_idx][1], blk_params.subsize, partition_type);
    }
    // Update HORZ / VERT best partition.
    if (sum_rdc->rdcost < best_rdc->rdcost) {
      sum_rdc->rdcost = RDCOST(x->rdmult, sum_rdc->rate, sum_rdc->dist);
      if (sum_rdc->rdcost < best_rdc->rdcost) {
        *best_rdc = *sum_rdc;
        part_search_state->found_best_partition = true;
        pc_tree->partitioning = partition_type;
      }
    } else {
      // Update HORZ / VERT win flag.
      if (rect_part_win_info != NULL)
        rect_part_win_info->rect_part_win[i] = false;
    }
#if CONFIG_COLLECT_PARTITION_STATS
    if (part_timing_stats->timer_is_on) {
      end_partition_block_timer(part_timing_stats, partition_type,
                                sum_rdc->rdcost);
    }
#endif
    av1_restore_context(x, x_ctx, blk_params.mi_row, blk_params.mi_col,
                        blk_params.bsize, av1_num_planes(cm));
  }
}

// AB partition type evaluation.
static void rd_pick_ab_part(
    AV1_COMP *const cpi, ThreadData *td, TileDataEnc *tile_data,
    TokenExtra **tp, MACROBLOCK *x, RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    PC_TREE *pc_tree, PICK_MODE_CONTEXT *dst_ctxs[SUB_PARTITIONS_AB],
    PartitionSearchState *part_search_state, RD_STATS *best_rdc,
    const BLOCK_SIZE ab_subsize[SUB_PARTITIONS_AB],
    const int ab_mi_pos[SUB_PARTITIONS_AB][2], const PARTITION_TYPE part_type,
    const MB_MODE_INFO **mode_cache) {
  const AV1_COMMON *const cm = &cpi->common;
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  const int mi_row = blk_params.mi_row;
  const int mi_col = blk_params.mi_col;
  const BLOCK_SIZE bsize = blk_params.bsize;
  int64_t this_rdcost = 0;

#if CONFIG_COLLECT_PARTITION_STATS
  PartitionTimingStats *part_timing_stats =
      &part_search_state->part_timing_stats;
  {
    RD_STATS tmp_sum_rdc;
    av1_init_rd_stats(&tmp_sum_rdc);
    tmp_sum_rdc.rate = part_search_state->partition_cost[part_type];
    tmp_sum_rdc.rdcost = RDCOST(x->rdmult, tmp_sum_rdc.rate, 0);
    if (best_rdc->rdcost - tmp_sum_rdc.rdcost >= 0) {
      start_partition_block_timer(part_timing_stats, part_type);
    }
  }
#endif

  // Test this partition and update the best partition.
  const bool find_best_ab_part = rd_test_partition3(
      cpi, td, tile_data, tp, pc_tree, best_rdc, &this_rdcost, dst_ctxs, mi_row,
      mi_col, bsize, part_type, ab_subsize, ab_mi_pos, mode_cache);
  part_search_state->found_best_partition |= find_best_ab_part;

#if CONFIG_COLLECT_PARTITION_STATS
  if (part_timing_stats->timer_is_on) {
    if (!find_best_ab_part) this_rdcost = INT64_MAX;
    end_partition_block_timer(part_timing_stats, part_type, this_rdcost);
  }
#endif
  av1_restore_context(x, x_ctx, mi_row, mi_col, bsize, av1_num_planes(cm));
}

// Set mode search context.
static inline void set_mode_search_ctx(
    PC_TREE *pc_tree, const int is_ctx_ready[NUM_AB_PARTS][2],
    PICK_MODE_CONTEXT **mode_srch_ctx[NUM_AB_PARTS][2]) {
  mode_srch_ctx[HORZ_B][0] = &pc_tree->horizontal[0];
  mode_srch_ctx[VERT_B][0] = &pc_tree->vertical[0];

  if (is_ctx_ready[HORZ_A][0])
    mode_srch_ctx[HORZ_A][0] = &pc_tree->split[0]->none;

  if (is_ctx_ready[VERT_A][0])
    mode_srch_ctx[VERT_A][0] = &pc_tree->split[0]->none;

  if (is_ctx_ready[HORZ_A][1])
    mode_srch_ctx[HORZ_A][1] = &pc_tree->split[1]->none;
}

static inline void copy_partition_mode_from_mode_context(
    const MB_MODE_INFO **dst_mode, const PICK_MODE_CONTEXT *ctx) {
  if (ctx && ctx->rd_stats.rate < INT_MAX) {
    *dst_mode = &ctx->mic;
  } else {
    *dst_mode = NULL;
  }
}

static inline void copy_partition_mode_from_pc_tree(
    const MB_MODE_INFO **dst_mode, const PC_TREE *pc_tree) {
  if (pc_tree) {
    copy_partition_mode_from_mode_context(dst_mode, pc_tree->none);
  } else {
    *dst_mode = NULL;
  }
}

static inline void set_mode_cache_for_partition_ab(
    const MB_MODE_INFO **mode_cache, const PC_TREE *pc_tree,
    AB_PART_TYPE ab_part_type) {
  switch (ab_part_type) {
    case HORZ_A:
      copy_partition_mode_from_pc_tree(&mode_cache[0], pc_tree->split[0]);
      copy_partition_mode_from_pc_tree(&mode_cache[1], pc_tree->split[1]);
      copy_partition_mode_from_mode_context(&mode_cache[2],
                                            pc_tree->horizontal[1]);
      break;
    case HORZ_B:
      copy_partition_mode_from_mode_context(&mode_cache[0],
                                            pc_tree->horizontal[0]);
      copy_partition_mode_from_pc_tree(&mode_cache[1], pc_tree->split[2]);
      copy_partition_mode_from_pc_tree(&mode_cache[2], pc_tree->split[3]);
      break;
    case VERT_A:
      copy_partition_mode_from_pc_tree(&mode_cache[0], pc_tree->split[0]);
      copy_partition_mode_from_pc_tree(&mode_cache[1], pc_tree->split[2]);
      copy_partition_mode_from_mode_context(&mode_cache[2],
                                            pc_tree->vertical[1]);
      break;
    case VERT_B:
      copy_partition_mode_from_mode_context(&mode_cache[0],
                                            pc_tree->vertical[0]);
      copy_partition_mode_from_pc_tree(&mode_cache[1], pc_tree->split[1]);
      copy_partition_mode_from_pc_tree(&mode_cache[2], pc_tree->split[3]);
      break;
    default: assert(0 && "Invalid ab partition type!\n");
  }
}

// AB Partitions type search.
static void ab_partitions_search(
    AV1_COMP *const cpi, ThreadData *td, TileDataEnc *tile_data,
    TokenExtra **tp, MACROBLOCK *x, RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    PC_TREE *pc_tree, PartitionSearchState *part_search_state,
    RD_STATS *best_rdc, RD_RECT_PART_WIN_INFO *rect_part_win_info,
    int pb_source_variance, int ext_partition_allowed,
    const AB_PART_TYPE start_type, const AB_PART_TYPE end_type) {
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  const int mi_row = blk_params.mi_row;
  const int mi_col = blk_params.mi_col;
  const BLOCK_SIZE bsize = blk_params.bsize;

  if (part_search_state->terminate_partition_search) {
    return;
  }

  int ab_partitions_allowed[NUM_AB_PARTS];
  // Prune AB partitions
  av1_prune_ab_partitions(cpi, x, pc_tree, pb_source_variance, best_rdc->rdcost,
                          rect_part_win_info, ext_partition_allowed,
                          part_search_state, ab_partitions_allowed);

  // Flags to indicate whether the mode search is done.
  const int is_ctx_ready[NUM_AB_PARTS][2] = {
    { part_search_state->is_split_ctx_is_ready[0],
      part_search_state->is_split_ctx_is_ready[1] },
    { part_search_state->is_rect_ctx_is_ready[HORZ], 0 },
    { part_search_state->is_split_ctx_is_ready[0], 0 },
    { part_search_state->is_rect_ctx_is_ready[VERT], 0 }
  };

  // Current partition context.
  PICK_MODE_CONTEXT **cur_part_ctxs[NUM_AB_PARTS] = { pc_tree->horizontala,
                                                      pc_tree->horizontalb,
                                                      pc_tree->verticala,
                                                      pc_tree->verticalb };

  // Context of already evaluted partition types.
  PICK_MODE_CONTEXT **mode_srch_ctx[NUM_AB_PARTS][2];
  // Set context of already evaluted partition types.
  set_mode_search_ctx(pc_tree, is_ctx_ready, mode_srch_ctx);

  // Array of sub-partition size of AB partition types.
  const BLOCK_SIZE ab_subsize[NUM_AB_PARTS][SUB_PARTITIONS_AB] = {
    { blk_params.split_bsize2, blk_params.split_bsize2,
      get_partition_subsize(bsize, PARTITION_HORZ_A) },
    { get_partition_subsize(bsize, PARTITION_HORZ_B), blk_params.split_bsize2,
      blk_params.split_bsize2 },
    { blk_params.split_bsize2, blk_params.split_bsize2,
      get_partition_subsize(bsize, PARTITION_VERT_A) },
    { get_partition_subsize(bsize, PARTITION_VERT_B), blk_params.split_bsize2,
      blk_params.split_bsize2 }
  };

  // Array of mi_row, mi_col positions corresponds to each sub-partition in AB
  // partition types.
  const int ab_mi_pos[NUM_AB_PARTS][SUB_PARTITIONS_AB][2] = {
    { { mi_row, mi_col },
      { mi_row, blk_params.mi_col_edge },
      { blk_params.mi_row_edge, mi_col } },
    { { mi_row, mi_col },
      { blk_params.mi_row_edge, mi_col },
      { blk_params.mi_row_edge, blk_params.mi_col_edge } },
    { { mi_row, mi_col },
      { blk_params.mi_row_edge, mi_col },
      { mi_row, blk_params.mi_col_edge } },
    { { mi_row, mi_col },
      { mi_row, blk_params.mi_col_edge },
      { blk_params.mi_row_edge, blk_params.mi_col_edge } }
  };

  // Loop over AB partition types.
  for (AB_PART_TYPE ab_part_type = start_type; ab_part_type <= end_type;
       ab_part_type++) {
    const PARTITION_TYPE part_type = ab_part_type + PARTITION_HORZ_A;

    // Check if the AB partition search is to be performed.
    if (!ab_partitions_allowed[ab_part_type]) {
      continue;
    }

    blk_params.subsize = get_partition_subsize(bsize, part_type);
    for (int i = 0; i < SUB_PARTITIONS_AB; i++) {
      // Set AB partition context.
      cur_part_ctxs[ab_part_type][i] = av1_alloc_pmc(
          cpi, ab_subsize[ab_part_type][i], &td->shared_coeff_buf);
      if (!cur_part_ctxs[ab_part_type][i])
        aom_internal_error(x->e_mbd.error_info, AOM_CODEC_MEM_ERROR,
                           "Failed to allocate PICK_MODE_CONTEXT");
      // Set mode as not ready.
      cur_part_ctxs[ab_part_type][i]->rd_mode_is_ready = 0;
    }

    if (cpi->sf.part_sf.reuse_prev_rd_results_for_part_ab) {
      // We can copy directly the mode search results if we have already
      // searched the current block and the contexts match.
      if (is_ctx_ready[ab_part_type][0]) {
        av1_copy_tree_context(cur_part_ctxs[ab_part_type][0],
                              mode_srch_ctx[ab_part_type][0][0]);
        cur_part_ctxs[ab_part_type][0]->mic.partition = part_type;
        cur_part_ctxs[ab_part_type][0]->rd_mode_is_ready = 1;
        if (is_ctx_ready[ab_part_type][1]) {
          av1_copy_tree_context(cur_part_ctxs[ab_part_type][1],
                                mode_srch_ctx[ab_part_type][1][0]);
          cur_part_ctxs[ab_part_type][1]->mic.partition = part_type;
          cur_part_ctxs[ab_part_type][1]->rd_mode_is_ready = 1;
        }
      }
    }

    // Even if the contexts don't match, we can still speed up by reusing the
    // previous prediction mode.
    const MB_MODE_INFO *mode_cache[3] = { NULL, NULL, NULL };
    if (cpi->sf.part_sf.reuse_best_prediction_for_part_ab) {
      set_mode_cache_for_partition_ab(mode_cache, pc_tree, ab_part_type);
    }

    // Evaluation of AB partition type.
    rd_pick_ab_part(cpi, td, tile_data, tp, x, x_ctx, pc_tree,
                    cur_part_ctxs[ab_part_type], part_search_state, best_rdc,
                    ab_subsize[ab_part_type], ab_mi_pos[ab_part_type],
                    part_type, mode_cache);
  }
}

// Set mi positions for HORZ4 / VERT4 sub-block partitions.
static void set_mi_pos_partition4(const int inc_step[NUM_PART4_TYPES],
                                  int mi_pos[SUB_PARTITIONS_PART4][2],
                                  const int mi_row, const int mi_col) {
  for (PART4_TYPES i = 0; i < SUB_PARTITIONS_PART4; i++) {
    mi_pos[i][0] = mi_row + i * inc_step[HORZ4];
    mi_pos[i][1] = mi_col + i * inc_step[VERT4];
  }
}

// Set context and RD cost for HORZ4 / VERT4 partition types.
static void set_4_part_ctx_and_rdcost(
    MACROBLOCK *x, const AV1_COMP *const cpi, ThreadData *td,
    PICK_MODE_CONTEXT *cur_part_ctx[SUB_PARTITIONS_PART4],
    PartitionSearchState *part_search_state, PARTITION_TYPE partition_type,
    BLOCK_SIZE bsize) {
  // Initialize sum_rdc RD cost structure.
  av1_init_rd_stats(&part_search_state->sum_rdc);
  const int subsize = get_partition_subsize(bsize, partition_type);
  part_search_state->sum_rdc.rate =
      part_search_state->partition_cost[partition_type];
  part_search_state->sum_rdc.rdcost =
      RDCOST(x->rdmult, part_search_state->sum_rdc.rate, 0);
  for (PART4_TYPES i = 0; i < SUB_PARTITIONS_PART4; ++i) {
    cur_part_ctx[i] = av1_alloc_pmc(cpi, subsize, &td->shared_coeff_buf);
    if (!cur_part_ctx[i])
      aom_internal_error(x->e_mbd.error_info, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate PICK_MODE_CONTEXT");
  }
}

// Partition search of HORZ4 / VERT4 partition types.
static void rd_pick_4partition(
    AV1_COMP *const cpi, ThreadData *td, TileDataEnc *tile_data,
    TokenExtra **tp, MACROBLOCK *x, RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    PC_TREE *pc_tree, PICK_MODE_CONTEXT *cur_part_ctx[SUB_PARTITIONS_PART4],
    PartitionSearchState *part_search_state, RD_STATS *best_rdc,
    const int inc_step[NUM_PART4_TYPES], PARTITION_TYPE partition_type) {
  const AV1_COMMON *const cm = &cpi->common;
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  // mi positions needed for HORZ4 and VERT4 partition types.
  int mi_pos_check[NUM_PART4_TYPES] = { cm->mi_params.mi_rows,
                                        cm->mi_params.mi_cols };
  const PART4_TYPES part4_idx = (partition_type != PARTITION_HORZ_4);
  int mi_pos[SUB_PARTITIONS_PART4][2];

  blk_params.subsize = get_partition_subsize(blk_params.bsize, partition_type);
  // Set partition context and RD cost.
  set_4_part_ctx_and_rdcost(x, cpi, td, cur_part_ctx, part_search_state,
                            partition_type, blk_params.bsize);
  // Set mi positions for sub-block sizes.
  set_mi_pos_partition4(inc_step, mi_pos, blk_params.mi_row, blk_params.mi_col);
#if CONFIG_COLLECT_PARTITION_STATS
  PartitionTimingStats *part_timing_stats =
      &part_search_state->part_timing_stats;
  if (best_rdc->rdcost - part_search_state->sum_rdc.rdcost >= 0) {
    start_partition_block_timer(part_timing_stats, partition_type);
  }
#endif
  // Loop over sub-block partitions.
  for (PART4_TYPES i = 0; i < SUB_PARTITIONS_PART4; ++i) {
    if (i > 0 && mi_pos[i][part4_idx] >= mi_pos_check[part4_idx]) break;

    // Sub-block evaluation of Horz4 / Vert4 partition type.
    cur_part_ctx[i]->rd_mode_is_ready = 0;
    if (!rd_try_subblock(
            cpi, td, tile_data, tp, (i == SUB_PARTITIONS_PART4 - 1),
            mi_pos[i][0], mi_pos[i][1], blk_params.subsize, *best_rdc,
            &part_search_state->sum_rdc, partition_type, cur_part_ctx[i])) {
      av1_invalid_rd_stats(&part_search_state->sum_rdc);
      break;
    }
  }

  // Calculate the total cost and update the best partition.
  av1_rd_cost_update(x->rdmult, &part_search_state->sum_rdc);
  if (part_search_state->sum_rdc.rdcost < best_rdc->rdcost) {
    *best_rdc = part_search_state->sum_rdc;
    part_search_state->found_best_partition = true;
    pc_tree->partitioning = partition_type;
  }
#if CONFIG_COLLECT_PARTITION_STATS
  if (part_timing_stats->timer_is_on) {
    end_partition_block_timer(part_timing_stats, partition_type,
                              part_search_state->sum_rdc.rdcost);
  }
#endif
  av1_restore_context(x, x_ctx, blk_params.mi_row, blk_params.mi_col,
                      blk_params.bsize, av1_num_planes(cm));
}

// Do not evaluate extended partitions if NONE partition is skippable.
static inline int prune_ext_part_none_skippable(
    PICK_MODE_CONTEXT *part_none, int must_find_valid_partition,
    int skip_non_sq_part_based_on_none, BLOCK_SIZE bsize) {
  if ((skip_non_sq_part_based_on_none >= 1) && (part_none != NULL)) {
    if (part_none->skippable && !must_find_valid_partition &&
        bsize >= BLOCK_16X16) {
      return 1;
    }
  }
  return 0;
}

// Allow ab partition search
static int allow_ab_partition_search(PartitionSearchState *part_search_state,
                                     PARTITION_SPEED_FEATURES *part_sf,
                                     PARTITION_TYPE curr_best_part,
                                     int must_find_valid_partition,
                                     int prune_ext_part_state,
                                     int64_t best_rdcost) {
  const PartitionBlkParams blk_params = part_search_state->part_blk_params;
  const BLOCK_SIZE bsize = blk_params.bsize;

  // Do not prune if there is no valid partition
  if (best_rdcost == INT64_MAX) return 1;

  // Determine bsize threshold to evaluate ab partitions
  BLOCK_SIZE ab_bsize_thresh = part_sf->ext_partition_eval_thresh;
  if (part_sf->ext_part_eval_based_on_cur_best && !must_find_valid_partition &&
      !(curr_best_part == PARTITION_HORZ || curr_best_part == PARTITION_VERT))
    ab_bsize_thresh = BLOCK_128X128;

  // ab partitions are only allowed for square block sizes BLOCK_16X16 or
  // higher, so ab_bsize_thresh must be large enough to exclude BLOCK_4X4 and
  // BLOCK_8X8.
  assert(ab_bsize_thresh >= BLOCK_8X8);

  int ab_partition_allowed =
      part_search_state->do_rectangular_split && bsize > ab_bsize_thresh &&
      av1_blk_has_rows_and_cols(&blk_params) && !prune_ext_part_state;

  return ab_partition_allowed;
}

// Prune 4-way partitions based on the number of horz/vert wins
// in the current block and sub-blocks in PARTITION_SPLIT.
static void prune_4_partition_using_split_info(
    AV1_COMP *const cpi, MACROBLOCK *x, PartitionSearchState *part_search_state,
    int part4_search_allowed[NUM_PART4_TYPES]) {
  PART4_TYPES cur_part[NUM_PART4_TYPES] = { HORZ4, VERT4 };
  // Count of child blocks in which HORZ or VERT partition has won
  int num_child_rect_win[NUM_RECT_PARTS] = { 0, 0 };
  // Prune HORZ4/VERT4 partitions based on number of HORZ/VERT winners of
  // split partiitons.
  // Conservative pruning for high quantizers.
  const int num_win_thresh = AOMMIN(3 * (MAXQ - x->qindex) / MAXQ + 1, 3);

  for (RECT_PART_TYPE i = HORZ; i < NUM_RECT_PARTS; i++) {
    if (!(cpi->sf.part_sf.prune_ext_part_using_split_info &&
          part4_search_allowed[cur_part[i]]))
      continue;
    // Loop over split partitions.
    // Get rectangular partitions winner info of split partitions.
    for (int idx = 0; idx < SUB_PARTITIONS_SPLIT; idx++)
      num_child_rect_win[i] +=
          (part_search_state->split_part_rect_win[idx].rect_part_win[i]) ? 1
                                                                         : 0;
    if (num_child_rect_win[i] < num_win_thresh) {
      part4_search_allowed[cur_part[i]] = 0;
    }
  }
}

// Prune 4-way partition search.
static void prune_4_way_partition_search(
    AV1_COMP *const cpi, MACROBLOCK *x, PC_TREE *pc_tree,
    PartitionSearchState *part_search_state, RD_STATS *best_rdc,
    int pb_source_variance, int prune_ext_part_state,
    int part4_search_allowed[NUM_PART4_TYPES]) {
  const PartitionBlkParams blk_params = part_search_state->part_blk_params;
  const BLOCK_SIZE bsize = blk_params.bsize;

  // Do not prune if there is no valid partition
  if (best_rdc->rdcost == INT64_MAX) return;

  // Determine bsize threshold to evaluate 4-way partitions
  BLOCK_SIZE part4_bsize_thresh = cpi->sf.part_sf.ext_partition_eval_thresh;
  if (cpi->sf.part_sf.ext_part_eval_based_on_cur_best &&
      !x->must_find_valid_partition && pc_tree->partitioning == PARTITION_NONE)
    part4_bsize_thresh = BLOCK_128X128;

  // 4-way partitions are only allowed for BLOCK_16X16, BLOCK_32X32, and
  // BLOCK_64X64, so part4_bsize_thresh must be large enough to exclude
  // BLOCK_4X4 and BLOCK_8X8.
  assert(part4_bsize_thresh >= BLOCK_8X8);

  bool partition4_allowed =
      part_search_state->do_rectangular_split && bsize > part4_bsize_thresh &&
      av1_blk_has_rows_and_cols(&blk_params) && !prune_ext_part_state;

  // Disable 4-way partition search flags for width less than a multiple of the
  // minimum partition width.
  if (blk_params.width < (blk_params.min_partition_size_1d
                          << cpi->sf.part_sf.prune_part4_search)) {
    part4_search_allowed[HORZ4] = 0;
    part4_search_allowed[VERT4] = 0;
    return;
  }

  PARTITION_TYPE cur_part[NUM_PART4_TYPES] = { PARTITION_HORZ_4,
                                               PARTITION_VERT_4 };
  const PartitionCfg *const part_cfg = &cpi->oxcf.part_cfg;
  // partition4_allowed is 1 if we can use a PARTITION_HORZ_4 or
  // PARTITION_VERT_4 for this block. This is almost the same as
  // partition4_allowed, except that we don't allow 128x32 or 32x128
  // blocks, so we require that bsize is not BLOCK_128X128.
  partition4_allowed &=
      part_cfg->enable_1to4_partitions && bsize != BLOCK_128X128;

  for (PART4_TYPES i = HORZ4; i < NUM_PART4_TYPES; i++) {
    part4_search_allowed[i] =
        partition4_allowed && part_search_state->partition_rect_allowed[i] &&
        get_plane_block_size(get_partition_subsize(bsize, cur_part[i]),
                             part_search_state->ss_x,
                             part_search_state->ss_y) != BLOCK_INVALID;
  }
  // Pruning: pruning out 4-way partitions based on the current best partition.
  if (cpi->sf.part_sf.prune_ext_partition_types_search_level == 2) {
    part4_search_allowed[HORZ4] &= (pc_tree->partitioning == PARTITION_HORZ ||
                                    pc_tree->partitioning == PARTITION_HORZ_A ||
                                    pc_tree->partitioning == PARTITION_HORZ_B ||
                                    pc_tree->partitioning == PARTITION_SPLIT ||
                                    pc_tree->partitioning == PARTITION_NONE);
    part4_search_allowed[VERT4] &= (pc_tree->partitioning == PARTITION_VERT ||
                                    pc_tree->partitioning == PARTITION_VERT_A ||
                                    pc_tree->partitioning == PARTITION_VERT_B ||
                                    pc_tree->partitioning == PARTITION_SPLIT ||
                                    pc_tree->partitioning == PARTITION_NONE);
  }

  // Pruning: pruning out some 4-way partitions using a DNN taking rd costs of
  // sub-blocks from basic partition types.
  if (cpi->sf.part_sf.ml_prune_partition && partition4_allowed &&
      part_search_state->partition_rect_allowed[HORZ] &&
      part_search_state->partition_rect_allowed[VERT]) {
    av1_ml_prune_4_partition(cpi, x, pc_tree->partitioning, best_rdc->rdcost,
                             part_search_state, part4_search_allowed,
                             pb_source_variance);
  }

  // Pruning: pruning out 4-way partitions based on the number of horz/vert wins
  // in the current block and sub-blocks in PARTITION_SPLIT.
  prune_4_partition_using_split_info(cpi, x, part_search_state,
                                     part4_search_allowed);
}

// Set params needed for PARTITION_NONE search.
static void set_none_partition_params(const AV1_COMP *const cpi, ThreadData *td,
                                      MACROBLOCK *x, PC_TREE *pc_tree,
                                      PartitionSearchState *part_search_state,
                                      RD_STATS *best_remain_rdcost,
                                      RD_STATS *best_rdc, int *pt_cost) {
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  RD_STATS partition_rdcost;
  // Set PARTITION_NONE context.
  if (pc_tree->none == NULL)
    pc_tree->none = av1_alloc_pmc(cpi, blk_params.bsize, &td->shared_coeff_buf);
  if (!pc_tree->none)
    aom_internal_error(x->e_mbd.error_info, AOM_CODEC_MEM_ERROR,
                       "Failed to allocate PICK_MODE_CONTEXT");

  // Set PARTITION_NONE type cost.
  if (part_search_state->partition_none_allowed) {
    if (blk_params.bsize_at_least_8x8) {
      *pt_cost = part_search_state->partition_cost[PARTITION_NONE] < INT_MAX
                     ? part_search_state->partition_cost[PARTITION_NONE]
                     : 0;
    }

    // Initialize the RD stats structure.
    av1_init_rd_stats(&partition_rdcost);
    partition_rdcost.rate = *pt_cost;
    av1_rd_cost_update(x->rdmult, &partition_rdcost);
    av1_rd_stats_subtraction(x->rdmult, best_rdc, &partition_rdcost,
                             best_remain_rdcost);
  }
}

// Skip other partitions based on PARTITION_NONE rd cost.
static void prune_partitions_after_none(AV1_COMP *const cpi, MACROBLOCK *x,
                                        SIMPLE_MOTION_DATA_TREE *sms_tree,
                                        PICK_MODE_CONTEXT *ctx_none,
                                        PartitionSearchState *part_search_state,
                                        RD_STATS *best_rdc,
                                        unsigned int *pb_source_variance) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  const PartitionBlkParams blk_params = part_search_state->part_blk_params;
  RD_STATS *this_rdc = &part_search_state->this_rdc;
  const BLOCK_SIZE bsize = blk_params.bsize;
  assert(bsize < BLOCK_SIZES_ALL);

  if (!frame_is_intra_only(cm) &&
      (part_search_state->do_square_split ||
       part_search_state->do_rectangular_split) &&
      !x->e_mbd.lossless[xd->mi[0]->segment_id] && ctx_none->skippable) {
    const int use_ml_based_breakout =
        bsize <= cpi->sf.part_sf.use_square_partition_only_threshold &&
        bsize > BLOCK_4X4 && cpi->sf.part_sf.ml_predict_breakout_level >= 1;
    if (use_ml_based_breakout) {
      av1_ml_predict_breakout(cpi, x, this_rdc, *pb_source_variance, xd->bd,
                              part_search_state);
    }

    // Adjust dist breakout threshold according to the partition size.
    const int64_t dist_breakout_thr =
        cpi->sf.part_sf.partition_search_breakout_dist_thr >>
        ((2 * (MAX_SB_SIZE_LOG2 - 2)) -
         (mi_size_wide_log2[bsize] + mi_size_high_log2[bsize]));
    const int rate_breakout_thr =
        cpi->sf.part_sf.partition_search_breakout_rate_thr *
        num_pels_log2_lookup[bsize];
    // If all y, u, v transform blocks in this partition are skippable,
    // and the dist & rate are within the thresholds, the partition
    // search is terminated for current branch of the partition search
    // tree. The dist & rate thresholds are set to 0 at speed 0 to
    // disable the early termination at that speed.
    if (best_rdc->dist < dist_breakout_thr &&
        best_rdc->rate < rate_breakout_thr) {
      part_search_state->do_square_split = 0;
      part_search_state->do_rectangular_split = 0;
    }
  }

  // Early termination: using simple_motion_search features and the
  // rate, distortion, and rdcost of PARTITION_NONE, a DNN will make a
  // decision on early terminating at PARTITION_NONE.
  if (cpi->sf.part_sf.simple_motion_search_early_term_none && cm->show_frame &&
      !frame_is_intra_only(cm) && bsize >= BLOCK_16X16 &&
      av1_blk_has_rows_and_cols(&blk_params) && this_rdc->rdcost < INT64_MAX &&
      this_rdc->rdcost >= 0 && this_rdc->rate < INT_MAX &&
      this_rdc->rate >= 0 &&
      (part_search_state->do_square_split ||
       part_search_state->do_rectangular_split)) {
    av1_simple_motion_search_early_term_none(cpi, x, sms_tree, this_rdc,
                                             part_search_state);
  }
}

// Decide early termination and rectangular partition pruning
// based on PARTITION_NONE and PARTITION_SPLIT costs.
static void prune_partitions_after_split(
    AV1_COMP *const cpi, MACROBLOCK *x, SIMPLE_MOTION_DATA_TREE *sms_tree,
    PartitionSearchState *part_search_state, RD_STATS *best_rdc,
    int64_t part_none_rd, int64_t part_split_rd) {
  const AV1_COMMON *const cm = &cpi->common;
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  const int mi_row = blk_params.mi_row;
  const int mi_col = blk_params.mi_col;
  const BLOCK_SIZE bsize = blk_params.bsize;
  assert(bsize < BLOCK_SIZES_ALL);

  // Early termination: using the rd costs of PARTITION_NONE and subblocks
  // from PARTITION_SPLIT to determine an early breakout.
  if (cpi->sf.part_sf.ml_early_term_after_part_split_level &&
      !frame_is_intra_only(cm) &&
      !part_search_state->terminate_partition_search &&
      part_search_state->do_rectangular_split &&
      (part_search_state->partition_rect_allowed[HORZ] ||
       part_search_state->partition_rect_allowed[VERT])) {
    av1_ml_early_term_after_split(
        cpi, x, sms_tree, best_rdc->rdcost, part_none_rd, part_split_rd,
        part_search_state->split_rd, part_search_state);
  }

  // Use the rd costs of PARTITION_NONE and subblocks from PARTITION_SPLIT
  // to prune out rectangular partitions in some directions.
  if (!cpi->sf.part_sf.ml_early_term_after_part_split_level &&
      cpi->sf.part_sf.ml_prune_partition && !frame_is_intra_only(cm) &&
      (part_search_state->partition_rect_allowed[HORZ] ||
       part_search_state->partition_rect_allowed[VERT]) &&
      !(part_search_state->prune_rect_part[HORZ] ||
        part_search_state->prune_rect_part[VERT]) &&
      !part_search_state->terminate_partition_search) {
    av1_setup_src_planes(x, cpi->source, mi_row, mi_col, av1_num_planes(cm),
                         bsize);
    av1_ml_prune_rect_partition(cpi, x, best_rdc->rdcost,
                                part_search_state->none_rd,
                                part_search_state->split_rd, part_search_state);
  }
}

// Returns true if either of the left and top neighbor blocks is larger than
// the current block; false otherwise.
static inline bool is_neighbor_blk_larger_than_cur_blk(const MACROBLOCKD *xd,
                                                       BLOCK_SIZE bsize) {
  const int cur_blk_area = (block_size_high[bsize] * block_size_wide[bsize]);
  if (xd->left_available) {
    const BLOCK_SIZE left_bsize = xd->left_mbmi->bsize;
    if (block_size_high[left_bsize] * block_size_wide[left_bsize] >
        cur_blk_area)
      return true;
  }

  if (xd->up_available) {
    const BLOCK_SIZE above_bsize = xd->above_mbmi->bsize;
    if (block_size_high[above_bsize] * block_size_wide[above_bsize] >
        cur_blk_area)
      return true;
  }
  return false;
}

static inline void prune_rect_part_using_none_pred_mode(
    const MACROBLOCKD *xd, PartitionSearchState *part_state,
    PREDICTION_MODE mode, BLOCK_SIZE bsize) {
  if (mode == DC_PRED || mode == SMOOTH_PRED) {
    // If the prediction mode of NONE partition is either DC_PRED or
    // SMOOTH_PRED, it indicates that the current block has less variation. In
    // this case, HORZ and VERT partitions are pruned if at least one of left
    // and top neighbor blocks is larger than the current block.
    if (is_neighbor_blk_larger_than_cur_blk(xd, bsize)) {
      part_state->prune_rect_part[HORZ] = 1;
      part_state->prune_rect_part[VERT] = 1;
    }
  } else if (mode == D67_PRED || mode == V_PRED || mode == D113_PRED) {
    // If the prediction mode chosen by NONE partition is close to 90 degrees,
    // it implies a dominant vertical pattern, and the chance of choosing a
    // vertical rectangular partition is high. Hence, horizontal partition is
    // pruned in these cases.
    part_state->prune_rect_part[HORZ] = 1;
  } else if (mode == D157_PRED || mode == H_PRED || mode == D203_PRED) {
    // If the prediction mode chosen by NONE partition is close to 180 degrees,
    // it implies a dominant horizontal pattern, and the chance of choosing a
    // horizontal rectangular partition is high. Hence, vertical partition is
    // pruned in these cases.
    part_state->prune_rect_part[VERT] = 1;
  }
}

// PARTITION_NONE search.
static void none_partition_search(
    AV1_COMP *const cpi, ThreadData *td, TileDataEnc *tile_data, MACROBLOCK *x,
    PC_TREE *pc_tree, SIMPLE_MOTION_DATA_TREE *sms_tree,
    RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    PartitionSearchState *part_search_state, RD_STATS *best_rdc,
    unsigned int *pb_source_variance, int64_t *none_rd, int64_t *part_none_rd) {
  const AV1_COMMON *const cm = &cpi->common;
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  RD_STATS *this_rdc = &part_search_state->this_rdc;
  const int mi_row = blk_params.mi_row;
  const int mi_col = blk_params.mi_col;
  const BLOCK_SIZE bsize = blk_params.bsize;
  assert(bsize < BLOCK_SIZES_ALL);

  if (part_search_state->terminate_partition_search ||
      !part_search_state->partition_none_allowed)
    return;

  int pt_cost = 0;
  RD_STATS best_remain_rdcost;
  av1_invalid_rd_stats(&best_remain_rdcost);

  // Set PARTITION_NONE context and cost.
  set_none_partition_params(cpi, td, x, pc_tree, part_search_state,
                            &best_remain_rdcost, best_rdc, &pt_cost);

#if CONFIG_COLLECT_PARTITION_STATS
  // Timer start for partition None.
  PartitionTimingStats *part_timing_stats =
      &part_search_state->part_timing_stats;
  if (best_remain_rdcost.rdcost >= 0) {
    start_partition_block_timer(part_timing_stats, PARTITION_NONE);
  }
#endif
  // PARTITION_NONE evaluation and cost update.
  pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, this_rdc, PARTITION_NONE,
                bsize, pc_tree->none, best_remain_rdcost);

  av1_rd_cost_update(x->rdmult, this_rdc);

#if CONFIG_COLLECT_PARTITION_STATS
  // Timer end for partition None.
  if (part_timing_stats->timer_is_on) {
    RD_STATS tmp_rdc;
    av1_init_rd_stats(&tmp_rdc);
    if (this_rdc->rate != INT_MAX) {
      tmp_rdc.rate = this_rdc->rate;
      tmp_rdc.dist = this_rdc->dist;
      tmp_rdc.rdcost = this_rdc->rdcost;
      if (blk_params.bsize_at_least_8x8) {
        tmp_rdc.rate += pt_cost;
        tmp_rdc.rdcost = RDCOST(x->rdmult, tmp_rdc.rate, tmp_rdc.dist);
      }
    }
    end_partition_block_timer(part_timing_stats, PARTITION_NONE,
                              tmp_rdc.rdcost);
  }
#endif
  *pb_source_variance = x->source_variance;
  if (none_rd) *none_rd = this_rdc->rdcost;
  part_search_state->none_rd = this_rdc->rdcost;
  if (this_rdc->rate != INT_MAX) {
    // Record picked ref frame to prune ref frames for other partition types.
    if (cpi->sf.inter_sf.prune_ref_frame_for_rect_partitions) {
      const int ref_type = av1_ref_frame_type(pc_tree->none->mic.ref_frame);
      av1_update_picked_ref_frames_mask(
          x, ref_type, bsize, cm->seq_params->mib_size, mi_row, mi_col);
    }

    // Calculate the total cost and update the best partition.
    if (blk_params.bsize_at_least_8x8) {
      this_rdc->rate += pt_cost;
      this_rdc->rdcost = RDCOST(x->rdmult, this_rdc->rate, this_rdc->dist);
    }
    *part_none_rd = this_rdc->rdcost;
    if (this_rdc->rdcost < best_rdc->rdcost) {
      *best_rdc = *this_rdc;
      part_search_state->found_best_partition = true;
      if (blk_params.bsize_at_least_8x8) {
        pc_tree->partitioning = PARTITION_NONE;
      }

      // Disable split and rectangular partition search
      // based on PARTITION_NONE cost.
      prune_partitions_after_none(cpi, x, sms_tree, pc_tree->none,
                                  part_search_state, best_rdc,
                                  pb_source_variance);
    }

    if (cpi->sf.part_sf.prune_rect_part_using_none_pred_mode)
      prune_rect_part_using_none_pred_mode(&x->e_mbd, part_search_state,
                                           pc_tree->none->mic.mode, bsize);
  }
  av1_restore_context(x, x_ctx, mi_row, mi_col, bsize, av1_num_planes(cm));
}

// PARTITION_SPLIT search.
static void split_partition_search(
    AV1_COMP *const cpi, ThreadData *td, TileDataEnc *tile_data,
    TokenExtra **tp, MACROBLOCK *x, PC_TREE *pc_tree,
    SIMPLE_MOTION_DATA_TREE *sms_tree, RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    PartitionSearchState *part_search_state, RD_STATS *best_rdc,
    SB_MULTI_PASS_MODE multi_pass_mode, int64_t *part_split_rd) {
  const AV1_COMMON *const cm = &cpi->common;
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int mi_row = blk_params.mi_row;
  const int mi_col = blk_params.mi_col;
  const BLOCK_SIZE bsize = blk_params.bsize;
  assert(bsize < BLOCK_SIZES_ALL);
  RD_STATS sum_rdc = part_search_state->sum_rdc;
  const BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);

  // Check if partition split is allowed.
  if (part_search_state->terminate_partition_search ||
      !part_search_state->do_square_split)
    return;

  for (int i = 0; i < SUB_PARTITIONS_SPLIT; ++i) {
    if (pc_tree->split[i] == NULL)
      pc_tree->split[i] = av1_alloc_pc_tree_node(subsize);
    if (!pc_tree->split[i])
      aom_internal_error(x->e_mbd.error_info, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate PC_TREE");
    pc_tree->split[i]->index = i;
  }

  // Initialization of this partition RD stats.
  av1_init_rd_stats(&sum_rdc);
  sum_rdc.rate = part_search_state->partition_cost[PARTITION_SPLIT];
  sum_rdc.rdcost = RDCOST(x->rdmult, sum_rdc.rate, 0);

  int idx;
#if CONFIG_COLLECT_PARTITION_STATS
  PartitionTimingStats *part_timing_stats =
      &part_search_state->part_timing_stats;
  if (best_rdc->rdcost - sum_rdc.rdcost >= 0) {
    start_partition_block_timer(part_timing_stats, PARTITION_SPLIT);
  }
#endif
  // Recursive partition search on 4 sub-blocks.
  for (idx = 0; idx < SUB_PARTITIONS_SPLIT && sum_rdc.rdcost < best_rdc->rdcost;
       ++idx) {
    const int x_idx = (idx & 1) * blk_params.mi_step;
    const int y_idx = (idx >> 1) * blk_params.mi_step;

    if (mi_row + y_idx >= mi_params->mi_rows ||
        mi_col + x_idx >= mi_params->mi_cols)
      continue;

    pc_tree->split[idx]->index = idx;
    int64_t *p_split_rd = &part_search_state->split_rd[idx];
    RD_STATS best_remain_rdcost;
    av1_rd_stats_subtraction(x->rdmult, best_rdc, &sum_rdc,
                             &best_remain_rdcost);

    int curr_quad_tree_idx = 0;
    if (frame_is_intra_only(cm) && bsize <= BLOCK_64X64) {
      curr_quad_tree_idx = part_search_state->intra_part_info->quad_tree_idx;
      part_search_state->intra_part_info->quad_tree_idx =
          4 * curr_quad_tree_idx + idx + 1;
    }
    // Split partition evaluation of corresponding idx.
    // If the RD cost exceeds the best cost then do not
    // evaluate other split sub-partitions.
    SIMPLE_MOTION_DATA_TREE *const sms_tree_split =
        (sms_tree == NULL) ? NULL : sms_tree->split[idx];
    if (!av1_rd_pick_partition(
            cpi, td, tile_data, tp, mi_row + y_idx, mi_col + x_idx, subsize,
            &part_search_state->this_rdc, best_remain_rdcost,
            pc_tree->split[idx], sms_tree_split, p_split_rd, multi_pass_mode,
            &part_search_state->split_part_rect_win[idx])) {
      av1_invalid_rd_stats(&sum_rdc);
      break;
    }
    if (frame_is_intra_only(cm) && bsize <= BLOCK_64X64) {
      part_search_state->intra_part_info->quad_tree_idx = curr_quad_tree_idx;
    }

    sum_rdc.rate += part_search_state->this_rdc.rate;
    sum_rdc.dist += part_search_state->this_rdc.dist;
    av1_rd_cost_update(x->rdmult, &sum_rdc);

    // Set split ctx as ready for use.
    if (idx <= 1 && (bsize <= BLOCK_8X8 ||
                     pc_tree->split[idx]->partitioning == PARTITION_NONE)) {
      const MB_MODE_INFO *const mbmi = &pc_tree->split[idx]->none->mic;
      const PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
      // Neither palette mode nor cfl predicted.
      if (pmi->palette_size[0] == 0 && pmi->palette_size[1] == 0) {
        if (mbmi->uv_mode != UV_CFL_PRED)
          part_search_state->is_split_ctx_is_ready[idx] = 1;
      }
    }
  }
#if CONFIG_COLLECT_PARTITION_STATS
  if (part_timing_stats->timer_is_on) {
    end_partition_block_timer(part_timing_stats, PARTITION_SPLIT,
                              sum_rdc.rdcost);
  }
#endif
  const int reached_last_index = (idx == SUB_PARTITIONS_SPLIT);

  // Calculate the total cost and update the best partition.
  *part_split_rd = sum_rdc.rdcost;
  if (reached_last_index && sum_rdc.rdcost < best_rdc->rdcost) {
    sum_rdc.rdcost = RDCOST(x->rdmult, sum_rdc.rate, sum_rdc.dist);
    if (sum_rdc.rdcost < best_rdc->rdcost) {
      *best_rdc = sum_rdc;
      part_search_state->found_best_partition = true;
      pc_tree->partitioning = PARTITION_SPLIT;
    }
  } else if (cpi->sf.part_sf.less_rectangular_check_level > 0) {
    // Skip rectangular partition test when partition type none gives better
    // rd than partition type split.
    if (cpi->sf.part_sf.less_rectangular_check_level == 2 || idx <= 2) {
      const int partition_none_valid = part_search_state->none_rd > 0;
      const int partition_none_better =
          part_search_state->none_rd < sum_rdc.rdcost;
      part_search_state->do_rectangular_split &=
          !(partition_none_valid && partition_none_better);
    }
  }
  // Restore the context for the following cases:
  // 1) Current block size not more than maximum partition size as dry run
  // encode happens for these cases
  // 2) Current block size same as superblock size as the final encode
  // happens for this case
  if (bsize <= x->sb_enc.max_partition_size || bsize == cm->seq_params->sb_size)
    av1_restore_context(x, x_ctx, mi_row, mi_col, bsize, av1_num_planes(cm));
}

// The max number of nodes in the partition tree.
// The number of leaf nodes is (128x128) / (4x4) = 1024.
// The number of All possible parent nodes is 1 + 2 + ... + 512 = 1023.
#define NUM_NODES 2048

static void write_partition_tree(AV1_COMP *const cpi,
                                 const PC_TREE *const pc_tree,
                                 const BLOCK_SIZE bsize, const int mi_row,
                                 const int mi_col) {
  (void)mi_row;
  (void)mi_col;
  const char *path = cpi->oxcf.partition_info_path;
  char filename[256];
  snprintf(filename, sizeof(filename), "%s/partition_tree_sb%d_c%d", path,
           cpi->sb_counter, 0);
  FILE *pfile = fopen(filename, "w");
  fprintf(pfile, "%d", bsize);

  // Write partition type with BFS order.
  const PC_TREE *tree_node_queue[NUM_NODES] = { NULL };
  int q_idx = 0;
  int last_idx = 1;
  int num_nodes = 1;

  // First traversal to get number of leaf nodes.
  tree_node_queue[q_idx] = pc_tree;
  while (num_nodes > 0) {
    const PC_TREE *node = tree_node_queue[q_idx];
    if (node->partitioning == PARTITION_SPLIT) {
      for (int i = 0; i < 4; ++i) {
        tree_node_queue[last_idx] = node->split[i];
        ++last_idx;
      }
      num_nodes += 4;
    }
    --num_nodes;
    ++q_idx;
  }
  const int num_leafs = last_idx;
  fprintf(pfile, ",%d,%d", num_leafs, /*num_configs=*/1);

  // Write partitions for each node.
  q_idx = 0;
  last_idx = 1;
  num_nodes = 1;
  tree_node_queue[q_idx] = pc_tree;
  while (num_nodes > 0) {
    const PC_TREE *node = tree_node_queue[q_idx];
    fprintf(pfile, ",%d", node->partitioning);
    if (node->partitioning == PARTITION_SPLIT) {
      for (int i = 0; i < 4; ++i) {
        tree_node_queue[last_idx] = node->split[i];
        ++last_idx;
      }
      num_nodes += 4;
    }
    --num_nodes;
    ++q_idx;
  }
  fprintf(pfile, "\n");

  fclose(pfile);
}

#if CONFIG_PARTITION_SEARCH_ORDER
static void verify_write_partition_tree(const AV1_COMP *const cpi,
                                        const PC_TREE *const pc_tree,
                                        const BLOCK_SIZE bsize,
                                        const int config_id, const int mi_row,
                                        const int mi_col) {
  (void)mi_row;
  (void)mi_col;
  const char *path = cpi->oxcf.partition_info_path;
  char filename[256];
  snprintf(filename, sizeof(filename), "%s/verify_partition_tree_sb%d_c%d",
           path, cpi->sb_counter, config_id);
  FILE *pfile = fopen(filename, "w");
  fprintf(pfile, "%d", bsize);

  // Write partition type with BFS order.
  const PC_TREE *tree_node_queue[NUM_NODES] = { NULL };
  int q_idx = 0;
  int last_idx = 1;
  int num_nodes = 1;

  // First traversal to get number of leaf nodes.
  tree_node_queue[q_idx] = pc_tree;
  while (num_nodes > 0) {
    const PC_TREE *node = tree_node_queue[q_idx];
    if (node != NULL && node->partitioning == PARTITION_SPLIT) {
      for (int i = 0; i < 4; ++i) {
        tree_node_queue[last_idx] = node->split[i];
        ++last_idx;
      }
      num_nodes += 4;
    }
    --num_nodes;
    ++q_idx;
  }
  const int num_leafs = last_idx;
  fprintf(pfile, ",%d,%d", num_leafs, /*num_configs=*/1);

  // Write partitions for each node.
  q_idx = 0;
  last_idx = 1;
  num_nodes = 1;
  tree_node_queue[q_idx] = pc_tree;
  while (num_nodes > 0) {
    const PC_TREE *node = tree_node_queue[q_idx];
    if (node != NULL) {  // suppress warning
      fprintf(pfile, ",%d", node->partitioning);
      if (node->partitioning == PARTITION_SPLIT) {
        for (int i = 0; i < 4; ++i) {
          tree_node_queue[last_idx] = node->split[i];
          ++last_idx;
        }
        num_nodes += 4;
      }
    }
    --num_nodes;
    ++q_idx;
  }
  fprintf(pfile, "\n");

  fclose(pfile);
}

static int read_partition_tree(AV1_COMP *const cpi, PC_TREE *const pc_tree,
                               struct aom_internal_error_info *error_info,
                               const int config_id) {
  const AV1_COMMON *const cm = &cpi->common;
  const char *path = cpi->oxcf.partition_info_path;
  char filename[256];
  snprintf(filename, sizeof(filename), "%s/partition_tree_sb%d_c%d", path,
           cpi->sb_counter, config_id);
  FILE *pfile = fopen(filename, "r");
  if (pfile == NULL) {
    aom_internal_error(cm->error, AOM_CODEC_ERROR, "Can't find input file: %s.",
                       filename);
  }

  int read_bsize;
  int num_nodes;
  int num_configs;
  fscanf(pfile, "%d,%d,%d", &read_bsize, &num_nodes, &num_configs);
  assert(read_bsize == cpi->common.seq_params->sb_size);
  BLOCK_SIZE bsize = (BLOCK_SIZE)read_bsize;
  assert(bsize == pc_tree->block_size);

  PC_TREE *tree_node_queue[NUM_NODES] = { NULL };
  int last_idx = 1;
  int q_idx = 0;
  tree_node_queue[q_idx] = pc_tree;
  while (num_nodes > 0) {
    int partitioning;
    fscanf(pfile, ",%d", &partitioning);
    assert(partitioning >= PARTITION_NONE &&
           partitioning < EXT_PARTITION_TYPES);
    PC_TREE *node = tree_node_queue[q_idx];
    if (node != NULL) {
      node->partitioning = partitioning;
      bsize = node->block_size;
    }
    if (partitioning == PARTITION_SPLIT) {
      const BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);
      for (int i = 0; i < 4; ++i) {
        if (node != NULL) {  // Suppress warning
          node->split[i] = av1_alloc_pc_tree_node(subsize);
          if (!node->split[i])
            aom_internal_error(error_info, AOM_CODEC_MEM_ERROR,
                               "Failed to allocate PC_TREE");
          node->split[i]->index = i;
          tree_node_queue[last_idx] = node->split[i];
          ++last_idx;
        }
      }
    }
    --num_nodes;
    ++q_idx;
  }
  fclose(pfile);

  return num_configs;
}

static RD_STATS rd_search_for_fixed_partition(
    AV1_COMP *const cpi, ThreadData *td, TileDataEnc *tile_data,
    TokenExtra **tp, SIMPLE_MOTION_DATA_TREE *sms_tree, int mi_row, int mi_col,
    const BLOCK_SIZE bsize, PC_TREE *pc_tree) {
  const PARTITION_TYPE partition = pc_tree->partitioning;
  const AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  TileInfo *const tile_info = &tile_data->tile_info;
  RD_STATS best_rdc;
  av1_invalid_rd_stats(&best_rdc);
  int sum_subblock_rate = 0;
  int64_t sum_subblock_dist = 0;
  PartitionSearchState part_search_state;
  init_partition_search_state_params(x, cpi, &part_search_state, mi_row, mi_col,
                                     bsize);
  // Override partition costs at the edges of the frame in the same
  // way as in read_partition (see decodeframe.c).
  PartitionBlkParams blk_params = part_search_state.part_blk_params;
  if (!av1_blk_has_rows_and_cols(&blk_params))
    set_partition_cost_for_edge_blk(cm, &part_search_state);

  av1_set_offsets(cpi, tile_info, x, mi_row, mi_col, bsize);

  // Save rdmult before it might be changed, so it can be restored later.
  const int orig_rdmult = x->rdmult;
  setup_block_rdmult(cpi, x, mi_row, mi_col, bsize, NO_AQ, NULL);
  (void)orig_rdmult;

  // Set the context.
  RD_SEARCH_MACROBLOCK_CONTEXT x_ctx;
  xd->above_txfm_context =
      cm->above_contexts.txfm[tile_info->tile_row] + mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (mi_row & MAX_MIB_MASK);
  av1_save_context(x, &x_ctx, mi_row, mi_col, bsize, num_planes);

  assert(bsize < BLOCK_SIZES_ALL);
  unsigned int pb_source_variance = UINT_MAX;
  int64_t part_none_rd = INT64_MAX;
  int64_t none_rd = INT64_MAX;
  int inc_step[NUM_PART4_TYPES] = { 0 };
  if (partition == PARTITION_HORZ_4) inc_step[HORZ4] = mi_size_high[bsize] / 4;
  if (partition == PARTITION_VERT_4) inc_step[VERT4] = mi_size_wide[bsize] / 4;

  switch (partition) {
    case PARTITION_NONE:
      none_partition_search(cpi, td, tile_data, x, pc_tree, sms_tree, &x_ctx,
                            &part_search_state, &best_rdc, &pb_source_variance,
                            &none_rd, &part_none_rd);
      break;
    case PARTITION_HORZ:
      rectangular_partition_search(cpi, td, tile_data, tp, x, pc_tree, &x_ctx,
                                   &part_search_state, &best_rdc, NULL, HORZ,
                                   HORZ);
      break;
    case PARTITION_VERT:
      rectangular_partition_search(cpi, td, tile_data, tp, x, pc_tree, &x_ctx,
                                   &part_search_state, &best_rdc, NULL, VERT,
                                   VERT);
      break;
    case PARTITION_HORZ_A:
      ab_partitions_search(cpi, td, tile_data, tp, x, &x_ctx, pc_tree,
                           &part_search_state, &best_rdc, NULL,
                           pb_source_variance, 1, HORZ_A, HORZ_A);
      break;
    case PARTITION_HORZ_B:
      ab_partitions_search(cpi, td, tile_data, tp, x, &x_ctx, pc_tree,
                           &part_search_state, &best_rdc, NULL,
                           pb_source_variance, 1, HORZ_B, HORZ_B);
      break;
    case PARTITION_VERT_A:
      ab_partitions_search(cpi, td, tile_data, tp, x, &x_ctx, pc_tree,
                           &part_search_state, &best_rdc, NULL,
                           pb_source_variance, 1, VERT_A, VERT_A);
      break;
    case PARTITION_VERT_B:
      ab_partitions_search(cpi, td, tile_data, tp, x, &x_ctx, pc_tree,
                           &part_search_state, &best_rdc, NULL,
                           pb_source_variance, 1, VERT_B, VERT_B);
      break;
    case PARTITION_HORZ_4:
      rd_pick_4partition(cpi, td, tile_data, tp, x, &x_ctx, pc_tree,
                         pc_tree->horizontal4, &part_search_state, &best_rdc,
                         inc_step, PARTITION_HORZ_4);
      break;
    case PARTITION_VERT_4:
      rd_pick_4partition(cpi, td, tile_data, tp, x, &x_ctx, pc_tree,
                         pc_tree->vertical4, &part_search_state, &best_rdc,
                         inc_step, PARTITION_VERT_4);
      break;
    case PARTITION_SPLIT:
      for (int idx = 0; idx < SUB_PARTITIONS_SPLIT; ++idx) {
        const BLOCK_SIZE subsize =
            get_partition_subsize(bsize, PARTITION_SPLIT);
        assert(subsize < BLOCK_SIZES_ALL);
        const int next_mi_row =
            idx < 2 ? mi_row : mi_row + mi_size_high[subsize];
        const int next_mi_col =
            idx % 2 == 0 ? mi_col : mi_col + mi_size_wide[subsize];
        if (next_mi_row >= cm->mi_params.mi_rows ||
            next_mi_col >= cm->mi_params.mi_cols) {
          continue;
        }
        const RD_STATS subblock_rdc = rd_search_for_fixed_partition(
            cpi, td, tile_data, tp, sms_tree->split[idx], next_mi_row,
            next_mi_col, subsize, pc_tree->split[idx]);
        sum_subblock_rate += subblock_rdc.rate;
        sum_subblock_dist += subblock_rdc.dist;
      }
      best_rdc.rate = sum_subblock_rate;
      best_rdc.rate += part_search_state.partition_cost[PARTITION_SPLIT];
      best_rdc.dist = sum_subblock_dist;
      best_rdc.rdcost = RDCOST(x->rdmult, best_rdc.rate, best_rdc.dist);
      break;
    default:
      assert(0 && "invalid partition type.");
      aom_internal_error(cm->error, AOM_CODEC_ERROR, "Invalid partition type.");
  }
  // Note: it is necessary to restore context information.
  av1_restore_context(x, &x_ctx, mi_row, mi_col, bsize, num_planes);

  if (bsize != cm->seq_params->sb_size) {
    encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, DRY_RUN_NORMAL, bsize,
              pc_tree, NULL);
  }
  x->rdmult = orig_rdmult;

  return best_rdc;
}

static void prepare_sb_features_before_search(
    AV1_COMP *const cpi, ThreadData *td, TileDataEnc *tile_data, int mi_row,
    int mi_col, const BLOCK_SIZE bsize, aom_partition_features_t *features) {
  av1_collect_motion_search_features_sb(cpi, td, tile_data, mi_row, mi_col,
                                        bsize, features);
  collect_tpl_stats_sb(cpi, bsize, mi_row, mi_col, features);
}

static void update_partition_stats(const RD_STATS *const this_rdcost,
                                   aom_partition_stats_t *stats) {
  stats->rate = this_rdcost->rate;
  stats->dist = this_rdcost->dist;
  stats->rdcost = this_rdcost->rdcost;
}

static void build_pc_tree_from_part_decision(
    const aom_partition_decision_t *partition_decision,
    const BLOCK_SIZE this_bsize, PC_TREE *pc_tree,
    struct aom_internal_error_info *error_info) {
  BLOCK_SIZE bsize = this_bsize;
  int num_nodes = partition_decision->num_nodes;
  PC_TREE *tree_node_queue[NUM_NODES] = { NULL };
  int last_idx = 1;
  int q_idx = 0;
  tree_node_queue[q_idx] = pc_tree;
  while (num_nodes > 0) {
    const int partitioning = partition_decision->partition_decision[q_idx];
    assert(partitioning >= PARTITION_NONE &&
           partitioning < EXT_PARTITION_TYPES);
    PC_TREE *node = tree_node_queue[q_idx];
    if (node != NULL) {
      node->partitioning = partitioning;
      bsize = node->block_size;
    }
    if (partitioning == PARTITION_SPLIT) {
      const BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);
      for (int i = 0; i < 4; ++i) {
        if (node != NULL) {  // Suppress warning
          node->split[i] = av1_alloc_pc_tree_node(subsize);
          if (!node->split[i])
            aom_internal_error(error_info, AOM_CODEC_MEM_ERROR,
                               "Failed to allocate PC_TREE");
          node->split[i]->index = i;
          tree_node_queue[last_idx] = node->split[i];
          ++last_idx;
        }
      }
    }
    --num_nodes;
    ++q_idx;
  }
}

// The ML model needs to provide the whole decision tree for the superblock.
static bool ml_partition_search_whole_tree(AV1_COMP *const cpi, ThreadData *td,
                                           TileDataEnc *tile_data,
                                           TokenExtra **tp,
                                           SIMPLE_MOTION_DATA_TREE *sms_root,
                                           int mi_row, int mi_col,
                                           const BLOCK_SIZE bsize) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  ExtPartController *const ext_part_controller = &cpi->ext_part_controller;
  struct aom_internal_error_info *error_info = x->e_mbd.error_info;
  aom_partition_features_t features;
  prepare_sb_features_before_search(cpi, td, tile_data, mi_row, mi_col, bsize,
                                    &features);
  features.mi_row = mi_row;
  features.mi_col = mi_col;
  features.frame_width = cpi->frame_info.frame_width;
  features.frame_height = cpi->frame_info.frame_height;
  features.block_size = bsize;
  av1_ext_part_send_features(ext_part_controller, &features);

  // rd mode search (dry run) for a valid partition decision from the ml model.
  aom_partition_decision_t partition_decision;
  do {
    const bool valid_decision = av1_ext_part_get_partition_decision(
        ext_part_controller, &partition_decision);
    if (!valid_decision) return false;

    // First, let's take the easy approach.
    // We require that the ml model has to provide partition decisions for the
    // whole superblock.
    td->pc_root = av1_alloc_pc_tree_node(bsize);
    if (!td->pc_root)
      aom_internal_error(error_info, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate PC_TREE");
    build_pc_tree_from_part_decision(&partition_decision, bsize, td->pc_root,
                                     error_info);

    const RD_STATS this_rdcost = rd_search_for_fixed_partition(
        cpi, td, tile_data, tp, sms_root, mi_row, mi_col, bsize, td->pc_root);
    aom_partition_stats_t stats;
    update_partition_stats(&this_rdcost, &stats);
    av1_ext_part_send_partition_stats(ext_part_controller, &stats);
    if (!partition_decision.is_final_decision) {
      av1_free_pc_tree_recursive(td->pc_root, av1_num_planes(cm), 0, 0,
                                 cpi->sf.part_sf.partition_search_type);
      td->pc_root = NULL;
    }
  } while (!partition_decision.is_final_decision);

  // Encode with the selected mode and partition.
  set_cb_offsets(x->cb_offset, 0, 0);
  encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, OUTPUT_ENABLED, bsize,
            td->pc_root, NULL);
  av1_free_pc_tree_recursive(td->pc_root, av1_num_planes(cm), 0, 0,
                             cpi->sf.part_sf.partition_search_type);
  td->pc_root = NULL;

  return true;
}

// Use a bitmask to represent the valid partition types for the current
// block. "1" represents the corresponding partition type is vaild.
// The least significant bit represents "PARTITION_NONE", the
// largest significant bit represents "PARTITION_VERT_4", follow
// the enum order for PARTITION_TYPE in "enums.h"
static int get_valid_partition_types(
    const AV1_COMP *const cpi,
    const PartitionSearchState *const part_search_state,
    const BLOCK_SIZE bsize) {
  const PartitionCfg *const part_cfg = &cpi->oxcf.part_cfg;
  const PartitionBlkParams blk_params = part_search_state->part_blk_params;
  int valid_types = 0;
  // PARTITION_NONE
  valid_types |= (part_search_state->partition_none_allowed << 0);
  // PARTITION_HORZ
  valid_types |= (part_search_state->partition_rect_allowed[HORZ] << 1);
  // PARTITION_VERT
  valid_types |= (part_search_state->partition_rect_allowed[VERT] << 2);
  // PARTITION_SPLIT
  valid_types |= (part_search_state->do_square_split << 3);
  // PARTITION_HORZ_A
  const int ext_partition_allowed = part_search_state->do_rectangular_split &&
                                    av1_blk_has_rows_and_cols(&blk_params);
  const int horzab_partition_allowed =
      ext_partition_allowed && part_cfg->enable_ab_partitions &&
      part_search_state->partition_rect_allowed[HORZ];
  valid_types |= (horzab_partition_allowed << 4);
  // PARTITION_HORZ_B
  valid_types |= (horzab_partition_allowed << 5);
  // PARTITION_VERT_A
  const int vertab_partition_allowed =
      ext_partition_allowed && part_cfg->enable_ab_partitions &&
      part_search_state->partition_rect_allowed[VERT];
  valid_types |= (vertab_partition_allowed << 6);
  // PARTITION_VERT_B
  valid_types |= (vertab_partition_allowed << 7);
  // PARTITION_HORZ_4
  const int partition4_allowed = part_cfg->enable_1to4_partitions &&
                                 ext_partition_allowed &&
                                 bsize != BLOCK_128X128;
  const int horz4_allowed =
      partition4_allowed && part_search_state->partition_rect_allowed[HORZ] &&
      get_plane_block_size(get_partition_subsize(bsize, PARTITION_HORZ_4),
                           part_search_state->ss_x,
                           part_search_state->ss_y) != BLOCK_INVALID;
  valid_types |= (horz4_allowed << 8);
  // PARTITION_VERT_4
  const int vert4_allowed =
      partition4_allowed && part_search_state->partition_rect_allowed[HORZ] &&
      get_plane_block_size(get_partition_subsize(bsize, PARTITION_VERT_4),
                           part_search_state->ss_x,
                           part_search_state->ss_y) != BLOCK_INVALID;
  valid_types |= (vert4_allowed << 9);

  return valid_types;
}

static void prepare_tpl_stats_block(const AV1_COMP *const cpi,
                                    const BLOCK_SIZE bsize, const int mi_row,
                                    const int mi_col, int64_t *intra_cost,
                                    int64_t *inter_cost, int64_t *mc_dep_cost) {
  const AV1_COMMON *const cm = &cpi->common;
  GF_GROUP *gf_group = &cpi->ppi->gf_group;
  if (gf_group->update_type[cpi->gf_frame_index] == INTNL_OVERLAY_UPDATE ||
      gf_group->update_type[cpi->gf_frame_index] == OVERLAY_UPDATE) {
    return;
  }

  TplParams *const tpl_data = &cpi->ppi->tpl_data;
  TplDepFrame *tpl_frame = &tpl_data->tpl_frame[cpi->gf_frame_index];
  TplDepStats *tpl_stats = tpl_frame->tpl_stats_ptr;
  // If tpl stats is not established, early return
  if (!tpl_data->ready || gf_group->max_layer_depth_allowed == 0) {
    return;
  }

  const int tpl_stride = tpl_frame->stride;
  const int step = 1 << tpl_data->tpl_stats_block_mis_log2;
  const int mi_width =
      AOMMIN(mi_size_wide[bsize], cm->mi_params.mi_cols - mi_col);
  const int mi_height =
      AOMMIN(mi_size_high[bsize], cm->mi_params.mi_rows - mi_row);

  int64_t sum_intra_cost = 0;
  int64_t sum_inter_cost = 0;
  int64_t sum_mc_dep_cost = 0;
  for (int row = 0; row < mi_height; row += step) {
    for (int col = 0; col < mi_width; col += step) {
      TplDepStats *this_stats =
          &tpl_stats[av1_tpl_ptr_pos(mi_row + row, mi_col + col, tpl_stride,
                                     tpl_data->tpl_stats_block_mis_log2)];
      sum_intra_cost += this_stats->intra_cost;
      sum_inter_cost += this_stats->inter_cost;
      const int64_t mc_dep_delta =
          RDCOST(tpl_frame->base_rdmult, this_stats->mc_dep_rate,
                 this_stats->mc_dep_dist);
      sum_mc_dep_cost += mc_dep_delta;
    }
  }

  *intra_cost = sum_intra_cost;
  *inter_cost = sum_inter_cost;
  *mc_dep_cost = sum_mc_dep_cost;
}

static bool recursive_partition(AV1_COMP *const cpi, ThreadData *td,
                                TileDataEnc *tile_data, TokenExtra **tp,
                                SIMPLE_MOTION_DATA_TREE *sms_root,
                                PC_TREE *pc_tree, int mi_row, int mi_col,
                                const BLOCK_SIZE bsize, RD_STATS *this_rdcost) {
  const AV1_COMMON *const cm = &cpi->common;
  ExtPartController *const ext_part_controller = &cpi->ext_part_controller;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  if (mi_row >= cm->mi_params.mi_rows || mi_col >= cm->mi_params.mi_cols) {
    return false;
  }
  aom_partition_decision_t partition_decision;
  do {
    PartitionSearchState part_search_state;
    // Initialization of state variables used in partition search.
    // TODO(chengchen): check if there is hidden conditions that don't allow
    // all possible partition types.
    init_partition_search_state_params(x, cpi, &part_search_state, mi_row,
                                       mi_col, bsize);
    // Override partition costs at the edges of the frame in the same
    // way as in read_partition (see decodeframe.c).
    PartitionBlkParams blk_params = part_search_state.part_blk_params;
    if (!av1_blk_has_rows_and_cols(&blk_params))
      set_partition_cost_for_edge_blk(cm, &part_search_state);
    const int orig_rdmult = x->rdmult;
    setup_block_rdmult(cpi, x, mi_row, mi_col, bsize, NO_AQ, NULL);
    const int valid_partition_types =
        get_valid_partition_types(cpi, &part_search_state, bsize);
    const FRAME_UPDATE_TYPE update_type =
        get_frame_update_type(&cpi->ppi->gf_group, cpi->gf_frame_index);
    const int qindex = av1_get_qindex(&cm->seg, xd->mi[0]->segment_id,
                                      cm->quant_params.base_qindex);
    // RD multiplier
    const int rdmult = x->rdmult;
    // pyramid level
    const int pyramid_level =
        cpi->ppi->gf_group.layer_depth[cpi->gf_frame_index];
    x->rdmult = orig_rdmult;
    // Neighbor information
    const int has_above = !!xd->above_mbmi;
    const int has_left = !!xd->left_mbmi;
    const BLOCK_SIZE above_bsize =
        has_above ? xd->above_mbmi->bsize : BLOCK_INVALID;
    const BLOCK_SIZE left_bsize =
        has_left ? xd->left_mbmi->bsize : BLOCK_INVALID;
    const int above_block_width =
        above_bsize == BLOCK_INVALID ? -1 : block_size_wide[above_bsize];
    const int above_block_height =
        above_bsize == BLOCK_INVALID ? -1 : block_size_high[above_bsize];
    const int left_block_width =
        left_bsize == BLOCK_INVALID ? -1 : block_size_wide[left_bsize];
    const int left_block_height =
        left_bsize == BLOCK_INVALID ? -1 : block_size_high[left_bsize];
    // Prepare simple motion search stats as features
    unsigned int block_sse = -1;
    unsigned int block_var = -1;
    unsigned int sub_block_sse[4] = { -1, -1, -1, -1 };
    unsigned int sub_block_var[4] = { -1, -1, -1, -1 };
    unsigned int horz_block_sse[2] = { -1, -1 };
    unsigned int horz_block_var[2] = { -1, -1 };
    unsigned int vert_block_sse[2] = { -1, -1 };
    unsigned int vert_block_var[2] = { -1, -1 };
    av1_prepare_motion_search_features_block(
        cpi, td, tile_data, mi_row, mi_col, bsize, valid_partition_types,
        &block_sse, &block_var, sub_block_sse, sub_block_var, horz_block_sse,
        horz_block_var, vert_block_sse, vert_block_var);
    // Prepare tpl stats for the current block as features
    int64_t tpl_intra_cost = -1;
    int64_t tpl_inter_cost = -1;
    int64_t tpl_mc_dep_cost = -1;
    prepare_tpl_stats_block(cpi, bsize, mi_row, mi_col, &tpl_intra_cost,
                            &tpl_inter_cost, &tpl_mc_dep_cost);

    aom_partition_features_t features;
    features.mi_row = mi_row;
    features.mi_col = mi_col;
    features.frame_width = cpi->frame_info.frame_width;
    features.frame_height = cpi->frame_info.frame_height;
    features.block_size = bsize;
    features.valid_partition_types = valid_partition_types;
    features.update_type = update_type;
    features.qindex = qindex;
    features.rdmult = rdmult;
    features.pyramid_level = pyramid_level;
    features.has_above_block = has_above;
    features.above_block_width = above_block_width;
    features.above_block_height = above_block_height;
    features.has_left_block = has_left;
    features.left_block_width = left_block_width;
    features.left_block_height = left_block_height;
    features.block_sse = block_sse;
    features.block_var = block_var;
    for (int i = 0; i < 4; ++i) {
      features.sub_block_sse[i] = sub_block_sse[i];
      features.sub_block_var[i] = sub_block_var[i];
    }
    for (int i = 0; i < 2; ++i) {
      features.horz_block_sse[i] = horz_block_sse[i];
      features.horz_block_var[i] = horz_block_var[i];
      features.vert_block_sse[i] = vert_block_sse[i];
      features.vert_block_var[i] = vert_block_var[i];
    }
    features.tpl_intra_cost = tpl_intra_cost;
    features.tpl_inter_cost = tpl_inter_cost;
    features.tpl_mc_dep_cost = tpl_mc_dep_cost;
    av1_ext_part_send_features(ext_part_controller, &features);
    const bool valid_decision = av1_ext_part_get_partition_decision(
        ext_part_controller, &partition_decision);
    if (!valid_decision) return false;
    pc_tree->partitioning = partition_decision.current_decision;

    av1_init_rd_stats(this_rdcost);
    if (partition_decision.current_decision == PARTITION_SPLIT) {
      assert(block_size_wide[bsize] >= 8 && block_size_high[bsize] >= 8);
      const BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);
      RD_STATS split_rdc[SUB_PARTITIONS_SPLIT];
      for (int i = 0; i < SUB_PARTITIONS_SPLIT; ++i) {
        av1_init_rd_stats(&split_rdc[i]);
        if (pc_tree->split[i] == NULL)
          pc_tree->split[i] = av1_alloc_pc_tree_node(subsize);
        if (!pc_tree->split[i])
          aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                             "Failed to allocate PC_TREE");
        pc_tree->split[i]->index = i;
      }
      const int orig_rdmult_tmp = x->rdmult;
      setup_block_rdmult(cpi, x, mi_row, mi_col, bsize, NO_AQ, NULL);
      // TODO(chengchen): check boundary conditions
      // top-left
      recursive_partition(cpi, td, tile_data, tp, sms_root, pc_tree->split[0],
                          mi_row, mi_col, subsize, &split_rdc[0]);
      // top-right
      recursive_partition(cpi, td, tile_data, tp, sms_root, pc_tree->split[1],
                          mi_row, mi_col + mi_size_wide[subsize], subsize,
                          &split_rdc[1]);
      // bottom-left
      recursive_partition(cpi, td, tile_data, tp, sms_root, pc_tree->split[2],
                          mi_row + mi_size_high[subsize], mi_col, subsize,
                          &split_rdc[2]);
      // bottom_right
      recursive_partition(cpi, td, tile_data, tp, sms_root, pc_tree->split[3],
                          mi_row + mi_size_high[subsize],
                          mi_col + mi_size_wide[subsize], subsize,
                          &split_rdc[3]);
      this_rdcost->rate += part_search_state.partition_cost[PARTITION_SPLIT];
      // problem is here, the rdmult is different from the rdmult in sub block.
      for (int i = 0; i < SUB_PARTITIONS_SPLIT; ++i) {
        this_rdcost->rate += split_rdc[i].rate;
        this_rdcost->dist += split_rdc[i].dist;
        av1_rd_cost_update(x->rdmult, this_rdcost);
      }
      x->rdmult = orig_rdmult_tmp;
    } else {
      *this_rdcost = rd_search_for_fixed_partition(
          cpi, td, tile_data, tp, sms_root, mi_row, mi_col, bsize, pc_tree);
    }

    aom_partition_stats_t stats;
    update_partition_stats(this_rdcost, &stats);
    av1_ext_part_send_partition_stats(ext_part_controller, &stats);
    if (!partition_decision.is_final_decision) {
      if (partition_decision.current_decision == PARTITION_SPLIT) {
        for (int i = 0; i < 4; ++i) {
          if (pc_tree->split[i] != NULL) {
            av1_free_pc_tree_recursive(pc_tree->split[i], av1_num_planes(cm), 0,
                                       0,
                                       cpi->sf.part_sf.partition_search_type);
            pc_tree->split[i] = NULL;
          }
        }
      }
    }
  } while (!partition_decision.is_final_decision);

  return true;
}

// The ML model only needs to make decisions for the current block each time.
static bool ml_partition_search_partial(AV1_COMP *const cpi, ThreadData *td,
                                        TileDataEnc *tile_data, TokenExtra **tp,
                                        SIMPLE_MOTION_DATA_TREE *sms_root,
                                        int mi_row, int mi_col,
                                        const BLOCK_SIZE bsize) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  ExtPartController *const ext_part_controller = &cpi->ext_part_controller;
  aom_partition_features_t features;
  prepare_sb_features_before_search(cpi, td, tile_data, mi_row, mi_col, bsize,
                                    &features);
  features.mi_row = mi_row;
  features.mi_col = mi_col;
  features.frame_width = cpi->frame_info.frame_width;
  features.frame_height = cpi->frame_info.frame_height;
  features.block_size = bsize;
  av1_ext_part_send_features(ext_part_controller, &features);
  td->pc_root = av1_alloc_pc_tree_node(bsize);
  if (!td->pc_root)
    aom_internal_error(x->e_mbd.error_info, AOM_CODEC_MEM_ERROR,
                       "Failed to allocate PC_TREE");

  RD_STATS rdcost;
  const bool valid_partition =
      recursive_partition(cpi, td, tile_data, tp, sms_root, td->pc_root, mi_row,
                          mi_col, bsize, &rdcost);
  if (!valid_partition) {
    return false;
  }

  // Encode with the selected mode and partition.
  set_cb_offsets(x->cb_offset, 0, 0);
  encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, OUTPUT_ENABLED, bsize,
            td->pc_root, NULL);
  av1_free_pc_tree_recursive(td->pc_root, av1_num_planes(cm), 0, 0,
                             cpi->sf.part_sf.partition_search_type);
  td->pc_root = NULL;

  return true;
}

bool av1_rd_partition_search(AV1_COMP *const cpi, ThreadData *td,
                             TileDataEnc *tile_data, TokenExtra **tp,
                             SIMPLE_MOTION_DATA_TREE *sms_root, int mi_row,
                             int mi_col, const BLOCK_SIZE bsize,
                             RD_STATS *best_rd_cost) {
  AV1_COMMON *const cm = &cpi->common;
  if (cpi->ext_part_controller.ready) {
    bool valid_search = true;
    const aom_ext_part_decision_mode_t decision_mode =
        av1_get_ext_part_decision_mode(&cpi->ext_part_controller);
    if (decision_mode == AOM_EXT_PART_WHOLE_TREE) {
      valid_search = ml_partition_search_whole_tree(
          cpi, td, tile_data, tp, sms_root, mi_row, mi_col, bsize);
    } else if (decision_mode == AOM_EXT_PART_RECURSIVE) {
      valid_search = ml_partition_search_partial(
          cpi, td, tile_data, tp, sms_root, mi_row, mi_col, bsize);
    } else {
      assert(0 && "Unknown decision mode.");
      return false;
    }
    if (!valid_search) {
      aom_internal_error(
          cm->error, AOM_CODEC_ERROR,
          "Invalid search from ML model, partition search failed");
    }
    return true;
  }

  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  int best_idx = 0;
  int64_t min_rdcost = INT64_MAX;
  int num_configs;
  int i = 0;
  do {
    td->pc_root = av1_alloc_pc_tree_node(bsize);
    if (!td->pc_root)
      aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate PC_TREE");
    num_configs = read_partition_tree(cpi, td->pc_root, xd->error_info, i);
    if (num_configs <= 0) {
      av1_free_pc_tree_recursive(td->pc_root, av1_num_planes(cm), 0, 0,
                                 cpi->sf.part_sf.partition_search_type);
      td->pc_root = NULL;
      aom_internal_error(xd->error_info, AOM_CODEC_ERROR, "Invalid configs.");
    }
    verify_write_partition_tree(cpi, td->pc_root, bsize, i, mi_row, mi_col);
    if (i == 0) {
      AOM_CHECK_MEM_ERROR(xd->error_info, x->rdcost,
                          aom_calloc(num_configs, sizeof(*x->rdcost)));
    }
    // Encode the block with the given partition tree. Get rdcost and encoding
    // time.
    x->rdcost[i] = rd_search_for_fixed_partition(
        cpi, td, tile_data, tp, sms_root, mi_row, mi_col, bsize, td->pc_root);

    if (x->rdcost[i].rdcost < min_rdcost) {
      min_rdcost = x->rdcost[i].rdcost;
      best_idx = i;
      *best_rd_cost = x->rdcost[i];
    }
    av1_free_pc_tree_recursive(td->pc_root, av1_num_planes(cm), 0, 0,
                               cpi->sf.part_sf.partition_search_type);
    td->pc_root = NULL;
    ++i;
  } while (i < num_configs);

  aom_free(x->rdcost);
  x->rdcost = NULL;
  // Encode with the partition configuration with the smallest rdcost.
  td->pc_root = av1_alloc_pc_tree_node(bsize);
  if (!td->pc_root)
    aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                       "Failed to allocate PC_TREE");
  read_partition_tree(cpi, td->pc_root, xd->error_info, best_idx);
  rd_search_for_fixed_partition(cpi, td, tile_data, tp, sms_root, mi_row,
                                mi_col, bsize, td->pc_root);
  set_cb_offsets(x->cb_offset, 0, 0);
  encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, OUTPUT_ENABLED, bsize,
            td->pc_root, NULL);
  av1_free_pc_tree_recursive(td->pc_root, av1_num_planes(cm), 0, 0,
                             cpi->sf.part_sf.partition_search_type);
  td->pc_root = NULL;
  ++cpi->sb_counter;

  return true;
}
#endif  // CONFIG_PARTITION_SEARCH_ORDER

static inline bool should_do_dry_run_encode_for_current_block(
    BLOCK_SIZE sb_size, BLOCK_SIZE max_partition_size, int curr_block_index,
    BLOCK_SIZE bsize) {
  if (bsize > max_partition_size) return false;

  // Enable the reconstruction with dry-run for the 4th sub-block only if its
  // parent block's reconstruction with dry-run is skipped. If
  // max_partition_size is the same as immediate split of superblock, then avoid
  // reconstruction of the 4th sub-block, as this data is not consumed.
  if (curr_block_index != 3) return true;

  const BLOCK_SIZE sub_sb_size =
      get_partition_subsize(sb_size, PARTITION_SPLIT);
  return bsize == max_partition_size && sub_sb_size != max_partition_size;
}

static void log_sub_block_var(const AV1_COMP *cpi, MACROBLOCK *x, BLOCK_SIZE bs,
                              double *var_min, double *var_max) {
  // This functions returns a the minimum and maximum log variances for 4x4
  // sub blocks in the current block.

  const MACROBLOCKD *const xd = &x->e_mbd;
  const int is_hbd = is_cur_buf_hbd(xd);
  const int right_overflow =
      (xd->mb_to_right_edge < 0) ? ((-xd->mb_to_right_edge) >> 3) : 0;
  const int bottom_overflow =
      (xd->mb_to_bottom_edge < 0) ? ((-xd->mb_to_bottom_edge) >> 3) : 0;
  const int bw = MI_SIZE * mi_size_wide[bs] - right_overflow;
  const int bh = MI_SIZE * mi_size_high[bs] - bottom_overflow;

  // Initialize minimum variance to a large value and maximum variance to 0.
  double min_var_4x4 = (double)INT_MAX;
  double max_var_4x4 = 0.0;

  aom_variance_fn_t vf = cpi->ppi->fn_ptr[BLOCK_4X4].vf;
  for (int i = 0; i < bh; i += MI_SIZE) {
    for (int j = 0; j < bw; j += MI_SIZE) {
      int var;
      // Calculate the 4x4 sub-block variance.
      var = av1_calc_normalized_variance(
          vf, x->plane[0].src.buf + (i * x->plane[0].src.stride) + j,
          x->plane[0].src.stride, is_hbd);

      // Record min and max for over-arching block
      min_var_4x4 = AOMMIN(min_var_4x4, var);
      max_var_4x4 = AOMMAX(max_var_4x4, var);
    }
  }
  *var_min = log1p(min_var_4x4 / 16.0);
  *var_max = log1p(max_var_4x4 / 16.0);
}

static inline void set_sms_tree_partitioning(SIMPLE_MOTION_DATA_TREE *sms_tree,
                                             PARTITION_TYPE partition) {
  if (sms_tree == NULL) return;
  sms_tree->partitioning = partition;
}

/*!\brief AV1 block partition search (full search).
*
* \ingroup partition_search
* \callgraph
* Searches for the best partition pattern for a block based on the
* rate-distortion cost, and returns a bool value to indicate whether a valid
* partition pattern is found. The partition can recursively go down to the
* smallest block size.
*
* \param[in]    cpi                Top-level encoder structure
* \param[in]    td                 Pointer to thread data
* \param[in]    tile_data          Pointer to struct holding adaptive
data/contexts/models for the tile during
encoding
* \param[in]    tp                 Pointer to the starting token
* \param[in]    mi_row             Row coordinate of the block in a step size
of MI_SIZE
* \param[in]    mi_col             Column coordinate of the block in a step
size of MI_SIZE
* \param[in]    bsize              Current block size
* \param[in]    rd_cost            Pointer to the final rd cost of the block
* \param[in]    best_rdc           Upper bound of rd cost of a valid partition
* \param[in]    pc_tree            Pointer to the PC_TREE node storing the
picked partitions and mode info for the
current block
* \param[in]    sms_tree           Pointer to struct holding simple motion
search data for the current block
* \param[in]    none_rd            Pointer to the rd cost in the case of not
splitting the current block
* \param[in]    multi_pass_mode    SB_SINGLE_PASS/SB_DRY_PASS/SB_WET_PASS
* \param[in]    rect_part_win_info Pointer to struct storing whether horz/vert
partition outperforms previously tested
partitions
*
* \return A bool value is returned indicating if a valid partition is found.
* The pc_tree struct is modified to store the picked partition and modes.
* The rd_cost struct is also updated with the RD stats corresponding to the
* best partition found.
*/
bool av1_rd_pick_partition(AV1_COMP *const cpi, ThreadData *td,
                           TileDataEnc *tile_data, TokenExtra **tp, int mi_row,
                           int mi_col, BLOCK_SIZE bsize, RD_STATS *rd_cost,
                           RD_STATS best_rdc, PC_TREE *pc_tree,
                           SIMPLE_MOTION_DATA_TREE *sms_tree, int64_t *none_rd,
                           SB_MULTI_PASS_MODE multi_pass_mode,
                           RD_RECT_PART_WIN_INFO *rect_part_win_info) {
  const AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  TileInfo *const tile_info = &tile_data->tile_info;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  RD_SEARCH_MACROBLOCK_CONTEXT x_ctx;
  const TokenExtra *const tp_orig = *tp;
  PartitionSearchState part_search_state;

  // Initialization of state variables used in partition search.
  init_partition_search_state_params(x, cpi, &part_search_state, mi_row, mi_col,
                                     bsize);
  PartitionBlkParams blk_params = part_search_state.part_blk_params;

  set_sms_tree_partitioning(sms_tree, PARTITION_NONE);
  if (best_rdc.rdcost < 0) {
    av1_invalid_rd_stats(rd_cost);
    return part_search_state.found_best_partition;
  }
  if (bsize == cm->seq_params->sb_size) x->must_find_valid_partition = 0;

  // Override skipping rectangular partition operations for edge blocks.
  if (none_rd) *none_rd = 0;
  (void)*tp_orig;

#if CONFIG_COLLECT_PARTITION_STATS
  // Stats at the current quad tree
  PartitionTimingStats *part_timing_stats =
      &part_search_state.part_timing_stats;
  // Stats aggregated at frame level
  FramePartitionTimingStats *fr_part_timing_stats = &cpi->partition_stats;
#endif  // CONFIG_COLLECT_PARTITION_STATS

  // Override partition costs at the edges of the frame in the same
  // way as in read_partition (see decodeframe.c).
  if (!av1_blk_has_rows_and_cols(&blk_params))
    set_partition_cost_for_edge_blk(cm, &part_search_state);

  // Disable rectangular partitions for inner blocks when the current block is
  // forced to only use square partitions.
  if (bsize > cpi->sf.part_sf.use_square_partition_only_threshold) {
    part_search_state.partition_rect_allowed[HORZ] &= !blk_params.has_rows;
    part_search_state.partition_rect_allowed[VERT] &= !blk_params.has_cols;
  }

#ifndef NDEBUG
  // Nothing should rely on the default value of this array (which is just
  // leftover from encoding the previous block. Setting it to fixed pattern
  // when debugging.
  // bit 0, 1, 2 are blk_skip of each plane
  // bit 4, 5, 6 are initialization checking of each plane
  memset(x->txfm_search_info.blk_skip, 0x77,
         sizeof(x->txfm_search_info.blk_skip));
#endif  // NDEBUG

  assert(mi_size_wide[bsize] == mi_size_high[bsize]);

  // Set buffers and offsets.
  av1_set_offsets(cpi, tile_info, x, mi_row, mi_col, bsize);

  if (cpi->oxcf.mode == ALLINTRA) {
    if (bsize == cm->seq_params->sb_size) {
      double var_min, var_max;
      log_sub_block_var(cpi, x, bsize, &var_min, &var_max);

      x->intra_sb_rdmult_modifier = 128;
      if ((var_min < 2.0) && (var_max > 4.0)) {
        if ((var_max - var_min) > 8.0) {
          x->intra_sb_rdmult_modifier -= 48;
        } else {
          x->intra_sb_rdmult_modifier -= (int)((var_max - var_min) * 6);
        }
      }
    }
  }

  // Save rdmult before it might be changed, so it can be restored later.
  const int orig_rdmult = x->rdmult;
  setup_block_rdmult(cpi, x, mi_row, mi_col, bsize, NO_AQ, NULL);

  // Apply simple motion search for the entire super block with fixed block
  // size, e.g., 16x16, to collect features and write to files for the
  // external ML model.
  // TODO(chengchen): reduce motion search. This function is similar to
  // av1_get_max_min_partition_features().
  if (COLLECT_MOTION_SEARCH_FEATURE_SB && !frame_is_intra_only(cm) &&
      bsize == cm->seq_params->sb_size) {
    av1_collect_motion_search_features_sb(cpi, td, tile_data, mi_row, mi_col,
                                          bsize, /*features=*/NULL);
    collect_tpl_stats_sb(cpi, bsize, mi_row, mi_col, /*features=*/NULL);
  }

  // Update rd cost of the bound using the current multiplier.
  av1_rd_cost_update(x->rdmult, &best_rdc);

  if (bsize == BLOCK_16X16 && cpi->vaq_refresh)
    x->mb_energy = av1_log_block_var(cpi, x, bsize);

  // Set the context.
  xd->above_txfm_context =
      cm->above_contexts.txfm[tile_info->tile_row] + mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (mi_row & MAX_MIB_MASK);
  av1_save_context(x, &x_ctx, mi_row, mi_col, bsize, num_planes);

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, av1_prune_partitions_time);
#endif
  // Pruning: before searching any partition type, using source and simple
  // motion search results to prune out unlikely partitions.
  av1_prune_partitions_before_search(cpi, x, sms_tree, &part_search_state);

  // Pruning: eliminating partition types leading to coding block sizes outside
  // the min and max bsize limitations set from the encoder.
  av1_prune_partitions_by_max_min_bsize(&x->sb_enc, &part_search_state);
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, av1_prune_partitions_time);
#endif

  // Partition search
BEGIN_PARTITION_SEARCH:
  // If a valid partition is required, usually when the first round cannot find
  // a valid one under the cost limit after pruning, reset the limitations on
  // partition types and intra cnn output.
  if (x->must_find_valid_partition) {
    reset_part_limitations(cpi, &part_search_state);
    av1_prune_partitions_by_max_min_bsize(&x->sb_enc, &part_search_state);
    // Invalidate intra cnn output for key frames.
    if (frame_is_intra_only(cm) && bsize == BLOCK_64X64) {
      part_search_state.intra_part_info->quad_tree_idx = 0;
      part_search_state.intra_part_info->cnn_output_valid = 0;
    }
  }
  // Partition block source pixel variance.
  unsigned int pb_source_variance = UINT_MAX;

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, none_partition_search_time);
#endif

  if (cpi->oxcf.mode == ALLINTRA) {
    const bool bsize_at_least_16x16 = (bsize >= BLOCK_16X16);
    const bool prune_rect_part_using_4x4_var_deviation =
        (cpi->sf.part_sf.prune_rect_part_using_4x4_var_deviation &&
         !x->must_find_valid_partition);

    if (bsize_at_least_16x16 || prune_rect_part_using_4x4_var_deviation) {
      double var_min, var_max;
      log_sub_block_var(cpi, x, bsize, &var_min, &var_max);

      // Further pruning or in some cases reverse pruning when allintra is set.
      // This code helps visual and in some cases metrics quality where the
      // current block comprises at least one very low variance sub-block and at
      // least one where the variance is much higher.
      //
      // The idea is that in such cases there is danger of ringing and other
      // visual artifacts from a high variance feature such as an edge into a
      // very low variance region.
      //
      // The approach taken is to force break down / split to a smaller block
      // size to try and separate out the low variance and well predicted blocks
      // from the more complex ones and to prevent propagation of ringing over a
      // large region.
      if (bsize_at_least_16x16 && (var_min < 0.272) &&
          ((var_max - var_min) > 3.0)) {
        part_search_state.partition_none_allowed = 0;
        part_search_state.terminate_partition_search = 0;
        part_search_state.do_square_split = 1;
      } else if (prune_rect_part_using_4x4_var_deviation &&
                 (var_max - var_min < 3.0)) {
        // Prune rectangular partitions if the variance deviation of 4x4
        // sub-blocks within the block is less than a threshold (derived
        // empirically).
        part_search_state.do_rectangular_split = 0;
      }
    }
  }

  // PARTITION_NONE search stage.
  int64_t part_none_rd = INT64_MAX;
  none_partition_search(cpi, td, tile_data, x, pc_tree, sms_tree, &x_ctx,
                        &part_search_state, &best_rdc, &pb_source_variance,
                        none_rd, &part_none_rd);

#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, none_partition_search_time);
#endif
#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, split_partition_search_time);
#endif
  // PARTITION_SPLIT search stage.
  int64_t part_split_rd = INT64_MAX;
  split_partition_search(cpi, td, tile_data, tp, x, pc_tree, sms_tree, &x_ctx,
                         &part_search_state, &best_rdc, multi_pass_mode,
                         &part_split_rd);
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, split_partition_search_time);
#endif
  // Terminate partition search for child partition,
  // when NONE and SPLIT partition rd_costs are INT64_MAX.
  if (cpi->sf.part_sf.early_term_after_none_split &&
      part_none_rd == INT64_MAX && part_split_rd == INT64_MAX &&
      !x->must_find_valid_partition && (bsize != cm->seq_params->sb_size)) {
    part_search_state.terminate_partition_search = 1;
  }

  // Do not evaluate non-square partitions if NONE partition did not choose a
  // newmv mode and is skippable.
  if ((cpi->sf.part_sf.skip_non_sq_part_based_on_none >= 2) &&
      (pc_tree->none != NULL)) {
    if (x->qindex <= 200 && is_inter_mode(pc_tree->none->mic.mode) &&
        !have_newmv_in_inter_mode(pc_tree->none->mic.mode) &&
        pc_tree->none->skippable && !x->must_find_valid_partition &&
        bsize >= BLOCK_16X16)
      part_search_state.do_rectangular_split = 0;
  }

  // Prune partitions based on PARTITION_NONE and PARTITION_SPLIT.
  prune_partitions_after_split(cpi, x, sms_tree, &part_search_state, &best_rdc,
                               part_none_rd, part_split_rd);
#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, rectangular_partition_search_time);
#endif
  // Rectangular partitions search stage.
  rectangular_partition_search(cpi, td, tile_data, tp, x, pc_tree, &x_ctx,
                               &part_search_state, &best_rdc,
                               rect_part_win_info, HORZ, VERT);
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, rectangular_partition_search_time);
#endif

  if (pb_source_variance == UINT_MAX) {
    av1_setup_src_planes(x, cpi->source, mi_row, mi_col, num_planes, bsize);
    pb_source_variance = av1_get_perpixel_variance_facade(
        cpi, xd, &x->plane[0].src, bsize, AOM_PLANE_Y);
  }

  assert(IMPLIES(!cpi->oxcf.part_cfg.enable_rect_partitions,
                 !part_search_state.do_rectangular_split));

  const int prune_ext_part_state = prune_ext_part_none_skippable(
      pc_tree->none, x->must_find_valid_partition,
      cpi->sf.part_sf.skip_non_sq_part_based_on_none, bsize);

  const int ab_partition_allowed = allow_ab_partition_search(
      &part_search_state, &cpi->sf.part_sf, pc_tree->partitioning,
      x->must_find_valid_partition, prune_ext_part_state, best_rdc.rdcost);

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, ab_partitions_search_time);
#endif
  // AB partitions search stage.
  ab_partitions_search(cpi, td, tile_data, tp, x, &x_ctx, pc_tree,
                       &part_search_state, &best_rdc, rect_part_win_info,
                       pb_source_variance, ab_partition_allowed, HORZ_A,
                       VERT_B);
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, ab_partitions_search_time);
#endif

  // 4-way partitions search stage.
  int part4_search_allowed[NUM_PART4_TYPES] = { 1, 1 };
  // Prune 4-way partition search.
  prune_4_way_partition_search(cpi, x, pc_tree, &part_search_state, &best_rdc,
                               pb_source_variance, prune_ext_part_state,
                               part4_search_allowed);

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, rd_pick_4partition_time);
#endif
  // PARTITION_HORZ_4
  assert(IMPLIES(!cpi->oxcf.part_cfg.enable_rect_partitions,
                 !part4_search_allowed[HORZ4]));
  if (!part_search_state.terminate_partition_search &&
      part4_search_allowed[HORZ4]) {
    const int inc_step[NUM_PART4_TYPES] = { mi_size_high[blk_params.bsize] / 4,
                                            0 };
    // Evaluation of Horz4 partition type.
    rd_pick_4partition(cpi, td, tile_data, tp, x, &x_ctx, pc_tree,
                       pc_tree->horizontal4, &part_search_state, &best_rdc,
                       inc_step, PARTITION_HORZ_4);
  }

  // PARTITION_VERT_4
  assert(IMPLIES(!cpi->oxcf.part_cfg.enable_rect_partitions,
                 !part4_search_allowed[VERT4]));
  if (!part_search_state.terminate_partition_search &&
      part4_search_allowed[VERT4] && blk_params.has_cols) {
    const int inc_step[NUM_PART4_TYPES] = { 0, mi_size_wide[blk_params.bsize] /
                                                   4 };
    // Evaluation of Vert4 partition type.
    rd_pick_4partition(cpi, td, tile_data, tp, x, &x_ctx, pc_tree,
                       pc_tree->vertical4, &part_search_state, &best_rdc,
                       inc_step, PARTITION_VERT_4);
  }
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, rd_pick_4partition_time);
#endif

  if (bsize == cm->seq_params->sb_size &&
      !part_search_state.found_best_partition) {
    // Did not find a valid partition, go back and search again, with less
    // constraint on which partition types to search.
    x->must_find_valid_partition = 1;
#if CONFIG_COLLECT_PARTITION_STATS
    fr_part_timing_stats->partition_redo += 1;
#endif  // CONFIG_COLLECT_PARTITION_STATS
    goto BEGIN_PARTITION_SEARCH;
  }

  // Store the final rd cost
  *rd_cost = best_rdc;

  // Also record the best partition in simple motion data tree because it is
  // necessary for the related speed features.
  set_sms_tree_partitioning(sms_tree, pc_tree->partitioning);

#if CONFIG_COLLECT_PARTITION_STATS
  if (best_rdc.rate < INT_MAX && best_rdc.dist < INT64_MAX) {
    part_timing_stats->partition_decisions[pc_tree->partitioning] += 1;
  }

  // If CONFIG_COLLECT_PARTITION_STATS is 1, then print out the stats for each
  // prediction block.
  print_partition_timing_stats_with_rdcost(
      part_timing_stats, mi_row, mi_col, bsize,
      cpi->ppi->gf_group.update_type[cpi->gf_frame_index],
      cm->current_frame.frame_number, &best_rdc, "part_timing.csv");
  const bool print_timing_stats = false;
  if (print_timing_stats) {
    print_partition_timing_stats(part_timing_stats, cm->show_frame,
                                 frame_is_intra_only(cm), bsize,
                                 "part_timing_data.csv");
  }
  // If CONFIG_COLLECTION_PARTITION_STATS is 2, then we print out the stats for
  // the whole clip. So we need to pass the information upstream to the encoder.
  accumulate_partition_timing_stats(fr_part_timing_stats, part_timing_stats,
                                    bsize);
#endif  // CONFIG_COLLECT_PARTITION_STATS

  // Reset the PC_TREE deallocation flag.
  int pc_tree_dealloc = 0;

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, encode_sb_time);
#endif
  if (part_search_state.found_best_partition) {
    if (bsize == cm->seq_params->sb_size) {
      // Encode the superblock.
      const int emit_output = multi_pass_mode != SB_DRY_PASS;
      const RUN_TYPE run_type = emit_output ? OUTPUT_ENABLED : DRY_RUN_NORMAL;

      // Write partition tree to file. Not used by default.
      if (COLLECT_MOTION_SEARCH_FEATURE_SB) {
        write_partition_tree(cpi, pc_tree, bsize, mi_row, mi_col);
        ++cpi->sb_counter;
      }

      set_cb_offsets(x->cb_offset, 0, 0);
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, run_type, bsize,
                pc_tree, NULL);
      assert(pc_tree == td->pc_root);
      // Dealloc the whole PC_TREE after a superblock is done.
      av1_free_pc_tree_recursive(pc_tree, num_planes, 0, 0,
                                 cpi->sf.part_sf.partition_search_type);
      pc_tree = NULL;
      td->pc_root = NULL;
      pc_tree_dealloc = 1;
    } else if (should_do_dry_run_encode_for_current_block(
                   cm->seq_params->sb_size, x->sb_enc.max_partition_size,
                   pc_tree->index, bsize)) {
      // Encode the smaller blocks in DRY_RUN mode.
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, DRY_RUN_NORMAL, bsize,
                pc_tree, NULL);
    }
  }
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, encode_sb_time);
#endif

  // If the tree still exists (non-superblock), dealloc most nodes, only keep
  // nodes for the best partition and PARTITION_NONE.
  if (pc_tree_dealloc == 0)
    av1_free_pc_tree_recursive(pc_tree, num_planes, 1, 1,
                               cpi->sf.part_sf.partition_search_type);

  if (bsize == cm->seq_params->sb_size) {
    assert(best_rdc.rate < INT_MAX);
    assert(best_rdc.dist < INT64_MAX);
  } else {
    assert(tp_orig == *tp);
  }

  // Restore the rd multiplier.
  x->rdmult = orig_rdmult;
  return part_search_state.found_best_partition;
}
#endif  // !CONFIG_REALTIME_ONLY

#undef COLLECT_MOTION_SEARCH_FEATURE_SB

#if CONFIG_RT_ML_PARTITIONING
#define FEATURES 6
#define LABELS 2
static int ml_predict_var_partitioning(AV1_COMP *cpi, MACROBLOCK *x,
                                       BLOCK_SIZE bsize, int mi_row,
                                       int mi_col) {
  AV1_COMMON *const cm = &cpi->common;
  const NN_CONFIG *nn_config = NULL;
  const float *means = NULL;
  const float *vars = NULL;
  switch (bsize) {
    case BLOCK_64X64:
      nn_config = &av1_var_part_nnconfig_64;
      means = av1_var_part_means_64;
      vars = av1_var_part_vars_64;
      break;
    case BLOCK_32X32:
      nn_config = &av1_var_part_nnconfig_32;
      means = av1_var_part_means_32;
      vars = av1_var_part_vars_32;
      break;
    case BLOCK_16X16:
      nn_config = &av1_var_part_nnconfig_16;
      means = av1_var_part_means_16;
      vars = av1_var_part_vars_16;
      break;
    case BLOCK_8X8:
    default: assert(0 && "Unexpected block size."); return -1;
  }

  if (!nn_config) return -1;

  {
    const float thresh = cpi->oxcf.speed <= 5 ? 1.25f : 0.0f;
    float features[FEATURES] = { 0.0f };
    const int dc_q = av1_dc_quant_QTX(cm->quant_params.base_qindex, 0,
                                      cm->seq_params->bit_depth);
    int feature_idx = 0;
    float score[LABELS];

    features[feature_idx] =
        (log1pf((float)(dc_q * dc_q) / 256.0f) - means[feature_idx]) /
        sqrtf(vars[feature_idx]);
    feature_idx++;
    av1_setup_src_planes(x, cpi->source, mi_row, mi_col, 1, bsize);
    {
      const int bs = block_size_wide[bsize];
      const BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);
      const int sb_offset_row = 4 * (mi_row & 15);
      const int sb_offset_col = 4 * (mi_col & 15);
      const uint8_t *pred = x->est_pred + sb_offset_row * 64 + sb_offset_col;
      const uint8_t *src = x->plane[0].src.buf;
      const int src_stride = x->plane[0].src.stride;
      const int pred_stride = 64;
      unsigned int sse;
      int i;
      // Variance of whole block.
      const unsigned int var =
          cpi->ppi->fn_ptr[bsize].vf(src, src_stride, pred, pred_stride, &sse);
      const float factor = (var == 0) ? 1.0f : (1.0f / (float)var);

      features[feature_idx] =
          (log1pf((float)var) - means[feature_idx]) / sqrtf(vars[feature_idx]);
      feature_idx++;
      for (i = 0; i < 4; ++i) {
        const int x_idx = (i & 1) * bs / 2;
        const int y_idx = (i >> 1) * bs / 2;
        const int src_offset = y_idx * src_stride + x_idx;
        const int pred_offset = y_idx * pred_stride + x_idx;
        // Variance of quarter block.
        const unsigned int sub_var =
            cpi->ppi->fn_ptr[subsize].vf(src + src_offset, src_stride,
                                         pred + pred_offset, pred_stride, &sse);
        const float var_ratio = (var == 0) ? 1.0f : factor * (float)sub_var;
        features[feature_idx] =
            (var_ratio - means[feature_idx]) / sqrtf(vars[feature_idx]);
        feature_idx++;
      }
    }
    //    for (int i = 0; i<FEATURES; i++)
    //      printf("F_%d, %f; ", i, features[i]);
    assert(feature_idx == FEATURES);
    av1_nn_predict(features, nn_config, 1, score);
    //    printf("Score %f, thr %f ", (float)score[0], thresh);
    if (score[0] > thresh) return PARTITION_SPLIT;
    if (score[0] < -thresh) return PARTITION_NONE;
    return -1;
  }
}
#undef FEATURES
#undef LABELS

// Uncomment for collecting data for ML-based partitioning
// #define _COLLECT_GROUND_TRUTH_

#ifdef _COLLECT_GROUND_TRUTH_
static int store_partition_data(AV1_COMP *cpi, MACROBLOCK *x, BLOCK_SIZE bsize,
                                int mi_row, int mi_col, PARTITION_TYPE part) {
  AV1_COMMON *const cm = &cpi->common;
  char fname[128];
  switch (bsize) {
    case BLOCK_64X64: sprintf(fname, "data_64x64.txt"); break;
    case BLOCK_32X32: sprintf(fname, "data_32x32.txt"); break;
    case BLOCK_16X16: sprintf(fname, "data_16x16.txt"); break;
    case BLOCK_8X8: sprintf(fname, "data_8x8.txt"); break;
    default: assert(0 && "Unexpected block size."); return -1;
  }

  float features[6];  // DC_Q, VAR, VAR_RATIO-0..3

  FILE *f = fopen(fname, "a");

  {
    const int dc_q = av1_dc_quant_QTX(cm->quant_params.base_qindex, 0,
                                      cm->seq_params->bit_depth);
    int feature_idx = 0;

    features[feature_idx++] = log1pf((float)(dc_q * dc_q) / 256.0f);
    av1_setup_src_planes(x, cpi->source, mi_row, mi_col, 1, bsize);
    {
      const int bs = block_size_wide[bsize];
      const BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);
      const int sb_offset_row = 4 * (mi_row & 15);
      const int sb_offset_col = 4 * (mi_col & 15);
      const uint8_t *pred = x->est_pred + sb_offset_row * 64 + sb_offset_col;
      const uint8_t *src = x->plane[0].src.buf;
      const int src_stride = x->plane[0].src.stride;
      const int pred_stride = 64;
      unsigned int sse;
      int i;
      // Variance of whole block.
      /*
                if (bs == 8)
                {
                  int r, c;
                  printf("%d %d\n", mi_row, mi_col);
                  for (r = 0; r < bs; ++r) {
                    for (c = 0; c < bs; ++c) {
                      printf("%3d ",
                             src[r * src_stride + c] - pred[64 * r + c]);
                    }
                    printf("\n");
                  }
                  printf("\n");
                }
      */
      const unsigned int var =
          cpi->fn_ptr[bsize].vf(src, src_stride, pred, pred_stride, &sse);
      const float factor = (var == 0) ? 1.0f : (1.0f / (float)var);

      features[feature_idx++] = log1pf((float)var);

      fprintf(f, "%f,%f,", features[0], features[1]);
      for (i = 0; i < 4; ++i) {
        const int x_idx = (i & 1) * bs / 2;
        const int y_idx = (i >> 1) * bs / 2;
        const int src_offset = y_idx * src_stride + x_idx;
        const int pred_offset = y_idx * pred_stride + x_idx;
        // Variance of quarter block.
        const unsigned int sub_var =
            cpi->fn_ptr[subsize].vf(src + src_offset, src_stride,
                                    pred + pred_offset, pred_stride, &sse);
        const float var_ratio = (var == 0) ? 1.0f : factor * (float)sub_var;
        features[feature_idx++] = var_ratio;
        fprintf(f, "%f,", var_ratio);
      }

      fprintf(f, "%d\n", part == PARTITION_NONE ? 0 : 1);
    }

    fclose(f);
    return -1;
  }
}
#endif

static void duplicate_mode_info_in_sb(AV1_COMMON *cm, MACROBLOCKD *xd,
                                      int mi_row, int mi_col,
                                      BLOCK_SIZE bsize) {
  const int block_width =
      AOMMIN(mi_size_wide[bsize], cm->mi_params.mi_cols - mi_col);
  const int block_height =
      AOMMIN(mi_size_high[bsize], cm->mi_params.mi_rows - mi_row);
  const int mi_stride = xd->mi_stride;
  MB_MODE_INFO *const src_mi = xd->mi[0];
  int i, j;

  for (j = 0; j < block_height; ++j)
    for (i = 0; i < block_width; ++i) xd->mi[j * mi_stride + i] = src_mi;
}

static inline void copy_mbmi_ext_frame_to_mbmi_ext(
    MB_MODE_INFO_EXT *const mbmi_ext,
    const MB_MODE_INFO_EXT_FRAME *mbmi_ext_best, uint8_t ref_frame_type) {
  memcpy(mbmi_ext->ref_mv_stack[ref_frame_type], mbmi_ext_best->ref_mv_stack,
         sizeof(mbmi_ext->ref_mv_stack[USABLE_REF_MV_STACK_SIZE]));
  memcpy(mbmi_ext->weight[ref_frame_type], mbmi_ext_best->weight,
         sizeof(mbmi_ext->weight[USABLE_REF_MV_STACK_SIZE]));
  mbmi_ext->mode_context[ref_frame_type] = mbmi_ext_best->mode_context;
  mbmi_ext->ref_mv_count[ref_frame_type] = mbmi_ext_best->ref_mv_count;
  memcpy(mbmi_ext->global_mvs, mbmi_ext_best->global_mvs,
         sizeof(mbmi_ext->global_mvs));
}

static void fill_mode_info_sb(AV1_COMP *cpi, MACROBLOCK *x, int mi_row,
                              int mi_col, BLOCK_SIZE bsize, PC_TREE *pc_tree) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  int hbs = mi_size_wide[bsize] >> 1;
  PARTITION_TYPE partition = pc_tree->partitioning;
  BLOCK_SIZE subsize = get_partition_subsize(bsize, partition);

  assert(bsize >= BLOCK_8X8);

  if (mi_row >= cm->mi_params.mi_rows || mi_col >= cm->mi_params.mi_cols)
    return;

  switch (partition) {
    case PARTITION_NONE:
      set_mode_info_offsets(&cm->mi_params, &cpi->mbmi_ext_info, x, xd, mi_row,
                            mi_col);
      *(xd->mi[0]) = pc_tree->none->mic;
      copy_mbmi_ext_frame_to_mbmi_ext(
          &x->mbmi_ext, &pc_tree->none->mbmi_ext_best, LAST_FRAME);
      duplicate_mode_info_in_sb(cm, xd, mi_row, mi_col, bsize);
      break;
    case PARTITION_SPLIT: {
      fill_mode_info_sb(cpi, x, mi_row, mi_col, subsize, pc_tree->split[0]);
      fill_mode_info_sb(cpi, x, mi_row, mi_col + hbs, subsize,
                        pc_tree->split[1]);
      fill_mode_info_sb(cpi, x, mi_row + hbs, mi_col, subsize,
                        pc_tree->split[2]);
      fill_mode_info_sb(cpi, x, mi_row + hbs, mi_col + hbs, subsize,
                        pc_tree->split[3]);
      break;
    }
    default: break;
  }
}

void av1_nonrd_pick_partition(AV1_COMP *cpi, ThreadData *td,
                              TileDataEnc *tile_data, TokenExtra **tp,
                              int mi_row, int mi_col, BLOCK_SIZE bsize,
                              RD_STATS *rd_cost, int do_recon, int64_t best_rd,
                              PC_TREE *pc_tree) {
  AV1_COMMON *const cm = &cpi->common;
  TileInfo *const tile_info = &tile_data->tile_info;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  const int hbs = mi_size_wide[bsize] >> 1;
  TokenExtra *tp_orig = *tp;
  const ModeCosts *mode_costs = &x->mode_costs;
  RD_STATS this_rdc, best_rdc;
  RD_SEARCH_MACROBLOCK_CONTEXT x_ctx;
  int do_split = bsize > BLOCK_8X8;
  // Override skipping rectangular partition operations for edge blocks
  const int force_horz_split = (mi_row + 2 * hbs > cm->mi_params.mi_rows);
  const int force_vert_split = (mi_col + 2 * hbs > cm->mi_params.mi_cols);

  int partition_none_allowed = !force_horz_split && !force_vert_split;

  assert(mi_size_wide[bsize] == mi_size_high[bsize]);  // Square partition only
  assert(cm->seq_params->sb_size == BLOCK_64X64);      // Small SB so far

  (void)*tp_orig;

  av1_invalid_rd_stats(&best_rdc);
  best_rdc.rdcost = best_rd;
#ifndef _COLLECT_GROUND_TRUTH_
  if (partition_none_allowed && do_split) {
    const int ml_predicted_partition =
        ml_predict_var_partitioning(cpi, x, bsize, mi_row, mi_col);
    if (ml_predicted_partition == PARTITION_NONE) do_split = 0;
    if (ml_predicted_partition == PARTITION_SPLIT) partition_none_allowed = 0;
  }
#endif

  xd->above_txfm_context =
      cm->above_contexts.txfm[tile_info->tile_row] + mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (mi_row & MAX_MIB_MASK);
  av1_save_context(x, &x_ctx, mi_row, mi_col, bsize, 3);

  // PARTITION_NONE
  if (partition_none_allowed) {
    pc_tree->none = av1_alloc_pmc(cpi, bsize, &td->shared_coeff_buf);
    if (!pc_tree->none)
      aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate PICK_MODE_CONTEXT");
    PICK_MODE_CONTEXT *ctx = pc_tree->none;

// Flip for RDO based pick mode
#if 0
    RD_STATS dummy;
    av1_invalid_rd_stats(&dummy);
    pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &this_rdc,
                  PARTITION_NONE, bsize, ctx, dummy);
#else
    pick_sb_modes_nonrd(cpi, tile_data, x, mi_row, mi_col, &this_rdc, bsize,
                        ctx);
#endif
    if (this_rdc.rate != INT_MAX) {
      const int pl = partition_plane_context(xd, mi_row, mi_col, bsize);

      this_rdc.rate += mode_costs->partition_cost[pl][PARTITION_NONE];
      this_rdc.rdcost = RDCOST(x->rdmult, this_rdc.rate, this_rdc.dist);
      if (this_rdc.rdcost < best_rdc.rdcost) {
        best_rdc = this_rdc;
        if (bsize >= BLOCK_8X8) pc_tree->partitioning = PARTITION_NONE;
      }
    }
  }

  // PARTITION_SPLIT
  if (do_split) {
    RD_STATS sum_rdc;
    const BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);

    av1_init_rd_stats(&sum_rdc);

    for (int i = 0; i < SUB_PARTITIONS_SPLIT; ++i) {
      pc_tree->split[i] = av1_alloc_pc_tree_node(subsize);
      if (!pc_tree->split[i])
        aom_internal_error(xd->error_info, AOM_CODEC_MEM_ERROR,
                           "Failed to allocate PC_TREE");
      pc_tree->split[i]->index = i;
    }

    int pl = partition_plane_context(xd, mi_row, mi_col, bsize);
    sum_rdc.rate += mode_costs->partition_cost[pl][PARTITION_SPLIT];
    sum_rdc.rdcost = RDCOST(x->rdmult, sum_rdc.rate, sum_rdc.dist);
    for (int i = 0;
         i < SUB_PARTITIONS_SPLIT && sum_rdc.rdcost < best_rdc.rdcost; ++i) {
      const int x_idx = (i & 1) * hbs;
      const int y_idx = (i >> 1) * hbs;

      if (mi_row + y_idx >= cm->mi_params.mi_rows ||
          mi_col + x_idx >= cm->mi_params.mi_cols)
        continue;
      av1_nonrd_pick_partition(cpi, td, tile_data, tp, mi_row + y_idx,
                               mi_col + x_idx, subsize, &this_rdc, i < 3,
                               best_rdc.rdcost - sum_rdc.rdcost,
                               pc_tree->split[i]);

      if (this_rdc.rate == INT_MAX) {
        av1_invalid_rd_stats(&sum_rdc);
      } else {
        sum_rdc.rate += this_rdc.rate;
        sum_rdc.dist += this_rdc.dist;
        sum_rdc.rdcost += this_rdc.rdcost;
      }
    }
    if (sum_rdc.rdcost < best_rdc.rdcost) {
      best_rdc = sum_rdc;
      pc_tree->partitioning = PARTITION_SPLIT;
    }
  }

#ifdef _COLLECT_GROUND_TRUTH_
  store_partition_data(cpi, x, bsize, mi_row, mi_col, pc_tree->partitioning);
#endif

  *rd_cost = best_rdc;

  av1_restore_context(x, &x_ctx, mi_row, mi_col, bsize, 3);

  if (best_rdc.rate == INT_MAX) {
    av1_invalid_rd_stats(rd_cost);
    return;
  }

  // update mode info array
  fill_mode_info_sb(cpi, x, mi_row, mi_col, bsize, pc_tree);

  if (do_recon) {
    if (bsize == cm->seq_params->sb_size) {
      // NOTE: To get estimate for rate due to the tokens, use:
      // int rate_coeffs = 0;
      // encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, DRY_RUN_COSTCOEFFS,
      //           bsize, pc_tree, &rate_coeffs);
      set_cb_offsets(x->cb_offset, 0, 0);
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, OUTPUT_ENABLED, bsize,
                pc_tree, NULL);
    } else {
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, DRY_RUN_NORMAL, bsize,
                pc_tree, NULL);
    }
  }

  if (bsize == BLOCK_64X64 && do_recon) {
    assert(best_rdc.rate < INT_MAX);
    assert(best_rdc.dist < INT64_MAX);
  } else {
    assert(tp_orig == *tp);
  }
}
#endif  // CONFIG_RT_ML_PARTITIONING
