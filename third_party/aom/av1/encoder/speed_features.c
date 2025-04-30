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

#include "av1/common/reconintra.h"

#include "av1/encoder/encoder.h"
#include "av1/encoder/speed_features.h"
#include "av1/encoder/rdopt.h"

#include "aom_dsp/aom_dsp_common.h"

#define MAX_MESH_SPEED 5  // Max speed setting for mesh motion method
// Max speed setting for tx domain evaluation
#define MAX_TX_DOMAIN_EVAL_SPEED 5
static const MESH_PATTERN
    good_quality_mesh_patterns[MAX_MESH_SPEED + 1][MAX_MESH_STEP] = {
      { { 64, 8 }, { 28, 4 }, { 15, 1 }, { 7, 1 } },
      { { 64, 8 }, { 28, 4 }, { 15, 1 }, { 7, 1 } },
      { { 64, 8 }, { 14, 2 }, { 7, 1 }, { 7, 1 } },
      { { 64, 16 }, { 24, 8 }, { 12, 4 }, { 7, 1 } },
      { { 64, 16 }, { 24, 8 }, { 12, 4 }, { 7, 1 } },
      { { 64, 16 }, { 24, 8 }, { 12, 4 }, { 7, 1 } },
    };

// TODO(huisu@google.com): These settings are pretty relaxed, tune them for
// each speed setting
static const MESH_PATTERN
    intrabc_mesh_patterns[MAX_MESH_SPEED + 1][MAX_MESH_STEP] = {
      { { 256, 1 }, { 256, 1 }, { 0, 0 }, { 0, 0 } },
      { { 256, 1 }, { 256, 1 }, { 0, 0 }, { 0, 0 } },
      { { 64, 1 }, { 64, 1 }, { 0, 0 }, { 0, 0 } },
      { { 64, 1 }, { 64, 1 }, { 0, 0 }, { 0, 0 } },
      { { 64, 4 }, { 16, 1 }, { 0, 0 }, { 0, 0 } },
      { { 64, 4 }, { 16, 1 }, { 0, 0 }, { 0, 0 } },
    };

// Threshold values to be used for pruning the txfm_domain_distortion
// based on block MSE
// Index 0: Default mode evaluation, Winner mode processing is not
// applicable (Eg : IntraBc). Index 1: Mode evaluation.
// Index 2: Winner mode evaluation. Index 1 and 2 are applicable when
// enable_winner_mode_for_use_tx_domain_dist speed feature is ON
// TODO(any): Experiment the threshold logic based on variance metric
static const unsigned int tx_domain_dist_thresholds[4][MODE_EVAL_TYPES] = {
  { UINT_MAX, UINT_MAX, UINT_MAX },
  { 22026, 22026, 22026 },
  { 1377, 1377, 1377 },
  { 0, 0, 0 }
};

// Number of different levels of aggressiveness in using transform domain
// distortion during the R-D evaluation based on the speed feature
// tx_domain_dist_level.
#define TX_DOMAIN_DIST_LEVELS 4

// Transform domain distortion type to be used for default, mode and winner mode
// evaluation Index 0: Default mode evaluation, Winner mode processing is not
// applicable (Eg : IntraBc). Index 1: Mode evaluation. Index 2: Winner mode
// evaluation. Index 1 and 2 are applicable when
// enable_winner_mode_for_use_tx_domain_dist speed feature is ON
static const unsigned int
    tx_domain_dist_types[TX_DOMAIN_DIST_LEVELS][MODE_EVAL_TYPES] = {
      { 0, 2, 0 }, { 1, 2, 0 }, { 2, 2, 0 }, { 2, 2, 2 }
    };

// Threshold values to be used for disabling coeff RD-optimization
// based on block MSE / qstep^2.
// TODO(any): Experiment the threshold logic based on variance metric.
// Table has satd and dist threshold value index 0 : dist,index 1: satd
// For each row, the indices are as follows.
// Index 0: Default mode evaluation, Winner mode processing is not applicable
// (Eg : IntraBc)
// Index 1: Mode evaluation.
// Index 2: Winner mode evaluation.
// Index 1 and 2 are applicable when enable_winner_mode_for_coeff_opt speed
// feature is ON
// There are 7 levels with increasing speed, mapping to vertical indices.
static const unsigned int coeff_opt_thresholds[9][MODE_EVAL_TYPES][2] = {
  { { UINT_MAX, UINT_MAX }, { UINT_MAX, UINT_MAX }, { UINT_MAX, UINT_MAX } },
  { { 3200, UINT_MAX }, { 250, UINT_MAX }, { UINT_MAX, UINT_MAX } },
  { { 1728, UINT_MAX }, { 142, UINT_MAX }, { UINT_MAX, UINT_MAX } },
  { { 864, UINT_MAX }, { 142, UINT_MAX }, { UINT_MAX, UINT_MAX } },
  { { 432, UINT_MAX }, { 86, UINT_MAX }, { UINT_MAX, UINT_MAX } },
  { { 864, 97 }, { 142, 16 }, { UINT_MAX, UINT_MAX } },
  { { 432, 97 }, { 86, 16 }, { UINT_MAX, UINT_MAX } },
  { { 216, 25 }, { 86, 10 }, { UINT_MAX, UINT_MAX } },
  { { 216, 25 }, { 0, 10 }, { UINT_MAX, UINT_MAX } }
};

// Transform size to be used for default, mode and winner mode evaluation
// Index 0: Default mode evaluation, Winner mode processing is not applicable
// (Eg : IntraBc) Index 1: Mode evaluation. Index 2: Winner mode evaluation.
// Index 1 and 2 are applicable when enable_winner_mode_for_tx_size_srch speed
// feature is ON
static const TX_SIZE_SEARCH_METHOD
    tx_size_search_methods[4][MODE_EVAL_TYPES] = {
      { USE_FULL_RD, USE_LARGESTALL, USE_FULL_RD },
      { USE_FAST_RD, USE_LARGESTALL, USE_FULL_RD },
      { USE_LARGESTALL, USE_LARGESTALL, USE_FULL_RD },
      { USE_LARGESTALL, USE_LARGESTALL, USE_LARGESTALL }
    };

// Predict transform skip levels to be used for default, mode and winner mode
// evaluation. Index 0: Default mode evaluation, Winner mode processing is not
// applicable. Index 1: Mode evaluation, Index 2: Winner mode evaluation
// Values indicate the aggressiveness of skip flag prediction.
// 0 : no early skip prediction
// 1 : conservative early skip prediction using DCT_DCT
// 2 : early skip prediction based on SSE
static const unsigned int predict_skip_levels[3][MODE_EVAL_TYPES] = {
  { 0, 0, 0 }, { 1, 1, 1 }, { 1, 2, 1 }
};

// Predict skip or DC block level used during transform type search. It is
// indexed using the following:
// First index  : Speed feature 'dc_blk_pred_level' (0 to 3)
// Second index : Mode evaluation type (DEFAULT_EVAL, MODE_EVAL and
// WINNER_MODE_EVAL).
//
// The values of predict_dc_levels[][] indicate the aggressiveness of predicting
// a block as transform skip or DC only.
// Type 0 : No skip block or DC only block prediction
// Type 1 : Prediction of skip block based on residual mean and variance
// Type 2 : Prediction of skip block or DC only block based on residual mean and
// variance
static const unsigned int predict_dc_levels[4][MODE_EVAL_TYPES] = {
  { 0, 0, 0 }, { 1, 1, 0 }, { 2, 2, 0 }, { 2, 2, 2 }
};

#if !CONFIG_FPMT_TEST
// This table holds the maximum number of reference frames for global motion.
// The table is indexed as per the speed feature 'gm_search_type'.
// 0 : All reference frames are allowed.
// 1 : All reference frames except L2 and L3 are allowed.
// 2 : All reference frames except L2, L3 and ARF2 are allowed.
// 3 : No reference frame is allowed.
static const int gm_available_reference_frames[GM_DISABLE_SEARCH + 1] = {
  INTER_REFS_PER_FRAME, INTER_REFS_PER_FRAME - 2, INTER_REFS_PER_FRAME - 3, 0
};
#endif

// Qindex threshold levels used for selecting full-pel motion search.
// ms_qthresh[i][j][k] indicates the qindex boundary value for 'k'th qindex band
// for resolution index 'j' for aggressiveness level 'i'.
// Aggressiveness increases from i = 0 to 2.
// j = 0: lower than 720p resolution, j = 1: 720p or larger resolution.
// Currently invoked only for speed 0, 1 and 2.
static const int ms_qindex_thresh[3][2][2] = { { { 200, 70 }, { MAXQ, 200 } },
                                               { { 170, 50 }, { MAXQ, 200 } },
                                               { { 170, 40 }, { 200, 40 } } };

// Full-pel search methods for aggressive search based on qindex.
// Index 0 is for resolutions lower than 720p, index 1 for 720p or larger
// resolutions. Currently invoked only for speed 1 and 2.
static const SEARCH_METHODS motion_search_method[2] = { CLAMPED_DIAMOND,
                                                        DIAMOND };

// Intra only frames, golden frames (except alt ref overlays) and
// alt ref frames tend to be coded at a higher than ambient quality
static int frame_is_boosted(const AV1_COMP *cpi) {
  return frame_is_kf_gf_arf(cpi);
}

// Set transform rd gate level for all transform search cases.
static inline void set_txfm_rd_gate_level(
    int txfm_rd_gate_level[TX_SEARCH_CASES], int level) {
  assert(level <= MAX_TX_RD_GATE_LEVEL);
  for (int idx = 0; idx < TX_SEARCH_CASES; idx++)
    txfm_rd_gate_level[idx] = level;
}

static void set_allintra_speed_feature_framesize_dependent(
    const AV1_COMP *const cpi, SPEED_FEATURES *const sf, int speed) {
  const AV1_COMMON *const cm = &cpi->common;
  const int is_480p_or_larger = AOMMIN(cm->width, cm->height) >= 480;
  const int is_720p_or_larger = AOMMIN(cm->width, cm->height) >= 720;
  const int is_1080p_or_larger = AOMMIN(cm->width, cm->height) >= 1080;
  const int is_4k_or_larger = AOMMIN(cm->width, cm->height) >= 2160;
  const bool use_hbd = cpi->oxcf.use_highbitdepth;

  if (is_480p_or_larger) {
    sf->part_sf.use_square_partition_only_threshold = BLOCK_128X128;
    if (is_720p_or_larger)
      sf->part_sf.auto_max_partition_based_on_simple_motion = ADAPT_PRED;
    else
      sf->part_sf.auto_max_partition_based_on_simple_motion = RELAXED_PRED;
  } else {
    sf->part_sf.use_square_partition_only_threshold = BLOCK_64X64;
    sf->part_sf.auto_max_partition_based_on_simple_motion = DIRECT_PRED;
    if (use_hbd) sf->tx_sf.prune_tx_size_level = 1;
  }

  if (is_4k_or_larger) {
    sf->part_sf.default_min_partition_size = BLOCK_8X8;
  }

  // TODO(huisu@google.com): train models for 720P and above.
  if (!is_720p_or_larger) {
    sf->part_sf.ml_partition_search_breakout_thresh[0] = 200;  // BLOCK_8X8
    sf->part_sf.ml_partition_search_breakout_thresh[1] = 250;  // BLOCK_16X16
    sf->part_sf.ml_partition_search_breakout_thresh[2] = 300;  // BLOCK_32X32
    sf->part_sf.ml_partition_search_breakout_thresh[3] = 500;  // BLOCK_64X64
    sf->part_sf.ml_partition_search_breakout_thresh[4] = -1;   // BLOCK_128X128
    sf->part_sf.ml_early_term_after_part_split_level = 1;
  }

  if (is_720p_or_larger) {
    // TODO(chiyotsai@google.com): make this speed feature adaptive based on
    // current block's vertical texture instead of hardcoded with resolution
    sf->mv_sf.use_downsampled_sad = 2;
  }

  if (speed >= 1) {
    if (is_720p_or_larger) {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_128X128;
    } else if (is_480p_or_larger) {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_64X64;
    } else {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_32X32;
    }

    if (!is_720p_or_larger) {
      sf->part_sf.ml_partition_search_breakout_thresh[0] = 200;  // BLOCK_8X8
      sf->part_sf.ml_partition_search_breakout_thresh[1] = 250;  // BLOCK_16X16
      sf->part_sf.ml_partition_search_breakout_thresh[2] = 300;  // BLOCK_32X32
      sf->part_sf.ml_partition_search_breakout_thresh[3] = 300;  // BLOCK_64X64
      sf->part_sf.ml_partition_search_breakout_thresh[4] = -1;  // BLOCK_128X128
    }
    sf->part_sf.ml_early_term_after_part_split_level = 2;
  }

  if (speed >= 2) {
    if (is_720p_or_larger) {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_64X64;
    } else if (is_480p_or_larger) {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_32X32;
    } else {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_32X32;
    }

    if (is_720p_or_larger) {
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 24);
      sf->part_sf.partition_search_breakout_rate_thr = 120;
    } else {
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 22);
      sf->part_sf.partition_search_breakout_rate_thr = 100;
    }

    if (is_480p_or_larger) {
      sf->tx_sf.tx_type_search.prune_tx_type_using_stats = 1;
      if (use_hbd) sf->tx_sf.prune_tx_size_level = 2;
    } else {
      if (use_hbd) sf->tx_sf.prune_tx_size_level = 3;
    }
  }

  if (speed >= 3) {
    sf->part_sf.ml_early_term_after_part_split_level = 0;

    if (is_720p_or_larger) {
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 25);
      sf->part_sf.partition_search_breakout_rate_thr = 200;
    } else {
      sf->part_sf.max_intra_bsize = BLOCK_32X32;
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 23);
      sf->part_sf.partition_search_breakout_rate_thr = 120;
    }
    if (use_hbd) sf->tx_sf.prune_tx_size_level = 3;
  }

  if (speed >= 4) {
    if (is_720p_or_larger) {
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 26);
    } else {
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 24);
    }

    if (is_480p_or_larger) {
      sf->tx_sf.tx_type_search.prune_tx_type_using_stats = 2;
    }
  }

  if (speed >= 6) {
    if (is_720p_or_larger) {
      sf->part_sf.auto_max_partition_based_on_simple_motion = NOT_IN_USE;
    } else if (is_480p_or_larger) {
      sf->part_sf.auto_max_partition_based_on_simple_motion = DIRECT_PRED;
    }

    if (is_1080p_or_larger) {
      sf->part_sf.default_min_partition_size = BLOCK_8X8;
    }

    sf->part_sf.use_square_partition_only_threshold = BLOCK_16X16;
  }

  if (speed >= 7) {
    // TODO(kyslov): add more speed features to control speed/quality
  }

  if (speed >= 8) {
    if (!is_480p_or_larger) {
      sf->rt_sf.nonrd_check_partition_merge_mode = 2;
    }
    if (is_720p_or_larger) {
      sf->rt_sf.force_large_partition_blocks_intra = 1;
    }
  }

  if (speed >= 9) {
    // TODO(kyslov): add more speed features to control speed/quality
    if (!is_4k_or_larger) {
      // In av1_select_sb_size(), superblock size is set to 64x64 only for
      // resolutions less than 4k in speed>=9, to improve the multithread
      // performance. If cost update levels are set to INTERNAL_COST_UPD_OFF
      // for resolutions >= 4k, the SB size setting can be modified for these
      // resolutions as well.
      sf->inter_sf.coeff_cost_upd_level = INTERNAL_COST_UPD_OFF;
      sf->inter_sf.mode_cost_upd_level = INTERNAL_COST_UPD_OFF;
    }
  }
}

static void set_allintra_speed_features_framesize_independent(
    const AV1_COMP *const cpi, SPEED_FEATURES *const sf, int speed) {
  const AV1_COMMON *const cm = &cpi->common;
  const int allow_screen_content_tools =
      cm->features.allow_screen_content_tools;
  const int use_hbd = cpi->oxcf.use_highbitdepth;

  sf->part_sf.less_rectangular_check_level = 1;
  sf->part_sf.ml_prune_partition = 1;
  sf->part_sf.prune_ext_partition_types_search_level = 1;
  sf->part_sf.prune_part4_search = 2;
  sf->part_sf.simple_motion_search_prune_rect = 1;
  sf->part_sf.ml_predict_breakout_level = use_hbd ? 1 : 3;
  sf->part_sf.reuse_prev_rd_results_for_part_ab = 1;
  sf->part_sf.use_best_rd_for_pruning = 1;

  sf->intra_sf.intra_pruning_with_hog = 1;
  sf->intra_sf.prune_luma_palette_size_search_level = 1;
  sf->intra_sf.dv_cost_upd_level = INTERNAL_COST_UPD_OFF;
  sf->intra_sf.early_term_chroma_palette_size_search = 1;

  sf->tx_sf.adaptive_txb_search_level = 1;
  sf->tx_sf.intra_tx_size_search_init_depth_sqr = 1;
  sf->tx_sf.model_based_prune_tx_search_level = 1;
  sf->tx_sf.tx_type_search.use_reduced_intra_txset = 1;

  sf->rt_sf.use_nonrd_pick_mode = 0;
  sf->rt_sf.use_real_time_ref_set = 0;

  if (cpi->twopass_frame.fr_content_type == FC_GRAPHICS_ANIMATION ||
      cpi->use_screen_content_tools) {
    sf->mv_sf.exhaustive_searches_thresh = (1 << 20);
  } else {
    sf->mv_sf.exhaustive_searches_thresh = (1 << 25);
  }

  sf->rd_sf.perform_coeff_opt = 1;
  sf->hl_sf.superres_auto_search_type = SUPERRES_AUTO_DUAL;

  if (speed >= 1) {
    sf->part_sf.intra_cnn_based_part_prune_level =
        allow_screen_content_tools ? 0 : 2;
    sf->part_sf.simple_motion_search_early_term_none = 1;
    // TODO(Venkat): Clean-up frame type dependency for
    // simple_motion_search_split in partition search function and set the
    // speed feature accordingly
    sf->part_sf.simple_motion_search_split = allow_screen_content_tools ? 1 : 2;
    sf->part_sf.ml_predict_breakout_level = use_hbd ? 2 : 3;
    sf->part_sf.reuse_best_prediction_for_part_ab = 1;

    sf->mv_sf.exhaustive_searches_thresh <<= 1;

    sf->intra_sf.prune_palette_search_level = 1;
    sf->intra_sf.prune_luma_palette_size_search_level = 2;
    sf->intra_sf.top_intra_model_count_allowed = 3;

    sf->tx_sf.adaptive_txb_search_level = 2;
    sf->tx_sf.inter_tx_size_search_init_depth_rect = 1;
    sf->tx_sf.inter_tx_size_search_init_depth_sqr = 1;
    sf->tx_sf.intra_tx_size_search_init_depth_rect = 1;
    sf->tx_sf.model_based_prune_tx_search_level = 0;
    sf->tx_sf.tx_type_search.ml_tx_split_thresh = 4000;
    sf->tx_sf.tx_type_search.prune_2d_txfm_mode = TX_TYPE_PRUNE_2;
    sf->tx_sf.tx_type_search.skip_tx_search = 1;

    sf->rd_sf.perform_coeff_opt = 2;
    sf->rd_sf.tx_domain_dist_level = 1;
    sf->rd_sf.tx_domain_dist_thres_level = 1;

    sf->lpf_sf.cdef_pick_method = CDEF_FAST_SEARCH_LVL1;
    sf->lpf_sf.dual_sgr_penalty_level = 1;
    sf->lpf_sf.enable_sgr_ep_pruning = 1;
  }

  if (speed >= 2) {
    sf->mv_sf.auto_mv_step_size = 1;

    sf->intra_sf.disable_smooth_intra = 1;
    sf->intra_sf.intra_pruning_with_hog = 2;
    sf->intra_sf.prune_filter_intra_level = 1;

    sf->rd_sf.perform_coeff_opt = 3;

    sf->lpf_sf.prune_wiener_based_on_src_var = 1;
    sf->lpf_sf.prune_sgr_based_on_wiener = 1;
  }

  if (speed >= 3) {
    sf->hl_sf.high_precision_mv_usage = CURRENT_Q;
    sf->hl_sf.recode_loop = ALLOW_RECODE_KFARFGF;

    sf->part_sf.less_rectangular_check_level = 2;
    sf->part_sf.simple_motion_search_prune_agg = SIMPLE_AGG_LVL1;
    sf->part_sf.prune_ext_part_using_split_info = 1;

    sf->mv_sf.full_pixel_search_level = 1;
    sf->mv_sf.search_method = DIAMOND;

    // TODO(chiyotsai@google.com): the thresholds chosen for intra hog are
    // inherited directly from luma hog with some minor tweaking. Eventually we
    // should run this with a bayesian optimizer to find the Pareto frontier.
    sf->intra_sf.chroma_intra_pruning_with_hog = 2;
    sf->intra_sf.intra_pruning_with_hog = 3;
    sf->intra_sf.prune_palette_search_level = 2;

    sf->tx_sf.adaptive_txb_search_level = 2;
    sf->tx_sf.tx_type_search.use_skip_flag_prediction = 2;
    sf->tx_sf.use_rd_based_breakout_for_intra_tx_search = true;

    // TODO(any): evaluate if these lpf features can be moved to speed 2.
    // For screen content, "prune_sgr_based_on_wiener = 2" cause large quality
    // loss.
    sf->lpf_sf.prune_sgr_based_on_wiener = allow_screen_content_tools ? 1 : 2;
    sf->lpf_sf.disable_loop_restoration_chroma = 0;
    sf->lpf_sf.reduce_wiener_window_size = 1;
    sf->lpf_sf.prune_wiener_based_on_src_var = 2;
  }

  if (speed >= 4) {
    sf->mv_sf.subpel_search_method = SUBPEL_TREE_PRUNED_MORE;

    sf->part_sf.simple_motion_search_prune_agg = SIMPLE_AGG_LVL2;
    sf->part_sf.simple_motion_search_reduce_search_steps = 4;
    sf->part_sf.prune_ext_part_using_split_info = 2;
    sf->part_sf.early_term_after_none_split = 1;
    sf->part_sf.ml_predict_breakout_level = 3;

    sf->intra_sf.prune_chroma_modes_using_luma_winner = 1;

    sf->mv_sf.simple_motion_subpel_force_stop = HALF_PEL;

    sf->tpl_sf.prune_starting_mv = 2;
    sf->tpl_sf.subpel_force_stop = HALF_PEL;
    sf->tpl_sf.search_method = FAST_BIGDIA;

    sf->tx_sf.tx_type_search.winner_mode_tx_type_pruning = 2;
    sf->tx_sf.tx_type_search.fast_intra_tx_type_search = 1;
    sf->tx_sf.tx_type_search.prune_2d_txfm_mode = TX_TYPE_PRUNE_3;
    sf->tx_sf.tx_type_search.prune_tx_type_est_rd = 1;

    sf->rd_sf.perform_coeff_opt = 5;
    sf->rd_sf.tx_domain_dist_thres_level = 3;

    sf->lpf_sf.lpf_pick = LPF_PICK_FROM_FULL_IMAGE_NON_DUAL;
    sf->lpf_sf.cdef_pick_method = CDEF_FAST_SEARCH_LVL3;

    sf->mv_sf.reduce_search_range = 1;

    sf->winner_mode_sf.enable_winner_mode_for_coeff_opt = 1;
    sf->winner_mode_sf.enable_winner_mode_for_use_tx_domain_dist = 1;
    sf->winner_mode_sf.multi_winner_mode_type = MULTI_WINNER_MODE_DEFAULT;
    sf->winner_mode_sf.enable_winner_mode_for_tx_size_srch = 1;
  }

  if (speed >= 5) {
    sf->part_sf.simple_motion_search_prune_agg = SIMPLE_AGG_LVL3;
    sf->part_sf.ext_partition_eval_thresh =
        allow_screen_content_tools ? BLOCK_8X8 : BLOCK_16X16;
    sf->part_sf.intra_cnn_based_part_prune_level =
        allow_screen_content_tools ? 1 : 2;

    sf->intra_sf.chroma_intra_pruning_with_hog = 3;

    sf->lpf_sf.use_coarse_filter_level_search = 0;
    // Disable Wiener and Self-guided Loop restoration filters.
    sf->lpf_sf.disable_wiener_filter = true;
    sf->lpf_sf.disable_sgr_filter = true;

    sf->mv_sf.prune_mesh_search = PRUNE_MESH_SEARCH_LVL_2;

    sf->winner_mode_sf.multi_winner_mode_type = MULTI_WINNER_MODE_FAST;
  }

  if (speed >= 6) {
    sf->intra_sf.prune_smooth_intra_mode_for_chroma = 1;
    sf->intra_sf.prune_filter_intra_level = 2;
    sf->intra_sf.chroma_intra_pruning_with_hog = 4;
    sf->intra_sf.intra_pruning_with_hog = 4;
    sf->intra_sf.cfl_search_range = 1;
    sf->intra_sf.top_intra_model_count_allowed = 2;
    sf->intra_sf.adapt_top_model_rd_count_using_neighbors = 1;
    sf->intra_sf.prune_luma_odd_delta_angles_in_intra = 1;

    sf->part_sf.prune_rectangular_split_based_on_qidx =
        allow_screen_content_tools ? 0 : 2;
    sf->part_sf.prune_rect_part_using_4x4_var_deviation = true;
    sf->part_sf.prune_rect_part_using_none_pred_mode = true;
    sf->part_sf.prune_sub_8x8_partition_level =
        allow_screen_content_tools ? 0 : 1;
    sf->part_sf.prune_part4_search = 3;
    // TODO(jingning): This might not be a good trade off if the
    // target image quality is very low.
    sf->part_sf.default_max_partition_size = BLOCK_32X32;

    sf->mv_sf.use_bsize_dependent_search_method = 1;

    sf->tx_sf.tx_type_search.winner_mode_tx_type_pruning = 3;
    sf->tx_sf.tx_type_search.prune_tx_type_est_rd = 0;
    sf->tx_sf.prune_intra_tx_depths_using_nn = true;

    sf->rd_sf.perform_coeff_opt = 6;
    sf->rd_sf.tx_domain_dist_level = 3;

    sf->lpf_sf.cdef_pick_method = CDEF_FAST_SEARCH_LVL4;
    sf->lpf_sf.lpf_pick = LPF_PICK_FROM_Q;

    sf->winner_mode_sf.multi_winner_mode_type = MULTI_WINNER_MODE_OFF;
    sf->winner_mode_sf.prune_winner_mode_eval_level = 1;
    sf->winner_mode_sf.dc_blk_pred_level = 1;
  }
  // The following should make all-intra mode speed 7 approximately equal
  // to real-time speed 6,
  // all-intra speed 8 close to real-time speed 7, and all-intra speed 9
  // close to real-time speed 8
  if (speed >= 7) {
    sf->part_sf.default_min_partition_size = BLOCK_8X8;
    sf->part_sf.partition_search_type = VAR_BASED_PARTITION;
    sf->lpf_sf.cdef_pick_method = CDEF_PICK_FROM_Q;
    sf->rt_sf.mode_search_skip_flags |= FLAG_SKIP_INTRA_DIRMISMATCH;
    sf->rt_sf.var_part_split_threshold_shift = 7;
  }

  if (speed >= 8) {
    sf->rt_sf.hybrid_intra_pickmode = 1;
    sf->rt_sf.use_nonrd_pick_mode = 1;
    sf->rt_sf.nonrd_check_partition_merge_mode = 1;
    sf->rt_sf.var_part_split_threshold_shift = 8;
    // Set mask for intra modes.
    for (int i = 0; i < BLOCK_SIZES; ++i)
      if (i >= BLOCK_32X32)
        sf->rt_sf.intra_y_mode_bsize_mask_nrd[i] = INTRA_DC;
      else
        // Use DC, H, V intra mode for block sizes < 32X32.
        sf->rt_sf.intra_y_mode_bsize_mask_nrd[i] = INTRA_DC_H_V;
  }

  if (speed >= 9) {
    sf->inter_sf.coeff_cost_upd_level = INTERNAL_COST_UPD_SBROW;
    sf->inter_sf.mode_cost_upd_level = INTERNAL_COST_UPD_SBROW;

    sf->rt_sf.nonrd_check_partition_merge_mode = 0;
    sf->rt_sf.hybrid_intra_pickmode = 0;
    sf->rt_sf.var_part_split_threshold_shift = 9;
    sf->rt_sf.vbp_prune_16x16_split_using_min_max_sub_blk_var = true;
    sf->rt_sf.prune_h_pred_using_best_mode_so_far = true;
    sf->rt_sf.enable_intra_mode_pruning_using_neighbors = true;
    sf->rt_sf.prune_intra_mode_using_best_sad_so_far = true;
  }

  // As the speed feature prune_chroma_modes_using_luma_winner already
  // constrains the number of chroma directional mode evaluations to a maximum
  // of 1, the HOG computation and the associated pruning logic does not seem to
  // help speed-up the chroma mode evaluations. Hence disable the speed feature
  // chroma_intra_pruning_with_hog when prune_chroma_modes_using_luma_winner is
  // enabled.
  if (sf->intra_sf.prune_chroma_modes_using_luma_winner)
    sf->intra_sf.chroma_intra_pruning_with_hog = 0;
}

// Configures framesize dependent speed features for low complexity decoding.
static void set_good_speed_features_lc_dec_framesize_dependent(
    const AV1_COMP *const cpi, SPEED_FEATURES *const sf, int speed) {
  if (speed < 1 || speed > 3) return;

  const AV1_COMMON *const cm = &cpi->common;
  const int is_608p_or_larger = AOMMIN(cm->width, cm->height) >= 608;
  const FRAME_UPDATE_TYPE update_type =
      get_frame_update_type(&cpi->ppi->gf_group, cpi->gf_frame_index);

  if (is_608p_or_larger) {
    sf->lpf_sf.skip_loop_filter_using_filt_error =
        (update_type != OVERLAY_UPDATE && update_type != INTNL_OVERLAY_UPDATE &&
         cpi->common.current_frame.pyramid_level > 1)
            ? 1
            : 0;
  }

  const int short_dimension = AOMMIN(cm->width, cm->height);
  if (short_dimension > 480 && short_dimension < 720) {
    const int leaf_and_overlay_frames =
        (update_type == LF_UPDATE || update_type == OVERLAY_UPDATE ||
         update_type == INTNL_OVERLAY_UPDATE);
    if (leaf_and_overlay_frames) sf->gm_sf.gm_search_type = GM_DISABLE_SEARCH;
  }
}

static void set_good_speed_feature_framesize_dependent(
    const AV1_COMP *const cpi, SPEED_FEATURES *const sf, int speed) {
  const AV1_COMMON *const cm = &cpi->common;
  const int is_480p_or_lesser = AOMMIN(cm->width, cm->height) <= 480;
  const int is_480p_or_larger = AOMMIN(cm->width, cm->height) >= 480;
  const int is_720p_or_larger = AOMMIN(cm->width, cm->height) >= 720;
  const int is_1080p_or_larger = AOMMIN(cm->width, cm->height) >= 1080;
  const int is_4k_or_larger = AOMMIN(cm->width, cm->height) >= 2160;
  const bool use_hbd = cpi->oxcf.use_highbitdepth;
  // Speed features applicable for temporal filtering and tpl modules may be
  // changed based on frame type at places where the sf is applied (Example :
  // use_downsampled_sad). This is because temporal filtering and tpl modules
  // are called before this function (except for the first key frame).
  // TODO(deepa.kg@ittiam.com): For the speed features applicable to temporal
  // filtering and tpl modules, modify the sf initialization appropriately
  // before calling the modules.
  const int boosted = frame_is_boosted(cpi);
  const int is_boosted_arf2_bwd_type =
      boosted ||
      cpi->ppi->gf_group.update_type[cpi->gf_frame_index] == INTNL_ARF_UPDATE;
  const int is_lf_frame =
      cpi->ppi->gf_group.update_type[cpi->gf_frame_index] == LF_UPDATE;
  const int allow_screen_content_tools =
      cm->features.allow_screen_content_tools;

  if (is_480p_or_larger) {
    sf->part_sf.use_square_partition_only_threshold = BLOCK_128X128;
    if (is_720p_or_larger)
      sf->part_sf.auto_max_partition_based_on_simple_motion = ADAPT_PRED;
    else
      sf->part_sf.auto_max_partition_based_on_simple_motion = RELAXED_PRED;
  } else {
    sf->part_sf.use_square_partition_only_threshold = BLOCK_64X64;
    sf->part_sf.auto_max_partition_based_on_simple_motion = DIRECT_PRED;
    if (use_hbd) sf->tx_sf.prune_tx_size_level = 1;
  }

  if (is_4k_or_larger) {
    sf->part_sf.default_min_partition_size = BLOCK_8X8;
  }

  // TODO(huisu@google.com): train models for 720P and above.
  if (!is_720p_or_larger) {
    sf->part_sf.ml_partition_search_breakout_thresh[0] = 200;  // BLOCK_8X8
    sf->part_sf.ml_partition_search_breakout_thresh[1] = 250;  // BLOCK_16X16
    sf->part_sf.ml_partition_search_breakout_thresh[2] = 300;  // BLOCK_32X32
    sf->part_sf.ml_partition_search_breakout_thresh[3] = 500;  // BLOCK_64X64
    sf->part_sf.ml_partition_search_breakout_thresh[4] = -1;   // BLOCK_128X128
    sf->part_sf.ml_early_term_after_part_split_level = 1;
  }

  if (is_720p_or_larger) {
    // TODO(chiyotsai@google.com): make this speed feature adaptive based on
    // current block's vertical texture instead of hardcoded with resolution
    sf->mv_sf.use_downsampled_sad = 2;
  }

  if (!is_720p_or_larger) {
    const RateControlCfg *const rc_cfg = &cpi->oxcf.rc_cfg;
    const int rate_tolerance =
        AOMMIN(rc_cfg->under_shoot_pct, rc_cfg->over_shoot_pct);
    sf->hl_sf.recode_tolerance = 25 + (rate_tolerance >> 2);
  }

  if (speed >= 1) {
    if (is_480p_or_lesser) sf->inter_sf.skip_newmv_in_drl = 1;

    if (is_720p_or_larger) {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_128X128;
    } else if (is_480p_or_larger) {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_64X64;
    } else {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_32X32;
    }

    if (!is_720p_or_larger) {
      sf->part_sf.ml_partition_search_breakout_thresh[0] = 200;  // BLOCK_8X8
      sf->part_sf.ml_partition_search_breakout_thresh[1] = 250;  // BLOCK_16X16
      sf->part_sf.ml_partition_search_breakout_thresh[2] = 300;  // BLOCK_32X32
      sf->part_sf.ml_partition_search_breakout_thresh[3] = 300;  // BLOCK_64X64
      sf->part_sf.ml_partition_search_breakout_thresh[4] = -1;  // BLOCK_128X128
    }
    sf->part_sf.ml_early_term_after_part_split_level = 2;

    sf->lpf_sf.cdef_pick_method = CDEF_FAST_SEARCH_LVL1;
  }

  if (speed >= 2) {
    if (is_720p_or_larger) {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_64X64;
    } else if (is_480p_or_larger) {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_32X32;
    } else {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_32X32;
    }

    if (is_720p_or_larger) {
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 24);
      sf->part_sf.partition_search_breakout_rate_thr = 120;
    } else {
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 22);
      sf->part_sf.partition_search_breakout_rate_thr = 100;
    }

    if (is_720p_or_larger) {
      sf->inter_sf.prune_obmc_prob_thresh = 16;
    } else {
      sf->inter_sf.prune_obmc_prob_thresh = 8;
    }

    if (is_480p_or_larger) {
      sf->inter_sf.disable_interintra_wedge_var_thresh = 100;
    } else {
      sf->inter_sf.disable_interintra_wedge_var_thresh = UINT_MAX;
    }

    if (is_480p_or_lesser) sf->inter_sf.skip_ext_comp_nearmv_mode = 1;

    if (is_720p_or_larger) {
      sf->inter_sf.limit_inter_mode_cands = is_lf_frame ? 1 : 0;
    } else {
      sf->inter_sf.limit_inter_mode_cands = is_lf_frame ? 2 : 0;
    }

    if (is_480p_or_larger) {
      sf->tx_sf.tx_type_search.prune_tx_type_using_stats = 1;
      if (use_hbd) sf->tx_sf.prune_tx_size_level = 2;
    } else {
      if (use_hbd) sf->tx_sf.prune_tx_size_level = 3;
      sf->tx_sf.tx_type_search.winner_mode_tx_type_pruning = boosted ? 0 : 1;
      sf->winner_mode_sf.enable_winner_mode_for_tx_size_srch = boosted ? 0 : 1;
    }

    if (!is_720p_or_larger) {
      sf->mv_sf.disable_second_mv = 1;
      sf->mv_sf.auto_mv_step_size = 2;
    } else {
      sf->mv_sf.disable_second_mv = boosted ? 0 : 2;
      sf->mv_sf.auto_mv_step_size = 1;
    }

    if (!is_720p_or_larger) {
      sf->hl_sf.recode_tolerance = 50;
      sf->inter_sf.disable_interinter_wedge_newmv_search =
          is_boosted_arf2_bwd_type ? 0 : 1;
      sf->inter_sf.enable_fast_wedge_mask_search = 1;
    }
  }

  if (speed >= 3) {
    sf->inter_sf.enable_fast_wedge_mask_search = 1;
    sf->inter_sf.skip_newmv_in_drl = 2;
    sf->inter_sf.skip_ext_comp_nearmv_mode = 1;
    sf->inter_sf.limit_inter_mode_cands = is_lf_frame ? 3 : 0;
    sf->inter_sf.disable_interinter_wedge_newmv_search = boosted ? 0 : 1;
    sf->tx_sf.tx_type_search.winner_mode_tx_type_pruning = 1;
    sf->winner_mode_sf.enable_winner_mode_for_tx_size_srch =
        frame_is_intra_only(&cpi->common) ? 0 : 1;

    sf->part_sf.ml_early_term_after_part_split_level = 0;

    if (is_720p_or_larger) {
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 25);
      sf->part_sf.partition_search_breakout_rate_thr = 200;
      sf->part_sf.skip_non_sq_part_based_on_none = is_lf_frame ? 2 : 0;
    } else {
      sf->part_sf.max_intra_bsize = BLOCK_32X32;
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 23);
      sf->part_sf.partition_search_breakout_rate_thr = 120;
      sf->part_sf.skip_non_sq_part_based_on_none = is_lf_frame ? 1 : 0;
    }
    if (use_hbd) sf->tx_sf.prune_tx_size_level = 3;

    if (is_480p_or_larger) {
      sf->part_sf.early_term_after_none_split = 1;
    } else {
      sf->part_sf.early_term_after_none_split = 0;
    }
    if (is_720p_or_larger) {
      sf->intra_sf.skip_intra_in_interframe = boosted ? 1 : 2;
    } else {
      sf->intra_sf.skip_intra_in_interframe = boosted ? 1 : 3;
    }

    if (is_720p_or_larger) {
      sf->inter_sf.disable_interinter_wedge_var_thresh = 100;
      sf->inter_sf.limit_txfm_eval_per_mode = boosted ? 0 : 1;
    } else {
      sf->inter_sf.disable_interinter_wedge_var_thresh = UINT_MAX;
      sf->inter_sf.limit_txfm_eval_per_mode = boosted ? 0 : 2;
      sf->lpf_sf.cdef_pick_method = CDEF_FAST_SEARCH_LVL2;
    }

    sf->inter_sf.disable_interintra_wedge_var_thresh = UINT_MAX;
  }

  if (speed >= 4) {
    sf->tx_sf.tx_type_search.winner_mode_tx_type_pruning = 2;
    sf->winner_mode_sf.enable_winner_mode_for_tx_size_srch = 1;
    if (is_720p_or_larger) {
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 26);
    } else {
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 24);
    }
    sf->part_sf.early_term_after_none_split = 1;

    if (is_480p_or_larger) {
      sf->tx_sf.tx_type_search.prune_tx_type_using_stats = 2;
    } else {
      sf->mv_sf.skip_fullpel_search_using_startmv = boosted ? 0 : 1;
    }

    sf->inter_sf.disable_interinter_wedge_var_thresh = UINT_MAX;
    sf->inter_sf.prune_obmc_prob_thresh = INT_MAX;
    sf->inter_sf.limit_txfm_eval_per_mode = boosted ? 0 : 2;
    if (is_480p_or_lesser) sf->inter_sf.skip_newmv_in_drl = 3;

    if (is_720p_or_larger) {
      sf->inter_sf.prune_comp_ref_frames = 1;
    } else if (is_480p_or_larger) {
      sf->inter_sf.prune_comp_ref_frames = is_boosted_arf2_bwd_type ? 0 : 1;
    }

    if (is_720p_or_larger)
      sf->hl_sf.recode_tolerance = 32;
    else
      sf->hl_sf.recode_tolerance = 55;

    sf->intra_sf.skip_intra_in_interframe = 4;

    sf->lpf_sf.cdef_pick_method = CDEF_FAST_SEARCH_LVL3;
  }

  if (speed >= 5) {
    if (is_720p_or_larger) {
      sf->inter_sf.prune_warped_prob_thresh = 16;
    } else if (is_480p_or_larger) {
      sf->inter_sf.prune_warped_prob_thresh = 8;
    }
    if (is_720p_or_larger) sf->hl_sf.recode_tolerance = 40;

    sf->inter_sf.skip_newmv_in_drl = 4;
    sf->inter_sf.prune_comp_ref_frames = 1;
    sf->mv_sf.skip_fullpel_search_using_startmv = boosted ? 0 : 1;

    if (!is_720p_or_larger) {
      sf->inter_sf.mv_cost_upd_level = INTERNAL_COST_UPD_SBROW_SET;
      sf->inter_sf.prune_nearest_near_mv_using_refmv_weight =
          (boosted || allow_screen_content_tools) ? 0 : 1;
      sf->mv_sf.use_downsampled_sad = 1;
    }

    if (!is_480p_or_larger) {
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 26);
    }

    if (is_480p_or_lesser) {
      sf->inter_sf.prune_nearmv_using_neighbors = PRUNE_NEARMV_LEVEL1;
    } else {
      sf->inter_sf.prune_nearmv_using_neighbors = PRUNE_NEARMV_LEVEL2;
    }

    if (is_720p_or_larger)
      sf->part_sf.ext_part_eval_based_on_cur_best =
          (allow_screen_content_tools || frame_is_intra_only(cm)) ? 0 : 1;

    if (is_480p_or_larger) {
      sf->tpl_sf.reduce_num_frames = 1;
    }
  }

  if (speed >= 6) {
    sf->tx_sf.tx_type_search.winner_mode_tx_type_pruning = 4;
    sf->inter_sf.prune_nearmv_using_neighbors = PRUNE_NEARMV_LEVEL3;
    sf->inter_sf.prune_comp_ref_frames = 2;
    sf->inter_sf.prune_nearest_near_mv_using_refmv_weight =
        (boosted || allow_screen_content_tools) ? 0 : 1;
    sf->mv_sf.skip_fullpel_search_using_startmv = boosted ? 0 : 2;

    if (is_720p_or_larger) {
      sf->part_sf.auto_max_partition_based_on_simple_motion = NOT_IN_USE;
    } else if (is_480p_or_larger) {
      sf->part_sf.auto_max_partition_based_on_simple_motion = DIRECT_PRED;
    }

    if (is_480p_or_larger) {
      sf->hl_sf.allow_sub_blk_me_in_tf = 1;
    }

    if (is_1080p_or_larger) {
      sf->part_sf.default_min_partition_size = BLOCK_8X8;
    }

    if (is_720p_or_larger) {
      sf->inter_sf.disable_masked_comp = 1;
    }

    if (!is_720p_or_larger) {
      sf->inter_sf.coeff_cost_upd_level = INTERNAL_COST_UPD_SBROW;
      sf->inter_sf.mode_cost_upd_level = INTERNAL_COST_UPD_SBROW;
    }

    if (is_720p_or_larger) {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_32X32;
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 28);
    } else {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_16X16;
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 26);
    }

    if (is_720p_or_larger) {
      sf->inter_sf.prune_ref_mv_idx_search = 2;
    } else {
      sf->inter_sf.prune_ref_mv_idx_search = 1;
    }

    if (!is_720p_or_larger) {
      sf->tx_sf.tx_type_search.fast_inter_tx_type_prob_thresh =
          is_boosted_arf2_bwd_type ? 450 : 150;
    }

    sf->lpf_sf.cdef_pick_method = CDEF_FAST_SEARCH_LVL4;

    sf->hl_sf.recode_tolerance = 55;
  }

  if (cpi->oxcf.enable_low_complexity_decode)
    set_good_speed_features_lc_dec_framesize_dependent(cpi, sf, speed);
}

// Configures framesize independent speed features for low complexity decoding.
static void set_good_speed_features_lc_dec_framesize_independent(
    const AV1_COMP *const cpi, SPEED_FEATURES *const sf) {
  const FRAME_UPDATE_TYPE update_type =
      get_frame_update_type(&cpi->ppi->gf_group, cpi->gf_frame_index);

  if (cpi->oxcf.enable_low_complexity_decode >= 1) {
    sf->lpf_sf.adaptive_luma_loop_filter_skip =
        (update_type != OVERLAY_UPDATE && update_type != INTNL_OVERLAY_UPDATE)
            ? 1
            : 0;
  }
}

static void set_good_speed_features_framesize_independent(
    const AV1_COMP *const cpi, SPEED_FEATURES *const sf, int speed) {
  const AV1_COMMON *const cm = &cpi->common;
  const GF_GROUP *const gf_group = &cpi->ppi->gf_group;
  const int boosted = frame_is_boosted(cpi);
  const int is_boosted_arf2_bwd_type =
      boosted || gf_group->update_type[cpi->gf_frame_index] == INTNL_ARF_UPDATE;
  const int is_inter_frame =
      gf_group->frame_type[cpi->gf_frame_index] == INTER_FRAME;
  const int allow_screen_content_tools =
      cm->features.allow_screen_content_tools;
  const int use_hbd = cpi->oxcf.use_highbitdepth;
  if (!cpi->oxcf.tile_cfg.enable_large_scale_tile) {
    sf->hl_sf.high_precision_mv_usage = LAST_MV_DATA;
  }

  // Speed 0 for all speed features that give neutral coding performance change.
  sf->gm_sf.gm_search_type = boosted ? GM_REDUCED_REF_SEARCH_SKIP_L2_L3_ARF2
                                     : GM_SEARCH_CLOSEST_REFS_ONLY;
  sf->gm_sf.prune_ref_frame_for_gm_search = boosted ? 0 : 1;
  sf->gm_sf.disable_gm_search_based_on_stats = 1;

  sf->part_sf.less_rectangular_check_level = 1;
  sf->part_sf.ml_prune_partition = 1;
  sf->part_sf.prune_ext_partition_types_search_level = 1;
  sf->part_sf.prune_part4_search = 2;
  sf->part_sf.simple_motion_search_prune_rect = 1;
  sf->part_sf.ml_predict_breakout_level = use_hbd ? 1 : 3;
  sf->part_sf.reuse_prev_rd_results_for_part_ab = 1;
  sf->part_sf.use_best_rd_for_pruning = 1;
  sf->part_sf.simple_motion_search_prune_agg =
      allow_screen_content_tools ? NO_PRUNING : SIMPLE_AGG_LVL0;

  // TODO(debargha): Test, tweak and turn on either 1 or 2
  sf->inter_sf.inter_mode_rd_model_estimation = 1;
  sf->inter_sf.model_based_post_interp_filter_breakout = 1;
  sf->inter_sf.prune_compound_using_single_ref = 1;
  sf->inter_sf.prune_mode_search_simple_translation = 1;
  sf->inter_sf.prune_ref_frame_for_rect_partitions =
      (boosted || (allow_screen_content_tools))
          ? 0
          : (is_boosted_arf2_bwd_type ? 1 : 2);
  sf->inter_sf.reduce_inter_modes = boosted ? 1 : 2;
  sf->inter_sf.selective_ref_frame = 1;
  sf->inter_sf.use_dist_wtd_comp_flag = DIST_WTD_COMP_SKIP_MV_SEARCH;

  sf->interp_sf.use_fast_interpolation_filter_search = 1;

  sf->intra_sf.intra_pruning_with_hog = 1;

  sf->tx_sf.adaptive_txb_search_level = 1;
  sf->tx_sf.intra_tx_size_search_init_depth_sqr = 1;
  sf->tx_sf.model_based_prune_tx_search_level = 1;
  sf->tx_sf.tx_type_search.use_reduced_intra_txset = 1;

  sf->tpl_sf.search_method = NSTEP_8PT;

  sf->rt_sf.use_nonrd_pick_mode = 0;
  sf->rt_sf.use_real_time_ref_set = 0;

  if (cpi->twopass_frame.fr_content_type == FC_GRAPHICS_ANIMATION ||
      cpi->use_screen_content_tools) {
    sf->mv_sf.exhaustive_searches_thresh = (1 << 20);
  } else {
    sf->mv_sf.exhaustive_searches_thresh = (1 << 25);
  }

  sf->rd_sf.perform_coeff_opt = 1;
  sf->hl_sf.superres_auto_search_type = SUPERRES_AUTO_DUAL;

  if (speed >= 1) {
    sf->hl_sf.adjust_num_frames_for_arf_filtering =
        allow_screen_content_tools ? 0 : 1;

    sf->part_sf.intra_cnn_based_part_prune_level =
        allow_screen_content_tools ? 0 : 2;
    sf->part_sf.simple_motion_search_early_term_none = 1;
    // TODO(Venkat): Clean-up frame type dependency for
    // simple_motion_search_split in partition search function and set the
    // speed feature accordingly
    sf->part_sf.simple_motion_search_split = allow_screen_content_tools ? 1 : 2;
    sf->part_sf.ml_predict_breakout_level = use_hbd ? 2 : 3;

    sf->mv_sf.exhaustive_searches_thresh <<= 1;
    sf->mv_sf.obmc_full_pixel_search_level = 1;
    sf->mv_sf.use_accurate_subpel_search = USE_4_TAPS;
    sf->mv_sf.disable_extensive_joint_motion_search = 1;

    sf->inter_sf.prune_comp_search_by_single_result = boosted ? 2 : 1;
    sf->inter_sf.prune_comp_type_by_comp_avg = 1;
    sf->inter_sf.prune_comp_type_by_model_rd = boosted ? 0 : 1;
    sf->inter_sf.prune_ref_frame_for_rect_partitions =
        (frame_is_intra_only(&cpi->common) || (allow_screen_content_tools))
            ? 0
            : (boosted ? 1 : 2);
    sf->inter_sf.reduce_inter_modes = boosted ? 1 : 3;
    sf->inter_sf.reuse_inter_intra_mode = 1;
    sf->inter_sf.selective_ref_frame = 2;
    sf->inter_sf.skip_arf_compound = 1;

    sf->interp_sf.use_interp_filter = 1;

    sf->intra_sf.prune_palette_search_level = 1;

    sf->tx_sf.adaptive_txb_search_level = 2;
    sf->tx_sf.inter_tx_size_search_init_depth_rect = 1;
    sf->tx_sf.inter_tx_size_search_init_depth_sqr = 1;
    sf->tx_sf.intra_tx_size_search_init_depth_rect = 1;
    sf->tx_sf.model_based_prune_tx_search_level = 0;
    sf->tx_sf.tx_type_search.ml_tx_split_thresh = 4000;
    sf->tx_sf.tx_type_search.prune_2d_txfm_mode = TX_TYPE_PRUNE_2;
    sf->tx_sf.tx_type_search.skip_tx_search = 1;

    sf->rd_sf.perform_coeff_opt = boosted ? 2 : 3;
    sf->rd_sf.tx_domain_dist_level = boosted ? 1 : 2;
    sf->rd_sf.tx_domain_dist_thres_level = 1;

    sf->lpf_sf.dual_sgr_penalty_level = 1;
    sf->lpf_sf.enable_sgr_ep_pruning = 1;

    // TODO(any, yunqing): move this feature to speed 0.
    sf->tpl_sf.skip_alike_starting_mv = 1;
  }

  if (speed >= 2) {
    sf->hl_sf.recode_loop = ALLOW_RECODE_KFARFGF;

    sf->fp_sf.skip_motion_search_threshold = 25;

    sf->gm_sf.num_refinement_steps = 2;

    sf->part_sf.reuse_best_prediction_for_part_ab =
        !frame_is_intra_only(&cpi->common);

    sf->mv_sf.simple_motion_subpel_force_stop = QUARTER_PEL;
    sf->mv_sf.subpel_iters_per_step = 1;
    sf->mv_sf.reduce_search_range = 1;

    // TODO(chiyotsai@google.com): We can get 10% speed up if we move
    // adaptive_rd_thresh to speed 1. But currently it performs poorly on some
    // clips (e.g. 5% loss on dinner_1080p). We need to examine the sequence a
    // bit more closely to figure out why.
    sf->inter_sf.adaptive_rd_thresh = 1;
    sf->inter_sf.disable_interinter_wedge_var_thresh = 100;
    sf->inter_sf.fast_interintra_wedge_search = 1;
    sf->inter_sf.prune_comp_search_by_single_result = boosted ? 4 : 1;
    sf->inter_sf.prune_ext_comp_using_neighbors = 1;
    sf->inter_sf.prune_comp_using_best_single_mode_ref = 2;
    sf->inter_sf.prune_comp_type_by_comp_avg = 2;
    sf->inter_sf.selective_ref_frame = 3;
    sf->inter_sf.use_dist_wtd_comp_flag = DIST_WTD_COMP_DISABLED;
    sf->inter_sf.enable_fast_compound_mode_search = 1;
    sf->inter_sf.reuse_mask_search_results = 1;
    set_txfm_rd_gate_level(sf->inter_sf.txfm_rd_gate_level, boosted ? 0 : 1);
    sf->inter_sf.inter_mode_txfm_breakout = boosted ? 0 : 1;
    sf->inter_sf.alt_ref_search_fp = 1;

    sf->interp_sf.adaptive_interp_filter_search = 1;
    sf->interp_sf.disable_dual_filter = 1;

    sf->intra_sf.disable_smooth_intra =
        !frame_is_intra_only(&cpi->common) || (cpi->rc.frames_to_key > 1);
    sf->intra_sf.intra_pruning_with_hog = 2;
    sf->intra_sf.skip_intra_in_interframe = is_inter_frame ? 2 : 1;
    sf->intra_sf.skip_filter_intra_in_inter_frames = 1;

    sf->tpl_sf.prune_starting_mv = 1;
    sf->tpl_sf.search_method = DIAMOND;

    sf->rd_sf.perform_coeff_opt = is_boosted_arf2_bwd_type ? 3 : 4;
    sf->rd_sf.use_mb_rd_hash = 1;

    sf->lpf_sf.prune_wiener_based_on_src_var = 1;
    sf->lpf_sf.prune_sgr_based_on_wiener = 1;
    sf->lpf_sf.disable_loop_restoration_chroma = boosted ? 0 : 1;
    sf->lpf_sf.reduce_wiener_window_size = boosted ? 0 : 1;

    // TODO(any): Re-evaluate this feature set to 1 in speed 2.
    sf->tpl_sf.allow_compound_pred = 0;
    sf->tpl_sf.prune_ref_frames_in_tpl = 1;
  }

  if (speed >= 3) {
    sf->hl_sf.high_precision_mv_usage = CURRENT_Q;

    sf->gm_sf.prune_ref_frame_for_gm_search = 1;
    sf->gm_sf.prune_zero_mv_with_sse = 1;
    sf->gm_sf.num_refinement_steps = 0;

    sf->part_sf.less_rectangular_check_level = 2;
    sf->part_sf.simple_motion_search_prune_agg =
        allow_screen_content_tools
            ? SIMPLE_AGG_LVL0
            : (boosted ? SIMPLE_AGG_LVL1 : QIDX_BASED_AGG_LVL1);
    sf->part_sf.prune_ext_part_using_split_info = 1;
    sf->part_sf.simple_motion_search_rect_split = 1;

    sf->mv_sf.full_pixel_search_level = 1;
    sf->mv_sf.subpel_search_method = SUBPEL_TREE_PRUNED;
    sf->mv_sf.search_method = DIAMOND;
    sf->mv_sf.disable_second_mv = 2;
    sf->mv_sf.prune_mesh_search = PRUNE_MESH_SEARCH_LVL_1;
    sf->mv_sf.use_intrabc = 0;

    sf->inter_sf.disable_interinter_wedge_newmv_search = boosted ? 0 : 1;
    sf->inter_sf.mv_cost_upd_level = INTERNAL_COST_UPD_SBROW;
    sf->inter_sf.disable_onesided_comp = 1;
    sf->inter_sf.disable_interintra_wedge_var_thresh = UINT_MAX;
    // TODO(any): Experiment with the early exit mechanism for speeds 0, 1 and 2
    // and clean-up the speed feature
    sf->inter_sf.perform_best_rd_based_gating_for_chroma = 1;
    sf->inter_sf.prune_inter_modes_based_on_tpl = boosted ? 0 : 1;
    sf->inter_sf.prune_comp_search_by_single_result = boosted ? 4 : 2;
    sf->inter_sf.selective_ref_frame = 5;
    sf->inter_sf.reuse_compound_type_decision = 1;
    set_txfm_rd_gate_level(sf->inter_sf.txfm_rd_gate_level,
                           boosted ? 0 : (is_boosted_arf2_bwd_type ? 1 : 2));
    sf->inter_sf.inter_mode_txfm_breakout = boosted ? 0 : 2;

    sf->interp_sf.adaptive_interp_filter_search = 2;

    // TODO(chiyotsai@google.com): the thresholds chosen for intra hog are
    // inherited directly from luma hog with some minor tweaking. Eventually we
    // should run this with a bayesian optimizer to find the Pareto frontier.
    sf->intra_sf.chroma_intra_pruning_with_hog = 2;
    sf->intra_sf.intra_pruning_with_hog = 3;
    sf->intra_sf.prune_palette_search_level = 2;
    sf->intra_sf.top_intra_model_count_allowed = 2;

    sf->tpl_sf.prune_starting_mv = 2;
    sf->tpl_sf.skip_alike_starting_mv = 2;
    sf->tpl_sf.prune_intra_modes = 1;
    sf->tpl_sf.reduce_first_step_size = 6;
    sf->tpl_sf.subpel_force_stop = QUARTER_PEL;
    sf->tpl_sf.gop_length_decision_method = 1;

    sf->tx_sf.adaptive_txb_search_level = boosted ? 2 : 3;
    sf->tx_sf.tx_type_search.use_skip_flag_prediction = 2;
    sf->tx_sf.tx_type_search.prune_2d_txfm_mode = TX_TYPE_PRUNE_3;

    // TODO(any): Refactor the code related to following winner mode speed
    // features
    sf->winner_mode_sf.enable_winner_mode_for_coeff_opt = 1;
    sf->winner_mode_sf.enable_winner_mode_for_use_tx_domain_dist = 1;
    sf->winner_mode_sf.motion_mode_for_winner_cand =
        boosted                                                          ? 0
        : gf_group->update_type[cpi->gf_frame_index] == INTNL_ARF_UPDATE ? 1
                                                                         : 2;
    sf->winner_mode_sf.prune_winner_mode_eval_level = boosted ? 0 : 4;

    // For screen content, "prune_sgr_based_on_wiener = 2" cause large quality
    // loss.
    sf->lpf_sf.prune_sgr_based_on_wiener = allow_screen_content_tools ? 1 : 2;
    sf->lpf_sf.prune_wiener_based_on_src_var = 2;
    sf->lpf_sf.use_coarse_filter_level_search =
        frame_is_intra_only(&cpi->common) ? 0 : 1;
    sf->lpf_sf.use_downsampled_wiener_stats = 1;
  }

  if (speed >= 4) {
    sf->mv_sf.subpel_search_method = SUBPEL_TREE_PRUNED_MORE;

    sf->gm_sf.prune_zero_mv_with_sse = 2;
    sf->gm_sf.downsample_level = 1;

    sf->part_sf.simple_motion_search_prune_agg =
        allow_screen_content_tools ? SIMPLE_AGG_LVL0 : SIMPLE_AGG_LVL2;
    sf->part_sf.simple_motion_search_reduce_search_steps = 4;
    sf->part_sf.prune_ext_part_using_split_info = 2;
    sf->part_sf.ml_predict_breakout_level = 3;
    sf->part_sf.prune_rectangular_split_based_on_qidx =
        (allow_screen_content_tools || frame_is_intra_only(&cpi->common)) ? 0
                                                                          : 1;

    sf->inter_sf.alt_ref_search_fp = 2;
    sf->inter_sf.txfm_rd_gate_level[TX_SEARCH_DEFAULT] = boosted ? 0 : 3;
    sf->inter_sf.txfm_rd_gate_level[TX_SEARCH_MOTION_MODE] = boosted ? 0 : 5;
    sf->inter_sf.txfm_rd_gate_level[TX_SEARCH_COMP_TYPE_MODE] = boosted ? 0 : 3;

    sf->inter_sf.prune_inter_modes_based_on_tpl = boosted ? 0 : 2;
    sf->inter_sf.prune_ext_comp_using_neighbors = 2;
    sf->inter_sf.prune_obmc_prob_thresh = INT_MAX;
    sf->inter_sf.disable_interinter_wedge_var_thresh = UINT_MAX;

    sf->interp_sf.cb_pred_filter_search = 1;
    sf->interp_sf.skip_sharp_interp_filter_search = 1;
    sf->interp_sf.use_interp_filter = 2;

    sf->intra_sf.intra_uv_mode_mask[TX_16X16] = UV_INTRA_DC_H_V_CFL;
    sf->intra_sf.intra_uv_mode_mask[TX_32X32] = UV_INTRA_DC_H_V_CFL;
    sf->intra_sf.intra_uv_mode_mask[TX_64X64] = UV_INTRA_DC_H_V_CFL;
    // TODO(any): "intra_y_mode_mask" doesn't help much at speed 4.
    // sf->intra_sf.intra_y_mode_mask[TX_16X16] = INTRA_DC_H_V;
    // sf->intra_sf.intra_y_mode_mask[TX_32X32] = INTRA_DC_H_V;
    // sf->intra_sf.intra_y_mode_mask[TX_64X64] = INTRA_DC_H_V;
    sf->intra_sf.skip_intra_in_interframe = 4;

    sf->mv_sf.simple_motion_subpel_force_stop = HALF_PEL;
    sf->mv_sf.prune_mesh_search = PRUNE_MESH_SEARCH_LVL_2;

    sf->tpl_sf.subpel_force_stop = HALF_PEL;
    sf->tpl_sf.search_method = FAST_BIGDIA;
    sf->tpl_sf.use_sad_for_mode_decision = 1;

    sf->tx_sf.tx_type_search.fast_intra_tx_type_search = 1;

    sf->rd_sf.perform_coeff_opt = is_boosted_arf2_bwd_type ? 5 : 7;

    // TODO(any): Extend multi-winner mode processing support for inter frames
    sf->winner_mode_sf.multi_winner_mode_type =
        frame_is_intra_only(&cpi->common) ? MULTI_WINNER_MODE_DEFAULT
                                          : MULTI_WINNER_MODE_OFF;
    sf->winner_mode_sf.dc_blk_pred_level = boosted ? 0 : 2;

    sf->lpf_sf.lpf_pick = LPF_PICK_FROM_FULL_IMAGE_NON_DUAL;
  }

  if (speed >= 5) {
    sf->hl_sf.weight_calc_level_in_tf = 1;
    sf->hl_sf.adjust_num_frames_for_arf_filtering =
        allow_screen_content_tools ? 0 : 2;

    sf->fp_sf.reduce_mv_step_param = 4;

    sf->part_sf.simple_motion_search_prune_agg =
        allow_screen_content_tools ? SIMPLE_AGG_LVL0 : SIMPLE_AGG_LVL3;
    sf->part_sf.ext_partition_eval_thresh =
        allow_screen_content_tools ? BLOCK_8X8 : BLOCK_16X16;
    sf->part_sf.prune_sub_8x8_partition_level =
        allow_screen_content_tools ? 1 : 2;

    sf->mv_sf.warp_search_method = WARP_SEARCH_DIAMOND;

    sf->inter_sf.prune_inter_modes_if_skippable = 1;
    sf->inter_sf.prune_single_ref = is_boosted_arf2_bwd_type ? 0 : 1;
    sf->inter_sf.txfm_rd_gate_level[TX_SEARCH_DEFAULT] = boosted ? 0 : 4;
    sf->inter_sf.txfm_rd_gate_level[TX_SEARCH_COMP_TYPE_MODE] = boosted ? 0 : 5;
    sf->inter_sf.enable_fast_compound_mode_search = 2;

    sf->interp_sf.skip_interp_filter_search = boosted ? 0 : 1;

    sf->intra_sf.chroma_intra_pruning_with_hog = 3;

    // TODO(any): Extend multi-winner mode processing support for inter frames
    sf->winner_mode_sf.multi_winner_mode_type =
        frame_is_intra_only(&cpi->common) ? MULTI_WINNER_MODE_FAST
                                          : MULTI_WINNER_MODE_OFF;

    // Disable Self-guided Loop restoration filter.
    sf->lpf_sf.disable_sgr_filter = true;
    sf->lpf_sf.disable_wiener_coeff_refine_search = true;

    sf->tpl_sf.prune_starting_mv = 3;
    sf->tpl_sf.use_y_only_rate_distortion = 1;
    sf->tpl_sf.subpel_force_stop = FULL_PEL;
    sf->tpl_sf.gop_length_decision_method = 2;
    sf->tpl_sf.use_sad_for_mode_decision = 2;

    sf->winner_mode_sf.dc_blk_pred_level = 2;

    sf->fp_sf.disable_recon = 1;
  }

  if (speed >= 6) {
    sf->hl_sf.disable_extra_sc_testing = 1;
    sf->hl_sf.second_alt_ref_filtering = 0;

    sf->gm_sf.downsample_level = 2;

    sf->inter_sf.prune_inter_modes_based_on_tpl = boosted ? 0 : 3;
    sf->inter_sf.selective_ref_frame = 6;
    sf->inter_sf.prune_single_ref = is_boosted_arf2_bwd_type ? 0 : 2;
    sf->inter_sf.prune_ext_comp_using_neighbors = 3;

    sf->intra_sf.chroma_intra_pruning_with_hog = 4;
    sf->intra_sf.intra_pruning_with_hog = 4;
    sf->intra_sf.intra_uv_mode_mask[TX_32X32] = UV_INTRA_DC;
    sf->intra_sf.intra_uv_mode_mask[TX_64X64] = UV_INTRA_DC;
    sf->intra_sf.intra_y_mode_mask[TX_32X32] = INTRA_DC;
    sf->intra_sf.intra_y_mode_mask[TX_64X64] = INTRA_DC;
    sf->intra_sf.early_term_chroma_palette_size_search = 1;

    sf->part_sf.prune_rectangular_split_based_on_qidx =
        boosted || allow_screen_content_tools ? 0 : 2;

    sf->part_sf.prune_part4_search = 3;

    sf->mv_sf.simple_motion_subpel_force_stop = FULL_PEL;
    sf->mv_sf.use_bsize_dependent_search_method = 1;

    sf->tpl_sf.gop_length_decision_method = 3;

    sf->rd_sf.perform_coeff_opt = is_boosted_arf2_bwd_type ? 6 : 8;

    sf->winner_mode_sf.dc_blk_pred_level = 3;
    sf->winner_mode_sf.multi_winner_mode_type = MULTI_WINNER_MODE_OFF;

    sf->fp_sf.skip_zeromv_motion_search = 1;
  }

  if (cpi->oxcf.enable_low_complexity_decode)
    set_good_speed_features_lc_dec_framesize_independent(cpi, sf);
}

static void set_rt_speed_feature_framesize_dependent(const AV1_COMP *const cpi,
                                                     SPEED_FEATURES *const sf,
                                                     int speed) {
  const AV1_COMMON *const cm = &cpi->common;
  const int boosted = frame_is_boosted(cpi);
  const int is_1080p_or_larger = AOMMIN(cm->width, cm->height) >= 1080;
  const int is_720p_or_larger = AOMMIN(cm->width, cm->height) >= 720;
  const int is_480p_or_larger = AOMMIN(cm->width, cm->height) >= 480;
  const int is_360p_or_larger = AOMMIN(cm->width, cm->height) >= 360;

  if (!is_360p_or_larger) {
    sf->rt_sf.prune_intra_mode_based_on_mv_range = 1;
    sf->rt_sf.prune_inter_modes_wrt_gf_arf_based_on_sad = 1;
    if (speed >= 6)
      sf->winner_mode_sf.prune_winner_mode_eval_level = boosted ? 0 : 2;
    if (speed == 7) sf->rt_sf.prefer_large_partition_blocks = 2;
    if (speed >= 7) {
      sf->lpf_sf.cdef_pick_method = CDEF_PICK_FROM_Q;
      sf->rt_sf.check_only_zero_zeromv_on_large_blocks = true;
      sf->rt_sf.use_rtc_tf = 2;
    }
    if (speed == 8) sf->rt_sf.prefer_large_partition_blocks = 1;
    if (speed >= 8) {
      sf->rt_sf.use_nonrd_filter_search = 1;
      sf->rt_sf.tx_size_level_based_on_qstep = 1;
    }
    if (speed >= 9) {
      sf->rt_sf.use_comp_ref_nonrd = 0;
      sf->rt_sf.nonrd_aggressive_skip = 1;
      sf->rt_sf.skip_intra_pred = 1;
      // Only turn on enable_ref_short_signaling for low resolution when only
      // LAST and GOLDEN ref frames are used.
      sf->rt_sf.enable_ref_short_signaling =
          (!sf->rt_sf.use_nonrd_altref_frame &&
           (!sf->rt_sf.use_comp_ref_nonrd ||
            (!sf->rt_sf.ref_frame_comp_nonrd[1] &&
             !sf->rt_sf.ref_frame_comp_nonrd[2])));

// TODO(kyslov) Re-enable when AV1 models are trained
#if 0
#if CONFIG_RT_ML_PARTITIONING
      if (!frame_is_intra_only(cm)) {
        sf->part_sf.partition_search_type = ML_BASED_PARTITION;
        sf->rt_sf.reuse_inter_pred_nonrd = 0;
      }
#endif
#endif
      sf->rt_sf.use_adaptive_subpel_search = false;
    }
    if (speed >= 10) {
      // TODO(yunqingwang@google.com): To be conservative, disable
      // sf->rt_sf.estimate_motion_for_var_based_partition = 3 for speed 10/qvga
      // for now. May enable it in the future.
      sf->rt_sf.estimate_motion_for_var_based_partition = 0;
      sf->rt_sf.skip_intra_pred = 2;
      sf->rt_sf.hybrid_intra_pickmode = 3;
      sf->rt_sf.reduce_mv_pel_precision_lowcomplex = 1;
      sf->rt_sf.reduce_mv_pel_precision_highmotion = 2;
      sf->rt_sf.use_nonrd_filter_search = 0;
    }
  } else {
    sf->rt_sf.prune_intra_mode_based_on_mv_range = 2;
    sf->intra_sf.skip_filter_intra_in_inter_frames = 1;
    if (speed <= 5) {
      sf->tx_sf.tx_type_search.fast_inter_tx_type_prob_thresh =
          boosted ? INT_MAX : 350;
      sf->winner_mode_sf.prune_winner_mode_eval_level = boosted ? 0 : 2;
    }
    if (speed == 6) sf->part_sf.disable_8x8_part_based_on_qidx = 1;
    if (speed >= 6) sf->rt_sf.skip_newmv_mode_based_on_sse = 2;
    if (speed == 7) {
      sf->rt_sf.prefer_large_partition_blocks = 1;
      // Enable this feature for [360p, 720p] resolution range initially.
      // Only enable for low bitdepth to mitigate issue: b/303023614.
      if (!cpi->rc.rtc_external_ratectrl &&
          AOMMIN(cm->width, cm->height) <= 720 && !cpi->oxcf.use_highbitdepth)
        sf->hl_sf.accurate_bit_estimate = cpi->oxcf.q_cfg.aq_mode == NO_AQ;
    }
    if (speed >= 7) {
      sf->rt_sf.use_rtc_tf = 1;
    }
    if (speed == 8 && !cpi->ppi->use_svc) {
      sf->rt_sf.short_circuit_low_temp_var = 0;
      sf->rt_sf.use_nonrd_altref_frame = 1;
    }
    if (speed >= 8) sf->rt_sf.tx_size_level_based_on_qstep = 2;
    if (speed >= 9) {
      sf->rt_sf.gf_length_lvl = 1;
      sf->rt_sf.skip_cdef_sb = 1;
      sf->rt_sf.sad_based_adp_altref_lag = 2;
      sf->rt_sf.reduce_mv_pel_precision_highmotion = 2;
      sf->rt_sf.use_adaptive_subpel_search = true;
      sf->interp_sf.cb_pred_filter_search = 1;
    }
    if (speed >= 10) {
      sf->rt_sf.hybrid_intra_pickmode = 2;
      sf->rt_sf.sad_based_adp_altref_lag = 4;
      sf->rt_sf.tx_size_level_based_on_qstep = 0;
      sf->rt_sf.reduce_mv_pel_precision_highmotion = 3;
      sf->rt_sf.use_adaptive_subpel_search = false;
      sf->interp_sf.cb_pred_filter_search = 2;
    }
  }
  if (!is_480p_or_larger) {
    if (speed == 7) {
      sf->rt_sf.nonrd_check_partition_merge_mode = 2;
    }
  }
  if (!is_720p_or_larger) {
    if (speed >= 9) {
      sf->rt_sf.force_large_partition_blocks_intra = 1;
    }
  } else {
    if (speed >= 6) sf->rt_sf.skip_newmv_mode_based_on_sse = 3;
    if (speed == 7) sf->rt_sf.prefer_large_partition_blocks = 0;
    if (speed >= 7) {
      sf->rt_sf.reduce_mv_pel_precision_lowcomplex = 2;
      sf->rt_sf.reduce_mv_pel_precision_highmotion = 1;
    }
    if (speed >= 9) {
      sf->rt_sf.sad_based_adp_altref_lag = 1;
      sf->rt_sf.reduce_mv_pel_precision_lowcomplex = 0;
      sf->rt_sf.reduce_mv_pel_precision_highmotion = 2;
    }
    if (speed >= 10) {
      sf->rt_sf.sad_based_adp_altref_lag = 3;
      sf->rt_sf.reduce_mv_pel_precision_highmotion = 3;
    }
  }
  // TODO(Any): Check/Tune settings of other sfs for 1080p.
  if (is_1080p_or_larger) {
    if (speed >= 7) {
      sf->rt_sf.reduce_mv_pel_precision_highmotion = 0;
      sf->rt_sf.use_adaptive_subpel_search = 0;
    }
    if (speed >= 9) sf->interp_sf.cb_pred_filter_search = 0;
  } else {
    if (speed >= 9) sf->lpf_sf.cdef_pick_method = CDEF_PICK_FROM_Q;
    if (speed >= 10) sf->rt_sf.nonrd_aggressive_skip = 1;
  }
  // TODO(marpan): Tune settings for speed 11 video mode,
  if (speed >= 11 && cpi->oxcf.tune_cfg.content != AOM_CONTENT_SCREEN) {
    sf->rt_sf.skip_cdef_sb = 1;
    sf->rt_sf.force_only_last_ref = 1;
    sf->rt_sf.selective_cdf_update = 1;
    sf->rt_sf.use_nonrd_filter_search = 0;
    if (is_360p_or_larger) {
      sf->part_sf.fixed_partition_size = BLOCK_32X32;
      sf->rt_sf.use_fast_fixed_part = 1;
      sf->rt_sf.reduce_mv_pel_precision_lowcomplex = 2;
    }
    sf->rt_sf.increase_source_sad_thresh = 1;
    sf->rt_sf.part_early_exit_zeromv = 2;
    sf->rt_sf.set_zeromv_skip_based_on_source_sad = 2;
    for (int i = 0; i < BLOCK_SIZES; ++i) {
      sf->rt_sf.intra_y_mode_bsize_mask_nrd[i] = INTRA_DC;
    }
    sf->rt_sf.hybrid_intra_pickmode = 0;
  }
  // Setting for SVC, or when the ref_frame_config control is
  // used to set the reference structure.
  if (cpi->ppi->use_svc || cpi->ppi->rtc_ref.set_ref_frame_config) {
    const RTC_REF *const rtc_ref = &cpi->ppi->rtc_ref;
    // For SVC: for greater than 2 temporal layers, use better mv search on
    // base temporal layers, and only on base spatial layer if highest
    // resolution is above 640x360.
    if (cpi->svc.number_temporal_layers >= 2 &&
        cpi->svc.temporal_layer_id == 0 &&
        (cpi->svc.spatial_layer_id == 0 ||
         cpi->oxcf.frm_dim_cfg.width * cpi->oxcf.frm_dim_cfg.height <=
             640 * 360)) {
      sf->mv_sf.search_method = NSTEP;
      sf->mv_sf.subpel_search_method = SUBPEL_TREE_PRUNED;
      sf->rt_sf.fullpel_search_step_param = 10;
      sf->rt_sf.reduce_mv_pel_precision_highmotion = 0;
      if (cm->width * cm->height <= 352 * 288)
        sf->rt_sf.nonrd_prune_ref_frame_search = 2;
      sf->rt_sf.force_large_partition_blocks_intra = 0;
    }
    if (speed >= 8) {
      if (cpi->svc.number_temporal_layers > 2)
        sf->rt_sf.disable_cdf_update_non_reference_frame = true;
      sf->rt_sf.reduce_mv_pel_precision_highmotion = 3;
      if (rtc_ref->non_reference_frame) {
        sf->rt_sf.nonrd_aggressive_skip = 1;
        sf->mv_sf.subpel_search_method = SUBPEL_TREE_PRUNED_MORE;
      }
    }
    if (speed <= 9 && cpi->svc.number_temporal_layers > 2 &&
        cpi->svc.temporal_layer_id == 0)
      sf->rt_sf.check_only_zero_zeromv_on_large_blocks = false;
    else
      sf->rt_sf.check_only_zero_zeromv_on_large_blocks = true;
    sf->rt_sf.frame_level_mode_cost_update = false;

    // Compound mode enabling.
    if (rtc_ref->ref_frame_comp[0] || rtc_ref->ref_frame_comp[1] ||
        rtc_ref->ref_frame_comp[2]) {
      sf->rt_sf.use_comp_ref_nonrd = 1;
      sf->rt_sf.ref_frame_comp_nonrd[0] =
          rtc_ref->ref_frame_comp[0] && rtc_ref->reference[GOLDEN_FRAME - 1];
      sf->rt_sf.ref_frame_comp_nonrd[1] =
          rtc_ref->ref_frame_comp[1] && rtc_ref->reference[LAST2_FRAME - 1];
      sf->rt_sf.ref_frame_comp_nonrd[2] =
          rtc_ref->ref_frame_comp[2] && rtc_ref->reference[ALTREF_FRAME - 1];
    } else {
      sf->rt_sf.use_comp_ref_nonrd = 0;
    }

    if (cpi->svc.number_spatial_layers > 1 ||
        cpi->svc.number_temporal_layers > 1)
      sf->hl_sf.accurate_bit_estimate = 0;

    sf->rt_sf.estimate_motion_for_var_based_partition = 1;

    // For single layers RPS: bias/adjustment for recovery frame.
    if (cpi->ppi->rtc_ref.bias_recovery_frame) {
      sf->mv_sf.search_method = NSTEP;
      sf->mv_sf.subpel_search_method = SUBPEL_TREE;
      sf->rt_sf.fullpel_search_step_param = 8;
      sf->rt_sf.nonrd_aggressive_skip = 0;
    }
  }
  // Screen settings.
  if (cpi->oxcf.tune_cfg.content == AOM_CONTENT_SCREEN) {
    // TODO(marpan): Check settings for speed 7 and 8.
    if (speed >= 7) {
      sf->rt_sf.reduce_mv_pel_precision_highmotion = 1;
      sf->mv_sf.use_bsize_dependent_search_method = 0;
      sf->rt_sf.skip_cdef_sb = 1;
      sf->rt_sf.increase_color_thresh_palette = 1;
      if (!frame_is_intra_only(cm)) sf->rt_sf.dct_only_palette_nonrd = 1;
    }
    if (speed >= 8) {
      sf->rt_sf.nonrd_check_partition_merge_mode = 3;
      sf->rt_sf.nonrd_prune_ref_frame_search = 1;
      sf->rt_sf.use_nonrd_filter_search = 0;
      sf->rt_sf.prune_hv_pred_modes_using_src_sad = false;
    }
    if (speed >= 9) {
      sf->rt_sf.prune_idtx_nonrd = 1;
      sf->rt_sf.part_early_exit_zeromv = 2;
      sf->rt_sf.skip_lf_screen = 1;
      sf->rt_sf.nonrd_prune_ref_frame_search = 3;
      sf->rt_sf.var_part_split_threshold_shift = 10;
      sf->mv_sf.subpel_search_method = SUBPEL_TREE_PRUNED_MORE;
      sf->rt_sf.reduce_mv_pel_precision_highmotion = 3;
      sf->rt_sf.reduce_mv_pel_precision_lowcomplex = 1;
      sf->lpf_sf.cdef_pick_method = CDEF_PICK_FROM_Q;
      sf->rt_sf.nonrd_check_partition_merge_mode = 0;
      sf->interp_sf.cb_pred_filter_search = 0;
    }
    if (speed >= 10) {
      if (cm->width * cm->height > 1920 * 1080)
        sf->part_sf.disable_8x8_part_based_on_qidx = 1;
      sf->rt_sf.screen_content_cdef_filter_qindex_thresh = 80;
      sf->rt_sf.part_early_exit_zeromv = 1;
      sf->rt_sf.nonrd_aggressive_skip = 1;
      sf->rt_sf.thresh_active_maps_skip_lf_cdef = 90;
      sf->rt_sf.hybrid_intra_pickmode = 0;
      sf->rt_sf.dct_only_palette_nonrd = 1;
      sf->rt_sf.prune_palette_search_nonrd = 1;
      sf->rt_sf.prune_intra_mode_using_best_sad_so_far = true;
      sf->rt_sf.rc_faster_convergence_static = 1;
      sf->rt_sf.rc_compute_spatial_var_sc = 1;
    }
    if (speed >= 11) {
      sf->rt_sf.skip_lf_screen = 2;
      sf->rt_sf.skip_cdef_sb = 2;
      sf->rt_sf.prune_palette_search_nonrd = 2;
      sf->rt_sf.increase_color_thresh_palette = 0;
      sf->rt_sf.prune_h_pred_using_best_mode_so_far = true;
      sf->rt_sf.enable_intra_mode_pruning_using_neighbors = true;
    }
    if (speed >= 12) {
      if (cpi->rc.high_source_sad && cpi->rc.frame_source_sad > 40000 &&
          cpi->rc.prev_avg_source_sad < 1000 &&
          cpi->oxcf.frm_dim_cfg.width * cpi->oxcf.frm_dim_cfg.height >=
              1280 * 720) {
        sf->rt_sf.prune_palette_search_nonrd = 3;
        sf->rt_sf.skip_newmv_mode_sad_screen = 1;
      }
    }
    sf->rt_sf.skip_encoding_non_reference_slide_change =
        cpi->oxcf.rc_cfg.drop_frames_water_mark > 0 ? 1 : 0;
    sf->rt_sf.skip_newmv_flat_blocks_screen = 1;
    sf->rt_sf.use_idtx_nonrd = 1;
    sf->rt_sf.higher_thresh_scene_detection = 0;
    sf->rt_sf.use_nonrd_altref_frame = 0;
    sf->rt_sf.use_rtc_tf = 0;
    sf->rt_sf.use_comp_ref_nonrd = 0;
    sf->rt_sf.source_metrics_sb_nonrd = 1;
    if (cpi->rc.high_source_sad == 1) {
      sf->rt_sf.prefer_large_partition_blocks = 0;
      sf->part_sf.max_intra_bsize = BLOCK_128X128;
      for (int i = 0; i < BLOCK_SIZES; ++i) {
        if (i > BLOCK_32X32)
          sf->rt_sf.intra_y_mode_bsize_mask_nrd[i] = INTRA_DC;
        else
          sf->rt_sf.intra_y_mode_bsize_mask_nrd[i] = INTRA_DC_H_V;
      }
    }
    if (speed >= 11 && cpi->rc.high_motion_content_screen_rtc) {
      sf->rt_sf.higher_thresh_scene_detection = 1;
      sf->rt_sf.force_only_last_ref = 1;
      sf->rt_sf.use_nonrd_filter_search = 0;
      sf->part_sf.fixed_partition_size = BLOCK_32X32;
      sf->rt_sf.use_fast_fixed_part = 1;
      sf->rt_sf.increase_source_sad_thresh = 1;
      sf->rt_sf.selective_cdf_update = 1;
      sf->mv_sf.search_method = FAST_DIAMOND;
    } else if (cpi->rc.max_block_source_sad > 20000 &&
               cpi->rc.frame_source_sad > 100 && speed >= 6 &&
               (cpi->rc.percent_blocks_with_motion > 1 ||
                cpi->svc.last_layer_dropped[0])) {
      sf->mv_sf.search_method = NSTEP;
      sf->rt_sf.fullpel_search_step_param = 2;
    }
    if (cpi->rc.high_source_sad && cpi->ppi->rtc_ref.non_reference_frame) {
      sf->rt_sf.use_idtx_nonrd = 0;
      sf->rt_sf.prefer_large_partition_blocks = 1;
      sf->mv_sf.subpel_search_method = SUBPEL_TREE_PRUNED_MORE;
      sf->rt_sf.fullpel_search_step_param = 10;
    }
    sf->rt_sf.partition_direct_merging = 0;
    sf->hl_sf.accurate_bit_estimate = 0;
    // This feature is for nonrd_pickmode.
    if (sf->rt_sf.use_nonrd_pick_mode)
      sf->rt_sf.estimate_motion_for_var_based_partition = 1;
    else
      sf->rt_sf.estimate_motion_for_var_based_partition = 0;
  }
  if (is_lossless_requested(&cpi->oxcf.rc_cfg)) {
    sf->rt_sf.use_rtc_tf = 0;
    // TODO(aomedia:3412): The setting accurate_bit_estimate = 0
    // can be removed once it's fixed for lossless mode.
    sf->hl_sf.accurate_bit_estimate = 0;
  }
  if (cpi->oxcf.use_highbitdepth) {
    // Disable for use_highbitdepth = 1 to mitigate issue: b/303023614.
    sf->rt_sf.estimate_motion_for_var_based_partition = 0;
  }
  if (cpi->oxcf.superres_cfg.enable_superres) {
    sf->rt_sf.use_rtc_tf = 0;
    sf->rt_sf.nonrd_prune_ref_frame_search = 1;
  }
  // rtc_tf feature allocates new source because of possible
  // temporal filtering which may change the input source during encoding:
  // this causes an issue on resized frames when psnr is calculated,
  // so disable it here for frames that are resized (encoding width/height
  // different from configured width/height).
  if (is_psnr_calc_enabled(cpi) && (cpi->oxcf.frm_dim_cfg.width != cm->width ||
                                    cpi->oxcf.frm_dim_cfg.height != cm->height))
    sf->rt_sf.use_rtc_tf = 0;
}

static void set_rt_speed_features_framesize_independent(AV1_COMP *cpi,
                                                        SPEED_FEATURES *sf,
                                                        int speed) {
  AV1_COMMON *const cm = &cpi->common;
  const int boosted = frame_is_boosted(cpi);

  // Currently, rt speed 0, 1, 2, 3, 4, 5 are the same.
  // Following set of speed features are not impacting encoder's decisions as
  // the relevant tools are disabled by default.
  sf->gm_sf.gm_search_type = GM_DISABLE_SEARCH;
  sf->hl_sf.recode_loop = ALLOW_RECODE_KFARFGF;
  sf->inter_sf.reuse_inter_intra_mode = 1;
  sf->inter_sf.prune_compound_using_single_ref = 0;
  sf->inter_sf.prune_comp_search_by_single_result = 2;
  sf->inter_sf.prune_comp_type_by_comp_avg = 2;
  sf->inter_sf.fast_wedge_sign_estimate = 1;
  sf->inter_sf.use_dist_wtd_comp_flag = DIST_WTD_COMP_DISABLED;
  sf->inter_sf.mv_cost_upd_level = INTERNAL_COST_UPD_SBROW;
  sf->inter_sf.disable_interinter_wedge_var_thresh = 100;
  sf->interp_sf.cb_pred_filter_search = 0;
  sf->interp_sf.skip_interp_filter_search = 1;
  sf->part_sf.ml_prune_partition = 1;
  sf->part_sf.reuse_prev_rd_results_for_part_ab = 1;
  sf->part_sf.prune_ext_partition_types_search_level = 2;
  sf->part_sf.less_rectangular_check_level = 2;
  sf->mv_sf.obmc_full_pixel_search_level = 1;
  sf->intra_sf.dv_cost_upd_level = INTERNAL_COST_UPD_OFF;
  sf->tx_sf.model_based_prune_tx_search_level = 0;
  sf->lpf_sf.dual_sgr_penalty_level = 1;
  // Disable Wiener and Self-guided Loop restoration filters.
  sf->lpf_sf.disable_wiener_filter = true;
  sf->lpf_sf.disable_sgr_filter = true;
  sf->intra_sf.prune_palette_search_level = 2;
  sf->intra_sf.prune_luma_palette_size_search_level = 2;
  sf->intra_sf.early_term_chroma_palette_size_search = 1;

  // End of set

  // TODO(any, yunqing): tune these features for real-time use cases.
  sf->hl_sf.superres_auto_search_type = SUPERRES_AUTO_SOLO;
  sf->hl_sf.frame_parameter_update = 0;

  sf->inter_sf.model_based_post_interp_filter_breakout = 1;
  // TODO(any): As per the experiments, this speed feature is doing redundant
  // computation since the model rd based pruning logic is similar to model rd
  // based gating when inter_mode_rd_model_estimation = 2. Enable this SF if
  // either of the condition becomes true.
  //    (1) inter_mode_rd_model_estimation != 2
  //    (2) skip_interp_filter_search == 0
  //    (3) Motion mode or compound mode is enabled */
  sf->inter_sf.prune_mode_search_simple_translation = 0;
  sf->inter_sf.prune_ref_frame_for_rect_partitions = !boosted;
  sf->inter_sf.disable_interintra_wedge_var_thresh = UINT_MAX;
  sf->inter_sf.selective_ref_frame = 4;
  sf->inter_sf.alt_ref_search_fp = 2;
  set_txfm_rd_gate_level(sf->inter_sf.txfm_rd_gate_level, boosted ? 0 : 4);
  sf->inter_sf.limit_txfm_eval_per_mode = 3;

  sf->inter_sf.adaptive_rd_thresh = 4;
  sf->inter_sf.inter_mode_rd_model_estimation = 2;
  sf->inter_sf.prune_inter_modes_if_skippable = 1;
  sf->inter_sf.prune_nearmv_using_neighbors = PRUNE_NEARMV_LEVEL3;
  sf->inter_sf.reduce_inter_modes = boosted ? 1 : 3;
  sf->inter_sf.skip_newmv_in_drl = 4;

  sf->interp_sf.use_fast_interpolation_filter_search = 1;
  sf->interp_sf.use_interp_filter = 1;
  sf->interp_sf.adaptive_interp_filter_search = 1;
  sf->interp_sf.disable_dual_filter = 1;

  sf->part_sf.default_max_partition_size = BLOCK_128X128;
  sf->part_sf.default_min_partition_size = BLOCK_8X8;
  sf->part_sf.use_best_rd_for_pruning = 1;
  sf->part_sf.early_term_after_none_split = 1;
  sf->part_sf.partition_search_breakout_dist_thr = (1 << 25);
  sf->part_sf.max_intra_bsize = BLOCK_16X16;
  sf->part_sf.partition_search_breakout_rate_thr = 500;
  sf->part_sf.partition_search_type = VAR_BASED_PARTITION;
  sf->part_sf.adjust_var_based_rd_partitioning = 2;

  sf->mv_sf.full_pixel_search_level = 1;
  sf->mv_sf.exhaustive_searches_thresh = INT_MAX;
  sf->mv_sf.auto_mv_step_size = 1;
  sf->mv_sf.subpel_iters_per_step = 1;
  sf->mv_sf.use_accurate_subpel_search = USE_2_TAPS;
  sf->mv_sf.search_method = FAST_DIAMOND;
  sf->mv_sf.subpel_force_stop = EIGHTH_PEL;
  sf->mv_sf.subpel_search_method = SUBPEL_TREE_PRUNED;

  for (int i = 0; i < TX_SIZES; ++i) {
    sf->intra_sf.intra_y_mode_mask[i] = INTRA_DC;
    sf->intra_sf.intra_uv_mode_mask[i] = UV_INTRA_DC_CFL;
  }
  sf->intra_sf.skip_intra_in_interframe = 5;
  sf->intra_sf.disable_smooth_intra = 1;
  sf->intra_sf.skip_filter_intra_in_inter_frames = 1;

  sf->tx_sf.intra_tx_size_search_init_depth_sqr = 1;
  sf->tx_sf.tx_type_search.use_reduced_intra_txset = 1;
  sf->tx_sf.adaptive_txb_search_level = 2;
  sf->tx_sf.intra_tx_size_search_init_depth_rect = 1;
  sf->tx_sf.tx_size_search_lgr_block = 1;
  sf->tx_sf.tx_type_search.ml_tx_split_thresh = 4000;
  sf->tx_sf.tx_type_search.skip_tx_search = 1;
  sf->tx_sf.inter_tx_size_search_init_depth_rect = 1;
  sf->tx_sf.inter_tx_size_search_init_depth_sqr = 1;
  sf->tx_sf.tx_type_search.prune_2d_txfm_mode = TX_TYPE_PRUNE_3;
  sf->tx_sf.refine_fast_tx_search_results = 0;
  sf->tx_sf.tx_type_search.fast_intra_tx_type_search = 1;
  sf->tx_sf.tx_type_search.use_skip_flag_prediction = 2;
  sf->tx_sf.tx_type_search.winner_mode_tx_type_pruning = 4;

  sf->rd_sf.optimize_coefficients = NO_TRELLIS_OPT;
  sf->rd_sf.simple_model_rd_from_var = 1;
  sf->rd_sf.tx_domain_dist_level = 2;
  sf->rd_sf.tx_domain_dist_thres_level = 2;

  sf->lpf_sf.cdef_pick_method = CDEF_FAST_SEARCH_LVL4;
  sf->lpf_sf.lpf_pick = LPF_PICK_FROM_Q;

  sf->winner_mode_sf.dc_blk_pred_level = frame_is_intra_only(cm) ? 0 : 3;
  sf->winner_mode_sf.enable_winner_mode_for_tx_size_srch = 1;
  sf->winner_mode_sf.tx_size_search_level = 1;
  sf->winner_mode_sf.winner_mode_ifs = 1;

  sf->rt_sf.check_intra_pred_nonrd = 1;
  sf->rt_sf.estimate_motion_for_var_based_partition = 2;
  sf->rt_sf.hybrid_intra_pickmode = 1;
  sf->rt_sf.use_comp_ref_nonrd = 0;
  sf->rt_sf.ref_frame_comp_nonrd[0] = 0;
  sf->rt_sf.ref_frame_comp_nonrd[1] = 0;
  sf->rt_sf.ref_frame_comp_nonrd[2] = 0;
  sf->rt_sf.use_nonrd_filter_search = 1;
  sf->rt_sf.mode_search_skip_flags |= FLAG_SKIP_INTRA_DIRMISMATCH;
  sf->rt_sf.num_inter_modes_for_tx_search = 5;
  sf->rt_sf.prune_inter_modes_using_temp_var = 1;
  sf->rt_sf.use_real_time_ref_set = 1;
  sf->rt_sf.use_simple_rd_model = 1;
  sf->rt_sf.prune_inter_modes_with_golden_ref = boosted ? 0 : 1;
  // TODO(any): This sf could be removed.
  sf->rt_sf.short_circuit_low_temp_var = 1;
  sf->rt_sf.check_scene_detection = 1;
  if (cpi->rc.rtc_external_ratectrl) sf->rt_sf.check_scene_detection = 0;
  if (cm->current_frame.frame_type != KEY_FRAME &&
      cpi->oxcf.rc_cfg.mode == AOM_CBR)
    sf->rt_sf.overshoot_detection_cbr = FAST_DETECTION_MAXQ;
  // Enable noise estimation only for high resolutions for now.
  //
  // Since use_temporal_noise_estimate has no effect for all-intra frame
  // encoding, it is disabled for this case.
  if (cpi->oxcf.kf_cfg.key_freq_max != 0 && cm->width * cm->height > 640 * 480)
    sf->rt_sf.use_temporal_noise_estimate = 1;
  sf->rt_sf.skip_tx_no_split_var_based_partition = 1;
  sf->rt_sf.skip_newmv_mode_based_on_sse = 1;
  sf->rt_sf.mode_search_skip_flags =
      (cm->current_frame.frame_type == KEY_FRAME)
          ? 0
          : FLAG_SKIP_INTRA_DIRMISMATCH | FLAG_SKIP_INTRA_BESTINTER |
                FLAG_SKIP_COMP_BESTINTRA | FLAG_SKIP_INTRA_LOWVAR |
                FLAG_EARLY_TERMINATE;
  sf->rt_sf.var_part_split_threshold_shift = 5;
  if (!frame_is_intra_only(&cpi->common)) sf->rt_sf.var_part_based_on_qidx = 1;
  sf->rt_sf.use_fast_fixed_part = 0;
  sf->rt_sf.increase_source_sad_thresh = 0;

  if (speed >= 6) {
    sf->mv_sf.use_fullpel_costlist = 1;

    sf->rd_sf.tx_domain_dist_thres_level = 3;

    sf->tx_sf.tx_type_search.fast_inter_tx_type_prob_thresh = 0;
    sf->inter_sf.limit_inter_mode_cands = 4;
    sf->inter_sf.prune_warped_prob_thresh = 8;
    sf->inter_sf.extra_prune_warped = 1;

    sf->rt_sf.gf_refresh_based_on_qp = 1;
    sf->rt_sf.prune_inter_modes_wrt_gf_arf_based_on_sad = 1;
    sf->rt_sf.var_part_split_threshold_shift = 7;
    if (!frame_is_intra_only(&cpi->common))
      sf->rt_sf.var_part_based_on_qidx = 2;

    sf->winner_mode_sf.prune_winner_mode_eval_level = boosted ? 0 : 3;
  }

  if (speed >= 7) {
    sf->rt_sf.sse_early_term_inter_search = EARLY_TERM_IDX_1;
    sf->rt_sf.use_comp_ref_nonrd = 1;
    sf->rt_sf.ref_frame_comp_nonrd[2] = 1;  // LAST_ALTREF
    sf->tx_sf.intra_tx_size_search_init_depth_sqr = 2;
    sf->part_sf.partition_search_type = VAR_BASED_PARTITION;
    sf->part_sf.max_intra_bsize = BLOCK_32X32;

    sf->mv_sf.search_method = FAST_DIAMOND;
    sf->mv_sf.subpel_force_stop = QUARTER_PEL;

    sf->inter_sf.inter_mode_rd_model_estimation = 2;
    // This sf is not applicable in non-rd path.
    sf->inter_sf.skip_newmv_in_drl = 0;

    sf->interp_sf.skip_interp_filter_search = 0;

    // Disable intra_y_mode_mask pruning since the performance at speed 7 isn't
    // good. May need more study.
    for (int i = 0; i < TX_SIZES; ++i) {
      sf->intra_sf.intra_y_mode_mask[i] = INTRA_ALL;
    }

    sf->lpf_sf.lpf_pick = LPF_PICK_FROM_Q;
    sf->lpf_sf.cdef_pick_method = CDEF_FAST_SEARCH_LVL5;

    sf->rt_sf.mode_search_skip_flags |= FLAG_SKIP_INTRA_DIRMISMATCH;
    sf->rt_sf.nonrd_prune_ref_frame_search = 1;
    // This is for rd path only.
    sf->rt_sf.prune_inter_modes_using_temp_var = 0;
    sf->rt_sf.prune_inter_modes_wrt_gf_arf_based_on_sad = 0;
    sf->rt_sf.prune_intra_mode_based_on_mv_range = 0;
#if !CONFIG_REALTIME_ONLY
    sf->rt_sf.reuse_inter_pred_nonrd =
        (cpi->oxcf.motion_mode_cfg.enable_warped_motion == 0);
#else
    sf->rt_sf.reuse_inter_pred_nonrd = 1;
#endif
#if CONFIG_AV1_TEMPORAL_DENOISING
    sf->rt_sf.reuse_inter_pred_nonrd = (cpi->oxcf.noise_sensitivity == 0);
#endif
    sf->rt_sf.short_circuit_low_temp_var = 0;
    // For spatial layers, only LAST and GOLDEN are currently used in the SVC
    // for nonrd. The flag use_nonrd_altref_frame can disable GOLDEN in the
    // get_ref_frame_flags() for some patterns, so disable it here for
    // spatial layers.
    sf->rt_sf.use_nonrd_altref_frame =
        (cpi->svc.number_spatial_layers > 1) ? 0 : 1;
    sf->rt_sf.use_nonrd_pick_mode = 1;
    sf->rt_sf.nonrd_check_partition_merge_mode = 3;
    sf->rt_sf.skip_intra_pred = 1;
    sf->rt_sf.source_metrics_sb_nonrd = 1;
    // Set mask for intra modes.
    for (int i = 0; i < BLOCK_SIZES; ++i)
      if (i >= BLOCK_32X32)
        sf->rt_sf.intra_y_mode_bsize_mask_nrd[i] = INTRA_DC;
      else
        // Use DC, H, V intra mode for block sizes < 32X32.
        sf->rt_sf.intra_y_mode_bsize_mask_nrd[i] = INTRA_DC_H_V;

    sf->winner_mode_sf.dc_blk_pred_level = 0;
    sf->rt_sf.var_part_based_on_qidx = 3;
    sf->rt_sf.prune_compoundmode_with_singlecompound_var = true;
    sf->rt_sf.prune_compoundmode_with_singlemode_var = true;
    sf->rt_sf.skip_compound_based_on_var = true;
    sf->rt_sf.use_adaptive_subpel_search = true;
  }

  if (speed >= 8) {
    sf->rt_sf.sse_early_term_inter_search = EARLY_TERM_IDX_2;
    sf->intra_sf.intra_pruning_with_hog = 1;
    sf->rt_sf.short_circuit_low_temp_var = 1;
    sf->rt_sf.use_nonrd_altref_frame = 0;
    sf->rt_sf.nonrd_prune_ref_frame_search = 2;
    sf->rt_sf.nonrd_check_partition_merge_mode = 0;
    sf->rt_sf.var_part_split_threshold_shift = 8;
    sf->rt_sf.var_part_based_on_qidx = 4;
    sf->rt_sf.partition_direct_merging = 1;
    sf->rt_sf.prune_compoundmode_with_singlemode_var = false;
    sf->mv_sf.use_bsize_dependent_search_method = 2;
    sf->rt_sf.prune_hv_pred_modes_using_src_sad = true;
  }
  if (speed >= 9) {
    sf->rt_sf.sse_early_term_inter_search = EARLY_TERM_IDX_3;
    sf->rt_sf.estimate_motion_for_var_based_partition = 3;
    sf->rt_sf.prefer_large_partition_blocks = 3;
    sf->rt_sf.skip_intra_pred = 2;
    sf->rt_sf.var_part_split_threshold_shift = 9;
    for (int i = 0; i < BLOCK_SIZES; ++i)
      sf->rt_sf.intra_y_mode_bsize_mask_nrd[i] = INTRA_DC;
    sf->rt_sf.var_part_based_on_qidx = 0;
    sf->rt_sf.frame_level_mode_cost_update = true;
    sf->rt_sf.check_only_zero_zeromv_on_large_blocks = true;
    sf->rt_sf.reduce_mv_pel_precision_highmotion = 0;
    sf->rt_sf.use_adaptive_subpel_search = true;
    sf->mv_sf.use_bsize_dependent_search_method = 0;
  }
  if (speed >= 10) {
    sf->rt_sf.sse_early_term_inter_search = EARLY_TERM_IDX_4;
    sf->rt_sf.nonrd_prune_ref_frame_search = 3;
    sf->rt_sf.var_part_split_threshold_shift = 10;
    sf->mv_sf.subpel_search_method = SUBPEL_TREE_PRUNED_MORE;
  }
  if (speed >= 11 && !frame_is_intra_only(cm) &&
      cpi->oxcf.tune_cfg.content == AOM_CONTENT_SCREEN) {
    sf->winner_mode_sf.dc_blk_pred_level = 3;
  }
}

static inline void init_hl_sf(HIGH_LEVEL_SPEED_FEATURES *hl_sf) {
  // best quality defaults
  hl_sf->frame_parameter_update = 1;
  hl_sf->recode_loop = ALLOW_RECODE;
  // Recode loop tolerance %.
  hl_sf->recode_tolerance = 25;
  hl_sf->high_precision_mv_usage = CURRENT_Q;
  hl_sf->superres_auto_search_type = SUPERRES_AUTO_ALL;
  hl_sf->disable_extra_sc_testing = 0;
  hl_sf->second_alt_ref_filtering = 1;
  hl_sf->adjust_num_frames_for_arf_filtering = 0;
  hl_sf->accurate_bit_estimate = 0;
  hl_sf->weight_calc_level_in_tf = 0;
  hl_sf->allow_sub_blk_me_in_tf = 0;
}

static inline void init_fp_sf(FIRST_PASS_SPEED_FEATURES *fp_sf) {
  fp_sf->reduce_mv_step_param = 3;
  fp_sf->skip_motion_search_threshold = 0;
  fp_sf->disable_recon = 0;
  fp_sf->skip_zeromv_motion_search = 0;
}

static inline void init_tpl_sf(TPL_SPEED_FEATURES *tpl_sf) {
  tpl_sf->gop_length_decision_method = 0;
  tpl_sf->prune_intra_modes = 0;
  tpl_sf->prune_starting_mv = 0;
  tpl_sf->reduce_first_step_size = 0;
  tpl_sf->skip_alike_starting_mv = 0;
  tpl_sf->subpel_force_stop = EIGHTH_PEL;
  tpl_sf->search_method = NSTEP;
  tpl_sf->prune_ref_frames_in_tpl = 0;
  tpl_sf->allow_compound_pred = 1;
  tpl_sf->use_y_only_rate_distortion = 0;
  tpl_sf->use_sad_for_mode_decision = 0;
  tpl_sf->reduce_num_frames = 0;
}

static inline void init_gm_sf(GLOBAL_MOTION_SPEED_FEATURES *gm_sf) {
  gm_sf->gm_search_type = GM_FULL_SEARCH;
  gm_sf->prune_ref_frame_for_gm_search = 0;
  gm_sf->prune_zero_mv_with_sse = 0;
  gm_sf->disable_gm_search_based_on_stats = 0;
  gm_sf->downsample_level = 0;
  gm_sf->num_refinement_steps = GM_MAX_REFINEMENT_STEPS;
}

static inline void init_part_sf(PARTITION_SPEED_FEATURES *part_sf) {
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
  part_sf->prune_rect_part_using_4x4_var_deviation = false;
  part_sf->prune_rect_part_using_none_pred_mode = false;
  part_sf->early_term_after_none_split = 0;
  part_sf->ml_predict_breakout_level = 0;
  part_sf->prune_sub_8x8_partition_level = 0;
  part_sf->simple_motion_search_rect_split = 0;
  part_sf->reuse_prev_rd_results_for_part_ab = 0;
  part_sf->reuse_best_prediction_for_part_ab = 0;
  part_sf->use_best_rd_for_pruning = 0;
  part_sf->skip_non_sq_part_based_on_none = 0;
  part_sf->disable_8x8_part_based_on_qidx = 0;
}

static inline void init_mv_sf(MV_SPEED_FEATURES *mv_sf) {
  mv_sf->full_pixel_search_level = 0;
  mv_sf->auto_mv_step_size = 0;
  mv_sf->exhaustive_searches_thresh = 0;
  mv_sf->obmc_full_pixel_search_level = 0;
  mv_sf->prune_mesh_search = PRUNE_MESH_SEARCH_DISABLED;
  mv_sf->reduce_search_range = 0;
  mv_sf->search_method = NSTEP;
  mv_sf->simple_motion_subpel_force_stop = EIGHTH_PEL;
  mv_sf->subpel_force_stop = EIGHTH_PEL;
  mv_sf->subpel_iters_per_step = 2;
  mv_sf->subpel_search_method = SUBPEL_TREE;
  mv_sf->use_accurate_subpel_search = USE_8_TAPS;
  mv_sf->use_bsize_dependent_search_method = 0;
  mv_sf->use_fullpel_costlist = 0;
  mv_sf->use_downsampled_sad = 0;
  mv_sf->disable_extensive_joint_motion_search = 0;
  mv_sf->disable_second_mv = 0;
  mv_sf->skip_fullpel_search_using_startmv = 0;
  mv_sf->warp_search_method = WARP_SEARCH_SQUARE;
  mv_sf->warp_search_iters = 8;
  mv_sf->use_intrabc = 1;
}

static inline void init_inter_sf(INTER_MODE_SPEED_FEATURES *inter_sf) {
  inter_sf->adaptive_rd_thresh = 0;
  inter_sf->model_based_post_interp_filter_breakout = 0;
  inter_sf->reduce_inter_modes = 0;
  inter_sf->alt_ref_search_fp = 0;
  inter_sf->prune_single_ref = 0;
  inter_sf->prune_comp_ref_frames = 0;
  inter_sf->selective_ref_frame = 0;
  inter_sf->prune_ref_frame_for_rect_partitions = 0;
  inter_sf->fast_wedge_sign_estimate = 0;
  inter_sf->use_dist_wtd_comp_flag = DIST_WTD_COMP_ENABLED;
  inter_sf->reuse_inter_intra_mode = 0;
  inter_sf->mv_cost_upd_level = INTERNAL_COST_UPD_SB;
  inter_sf->coeff_cost_upd_level = INTERNAL_COST_UPD_SB;
  inter_sf->mode_cost_upd_level = INTERNAL_COST_UPD_SB;
  inter_sf->prune_inter_modes_based_on_tpl = 0;
  inter_sf->prune_nearmv_using_neighbors = PRUNE_NEARMV_OFF;
  inter_sf->prune_comp_search_by_single_result = 0;
  inter_sf->skip_repeated_ref_mv = 0;
  inter_sf->skip_newmv_in_drl = 0;
  inter_sf->inter_mode_rd_model_estimation = 0;
  inter_sf->prune_compound_using_single_ref = 0;
  inter_sf->prune_ext_comp_using_neighbors = 0;
  inter_sf->skip_ext_comp_nearmv_mode = 0;
  inter_sf->prune_comp_using_best_single_mode_ref = 0;
  inter_sf->prune_nearest_near_mv_using_refmv_weight = 0;
  inter_sf->disable_onesided_comp = 0;
  inter_sf->prune_mode_search_simple_translation = 0;
  inter_sf->prune_comp_type_by_comp_avg = 0;
  inter_sf->disable_interinter_wedge_newmv_search = 0;
  inter_sf->fast_interintra_wedge_search = 0;
  inter_sf->prune_comp_type_by_model_rd = 0;
  inter_sf->perform_best_rd_based_gating_for_chroma = 0;
  inter_sf->prune_obmc_prob_thresh = 0;
  inter_sf->disable_interinter_wedge_var_thresh = 0;
  inter_sf->disable_interintra_wedge_var_thresh = 0;
  inter_sf->prune_ref_mv_idx_search = 0;
  inter_sf->prune_warped_prob_thresh = 0;
  inter_sf->reuse_compound_type_decision = 0;
  inter_sf->prune_inter_modes_if_skippable = 0;
  inter_sf->disable_masked_comp = 0;
  inter_sf->enable_fast_compound_mode_search = 0;
  inter_sf->reuse_mask_search_results = 0;
  inter_sf->enable_fast_wedge_mask_search = 0;
  inter_sf->inter_mode_txfm_breakout = 0;
  inter_sf->limit_inter_mode_cands = 0;
  inter_sf->limit_txfm_eval_per_mode = 0;
  inter_sf->skip_arf_compound = 0;
  set_txfm_rd_gate_level(inter_sf->txfm_rd_gate_level, 0);
}

static inline void init_interp_sf(INTERP_FILTER_SPEED_FEATURES *interp_sf) {
  interp_sf->adaptive_interp_filter_search = 0;
  interp_sf->cb_pred_filter_search = 0;
  interp_sf->disable_dual_filter = 0;
  interp_sf->skip_sharp_interp_filter_search = 0;
  interp_sf->use_fast_interpolation_filter_search = 0;
  interp_sf->use_interp_filter = 0;
  interp_sf->skip_interp_filter_search = 0;
}

static inline void init_intra_sf(INTRA_MODE_SPEED_FEATURES *intra_sf) {
  intra_sf->dv_cost_upd_level = INTERNAL_COST_UPD_SB;
  intra_sf->skip_intra_in_interframe = 1;
  intra_sf->intra_pruning_with_hog = 0;
  intra_sf->chroma_intra_pruning_with_hog = 0;
  intra_sf->prune_palette_search_level = 0;
  intra_sf->prune_luma_palette_size_search_level = 0;

  for (int i = 0; i < TX_SIZES; i++) {
    intra_sf->intra_y_mode_mask[i] = INTRA_ALL;
    intra_sf->intra_uv_mode_mask[i] = UV_INTRA_ALL;
  }
  intra_sf->disable_smooth_intra = 0;
  intra_sf->prune_smooth_intra_mode_for_chroma = 0;
  intra_sf->prune_filter_intra_level = 0;
  intra_sf->prune_chroma_modes_using_luma_winner = 0;
  intra_sf->cfl_search_range = 3;
  intra_sf->top_intra_model_count_allowed = TOP_INTRA_MODEL_COUNT;
  intra_sf->adapt_top_model_rd_count_using_neighbors = 0;
  intra_sf->early_term_chroma_palette_size_search = 0;
  intra_sf->skip_filter_intra_in_inter_frames = 0;
  intra_sf->prune_luma_odd_delta_angles_in_intra = 0;
}

static inline void init_tx_sf(TX_SPEED_FEATURES *tx_sf) {
  tx_sf->inter_tx_size_search_init_depth_sqr = 0;
  tx_sf->inter_tx_size_search_init_depth_rect = 0;
  tx_sf->intra_tx_size_search_init_depth_rect = 0;
  tx_sf->intra_tx_size_search_init_depth_sqr = 0;
  tx_sf->tx_size_search_lgr_block = 0;
  tx_sf->model_based_prune_tx_search_level = 0;
  tx_sf->tx_type_search.prune_2d_txfm_mode = TX_TYPE_PRUNE_1;
  tx_sf->tx_type_search.ml_tx_split_thresh = 8500;
  tx_sf->tx_type_search.use_skip_flag_prediction = 1;
  tx_sf->tx_type_search.use_reduced_intra_txset = 0;
  tx_sf->tx_type_search.fast_intra_tx_type_search = 0;
  tx_sf->tx_type_search.fast_inter_tx_type_prob_thresh = INT_MAX;
  tx_sf->tx_type_search.skip_tx_search = 0;
  tx_sf->tx_type_search.prune_tx_type_using_stats = 0;
  tx_sf->tx_type_search.prune_tx_type_est_rd = 0;
  tx_sf->tx_type_search.winner_mode_tx_type_pruning = 0;
  tx_sf->txb_split_cap = 1;
  tx_sf->adaptive_txb_search_level = 0;
  tx_sf->refine_fast_tx_search_results = 1;
  tx_sf->prune_tx_size_level = 0;
  tx_sf->prune_intra_tx_depths_using_nn = false;
  tx_sf->use_rd_based_breakout_for_intra_tx_search = false;
}

static inline void init_rd_sf(RD_CALC_SPEED_FEATURES *rd_sf,
                              const AV1EncoderConfig *oxcf) {
  const int disable_trellis_quant = oxcf->algo_cfg.disable_trellis_quant;
  if (disable_trellis_quant == 3) {
    rd_sf->optimize_coefficients = !is_lossless_requested(&oxcf->rc_cfg)
                                       ? NO_ESTIMATE_YRD_TRELLIS_OPT
                                       : NO_TRELLIS_OPT;
  } else if (disable_trellis_quant == 2) {
    rd_sf->optimize_coefficients = !is_lossless_requested(&oxcf->rc_cfg)
                                       ? FINAL_PASS_TRELLIS_OPT
                                       : NO_TRELLIS_OPT;
  } else if (disable_trellis_quant == 0) {
    if (is_lossless_requested(&oxcf->rc_cfg)) {
      rd_sf->optimize_coefficients = NO_TRELLIS_OPT;
    } else {
      rd_sf->optimize_coefficients = FULL_TRELLIS_OPT;
    }
  } else if (disable_trellis_quant == 1) {
    rd_sf->optimize_coefficients = NO_TRELLIS_OPT;
  } else {
    assert(0 && "Invalid disable_trellis_quant value");
  }
  rd_sf->use_mb_rd_hash = 0;
  rd_sf->simple_model_rd_from_var = 0;
  rd_sf->tx_domain_dist_level = 0;
  rd_sf->tx_domain_dist_thres_level = 0;
  rd_sf->perform_coeff_opt = 0;
}

static inline void init_winner_mode_sf(
    WINNER_MODE_SPEED_FEATURES *winner_mode_sf) {
  winner_mode_sf->motion_mode_for_winner_cand = 0;
  // Set this at the appropriate speed levels
  winner_mode_sf->tx_size_search_level = 0;
  winner_mode_sf->enable_winner_mode_for_coeff_opt = 0;
  winner_mode_sf->enable_winner_mode_for_tx_size_srch = 0;
  winner_mode_sf->enable_winner_mode_for_use_tx_domain_dist = 0;
  winner_mode_sf->multi_winner_mode_type = 0;
  winner_mode_sf->dc_blk_pred_level = 0;
  winner_mode_sf->winner_mode_ifs = 0;
  winner_mode_sf->prune_winner_mode_eval_level = 0;
}

static inline void init_lpf_sf(LOOP_FILTER_SPEED_FEATURES *lpf_sf) {
  lpf_sf->disable_loop_restoration_chroma = 0;
  lpf_sf->disable_loop_restoration_luma = 0;
  lpf_sf->min_lr_unit_size = RESTORATION_PROC_UNIT_SIZE;
  lpf_sf->max_lr_unit_size = RESTORATION_UNITSIZE_MAX;
  lpf_sf->prune_wiener_based_on_src_var = 0;
  lpf_sf->prune_sgr_based_on_wiener = 0;
  lpf_sf->enable_sgr_ep_pruning = 0;
  lpf_sf->reduce_wiener_window_size = 0;
  lpf_sf->adaptive_luma_loop_filter_skip = 0;
  lpf_sf->skip_loop_filter_using_filt_error = 0;
  lpf_sf->lpf_pick = LPF_PICK_FROM_FULL_IMAGE;
  lpf_sf->use_coarse_filter_level_search = 0;
  lpf_sf->cdef_pick_method = CDEF_FULL_SEARCH;
  // Set decoder side speed feature to use less dual sgr modes
  lpf_sf->dual_sgr_penalty_level = 0;
  // Enable Wiener and Self-guided Loop restoration filters by default.
  lpf_sf->disable_wiener_filter = false;
  lpf_sf->disable_sgr_filter = false;
  lpf_sf->disable_wiener_coeff_refine_search = false;
  lpf_sf->use_downsampled_wiener_stats = 0;
}

static inline void init_rt_sf(REAL_TIME_SPEED_FEATURES *rt_sf) {
  rt_sf->check_intra_pred_nonrd = 0;
  rt_sf->skip_intra_pred = 0;
  rt_sf->estimate_motion_for_var_based_partition = 0;
  rt_sf->nonrd_check_partition_merge_mode = 0;
  rt_sf->nonrd_check_partition_split = 0;
  rt_sf->mode_search_skip_flags = 0;
  rt_sf->nonrd_prune_ref_frame_search = 0;
  rt_sf->use_nonrd_pick_mode = 0;
  rt_sf->use_nonrd_altref_frame = 0;
  rt_sf->use_comp_ref_nonrd = 0;
  rt_sf->use_real_time_ref_set = 0;
  rt_sf->short_circuit_low_temp_var = 0;
  rt_sf->reuse_inter_pred_nonrd = 0;
  rt_sf->num_inter_modes_for_tx_search = INT_MAX;
  rt_sf->use_nonrd_filter_search = 0;
  rt_sf->use_simple_rd_model = 0;
  rt_sf->hybrid_intra_pickmode = 0;
  rt_sf->prune_palette_search_nonrd = 0;
  rt_sf->source_metrics_sb_nonrd = 0;
  rt_sf->overshoot_detection_cbr = NO_DETECTION;
  rt_sf->check_scene_detection = 0;
  rt_sf->rc_adjust_keyframe = 0;
  rt_sf->rc_compute_spatial_var_sc = 0;
  rt_sf->prefer_large_partition_blocks = 0;
  rt_sf->use_temporal_noise_estimate = 0;
  rt_sf->fullpel_search_step_param = 0;
  for (int i = 0; i < BLOCK_SIZES; ++i)
    rt_sf->intra_y_mode_bsize_mask_nrd[i] = INTRA_ALL;
  rt_sf->prune_hv_pred_modes_using_src_sad = false;
  rt_sf->nonrd_aggressive_skip = 0;
  rt_sf->skip_cdef_sb = 0;
  rt_sf->force_large_partition_blocks_intra = 0;
  rt_sf->skip_tx_no_split_var_based_partition = 0;
  rt_sf->skip_newmv_mode_based_on_sse = 0;
  rt_sf->gf_length_lvl = 0;
  rt_sf->prune_inter_modes_with_golden_ref = 0;
  rt_sf->prune_inter_modes_wrt_gf_arf_based_on_sad = 0;
  rt_sf->prune_inter_modes_using_temp_var = 0;
  rt_sf->reduce_mv_pel_precision_highmotion = 0;
  rt_sf->reduce_mv_pel_precision_lowcomplex = 0;
  rt_sf->prune_intra_mode_based_on_mv_range = 0;
  rt_sf->var_part_split_threshold_shift = 7;
  rt_sf->gf_refresh_based_on_qp = 0;
  rt_sf->use_rtc_tf = 0;
  rt_sf->use_idtx_nonrd = 0;
  rt_sf->prune_idtx_nonrd = 0;
  rt_sf->dct_only_palette_nonrd = 0;
  rt_sf->part_early_exit_zeromv = 0;
  rt_sf->sse_early_term_inter_search = EARLY_TERM_DISABLED;
  rt_sf->skip_lf_screen = 0;
  rt_sf->thresh_active_maps_skip_lf_cdef = 100;
  rt_sf->sad_based_adp_altref_lag = 0;
  rt_sf->partition_direct_merging = 0;
  rt_sf->var_part_based_on_qidx = 0;
  rt_sf->tx_size_level_based_on_qstep = 0;
  rt_sf->vbp_prune_16x16_split_using_min_max_sub_blk_var = false;
  rt_sf->prune_compoundmode_with_singlecompound_var = false;
  rt_sf->frame_level_mode_cost_update = false;
  rt_sf->prune_h_pred_using_best_mode_so_far = false;
  rt_sf->enable_intra_mode_pruning_using_neighbors = false;
  rt_sf->prune_intra_mode_using_best_sad_so_far = false;
  rt_sf->check_only_zero_zeromv_on_large_blocks = false;
  rt_sf->disable_cdf_update_non_reference_frame = false;
  rt_sf->prune_compoundmode_with_singlemode_var = false;
  rt_sf->skip_compound_based_on_var = false;
  rt_sf->set_zeromv_skip_based_on_source_sad = 1;
  rt_sf->use_adaptive_subpel_search = false;
  rt_sf->screen_content_cdef_filter_qindex_thresh = 0;
  rt_sf->enable_ref_short_signaling = false;
  rt_sf->check_globalmv_on_single_ref = true;
  rt_sf->increase_color_thresh_palette = false;
  rt_sf->selective_cdf_update = 0;
  rt_sf->force_only_last_ref = 0;
  rt_sf->higher_thresh_scene_detection = 1;
  rt_sf->skip_newmv_flat_blocks_screen = 0;
  rt_sf->skip_encoding_non_reference_slide_change = 0;
  rt_sf->rc_faster_convergence_static = 0;
  rt_sf->skip_newmv_mode_sad_screen = 0;
}

static fractional_mv_step_fp
    *const fractional_mv_search[SUBPEL_SEARCH_METHODS] = {
      av1_find_best_sub_pixel_tree,             // SUBPEL_TREE = 0
      av1_find_best_sub_pixel_tree_pruned,      // SUBPEL_TREE_PRUNED = 1
      av1_find_best_sub_pixel_tree_pruned_more  // SUBPEL_TREE_PRUNED_MORE = 2
    };

// Populate appropriate sub-pel search method based on speed feature and user
// specified settings
static void set_subpel_search_method(
    MotionVectorSearchParams *mv_search_params,
    unsigned int motion_vector_unit_test,
    SUBPEL_SEARCH_METHOD subpel_search_method) {
  assert(subpel_search_method <= SUBPEL_TREE_PRUNED_MORE);
  mv_search_params->find_fractional_mv_step =
      fractional_mv_search[subpel_search_method];

  // This is only used in motion vector unit test.
  if (motion_vector_unit_test == 1)
    mv_search_params->find_fractional_mv_step = av1_return_max_sub_pixel_mv;
  else if (motion_vector_unit_test == 2)
    mv_search_params->find_fractional_mv_step = av1_return_min_sub_pixel_mv;
}

void av1_set_speed_features_framesize_dependent(AV1_COMP *cpi, int speed) {
  SPEED_FEATURES *const sf = &cpi->sf;
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;

  switch (oxcf->mode) {
    case GOOD:
      set_good_speed_feature_framesize_dependent(cpi, sf, speed);
      break;
    case ALLINTRA:
      set_allintra_speed_feature_framesize_dependent(cpi, sf, speed);
      break;
    case REALTIME:
      set_rt_speed_feature_framesize_dependent(cpi, sf, speed);
      break;
  }

  if (!cpi->ppi->seq_params_locked) {
    cpi->common.seq_params->enable_masked_compound &=
        !sf->inter_sf.disable_masked_comp;
    cpi->common.seq_params->enable_interintra_compound &=
        (sf->inter_sf.disable_interintra_wedge_var_thresh != UINT_MAX);
  }

  set_subpel_search_method(&cpi->mv_search_params,
                           cpi->oxcf.unit_test_cfg.motion_vector_unit_test,
                           sf->mv_sf.subpel_search_method);

  // For multi-thread use case with row_mt enabled, cost update for a set of
  // SB rows is not desirable. Hence, the sf mv_cost_upd_level is set to
  // INTERNAL_COST_UPD_SBROW in such cases.
  if ((cpi->oxcf.row_mt == 1) && (cpi->mt_info.num_workers > 1)) {
    if (sf->inter_sf.mv_cost_upd_level == INTERNAL_COST_UPD_SBROW_SET) {
      // Set mv_cost_upd_level to use row level update.
      sf->inter_sf.mv_cost_upd_level = INTERNAL_COST_UPD_SBROW;
    }
  }
}

void av1_set_speed_features_framesize_independent(AV1_COMP *cpi, int speed) {
  SPEED_FEATURES *const sf = &cpi->sf;
  WinnerModeParams *const winner_mode_params = &cpi->winner_mode_params;
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  int i;

  init_hl_sf(&sf->hl_sf);
  init_fp_sf(&sf->fp_sf);
  init_tpl_sf(&sf->tpl_sf);
  init_gm_sf(&sf->gm_sf);
  init_part_sf(&sf->part_sf);
  init_mv_sf(&sf->mv_sf);
  init_inter_sf(&sf->inter_sf);
  init_interp_sf(&sf->interp_sf);
  init_intra_sf(&sf->intra_sf);
  init_tx_sf(&sf->tx_sf);
  init_rd_sf(&sf->rd_sf, oxcf);
  init_winner_mode_sf(&sf->winner_mode_sf);
  init_lpf_sf(&sf->lpf_sf);
  init_rt_sf(&sf->rt_sf);

  switch (oxcf->mode) {
    case GOOD:
      set_good_speed_features_framesize_independent(cpi, sf, speed);
      break;
    case ALLINTRA:
      set_allintra_speed_features_framesize_independent(cpi, sf, speed);
      break;
    case REALTIME:
      set_rt_speed_features_framesize_independent(cpi, sf, speed);
      break;
  }

  // Note: when use_nonrd_pick_mode is true, the transform size is the
  // minimum of 16x16 and the largest possible size of the current block,
  // which conflicts with the speed feature "enable_tx_size_search".
  if (!oxcf->txfm_cfg.enable_tx_size_search &&
      sf->rt_sf.use_nonrd_pick_mode == 0) {
    sf->winner_mode_sf.tx_size_search_level = 3;
  }

  if (cpi->mt_info.num_workers > 1) {
    // Loop restoration stage is conditionally disabled for speed 5, 6 when
    // num_workers > 1. Since av1_pick_filter_restoration() is not
    // multi-threaded, enabling the Loop restoration stage will cause an
    // increase in encode time (3% to 7% increase depends on frame
    // resolution).
    // TODO(aomedia:3446): Implement multi-threading of
    // av1_pick_filter_restoration() and enable Wiener filter for speed 5, 6
    // similar to single thread encoding path.
    if (speed >= 5) {
      sf->lpf_sf.disable_sgr_filter = true;
      sf->lpf_sf.disable_wiener_filter = true;
    }
  }

  if (!cpi->ppi->seq_params_locked) {
    cpi->common.seq_params->order_hint_info.enable_dist_wtd_comp &=
        (sf->inter_sf.use_dist_wtd_comp_flag != DIST_WTD_COMP_DISABLED);
    cpi->common.seq_params->enable_dual_filter &=
        !sf->interp_sf.disable_dual_filter;
    // Set the flag 'enable_restoration', if one the Loop restoration filters
    // (i.e., Wiener or Self-guided) is enabled.
    cpi->common.seq_params->enable_restoration &=
        (!sf->lpf_sf.disable_wiener_filter || !sf->lpf_sf.disable_sgr_filter);

    cpi->common.seq_params->enable_interintra_compound &=
        (sf->inter_sf.disable_interintra_wedge_var_thresh != UINT_MAX);
  }

  const int mesh_speed = AOMMIN(speed, MAX_MESH_SPEED);
  for (i = 0; i < MAX_MESH_STEP; ++i) {
    sf->mv_sf.mesh_patterns[i].range =
        good_quality_mesh_patterns[mesh_speed][i].range;
    sf->mv_sf.mesh_patterns[i].interval =
        good_quality_mesh_patterns[mesh_speed][i].interval;
  }

  // Update the mesh pattern of exhaustive motion search for intraBC
  // Though intraBC mesh pattern is populated for all frame types, it is used
  // only for intra frames of screen contents
  for (i = 0; i < MAX_MESH_STEP; ++i) {
    sf->mv_sf.intrabc_mesh_patterns[i].range =
        intrabc_mesh_patterns[mesh_speed][i].range;
    sf->mv_sf.intrabc_mesh_patterns[i].interval =
        intrabc_mesh_patterns[mesh_speed][i].interval;
  }

  // Slow quant, dct and trellis not worthwhile for first pass
  // so make sure they are always turned off.
  if (is_stat_generation_stage(cpi))
    sf->rd_sf.optimize_coefficients = NO_TRELLIS_OPT;

  // No recode for 1 pass.
  if (oxcf->pass == AOM_RC_ONE_PASS && has_no_stats_stage(cpi))
    sf->hl_sf.recode_loop = DISALLOW_RECODE;

  set_subpel_search_method(&cpi->mv_search_params,
                           cpi->oxcf.unit_test_cfg.motion_vector_unit_test,
                           sf->mv_sf.subpel_search_method);

  // assert ensures that tx_domain_dist_level is accessed correctly
  assert(cpi->sf.rd_sf.tx_domain_dist_thres_level >= 0 &&
         cpi->sf.rd_sf.tx_domain_dist_thres_level < 4);
  memcpy(winner_mode_params->tx_domain_dist_threshold,
         tx_domain_dist_thresholds[cpi->sf.rd_sf.tx_domain_dist_thres_level],
         sizeof(winner_mode_params->tx_domain_dist_threshold));

  assert(cpi->sf.rd_sf.tx_domain_dist_level >= 0 &&
         cpi->sf.rd_sf.tx_domain_dist_level < TX_DOMAIN_DIST_LEVELS);
  memcpy(winner_mode_params->use_transform_domain_distortion,
         tx_domain_dist_types[cpi->sf.rd_sf.tx_domain_dist_level],
         sizeof(winner_mode_params->use_transform_domain_distortion));

  // assert ensures that coeff_opt_thresholds is accessed correctly
  assert(cpi->sf.rd_sf.perform_coeff_opt >= 0 &&
         cpi->sf.rd_sf.perform_coeff_opt < 9);
  memcpy(winner_mode_params->coeff_opt_thresholds,
         &coeff_opt_thresholds[cpi->sf.rd_sf.perform_coeff_opt],
         sizeof(winner_mode_params->coeff_opt_thresholds));

  // assert ensures that predict_skip_levels is accessed correctly
  assert(cpi->sf.tx_sf.tx_type_search.use_skip_flag_prediction >= 0 &&
         cpi->sf.tx_sf.tx_type_search.use_skip_flag_prediction < 3);
  memcpy(winner_mode_params->skip_txfm_level,
         predict_skip_levels[cpi->sf.tx_sf.tx_type_search
                                 .use_skip_flag_prediction],
         sizeof(winner_mode_params->skip_txfm_level));

  // assert ensures that tx_size_search_level is accessed correctly
  assert(cpi->sf.winner_mode_sf.tx_size_search_level >= 0 &&
         cpi->sf.winner_mode_sf.tx_size_search_level <= 3);
  memcpy(winner_mode_params->tx_size_search_methods,
         tx_size_search_methods[cpi->sf.winner_mode_sf.tx_size_search_level],
         sizeof(winner_mode_params->tx_size_search_methods));
  memcpy(winner_mode_params->predict_dc_level,
         predict_dc_levels[cpi->sf.winner_mode_sf.dc_blk_pred_level],
         sizeof(winner_mode_params->predict_dc_level));

  if (cpi->oxcf.row_mt == 1 && (cpi->mt_info.num_workers > 1)) {
    if (sf->inter_sf.inter_mode_rd_model_estimation == 1) {
      // Revert to type 2
      sf->inter_sf.inter_mode_rd_model_estimation = 2;
    }

#if !CONFIG_FPMT_TEST
    // Disable the speed feature 'prune_ref_frame_for_gm_search' to achieve
    // better parallelism when number of threads available are greater than or
    // equal to maximum number of reference frames allowed for global motion.
    if (sf->gm_sf.gm_search_type != GM_DISABLE_SEARCH &&
        (cpi->mt_info.num_workers >=
         gm_available_reference_frames[sf->gm_sf.gm_search_type]))
      sf->gm_sf.prune_ref_frame_for_gm_search = 0;
#endif
  }

  // This only applies to the real time mode. Adaptive gf refresh is disabled if
  // gf_cbr_boost_pct that is set by the user is larger than 0.
  if (cpi->oxcf.rc_cfg.gf_cbr_boost_pct > 0)
    sf->rt_sf.gf_refresh_based_on_qp = 0;
}

// Override some speed features based on qindex
void av1_set_speed_features_qindex_dependent(AV1_COMP *cpi, int speed) {
  AV1_COMMON *const cm = &cpi->common;
  SPEED_FEATURES *const sf = &cpi->sf;
  WinnerModeParams *const winner_mode_params = &cpi->winner_mode_params;
  const int boosted = frame_is_boosted(cpi);
  const int is_480p_or_lesser = AOMMIN(cm->width, cm->height) <= 480;
  const int is_480p_or_larger = AOMMIN(cm->width, cm->height) >= 480;
  const int is_720p_or_larger = AOMMIN(cm->width, cm->height) >= 720;
  const int is_1080p_or_larger = AOMMIN(cm->width, cm->height) >= 1080;
  const int is_1440p_or_larger = AOMMIN(cm->width, cm->height) >= 1440;
  const int is_arf2_bwd_type =
      cpi->ppi->gf_group.update_type[cpi->gf_frame_index] == INTNL_ARF_UPDATE;

  if (cpi->oxcf.mode == REALTIME) {
    if (speed >= 6) {
      const int qindex_thresh = boosted ? 190 : (is_720p_or_larger ? 120 : 150);
      sf->part_sf.adjust_var_based_rd_partitioning =
          frame_is_intra_only(cm)
              ? 0
              : cm->quant_params.base_qindex > qindex_thresh;
    }
    return;
  }

  if (speed == 0) {
    // qindex_thresh for resolution < 720p
    const int qindex_thresh = boosted ? 70 : (is_arf2_bwd_type ? 110 : 140);
    if (!is_720p_or_larger && cm->quant_params.base_qindex <= qindex_thresh) {
      sf->part_sf.simple_motion_search_split =
          cm->features.allow_screen_content_tools ? 1 : 2;
      sf->part_sf.simple_motion_search_early_term_none = 1;
      sf->tx_sf.model_based_prune_tx_search_level = 0;
    }

    if (is_720p_or_larger && cm->quant_params.base_qindex <= 128) {
      sf->rd_sf.perform_coeff_opt = 2 + is_1080p_or_larger;
      memcpy(winner_mode_params->coeff_opt_thresholds,
             &coeff_opt_thresholds[sf->rd_sf.perform_coeff_opt],
             sizeof(winner_mode_params->coeff_opt_thresholds));
      sf->part_sf.simple_motion_search_split =
          cm->features.allow_screen_content_tools ? 1 : 2;
      sf->tx_sf.inter_tx_size_search_init_depth_rect = 1;
      sf->tx_sf.inter_tx_size_search_init_depth_sqr = 1;
      sf->tx_sf.intra_tx_size_search_init_depth_rect = 1;
      sf->tx_sf.model_based_prune_tx_search_level = 0;

      if (is_1080p_or_larger && cm->quant_params.base_qindex <= 108) {
        sf->inter_sf.selective_ref_frame = 2;
        sf->rd_sf.tx_domain_dist_level = boosted ? 1 : 2;
        sf->rd_sf.tx_domain_dist_thres_level = 1;
        sf->part_sf.simple_motion_search_early_term_none = 1;
        sf->tx_sf.tx_type_search.ml_tx_split_thresh = 4000;
        sf->interp_sf.cb_pred_filter_search = 0;
        sf->tx_sf.tx_type_search.prune_2d_txfm_mode = TX_TYPE_PRUNE_2;
        sf->tx_sf.tx_type_search.skip_tx_search = 1;
      }
    }
  }

  if (speed >= 2) {
    // Disable extended partitions for lower quantizers
    const int aggr = AOMMIN(4, speed - 2);
    const int qindex_thresh1[4] = { 50, 50, 80, 100 };
    const int qindex_thresh2[4] = { 80, 100, 120, 160 };
    int qindex_thresh;
    if (aggr <= 1) {
      const int qthresh2 =
          (!aggr && !is_480p_or_larger) ? 70 : qindex_thresh2[aggr];
      qindex_thresh = cm->features.allow_screen_content_tools
                          ? qindex_thresh1[aggr]
                          : qthresh2;
      if (cm->quant_params.base_qindex <= qindex_thresh && !boosted)
        sf->part_sf.ext_partition_eval_thresh = BLOCK_128X128;
    } else if (aggr <= 2) {
      qindex_thresh = boosted ? qindex_thresh1[aggr] : qindex_thresh2[aggr];
      if (cm->quant_params.base_qindex <= qindex_thresh &&
          !frame_is_intra_only(cm))
        sf->part_sf.ext_partition_eval_thresh = BLOCK_128X128;
    } else if (aggr <= 3) {
      if (!is_480p_or_larger) {
        sf->part_sf.ext_partition_eval_thresh = BLOCK_128X128;
      } else if (!is_720p_or_larger && !frame_is_intra_only(cm) &&
                 !cm->features.allow_screen_content_tools) {
        sf->part_sf.ext_partition_eval_thresh = BLOCK_128X128;
      } else {
        qindex_thresh = boosted ? qindex_thresh1[aggr] : qindex_thresh2[aggr];
        if (cm->quant_params.base_qindex <= qindex_thresh &&
            !frame_is_intra_only(cm))
          sf->part_sf.ext_partition_eval_thresh = BLOCK_128X128;
      }
    } else {
      sf->part_sf.ext_partition_eval_thresh = BLOCK_128X128;
    }
  }

  if (speed >= 4) {
    // Disable rectangular partitions for lower quantizers
    const int aggr = AOMMIN(1, speed - 4);
    const int qindex_thresh[2] = { 65, 80 };
    int disable_rect_part;
    disable_rect_part = !boosted;
    if (cm->quant_params.base_qindex <= qindex_thresh[aggr] &&
        disable_rect_part && is_480p_or_larger) {
      sf->part_sf.rect_partition_eval_thresh = BLOCK_8X8;
    }
  }

  if (speed <= 2) {
    if (!is_stat_generation_stage(cpi)) {
      // Use faster full-pel motion search for high quantizers.
      // Also use reduced total search range for low resolutions at high
      // quantizers.
      const int aggr = speed;
      const int qindex_thresh1 = ms_qindex_thresh[aggr][is_720p_or_larger][0];
      const int qindex_thresh2 = ms_qindex_thresh[aggr][is_720p_or_larger][1];
      const SEARCH_METHODS search_method =
          motion_search_method[is_720p_or_larger];
      if (cm->quant_params.base_qindex > qindex_thresh1) {
        sf->mv_sf.search_method = search_method;
        sf->tpl_sf.search_method = search_method;
      } else if (cm->quant_params.base_qindex > qindex_thresh2) {
        sf->mv_sf.search_method = NSTEP_8PT;
      }
    }
  }

  if (speed >= 4) {
    // Disable LR search at low and high quantizers and enable only for
    // mid-quantizer range.
    if (!boosted && !is_arf2_bwd_type) {
      const int qindex_low[2] = { 100, 60 };
      const int qindex_high[2] = { 180, 160 };
      if (cm->quant_params.base_qindex <= qindex_low[is_720p_or_larger] ||
          cm->quant_params.base_qindex > qindex_high[is_720p_or_larger]) {
        sf->lpf_sf.disable_loop_restoration_luma = 1;
      }
    }
  }

  if (speed == 1) {
    // Reuse interinter wedge mask search from first search for non-boosted
    // non-internal-arf frames, except at very high quantizers.
    if (cm->quant_params.base_qindex <= 200) {
      if (!boosted && !is_arf2_bwd_type)
        sf->inter_sf.reuse_mask_search_results = 1;
    }
  }

  if (speed == 5) {
    if (!(frame_is_intra_only(&cpi->common) ||
          cm->features.allow_screen_content_tools)) {
      const int qindex[2] = { 256, 128 };
      // Set the sf value as 3 for low resolution and
      // for higher resolutions with low quantizers.
      if (cm->quant_params.base_qindex < qindex[is_480p_or_larger])
        sf->tx_sf.tx_type_search.winner_mode_tx_type_pruning = 3;
    }
  }

  if (speed >= 5) {
    // Disable the sf for low quantizers in case of low resolution screen
    // contents.
    if (cm->features.allow_screen_content_tools &&
        cm->quant_params.base_qindex < 128 && is_480p_or_lesser) {
      sf->part_sf.prune_sub_8x8_partition_level = 0;
    }
  }

  // Loop restoration size search
  // At speed 0, always search all available sizes for the maximum possible gain
  sf->lpf_sf.min_lr_unit_size = RESTORATION_PROC_UNIT_SIZE;
  sf->lpf_sf.max_lr_unit_size = RESTORATION_UNITSIZE_MAX;

  if (speed >= 1) {
    // For large frames, small restoration units are almost never useful,
    // so prune them away
    if (is_1440p_or_larger) {
      sf->lpf_sf.min_lr_unit_size = RESTORATION_UNITSIZE_MAX;
    } else if (is_720p_or_larger) {
      sf->lpf_sf.min_lr_unit_size = RESTORATION_UNITSIZE_MAX >> 1;
    }
  }

  if (speed >= 3 || (cpi->oxcf.mode == ALLINTRA && speed >= 1)) {
    // At this speed, a full search is too expensive. Instead, pick a single
    // size based on size and qindex. Note that, in general, higher quantizers
    // (== lower quality) and larger frames generally want to use larger
    // restoration units.
    int qindex_thresh = 96;
    if (cm->quant_params.base_qindex <= qindex_thresh && !is_1440p_or_larger) {
      sf->lpf_sf.min_lr_unit_size = RESTORATION_UNITSIZE_MAX >> 1;
      sf->lpf_sf.max_lr_unit_size = RESTORATION_UNITSIZE_MAX >> 1;
    } else {
      sf->lpf_sf.min_lr_unit_size = RESTORATION_UNITSIZE_MAX;
      sf->lpf_sf.max_lr_unit_size = RESTORATION_UNITSIZE_MAX;
    }
  }

  set_subpel_search_method(&cpi->mv_search_params,
                           cpi->oxcf.unit_test_cfg.motion_vector_unit_test,
                           sf->mv_sf.subpel_search_method);
}
