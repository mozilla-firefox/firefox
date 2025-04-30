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
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "av1/common/scale.h"
#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"

#include "aom/aomcx.h"

#if CONFIG_DENOISE
#include "aom_dsp/grain_table.h"
#include "aom_dsp/noise_util.h"
#include "aom_dsp/noise_model.h"
#endif
#include "aom_dsp/flow_estimation/corner_detect.h"
#include "aom_dsp/psnr.h"
#if CONFIG_INTERNAL_STATS
#include "aom_dsp/ssim.h"
#endif
#include "aom_ports/aom_timer.h"
#include "aom_ports/mem.h"
#include "aom_util/aom_pthread.h"
#if CONFIG_BITSTREAM_DEBUG
#include "aom_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG

#include "av1/common/alloccommon.h"
#include "av1/common/debugmodes.h"
#include "av1/common/filter.h"
#include "av1/common/idct.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"
#include "av1/common/resize.h"
#include "av1/common/tile_common.h"

#include "av1/encoder/allintra_vis.h"
#include "av1/encoder/aq_complexity.h"
#include "av1/encoder/aq_cyclicrefresh.h"
#include "av1/encoder/aq_variance.h"
#include "av1/encoder/bitstream.h"
#if CONFIG_INTERNAL_STATS
#include "av1/encoder/blockiness.h"
#endif
#include "av1/encoder/context_tree.h"
#include "av1/encoder/dwt.h"
#include "av1/encoder/encodeframe.h"
#include "av1/encoder/encodemv.h"
#include "av1/encoder/encode_strategy.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/encoder_alloc.h"
#include "av1/encoder/encoder_utils.h"
#include "av1/encoder/encodetxb.h"
#include "av1/encoder/ethread.h"
#include "av1/encoder/firstpass.h"
#include "av1/encoder/hash_motion.h"
#include "av1/encoder/hybrid_fwd_txfm.h"
#include "av1/encoder/intra_mode_search.h"
#include "av1/encoder/mv_prec.h"
#include "av1/encoder/pass2_strategy.h"
#include "av1/encoder/pickcdef.h"
#include "av1/encoder/picklpf.h"
#include "av1/encoder/pickrst.h"
#include "av1/encoder/random.h"
#include "av1/encoder/ratectrl.h"
#include "av1/encoder/rc_utils.h"
#include "av1/encoder/rd.h"
#include "av1/encoder/rdopt.h"
#if CONFIG_SALIENCY_MAP
#include "av1/encoder/saliency_map.h"
#endif
#include "av1/encoder/segmentation.h"
#include "av1/encoder/speed_features.h"
#include "av1/encoder/superres_scale.h"
#if CONFIG_THREE_PASS
#include "av1/encoder/thirdpass.h"
#endif
#include "av1/encoder/tpl_model.h"
#include "av1/encoder/reconinter_enc.h"
#include "av1/encoder/var_based_part.h"

#define DEFAULT_EXPLICIT_ORDER_HINT_BITS 7

// #define OUTPUT_YUV_REC
#ifdef OUTPUT_YUV_REC
FILE *yuv_rec_file;
#define FILE_NAME_LEN 100
#endif

#ifdef OUTPUT_YUV_DENOISED
FILE *yuv_denoised_file = NULL;
#endif

static inline void Scale2Ratio(AOM_SCALING_MODE mode, int *hr, int *hs) {
  switch (mode) {
    case AOME_NORMAL:
      *hr = 1;
      *hs = 1;
      break;
    case AOME_FOURFIVE:
      *hr = 4;
      *hs = 5;
      break;
    case AOME_THREEFIVE:
      *hr = 3;
      *hs = 5;
      break;
    case AOME_THREEFOUR:
      *hr = 3;
      *hs = 4;
      break;
    case AOME_ONEFOUR:
      *hr = 1;
      *hs = 4;
      break;
    case AOME_ONEEIGHT:
      *hr = 1;
      *hs = 8;
      break;
    case AOME_ONETWO:
      *hr = 1;
      *hs = 2;
      break;
    case AOME_TWOTHREE:
      *hr = 2;
      *hs = 3;
      break;
    case AOME_ONETHREE:
      *hr = 1;
      *hs = 3;
      break;
    default:
      *hr = 1;
      *hs = 1;
      assert(0);
      break;
  }
}

int av1_set_active_map(AV1_COMP *cpi, unsigned char *new_map_16x16, int rows,
                       int cols) {
  const CommonModeInfoParams *const mi_params = &cpi->common.mi_params;
  if (rows == mi_params->mb_rows && cols == mi_params->mb_cols) {
    unsigned char *const active_map_4x4 = cpi->active_map.map;
    const int mi_rows = mi_params->mi_rows;
    const int mi_cols = mi_params->mi_cols;
    cpi->active_map.update = 0;
    cpi->rc.percent_blocks_inactive = 0;
    assert(mi_rows % 2 == 0 && mi_rows > 0);
    assert(mi_cols % 2 == 0 && mi_cols > 0);
    if (new_map_16x16) {
      int num_samples = 0;
      int num_blocks_inactive = 0;
      for (int r = 0; r < mi_rows; r += 4) {
        for (int c = 0; c < mi_cols; c += 4) {
          const uint8_t val = new_map_16x16[(r >> 2) * cols + (c >> 2)]
                                  ? AM_SEGMENT_ID_ACTIVE
                                  : AM_SEGMENT_ID_INACTIVE;
          num_samples++;
          if (val == AM_SEGMENT_ID_INACTIVE) num_blocks_inactive++;
          const int row_max = AOMMIN(4, mi_rows - r);
          const int col_max = AOMMIN(4, mi_cols - c);
          for (int x = 0; x < row_max; ++x) {
            for (int y = 0; y < col_max; ++y) {
              active_map_4x4[(r + x) * mi_cols + (c + y)] = val;
            }
          }
        }
      }
      cpi->active_map.enabled = 1;
      cpi->active_map.update = 1;
      assert(num_samples);
      cpi->rc.percent_blocks_inactive =
          (num_blocks_inactive * 100) / num_samples;
    }
    return 0;
  }

  return -1;
}

int av1_get_active_map(AV1_COMP *cpi, unsigned char *new_map_16x16, int rows,
                       int cols) {
  const CommonModeInfoParams *const mi_params = &cpi->common.mi_params;
  if (rows == mi_params->mb_rows && cols == mi_params->mb_cols &&
      new_map_16x16) {
    unsigned char *const seg_map_8x8 = cpi->enc_seg.map;
    const int mi_rows = mi_params->mi_rows;
    const int mi_cols = mi_params->mi_cols;
    const int row_scale = mi_size_high_log2[BLOCK_16X16];
    const int col_scale = mi_size_wide_log2[BLOCK_16X16];
    assert(mi_rows % 2 == 0);
    assert(mi_cols % 2 == 0);

    memset(new_map_16x16, !cpi->active_map.enabled, rows * cols);
    if (cpi->active_map.enabled) {
      for (int r = 0; r < (mi_rows >> row_scale); ++r) {
        for (int c = 0; c < (mi_cols >> col_scale); ++c) {
          // Cyclic refresh segments are considered active despite not having
          // AM_SEGMENT_ID_ACTIVE
          uint8_t temp = 0;
          temp |= seg_map_8x8[(2 * r + 0) * mi_cols + (2 * c + 0)] !=
                  AM_SEGMENT_ID_INACTIVE;
          temp |= seg_map_8x8[(2 * r + 0) * mi_cols + (2 * c + 1)] !=
                  AM_SEGMENT_ID_INACTIVE;
          temp |= seg_map_8x8[(2 * r + 1) * mi_cols + (2 * c + 0)] !=
                  AM_SEGMENT_ID_INACTIVE;
          temp |= seg_map_8x8[(2 * r + 1) * mi_cols + (2 * c + 1)] !=
                  AM_SEGMENT_ID_INACTIVE;
          new_map_16x16[r * cols + c] |= temp;
        }
      }
    }
    return 0;
  }

  return -1;
}

void av1_initialize_enc(unsigned int usage, enum aom_rc_mode end_usage) {
  bool is_allintra = usage == ALLINTRA;

  av1_rtcd();
  aom_dsp_rtcd();
  aom_scale_rtcd();
  av1_init_intra_predictors();
  av1_init_me_luts();
  if (!is_allintra) av1_init_wedge_masks();
  if (!is_allintra || end_usage != AOM_Q) av1_rc_init_minq_luts();
}

void av1_new_framerate(AV1_COMP *cpi, double framerate) {
  cpi->framerate = framerate < 0.1 ? 30 : framerate;
  av1_rc_update_framerate(cpi, cpi->common.width, cpi->common.height);
}

double av1_get_compression_ratio(const AV1_COMMON *const cm,
                                 size_t encoded_frame_size) {
  const int upscaled_width = cm->superres_upscaled_width;
  const int height = cm->height;
  const int64_t luma_pic_size = (int64_t)upscaled_width * height;
  const SequenceHeader *const seq_params = cm->seq_params;
  const BITSTREAM_PROFILE profile = seq_params->profile;
  const int pic_size_profile_factor =
      profile == PROFILE_0 ? 15 : (profile == PROFILE_1 ? 30 : 36);
  encoded_frame_size =
      (encoded_frame_size > 129 ? encoded_frame_size - 128 : 1);
  const int64_t uncompressed_frame_size =
      (luma_pic_size * pic_size_profile_factor) >> 3;
  return (double)uncompressed_frame_size / encoded_frame_size;
}

static void auto_tile_size_balancing(AV1_COMMON *const cm, int num_sbs,
                                     int num_tiles_lg, int tile_col_row) {
  CommonTileParams *const tiles = &cm->tiles;
  int i, start_sb;
  int size_sb = num_sbs >> num_tiles_lg;
  int res_sbs = num_sbs - (size_sb << num_tiles_lg);
  int num_tiles = 1 << num_tiles_lg;
  int inc_index = num_tiles - res_sbs;

  tiles->uniform_spacing = 0;

  for (i = 0, start_sb = 0; start_sb < num_sbs && i < MAX_TILE_COLS; ++i) {
    if (i == inc_index) ++size_sb;
    if (tile_col_row)
      tiles->col_start_sb[i] = start_sb;
    else
      tiles->row_start_sb[i] = start_sb;

    start_sb += AOMMIN(size_sb, tiles->max_width_sb);
  }

  if (tile_col_row) {
    tiles->cols = i;
    tiles->col_start_sb[i] = num_sbs;
  } else {
    tiles->rows = i;
    tiles->row_start_sb[i] = num_sbs;
  }
}

static void set_tile_info(AV1_COMMON *const cm,
                          const TileConfig *const tile_cfg) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const SequenceHeader *const seq_params = cm->seq_params;
  CommonTileParams *const tiles = &cm->tiles;
  int i, start_sb;

  av1_get_tile_limits(cm);

  int sb_cols =
      CEIL_POWER_OF_TWO(mi_params->mi_cols, seq_params->mib_size_log2);
  // configure tile columns
  if (tile_cfg->tile_width_count == 0 || tile_cfg->tile_height_count == 0) {
    tiles->uniform_spacing = 1;
    tiles->log2_cols = AOMMAX(tile_cfg->tile_columns, tiles->min_log2_cols);
    // Add a special case to handle super resolution
    sb_cols = coded_to_superres_mi(sb_cols, cm->superres_scale_denominator);
    int min_log2_cols = 0;
    for (; (tiles->max_width_sb << min_log2_cols) <= sb_cols; ++min_log2_cols) {
    }
    tiles->log2_cols = AOMMAX(tiles->log2_cols, min_log2_cols);

    tiles->log2_cols = AOMMIN(tiles->log2_cols, tiles->max_log2_cols);
  } else if (tile_cfg->tile_widths[0] < 0) {
    auto_tile_size_balancing(cm, sb_cols, tile_cfg->tile_columns, 1);
  } else {
    int size_sb, j = 0;
    tiles->uniform_spacing = 0;
    for (i = 0, start_sb = 0; start_sb < sb_cols && i < MAX_TILE_COLS; i++) {
      tiles->col_start_sb[i] = start_sb;
      size_sb = tile_cfg->tile_widths[j++];
      if (j >= tile_cfg->tile_width_count) j = 0;
      start_sb += AOMMIN(size_sb, tiles->max_width_sb);
    }
    tiles->cols = i;
    tiles->col_start_sb[i] = sb_cols;
  }
  av1_calculate_tile_cols(seq_params, mi_params->mi_rows, mi_params->mi_cols,
                          tiles);

  // configure tile rows
  int sb_rows =
      CEIL_POWER_OF_TWO(mi_params->mi_rows, seq_params->mib_size_log2);
  if (tiles->uniform_spacing) {
    tiles->log2_rows = AOMMAX(tile_cfg->tile_rows, tiles->min_log2_rows);
    tiles->log2_rows = AOMMIN(tiles->log2_rows, tiles->max_log2_rows);
  } else if (tile_cfg->tile_heights[0] < 0) {
    auto_tile_size_balancing(cm, sb_rows, tile_cfg->tile_rows, 0);
  } else {
    int size_sb, j = 0;
    for (i = 0, start_sb = 0; start_sb < sb_rows && i < MAX_TILE_ROWS; i++) {
      tiles->row_start_sb[i] = start_sb;
      size_sb = tile_cfg->tile_heights[j++];
      if (j >= tile_cfg->tile_height_count) j = 0;
      start_sb += AOMMIN(size_sb, tiles->max_height_sb);
    }
    tiles->rows = i;
    tiles->row_start_sb[i] = sb_rows;
  }
  av1_calculate_tile_rows(seq_params, mi_params->mi_rows, tiles);
}

void av1_update_frame_size(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &cpi->td.mb.e_mbd;

  // Setup mi_params here in case we need more mi's.
  CommonModeInfoParams *const mi_params = &cm->mi_params;
  mi_params->set_mb_mi(mi_params, cm->width, cm->height,
                       cpi->sf.part_sf.default_min_partition_size);

  av1_init_macroblockd(cm, xd);

  if (!cpi->ppi->seq_params_locked)
    set_sb_size(cm->seq_params,
                av1_select_sb_size(&cpi->oxcf, cm->width, cm->height,
                                   cpi->ppi->number_spatial_layers));

  set_tile_info(cm, &cpi->oxcf.tile_cfg);
}

static inline int does_level_match(int width, int height, double fps,
                                   int lvl_width, int lvl_height,
                                   double lvl_fps, int lvl_dim_mult) {
  const int64_t lvl_luma_pels = (int64_t)lvl_width * lvl_height;
  const double lvl_display_sample_rate = lvl_luma_pels * lvl_fps;
  const int64_t luma_pels = (int64_t)width * height;
  const double display_sample_rate = luma_pels * fps;
  return luma_pels <= lvl_luma_pels &&
         display_sample_rate <= lvl_display_sample_rate &&
         width <= lvl_width * lvl_dim_mult &&
         height <= lvl_height * lvl_dim_mult;
}

static void set_bitstream_level_tier(AV1_PRIMARY *const ppi, int width,
                                     int height, double init_framerate) {
  SequenceHeader *const seq_params = &ppi->seq_params;
  const AV1LevelParams *const level_params = &ppi->level_params;
  // TODO(any): This is a placeholder function that only addresses dimensions
  // and max display sample rates.
  // Need to add checks for max bit rate, max decoded luma sample rate, header
  // rate, etc. that are not covered by this function.
  AV1_LEVEL level = SEQ_LEVEL_MAX;
  if (does_level_match(width, height, init_framerate, 512, 288, 30.0, 4)) {
    level = SEQ_LEVEL_2_0;
  } else if (does_level_match(width, height, init_framerate, 704, 396, 30.0,
                              4)) {
    level = SEQ_LEVEL_2_1;
  } else if (does_level_match(width, height, init_framerate, 1088, 612, 30.0,
                              4)) {
    level = SEQ_LEVEL_3_0;
  } else if (does_level_match(width, height, init_framerate, 1376, 774, 30.0,
                              4)) {
    level = SEQ_LEVEL_3_1;
  } else if (does_level_match(width, height, init_framerate, 2048, 1152, 30.0,
                              3)) {
    level = SEQ_LEVEL_4_0;
  } else if (does_level_match(width, height, init_framerate, 2048, 1152, 60.0,
                              3)) {
    level = SEQ_LEVEL_4_1;
  } else if (does_level_match(width, height, init_framerate, 4096, 2176, 30.0,
                              2)) {
    level = SEQ_LEVEL_5_0;
  } else if (does_level_match(width, height, init_framerate, 4096, 2176, 60.0,
                              2)) {
    level = SEQ_LEVEL_5_1;
  } else if (does_level_match(width, height, init_framerate, 4096, 2176, 120.0,
                              2)) {
    level = SEQ_LEVEL_5_2;
  } else if (does_level_match(width, height, init_framerate, 8192, 4352, 30.0,
                              2)) {
    level = SEQ_LEVEL_6_0;
  } else if (does_level_match(width, height, init_framerate, 8192, 4352, 60.0,
                              2)) {
    level = SEQ_LEVEL_6_1;
  } else if (does_level_match(width, height, init_framerate, 8192, 4352, 120.0,
                              2)) {
    level = SEQ_LEVEL_6_2;
  }
#if CONFIG_CWG_C013
  // TODO(bohanli): currently target level is only working for the 0th operating
  // point, so scalable coding is not supported.
  else if (level_params->target_seq_level_idx[0] >= SEQ_LEVEL_7_0 &&
           level_params->target_seq_level_idx[0] <= SEQ_LEVEL_8_3) {
    // Only use level 7.x to 8.x when explicitly asked to.
    if (does_level_match(width, height, init_framerate, 16384, 8704, 30.0, 2)) {
      level = SEQ_LEVEL_7_0;
    } else if (does_level_match(width, height, init_framerate, 16384, 8704,
                                60.0, 2)) {
      level = SEQ_LEVEL_7_1;
    } else if (does_level_match(width, height, init_framerate, 16384, 8704,
                                120.0, 2)) {
      level = SEQ_LEVEL_7_2;
    } else if (does_level_match(width, height, init_framerate, 32768, 17408,
                                30.0, 2)) {
      level = SEQ_LEVEL_8_0;
    } else if (does_level_match(width, height, init_framerate, 32768, 17408,
                                60.0, 2)) {
      level = SEQ_LEVEL_8_1;
    } else if (does_level_match(width, height, init_framerate, 32768, 17408,
                                120.0, 2)) {
      level = SEQ_LEVEL_8_2;
    }
  }
#endif

  for (int i = 0; i < MAX_NUM_OPERATING_POINTS; ++i) {
    assert(is_valid_seq_level_idx(level_params->target_seq_level_idx[i]) ||
           level_params->target_seq_level_idx[i] == SEQ_LEVEL_KEEP_STATS);
    // If a higher target level is specified, it is then used rather than the
    // inferred one from resolution and framerate.
    seq_params->seq_level_idx[i] =
        level_params->target_seq_level_idx[i] < SEQ_LEVELS &&
                level_params->target_seq_level_idx[i] > level
            ? level_params->target_seq_level_idx[i]
            : level;
    // Set the maximum parameters for bitrate and buffer size for this profile,
    // level, and tier
    seq_params->op_params[i].bitrate = av1_max_level_bitrate(
        seq_params->profile, seq_params->seq_level_idx[i], seq_params->tier[i]);
    // Level with seq_level_idx = 31 returns a high "dummy" bitrate to pass the
    // check
    if (seq_params->op_params[i].bitrate == 0)
      aom_internal_error(
          &ppi->error, AOM_CODEC_UNSUP_BITSTREAM,
          "AV1 does not support this combination of profile, level, and tier.");
    // Buffer size in bits/s is bitrate in bits/s * 1 s
    seq_params->op_params[i].buffer_size = seq_params->op_params[i].bitrate;
  }
}

static void init_seq_coding_tools(AV1_PRIMARY *const ppi,
                                  const AV1EncoderConfig *oxcf,
                                  int disable_frame_id_numbers) {
  SequenceHeader *const seq = &ppi->seq_params;
  const FrameDimensionCfg *const frm_dim_cfg = &oxcf->frm_dim_cfg;
  const ToolCfg *const tool_cfg = &oxcf->tool_cfg;

  seq->still_picture =
      !tool_cfg->force_video_mode && (oxcf->input_cfg.limit == 1);
  seq->reduced_still_picture_hdr =
      seq->still_picture && !tool_cfg->full_still_picture_hdr;
  seq->force_screen_content_tools = 2;
  seq->force_integer_mv = 2;
  seq->order_hint_info.enable_order_hint = tool_cfg->enable_order_hint;
  seq->frame_id_numbers_present_flag =
      !seq->reduced_still_picture_hdr &&
      !oxcf->tile_cfg.enable_large_scale_tile &&
      tool_cfg->error_resilient_mode && !disable_frame_id_numbers;
  if (seq->reduced_still_picture_hdr) {
    seq->order_hint_info.enable_order_hint = 0;
    seq->force_screen_content_tools = 2;
    seq->force_integer_mv = 2;
  }
  seq->order_hint_info.order_hint_bits_minus_1 =
      seq->order_hint_info.enable_order_hint
          ? DEFAULT_EXPLICIT_ORDER_HINT_BITS - 1
          : -1;

  seq->max_frame_width = frm_dim_cfg->forced_max_frame_width
                             ? frm_dim_cfg->forced_max_frame_width
                             : AOMMAX(seq->max_frame_width, frm_dim_cfg->width);
  seq->max_frame_height =
      frm_dim_cfg->forced_max_frame_height
          ? frm_dim_cfg->forced_max_frame_height
          : AOMMAX(seq->max_frame_height, frm_dim_cfg->height);
  seq->num_bits_width =
      (seq->max_frame_width > 1) ? get_msb(seq->max_frame_width - 1) + 1 : 1;
  seq->num_bits_height =
      (seq->max_frame_height > 1) ? get_msb(seq->max_frame_height - 1) + 1 : 1;
  assert(seq->num_bits_width <= 16);
  assert(seq->num_bits_height <= 16);

  seq->frame_id_length = FRAME_ID_LENGTH;
  seq->delta_frame_id_length = DELTA_FRAME_ID_LENGTH;

  seq->enable_dual_filter = tool_cfg->enable_dual_filter;
  seq->order_hint_info.enable_dist_wtd_comp =
      oxcf->comp_type_cfg.enable_dist_wtd_comp;
  seq->order_hint_info.enable_dist_wtd_comp &=
      seq->order_hint_info.enable_order_hint;
  seq->order_hint_info.enable_ref_frame_mvs = tool_cfg->ref_frame_mvs_present;
  seq->order_hint_info.enable_ref_frame_mvs &=
      seq->order_hint_info.enable_order_hint;
  seq->enable_superres = oxcf->superres_cfg.enable_superres;
  seq->enable_cdef = tool_cfg->cdef_control != CDEF_NONE ? 1 : 0;
  seq->enable_restoration = tool_cfg->enable_restoration;
  seq->enable_warped_motion = oxcf->motion_mode_cfg.enable_warped_motion;
  seq->enable_interintra_compound = tool_cfg->enable_interintra_comp;
  seq->enable_masked_compound = oxcf->comp_type_cfg.enable_masked_comp;
  seq->enable_intra_edge_filter = oxcf->intra_mode_cfg.enable_intra_edge_filter;
  seq->enable_filter_intra = oxcf->intra_mode_cfg.enable_filter_intra;

  set_bitstream_level_tier(ppi, frm_dim_cfg->width, frm_dim_cfg->height,
                           oxcf->input_cfg.init_framerate);

  if (seq->operating_points_cnt_minus_1 == 0) {
    seq->operating_point_idc[0] = 0;
    seq->has_nonzero_operating_point_idc = false;
  } else {
    // Set operating_point_idc[] such that the i=0 point corresponds to the
    // highest quality operating point (all layers), and subsequent
    // operarting points (i > 0) are lower quality corresponding to
    // skip decoding enhancement  layers (temporal first).
    int i = 0;
    assert(seq->operating_points_cnt_minus_1 ==
           (int)(ppi->number_spatial_layers * ppi->number_temporal_layers - 1));
    for (unsigned int sl = 0; sl < ppi->number_spatial_layers; sl++) {
      for (unsigned int tl = 0; tl < ppi->number_temporal_layers; tl++) {
        seq->operating_point_idc[i] =
            (~(~0u << (ppi->number_spatial_layers - sl)) << 8) |
            ~(~0u << (ppi->number_temporal_layers - tl));
        assert(seq->operating_point_idc[i] != 0);
        i++;
      }
    }
    seq->has_nonzero_operating_point_idc = true;
  }
}

static void init_config_sequence(struct AV1_PRIMARY *ppi,
                                 const AV1EncoderConfig *oxcf) {
  SequenceHeader *const seq_params = &ppi->seq_params;
  const DecoderModelCfg *const dec_model_cfg = &oxcf->dec_model_cfg;
  const ColorCfg *const color_cfg = &oxcf->color_cfg;

  ppi->use_svc = 0;
  ppi->number_spatial_layers = 1;
  ppi->number_temporal_layers = 1;

  seq_params->profile = oxcf->profile;
  seq_params->bit_depth = oxcf->tool_cfg.bit_depth;
  seq_params->use_highbitdepth = oxcf->use_highbitdepth;
  seq_params->color_primaries = color_cfg->color_primaries;
  seq_params->transfer_characteristics = color_cfg->transfer_characteristics;
  seq_params->matrix_coefficients = color_cfg->matrix_coefficients;
  seq_params->monochrome = oxcf->tool_cfg.enable_monochrome;
  seq_params->chroma_sample_position = color_cfg->chroma_sample_position;
  seq_params->color_range = color_cfg->color_range;
  seq_params->timing_info_present = dec_model_cfg->timing_info_present;
  seq_params->timing_info.num_units_in_display_tick =
      dec_model_cfg->timing_info.num_units_in_display_tick;
  seq_params->timing_info.time_scale = dec_model_cfg->timing_info.time_scale;
  seq_params->timing_info.equal_picture_interval =
      dec_model_cfg->timing_info.equal_picture_interval;
  seq_params->timing_info.num_ticks_per_picture =
      dec_model_cfg->timing_info.num_ticks_per_picture;

  seq_params->display_model_info_present_flag =
      dec_model_cfg->display_model_info_present_flag;
  seq_params->decoder_model_info_present_flag =
      dec_model_cfg->decoder_model_info_present_flag;
  if (dec_model_cfg->decoder_model_info_present_flag) {
    // set the decoder model parameters in schedule mode
    seq_params->decoder_model_info.num_units_in_decoding_tick =
        dec_model_cfg->num_units_in_decoding_tick;
    ppi->buffer_removal_time_present = 1;
    av1_set_aom_dec_model_info(&seq_params->decoder_model_info);
    av1_set_dec_model_op_parameters(&seq_params->op_params[0]);
  } else if (seq_params->timing_info_present &&
             seq_params->timing_info.equal_picture_interval &&
             !seq_params->decoder_model_info_present_flag) {
    // set the decoder model parameters in resource availability mode
    av1_set_resource_availability_parameters(&seq_params->op_params[0]);
  } else {
    seq_params->op_params[0].initial_display_delay =
        10;  // Default value (not signaled)
  }

  if (seq_params->monochrome) {
    seq_params->subsampling_x = 1;
    seq_params->subsampling_y = 1;
  } else if (seq_params->color_primaries == AOM_CICP_CP_BT_709 &&
             seq_params->transfer_characteristics == AOM_CICP_TC_SRGB &&
             seq_params->matrix_coefficients == AOM_CICP_MC_IDENTITY) {
    seq_params->subsampling_x = 0;
    seq_params->subsampling_y = 0;
  } else {
    if (seq_params->profile == 0) {
      seq_params->subsampling_x = 1;
      seq_params->subsampling_y = 1;
    } else if (seq_params->profile == 1) {
      seq_params->subsampling_x = 0;
      seq_params->subsampling_y = 0;
    } else {
      if (seq_params->bit_depth == AOM_BITS_12) {
        seq_params->subsampling_x = oxcf->input_cfg.chroma_subsampling_x;
        seq_params->subsampling_y = oxcf->input_cfg.chroma_subsampling_y;
      } else {
        seq_params->subsampling_x = 1;
        seq_params->subsampling_y = 0;
      }
    }
  }
  av1_change_config_seq(ppi, oxcf, NULL);
}

static void init_config(struct AV1_COMP *cpi, const AV1EncoderConfig *oxcf) {
  AV1_COMMON *const cm = &cpi->common;
  ResizePendingParams *resize_pending_params = &cpi->resize_pending_params;

  cpi->oxcf = *oxcf;
  cpi->framerate = oxcf->input_cfg.init_framerate;

  cm->width = oxcf->frm_dim_cfg.width;
  cm->height = oxcf->frm_dim_cfg.height;
  cpi->is_dropped_frame = false;

  alloc_compressor_data(cpi);

  cpi->data_alloc_width = cm->width;
  cpi->data_alloc_height = cm->height;
  cpi->frame_size_related_setup_done = false;

  // Single thread case: use counts in common.
  cpi->td.counts = &cpi->counts;

  // Init SVC parameters.
  cpi->svc.number_spatial_layers = 1;
  cpi->svc.number_temporal_layers = 1;
  cm->spatial_layer_id = 0;
  cm->temporal_layer_id = 0;
  // Init rtc_ref parameters.
  cpi->ppi->rtc_ref.set_ref_frame_config = 0;
  cpi->ppi->rtc_ref.non_reference_frame = 0;
  cpi->ppi->rtc_ref.ref_frame_comp[0] = 0;
  cpi->ppi->rtc_ref.ref_frame_comp[1] = 0;
  cpi->ppi->rtc_ref.ref_frame_comp[2] = 0;

  // change includes all joint functionality
  av1_change_config(cpi, oxcf, false);

  cpi->ref_frame_flags = 0;

  // Reset resize pending flags
  resize_pending_params->width = 0;
  resize_pending_params->height = 0;

  // Setup identity scale factor
  av1_setup_scale_factors_for_frame(&cm->sf_identity, 1, 1, 1, 1);

  init_buffer_indices(&cpi->force_intpel_info, cm->remapped_ref_idx);

  av1_noise_estimate_init(&cpi->noise_estimate, cm->width, cm->height);
}

void av1_change_config_seq(struct AV1_PRIMARY *ppi,
                           const AV1EncoderConfig *oxcf,
                           bool *is_sb_size_changed) {
  SequenceHeader *const seq_params = &ppi->seq_params;
  const FrameDimensionCfg *const frm_dim_cfg = &oxcf->frm_dim_cfg;
  const DecoderModelCfg *const dec_model_cfg = &oxcf->dec_model_cfg;
  const ColorCfg *const color_cfg = &oxcf->color_cfg;

  if (seq_params->profile != oxcf->profile) seq_params->profile = oxcf->profile;
  seq_params->bit_depth = oxcf->tool_cfg.bit_depth;
  seq_params->color_primaries = color_cfg->color_primaries;
  seq_params->transfer_characteristics = color_cfg->transfer_characteristics;
  seq_params->matrix_coefficients = color_cfg->matrix_coefficients;
  seq_params->monochrome = oxcf->tool_cfg.enable_monochrome;
  seq_params->chroma_sample_position = color_cfg->chroma_sample_position;
  seq_params->color_range = color_cfg->color_range;

  assert(IMPLIES(seq_params->profile <= PROFILE_1,
                 seq_params->bit_depth <= AOM_BITS_10));

  seq_params->timing_info_present = dec_model_cfg->timing_info_present;
  seq_params->timing_info.num_units_in_display_tick =
      dec_model_cfg->timing_info.num_units_in_display_tick;
  seq_params->timing_info.time_scale = dec_model_cfg->timing_info.time_scale;
  seq_params->timing_info.equal_picture_interval =
      dec_model_cfg->timing_info.equal_picture_interval;
  seq_params->timing_info.num_ticks_per_picture =
      dec_model_cfg->timing_info.num_ticks_per_picture;

  seq_params->display_model_info_present_flag =
      dec_model_cfg->display_model_info_present_flag;
  seq_params->decoder_model_info_present_flag =
      dec_model_cfg->decoder_model_info_present_flag;
  if (dec_model_cfg->decoder_model_info_present_flag) {
    // set the decoder model parameters in schedule mode
    seq_params->decoder_model_info.num_units_in_decoding_tick =
        dec_model_cfg->num_units_in_decoding_tick;
    ppi->buffer_removal_time_present = 1;
    av1_set_aom_dec_model_info(&seq_params->decoder_model_info);
    av1_set_dec_model_op_parameters(&seq_params->op_params[0]);
  } else if (seq_params->timing_info_present &&
             seq_params->timing_info.equal_picture_interval &&
             !seq_params->decoder_model_info_present_flag) {
    // set the decoder model parameters in resource availability mode
    av1_set_resource_availability_parameters(&seq_params->op_params[0]);
  } else {
    seq_params->op_params[0].initial_display_delay =
        10;  // Default value (not signaled)
  }

#if !CONFIG_REALTIME_ONLY
  av1_update_film_grain_parameters_seq(ppi, oxcf);
#endif

  int sb_size = seq_params->sb_size;
  // Superblock size should not be updated after the first key frame.
  if (!ppi->seq_params_locked) {
    set_sb_size(seq_params, av1_select_sb_size(oxcf, frm_dim_cfg->width,
                                               frm_dim_cfg->height,
                                               ppi->number_spatial_layers));
    for (int i = 0; i < MAX_NUM_OPERATING_POINTS; ++i)
      seq_params->tier[i] = (oxcf->tier_mask >> i) & 1;
  }
  if (is_sb_size_changed != NULL && sb_size != seq_params->sb_size)
    *is_sb_size_changed = true;

  // Init sequence level coding tools
  // This should not be called after the first key frame.
  if (!ppi->seq_params_locked) {
    seq_params->operating_points_cnt_minus_1 =
        (ppi->number_spatial_layers > 1 || ppi->number_temporal_layers > 1)
            ? ppi->number_spatial_layers * ppi->number_temporal_layers - 1
            : 0;
    init_seq_coding_tools(ppi, oxcf,
                          ppi->use_svc || ppi->rtc_ref.set_ref_frame_config);
  }
  seq_params->timing_info_present &= !seq_params->reduced_still_picture_hdr;

#if CONFIG_AV1_HIGHBITDEPTH
  highbd_set_var_fns(ppi);
#endif

  set_primary_rc_buffer_sizes(oxcf, ppi);
}

void av1_change_config(struct AV1_COMP *cpi, const AV1EncoderConfig *oxcf,
                       bool is_sb_size_changed) {
  AV1_COMMON *const cm = &cpi->common;
  SequenceHeader *const seq_params = cm->seq_params;
  RATE_CONTROL *const rc = &cpi->rc;
  PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  MACROBLOCK *const x = &cpi->td.mb;
  AV1LevelParams *const level_params = &cpi->ppi->level_params;
  RefreshFrameInfo *const refresh_frame = &cpi->refresh_frame;
  const FrameDimensionCfg *const frm_dim_cfg = &cpi->oxcf.frm_dim_cfg;
  const RateControlCfg *const rc_cfg = &oxcf->rc_cfg;
  FeatureFlags *const features = &cm->features;

  // in case of LAP, lag in frames is set according to number of lap buffers
  // calculated at init time. This stores and restores LAP's lag in frames to
  // prevent override by new cfg.
  int lap_lag_in_frames = -1;
  if (cpi->ppi->lap_enabled && cpi->compressor_stage == LAP_STAGE) {
    lap_lag_in_frames = cpi->oxcf.gf_cfg.lag_in_frames;
  }

  cpi->oxcf = *oxcf;

#if !CONFIG_REALTIME_ONLY
  av1_update_film_grain_parameters(cpi, oxcf);
#endif

  // When user provides superres_mode = AOM_SUPERRES_AUTO, we still initialize
  // superres mode for current encoding = AOM_SUPERRES_NONE. This is to ensure
  // that any analysis (e.g. TPL) happening outside the main encoding loop still
  // happens at full resolution.
  // This value will later be set appropriately just before main encoding loop.
  cpi->superres_mode = oxcf->superres_cfg.superres_mode == AOM_SUPERRES_AUTO
                           ? AOM_SUPERRES_NONE
                           : oxcf->superres_cfg.superres_mode;  // default
  x->e_mbd.bd = (int)seq_params->bit_depth;
  x->e_mbd.global_motion = cm->global_motion;

  memcpy(level_params->target_seq_level_idx, cpi->oxcf.target_seq_level_idx,
         sizeof(level_params->target_seq_level_idx));
  level_params->keep_level_stats = 0;
  for (int i = 0; i < MAX_NUM_OPERATING_POINTS; ++i) {
    if (level_params->target_seq_level_idx[i] < SEQ_LEVELS ||
        level_params->target_seq_level_idx[i] == SEQ_LEVEL_KEEP_STATS) {
      level_params->keep_level_stats |= 1u << i;
      if (!level_params->level_info[i]) {
        CHECK_MEM_ERROR(cm, level_params->level_info[i],
                        aom_calloc(1, sizeof(*level_params->level_info[i])));
      }
    }
  }

  // TODO(huisu@): level targeting currently only works for the 0th operating
  // point, so scalable coding is not supported yet.
  if (level_params->target_seq_level_idx[0] < SEQ_LEVELS) {
    // Adjust encoder config in order to meet target level.
    config_target_level(cpi, level_params->target_seq_level_idx[0],
                        seq_params->tier[0]);
  }

  if (has_no_stats_stage(cpi) && (rc_cfg->mode == AOM_Q)) {
    p_rc->baseline_gf_interval = FIXED_GF_INTERVAL;
  } else if (!is_one_pass_rt_params(cpi) ||
             cm->current_frame.frame_number == 0) {
    // For rtc mode: logic for setting the baseline_gf_interval is done
    // in av1_get_one_pass_rt_params(), and it should not be reset here in
    // change_config(), unless after init_config (first frame).
    p_rc->baseline_gf_interval = (MIN_GF_INTERVAL + MAX_GF_INTERVAL) / 2;
  }

  refresh_frame->golden_frame = false;
  refresh_frame->bwd_ref_frame = false;

  features->refresh_frame_context =
      (oxcf->tool_cfg.frame_parallel_decoding_mode)
          ? REFRESH_FRAME_CONTEXT_DISABLED
          : REFRESH_FRAME_CONTEXT_BACKWARD;
  if (oxcf->tile_cfg.enable_large_scale_tile)
    features->refresh_frame_context = REFRESH_FRAME_CONTEXT_DISABLED;

  if (x->palette_buffer == NULL) {
    CHECK_MEM_ERROR(cm, x->palette_buffer,
                    aom_memalign(16, sizeof(*x->palette_buffer)));
  }

  if (x->tmp_conv_dst == NULL) {
    CHECK_MEM_ERROR(
        cm, x->tmp_conv_dst,
        aom_memalign(32, MAX_SB_SIZE * MAX_SB_SIZE * sizeof(*x->tmp_conv_dst)));
    x->e_mbd.tmp_conv_dst = x->tmp_conv_dst;
  }
  // The buffers 'tmp_pred_bufs[]' and 'comp_rd_buffer' are used in inter frames
  // to store intermediate inter mode prediction results and are not required
  // for allintra encoding mode. Hence, the memory allocations for these buffers
  // are avoided for allintra encoding mode.
  if (cpi->oxcf.kf_cfg.key_freq_max != 0) {
    if (x->comp_rd_buffer.pred0 == NULL)
      alloc_compound_type_rd_buffers(cm->error, &x->comp_rd_buffer);

    for (int i = 0; i < 2; ++i) {
      if (x->tmp_pred_bufs[i] == NULL) {
        CHECK_MEM_ERROR(cm, x->tmp_pred_bufs[i],
                        aom_memalign(32, 2 * MAX_MB_PLANE * MAX_SB_SQUARE *
                                             sizeof(*x->tmp_pred_bufs[i])));
        x->e_mbd.tmp_obmc_bufs[i] = x->tmp_pred_bufs[i];
      }
    }
  }

  av1_reset_segment_features(cm);

  av1_set_high_precision_mv(cpi, 1, 0);

  // Under a configuration change, where maximum_buffer_size may change,
  // keep buffer level clipped to the maximum allowed buffer size.
  p_rc->bits_off_target =
      AOMMIN(p_rc->bits_off_target, p_rc->maximum_buffer_size);
  p_rc->buffer_level = AOMMIN(p_rc->buffer_level, p_rc->maximum_buffer_size);

  // Set up frame rate and related parameters rate control values.
  av1_new_framerate(cpi, cpi->framerate);

  // Set absolute upper and lower quality limits
  rc->worst_quality = rc_cfg->worst_allowed_q;
  rc->best_quality = rc_cfg->best_allowed_q;

  // If lossless has been requested make sure average Q accumulators are reset.
  if (is_lossless_requested(&cpi->oxcf.rc_cfg)) {
    int i;
    for (i = 0; i < FRAME_TYPES; ++i) {
      p_rc->avg_frame_qindex[i] = 0;
    }
  }

  features->interp_filter =
      oxcf->tile_cfg.enable_large_scale_tile ? EIGHTTAP_REGULAR : SWITCHABLE;
  features->switchable_motion_mode = is_switchable_motion_mode_allowed(
      features->allow_warped_motion, oxcf->motion_mode_cfg.enable_obmc);

  if (frm_dim_cfg->render_width > 0 && frm_dim_cfg->render_height > 0) {
    cm->render_width = frm_dim_cfg->render_width;
    cm->render_height = frm_dim_cfg->render_height;
  } else {
    cm->render_width = frm_dim_cfg->width;
    cm->render_height = frm_dim_cfg->height;
  }

  int last_width = cm->width;
  int last_height = cm->height;
  cm->width = frm_dim_cfg->width;
  cm->height = frm_dim_cfg->height;

  if (cm->width > cpi->data_alloc_width ||
      cm->height > cpi->data_alloc_height || is_sb_size_changed) {
    av1_free_context_buffers(cm);
    av1_free_shared_coeff_buffer(&cpi->td.shared_coeff_buf);
    av1_free_sms_tree(&cpi->td);
    av1_free_pmc(cpi->td.firstpass_ctx, av1_num_planes(cm));
    cpi->td.firstpass_ctx = NULL;
    alloc_compressor_data(cpi);
    realloc_segmentation_maps(cpi);
    cpi->data_alloc_width = cm->width;
    cpi->data_alloc_height = cm->height;
    cpi->frame_size_related_setup_done = false;
  }
  av1_update_frame_size(cpi);

  if (cm->width != last_width || cm->height != last_height) {
    if (cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ) {
      int mi_rows = cpi->common.mi_params.mi_rows;
      int mi_cols = cpi->common.mi_params.mi_cols;
      aom_free(cpi->cyclic_refresh->map);
      CHECK_MEM_ERROR(
          cm, cpi->cyclic_refresh->map,
          aom_calloc(mi_rows * mi_cols, sizeof(*cpi->cyclic_refresh->map)));
      if (cpi->svc.number_spatial_layers > 1) {
        for (int sl = 0; sl < cpi->svc.number_spatial_layers; ++sl) {
          const int layer =
              LAYER_IDS_TO_IDX(sl, 0, cpi->svc.number_temporal_layers);
          LAYER_CONTEXT *const lc = &cpi->svc.layer_context[layer];
          lc->sb_index = 0;
          lc->actual_num_seg1_blocks = 0;
          lc->actual_num_seg2_blocks = 0;
          lc->counter_encode_maxq_scene_change = 0;
          aom_free(lc->map);
          CHECK_MEM_ERROR(cm, lc->map,
                          aom_calloc(mi_rows * mi_cols, sizeof(*lc->map)));
        }
      }
    }
  }

  rc->is_src_frame_alt_ref = 0;

  if (!cpi->ppi->rtc_ref.set_ref_frame_config)
    cpi->ext_flags.refresh_frame.update_pending = 0;
  cpi->ext_flags.refresh_frame_context_pending = 0;

  if (cpi->ppi->use_svc)
    av1_update_layer_context_change_config(cpi, rc_cfg->target_bandwidth);

  check_reset_rc_flag(cpi);

  // restore the value of lag_in_frame for LAP stage.
  if (lap_lag_in_frames != -1) {
    cpi->oxcf.gf_cfg.lag_in_frames = lap_lag_in_frames;
  }

#if CONFIG_REALTIME_ONLY
  assert(!oxcf->tool_cfg.enable_global_motion);
  cpi->alloc_pyramid = false;
#else
  cpi->alloc_pyramid = oxcf->tool_cfg.enable_global_motion;
#endif  // CONFIG_REALTIME_ONLY
}

static inline void init_frame_info(FRAME_INFO *frame_info,
                                   const AV1_COMMON *const cm) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const SequenceHeader *const seq_params = cm->seq_params;
  frame_info->frame_width = cm->width;
  frame_info->frame_height = cm->height;
  frame_info->mi_cols = mi_params->mi_cols;
  frame_info->mi_rows = mi_params->mi_rows;
  frame_info->mb_cols = mi_params->mb_cols;
  frame_info->mb_rows = mi_params->mb_rows;
  frame_info->num_mbs = mi_params->MBs;
  frame_info->bit_depth = seq_params->bit_depth;
  frame_info->subsampling_x = seq_params->subsampling_x;
  frame_info->subsampling_y = seq_params->subsampling_y;
}

static inline void init_frame_index_set(FRAME_INDEX_SET *frame_index_set) {
  frame_index_set->show_frame_count = 0;
}

static inline void update_counters_for_show_frame(AV1_COMP *const cpi) {
  assert(cpi->common.show_frame);
  cpi->frame_index_set.show_frame_count++;
  cpi->common.current_frame.frame_number++;
}

AV1_PRIMARY *av1_create_primary_compressor(
    struct aom_codec_pkt_list *pkt_list_head, int num_lap_buffers,
    const AV1EncoderConfig *oxcf) {
  AV1_PRIMARY *volatile const ppi = aom_memalign(32, sizeof(AV1_PRIMARY));
  if (!ppi) return NULL;
  av1_zero(*ppi);

  // The jmp_buf is valid only for the duration of the function that calls
  // setjmp(). Therefore, this function must reset the 'setjmp' field to 0
  // before it returns.
  if (setjmp(ppi->error.jmp)) {
    ppi->error.setjmp = 0;
    av1_remove_primary_compressor(ppi);
    return 0;
  }
  ppi->error.setjmp = 1;

  ppi->seq_params_locked = 0;
  ppi->lap_enabled = num_lap_buffers > 0;
  ppi->output_pkt_list = pkt_list_head;
  ppi->b_calculate_psnr = CONFIG_INTERNAL_STATS;
  ppi->frames_left = oxcf->input_cfg.limit;
  ppi->num_fp_contexts = 1;

  init_config_sequence(ppi, oxcf);

#if CONFIG_ENTROPY_STATS
  av1_zero(ppi->aggregate_fc);
#endif  // CONFIG_ENTROPY_STATS

  av1_primary_rc_init(oxcf, &ppi->p_rc);

  // For two pass and lag_in_frames > 33 in LAP.
  ppi->p_rc.enable_scenecut_detection = ENABLE_SCENECUT_MODE_2;
  if (ppi->lap_enabled) {
    if ((num_lap_buffers <
         (MAX_GF_LENGTH_LAP + SCENE_CUT_KEY_TEST_INTERVAL + 1)) &&
        num_lap_buffers >= (MAX_GF_LENGTH_LAP + 3)) {
      /*
       * For lag in frames >= 19 and <33, enable scenecut
       * with limited future frame prediction.
       */
      ppi->p_rc.enable_scenecut_detection = ENABLE_SCENECUT_MODE_1;
    } else if (num_lap_buffers < (MAX_GF_LENGTH_LAP + 3)) {
      // Disable scenecut when lag_in_frames < 19.
      ppi->p_rc.enable_scenecut_detection = DISABLE_SCENECUT;
    }
  }

#define BFP(BT, SDF, SDAF, VF, SVF, SVAF, SDX4DF, SDX3DF) \
  ppi->fn_ptr[BT].sdf = SDF;                              \
  ppi->fn_ptr[BT].sdaf = SDAF;                            \
  ppi->fn_ptr[BT].vf = VF;                                \
  ppi->fn_ptr[BT].svf = SVF;                              \
  ppi->fn_ptr[BT].svaf = SVAF;                            \
  ppi->fn_ptr[BT].sdx4df = SDX4DF;                        \
  ppi->fn_ptr[BT].sdx3df = SDX3DF;

// Realtime mode doesn't use 4x rectangular blocks.
#if !CONFIG_REALTIME_ONLY
  // sdaf (used in compound prediction, get_mvpred_compound_sad()) is unused
  // for 4xN and Nx4 blocks.
  BFP(BLOCK_4X16, aom_sad4x16, /*SDAF=*/NULL, aom_variance4x16,
      aom_sub_pixel_variance4x16, aom_sub_pixel_avg_variance4x16,
      aom_sad4x16x4d, aom_sad4x16x3d)

  // sdaf (used in compound prediction, get_mvpred_compound_sad()) is unused
  // for 4xN and Nx4 blocks.
  BFP(BLOCK_16X4, aom_sad16x4, /*SDAF=*/NULL, aom_variance16x4,
      aom_sub_pixel_variance16x4, aom_sub_pixel_avg_variance16x4,
      aom_sad16x4x4d, aom_sad16x4x3d)

  BFP(BLOCK_8X32, aom_sad8x32, aom_sad8x32_avg, aom_variance8x32,
      aom_sub_pixel_variance8x32, aom_sub_pixel_avg_variance8x32,
      aom_sad8x32x4d, aom_sad8x32x3d)

  BFP(BLOCK_32X8, aom_sad32x8, aom_sad32x8_avg, aom_variance32x8,
      aom_sub_pixel_variance32x8, aom_sub_pixel_avg_variance32x8,
      aom_sad32x8x4d, aom_sad32x8x3d)

  BFP(BLOCK_16X64, aom_sad16x64, aom_sad16x64_avg, aom_variance16x64,
      aom_sub_pixel_variance16x64, aom_sub_pixel_avg_variance16x64,
      aom_sad16x64x4d, aom_sad16x64x3d)

  BFP(BLOCK_64X16, aom_sad64x16, aom_sad64x16_avg, aom_variance64x16,
      aom_sub_pixel_variance64x16, aom_sub_pixel_avg_variance64x16,
      aom_sad64x16x4d, aom_sad64x16x3d)
#endif  // !CONFIG_REALTIME_ONLY

  BFP(BLOCK_128X128, aom_sad128x128, aom_sad128x128_avg, aom_variance128x128,
      aom_sub_pixel_variance128x128, aom_sub_pixel_avg_variance128x128,
      aom_sad128x128x4d, aom_sad128x128x3d)

  BFP(BLOCK_128X64, aom_sad128x64, aom_sad128x64_avg, aom_variance128x64,
      aom_sub_pixel_variance128x64, aom_sub_pixel_avg_variance128x64,
      aom_sad128x64x4d, aom_sad128x64x3d)

  BFP(BLOCK_64X128, aom_sad64x128, aom_sad64x128_avg, aom_variance64x128,
      aom_sub_pixel_variance64x128, aom_sub_pixel_avg_variance64x128,
      aom_sad64x128x4d, aom_sad64x128x3d)

  BFP(BLOCK_32X16, aom_sad32x16, aom_sad32x16_avg, aom_variance32x16,
      aom_sub_pixel_variance32x16, aom_sub_pixel_avg_variance32x16,
      aom_sad32x16x4d, aom_sad32x16x3d)

  BFP(BLOCK_16X32, aom_sad16x32, aom_sad16x32_avg, aom_variance16x32,
      aom_sub_pixel_variance16x32, aom_sub_pixel_avg_variance16x32,
      aom_sad16x32x4d, aom_sad16x32x3d)

  BFP(BLOCK_64X32, aom_sad64x32, aom_sad64x32_avg, aom_variance64x32,
      aom_sub_pixel_variance64x32, aom_sub_pixel_avg_variance64x32,
      aom_sad64x32x4d, aom_sad64x32x3d)

  BFP(BLOCK_32X64, aom_sad32x64, aom_sad32x64_avg, aom_variance32x64,
      aom_sub_pixel_variance32x64, aom_sub_pixel_avg_variance32x64,
      aom_sad32x64x4d, aom_sad32x64x3d)

  BFP(BLOCK_32X32, aom_sad32x32, aom_sad32x32_avg, aom_variance32x32,
      aom_sub_pixel_variance32x32, aom_sub_pixel_avg_variance32x32,
      aom_sad32x32x4d, aom_sad32x32x3d)

  BFP(BLOCK_64X64, aom_sad64x64, aom_sad64x64_avg, aom_variance64x64,
      aom_sub_pixel_variance64x64, aom_sub_pixel_avg_variance64x64,
      aom_sad64x64x4d, aom_sad64x64x3d)

  BFP(BLOCK_16X16, aom_sad16x16, aom_sad16x16_avg, aom_variance16x16,
      aom_sub_pixel_variance16x16, aom_sub_pixel_avg_variance16x16,
      aom_sad16x16x4d, aom_sad16x16x3d)

  BFP(BLOCK_16X8, aom_sad16x8, aom_sad16x8_avg, aom_variance16x8,
      aom_sub_pixel_variance16x8, aom_sub_pixel_avg_variance16x8,
      aom_sad16x8x4d, aom_sad16x8x3d)

  BFP(BLOCK_8X16, aom_sad8x16, aom_sad8x16_avg, aom_variance8x16,
      aom_sub_pixel_variance8x16, aom_sub_pixel_avg_variance8x16,
      aom_sad8x16x4d, aom_sad8x16x3d)

  BFP(BLOCK_8X8, aom_sad8x8, aom_sad8x8_avg, aom_variance8x8,
      aom_sub_pixel_variance8x8, aom_sub_pixel_avg_variance8x8, aom_sad8x8x4d,
      aom_sad8x8x3d)

  // sdaf (used in compound prediction, get_mvpred_compound_sad()) is unused
  // for 4xN and Nx4 blocks.
  BFP(BLOCK_8X4, aom_sad8x4, /*SDAF=*/NULL, aom_variance8x4,
      aom_sub_pixel_variance8x4, aom_sub_pixel_avg_variance8x4, aom_sad8x4x4d,
      aom_sad8x4x3d)

  // sdaf (used in compound prediction, get_mvpred_compound_sad()) is unused
  // for 4xN and Nx4 blocks.
  BFP(BLOCK_4X8, aom_sad4x8, /*SDAF=*/NULL, aom_variance4x8,
      aom_sub_pixel_variance4x8, aom_sub_pixel_avg_variance4x8, aom_sad4x8x4d,
      aom_sad4x8x3d)

  // sdaf (used in compound prediction, get_mvpred_compound_sad()) is unused
  // for 4xN and Nx4 blocks.
  BFP(BLOCK_4X4, aom_sad4x4, /*SDAF=*/NULL, aom_variance4x4,
      aom_sub_pixel_variance4x4, aom_sub_pixel_avg_variance4x4, aom_sad4x4x4d,
      aom_sad4x4x3d)

#if !CONFIG_REALTIME_ONLY
#define OBFP(BT, OSDF, OVF, OSVF) \
  ppi->fn_ptr[BT].osdf = OSDF;    \
  ppi->fn_ptr[BT].ovf = OVF;      \
  ppi->fn_ptr[BT].osvf = OSVF;

  OBFP(BLOCK_128X128, aom_obmc_sad128x128, aom_obmc_variance128x128,
       aom_obmc_sub_pixel_variance128x128)
  OBFP(BLOCK_128X64, aom_obmc_sad128x64, aom_obmc_variance128x64,
       aom_obmc_sub_pixel_variance128x64)
  OBFP(BLOCK_64X128, aom_obmc_sad64x128, aom_obmc_variance64x128,
       aom_obmc_sub_pixel_variance64x128)
  OBFP(BLOCK_64X64, aom_obmc_sad64x64, aom_obmc_variance64x64,
       aom_obmc_sub_pixel_variance64x64)
  OBFP(BLOCK_64X32, aom_obmc_sad64x32, aom_obmc_variance64x32,
       aom_obmc_sub_pixel_variance64x32)
  OBFP(BLOCK_32X64, aom_obmc_sad32x64, aom_obmc_variance32x64,
       aom_obmc_sub_pixel_variance32x64)
  OBFP(BLOCK_32X32, aom_obmc_sad32x32, aom_obmc_variance32x32,
       aom_obmc_sub_pixel_variance32x32)
  OBFP(BLOCK_32X16, aom_obmc_sad32x16, aom_obmc_variance32x16,
       aom_obmc_sub_pixel_variance32x16)
  OBFP(BLOCK_16X32, aom_obmc_sad16x32, aom_obmc_variance16x32,
       aom_obmc_sub_pixel_variance16x32)
  OBFP(BLOCK_16X16, aom_obmc_sad16x16, aom_obmc_variance16x16,
       aom_obmc_sub_pixel_variance16x16)
  OBFP(BLOCK_16X8, aom_obmc_sad16x8, aom_obmc_variance16x8,
       aom_obmc_sub_pixel_variance16x8)
  OBFP(BLOCK_8X16, aom_obmc_sad8x16, aom_obmc_variance8x16,
       aom_obmc_sub_pixel_variance8x16)
  OBFP(BLOCK_8X8, aom_obmc_sad8x8, aom_obmc_variance8x8,
       aom_obmc_sub_pixel_variance8x8)
  OBFP(BLOCK_4X8, aom_obmc_sad4x8, aom_obmc_variance4x8,
       aom_obmc_sub_pixel_variance4x8)
  OBFP(BLOCK_8X4, aom_obmc_sad8x4, aom_obmc_variance8x4,
       aom_obmc_sub_pixel_variance8x4)
  OBFP(BLOCK_4X4, aom_obmc_sad4x4, aom_obmc_variance4x4,
       aom_obmc_sub_pixel_variance4x4)
  OBFP(BLOCK_4X16, aom_obmc_sad4x16, aom_obmc_variance4x16,
       aom_obmc_sub_pixel_variance4x16)
  OBFP(BLOCK_16X4, aom_obmc_sad16x4, aom_obmc_variance16x4,
       aom_obmc_sub_pixel_variance16x4)
  OBFP(BLOCK_8X32, aom_obmc_sad8x32, aom_obmc_variance8x32,
       aom_obmc_sub_pixel_variance8x32)
  OBFP(BLOCK_32X8, aom_obmc_sad32x8, aom_obmc_variance32x8,
       aom_obmc_sub_pixel_variance32x8)
  OBFP(BLOCK_16X64, aom_obmc_sad16x64, aom_obmc_variance16x64,
       aom_obmc_sub_pixel_variance16x64)
  OBFP(BLOCK_64X16, aom_obmc_sad64x16, aom_obmc_variance64x16,
       aom_obmc_sub_pixel_variance64x16)
#endif  // !CONFIG_REALTIME_ONLY

#define MBFP(BT, MCSDF, MCSVF)  \
  ppi->fn_ptr[BT].msdf = MCSDF; \
  ppi->fn_ptr[BT].msvf = MCSVF;

  MBFP(BLOCK_128X128, aom_masked_sad128x128,
       aom_masked_sub_pixel_variance128x128)
  MBFP(BLOCK_128X64, aom_masked_sad128x64, aom_masked_sub_pixel_variance128x64)
  MBFP(BLOCK_64X128, aom_masked_sad64x128, aom_masked_sub_pixel_variance64x128)
  MBFP(BLOCK_64X64, aom_masked_sad64x64, aom_masked_sub_pixel_variance64x64)
  MBFP(BLOCK_64X32, aom_masked_sad64x32, aom_masked_sub_pixel_variance64x32)
  MBFP(BLOCK_32X64, aom_masked_sad32x64, aom_masked_sub_pixel_variance32x64)
  MBFP(BLOCK_32X32, aom_masked_sad32x32, aom_masked_sub_pixel_variance32x32)
  MBFP(BLOCK_32X16, aom_masked_sad32x16, aom_masked_sub_pixel_variance32x16)
  MBFP(BLOCK_16X32, aom_masked_sad16x32, aom_masked_sub_pixel_variance16x32)
  MBFP(BLOCK_16X16, aom_masked_sad16x16, aom_masked_sub_pixel_variance16x16)
  MBFP(BLOCK_16X8, aom_masked_sad16x8, aom_masked_sub_pixel_variance16x8)
  MBFP(BLOCK_8X16, aom_masked_sad8x16, aom_masked_sub_pixel_variance8x16)
  MBFP(BLOCK_8X8, aom_masked_sad8x8, aom_masked_sub_pixel_variance8x8)
  MBFP(BLOCK_4X8, aom_masked_sad4x8, aom_masked_sub_pixel_variance4x8)
  MBFP(BLOCK_8X4, aom_masked_sad8x4, aom_masked_sub_pixel_variance8x4)
  MBFP(BLOCK_4X4, aom_masked_sad4x4, aom_masked_sub_pixel_variance4x4)

#if !CONFIG_REALTIME_ONLY
  MBFP(BLOCK_4X16, aom_masked_sad4x16, aom_masked_sub_pixel_variance4x16)
  MBFP(BLOCK_16X4, aom_masked_sad16x4, aom_masked_sub_pixel_variance16x4)
  MBFP(BLOCK_8X32, aom_masked_sad8x32, aom_masked_sub_pixel_variance8x32)
  MBFP(BLOCK_32X8, aom_masked_sad32x8, aom_masked_sub_pixel_variance32x8)
  MBFP(BLOCK_16X64, aom_masked_sad16x64, aom_masked_sub_pixel_variance16x64)
  MBFP(BLOCK_64X16, aom_masked_sad64x16, aom_masked_sub_pixel_variance64x16)
#endif

#define SDSFP(BT, SDSF, SDSX4DF) \
  ppi->fn_ptr[BT].sdsf = SDSF;   \
  ppi->fn_ptr[BT].sdsx4df = SDSX4DF;

  SDSFP(BLOCK_128X128, aom_sad_skip_128x128, aom_sad_skip_128x128x4d)
  SDSFP(BLOCK_128X64, aom_sad_skip_128x64, aom_sad_skip_128x64x4d)
  SDSFP(BLOCK_64X128, aom_sad_skip_64x128, aom_sad_skip_64x128x4d)
  SDSFP(BLOCK_64X64, aom_sad_skip_64x64, aom_sad_skip_64x64x4d)
  SDSFP(BLOCK_64X32, aom_sad_skip_64x32, aom_sad_skip_64x32x4d)

  SDSFP(BLOCK_32X64, aom_sad_skip_32x64, aom_sad_skip_32x64x4d)
  SDSFP(BLOCK_32X32, aom_sad_skip_32x32, aom_sad_skip_32x32x4d)
  SDSFP(BLOCK_32X16, aom_sad_skip_32x16, aom_sad_skip_32x16x4d)

  SDSFP(BLOCK_16X32, aom_sad_skip_16x32, aom_sad_skip_16x32x4d)
  SDSFP(BLOCK_16X16, aom_sad_skip_16x16, aom_sad_skip_16x16x4d)
  SDSFP(BLOCK_8X16, aom_sad_skip_8x16, aom_sad_skip_8x16x4d)

#if !CONFIG_REALTIME_ONLY
  SDSFP(BLOCK_64X16, aom_sad_skip_64x16, aom_sad_skip_64x16x4d)
  SDSFP(BLOCK_16X64, aom_sad_skip_16x64, aom_sad_skip_16x64x4d)
  SDSFP(BLOCK_8X32, aom_sad_skip_8x32, aom_sad_skip_8x32x4d)
  SDSFP(BLOCK_4X16, aom_sad_skip_4x16, aom_sad_skip_4x16x4d)
#endif
#undef SDSFP

#if CONFIG_AV1_HIGHBITDEPTH
  highbd_set_var_fns(ppi);
#endif

  {
    // As cm->mi_params is a part of the frame level context (cpi), it is
    // unavailable at this point. mi_params is created as a local temporary
    // variable, to be passed into the functions used for allocating tpl
    // buffers. The values in this variable are populated according to initial
    // width and height of the frame.
    CommonModeInfoParams mi_params;
    enc_set_mb_mi(&mi_params, oxcf->frm_dim_cfg.width, oxcf->frm_dim_cfg.height,
                  BLOCK_4X4);

    const BLOCK_SIZE bsize = BLOCK_16X16;
    const int w = mi_size_wide[bsize];
    const int h = mi_size_high[bsize];
    const int num_cols = (mi_params.mi_cols + w - 1) / w;
    const int num_rows = (mi_params.mi_rows + h - 1) / h;
    AOM_CHECK_MEM_ERROR(
        &ppi->error, ppi->tpl_sb_rdmult_scaling_factors,
        aom_calloc(num_rows * num_cols,
                   sizeof(*ppi->tpl_sb_rdmult_scaling_factors)));

#if CONFIG_INTERNAL_STATS
    ppi->b_calculate_blockiness = 1;
    ppi->b_calculate_consistency = 1;

    for (int i = 0; i <= STAT_ALL; i++) {
      ppi->psnr[0].stat[i] = 0;
      ppi->psnr[1].stat[i] = 0;

      ppi->fastssim.stat[i] = 0;
      ppi->psnrhvs.stat[i] = 0;
    }

    ppi->psnr[0].worst = 100.0;
    ppi->psnr[1].worst = 100.0;
    ppi->worst_ssim = 100.0;
    ppi->worst_ssim_hbd = 100.0;

    ppi->count[0] = 0;
    ppi->count[1] = 0;
    ppi->total_bytes = 0;

    if (ppi->b_calculate_psnr) {
      ppi->total_sq_error[0] = 0;
      ppi->total_samples[0] = 0;
      ppi->total_sq_error[1] = 0;
      ppi->total_samples[1] = 0;
      ppi->total_recode_hits = 0;
      ppi->summed_quality = 0;
      ppi->summed_weights = 0;
      ppi->summed_quality_hbd = 0;
      ppi->summed_weights_hbd = 0;
    }

    ppi->fastssim.worst = 100.0;
    ppi->psnrhvs.worst = 100.0;

    if (ppi->b_calculate_blockiness) {
      ppi->total_blockiness = 0;
      ppi->worst_blockiness = 0.0;
    }

    ppi->total_inconsistency = 0;
    ppi->worst_consistency = 100.0;
    if (ppi->b_calculate_consistency) {
      AOM_CHECK_MEM_ERROR(&ppi->error, ppi->ssim_vars,
                          aom_malloc(sizeof(*ppi->ssim_vars) * 4 *
                                     mi_params.mi_rows * mi_params.mi_cols));
    }
#endif
  }

  ppi->error.setjmp = 0;
  return ppi;
}

AV1_COMP *av1_create_compressor(AV1_PRIMARY *ppi, const AV1EncoderConfig *oxcf,
                                BufferPool *const pool, COMPRESSOR_STAGE stage,
                                int lap_lag_in_frames) {
  AV1_COMP *volatile const cpi = aom_memalign(32, sizeof(AV1_COMP));

  if (!cpi) return NULL;

  av1_zero(*cpi);

  cpi->ppi = ppi;

  AV1_COMMON *volatile const cm = &cpi->common;
  cm->seq_params = &ppi->seq_params;
  cm->error =
      (struct aom_internal_error_info *)aom_calloc(1, sizeof(*cm->error));
  if (!cm->error) {
    aom_free(cpi);
    return NULL;
  }

  // The jmp_buf is valid only for the duration of the function that calls
  // setjmp(). Therefore, this function must reset the 'setjmp' field to 0
  // before it returns.
  if (setjmp(cm->error->jmp)) {
    cm->error->setjmp = 0;
    av1_remove_compressor(cpi);
    return NULL;
  }

  cm->error->setjmp = 1;
  cpi->compressor_stage = stage;

  cpi->do_frame_data_update = true;

  CommonModeInfoParams *const mi_params = &cm->mi_params;
  mi_params->free_mi = enc_free_mi;
  mi_params->setup_mi = enc_setup_mi;
  mi_params->set_mb_mi =
      (oxcf->pass == AOM_RC_FIRST_PASS || cpi->compressor_stage == LAP_STAGE)
          ? stat_stage_set_mb_mi
          : enc_set_mb_mi;

  mi_params->mi_alloc_bsize = BLOCK_4X4;

  CHECK_MEM_ERROR(cm, cm->fc,
                  (FRAME_CONTEXT *)aom_memalign(32, sizeof(*cm->fc)));
  CHECK_MEM_ERROR(
      cm, cm->default_frame_context,
      (FRAME_CONTEXT *)aom_memalign(32, sizeof(*cm->default_frame_context)));
  memset(cm->fc, 0, sizeof(*cm->fc));
  memset(cm->default_frame_context, 0, sizeof(*cm->default_frame_context));

  cpi->common.buffer_pool = pool;

  init_config(cpi, oxcf);
  if (cpi->compressor_stage == LAP_STAGE) {
    cpi->oxcf.gf_cfg.lag_in_frames = lap_lag_in_frames;
  }

  av1_rc_init(&cpi->oxcf, &cpi->rc);

  init_frame_info(&cpi->frame_info, cm);
  init_frame_index_set(&cpi->frame_index_set);

  cm->current_frame.frame_number = 0;
  cpi->rc.frame_number_encoded = 0;
  cpi->rc.prev_frame_is_dropped = 0;
  cpi->rc.max_consec_drop = INT_MAX;
  cpi->rc.drop_count_consec = 0;
  cm->current_frame_id = -1;
  cpi->tile_data = NULL;
  cpi->last_show_frame_buf = NULL;
  realloc_segmentation_maps(cpi);

  cpi->refresh_frame.alt_ref_frame = false;

#if CONFIG_SPEED_STATS
  cpi->tx_search_count = 0;
#endif  // CONFIG_SPEED_STATS

  cpi->time_stamps.first_ts_start = INT64_MAX;

#ifdef OUTPUT_YUV_REC
  yuv_rec_file = fopen("rec.yuv", "wb");
#endif
#ifdef OUTPUT_YUV_DENOISED
  yuv_denoised_file = fopen("denoised.yuv", "wb");
#endif

#if !CONFIG_REALTIME_ONLY
  if (is_stat_consumption_stage(cpi)) {
    const size_t packet_sz = sizeof(FIRSTPASS_STATS);
    const int packets = (int)(oxcf->twopass_stats_in.sz / packet_sz);

    if (!cpi->ppi->lap_enabled) {
      /*Re-initialize to stats buffer, populated by application in the case of
       * two pass*/
      cpi->ppi->twopass.stats_buf_ctx->stats_in_start =
          oxcf->twopass_stats_in.buf;
      cpi->twopass_frame.stats_in =
          cpi->ppi->twopass.stats_buf_ctx->stats_in_start;
      cpi->ppi->twopass.stats_buf_ctx->stats_in_end =
          &cpi->ppi->twopass.stats_buf_ctx->stats_in_start[packets - 1];

      // The buffer size is packets - 1 because the last packet is total_stats.
      av1_firstpass_info_init(&cpi->ppi->twopass.firstpass_info,
                              oxcf->twopass_stats_in.buf, packets - 1);
      av1_init_second_pass(cpi);
    } else {
      av1_firstpass_info_init(&cpi->ppi->twopass.firstpass_info, NULL, 0);
      av1_init_single_pass_lap(cpi);
    }
  }
#endif

  // The buffer "obmc_buffer" is used in inter frames for fast obmc search.
  // Hence, the memory allocation for the same is avoided for allintra encoding
  // mode.
  if (cpi->oxcf.kf_cfg.key_freq_max != 0)
    alloc_obmc_buffers(&cpi->td.mb.obmc_buffer, cm->error);

  for (int x = 0; x < 2; x++)
    for (int y = 0; y < 2; y++)
      CHECK_MEM_ERROR(
          cm, cpi->td.mb.intrabc_hash_info.hash_value_buffer[x][y],
          (uint32_t *)aom_malloc(
              AOM_BUFFER_SIZE_FOR_BLOCK_HASH *
              sizeof(*cpi->td.mb.intrabc_hash_info.hash_value_buffer[0][0])));

  cpi->td.mb.intrabc_hash_info.g_crc_initialized = 0;

  av1_set_speed_features_framesize_independent(cpi, oxcf->speed);
  av1_set_speed_features_framesize_dependent(cpi, oxcf->speed);

  int max_mi_cols = mi_params->mi_cols;
  int max_mi_rows = mi_params->mi_rows;
  if (oxcf->frm_dim_cfg.forced_max_frame_width) {
    max_mi_cols = size_in_mi(oxcf->frm_dim_cfg.forced_max_frame_width);
  }
  if (oxcf->frm_dim_cfg.forced_max_frame_height) {
    max_mi_rows = size_in_mi(oxcf->frm_dim_cfg.forced_max_frame_height);
  }

  const int consec_zero_mv_alloc_size = (max_mi_rows * max_mi_cols) >> 2;
  CHECK_MEM_ERROR(
      cm, cpi->consec_zero_mv,
      aom_calloc(consec_zero_mv_alloc_size, sizeof(*cpi->consec_zero_mv)));
  cpi->consec_zero_mv_alloc_size = consec_zero_mv_alloc_size;

  cpi->mb_weber_stats = NULL;
  cpi->mb_delta_q = NULL;
  cpi->palette_pixel_num = 0;
  cpi->scaled_last_source_available = 0;

  {
    const BLOCK_SIZE bsize = BLOCK_16X16;
    const int w = mi_size_wide[bsize];
    const int h = mi_size_high[bsize];
    const int num_cols = (max_mi_cols + w - 1) / w;
    const int num_rows = (max_mi_rows + h - 1) / h;
    CHECK_MEM_ERROR(cm, cpi->ssim_rdmult_scaling_factors,
                    aom_calloc(num_rows * num_cols,
                               sizeof(*cpi->ssim_rdmult_scaling_factors)));
    CHECK_MEM_ERROR(cm, cpi->tpl_rdmult_scaling_factors,
                    aom_calloc(num_rows * num_cols,
                               sizeof(*cpi->tpl_rdmult_scaling_factors)));
  }

#if CONFIG_TUNE_VMAF
  {
    const BLOCK_SIZE bsize = BLOCK_64X64;
    const int w = mi_size_wide[bsize];
    const int h = mi_size_high[bsize];
    const int num_cols = (mi_params->mi_cols + w - 1) / w;
    const int num_rows = (mi_params->mi_rows + h - 1) / h;
    CHECK_MEM_ERROR(cm, cpi->vmaf_info.rdmult_scaling_factors,
                    aom_calloc(num_rows * num_cols,
                               sizeof(*cpi->vmaf_info.rdmult_scaling_factors)));
    for (int i = 0; i < MAX_ARF_LAYERS; i++) {
      cpi->vmaf_info.last_frame_unsharp_amount[i] = -1.0;
      cpi->vmaf_info.last_frame_ysse[i] = -1.0;
      cpi->vmaf_info.last_frame_vmaf[i] = -1.0;
    }
    cpi->vmaf_info.original_qindex = -1;
    cpi->vmaf_info.vmaf_model = NULL;
  }
#endif

#if CONFIG_TUNE_BUTTERAUGLI
  {
    const int w = mi_size_wide[butteraugli_rdo_bsize];
    const int h = mi_size_high[butteraugli_rdo_bsize];
    const int num_cols = (mi_params->mi_cols + w - 1) / w;
    const int num_rows = (mi_params->mi_rows + h - 1) / h;
    CHECK_MEM_ERROR(
        cm, cpi->butteraugli_info.rdmult_scaling_factors,
        aom_malloc(num_rows * num_cols *
                   sizeof(*cpi->butteraugli_info.rdmult_scaling_factors)));
    memset(&cpi->butteraugli_info.source, 0,
           sizeof(cpi->butteraugli_info.source));
    memset(&cpi->butteraugli_info.resized_source, 0,
           sizeof(cpi->butteraugli_info.resized_source));
    cpi->butteraugli_info.recon_set = false;
  }
#endif

#if CONFIG_SALIENCY_MAP
  {
    CHECK_MEM_ERROR(cm, cpi->saliency_map,
                    (uint8_t *)aom_calloc(cm->height * cm->width,
                                          sizeof(*cpi->saliency_map)));
    // Buffer initialization based on MIN_MIB_SIZE_LOG2 to ensure that
    // cpi->sm_scaling_factor buffer is allocated big enough, since we have no
    // idea of the actual superblock size we are going to use yet.
    const int min_mi_w_sb = (1 << MIN_MIB_SIZE_LOG2);
    const int min_mi_h_sb = (1 << MIN_MIB_SIZE_LOG2);
    const int max_sb_cols =
        (cm->mi_params.mi_cols + min_mi_w_sb - 1) / min_mi_w_sb;
    const int max_sb_rows =
        (cm->mi_params.mi_rows + min_mi_h_sb - 1) / min_mi_h_sb;
    CHECK_MEM_ERROR(cm, cpi->sm_scaling_factor,
                    (double *)aom_calloc(max_sb_rows * max_sb_cols,
                                         sizeof(*cpi->sm_scaling_factor)));
  }
#endif

#if CONFIG_COLLECT_PARTITION_STATS
  av1_zero(cpi->partition_stats);
#endif  // CONFIG_COLLECT_PARTITION_STATS

  // Initialize the members of DeltaQuantParams with INT_MAX to ensure that
  // the quantizer tables are correctly initialized using the default deltaq
  // parameters when av1_init_quantizer is called for the first time.
  DeltaQuantParams *const prev_deltaq_params =
      &cpi->enc_quant_dequant_params.prev_deltaq_params;
  prev_deltaq_params->y_dc_delta_q = INT_MAX;
  prev_deltaq_params->u_dc_delta_q = INT_MAX;
  prev_deltaq_params->v_dc_delta_q = INT_MAX;
  prev_deltaq_params->u_ac_delta_q = INT_MAX;
  prev_deltaq_params->v_ac_delta_q = INT_MAX;

  av1_init_quantizer(&cpi->enc_quant_dequant_params, &cm->quant_params,
                     cm->seq_params->bit_depth, cpi->oxcf.algo_cfg.sharpness);
  av1_qm_init(&cm->quant_params, av1_num_planes(cm));

  av1_loop_filter_init(cm);
  cm->superres_scale_denominator = SCALE_NUMERATOR;
  cm->superres_upscaled_width = oxcf->frm_dim_cfg.width;
  cm->superres_upscaled_height = oxcf->frm_dim_cfg.height;
#if !CONFIG_REALTIME_ONLY
  av1_loop_restoration_precal();
#endif

#if CONFIG_THREE_PASS
  cpi->third_pass_ctx = NULL;
  if (cpi->oxcf.pass == AOM_RC_THIRD_PASS) {
    av1_init_thirdpass_ctx(cm, &cpi->third_pass_ctx, NULL);
  }
#endif

  cpi->second_pass_log_stream = NULL;
  cpi->use_ducky_encode = 0;

  cm->error->setjmp = 0;
  return cpi;
}

#if CONFIG_INTERNAL_STATS
#define SNPRINT(H, T) snprintf((H) + strlen(H), sizeof(H) - strlen(H), (T))

#define SNPRINT2(H, T, V) \
  snprintf((H) + strlen(H), sizeof(H) - strlen(H), (T), (V))
#endif  // CONFIG_INTERNAL_STATS

void av1_remove_primary_compressor(AV1_PRIMARY *ppi) {
  if (!ppi) return;
#if !CONFIG_REALTIME_ONLY
  av1_tf_info_free(&ppi->tf_info);
#endif  // !CONFIG_REALTIME_ONLY

  for (int i = 0; i < MAX_NUM_OPERATING_POINTS; ++i) {
    aom_free(ppi->level_params.level_info[i]);
  }
  av1_lookahead_destroy(ppi->lookahead);

  aom_free(ppi->tpl_sb_rdmult_scaling_factors);
  ppi->tpl_sb_rdmult_scaling_factors = NULL;

  TplParams *const tpl_data = &ppi->tpl_data;
  aom_free(tpl_data->txfm_stats_list);

  for (int frame = 0; frame < MAX_LAG_BUFFERS; ++frame) {
    aom_free(tpl_data->tpl_stats_pool[frame]);
    aom_free_frame_buffer(&tpl_data->tpl_rec_pool[frame]);
    tpl_data->tpl_stats_pool[frame] = NULL;
  }

#if !CONFIG_REALTIME_ONLY
  av1_tpl_dealloc(&tpl_data->tpl_mt_sync);
#endif

  av1_terminate_workers(ppi);
  free_thread_data(ppi);

  aom_free(ppi->p_mt_info.tile_thr_data);
  ppi->p_mt_info.tile_thr_data = NULL;
  aom_free(ppi->p_mt_info.workers);
  ppi->p_mt_info.workers = NULL;
  ppi->p_mt_info.num_workers = 0;

  aom_free(ppi);
}

void av1_remove_compressor(AV1_COMP *cpi) {
  if (!cpi) return;
#if CONFIG_RATECTRL_LOG
  if (cpi->oxcf.pass == 3) {
    rc_log_show(&cpi->rc_log);
  }
#endif  // CONFIG_RATECTRL_LOG

  AV1_COMMON *cm = &cpi->common;
  if (cm->current_frame.frame_number > 0) {
#if CONFIG_SPEED_STATS
    if (!is_stat_generation_stage(cpi)) {
      fprintf(stdout, "tx_search_count = %d\n", cpi->tx_search_count);
    }
#endif  // CONFIG_SPEED_STATS

#if CONFIG_COLLECT_PARTITION_STATS == 2
    if (!is_stat_generation_stage(cpi)) {
      av1_print_fr_partition_timing_stats(&cpi->partition_stats,
                                          "fr_part_timing_data.csv");
    }
#endif
  }

#if CONFIG_AV1_TEMPORAL_DENOISING
  av1_denoiser_free(&(cpi->denoiser));
#endif

  if (cm->error) {
    // Help detect use after free of the error detail string.
    memset(cm->error->detail, 'A', sizeof(cm->error->detail) - 1);
    cm->error->detail[sizeof(cm->error->detail) - 1] = '\0';
    aom_free(cm->error);
  }
  aom_free(cpi->td.tctx);
  MultiThreadInfo *const mt_info = &cpi->mt_info;
#if CONFIG_MULTITHREAD
  pthread_mutex_t *const enc_row_mt_mutex_ = mt_info->enc_row_mt.mutex_;
  pthread_cond_t *const enc_row_mt_cond_ = mt_info->enc_row_mt.cond_;
  pthread_mutex_t *const gm_mt_mutex_ = mt_info->gm_sync.mutex_;
  pthread_mutex_t *const tpl_error_mutex_ = mt_info->tpl_row_mt.mutex_;
  pthread_mutex_t *const pack_bs_mt_mutex_ = mt_info->pack_bs_sync.mutex_;
  if (enc_row_mt_mutex_ != NULL) {
    pthread_mutex_destroy(enc_row_mt_mutex_);
    aom_free(enc_row_mt_mutex_);
  }
  if (enc_row_mt_cond_ != NULL) {
    pthread_cond_destroy(enc_row_mt_cond_);
    aom_free(enc_row_mt_cond_);
  }
  if (gm_mt_mutex_ != NULL) {
    pthread_mutex_destroy(gm_mt_mutex_);
    aom_free(gm_mt_mutex_);
  }
  if (tpl_error_mutex_ != NULL) {
    pthread_mutex_destroy(tpl_error_mutex_);
    aom_free(tpl_error_mutex_);
  }
  if (pack_bs_mt_mutex_ != NULL) {
    pthread_mutex_destroy(pack_bs_mt_mutex_);
    aom_free(pack_bs_mt_mutex_);
  }
#endif
  av1_row_mt_mem_dealloc(cpi);

  if (mt_info->num_workers > 1) {
    av1_row_mt_sync_mem_dealloc(&cpi->ppi->intra_row_mt_sync);
    av1_loop_filter_dealloc(&mt_info->lf_row_sync);
    av1_cdef_mt_dealloc(&mt_info->cdef_sync);
#if !CONFIG_REALTIME_ONLY
    av1_loop_restoration_dealloc(&mt_info->lr_row_sync);
    av1_tf_mt_dealloc(&mt_info->tf_sync);
#endif
  }

#if CONFIG_THREE_PASS
  av1_free_thirdpass_ctx(cpi->third_pass_ctx);

  av1_close_second_pass_log(cpi);
#endif

  dealloc_compressor_data(cpi);

  av1_ext_part_delete(&cpi->ext_part_controller);

  av1_remove_common(cm);

  aom_free(cpi);

#ifdef OUTPUT_YUV_REC
  fclose(yuv_rec_file);
#endif

#ifdef OUTPUT_YUV_DENOISED
  fclose(yuv_denoised_file);
#endif
}

static void generate_psnr_packet(AV1_COMP *cpi) {
  struct aom_codec_cx_pkt pkt;
  int i;
  PSNR_STATS psnr;
#if CONFIG_AV1_HIGHBITDEPTH
  const uint32_t in_bit_depth = cpi->oxcf.input_cfg.input_bit_depth;
  const uint32_t bit_depth = cpi->td.mb.e_mbd.bd;
  aom_calc_highbd_psnr(cpi->source, &cpi->common.cur_frame->buf, &psnr,
                       bit_depth, in_bit_depth);
#else
  aom_calc_psnr(cpi->source, &cpi->common.cur_frame->buf, &psnr);
#endif

  for (i = 0; i < 4; ++i) {
    pkt.data.psnr.samples[i] = psnr.samples[i];
    pkt.data.psnr.sse[i] = psnr.sse[i];
    pkt.data.psnr.psnr[i] = psnr.psnr[i];
  }

#if CONFIG_AV1_HIGHBITDEPTH
  if ((cpi->source->flags & YV12_FLAG_HIGHBITDEPTH) &&
      (in_bit_depth < bit_depth)) {
    for (i = 0; i < 4; ++i) {
      pkt.data.psnr.samples_hbd[i] = psnr.samples_hbd[i];
      pkt.data.psnr.sse_hbd[i] = psnr.sse_hbd[i];
      pkt.data.psnr.psnr_hbd[i] = psnr.psnr_hbd[i];
    }
  }
#endif

  pkt.kind = AOM_CODEC_PSNR_PKT;
  aom_codec_pkt_list_add(cpi->ppi->output_pkt_list, &pkt);
}

int av1_use_as_reference(int *ext_ref_frame_flags, int ref_frame_flags) {
  if (ref_frame_flags > ((1 << INTER_REFS_PER_FRAME) - 1)) return -1;

  *ext_ref_frame_flags = ref_frame_flags;
  return 0;
}

int av1_copy_reference_enc(AV1_COMP *cpi, int idx, YV12_BUFFER_CONFIG *sd) {
  AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  YV12_BUFFER_CONFIG *cfg = get_ref_frame(cm, idx);
  if (cfg) {
    aom_yv12_copy_frame(cfg, sd, num_planes);
    return 0;
  } else {
    return -1;
  }
}

int av1_set_reference_enc(AV1_COMP *cpi, int idx, YV12_BUFFER_CONFIG *sd) {
  AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  YV12_BUFFER_CONFIG *cfg = get_ref_frame(cm, idx);
  if (cfg) {
    aom_yv12_copy_frame(sd, cfg, num_planes);
    return 0;
  } else {
    return -1;
  }
}

#ifdef OUTPUT_YUV_REC
void aom_write_one_yuv_frame(AV1_COMMON *cm, YV12_BUFFER_CONFIG *s) {
  uint8_t *src = s->y_buffer;
  int h = cm->height;
  if (yuv_rec_file == NULL) return;
  if (s->flags & YV12_FLAG_HIGHBITDEPTH) {
    uint16_t *src16 = CONVERT_TO_SHORTPTR(s->y_buffer);

    do {
      fwrite(src16, s->y_width, 2, yuv_rec_file);
      src16 += s->y_stride;
    } while (--h);

    src16 = CONVERT_TO_SHORTPTR(s->u_buffer);
    h = s->uv_height;

    do {
      fwrite(src16, s->uv_width, 2, yuv_rec_file);
      src16 += s->uv_stride;
    } while (--h);

    src16 = CONVERT_TO_SHORTPTR(s->v_buffer);
    h = s->uv_height;

    do {
      fwrite(src16, s->uv_width, 2, yuv_rec_file);
      src16 += s->uv_stride;
    } while (--h);

    fflush(yuv_rec_file);
    return;
  }

  do {
    fwrite(src, s->y_width, 1, yuv_rec_file);
    src += s->y_stride;
  } while (--h);

  src = s->u_buffer;
  h = s->uv_height;

  do {
    fwrite(src, s->uv_width, 1, yuv_rec_file);
    src += s->uv_stride;
  } while (--h);

  src = s->v_buffer;
  h = s->uv_height;

  do {
    fwrite(src, s->uv_width, 1, yuv_rec_file);
    src += s->uv_stride;
  } while (--h);

  fflush(yuv_rec_file);
}
#endif  // OUTPUT_YUV_REC

void av1_set_mv_search_params(AV1_COMP *cpi) {
  const AV1_COMMON *const cm = &cpi->common;
  MotionVectorSearchParams *const mv_search_params = &cpi->mv_search_params;
  const int max_mv_def = AOMMAX(cm->width, cm->height);

  // Default based on max resolution.
  mv_search_params->mv_step_param = av1_init_search_range(max_mv_def);

  if (cpi->sf.mv_sf.auto_mv_step_size) {
    if (frame_is_intra_only(cm)) {
      // Initialize max_mv_magnitude for use in the first INTER frame
      // after a key/intra-only frame.
      mv_search_params->max_mv_magnitude = max_mv_def;
    } else {
      // Use adaptive mv steps based on previous frame stats for show frames and
      // internal arfs.
      FRAME_UPDATE_TYPE cur_update_type =
          cpi->ppi->gf_group.update_type[cpi->gf_frame_index];
      int use_auto_mv_step =
          (cm->show_frame || cur_update_type == INTNL_ARF_UPDATE) &&
          mv_search_params->max_mv_magnitude != -1 &&
          cpi->sf.mv_sf.auto_mv_step_size >= 2;
      if (use_auto_mv_step) {
        // Allow mv_steps to correspond to twice the max mv magnitude found
        // in the previous frame, capped by the default max_mv_magnitude based
        // on resolution.
        mv_search_params->mv_step_param = av1_init_search_range(
            AOMMIN(max_mv_def, 2 * mv_search_params->max_mv_magnitude));
      }
      // Reset max_mv_magnitude based on update flag.
      if (cpi->do_frame_data_update) mv_search_params->max_mv_magnitude = -1;
    }
  }
}

void av1_set_screen_content_options(AV1_COMP *cpi, FeatureFlags *features) {
  const AV1_COMMON *const cm = &cpi->common;
  const MACROBLOCKD *const xd = &cpi->td.mb.e_mbd;

  if (cm->seq_params->force_screen_content_tools != 2) {
    features->allow_screen_content_tools = features->allow_intrabc =
        cm->seq_params->force_screen_content_tools;
    return;
  }

  if (cpi->oxcf.tune_cfg.content == AOM_CONTENT_SCREEN) {
    features->allow_screen_content_tools = 1;
    features->allow_intrabc = cpi->oxcf.mode == REALTIME ? 0 : 1;
    cpi->is_screen_content_type = 1;
    cpi->use_screen_content_tools = 1;
    return;
  }

  if (cpi->oxcf.mode == REALTIME) {
    features->allow_screen_content_tools = features->allow_intrabc = 0;
    return;
  }

  // Screen content tools are not evaluated in non-RD encoding mode unless
  // content type is not set explicitly, i.e., when
  // cpi->oxcf.tune_cfg.content != AOM_CONTENT_SCREEN, use_nonrd_pick_mode = 1
  // and hybrid_intra_pickmode = 0. Hence, screen content detection is
  // disabled.
  if (cpi->sf.rt_sf.use_nonrd_pick_mode &&
      !cpi->sf.rt_sf.hybrid_intra_pickmode) {
    features->allow_screen_content_tools = features->allow_intrabc = 0;
    return;
  }

  // Estimate if the source frame is screen content, based on the portion of
  // blocks that have few luma colors.
  const uint8_t *src = cpi->unfiltered_source->y_buffer;
  assert(src != NULL);
  const int use_hbd = cpi->unfiltered_source->flags & YV12_FLAG_HIGHBITDEPTH;
  const int stride = cpi->unfiltered_source->y_stride;
  const int width = cpi->unfiltered_source->y_width;
  const int height = cpi->unfiltered_source->y_height;
  const int64_t area = (int64_t)width * height;
  const int bd = cm->seq_params->bit_depth;
  const int blk_w = 16;
  const int blk_h = 16;
  // These threshold values are selected experimentally.
  const int color_thresh = 4;
  const unsigned int var_thresh = 0;
  // Counts of blocks with no more than color_thresh colors.
  int64_t counts_1 = 0;
  // Counts of blocks with no more than color_thresh colors and variance larger
  // than var_thresh.
  int64_t counts_2 = 0;

  for (int r = 0; r + blk_h <= height; r += blk_h) {
    for (int c = 0; c + blk_w <= width; c += blk_w) {
      int count_buf[1 << 8];  // Maximum (1 << 8) bins for hbd path.
      const uint8_t *const this_src = src + r * stride + c;
      int n_colors;
      if (use_hbd)
        av1_count_colors_highbd(this_src, stride, blk_w, blk_h, bd, NULL,
                                count_buf, &n_colors, NULL);
      else
        av1_count_colors(this_src, stride, blk_w, blk_h, count_buf, &n_colors);
      if (n_colors > 1 && n_colors <= color_thresh) {
        ++counts_1;
        struct buf_2d buf;
        buf.stride = stride;
        buf.buf = (uint8_t *)this_src;
        const unsigned int var = av1_get_perpixel_variance(
            cpi, xd, &buf, BLOCK_16X16, AOM_PLANE_Y, use_hbd);
        if (var > var_thresh) ++counts_2;
      }
    }
  }

  // The threshold values are selected experimentally.
  features->allow_screen_content_tools = counts_1 * blk_h * blk_w * 10 > area;
  // IntraBC would force loop filters off, so we use more strict rules that also
  // requires that the block has high variance.
  features->allow_intrabc = features->allow_screen_content_tools &&
                            counts_2 * blk_h * blk_w * 12 > area;
  cpi->use_screen_content_tools = features->allow_screen_content_tools;
  cpi->is_screen_content_type =
      features->allow_intrabc || (counts_1 * blk_h * blk_w * 10 > area * 4 &&
                                  counts_2 * blk_h * blk_w * 30 > area);
}

static void init_motion_estimation(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  MotionVectorSearchParams *const mv_search_params = &cpi->mv_search_params;
  const int aligned_width = (cm->width + 7) & ~7;
  const int y_stride =
      aom_calc_y_stride(aligned_width, cpi->oxcf.border_in_pixels);
  const int y_stride_src = ((cpi->oxcf.frm_dim_cfg.width != cm->width ||
                             cpi->oxcf.frm_dim_cfg.height != cm->height) ||
                            av1_superres_scaled(cm))
                               ? y_stride
                               : cpi->ppi->lookahead->buf->img.y_stride;
  int fpf_y_stride =
      cm->cur_frame != NULL ? cm->cur_frame->buf.y_stride : y_stride;

  // Update if search_site_cfg is uninitialized or the current frame has a new
  // stride
  const int should_update =
      !mv_search_params->search_site_cfg[SS_CFG_SRC][DIAMOND].stride ||
      !mv_search_params->search_site_cfg[SS_CFG_LOOKAHEAD][DIAMOND].stride ||
      (y_stride !=
       mv_search_params->search_site_cfg[SS_CFG_SRC][DIAMOND].stride);

  if (!should_update) {
    return;
  }

  // Initialization of search_site_cfg for NUM_DISTINCT_SEARCH_METHODS.
  for (SEARCH_METHODS i = DIAMOND; i < NUM_DISTINCT_SEARCH_METHODS; i++) {
    const int level = ((i == NSTEP_8PT) || (i == CLAMPED_DIAMOND)) ? 1 : 0;
    av1_init_motion_compensation[i](
        &mv_search_params->search_site_cfg[SS_CFG_SRC][i], y_stride, level);
    av1_init_motion_compensation[i](
        &mv_search_params->search_site_cfg[SS_CFG_LOOKAHEAD][i], y_stride_src,
        level);
  }

  // First pass search site config initialization.
  av1_init_motion_fpf(&mv_search_params->search_site_cfg[SS_CFG_FPF][DIAMOND],
                      fpf_y_stride);
  for (SEARCH_METHODS i = NSTEP; i < NUM_DISTINCT_SEARCH_METHODS; i++) {
    memcpy(&mv_search_params->search_site_cfg[SS_CFG_FPF][i],
           &mv_search_params->search_site_cfg[SS_CFG_FPF][DIAMOND],
           sizeof(search_site_config));
  }
}

static void init_ref_frame_bufs(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  int i;
  if (cm->cur_frame) {
    cm->cur_frame->ref_count--;
    cm->cur_frame = NULL;
  }
  for (i = 0; i < REF_FRAMES; ++i) {
    if (cm->ref_frame_map[i]) {
      cm->ref_frame_map[i]->ref_count--;
      cm->ref_frame_map[i] = NULL;
    }
  }
#ifndef NDEBUG
  BufferPool *const pool = cm->buffer_pool;
  for (i = 0; i < pool->num_frame_bufs; ++i) {
    assert(pool->frame_bufs[i].ref_count == 0);
  }
#endif
}

// TODO(chengchen): consider renaming this function as it is necessary
// for the encoder to setup critical parameters, and it does not
// deal with initial width any longer.
aom_codec_err_t av1_check_initial_width(AV1_COMP *cpi, int use_highbitdepth,
                                        int subsampling_x, int subsampling_y) {
  AV1_COMMON *const cm = &cpi->common;
  SequenceHeader *const seq_params = cm->seq_params;

  if (!cpi->frame_size_related_setup_done ||
      seq_params->use_highbitdepth != use_highbitdepth ||
      seq_params->subsampling_x != subsampling_x ||
      seq_params->subsampling_y != subsampling_y) {
    seq_params->subsampling_x = subsampling_x;
    seq_params->subsampling_y = subsampling_y;
    seq_params->use_highbitdepth = use_highbitdepth;

    av1_set_speed_features_framesize_independent(cpi, cpi->oxcf.speed);
    av1_set_speed_features_framesize_dependent(cpi, cpi->oxcf.speed);

    if (!is_stat_generation_stage(cpi)) {
#if !CONFIG_REALTIME_ONLY
      if (!av1_tf_info_alloc(&cpi->ppi->tf_info, cpi))
        return AOM_CODEC_MEM_ERROR;
#endif  // !CONFIG_REALTIME_ONLY
    }
    init_ref_frame_bufs(cpi);

    init_motion_estimation(cpi);  // TODO(agrange) This can be removed.

    cpi->initial_mbs = cm->mi_params.MBs;
    cpi->frame_size_related_setup_done = true;
  }
  return AOM_CODEC_OK;
}

#if CONFIG_AV1_TEMPORAL_DENOISING
static void setup_denoiser_buffer(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  if (cpi->oxcf.noise_sensitivity > 0 &&
      !cpi->denoiser.frame_buffer_initialized) {
    if (av1_denoiser_alloc(
            cm, &cpi->svc, &cpi->denoiser, cpi->ppi->use_svc,
            cpi->oxcf.noise_sensitivity, cm->width, cm->height,
            cm->seq_params->subsampling_x, cm->seq_params->subsampling_y,
            cm->seq_params->use_highbitdepth, AOM_BORDER_IN_PIXELS))
      aom_internal_error(cm->error, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate denoiser");
  }
}
#endif

// Returns 1 if the assigned width or height was <= 0.
static int set_size_literal(AV1_COMP *cpi, int width, int height) {
  AV1_COMMON *cm = &cpi->common;
  aom_codec_err_t err = av1_check_initial_width(
      cpi, cm->seq_params->use_highbitdepth, cm->seq_params->subsampling_x,
      cm->seq_params->subsampling_y);
  if (err != AOM_CODEC_OK) {
    aom_internal_error(cm->error, err, "av1_check_initial_width() failed");
  }

  if (width <= 0 || height <= 0) return 1;

  cm->width = width;
  cm->height = height;

#if CONFIG_AV1_TEMPORAL_DENOISING
  setup_denoiser_buffer(cpi);
#endif

  if (cm->width > cpi->data_alloc_width ||
      cm->height > cpi->data_alloc_height) {
    av1_free_context_buffers(cm);
    av1_free_shared_coeff_buffer(&cpi->td.shared_coeff_buf);
    av1_free_sms_tree(&cpi->td);
    av1_free_pmc(cpi->td.firstpass_ctx, av1_num_planes(cm));
    cpi->td.firstpass_ctx = NULL;
    alloc_compressor_data(cpi);
    realloc_segmentation_maps(cpi);
    cpi->data_alloc_width = cm->width;
    cpi->data_alloc_height = cm->height;
    cpi->frame_size_related_setup_done = false;
  }
  alloc_mb_mode_info_buffers(cpi);
  av1_update_frame_size(cpi);

  return 0;
}

void av1_set_frame_size(AV1_COMP *cpi, int width, int height) {
  AV1_COMMON *const cm = &cpi->common;
  const SequenceHeader *const seq_params = cm->seq_params;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &cpi->td.mb.e_mbd;
  int ref_frame;

  if (width != cm->width || height != cm->height) {
    // There has been a change in the encoded frame size
    set_size_literal(cpi, width, height);
    // Recalculate 'all_lossless' in case super-resolution was (un)selected.
    cm->features.all_lossless =
        cm->features.coded_lossless && !av1_superres_scaled(cm);

    av1_noise_estimate_init(&cpi->noise_estimate, cm->width, cm->height);
#if CONFIG_AV1_TEMPORAL_DENOISING
    // Reset the denoiser on the resized frame.
    if (cpi->oxcf.noise_sensitivity > 0) {
      av1_denoiser_free(&(cpi->denoiser));
      setup_denoiser_buffer(cpi);
    }
#endif
  }
  if (is_stat_consumption_stage(cpi)) {
    av1_set_target_rate(cpi, cm->width, cm->height);
  }

  alloc_frame_mvs(cm, cm->cur_frame);

  // Allocate above context buffers
  CommonContexts *const above_contexts = &cm->above_contexts;
  if (above_contexts->num_planes < av1_num_planes(cm) ||
      above_contexts->num_mi_cols < cm->mi_params.mi_cols ||
      above_contexts->num_tile_rows < cm->tiles.rows) {
    av1_free_above_context_buffers(above_contexts);
    if (av1_alloc_above_context_buffers(above_contexts, cm->tiles.rows,
                                        cm->mi_params.mi_cols,
                                        av1_num_planes(cm)))
      aom_internal_error(cm->error, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate context buffers");
  }

  AV1EncoderConfig *oxcf = &cpi->oxcf;
  oxcf->border_in_pixels = av1_get_enc_border_size(
      av1_is_resize_needed(oxcf), oxcf->kf_cfg.key_freq_max == 0,
      cm->seq_params->sb_size);

  // Reset the frame pointers to the current frame size.
  if (aom_realloc_frame_buffer(
          &cm->cur_frame->buf, cm->width, cm->height, seq_params->subsampling_x,
          seq_params->subsampling_y, seq_params->use_highbitdepth,
          cpi->oxcf.border_in_pixels, cm->features.byte_alignment, NULL, NULL,
          NULL, cpi->alloc_pyramid, 0))
    aom_internal_error(cm->error, AOM_CODEC_MEM_ERROR,
                       "Failed to allocate frame buffer");

  if (!is_stat_generation_stage(cpi)) av1_init_cdef_worker(cpi);

#if !CONFIG_REALTIME_ONLY
  if (is_restoration_used(cm)) {
    for (int i = 0; i < num_planes; ++i)
      cm->rst_info[i].frame_restoration_type = RESTORE_NONE;

    const bool is_sgr_enabled = !cpi->sf.lpf_sf.disable_sgr_filter;
    av1_alloc_restoration_buffers(cm, is_sgr_enabled);
    // Store the allocated restoration buffers in MT object.
    if (cpi->ppi->p_mt_info.num_workers > 1) {
      av1_init_lr_mt_buffers(cpi);
    }
  }
#endif

  init_motion_estimation(cpi);

  int has_valid_ref_frame = 0;
  for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ++ref_frame) {
    RefCntBuffer *const buf = get_ref_frame_buf(cm, ref_frame);
    if (buf != NULL) {
      struct scale_factors *sf = get_ref_scale_factors(cm, ref_frame);
      av1_setup_scale_factors_for_frame(sf, buf->buf.y_crop_width,
                                        buf->buf.y_crop_height, cm->width,
                                        cm->height);
      has_valid_ref_frame |= av1_is_valid_scale(sf);
      if (av1_is_scaled(sf)) aom_extend_frame_borders(&buf->buf, num_planes);
    }
  }
  if (!frame_is_intra_only(cm) && !has_valid_ref_frame) {
    aom_internal_error(
        cm->error, AOM_CODEC_CORRUPT_FRAME,
        "Can't find at least one reference frame with valid size");
  }

  av1_setup_scale_factors_for_frame(&cm->sf_identity, cm->width, cm->height,
                                    cm->width, cm->height);

  set_ref_ptrs(cm, xd, LAST_FRAME, LAST_FRAME);
}

static inline int extend_borders_mt(const AV1_COMP *cpi,
                                    MULTI_THREADED_MODULES stage, int plane) {
  const AV1_COMMON *const cm = &cpi->common;
  if (cpi->mt_info.num_mod_workers[stage] < 2) return 0;
  switch (stage) {
    // TODO(deepa.kg@ittiam.com): When cdef and loop-restoration are disabled,
    // multi-thread frame border extension along with loop filter frame.
    // As loop-filtering of a superblock row modifies the pixels of the
    // above superblock row, border extension requires that loop filtering
    // of the current and above superblock row is complete.
    case MOD_LPF: return 0;
    case MOD_CDEF:
      return is_cdef_used(cm) && !cpi->ppi->rtc_ref.non_reference_frame &&
             !is_restoration_used(cm) && !av1_superres_scaled(cm);
    case MOD_LR:
      return is_restoration_used(cm) &&
             (cm->rst_info[plane].frame_restoration_type != RESTORE_NONE);
    default: assert(0);
  }
  return 0;
}

/*!\brief Select and apply cdef filters and switchable restoration filters
 *
 * \ingroup high_level_algo
 */
static void cdef_restoration_frame(AV1_COMP *cpi, AV1_COMMON *cm,
                                   MACROBLOCKD *xd, int use_restoration,
                                   int use_cdef,
                                   unsigned int skip_apply_postproc_filters) {
#if !CONFIG_REALTIME_ONLY
  if (use_restoration)
    av1_loop_restoration_save_boundary_lines(&cm->cur_frame->buf, cm, 0);
#else
  (void)use_restoration;
#endif

  if (use_cdef) {
#if CONFIG_COLLECT_COMPONENT_TIMING
    start_timing(cpi, cdef_time);
#endif
    const int num_workers = cpi->mt_info.num_mod_workers[MOD_CDEF];
    // Find CDEF parameters
    av1_cdef_search(cpi);

    // Apply the filter
    if ((skip_apply_postproc_filters & SKIP_APPLY_CDEF) == 0) {
      assert(!cpi->ppi->rtc_ref.non_reference_frame);
      if (num_workers > 1) {
        // Extension of frame borders is multi-threaded along with cdef.
        const int do_extend_border =
            extend_borders_mt(cpi, MOD_CDEF, /* plane */ 0);
        av1_cdef_frame_mt(cm, xd, cpi->mt_info.cdef_worker,
                          cpi->mt_info.workers, &cpi->mt_info.cdef_sync,
                          num_workers, av1_cdef_init_fb_row_mt,
                          do_extend_border);
      } else {
        av1_cdef_frame(&cm->cur_frame->buf, cm, xd, av1_cdef_init_fb_row);
      }
    }
#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(cpi, cdef_time);
#endif
  }

  const int use_superres = av1_superres_scaled(cm);
  if (use_superres) {
    if ((skip_apply_postproc_filters & SKIP_APPLY_SUPERRES) == 0) {
      av1_superres_post_encode(cpi);
    }
  }

#if !CONFIG_REALTIME_ONLY
#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, loop_restoration_time);
#endif
  if (use_restoration) {
    MultiThreadInfo *const mt_info = &cpi->mt_info;
    const int num_workers = mt_info->num_mod_workers[MOD_LR];
    av1_loop_restoration_save_boundary_lines(&cm->cur_frame->buf, cm, 1);
    av1_pick_filter_restoration(cpi->source, cpi);
    if ((skip_apply_postproc_filters & SKIP_APPLY_RESTORATION) == 0 &&
        (cm->rst_info[0].frame_restoration_type != RESTORE_NONE ||
         cm->rst_info[1].frame_restoration_type != RESTORE_NONE ||
         cm->rst_info[2].frame_restoration_type != RESTORE_NONE)) {
      if (num_workers > 1) {
        // Extension of frame borders is multi-threaded along with loop
        // restoration filter.
        const int do_extend_border = 1;
        av1_loop_restoration_filter_frame_mt(
            &cm->cur_frame->buf, cm, 0, mt_info->workers, num_workers,
            &mt_info->lr_row_sync, &cpi->lr_ctxt, do_extend_border);
      } else {
        av1_loop_restoration_filter_frame(&cm->cur_frame->buf, cm, 0,
                                          &cpi->lr_ctxt);
      }
    }
  }
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, loop_restoration_time);
#endif
#endif  // !CONFIG_REALTIME_ONLY
}

static void extend_frame_borders(AV1_COMP *cpi) {
  const AV1_COMMON *const cm = &cpi->common;
  // TODO(debargha): Fix mv search range on encoder side
  for (int plane = 0; plane < av1_num_planes(cm); ++plane) {
    const bool extend_border_done = extend_borders_mt(cpi, MOD_CDEF, plane) ||
                                    extend_borders_mt(cpi, MOD_LR, plane);
    if (!extend_border_done) {
      const YV12_BUFFER_CONFIG *const ybf = &cm->cur_frame->buf;
      aom_extend_frame_borders_plane_row(ybf, plane, 0,
                                         ybf->crop_heights[plane > 0]);
    }
  }
}

/*!\brief Select and apply deblocking filters, cdef filters, and restoration
 * filters.
 *
 * \ingroup high_level_algo
 */
static void loopfilter_frame(AV1_COMP *cpi, AV1_COMMON *cm) {
  MultiThreadInfo *const mt_info = &cpi->mt_info;
  const int num_workers = mt_info->num_mod_workers[MOD_LPF];
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *xd = &cpi->td.mb.e_mbd;
  cpi->td.mb.rdmult = cpi->rd.RDMULT;

  assert(IMPLIES(is_lossless_requested(&cpi->oxcf.rc_cfg),
                 cm->features.coded_lossless && cm->features.all_lossless));

  const int use_loopfilter =
      is_loopfilter_used(cm) && !cpi->mt_info.pipeline_lpf_mt_with_enc;
  const int use_cdef = is_cdef_used(cm);
  const int use_superres = av1_superres_scaled(cm);
  const int use_restoration = is_restoration_used(cm);

  const unsigned int skip_apply_postproc_filters =
      derive_skip_apply_postproc_filters(cpi, use_loopfilter, use_cdef,
                                         use_superres, use_restoration);

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, loop_filter_time);
#endif
  if (use_loopfilter) {
    av1_pick_filter_level(cpi->source, cpi, cpi->sf.lpf_sf.lpf_pick);
    struct loopfilter *lf = &cm->lf;
    if ((lf->filter_level[0] || lf->filter_level[1]) &&
        (skip_apply_postproc_filters & SKIP_APPLY_LOOPFILTER) == 0) {
      assert(!cpi->ppi->rtc_ref.non_reference_frame);
      // lpf_opt_level = 1 : Enables dual/quad loop-filtering.
      // lpf_opt_level is set to 1 if transform size search depth in inter
      // blocks is limited to one as quad loop filtering assumes that all the
      // transform blocks within a 16x8/8x16/16x16 prediction block are of the
      // same size. lpf_opt_level = 2 : Filters both chroma planes together, in
      // addition to enabling dual/quad loop-filtering. This is enabled when lpf
      // pick method is LPF_PICK_FROM_Q as u and v plane filter levels are
      // equal.
      int lpf_opt_level = get_lpf_opt_level(&cpi->sf);
      av1_loop_filter_frame_mt(&cm->cur_frame->buf, cm, xd, 0, num_planes, 0,
                               mt_info->workers, num_workers,
                               &mt_info->lf_row_sync, lpf_opt_level);
    }
  }

#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, loop_filter_time);
#endif

  cdef_restoration_frame(cpi, cm, xd, use_restoration, use_cdef,
                         skip_apply_postproc_filters);
}

static void update_motion_stat(AV1_COMP *const cpi) {
  AV1_COMMON *const cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  RATE_CONTROL *const rc = &cpi->rc;
  SVC *const svc = &cpi->svc;
  const int avg_cnt_zeromv =
      100 * cpi->rc.cnt_zeromv / (mi_params->mi_rows * mi_params->mi_cols);
  if (!cpi->ppi->use_svc ||
      (cpi->ppi->use_svc &&
       !cpi->svc.layer_context[cpi->svc.temporal_layer_id].is_key_frame &&
       cpi->svc.spatial_layer_id == cpi->svc.number_spatial_layers - 1)) {
    rc->avg_frame_low_motion =
        (rc->avg_frame_low_motion == 0)
            ? avg_cnt_zeromv
            : (3 * rc->avg_frame_low_motion + avg_cnt_zeromv) / 4;
    // For SVC: set avg_frame_low_motion (only computed on top spatial layer)
    // to all lower spatial layers.
    if (cpi->ppi->use_svc &&
        svc->spatial_layer_id == svc->number_spatial_layers - 1) {
      for (int i = 0; i < svc->number_spatial_layers - 1; ++i) {
        const int layer = LAYER_IDS_TO_IDX(i, svc->temporal_layer_id,
                                           svc->number_temporal_layers);
        LAYER_CONTEXT *const lc = &svc->layer_context[layer];
        RATE_CONTROL *const lrc = &lc->rc;
        lrc->avg_frame_low_motion = rc->avg_frame_low_motion;
      }
    }
  }
}

/*!\brief Encode a frame without the recode loop, usually used in one-pass
 * encoding and realtime coding.
 *
 * \ingroup high_level_algo
 *
 * \param[in]    cpi             Top-level encoder structure
 *
 * \return Returns a value to indicate if the encoding is done successfully.
 * \retval #AOM_CODEC_OK
 * \retval #AOM_CODEC_ERROR
 */
static int encode_without_recode(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  const QuantizationCfg *const q_cfg = &cpi->oxcf.q_cfg;
  SVC *const svc = &cpi->svc;
  const int resize_pending = is_frame_resize_pending(cpi);
  int top_index = 0, bottom_index = 0, q = 0;
  YV12_BUFFER_CONFIG *unscaled = cpi->unscaled_source;
  InterpFilter filter_scaler =
      cpi->ppi->use_svc ? svc->downsample_filter_type[svc->spatial_layer_id]
                        : EIGHTTAP_SMOOTH;
  int phase_scaler = cpi->ppi->use_svc
                         ? svc->downsample_filter_phase[svc->spatial_layer_id]
                         : 0;

  if (cpi->rc.postencode_drop && allow_postencode_drop_rtc(cpi))
    av1_save_all_coding_context(cpi);

  set_size_independent_vars(cpi);
  av1_setup_frame_size(cpi);
  cm->prev_frame = get_primary_ref_frame_buf(cm);
  av1_set_size_dependent_vars(cpi, &q, &bottom_index, &top_index);
  av1_set_mv_search_params(cpi);

  if (cm->current_frame.frame_number == 0 &&
      (cpi->ppi->use_svc || cpi->oxcf.rc_cfg.drop_frames_water_mark > 0) &&
      cpi->svc.temporal_layer_id == 0) {
    const SequenceHeader *seq_params = cm->seq_params;
    if (aom_alloc_frame_buffer(
            &cpi->svc.source_last_TL0, cpi->oxcf.frm_dim_cfg.width,
            cpi->oxcf.frm_dim_cfg.height, seq_params->subsampling_x,
            seq_params->subsampling_y, seq_params->use_highbitdepth,
            cpi->oxcf.border_in_pixels, cm->features.byte_alignment, false,
            0)) {
      aom_internal_error(cm->error, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate buffer for source_last_TL0");
    }
  }

  if (!cpi->ppi->use_svc) {
    phase_scaler = 8;
    // 2:1 scaling.
    if ((cm->width << 1) == unscaled->y_crop_width &&
        (cm->height << 1) == unscaled->y_crop_height) {
      filter_scaler = BILINEAR;
      // For lower resolutions use eighttap_smooth.
      if (cm->width * cm->height <= 320 * 180) filter_scaler = EIGHTTAP_SMOOTH;
    } else if ((cm->width << 2) == unscaled->y_crop_width &&
               (cm->height << 2) == unscaled->y_crop_height) {
      // 4:1 scaling.
      filter_scaler = EIGHTTAP_SMOOTH;
    } else if ((cm->width << 2) == 3 * unscaled->y_crop_width &&
               (cm->height << 2) == 3 * unscaled->y_crop_height) {
      // 4:3 scaling.
      filter_scaler = EIGHTTAP_REGULAR;
    }
  }

  allocate_gradient_info_for_hog(cpi);

  allocate_src_var_of_4x4_sub_block_buf(cpi);

  const SPEED_FEATURES *sf = &cpi->sf;
  if (sf->part_sf.partition_search_type == VAR_BASED_PARTITION)
    variance_partition_alloc(cpi);

  if (cm->current_frame.frame_type == KEY_FRAME ||
      ((sf->inter_sf.extra_prune_warped && cpi->refresh_frame.golden_frame)))
    copy_frame_prob_info(cpi);

#if CONFIG_COLLECT_COMPONENT_TIMING
  printf("\n Encoding a frame: \n");
#endif

#if CONFIG_TUNE_BUTTERAUGLI
  if (cpi->oxcf.tune_cfg.tuning == AOM_TUNE_BUTTERAUGLI) {
    av1_setup_butteraugli_rdmult(cpi);
  }
#endif

  cpi->source = av1_realloc_and_scale_if_required(
      cm, unscaled, &cpi->scaled_source, filter_scaler, phase_scaler, true,
      false, cpi->oxcf.border_in_pixels, cpi->alloc_pyramid);
  if (frame_is_intra_only(cm) || resize_pending != 0) {
    const int current_size =
        (cm->mi_params.mi_rows * cm->mi_params.mi_cols) >> 2;
    if (cpi->consec_zero_mv &&
        (cpi->consec_zero_mv_alloc_size < current_size)) {
      aom_free(cpi->consec_zero_mv);
      cpi->consec_zero_mv_alloc_size = 0;
      CHECK_MEM_ERROR(cm, cpi->consec_zero_mv,
                      aom_malloc(current_size * sizeof(*cpi->consec_zero_mv)));
      cpi->consec_zero_mv_alloc_size = current_size;
    }
    assert(cpi->consec_zero_mv != NULL);
    memset(cpi->consec_zero_mv, 0, current_size * sizeof(*cpi->consec_zero_mv));
  }

  if (cpi->scaled_last_source_available) {
    cpi->last_source = &cpi->scaled_last_source;
    cpi->scaled_last_source_available = 0;
  } else if (cpi->unscaled_last_source != NULL) {
    cpi->last_source = av1_realloc_and_scale_if_required(
        cm, cpi->unscaled_last_source, &cpi->scaled_last_source, filter_scaler,
        phase_scaler, true, false, cpi->oxcf.border_in_pixels,
        cpi->alloc_pyramid);
  }

  if (cpi->sf.rt_sf.use_temporal_noise_estimate) {
    av1_update_noise_estimate(cpi);
  }

#if CONFIG_AV1_TEMPORAL_DENOISING
  if (cpi->oxcf.noise_sensitivity > 0 && cpi->ppi->use_svc)
    av1_denoiser_reset_on_first_frame(cpi);
#endif

  // For 1 spatial layer encoding: if the (non-LAST) reference has different
  // resolution from the source then disable that reference. This is to avoid
  // significant increase in encode time from scaling the references in
  // av1_scale_references. Note GOLDEN is forced to update on the (first/tigger)
  // resized frame and ALTREF will be refreshed ~4 frames later, so both
  // references become available again after few frames.
  // For superres: don't disable golden reference.
  if (svc->number_spatial_layers == 1) {
    if (!cpi->oxcf.superres_cfg.enable_superres) {
      if (cpi->ref_frame_flags & av1_ref_frame_flag_list[GOLDEN_FRAME]) {
        const YV12_BUFFER_CONFIG *const ref =
            get_ref_frame_yv12_buf(cm, GOLDEN_FRAME);
        if (ref == NULL || ref->y_crop_width != cm->width ||
            ref->y_crop_height != cm->height) {
          cpi->ref_frame_flags ^= AOM_GOLD_FLAG;
        }
      }
    }
    if (cpi->ref_frame_flags & av1_ref_frame_flag_list[ALTREF_FRAME]) {
      const YV12_BUFFER_CONFIG *const ref =
          get_ref_frame_yv12_buf(cm, ALTREF_FRAME);
      if (ref == NULL || ref->y_crop_width != cm->width ||
          ref->y_crop_height != cm->height) {
        cpi->ref_frame_flags ^= AOM_ALT_FLAG;
      }
    }
  }

  int scale_references = 0;
#if CONFIG_FPMT_TEST
  scale_references =
      cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE ? 1 : 0;
#endif  // CONFIG_FPMT_TEST
  if (scale_references ||
      cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] == 0) {
    if (!frame_is_intra_only(cm)) {
      av1_scale_references(cpi, filter_scaler, phase_scaler, 1);
    }
  }

  av1_set_quantizer(cm, q_cfg->qm_minlevel, q_cfg->qm_maxlevel, q,
                    q_cfg->enable_chroma_deltaq, q_cfg->enable_hdr_deltaq,
                    cpi->oxcf.mode == ALLINTRA, cpi->oxcf.tune_cfg.tuning);
  av1_set_speed_features_qindex_dependent(cpi, cpi->oxcf.speed);
  av1_init_quantizer(&cpi->enc_quant_dequant_params, &cm->quant_params,
                     cm->seq_params->bit_depth, cpi->oxcf.algo_cfg.sharpness);
  av1_set_variance_partition_thresholds(cpi, q, 0);
  av1_setup_frame(cpi);

  // Check if this high_source_sad (scene/slide change) frame should be
  // encoded at high/max QP, and if so, set the q and adjust some rate
  // control parameters.
  if (cpi->sf.rt_sf.overshoot_detection_cbr == FAST_DETECTION_MAXQ &&
      cpi->rc.high_source_sad) {
    if (av1_encodedframe_overshoot_cbr(cpi, &q)) {
      av1_set_quantizer(cm, q_cfg->qm_minlevel, q_cfg->qm_maxlevel, q,
                        q_cfg->enable_chroma_deltaq, q_cfg->enable_hdr_deltaq,
                        cpi->oxcf.mode == ALLINTRA, cpi->oxcf.tune_cfg.tuning);
      av1_set_speed_features_qindex_dependent(cpi, cpi->oxcf.speed);
      av1_init_quantizer(&cpi->enc_quant_dequant_params, &cm->quant_params,
                         cm->seq_params->bit_depth,
                         cpi->oxcf.algo_cfg.sharpness);
      av1_set_variance_partition_thresholds(cpi, q, 0);
      if (frame_is_intra_only(cm) || cm->features.error_resilient_mode ||
          cm->features.primary_ref_frame == PRIMARY_REF_NONE)
        av1_setup_frame(cpi);
    }
  }
  av1_apply_active_map(cpi);
  if (q_cfg->aq_mode == CYCLIC_REFRESH_AQ) av1_cyclic_refresh_setup(cpi);
  if (cm->seg.enabled) {
    if (!cm->seg.update_data && cm->prev_frame) {
      segfeatures_copy(&cm->seg, &cm->prev_frame->seg);
      cm->seg.enabled = cm->prev_frame->seg.enabled;
    } else {
      av1_calculate_segdata(&cm->seg);
    }
  } else {
    memset(&cm->seg, 0, sizeof(cm->seg));
  }
  segfeatures_copy(&cm->cur_frame->seg, &cm->seg);
  cm->cur_frame->seg.enabled = cm->seg.enabled;

  // This is for rtc temporal filtering case.
  if (is_psnr_calc_enabled(cpi) && cpi->sf.rt_sf.use_rtc_tf) {
    const SequenceHeader *seq_params = cm->seq_params;

    if (cpi->orig_source.buffer_alloc_sz == 0 ||
        cpi->rc.prev_coded_width != cpi->oxcf.frm_dim_cfg.width ||
        cpi->rc.prev_coded_height != cpi->oxcf.frm_dim_cfg.height) {
      // Allocate a source buffer to store the true source for psnr calculation.
      if (aom_alloc_frame_buffer(
              &cpi->orig_source, cpi->oxcf.frm_dim_cfg.width,
              cpi->oxcf.frm_dim_cfg.height, seq_params->subsampling_x,
              seq_params->subsampling_y, seq_params->use_highbitdepth,
              cpi->oxcf.border_in_pixels, cm->features.byte_alignment, false,
              0))
        aom_internal_error(cm->error, AOM_CODEC_MEM_ERROR,
                           "Failed to allocate scaled buffer");
    }

    aom_yv12_copy_y(cpi->source, &cpi->orig_source, 1);
    aom_yv12_copy_u(cpi->source, &cpi->orig_source, 1);
    aom_yv12_copy_v(cpi->source, &cpi->orig_source, 1);
  }

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, av1_encode_frame_time);
#endif

  // Set the motion vector precision based on mv stats from the last coded
  // frame.
  if (!frame_is_intra_only(cm)) av1_pick_and_set_high_precision_mv(cpi, q);

  // transform / motion compensation build reconstruction frame
  av1_encode_frame(cpi);

  if (!cpi->rc.rtc_external_ratectrl && !frame_is_intra_only(cm))
    update_motion_stat(cpi);

  // Adjust the refresh of the golden (longer-term) reference based on QP
  // selected for this frame. This is for CBR real-time mode, and only
  // for single layer without usage of the set_ref_frame_config (so
  // reference structure for 1 layer is set internally).
  if (!frame_is_intra_only(cm) && cpi->oxcf.rc_cfg.mode == AOM_CBR &&
      cpi->oxcf.mode == REALTIME && svc->number_spatial_layers == 1 &&
      svc->number_temporal_layers == 1 && !cpi->rc.rtc_external_ratectrl &&
      !cpi->ppi->rtc_ref.set_ref_frame_config &&
      sf->rt_sf.gf_refresh_based_on_qp)
    av1_adjust_gf_refresh_qp_one_pass_rt(cpi);

  // For non-svc: if scaling is required, copy scaled_source
  // into scaled_last_source.
  if (cm->current_frame.frame_number > 1 && !cpi->ppi->use_svc &&
      cpi->scaled_source.y_buffer != NULL &&
      cpi->scaled_last_source.y_buffer != NULL &&
      cpi->scaled_source.y_crop_width == cpi->scaled_last_source.y_crop_width &&
      cpi->scaled_source.y_crop_height ==
          cpi->scaled_last_source.y_crop_height &&
      (cm->width != cpi->unscaled_source->y_crop_width ||
       cm->height != cpi->unscaled_source->y_crop_height)) {
    cpi->scaled_last_source_available = 1;
    aom_yv12_copy_y(&cpi->scaled_source, &cpi->scaled_last_source, 1);
    aom_yv12_copy_u(&cpi->scaled_source, &cpi->scaled_last_source, 1);
    aom_yv12_copy_v(&cpi->scaled_source, &cpi->scaled_last_source, 1);
  }

#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, av1_encode_frame_time);
#endif
#if CONFIG_INTERNAL_STATS
  ++cpi->frame_recode_hits;
#endif

  return AOM_CODEC_OK;
}

#if !CONFIG_REALTIME_ONLY

/*!\brief Recode loop for encoding one frame. the purpose of encoding one frame
 * for multiple times can be approaching a target bitrate or adjusting the usage
 * of global motions.
 *
 * \ingroup high_level_algo
 *
 * \param[in]    cpi             Top-level encoder structure
 * \param[in]    size            Bitstream size
 * \param[out]   dest            Bitstream output buffer
 * \param[in]    dest_size       Bitstream output buffer size
 *
 * \return Returns a value to indicate if the encoding is done successfully.
 * \retval #AOM_CODEC_OK
 * \retval -1
 * \retval #AOM_CODEC_ERROR
 */
static int encode_with_recode_loop(AV1_COMP *cpi, size_t *size, uint8_t *dest,
                                   size_t dest_size) {
  AV1_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;
  GlobalMotionInfo *const gm_info = &cpi->gm_info;
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  const QuantizationCfg *const q_cfg = &oxcf->q_cfg;
  const int allow_recode = (cpi->sf.hl_sf.recode_loop != DISALLOW_RECODE);
  // Must allow recode if minimum compression ratio is set.
  assert(IMPLIES(oxcf->rc_cfg.min_cr > 0, allow_recode));

  set_size_independent_vars(cpi);
  if (is_stat_consumption_stage_twopass(cpi) &&
      cpi->sf.interp_sf.adaptive_interp_filter_search)
    cpi->interp_search_flags.interp_filter_search_mask =
        av1_setup_interp_filter_search_mask(cpi);

  av1_setup_frame_size(cpi);

  if (av1_superres_in_recode_allowed(cpi) &&
      cpi->superres_mode != AOM_SUPERRES_NONE &&
      cm->superres_scale_denominator == SCALE_NUMERATOR) {
    // Superres mode is currently enabled, but the denominator selected will
    // disable superres. So no need to continue, as we will go through another
    // recode loop for full-resolution after this anyway.
    return -1;
  }

  int top_index = 0, bottom_index = 0;
  int q = 0, q_low = 0, q_high = 0;
  av1_set_size_dependent_vars(cpi, &q, &bottom_index, &top_index);
  q_low = bottom_index;
  q_high = top_index;

  av1_set_mv_search_params(cpi);

  allocate_gradient_info_for_hog(cpi);

  allocate_src_var_of_4x4_sub_block_buf(cpi);

  if (cpi->sf.part_sf.partition_search_type == VAR_BASED_PARTITION)
    variance_partition_alloc(cpi);

  if (cm->current_frame.frame_type == KEY_FRAME) copy_frame_prob_info(cpi);

#if CONFIG_COLLECT_COMPONENT_TIMING
  printf("\n Encoding a frame: \n");
#endif

#if !CONFIG_RD_COMMAND
  // Determine whether to use screen content tools using two fast encoding.
  if (!cpi->sf.hl_sf.disable_extra_sc_testing && !cpi->use_ducky_encode)
    av1_determine_sc_tools_with_encoding(cpi, q);
#endif  // !CONFIG_RD_COMMAND

#if CONFIG_TUNE_VMAF
  if (oxcf->tune_cfg.tuning == AOM_TUNE_VMAF_NEG_MAX_GAIN) {
    av1_vmaf_neg_preprocessing(cpi, cpi->unscaled_source);
  }
#endif

#if CONFIG_TUNE_BUTTERAUGLI
  cpi->butteraugli_info.recon_set = false;
  int original_q = 0;
#endif

  cpi->num_frame_recode = 0;

  // Loop variables
  int loop = 0;
  int loop_count = 0;
  int overshoot_seen = 0;
  int undershoot_seen = 0;
  int low_cr_seen = 0;
  int last_loop_allow_hp = 0;

  do {
    loop = 0;
    int do_mv_stats_collection = 1;

    // if frame was scaled calculate global_motion_search again if already
    // done
    if (loop_count > 0 && cpi->source && gm_info->search_done) {
      if (cpi->source->y_crop_width != cm->width ||
          cpi->source->y_crop_height != cm->height) {
        gm_info->search_done = 0;
      }
    }
    cpi->source = av1_realloc_and_scale_if_required(
        cm, cpi->unscaled_source, &cpi->scaled_source, EIGHTTAP_REGULAR, 0,
        false, false, cpi->oxcf.border_in_pixels, cpi->alloc_pyramid);

#if CONFIG_TUNE_BUTTERAUGLI
    if (oxcf->tune_cfg.tuning == AOM_TUNE_BUTTERAUGLI) {
      if (loop_count == 0) {
        original_q = q;
        // TODO(sdeng): different q here does not make big difference. Use a
        // faster pass instead.
        q = 96;
        av1_setup_butteraugli_source(cpi);
      } else {
        q = original_q;
      }
    }
#endif

    if (cpi->unscaled_last_source != NULL) {
      cpi->last_source = av1_realloc_and_scale_if_required(
          cm, cpi->unscaled_last_source, &cpi->scaled_last_source,
          EIGHTTAP_REGULAR, 0, false, false, cpi->oxcf.border_in_pixels,
          cpi->alloc_pyramid);
    }

    int scale_references = 0;
#if CONFIG_FPMT_TEST
    scale_references =
        cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE ? 1 : 0;
#endif  // CONFIG_FPMT_TEST
    if (scale_references ||
        cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] == 0) {
      if (!frame_is_intra_only(cm)) {
        if (loop_count > 0) {
          release_scaled_references(cpi);
        }
        av1_scale_references(cpi, EIGHTTAP_REGULAR, 0, 0);
      }
    }

#if CONFIG_TUNE_VMAF
    if (oxcf->tune_cfg.tuning >= AOM_TUNE_VMAF_WITH_PREPROCESSING &&
        oxcf->tune_cfg.tuning <= AOM_TUNE_VMAF_NEG_MAX_GAIN) {
      cpi->vmaf_info.original_qindex = q;
      q = av1_get_vmaf_base_qindex(cpi, q);
    }
#endif

#if CONFIG_RD_COMMAND
    RD_COMMAND *rd_command = &cpi->rd_command;
    RD_OPTION option = rd_command->option_ls[rd_command->frame_index];
    if (option == RD_OPTION_SET_Q || option == RD_OPTION_SET_Q_RDMULT) {
      q = rd_command->q_index_ls[rd_command->frame_index];
    }
#endif  // CONFIG_RD_COMMAND

#if CONFIG_BITRATE_ACCURACY
#if CONFIG_THREE_PASS
    if (oxcf->pass == AOM_RC_THIRD_PASS && cpi->vbr_rc_info.ready == 1) {
      int frame_coding_idx =
          av1_vbr_rc_frame_coding_idx(&cpi->vbr_rc_info, cpi->gf_frame_index);
      if (frame_coding_idx < cpi->vbr_rc_info.total_frame_count) {
        q = cpi->vbr_rc_info.q_index_list[frame_coding_idx];
      } else {
        // TODO(angiebird): Investigate why sometimes there is an extra frame
        // after the last GOP.
        q = cpi->vbr_rc_info.base_q_index;
      }
    }
#else
    if (cpi->vbr_rc_info.q_index_list_ready) {
      q = cpi->vbr_rc_info.q_index_list[cpi->gf_frame_index];
    }
#endif  // CONFIG_THREE_PASS
#endif  // CONFIG_BITRATE_ACCURACY

#if CONFIG_RATECTRL_LOG && CONFIG_THREE_PASS && CONFIG_BITRATE_ACCURACY
    // TODO(angiebird): Move this into a function.
    if (oxcf->pass == AOM_RC_THIRD_PASS) {
      int frame_coding_idx =
          av1_vbr_rc_frame_coding_idx(&cpi->vbr_rc_info, cpi->gf_frame_index);
      double qstep_ratio = cpi->vbr_rc_info.qstep_ratio_list[frame_coding_idx];
      FRAME_UPDATE_TYPE update_type =
          cpi->vbr_rc_info.update_type_list[frame_coding_idx];
      rc_log_frame_encode_param(&cpi->rc_log, frame_coding_idx, qstep_ratio, q,
                                update_type);
    }
#endif  // CONFIG_RATECTRL_LOG && CONFIG_THREE_PASS && CONFIG_BITRATE_ACCURACY

    if (cpi->use_ducky_encode) {
      const DuckyEncodeFrameInfo *frame_info =
          &cpi->ducky_encode_info.frame_info;
      if (frame_info->qp_mode == DUCKY_ENCODE_FRAME_MODE_QINDEX) {
        q = frame_info->q_index;
        cm->delta_q_info.delta_q_present_flag = frame_info->delta_q_enabled;
      }
    }

    av1_set_quantizer(cm, q_cfg->qm_minlevel, q_cfg->qm_maxlevel, q,
                      q_cfg->enable_chroma_deltaq, q_cfg->enable_hdr_deltaq,
                      oxcf->mode == ALLINTRA, oxcf->tune_cfg.tuning);
    av1_set_speed_features_qindex_dependent(cpi, oxcf->speed);
    av1_init_quantizer(&cpi->enc_quant_dequant_params, &cm->quant_params,
                       cm->seq_params->bit_depth, cpi->oxcf.algo_cfg.sharpness);

    av1_set_variance_partition_thresholds(cpi, q, 0);

    // printf("Frame %d/%d: q = %d, frame_type = %d superres_denom = %d\n",
    //        cm->current_frame.frame_number, cm->show_frame, q,
    //        cm->current_frame.frame_type, cm->superres_scale_denominator);

    if (loop_count == 0) {
      av1_setup_frame(cpi);
    } else if (get_primary_ref_frame_buf(cm) == NULL) {
      // Base q-index may have changed, so we need to assign proper default coef
      // probs before every iteration.
      av1_default_coef_probs(cm);
      av1_setup_frame_contexts(cm);
    }

    if (q_cfg->aq_mode == VARIANCE_AQ) {
      av1_vaq_frame_setup(cpi);
    } else if (q_cfg->aq_mode == COMPLEXITY_AQ) {
      av1_setup_in_frame_q_adj(cpi);
    }

    if (cm->seg.enabled) {
      if (!cm->seg.update_data && cm->prev_frame) {
        segfeatures_copy(&cm->seg, &cm->prev_frame->seg);
        cm->seg.enabled = cm->prev_frame->seg.enabled;
      } else {
        av1_calculate_segdata(&cm->seg);
      }
    } else {
      memset(&cm->seg, 0, sizeof(cm->seg));
    }
    segfeatures_copy(&cm->cur_frame->seg, &cm->seg);
    cm->cur_frame->seg.enabled = cm->seg.enabled;

#if CONFIG_COLLECT_COMPONENT_TIMING
    start_timing(cpi, av1_encode_frame_time);
#endif
    // Set the motion vector precision based on mv stats from the last coded
    // frame.
    if (!frame_is_intra_only(cm)) {
      av1_pick_and_set_high_precision_mv(cpi, q);

      // If the precision has changed during different iteration of the loop,
      // then we need to reset the global motion vectors
      if (loop_count > 0 &&
          cm->features.allow_high_precision_mv != last_loop_allow_hp) {
        gm_info->search_done = 0;
      }
      last_loop_allow_hp = cm->features.allow_high_precision_mv;
    }

    // transform / motion compensation build reconstruction frame
    av1_encode_frame(cpi);

    // Disable mv_stats collection for parallel frames based on update flag.
    if (!cpi->do_frame_data_update) do_mv_stats_collection = 0;

    // Reset the mv_stats in case we are interrupted by an intraframe or an
    // overlay frame.
    if (cpi->mv_stats.valid && do_mv_stats_collection) av1_zero(cpi->mv_stats);

    // Gather the mv_stats for the next frame
    if (cpi->sf.hl_sf.high_precision_mv_usage == LAST_MV_DATA &&
        av1_frame_allows_smart_mv(cpi) && do_mv_stats_collection) {
      av1_collect_mv_stats(cpi, q);
    }

#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(cpi, av1_encode_frame_time);
#endif

#if CONFIG_BITRATE_ACCURACY || CONFIG_RD_COMMAND
    const int do_dummy_pack = 1;
#else   // CONFIG_BITRATE_ACCURACY
    // Dummy pack of the bitstream using up to date stats to get an
    // accurate estimate of output frame size to determine if we need
    // to recode.
    const int do_dummy_pack =
        (cpi->sf.hl_sf.recode_loop >= ALLOW_RECODE_KFARFGF &&
         oxcf->rc_cfg.mode != AOM_Q) ||
        oxcf->rc_cfg.min_cr > 0;
#endif  // CONFIG_BITRATE_ACCURACY
    if (do_dummy_pack) {
      av1_finalize_encoded_frame(cpi);
      int largest_tile_id = 0;  // Output from bitstream: unused here
      rc->coefficient_size = 0;
      if (av1_pack_bitstream(cpi, dest, dest_size, size, &largest_tile_id) !=
          AOM_CODEC_OK) {
        return AOM_CODEC_ERROR;
      }

      // bits used for this frame
      rc->projected_frame_size = (int)(*size) << 3;
#if CONFIG_RD_COMMAND
      PSNR_STATS psnr;
      aom_calc_psnr(cpi->source, &cpi->common.cur_frame->buf, &psnr);
      printf("q %d rdmult %d rate %d dist %" PRIu64 "\n", q, cpi->rd.RDMULT,
             rc->projected_frame_size, psnr.sse[0]);
      ++rd_command->frame_index;
      if (rd_command->frame_index == rd_command->frame_count) {
        return AOM_CODEC_ERROR;
      }
#endif  // CONFIG_RD_COMMAND

#if CONFIG_RATECTRL_LOG && CONFIG_THREE_PASS && CONFIG_BITRATE_ACCURACY
      if (oxcf->pass == AOM_RC_THIRD_PASS) {
        int frame_coding_idx =
            av1_vbr_rc_frame_coding_idx(&cpi->vbr_rc_info, cpi->gf_frame_index);
        rc_log_frame_entropy(&cpi->rc_log, frame_coding_idx,
                             rc->projected_frame_size, rc->coefficient_size);
      }
#endif  // CONFIG_RATECTRL_LOG && CONFIG_THREE_PASS && CONFIG_BITRATE_ACCURACY
    }

#if CONFIG_TUNE_VMAF
    if (oxcf->tune_cfg.tuning >= AOM_TUNE_VMAF_WITH_PREPROCESSING &&
        oxcf->tune_cfg.tuning <= AOM_TUNE_VMAF_NEG_MAX_GAIN) {
      q = cpi->vmaf_info.original_qindex;
    }
#endif
    if (allow_recode) {
      // Update q and decide whether to do a recode loop
      recode_loop_update_q(cpi, &loop, &q, &q_low, &q_high, top_index,
                           bottom_index, &undershoot_seen, &overshoot_seen,
                           &low_cr_seen, loop_count);
    }

#if CONFIG_TUNE_BUTTERAUGLI
    if (loop_count == 0 && oxcf->tune_cfg.tuning == AOM_TUNE_BUTTERAUGLI) {
      loop = 1;
      av1_setup_butteraugli_rdmult_and_restore_source(cpi, 0.4);
    }
#endif

    if (cpi->use_ducky_encode) {
      // Ducky encode currently does not support recode loop.
      loop = 0;
    }
#if CONFIG_BITRATE_ACCURACY || CONFIG_RD_COMMAND
    loop = 0;  // turn off recode loop when CONFIG_BITRATE_ACCURACY is on
#endif         // CONFIG_BITRATE_ACCURACY || CONFIG_RD_COMMAND

    if (loop) {
      ++loop_count;
      cpi->num_frame_recode =
          (cpi->num_frame_recode < (NUM_RECODES_PER_FRAME - 1))
              ? (cpi->num_frame_recode + 1)
              : (NUM_RECODES_PER_FRAME - 1);
#if CONFIG_INTERNAL_STATS
      ++cpi->frame_recode_hits;
#endif
    }
#if CONFIG_COLLECT_COMPONENT_TIMING
    if (loop) printf("\n Recoding:");
#endif
  } while (loop);

  return AOM_CODEC_OK;
}
#endif  // !CONFIG_REALTIME_ONLY

// TODO(jingning, paulwilkins): Set up high grain level to test
// hardware decoders. Need to adapt the actual noise variance
// according to the difference between reconstructed frame and the
// source signal.
static void set_grain_syn_params(AV1_COMMON *cm) {
  aom_film_grain_t *film_grain_params = &cm->film_grain_params;
  film_grain_params->apply_grain = 1;
  film_grain_params->update_parameters = 1;
  film_grain_params->random_seed = rand() & 0xffff;

  film_grain_params->num_y_points = 1;
  film_grain_params->scaling_points_y[0][0] = 128;
  film_grain_params->scaling_points_y[0][1] = 100;

  if (!cm->seq_params->monochrome) {
    film_grain_params->num_cb_points = 1;
    film_grain_params->scaling_points_cb[0][0] = 128;
    film_grain_params->scaling_points_cb[0][1] = 100;

    film_grain_params->num_cr_points = 1;
    film_grain_params->scaling_points_cr[0][0] = 128;
    film_grain_params->scaling_points_cr[0][1] = 100;
  } else {
    film_grain_params->num_cb_points = 0;
    film_grain_params->num_cr_points = 0;
  }

  film_grain_params->chroma_scaling_from_luma = 0;

  film_grain_params->scaling_shift = 1;
  film_grain_params->ar_coeff_lag = 0;
  film_grain_params->ar_coeff_shift = 1;
  film_grain_params->overlap_flag = 1;
  film_grain_params->grain_scale_shift = 0;
}

/*!\brief Recode loop or a single loop for encoding one frame, followed by
 * in-loop deblocking filters, CDEF filters, and restoration filters.
 *
 * \ingroup high_level_algo
 * \callgraph
 * \callergraph
 *
 * \param[in]    cpi             Top-level encoder structure
 * \param[in]    size            Bitstream size
 * \param[out]   dest            Bitstream output buffer
 * \param[in]    dest_size       Bitstream output buffer size
 * \param[in]    sse             Total distortion of the frame
 * \param[in]    rate            Total rate of the frame
 * \param[in]    largest_tile_id Tile id of the last tile
 *
 * \return Returns a value to indicate if the encoding is done successfully.
 * \retval #AOM_CODEC_OK
 * \retval #AOM_CODEC_ERROR
 */
static int encode_with_recode_loop_and_filter(AV1_COMP *cpi, size_t *size,
                                              uint8_t *dest, size_t dest_size,
                                              int64_t *sse, int64_t *rate,
                                              int *largest_tile_id) {
#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, encode_with_or_without_recode_time);
#endif
  for (int i = 0; i < NUM_RECODES_PER_FRAME; i++) {
    cpi->do_update_frame_probs_txtype[i] = 0;
    cpi->do_update_frame_probs_obmc[i] = 0;
    cpi->do_update_frame_probs_warp[i] = 0;
    cpi->do_update_frame_probs_interpfilter[i] = 0;
  }

  cpi->do_update_vbr_bits_off_target_fast = 0;
  int err;
#if CONFIG_REALTIME_ONLY
  err = encode_without_recode(cpi);
#else
  if (cpi->sf.hl_sf.recode_loop == DISALLOW_RECODE)
    err = encode_without_recode(cpi);
  else
    err = encode_with_recode_loop(cpi, size, dest, dest_size);
#endif
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, encode_with_or_without_recode_time);
#endif
  if (err != AOM_CODEC_OK) {
    if (err == -1) {
      // special case as described in encode_with_recode_loop().
      // Encoding was skipped.
      err = AOM_CODEC_OK;
      if (sse != NULL) *sse = INT64_MAX;
      if (rate != NULL) *rate = INT64_MAX;
      *largest_tile_id = 0;
    }
    return err;
  }

#ifdef OUTPUT_YUV_DENOISED
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  if (oxcf->noise_sensitivity > 0 && denoise_svc(cpi)) {
    aom_write_yuv_frame(yuv_denoised_file,
                        &cpi->denoiser.running_avg_y[INTRA_FRAME]);
  }
#endif

  AV1_COMMON *const cm = &cpi->common;
  SequenceHeader *const seq_params = cm->seq_params;

  // Special case code to reduce pulsing when key frames are forced at a
  // fixed interval. Note the reconstruction error if it is the frame before
  // the force key frame
  if (cpi->ppi->p_rc.next_key_frame_forced && cpi->rc.frames_to_key == 1) {
#if CONFIG_AV1_HIGHBITDEPTH
    if (seq_params->use_highbitdepth) {
      cpi->ambient_err = aom_highbd_get_y_sse(cpi->source, &cm->cur_frame->buf);
    } else {
      cpi->ambient_err = aom_get_y_sse(cpi->source, &cm->cur_frame->buf);
    }
#else
    cpi->ambient_err = aom_get_y_sse(cpi->source, &cm->cur_frame->buf);
#endif
  }

  cm->cur_frame->buf.color_primaries = seq_params->color_primaries;
  cm->cur_frame->buf.transfer_characteristics =
      seq_params->transfer_characteristics;
  cm->cur_frame->buf.matrix_coefficients = seq_params->matrix_coefficients;
  cm->cur_frame->buf.monochrome = seq_params->monochrome;
  cm->cur_frame->buf.chroma_sample_position =
      seq_params->chroma_sample_position;
  cm->cur_frame->buf.color_range = seq_params->color_range;
  cm->cur_frame->buf.render_width = cm->render_width;
  cm->cur_frame->buf.render_height = cm->render_height;

  if (!cpi->mt_info.pipeline_lpf_mt_with_enc)
    set_postproc_filter_default_params(&cpi->common);

  if (!cm->features.allow_intrabc) {
    loopfilter_frame(cpi, cm);
  }

  if (cpi->oxcf.mode != ALLINTRA && !cpi->ppi->rtc_ref.non_reference_frame) {
    extend_frame_borders(cpi);
  }

#ifdef OUTPUT_YUV_REC
  aom_write_one_yuv_frame(cm, &cm->cur_frame->buf);
#endif

  if (cpi->oxcf.tune_cfg.content == AOM_CONTENT_FILM) {
    set_grain_syn_params(cm);
  }

  av1_finalize_encoded_frame(cpi);
  // Build the bitstream
#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, av1_pack_bitstream_final_time);
#endif
  cpi->rc.coefficient_size = 0;
  if (av1_pack_bitstream(cpi, dest, dest_size, size, largest_tile_id) !=
      AOM_CODEC_OK)
    return AOM_CODEC_ERROR;
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, av1_pack_bitstream_final_time);
#endif

  if (cpi->rc.postencode_drop && allow_postencode_drop_rtc(cpi) &&
      av1_postencode_drop_cbr(cpi, size)) {
    return AOM_CODEC_OK;
  }

  // Compute sse and rate.
  if (sse != NULL) {
#if CONFIG_AV1_HIGHBITDEPTH
    *sse = (seq_params->use_highbitdepth)
               ? aom_highbd_get_y_sse(cpi->source, &cm->cur_frame->buf)
               : aom_get_y_sse(cpi->source, &cm->cur_frame->buf);
#else
    *sse = aom_get_y_sse(cpi->source, &cm->cur_frame->buf);
#endif
  }
  if (rate != NULL) {
    const int64_t bits = (*size << 3);
    *rate = (bits << 5);  // To match scale.
  }

#if !CONFIG_REALTIME_ONLY
  if (cpi->use_ducky_encode) {
    PSNR_STATS psnr;
    aom_calc_psnr(cpi->source, &cpi->common.cur_frame->buf, &psnr);
    DuckyEncodeFrameResult *frame_result = &cpi->ducky_encode_info.frame_result;
    frame_result->global_order_idx = cm->cur_frame->display_order_hint;
    frame_result->q_index = cm->quant_params.base_qindex;
    frame_result->rdmult = cpi->rd.RDMULT;
    frame_result->rate = (int)(*size) * 8;
    frame_result->dist = psnr.sse[0];
    frame_result->psnr = psnr.psnr[0];
  }
#endif  // !CONFIG_REALTIME_ONLY

  return AOM_CODEC_OK;
}

static int encode_with_and_without_superres(AV1_COMP *cpi, size_t *size,
                                            uint8_t *dest, size_t dest_size,
                                            int *largest_tile_id) {
  const AV1_COMMON *const cm = &cpi->common;
  assert(cm->seq_params->enable_superres);
  assert(av1_superres_in_recode_allowed(cpi));
  aom_codec_err_t err = AOM_CODEC_OK;
  av1_save_all_coding_context(cpi);

  int64_t sse1 = INT64_MAX;
  int64_t rate1 = INT64_MAX;
  int largest_tile_id1 = 0;
  int64_t sse2 = INT64_MAX;
  int64_t rate2 = INT64_MAX;
  int largest_tile_id2;
  double proj_rdcost1 = DBL_MAX;
  const GF_GROUP *const gf_group = &cpi->ppi->gf_group;
  const FRAME_UPDATE_TYPE update_type =
      gf_group->update_type[cpi->gf_frame_index];
  const aom_bit_depth_t bit_depth = cm->seq_params->bit_depth;

  // Encode with superres.
  if (cpi->sf.hl_sf.superres_auto_search_type == SUPERRES_AUTO_ALL) {
    SuperResCfg *const superres_cfg = &cpi->oxcf.superres_cfg;
    int64_t superres_sses[SCALE_NUMERATOR];
    int64_t superres_rates[SCALE_NUMERATOR];
    int superres_largest_tile_ids[SCALE_NUMERATOR];
    // Use superres for Key-frames and Alt-ref frames only.
    if (update_type != OVERLAY_UPDATE && update_type != INTNL_OVERLAY_UPDATE) {
      for (int denom = SCALE_NUMERATOR + 1; denom <= 2 * SCALE_NUMERATOR;
           ++denom) {
        superres_cfg->superres_scale_denominator = denom;
        superres_cfg->superres_kf_scale_denominator = denom;
        const int this_index = denom - (SCALE_NUMERATOR + 1);

        cpi->superres_mode = AOM_SUPERRES_AUTO;  // Super-res on for this loop.
        err = encode_with_recode_loop_and_filter(
            cpi, size, dest, dest_size, &superres_sses[this_index],
            &superres_rates[this_index],
            &superres_largest_tile_ids[this_index]);
        cpi->superres_mode = AOM_SUPERRES_NONE;  // Reset to default (full-res).
        if (err != AOM_CODEC_OK) return err;
        restore_all_coding_context(cpi);
      }
      // Reset.
      superres_cfg->superres_scale_denominator = SCALE_NUMERATOR;
      superres_cfg->superres_kf_scale_denominator = SCALE_NUMERATOR;
    } else {
      for (int denom = SCALE_NUMERATOR + 1; denom <= 2 * SCALE_NUMERATOR;
           ++denom) {
        const int this_index = denom - (SCALE_NUMERATOR + 1);
        superres_sses[this_index] = INT64_MAX;
        superres_rates[this_index] = INT64_MAX;
        superres_largest_tile_ids[this_index] = 0;
      }
    }
    // Encode without superres.
    assert(cpi->superres_mode == AOM_SUPERRES_NONE);
    err = encode_with_recode_loop_and_filter(cpi, size, dest, dest_size, &sse2,
                                             &rate2, &largest_tile_id2);
    if (err != AOM_CODEC_OK) return err;

    // Note: Both use common rdmult based on base qindex of fullres.
    const int64_t rdmult = av1_compute_rd_mult_based_on_qindex(
        bit_depth, update_type, cm->quant_params.base_qindex,
        cpi->oxcf.tune_cfg.tuning);

    // Find the best rdcost among all superres denoms.
    int best_denom = -1;
    for (int denom = SCALE_NUMERATOR + 1; denom <= 2 * SCALE_NUMERATOR;
         ++denom) {
      const int this_index = denom - (SCALE_NUMERATOR + 1);
      const int64_t this_sse = superres_sses[this_index];
      const int64_t this_rate = superres_rates[this_index];
      const int this_largest_tile_id = superres_largest_tile_ids[this_index];
      const double this_rdcost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
          rdmult, this_rate, this_sse, bit_depth);
      if (this_rdcost < proj_rdcost1) {
        sse1 = this_sse;
        rate1 = this_rate;
        largest_tile_id1 = this_largest_tile_id;
        proj_rdcost1 = this_rdcost;
        best_denom = denom;
      }
    }
    const double proj_rdcost2 =
        RDCOST_DBL_WITH_NATIVE_BD_DIST(rdmult, rate2, sse2, bit_depth);
    // Re-encode with superres if it's better.
    if (proj_rdcost1 < proj_rdcost2) {
      restore_all_coding_context(cpi);
      // TODO(urvang): We should avoid rerunning the recode loop by saving
      // previous output+state, or running encode only for the selected 'q' in
      // previous step.
      // Again, temporarily force the best denom.
      superres_cfg->superres_scale_denominator = best_denom;
      superres_cfg->superres_kf_scale_denominator = best_denom;
      int64_t sse3 = INT64_MAX;
      int64_t rate3 = INT64_MAX;
      cpi->superres_mode =
          AOM_SUPERRES_AUTO;  // Super-res on for this recode loop.
      err = encode_with_recode_loop_and_filter(cpi, size, dest, dest_size,
                                               &sse3, &rate3, largest_tile_id);
      cpi->superres_mode = AOM_SUPERRES_NONE;  // Reset to default (full-res).
      assert(sse1 == sse3);
      assert(rate1 == rate3);
      assert(largest_tile_id1 == *largest_tile_id);
      // Reset.
      superres_cfg->superres_scale_denominator = SCALE_NUMERATOR;
      superres_cfg->superres_kf_scale_denominator = SCALE_NUMERATOR;
    } else {
      *largest_tile_id = largest_tile_id2;
    }
  } else {
    assert(cpi->sf.hl_sf.superres_auto_search_type == SUPERRES_AUTO_DUAL);
    cpi->superres_mode =
        AOM_SUPERRES_AUTO;  // Super-res on for this recode loop.
    err = encode_with_recode_loop_and_filter(cpi, size, dest, dest_size, &sse1,
                                             &rate1, &largest_tile_id1);
    cpi->superres_mode = AOM_SUPERRES_NONE;  // Reset to default (full-res).
    if (err != AOM_CODEC_OK) return err;
    restore_all_coding_context(cpi);
    // Encode without superres.
    assert(cpi->superres_mode == AOM_SUPERRES_NONE);
    err = encode_with_recode_loop_and_filter(cpi, size, dest, dest_size, &sse2,
                                             &rate2, &largest_tile_id2);
    if (err != AOM_CODEC_OK) return err;

    // Note: Both use common rdmult based on base qindex of fullres.
    const int64_t rdmult = av1_compute_rd_mult_based_on_qindex(
        bit_depth, update_type, cm->quant_params.base_qindex,
        cpi->oxcf.tune_cfg.tuning);
    proj_rdcost1 =
        RDCOST_DBL_WITH_NATIVE_BD_DIST(rdmult, rate1, sse1, bit_depth);
    const double proj_rdcost2 =
        RDCOST_DBL_WITH_NATIVE_BD_DIST(rdmult, rate2, sse2, bit_depth);
    // Re-encode with superres if it's better.
    if (proj_rdcost1 < proj_rdcost2) {
      restore_all_coding_context(cpi);
      // TODO(urvang): We should avoid rerunning the recode loop by saving
      // previous output+state, or running encode only for the selected 'q' in
      // previous step.
      int64_t sse3 = INT64_MAX;
      int64_t rate3 = INT64_MAX;
      cpi->superres_mode =
          AOM_SUPERRES_AUTO;  // Super-res on for this recode loop.
      err = encode_with_recode_loop_and_filter(cpi, size, dest, dest_size,
                                               &sse3, &rate3, largest_tile_id);
      cpi->superres_mode = AOM_SUPERRES_NONE;  // Reset to default (full-res).
      assert(sse1 == sse3);
      assert(rate1 == rate3);
      assert(largest_tile_id1 == *largest_tile_id);
    } else {
      *largest_tile_id = largest_tile_id2;
    }
  }

  return err;
}

// Conditions to disable cdf_update mode in selective mode for real-time.
// Handle case for layers, scene change, and resizing.
static inline int selective_disable_cdf_rtc(const AV1_COMP *cpi) {
  const AV1_COMMON *const cm = &cpi->common;
  const RATE_CONTROL *const rc = &cpi->rc;
  // For single layer.
  if (cpi->svc.number_spatial_layers == 1 &&
      cpi->svc.number_temporal_layers == 1) {
    // Don't disable on intra_only, scene change (high_source_sad = 1),
    // or resized frame. To avoid quality loss force enable at
    // for ~30 frames after key or scene/slide change, and
    // after 8 frames since last update if frame_source_sad > 0.
    if (frame_is_intra_only(cm) || is_frame_resize_pending(cpi) ||
        rc->high_source_sad || rc->frames_since_key < 30 ||
        (cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ &&
         cpi->cyclic_refresh->counter_encode_maxq_scene_change < 30) ||
        (cpi->frames_since_last_update > 8 && cpi->rc.frame_source_sad > 0))
      return 0;
    else
      return 1;
  } else if (cpi->svc.number_temporal_layers > 1) {
    // Disable only on top temporal enhancement layer for now.
    return cpi->svc.temporal_layer_id == cpi->svc.number_temporal_layers - 1;
  }
  return 1;
}

#if !CONFIG_REALTIME_ONLY
static void subtract_stats(FIRSTPASS_STATS *section,
                           const FIRSTPASS_STATS *frame) {
  section->frame -= frame->frame;
  section->weight -= frame->weight;
  section->intra_error -= frame->intra_error;
  section->frame_avg_wavelet_energy -= frame->frame_avg_wavelet_energy;
  section->coded_error -= frame->coded_error;
  section->sr_coded_error -= frame->sr_coded_error;
  section->pcnt_inter -= frame->pcnt_inter;
  section->pcnt_motion -= frame->pcnt_motion;
  section->pcnt_second_ref -= frame->pcnt_second_ref;
  section->pcnt_neutral -= frame->pcnt_neutral;
  section->intra_skip_pct -= frame->intra_skip_pct;
  section->inactive_zone_rows -= frame->inactive_zone_rows;
  section->inactive_zone_cols -= frame->inactive_zone_cols;
  section->MVr -= frame->MVr;
  section->mvr_abs -= frame->mvr_abs;
  section->MVc -= frame->MVc;
  section->mvc_abs -= frame->mvc_abs;
  section->MVrv -= frame->MVrv;
  section->MVcv -= frame->MVcv;
  section->mv_in_out_count -= frame->mv_in_out_count;
  section->new_mv_count -= frame->new_mv_count;
  section->count -= frame->count;
  section->duration -= frame->duration;
}

static void calculate_frame_avg_haar_energy(AV1_COMP *cpi) {
  TWO_PASS *const twopass = &cpi->ppi->twopass;
  const FIRSTPASS_STATS *const total_stats =
      twopass->stats_buf_ctx->total_stats;

  if (is_one_pass_rt_params(cpi) ||
      (cpi->oxcf.q_cfg.deltaq_mode != DELTA_Q_PERCEPTUAL) ||
      (is_fp_wavelet_energy_invalid(total_stats) == 0))
    return;

  const int num_mbs = (cpi->oxcf.resize_cfg.resize_mode != RESIZE_NONE)
                          ? cpi->initial_mbs
                          : cpi->common.mi_params.MBs;
  const YV12_BUFFER_CONFIG *const unfiltered_source = cpi->unfiltered_source;
  const uint8_t *const src = unfiltered_source->y_buffer;
  const int hbd = unfiltered_source->flags & YV12_FLAG_HIGHBITDEPTH;
  const int stride = unfiltered_source->y_stride;
  const BLOCK_SIZE fp_block_size =
      get_fp_block_size(cpi->is_screen_content_type);
  const int fp_block_size_width = block_size_wide[fp_block_size];
  const int fp_block_size_height = block_size_high[fp_block_size];
  const int num_unit_cols =
      get_num_blocks(unfiltered_source->y_crop_width, fp_block_size_width);
  const int num_unit_rows =
      get_num_blocks(unfiltered_source->y_crop_height, fp_block_size_height);
  const int num_8x8_cols = num_unit_cols * (fp_block_size_width / 8);
  const int num_8x8_rows = num_unit_rows * (fp_block_size_height / 8);
  int64_t frame_avg_wavelet_energy = av1_haar_ac_sad_mxn_uint8_input(
      src, stride, hbd, num_8x8_rows, num_8x8_cols);

  cpi->twopass_frame.frame_avg_haar_energy =
      log1p((double)frame_avg_wavelet_energy / num_mbs);
}
#endif

/*!\brief Run the final pass encoding for 1-pass/2-pass encoding mode, and pack
 * the bitstream
 *
 * \ingroup high_level_algo
 * \callgraph
 * \callergraph
 *
 * \param[in]    cpi             Top-level encoder structure
 * \param[in]    size            Bitstream size
 * \param[out]   dest            Bitstream output buffer
 * \param[in]    dest_size       Bitstream output buffer size
 *
 * \return Returns a value to indicate if the encoding is done successfully.
 * \retval #AOM_CODEC_OK
 * \retval #AOM_CODEC_ERROR
 */
static int encode_frame_to_data_rate(AV1_COMP *cpi, size_t *size, uint8_t *dest,
                                     size_t dest_size) {
  AV1_COMMON *const cm = &cpi->common;
  SequenceHeader *const seq_params = cm->seq_params;
  CurrentFrame *const current_frame = &cm->current_frame;
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  struct segmentation *const seg = &cm->seg;
  FeatureFlags *const features = &cm->features;
  const TileConfig *const tile_cfg = &oxcf->tile_cfg;
  assert(cpi->source != NULL);
  cpi->td.mb.e_mbd.cur_buf = cpi->source;

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, encode_frame_to_data_rate_time);
#endif

#if !CONFIG_REALTIME_ONLY
  calculate_frame_avg_haar_energy(cpi);
#endif

  // frame type has been decided outside of this function call
  cm->cur_frame->frame_type = current_frame->frame_type;

  cm->tiles.large_scale = tile_cfg->enable_large_scale_tile;
  cm->tiles.single_tile_decoding = tile_cfg->enable_single_tile_decoding;

  features->allow_ref_frame_mvs &= frame_might_allow_ref_frame_mvs(cm);
  // features->allow_ref_frame_mvs needs to be written into the frame header
  // while cm->tiles.large_scale is 1, therefore, "cm->tiles.large_scale=1" case
  // is separated from frame_might_allow_ref_frame_mvs().
  features->allow_ref_frame_mvs &= !cm->tiles.large_scale;

  features->allow_warped_motion = oxcf->motion_mode_cfg.allow_warped_motion &&
                                  frame_might_allow_warped_motion(cm);

  cpi->last_frame_type = current_frame->frame_type;

  if (frame_is_intra_only(cm)) {
    cpi->frames_since_last_update = 0;
  }

  if (frame_is_sframe(cm)) {
    GF_GROUP *gf_group = &cpi->ppi->gf_group;
    // S frame will wipe out any previously encoded altref so we cannot place
    // an overlay frame
    gf_group->update_type[gf_group->size] = GF_UPDATE;
  }

  if (encode_show_existing_frame(cm)) {
#if CONFIG_RATECTRL_LOG && CONFIG_THREE_PASS && CONFIG_BITRATE_ACCURACY
    // TODO(angiebird): Move this into a function.
    if (oxcf->pass == AOM_RC_THIRD_PASS) {
      int frame_coding_idx =
          av1_vbr_rc_frame_coding_idx(&cpi->vbr_rc_info, cpi->gf_frame_index);
      rc_log_frame_encode_param(
          &cpi->rc_log, frame_coding_idx, 1, 255,
          cpi->ppi->gf_group.update_type[cpi->gf_frame_index]);
    }
#endif
    av1_finalize_encoded_frame(cpi);
    // Build the bitstream
    int largest_tile_id = 0;  // Output from bitstream: unused here
    cpi->rc.coefficient_size = 0;
    if (av1_pack_bitstream(cpi, dest, dest_size, size, &largest_tile_id) !=
        AOM_CODEC_OK)
      return AOM_CODEC_ERROR;

    if (seq_params->frame_id_numbers_present_flag &&
        current_frame->frame_type == KEY_FRAME) {
      // Displaying a forward key-frame, so reset the ref buffer IDs
      int display_frame_id = cm->ref_frame_id[cpi->existing_fb_idx_to_show];
      for (int i = 0; i < REF_FRAMES; i++)
        cm->ref_frame_id[i] = display_frame_id;
    }

#if DUMP_RECON_FRAMES == 1
    // NOTE(zoeliu): For debug - Output the filtered reconstructed video.
    av1_dump_filtered_recon_frames(cpi);
#endif  // DUMP_RECON_FRAMES

    // NOTE: Save the new show frame buffer index for --test-code=warn, i.e.,
    //       for the purpose to verify no mismatch between encoder and decoder.
    if (cm->show_frame) cpi->last_show_frame_buf = cm->cur_frame;

#if CONFIG_AV1_TEMPORAL_DENOISING
    av1_denoiser_update_ref_frame(cpi);
#endif

    // Since we allocate a spot for the OVERLAY frame in the gf group, we need
    // to do post-encoding update accordingly.
    av1_set_target_rate(cpi, cm->width, cm->height);

    if (is_psnr_calc_enabled(cpi)) {
      cpi->source =
          realloc_and_scale_source(cpi, cm->cur_frame->buf.y_crop_width,
                                   cm->cur_frame->buf.y_crop_height);
    }

#if !CONFIG_REALTIME_ONLY
    if (cpi->use_ducky_encode) {
      PSNR_STATS psnr;
      aom_calc_psnr(cpi->source, &cpi->common.cur_frame->buf, &psnr);
      DuckyEncodeFrameResult *frame_result =
          &cpi->ducky_encode_info.frame_result;
      frame_result->global_order_idx = cm->cur_frame->display_order_hint;
      frame_result->q_index = cm->quant_params.base_qindex;
      frame_result->rdmult = cpi->rd.RDMULT;
      frame_result->rate = (int)(*size) * 8;
      frame_result->dist = psnr.sse[0];
      frame_result->psnr = psnr.psnr[0];
    }
#endif  // !CONFIG_REALTIME_ONLY

    update_counters_for_show_frame(cpi);
    return AOM_CODEC_OK;
  }

  // Work out whether to force_integer_mv this frame
  if (!is_stat_generation_stage(cpi) &&
      cpi->common.features.allow_screen_content_tools &&
      !frame_is_intra_only(cm) && !cpi->sf.rt_sf.use_nonrd_pick_mode) {
    if (cpi->common.seq_params->force_integer_mv == 2) {
      // Adaptive mode: see what previous frame encoded did
      if (cpi->unscaled_last_source != NULL) {
        features->cur_frame_force_integer_mv = av1_is_integer_mv(
            cpi->source, cpi->unscaled_last_source, &cpi->force_intpel_info);
      } else {
        cpi->common.features.cur_frame_force_integer_mv = 0;
      }
    } else {
      cpi->common.features.cur_frame_force_integer_mv =
          cpi->common.seq_params->force_integer_mv;
    }
  } else {
    cpi->common.features.cur_frame_force_integer_mv = 0;
  }

  // This is used by av1_pack_bitstream. So this needs to be set in case of
  // row-mt where the encoding code will use a temporary structure.
  cpi->td.mb.e_mbd.cur_frame_force_integer_mv =
      cpi->common.features.cur_frame_force_integer_mv;

  // Set default state for segment based loop filter update flags.
  cm->lf.mode_ref_delta_update = 0;

  // Set various flags etc to special state if it is a key frame.
  if (frame_is_intra_only(cm) || frame_is_sframe(cm)) {
    // Reset the loop filter deltas and segmentation map.
    av1_reset_segment_features(cm);

    // If segmentation is enabled force a map update for key frames.
    if (seg->enabled) {
      seg->update_map = 1;
      seg->update_data = 1;
    }
  }
  if (tile_cfg->mtu == 0) {
    cpi->num_tg = tile_cfg->num_tile_groups;
  } else {
    // Use a default value for the purposes of weighting costs in probability
    // updates
    cpi->num_tg = DEFAULT_MAX_NUM_TG;
  }

  // For 1 pass CBR mode: check if we are dropping this frame.
  if (has_no_stats_stage(cpi) && oxcf->rc_cfg.mode == AOM_CBR) {
    // Always drop for spatial enhancement layer if layer bandwidth is 0.
    // Otherwise check for frame-dropping based on buffer level in
    // av1_rc_drop_frame().
    if ((cpi->svc.spatial_layer_id > 0 &&
         cpi->oxcf.rc_cfg.target_bandwidth == 0) ||
        av1_rc_drop_frame(cpi)) {
      cpi->is_dropped_frame = true;
    }
    if (cpi->is_dropped_frame) {
      av1_setup_frame_size(cpi);
      av1_set_mv_search_params(cpi);
      av1_rc_postencode_update_drop_frame(cpi);
      release_scaled_references(cpi);
      cpi->ppi->gf_group.is_frame_dropped[cpi->gf_frame_index] = true;
      // A dropped frame might not be shown but it always takes a slot in the gf
      // group. Therefore, even when it is not shown, we still need to update
      // the relevant frame counters.
      if (cm->show_frame) {
        update_counters_for_show_frame(cpi);
      }
      return AOM_CODEC_OK;
    }
  }

  if (oxcf->tune_cfg.tuning == AOM_TUNE_SSIM ||
      oxcf->tune_cfg.tuning == AOM_TUNE_IQ ||
      oxcf->tune_cfg.tuning == AOM_TUNE_SSIMULACRA2) {
    av1_set_mb_ssim_rdmult_scaling(cpi);
  }
#if CONFIG_SALIENCY_MAP
  else if (oxcf->tune_cfg.tuning == AOM_TUNE_VMAF_SALIENCY_MAP &&
           !(cpi->source->flags & YV12_FLAG_HIGHBITDEPTH)) {
    if (av1_set_saliency_map(cpi) == 0) {
      return AOM_CODEC_MEM_ERROR;
    }
#if !CONFIG_REALTIME_ONLY
    double motion_ratio = av1_setup_motion_ratio(cpi);
#else
    double motion_ratio = 1.0;
#endif
    if (av1_setup_sm_rdmult_scaling_factor(cpi, motion_ratio) == 0) {
      return AOM_CODEC_MEM_ERROR;
    }
  }
#endif
#if CONFIG_TUNE_VMAF
  else if (oxcf->tune_cfg.tuning == AOM_TUNE_VMAF_WITHOUT_PREPROCESSING ||
           oxcf->tune_cfg.tuning == AOM_TUNE_VMAF_MAX_GAIN ||
           oxcf->tune_cfg.tuning == AOM_TUNE_VMAF_NEG_MAX_GAIN) {
    av1_set_mb_vmaf_rdmult_scaling(cpi);
  }
#endif

  if (cpi->oxcf.q_cfg.deltaq_mode == DELTA_Q_PERCEPTUAL_AI &&
      cpi->sf.rt_sf.use_nonrd_pick_mode == 0) {
    av1_init_mb_wiener_var_buffer(cpi);
    av1_set_mb_wiener_variance(cpi);
  }

  if (cpi->oxcf.q_cfg.deltaq_mode == DELTA_Q_USER_RATING_BASED) {
    av1_init_mb_ur_var_buffer(cpi);
    av1_set_mb_ur_variance(cpi);
  }

#if CONFIG_INTERNAL_STATS
  memset(cpi->mode_chosen_counts, 0,
         MAX_MODES * sizeof(*cpi->mode_chosen_counts));
#endif

  if (seq_params->frame_id_numbers_present_flag) {
    /* Non-normative definition of current_frame_id ("frame counter" with
     * wraparound) */
    if (cm->current_frame_id == -1) {
      int lsb, msb;
      /* quasi-random initialization of current_frame_id for a key frame */
      if (cpi->source->flags & YV12_FLAG_HIGHBITDEPTH) {
        lsb = CONVERT_TO_SHORTPTR(cpi->source->y_buffer)[0] & 0xff;
        msb = CONVERT_TO_SHORTPTR(cpi->source->y_buffer)[1] & 0xff;
      } else {
        lsb = cpi->source->y_buffer[0] & 0xff;
        msb = cpi->source->y_buffer[1] & 0xff;
      }
      cm->current_frame_id =
          ((msb << 8) + lsb) % (1 << seq_params->frame_id_length);

      // S_frame is meant for stitching different streams of different
      // resolutions together, so current_frame_id must be the
      // same across different streams of the same content current_frame_id
      // should be the same and not random. 0x37 is a chosen number as start
      // point
      if (oxcf->kf_cfg.sframe_dist != 0) cm->current_frame_id = 0x37;
    } else {
      cm->current_frame_id =
          (cm->current_frame_id + 1 + (1 << seq_params->frame_id_length)) %
          (1 << seq_params->frame_id_length);
    }
  }

  switch (oxcf->algo_cfg.cdf_update_mode) {
    case 0:  // No CDF update for any frames(4~6% compression loss).
      features->disable_cdf_update = 1;
      break;
    case 1:  // Enable CDF update for all frames.
      if (cpi->sf.rt_sf.disable_cdf_update_non_reference_frame &&
          cpi->ppi->rtc_ref.non_reference_frame && cpi->rc.frames_since_key > 2)
        features->disable_cdf_update = 1;
      else if (cpi->sf.rt_sf.selective_cdf_update)
        features->disable_cdf_update = selective_disable_cdf_rtc(cpi);
      else
        features->disable_cdf_update = 0;
      break;
    case 2:
      // Strategically determine at which frames to do CDF update.
      // Currently only enable CDF update for all-intra and no-show frames(1.5%
      // compression loss) for good qualiy or allintra mode.
      if (oxcf->mode == GOOD || oxcf->mode == ALLINTRA) {
        features->disable_cdf_update =
            (frame_is_intra_only(cm) || !cm->show_frame) ? 0 : 1;
      } else {
        features->disable_cdf_update = selective_disable_cdf_rtc(cpi);
      }
      break;
  }

  // Disable cdf update for the INTNL_ARF_UPDATE frame with
  // frame_parallel_level 1.
  if (!cpi->do_frame_data_update &&
      cpi->ppi->gf_group.update_type[cpi->gf_frame_index] == INTNL_ARF_UPDATE) {
    assert(cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] == 1);
    features->disable_cdf_update = 1;
  }

#if !CONFIG_REALTIME_ONLY
  if (cpi->oxcf.tool_cfg.enable_global_motion && !frame_is_intra_only(cm)) {
    // Flush any stale global motion information, which may be left over
    // from a previous frame
    aom_invalidate_pyramid(cpi->source->y_pyramid);
    av1_invalidate_corner_list(cpi->source->corners);
  }
#endif  // !CONFIG_REALTIME_ONLY

  int largest_tile_id = 0;
  if (av1_superres_in_recode_allowed(cpi)) {
    if (encode_with_and_without_superres(cpi, size, dest, dest_size,
                                         &largest_tile_id) != AOM_CODEC_OK) {
      return AOM_CODEC_ERROR;
    }
  } else {
    const aom_superres_mode orig_superres_mode = cpi->superres_mode;  // save
    cpi->superres_mode = cpi->oxcf.superres_cfg.superres_mode;
    if (encode_with_recode_loop_and_filter(cpi, size, dest, dest_size, NULL,
                                           NULL,
                                           &largest_tile_id) != AOM_CODEC_OK) {
      return AOM_CODEC_ERROR;
    }
    cpi->superres_mode = orig_superres_mode;  // restore
  }

  // Update reference frame ids for reference frames this frame will overwrite
  if (seq_params->frame_id_numbers_present_flag) {
    for (int i = 0; i < REF_FRAMES; i++) {
      if ((current_frame->refresh_frame_flags >> i) & 1) {
        cm->ref_frame_id[i] = cm->current_frame_id;
      }
    }
  }

  if (cpi->svc.spatial_layer_id == cpi->svc.number_spatial_layers - 1)
    cpi->svc.num_encoded_top_layer++;

#if DUMP_RECON_FRAMES == 1
  // NOTE(zoeliu): For debug - Output the filtered reconstructed video.
  av1_dump_filtered_recon_frames(cpi);
#endif  // DUMP_RECON_FRAMES

  if (cm->seg.enabled) {
    if (cm->seg.update_map == 0 && cm->last_frame_seg_map) {
      memcpy(cm->cur_frame->seg_map, cm->last_frame_seg_map,
             cm->cur_frame->mi_cols * cm->cur_frame->mi_rows *
                 sizeof(*cm->cur_frame->seg_map));
    }
  }

  int release_scaled_refs = 0;
#if CONFIG_FPMT_TEST
  release_scaled_refs =
      (cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE) ? 1 : 0;
#endif  // CONFIG_FPMT_TEST
  if (release_scaled_refs ||
      cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] == 0) {
    if (frame_is_intra_only(cm) == 0) {
      release_scaled_references(cpi);
    }
  }
#if CONFIG_AV1_TEMPORAL_DENOISING
  av1_denoiser_update_ref_frame(cpi);
#endif

  // NOTE: Save the new show frame buffer index for --test-code=warn, i.e.,
  //       for the purpose to verify no mismatch between encoder and decoder.
  if (cm->show_frame) cpi->last_show_frame_buf = cm->cur_frame;

  if (features->refresh_frame_context == REFRESH_FRAME_CONTEXT_BACKWARD) {
    *cm->fc = cpi->tile_data[largest_tile_id].tctx;
    av1_reset_cdf_symbol_counters(cm->fc);
  }
  if (!cm->tiles.large_scale) {
    cm->cur_frame->frame_context = *cm->fc;
  }

  if (tile_cfg->enable_ext_tile_debug) {
    // (yunqing) This test ensures the correctness of large scale tile coding.
    if (cm->tiles.large_scale && is_stat_consumption_stage(cpi)) {
      char fn[20] = "./fc";
      fn[4] = current_frame->frame_number / 100 + '0';
      fn[5] = (current_frame->frame_number % 100) / 10 + '0';
      fn[6] = (current_frame->frame_number % 10) + '0';
      fn[7] = '\0';
      av1_print_frame_contexts(cm->fc, fn);
    }
  }

  cpi->last_frame_type = current_frame->frame_type;

  if (cm->features.disable_cdf_update) {
    cpi->frames_since_last_update++;
  } else {
    cpi->frames_since_last_update = 1;
  }

  if (cpi->svc.spatial_layer_id == cpi->svc.number_spatial_layers - 1)
    cpi->svc.prev_number_spatial_layers = cpi->svc.number_spatial_layers;

  // Clear the one shot update flags for segmentation map and mode/ref loop
  // filter deltas.
  cm->seg.update_map = 0;
  cm->seg.update_data = 0;
  cm->lf.mode_ref_delta_update = 0;

  if (cm->show_frame) {
    update_counters_for_show_frame(cpi);
  }

#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, encode_frame_to_data_rate_time);
#endif

  return AOM_CODEC_OK;
}

int av1_encode(AV1_COMP *const cpi, uint8_t *const dest, size_t dest_size,
               const EncodeFrameInput *const frame_input,
               const EncodeFrameParams *const frame_params,
               size_t *const frame_size) {
  AV1_COMMON *const cm = &cpi->common;
  CurrentFrame *const current_frame = &cm->current_frame;

  cpi->unscaled_source = frame_input->source;
  cpi->source = frame_input->source;
  cpi->unscaled_last_source = frame_input->last_source;

  current_frame->refresh_frame_flags = frame_params->refresh_frame_flags;
  cm->features.error_resilient_mode = frame_params->error_resilient_mode;
  cm->features.primary_ref_frame = frame_params->primary_ref_frame;
  cm->current_frame.frame_type = frame_params->frame_type;
  cm->show_frame = frame_params->show_frame;
  cpi->ref_frame_flags = frame_params->ref_frame_flags;
  cpi->speed = frame_params->speed;
  cm->show_existing_frame = frame_params->show_existing_frame;
  cpi->existing_fb_idx_to_show = frame_params->existing_fb_idx_to_show;

  memcpy(cm->remapped_ref_idx, frame_params->remapped_ref_idx,
         REF_FRAMES * sizeof(*cm->remapped_ref_idx));

  memcpy(&cpi->refresh_frame, &frame_params->refresh_frame,
         sizeof(cpi->refresh_frame));

  if (current_frame->frame_type == KEY_FRAME &&
      cpi->ppi->gf_group.refbuf_state[cpi->gf_frame_index] == REFBUF_RESET) {
    current_frame->frame_number = 0;
  }

  current_frame->order_hint =
      current_frame->frame_number + frame_params->order_offset;

  current_frame->display_order_hint = current_frame->order_hint;
  current_frame->order_hint %=
      (1 << (cm->seq_params->order_hint_info.order_hint_bits_minus_1 + 1));

  current_frame->pyramid_level = get_true_pyr_level(
      cpi->ppi->gf_group.layer_depth[cpi->gf_frame_index],
      current_frame->display_order_hint, cpi->ppi->gf_group.max_layer_depth);

  if (is_stat_generation_stage(cpi)) {
#if !CONFIG_REALTIME_ONLY
    if (cpi->oxcf.q_cfg.use_fixed_qp_offsets)
      av1_noop_first_pass_frame(cpi, frame_input->ts_duration);
    else
      av1_first_pass(cpi, frame_input->ts_duration);
#endif
  } else if (cpi->oxcf.pass == AOM_RC_ONE_PASS ||
             cpi->oxcf.pass >= AOM_RC_SECOND_PASS) {
    if (encode_frame_to_data_rate(cpi, frame_size, dest, dest_size) !=
        AOM_CODEC_OK) {
      return AOM_CODEC_ERROR;
    }
  } else {
    return AOM_CODEC_ERROR;
  }

  return AOM_CODEC_OK;
}

#if CONFIG_DENOISE && !CONFIG_REALTIME_ONLY
static int apply_denoise_2d(AV1_COMP *cpi, const YV12_BUFFER_CONFIG *sd,
                            int block_size, float noise_level,
                            int64_t time_stamp, int64_t end_time) {
  AV1_COMMON *const cm = &cpi->common;
  if (!cpi->denoise_and_model) {
    cpi->denoise_and_model = aom_denoise_and_model_alloc(
        cm->seq_params->bit_depth, block_size, noise_level);
    if (!cpi->denoise_and_model) {
      aom_set_error(cm->error, AOM_CODEC_MEM_ERROR,
                    "Error allocating denoise and model");
      return -1;
    }
  }
  if (!cpi->film_grain_table) {
    cpi->film_grain_table = aom_malloc(sizeof(*cpi->film_grain_table));
    if (!cpi->film_grain_table) {
      aom_set_error(cm->error, AOM_CODEC_MEM_ERROR,
                    "Error allocating grain table");
      return -1;
    }
    memset(cpi->film_grain_table, 0, sizeof(*cpi->film_grain_table));
  }
  if (aom_denoise_and_model_run(cpi->denoise_and_model, sd,
                                &cm->film_grain_params,
                                cpi->oxcf.enable_dnl_denoising)) {
    if (cm->film_grain_params.apply_grain) {
      aom_film_grain_table_append(cpi->film_grain_table, time_stamp, end_time,
                                  &cm->film_grain_params);
    }
  }
  return 0;
}
#endif

int av1_receive_raw_frame(AV1_COMP *cpi, aom_enc_frame_flags_t frame_flags,
                          const YV12_BUFFER_CONFIG *sd, int64_t time_stamp,
                          int64_t end_time) {
  AV1_COMMON *const cm = &cpi->common;
  const SequenceHeader *const seq_params = cm->seq_params;
  int res = 0;
  const int subsampling_x = sd->subsampling_x;
  const int subsampling_y = sd->subsampling_y;
  const int use_highbitdepth = (sd->flags & YV12_FLAG_HIGHBITDEPTH) != 0;

#if CONFIG_TUNE_VMAF
  if (!is_stat_generation_stage(cpi) &&
      cpi->oxcf.tune_cfg.tuning == AOM_TUNE_VMAF_WITH_PREPROCESSING) {
    av1_vmaf_frame_preprocessing(cpi, sd);
  }
  if (!is_stat_generation_stage(cpi) &&
      cpi->oxcf.tune_cfg.tuning == AOM_TUNE_VMAF_MAX_GAIN) {
    av1_vmaf_blk_preprocessing(cpi, sd);
  }
#endif

#if CONFIG_INTERNAL_STATS
  struct aom_usec_timer timer;
  aom_usec_timer_start(&timer);
#endif

#if CONFIG_AV1_TEMPORAL_DENOISING
  setup_denoiser_buffer(cpi);
#endif

#if CONFIG_DENOISE
  // even if denoise_noise_level is > 0, we don't need need to denoise on pass
  // 1 of 2 if enable_dnl_denoising is disabled since the 2nd pass will be
  // encoding the original (non-denoised) frame
  if (cpi->oxcf.noise_level > 0 && !(cpi->oxcf.pass == AOM_RC_FIRST_PASS &&
                                     !cpi->oxcf.enable_dnl_denoising)) {
#if !CONFIG_REALTIME_ONLY
    // Choose a synthetic noise level for still images for enhanced perceptual
    // quality based on an estimated noise level in the source, but only if
    // the noise level is set on the command line to > 0.
    if (cpi->oxcf.mode == ALLINTRA) {
      // No noise synthesis if source is very clean.
      // Uses a low edge threshold to focus on smooth areas.
      // Increase output noise setting a little compared to measured value.
      double y_noise_level = 0.0;
      av1_estimate_noise_level(sd, &y_noise_level, AOM_PLANE_Y, AOM_PLANE_Y,
                               cm->seq_params->bit_depth, 16);
      cpi->oxcf.noise_level = (float)(y_noise_level - 0.1);
      cpi->oxcf.noise_level = (float)AOMMAX(0.0, cpi->oxcf.noise_level);
      if (cpi->oxcf.noise_level > 0.0) {
        cpi->oxcf.noise_level += (float)0.5;
      }
      cpi->oxcf.noise_level = (float)AOMMIN(5.0, cpi->oxcf.noise_level);
    }

    if (apply_denoise_2d(cpi, sd, cpi->oxcf.noise_block_size,
                         cpi->oxcf.noise_level, time_stamp, end_time) < 0)
      res = -1;
#endif  // !CONFIG_REALTIME_ONLY
  }
#endif  //  CONFIG_DENOISE

  if (av1_lookahead_push(cpi->ppi->lookahead, sd, time_stamp, end_time,
                         use_highbitdepth, cpi->alloc_pyramid, frame_flags)) {
    aom_set_error(cm->error, AOM_CODEC_ERROR, "av1_lookahead_push() failed");
    res = -1;
  }
#if CONFIG_INTERNAL_STATS
  aom_usec_timer_mark(&timer);
  cpi->ppi->total_time_receive_data += aom_usec_timer_elapsed(&timer);
#endif

  // Note: Regarding profile setting, the following checks are added to help
  // choose a proper profile for the input video. The criterion is that all
  // bitstreams must be designated as the lowest profile that match its content.
  // E.G. A bitstream that contains 4:4:4 video must be designated as High
  // Profile in the seq header, and likewise a bitstream that contains 4:2:2
  // bitstream must be designated as Professional Profile in the sequence
  // header.
  if ((seq_params->profile == PROFILE_0) && !seq_params->monochrome &&
      (subsampling_x != 1 || subsampling_y != 1)) {
    aom_set_error(cm->error, AOM_CODEC_INVALID_PARAM,
                  "Non-4:2:0 color format requires profile 1 or 2");
    res = -1;
  }
  if ((seq_params->profile == PROFILE_1) &&
      !(subsampling_x == 0 && subsampling_y == 0)) {
    aom_set_error(cm->error, AOM_CODEC_INVALID_PARAM,
                  "Profile 1 requires 4:4:4 color format");
    res = -1;
  }
  if ((seq_params->profile == PROFILE_2) &&
      (seq_params->bit_depth <= AOM_BITS_10) &&
      !(subsampling_x == 1 && subsampling_y == 0)) {
    aom_set_error(cm->error, AOM_CODEC_INVALID_PARAM,
                  "Profile 2 bit-depth <= 10 requires 4:2:2 color format");
    res = -1;
  }

  return res;
}

#if CONFIG_ENTROPY_STATS
void print_entropy_stats(AV1_PRIMARY *const ppi) {
  if (!ppi->cpi) return;

  if (ppi->cpi->oxcf.pass != 1 &&
      ppi->cpi->common.current_frame.frame_number > 0) {
    fprintf(stderr, "Writing counts.stt\n");
    FILE *f = fopen("counts.stt", "wb");
    fwrite(&ppi->aggregate_fc, sizeof(ppi->aggregate_fc), 1, f);
    fclose(f);
  }
}
#endif  // CONFIG_ENTROPY_STATS

#if CONFIG_INTERNAL_STATS
static void adjust_image_stat(double y, double u, double v, double all,
                              ImageStat *s) {
  s->stat[STAT_Y] += y;
  s->stat[STAT_U] += u;
  s->stat[STAT_V] += v;
  s->stat[STAT_ALL] += all;
  s->worst = AOMMIN(s->worst, all);
}

static void compute_internal_stats(AV1_COMP *cpi, int frame_bytes) {
  AV1_PRIMARY *const ppi = cpi->ppi;
  AV1_COMMON *const cm = &cpi->common;
  double samples = 0.0;
  const uint32_t in_bit_depth = cpi->oxcf.input_cfg.input_bit_depth;
  const uint32_t bit_depth = cpi->td.mb.e_mbd.bd;

  if (cpi->ppi->use_svc &&
      cpi->svc.spatial_layer_id < cpi->svc.number_spatial_layers - 1)
    return;

#if CONFIG_INTER_STATS_ONLY
  if (cm->current_frame.frame_type == KEY_FRAME) return;  // skip key frame
#endif
  cpi->bytes += frame_bytes;
  if (cm->show_frame) {
    const YV12_BUFFER_CONFIG *orig = cpi->source;
    const YV12_BUFFER_CONFIG *recon = &cpi->common.cur_frame->buf;
    double y, u, v, frame_all;

    ppi->count[0]++;
    ppi->count[1]++;
    if (cpi->ppi->b_calculate_psnr) {
      PSNR_STATS psnr;
      double weight[2] = { 0.0, 0.0 };
      double frame_ssim2[2] = { 0.0, 0.0 };
#if CONFIG_AV1_HIGHBITDEPTH
      aom_calc_highbd_psnr(orig, recon, &psnr, bit_depth, in_bit_depth);
#else
      aom_calc_psnr(orig, recon, &psnr);
#endif
      adjust_image_stat(psnr.psnr[1], psnr.psnr[2], psnr.psnr[3], psnr.psnr[0],
                        &(ppi->psnr[0]));
      ppi->total_sq_error[0] += psnr.sse[0];
      ppi->total_samples[0] += psnr.samples[0];
      samples = psnr.samples[0];

      aom_calc_ssim(orig, recon, bit_depth, in_bit_depth,
                    cm->seq_params->use_highbitdepth, weight, frame_ssim2);

      ppi->worst_ssim = AOMMIN(ppi->worst_ssim, frame_ssim2[0]);
      ppi->summed_quality += frame_ssim2[0] * weight[0];
      ppi->summed_weights += weight[0];

#if CONFIG_AV1_HIGHBITDEPTH
      // Compute PSNR based on stream bit depth
      if ((cpi->source->flags & YV12_FLAG_HIGHBITDEPTH) &&
          (in_bit_depth < bit_depth)) {
        adjust_image_stat(psnr.psnr_hbd[1], psnr.psnr_hbd[2], psnr.psnr_hbd[3],
                          psnr.psnr_hbd[0], &ppi->psnr[1]);
        ppi->total_sq_error[1] += psnr.sse_hbd[0];
        ppi->total_samples[1] += psnr.samples_hbd[0];

        ppi->worst_ssim_hbd = AOMMIN(ppi->worst_ssim_hbd, frame_ssim2[1]);
        ppi->summed_quality_hbd += frame_ssim2[1] * weight[1];
        ppi->summed_weights_hbd += weight[1];
      }
#endif

#if 0
      {
        FILE *f = fopen("q_used.stt", "a");
        double y2 = psnr.psnr[1];
        double u2 = psnr.psnr[2];
        double v2 = psnr.psnr[3];
        double frame_psnr2 = psnr.psnr[0];
        fprintf(f, "%5d : Y%f7.3:U%f7.3:V%f7.3:F%f7.3:S%7.3f\n",
                cm->current_frame.frame_number, y2, u2, v2,
                frame_psnr2, frame_ssim2);
        fclose(f);
      }
#endif
    }
    if (ppi->b_calculate_blockiness) {
      if (!cm->seq_params->use_highbitdepth) {
        const double frame_blockiness =
            av1_get_blockiness(orig->y_buffer, orig->y_stride, recon->y_buffer,
                               recon->y_stride, orig->y_width, orig->y_height);
        ppi->worst_blockiness = AOMMAX(ppi->worst_blockiness, frame_blockiness);
        ppi->total_blockiness += frame_blockiness;
      }

      if (ppi->b_calculate_consistency) {
        if (!cm->seq_params->use_highbitdepth) {
          const double this_inconsistency = aom_get_ssim_metrics(
              orig->y_buffer, orig->y_stride, recon->y_buffer, recon->y_stride,
              orig->y_width, orig->y_height, ppi->ssim_vars, &ppi->metrics, 1);

          const double peak = (double)((1 << in_bit_depth) - 1);
          const double consistency =
              aom_sse_to_psnr(samples, peak, ppi->total_inconsistency);
          if (consistency > 0.0)
            ppi->worst_consistency =
                AOMMIN(ppi->worst_consistency, consistency);
          ppi->total_inconsistency += this_inconsistency;
        }
      }
    }

    frame_all =
        aom_calc_fastssim(orig, recon, &y, &u, &v, bit_depth, in_bit_depth);
    adjust_image_stat(y, u, v, frame_all, &ppi->fastssim);
    frame_all = aom_psnrhvs(orig, recon, &y, &u, &v, bit_depth, in_bit_depth);
    adjust_image_stat(y, u, v, frame_all, &ppi->psnrhvs);
  }
}

void print_internal_stats(AV1_PRIMARY *ppi) {
  if (!ppi->cpi) return;
  AV1_COMP *const cpi = ppi->cpi;

  if (ppi->cpi->oxcf.pass != 1 &&
      ppi->cpi->common.current_frame.frame_number > 0) {
    char headings[512] = { 0 };
    char results[512] = { 0 };
    FILE *f = fopen("opsnr.stt", "a");
    double time_encoded =
        (cpi->time_stamps.prev_ts_end - cpi->time_stamps.first_ts_start) /
        10000000.000;
    double total_encode_time =
        (ppi->total_time_receive_data + ppi->total_time_compress_data) /
        1000.000;
    const double dr =
        (double)ppi->total_bytes * (double)8 / (double)1000 / time_encoded;
    const double peak =
        (double)((1 << ppi->cpi->oxcf.input_cfg.input_bit_depth) - 1);
    const double target_rate =
        (double)ppi->cpi->oxcf.rc_cfg.target_bandwidth / 1000;
    const double rate_err = ((100.0 * (dr - target_rate)) / target_rate);

    if (ppi->b_calculate_psnr) {
      const double total_psnr = aom_sse_to_psnr(
          (double)ppi->total_samples[0], peak, (double)ppi->total_sq_error[0]);
      const double total_ssim =
          100 * pow(ppi->summed_quality / ppi->summed_weights, 8.0);
      snprintf(headings, sizeof(headings),
               "Bitrate\tAVGPsnr\tGLBPsnr\tAVPsnrP\tGLPsnrP\t"
               "AOMSSIM\tVPSSIMP\tFASTSIM\tPSNRHVS\t"
               "WstPsnr\tWstSsim\tWstFast\tWstHVS\t"
               "AVPsrnY\tAPsnrCb\tAPsnrCr");
      snprintf(results, sizeof(results),
               "%7.2f\t%7.3f\t%7.3f\t%7.3f\t%7.3f\t"
               "%7.3f\t%7.3f\t%7.3f\t%7.3f\t"
               "%7.3f\t%7.3f\t%7.3f\t%7.3f\t"
               "%7.3f\t%7.3f\t%7.3f",
               dr, ppi->psnr[0].stat[STAT_ALL] / ppi->count[0], total_psnr,
               ppi->psnr[0].stat[STAT_ALL] / ppi->count[0], total_psnr,
               total_ssim, total_ssim,
               ppi->fastssim.stat[STAT_ALL] / ppi->count[0],
               ppi->psnrhvs.stat[STAT_ALL] / ppi->count[0], ppi->psnr[0].worst,
               ppi->worst_ssim, ppi->fastssim.worst, ppi->psnrhvs.worst,
               ppi->psnr[0].stat[STAT_Y] / ppi->count[0],
               ppi->psnr[0].stat[STAT_U] / ppi->count[0],
               ppi->psnr[0].stat[STAT_V] / ppi->count[0]);

      if (ppi->b_calculate_blockiness) {
        SNPRINT(headings, "\t  Block\tWstBlck");
        SNPRINT2(results, "\t%7.3f", ppi->total_blockiness / ppi->count[0]);
        SNPRINT2(results, "\t%7.3f", ppi->worst_blockiness);
      }

      if (ppi->b_calculate_consistency) {
        double consistency =
            aom_sse_to_psnr((double)ppi->total_samples[0], peak,
                            (double)ppi->total_inconsistency);

        SNPRINT(headings, "\tConsist\tWstCons");
        SNPRINT2(results, "\t%7.3f", consistency);
        SNPRINT2(results, "\t%7.3f", ppi->worst_consistency);
      }

      SNPRINT(headings, "\t   Time\tRcErr\tAbsErr");
      SNPRINT2(results, "\t%8.0f", total_encode_time);
      SNPRINT2(results, " %7.2f", rate_err);
      SNPRINT2(results, " %7.2f", fabs(rate_err));

      SNPRINT(headings, "\tAPsnr611");
      SNPRINT2(results, " %7.3f",
               (6 * ppi->psnr[0].stat[STAT_Y] + ppi->psnr[0].stat[STAT_U] +
                ppi->psnr[0].stat[STAT_V]) /
                   (ppi->count[0] * 8));

#if CONFIG_AV1_HIGHBITDEPTH
      const uint32_t in_bit_depth = ppi->cpi->oxcf.input_cfg.input_bit_depth;
      const uint32_t bit_depth = ppi->seq_params.bit_depth;
      // Since cpi->source->flags is not available here, but total_samples[1]
      // will be non-zero if cpi->source->flags & YV12_FLAG_HIGHBITDEPTH was
      // true in compute_internal_stats
      if ((ppi->total_samples[1] > 0) && (in_bit_depth < bit_depth)) {
        const double peak_hbd = (double)((1 << bit_depth) - 1);
        const double total_psnr_hbd =
            aom_sse_to_psnr((double)ppi->total_samples[1], peak_hbd,
                            (double)ppi->total_sq_error[1]);
        const double total_ssim_hbd =
            100 * pow(ppi->summed_quality_hbd / ppi->summed_weights_hbd, 8.0);
        SNPRINT(headings,
                "\t AVGPsnrH GLBPsnrH AVPsnrPH GLPsnrPH"
                " AVPsnrYH APsnrCbH APsnrCrH WstPsnrH"
                " AOMSSIMH VPSSIMPH WstSsimH");
        SNPRINT2(results, "\t%7.3f",
                 ppi->psnr[1].stat[STAT_ALL] / ppi->count[1]);
        SNPRINT2(results, "  %7.3f", total_psnr_hbd);
        SNPRINT2(results, "  %7.3f",
                 ppi->psnr[1].stat[STAT_ALL] / ppi->count[1]);
        SNPRINT2(results, "  %7.3f", total_psnr_hbd);
        SNPRINT2(results, "  %7.3f", ppi->psnr[1].stat[STAT_Y] / ppi->count[1]);
        SNPRINT2(results, "  %7.3f", ppi->psnr[1].stat[STAT_U] / ppi->count[1]);
        SNPRINT2(results, "  %7.3f", ppi->psnr[1].stat[STAT_V] / ppi->count[1]);
        SNPRINT2(results, "  %7.3f", ppi->psnr[1].worst);
        SNPRINT2(results, "  %7.3f", total_ssim_hbd);
        SNPRINT2(results, "  %7.3f", total_ssim_hbd);
        SNPRINT2(results, "  %7.3f", ppi->worst_ssim_hbd);
      }
#endif
      fprintf(f, "%s\n", headings);
      fprintf(f, "%s\n", results);
    }

    fclose(f);

    aom_free(ppi->ssim_vars);
    ppi->ssim_vars = NULL;
  }
}
#endif  // CONFIG_INTERNAL_STATS

static inline void update_keyframe_counters(AV1_COMP *cpi) {
  if (cpi->common.show_frame && cpi->rc.frames_to_key) {
#if !CONFIG_REALTIME_ONLY
    FIRSTPASS_INFO *firstpass_info = &cpi->ppi->twopass.firstpass_info;
    if (firstpass_info->past_stats_count > FIRSTPASS_INFO_STATS_PAST_MIN) {
      av1_firstpass_info_move_cur_index_and_pop(firstpass_info);
    } else {
      // When there is not enough past stats, we move the current
      // index without popping the past stats
      av1_firstpass_info_move_cur_index(firstpass_info);
    }
#endif
    if (cpi->svc.spatial_layer_id == cpi->svc.number_spatial_layers - 1) {
      cpi->rc.frames_since_key++;
      cpi->rc.frames_to_key--;
      cpi->rc.frames_to_fwd_kf--;
      cpi->rc.frames_since_scene_change++;
    }
  }
}

static inline void update_frames_till_gf_update(AV1_COMP *cpi) {
  // TODO(weitinglin): Updating this counter for is_frame_droppable
  // is a work-around to handle the condition when a frame is drop.
  // We should fix the cpi->common.show_frame flag
  // instead of checking the other condition to update the counter properly.
  if (cpi->common.show_frame ||
      is_frame_droppable(&cpi->ppi->rtc_ref, &cpi->ext_flags.refresh_frame)) {
    // Decrement count down till next gf
    if (cpi->rc.frames_till_gf_update_due > 0)
      cpi->rc.frames_till_gf_update_due--;
  }
}

static inline void update_gf_group_index(AV1_COMP *cpi) {
  // Increment the gf group index ready for the next frame.
  if (is_one_pass_rt_params(cpi) &&
      cpi->svc.spatial_layer_id == cpi->svc.number_spatial_layers - 1) {
    ++cpi->gf_frame_index;
    // Reset gf_frame_index in case it reaches MAX_STATIC_GF_GROUP_LENGTH
    // for real time encoding.
    if (cpi->gf_frame_index == MAX_STATIC_GF_GROUP_LENGTH)
      cpi->gf_frame_index = 0;
  } else {
    ++cpi->gf_frame_index;
  }
}

static void update_fb_of_context_type(const AV1_COMP *const cpi,
                                      int *const fb_of_context_type) {
  const AV1_COMMON *const cm = &cpi->common;
  const int current_frame_ref_type = get_current_frame_ref_type(cpi);

  if (frame_is_intra_only(cm) || cm->features.error_resilient_mode ||
      cpi->ext_flags.use_primary_ref_none) {
    for (int i = 0; i < REF_FRAMES; i++) {
      fb_of_context_type[i] = -1;
    }
    fb_of_context_type[current_frame_ref_type] =
        cm->show_frame ? get_ref_frame_map_idx(cm, GOLDEN_FRAME)
                       : get_ref_frame_map_idx(cm, ALTREF_FRAME);
  }

  if (!encode_show_existing_frame(cm)) {
    // Refresh fb_of_context_type[]: see encoder.h for explanation
    if (cm->current_frame.frame_type == KEY_FRAME) {
      // All ref frames are refreshed, pick one that will live long enough
      fb_of_context_type[current_frame_ref_type] = 0;
    } else {
      // If more than one frame is refreshed, it doesn't matter which one we
      // pick so pick the first.  LST sometimes doesn't refresh any: this is ok

      for (int i = 0; i < REF_FRAMES; i++) {
        if (cm->current_frame.refresh_frame_flags & (1 << i)) {
          fb_of_context_type[current_frame_ref_type] = i;
          break;
        }
      }
    }
  }
}

static void update_rc_counts(AV1_COMP *cpi) {
  update_keyframe_counters(cpi);
  update_frames_till_gf_update(cpi);
  update_gf_group_index(cpi);
}

static void update_end_of_frame_stats(AV1_COMP *cpi) {
  if (cpi->do_frame_data_update) {
    // Store current frame loopfilter levels in ppi, if update flag is set.
    if (!cpi->common.show_existing_frame) {
      AV1_COMMON *const cm = &cpi->common;
      struct loopfilter *const lf = &cm->lf;
      cpi->ppi->filter_level[0] = lf->backup_filter_level[0];
      cpi->ppi->filter_level[1] = lf->backup_filter_level[1];
      cpi->ppi->filter_level_u = lf->backup_filter_level_u;
      cpi->ppi->filter_level_v = lf->backup_filter_level_v;
    }
  }
  // Store frame level mv_stats from cpi to ppi.
  cpi->ppi->mv_stats = cpi->mv_stats;
}

// Updates frame level stats related to global motion
static inline void update_gm_stats(AV1_COMP *cpi) {
  FRAME_UPDATE_TYPE update_type =
      cpi->ppi->gf_group.update_type[cpi->gf_frame_index];
  int i, is_gm_present = 0;

  // Check if the current frame has any valid global motion model across its
  // reference frames
  for (i = 0; i < REF_FRAMES; i++) {
    if (cpi->common.global_motion[i].wmtype != IDENTITY) {
      is_gm_present = 1;
      break;
    }
  }
  int update_actual_stats = 1;
#if CONFIG_FPMT_TEST
  update_actual_stats =
      (cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE) ? 0 : 1;
  if (!update_actual_stats) {
    if (cpi->ppi->temp_valid_gm_model_found[update_type] == INT32_MAX) {
      cpi->ppi->temp_valid_gm_model_found[update_type] = is_gm_present;
    } else {
      cpi->ppi->temp_valid_gm_model_found[update_type] |= is_gm_present;
    }
    int show_existing_between_parallel_frames =
        (cpi->ppi->gf_group.update_type[cpi->gf_frame_index] ==
             INTNL_OVERLAY_UPDATE &&
         cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index + 1] == 2);
    if (cpi->do_frame_data_update == 1 &&
        !show_existing_between_parallel_frames) {
      for (i = 0; i < FRAME_UPDATE_TYPES; i++) {
        cpi->ppi->valid_gm_model_found[i] =
            cpi->ppi->temp_valid_gm_model_found[i];
      }
    }
  }
#endif
  if (update_actual_stats) {
    if (cpi->ppi->valid_gm_model_found[update_type] == INT32_MAX) {
      cpi->ppi->valid_gm_model_found[update_type] = is_gm_present;
    } else {
      cpi->ppi->valid_gm_model_found[update_type] |= is_gm_present;
    }
  }
}

void av1_post_encode_updates(AV1_COMP *const cpi,
                             const AV1_COMP_DATA *const cpi_data) {
  AV1_PRIMARY *const ppi = cpi->ppi;
  AV1_COMMON *const cm = &cpi->common;

  update_gm_stats(cpi);

#if !CONFIG_REALTIME_ONLY
  // Update the total stats remaining structure.
  if (cpi->twopass_frame.this_frame != NULL &&
      ppi->twopass.stats_buf_ctx->total_left_stats) {
    subtract_stats(ppi->twopass.stats_buf_ctx->total_left_stats,
                   cpi->twopass_frame.this_frame);
  }
#endif

#if CONFIG_OUTPUT_FRAME_SIZE
  FILE *f = fopen("frame_sizes.csv", "a");
  fprintf(f, "%d,", 8 * (int)cpi_data->frame_size);
  fprintf(f, "%d\n", cm->quant_params.base_qindex);
  fclose(f);
#endif  // CONFIG_OUTPUT_FRAME_SIZE

  if (!is_stat_generation_stage(cpi) && !cpi->is_dropped_frame) {
    // Before calling refresh_reference_frames(), copy ppi->ref_frame_map_copy
    // to cm->ref_frame_map for frame_parallel_level 2 frame in a parallel
    // encode set of lower layer frames.
    // TODO(Remya): Move ref_frame_map from AV1_COMMON to AV1_PRIMARY to avoid
    // copy.
    if (ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] == 2 &&
        ppi->gf_group.frame_parallel_level[cpi->gf_frame_index - 1] == 1 &&
        ppi->gf_group.update_type[cpi->gf_frame_index - 1] ==
            INTNL_ARF_UPDATE) {
      memcpy(cm->ref_frame_map, ppi->ref_frame_map_copy,
             sizeof(cm->ref_frame_map));
    }
    refresh_reference_frames(cpi);
    // For frame_parallel_level 1 frame in a parallel encode set of lower layer
    // frames, store the updated cm->ref_frame_map in ppi->ref_frame_map_copy.
    if (ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] == 1 &&
        ppi->gf_group.update_type[cpi->gf_frame_index] == INTNL_ARF_UPDATE) {
      memcpy(ppi->ref_frame_map_copy, cm->ref_frame_map,
             sizeof(cm->ref_frame_map));
    }
    av1_rc_postencode_update(cpi, cpi_data->frame_size);
  }

  if (cpi_data->pop_lookahead == 1) {
    av1_lookahead_pop(cpi->ppi->lookahead, cpi_data->flush,
                      cpi->compressor_stage);
  }
  if (cpi->common.show_frame) {
    cpi->ppi->ts_start_last_show_frame = cpi_data->ts_frame_start;
    cpi->ppi->ts_end_last_show_frame = cpi_data->ts_frame_end;
  }
  if (ppi->level_params.keep_level_stats && !is_stat_generation_stage(cpi)) {
    // Initialize level info. at the beginning of each sequence.
    if (cm->current_frame.frame_type == KEY_FRAME &&
        ppi->gf_group.refbuf_state[cpi->gf_frame_index] == REFBUF_RESET) {
      av1_init_level_info(cpi);
    }
    av1_update_level_info(cpi, cpi_data->frame_size, cpi_data->ts_frame_start,
                          cpi_data->ts_frame_end);
  }

  if (!is_stat_generation_stage(cpi)) {
#if !CONFIG_REALTIME_ONLY
    if (!has_no_stats_stage(cpi)) av1_twopass_postencode_update(cpi);
#endif
    update_fb_of_context_type(cpi, ppi->fb_of_context_type);
    update_rc_counts(cpi);
    update_end_of_frame_stats(cpi);
  }

#if CONFIG_THREE_PASS
  if (cpi->oxcf.pass == AOM_RC_THIRD_PASS && cpi->third_pass_ctx) {
    av1_pop_third_pass_info(cpi->third_pass_ctx);
  }
#endif

  if (ppi->rtc_ref.set_ref_frame_config && !cpi->is_dropped_frame) {
    av1_svc_update_buffer_slot_refreshed(cpi);
    av1_svc_set_reference_was_previous(cpi);
  }

  if (ppi->use_svc) av1_save_layer_context(cpi);

  // Note *size = 0 indicates a dropped frame for which psnr is not calculated
  if (ppi->b_calculate_psnr && cpi_data->frame_size > 0) {
    if (cm->show_existing_frame ||
        (!is_stat_generation_stage(cpi) && cm->show_frame)) {
      generate_psnr_packet(cpi);
    }
  }

#if CONFIG_INTERNAL_STATS
  if (!is_stat_generation_stage(cpi)) {
    compute_internal_stats(cpi, (int)cpi_data->frame_size);
  }
#endif  // CONFIG_INTERNAL_STATS

#if CONFIG_THREE_PASS
  // Write frame info. Subtract 1 from frame index since if was incremented in
  // update_rc_counts.
  av1_write_second_pass_per_frame_info(cpi, cpi->gf_frame_index - 1);
#endif
}

int av1_get_compressed_data(AV1_COMP *cpi, AV1_COMP_DATA *const cpi_data) {
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  AV1_COMMON *const cm = &cpi->common;

  // The jmp_buf is valid only for the duration of the function that calls
  // setjmp(). Therefore, this function must reset the 'setjmp' field to 0
  // before it returns.
  if (setjmp(cm->error->jmp)) {
    cm->error->setjmp = 0;
    return cm->error->error_code;
  }
  cm->error->setjmp = 1;

#if CONFIG_INTERNAL_STATS
  cpi->frame_recode_hits = 0;
  cpi->time_compress_data = 0;
  cpi->bytes = 0;
#endif
#if CONFIG_ENTROPY_STATS
  if (cpi->compressor_stage == ENCODE_STAGE) {
    av1_zero(cpi->counts);
  }
#endif

#if CONFIG_BITSTREAM_DEBUG
  assert(cpi->oxcf.max_threads <= 1 &&
         "bitstream debug tool does not support multithreading");
  bitstream_queue_record_write();

  if (cm->seq_params->order_hint_info.enable_order_hint) {
    aom_bitstream_queue_set_frame_write(cm->current_frame.order_hint * 2 +
                                        cm->show_frame);
  } else {
    // This is currently used in RTC encoding. cm->show_frame is always 1.
    aom_bitstream_queue_set_frame_write(cm->current_frame.frame_number);
  }
#endif
  if (cpi->ppi->use_svc) {
    av1_one_pass_cbr_svc_start_layer(cpi);
  }

  cpi->is_dropped_frame = false;
  cm->showable_frame = 0;
  cpi_data->frame_size = 0;
  cpi->available_bs_size = cpi_data->cx_data_sz;
#if CONFIG_INTERNAL_STATS
  struct aom_usec_timer cmptimer;
  aom_usec_timer_start(&cmptimer);
#endif
  av1_set_high_precision_mv(cpi, 1, 0);

  // Normal defaults
  cm->features.refresh_frame_context =
      oxcf->tool_cfg.frame_parallel_decoding_mode
          ? REFRESH_FRAME_CONTEXT_DISABLED
          : REFRESH_FRAME_CONTEXT_BACKWARD;
  if (oxcf->tile_cfg.enable_large_scale_tile)
    cm->features.refresh_frame_context = REFRESH_FRAME_CONTEXT_DISABLED;

  if (assign_cur_frame_new_fb(cm) == NULL) {
    aom_internal_error(cpi->common.error, AOM_CODEC_ERROR,
                       "Failed to allocate new cur_frame");
  }

#if CONFIG_COLLECT_COMPONENT_TIMING
  // Accumulate 2nd pass time in 2-pass case or 1 pass time in 1-pass case.
  if (cpi->oxcf.pass == 2 || cpi->oxcf.pass == 0)
    start_timing(cpi, av1_encode_strategy_time);
#endif

  const int result = av1_encode_strategy(
      cpi, &cpi_data->frame_size, cpi_data->cx_data, cpi_data->cx_data_sz,
      &cpi_data->lib_flags, &cpi_data->ts_frame_start, &cpi_data->ts_frame_end,
      cpi_data->timestamp_ratio, &cpi_data->pop_lookahead, cpi_data->flush);

#if CONFIG_COLLECT_COMPONENT_TIMING
  if (cpi->oxcf.pass == 2 || cpi->oxcf.pass == 0)
    end_timing(cpi, av1_encode_strategy_time);

  // Print out timing information.
  // Note: Use "cpi->frame_component_time[0] > 100 us" to avoid showing of
  // show_existing_frame and lag-in-frames.
  if ((cpi->oxcf.pass == 2 || cpi->oxcf.pass == 0) &&
      cpi->frame_component_time[0] > 100) {
    int i;
    uint64_t frame_total = 0, total = 0;
    const GF_GROUP *const gf_group = &cpi->ppi->gf_group;
    FRAME_UPDATE_TYPE frame_update_type =
        get_frame_update_type(gf_group, cpi->gf_frame_index);

    fprintf(stderr,
            "\n Frame number: %d, Frame type: %s, Show Frame: %d, Frame Update "
            "Type: %d, Q: %d\n",
            cm->current_frame.frame_number,
            get_frame_type_enum(cm->current_frame.frame_type), cm->show_frame,
            frame_update_type, cm->quant_params.base_qindex);
    for (i = 0; i < kTimingComponents; i++) {
      cpi->component_time[i] += cpi->frame_component_time[i];
      // Use av1_encode_strategy_time (i = 0) as the total time.
      if (i == 0) {
        frame_total = cpi->frame_component_time[0];
        total = cpi->component_time[0];
      }
      fprintf(stderr,
              " %50s:  %15" PRId64 " us [%6.2f%%] (total: %15" PRId64
              " us [%6.2f%%])\n",
              get_component_name(i), cpi->frame_component_time[i],
              (float)((float)cpi->frame_component_time[i] * 100.0 /
                      (float)frame_total),
              cpi->component_time[i],
              (float)((float)cpi->component_time[i] * 100.0 / (float)total));
      cpi->frame_component_time[i] = 0;
    }
  }
#endif

  // Reset the flag to 0 afer encoding.
  cpi->rc.use_external_qp_one_pass = 0;

  if (result == -1) {
    cm->error->setjmp = 0;
    // Returning -1 indicates no frame encoded; more input is required
    return -1;
  }
  if (result != AOM_CODEC_OK) {
    aom_internal_error(cpi->common.error, AOM_CODEC_ERROR,
                       "Failed to encode frame");
  }
#if CONFIG_INTERNAL_STATS
  aom_usec_timer_mark(&cmptimer);
  cpi->time_compress_data += aom_usec_timer_elapsed(&cmptimer);
#endif  // CONFIG_INTERNAL_STATS

#if CONFIG_SPEED_STATS
  if (!is_stat_generation_stage(cpi) && !cm->show_existing_frame) {
    cpi->tx_search_count += cpi->td.mb.txfm_search_info.tx_search_count;
    cpi->td.mb.txfm_search_info.tx_search_count = 0;
  }
#endif  // CONFIG_SPEED_STATS

  cm->error->setjmp = 0;
  return AOM_CODEC_OK;
}

// Populates cpi->scaled_ref_buf corresponding to frames in a parallel encode
// set. Also sets the bitmask 'ref_buffers_used_map'.
static void scale_references_fpmt(AV1_COMP *cpi, int *ref_buffers_used_map) {
  AV1_COMMON *cm = &cpi->common;
  MV_REFERENCE_FRAME ref_frame;

  for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ++ref_frame) {
    // Need to convert from AOM_REFFRAME to index into ref_mask (subtract 1).
    if (cpi->ref_frame_flags & av1_ref_frame_flag_list[ref_frame]) {
      const YV12_BUFFER_CONFIG *const ref =
          get_ref_frame_yv12_buf(cm, ref_frame);

      if (ref == NULL) {
        cpi->scaled_ref_buf[ref_frame - 1] = NULL;
        continue;
      }

      // FPMT does not support scaling yet.
      assert(ref->y_crop_width == cm->width &&
             ref->y_crop_height == cm->height);

      RefCntBuffer *buf = get_ref_frame_buf(cm, ref_frame);
      cpi->scaled_ref_buf[ref_frame - 1] = buf;
      for (int i = 0; i < cm->buffer_pool->num_frame_bufs; ++i) {
        if (&cm->buffer_pool->frame_bufs[i] == buf) {
          *ref_buffers_used_map |= (1 << i);
        }
      }
    } else {
      if (!has_no_stats_stage(cpi)) cpi->scaled_ref_buf[ref_frame - 1] = NULL;
    }
  }
}

// Increments the ref_count of frame buffers referenced by cpi->scaled_ref_buf
// corresponding to frames in a parallel encode set.
static void increment_scaled_ref_counts_fpmt(BufferPool *buffer_pool,
                                             int ref_buffers_used_map) {
  for (int i = 0; i < buffer_pool->num_frame_bufs; ++i) {
    if (ref_buffers_used_map & (1 << i)) {
      ++buffer_pool->frame_bufs[i].ref_count;
    }
  }
}

// Releases cpi->scaled_ref_buf corresponding to frames in a parallel encode
// set.
void av1_release_scaled_references_fpmt(AV1_COMP *cpi) {
  // TODO(isbs): only refresh the necessary frames, rather than all of them
  for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    RefCntBuffer *const buf = cpi->scaled_ref_buf[i];
    if (buf != NULL) {
      cpi->scaled_ref_buf[i] = NULL;
    }
  }
}

// Decrements the ref_count of frame buffers referenced by cpi->scaled_ref_buf
// corresponding to frames in a parallel encode set.
void av1_decrement_ref_counts_fpmt(BufferPool *buffer_pool,
                                   int ref_buffers_used_map) {
  for (int i = 0; i < buffer_pool->num_frame_bufs; ++i) {
    if (ref_buffers_used_map & (1 << i)) {
      --buffer_pool->frame_bufs[i].ref_count;
    }
  }
}

// Initialize parallel frame contexts with screen content decisions.
void av1_init_sc_decisions(AV1_PRIMARY *const ppi) {
  AV1_COMP *const first_cpi = ppi->cpi;
  for (int i = 1; i < ppi->num_fp_contexts; ++i) {
    AV1_COMP *cur_cpi = ppi->parallel_cpi[i];
    cur_cpi->common.features.allow_screen_content_tools =
        first_cpi->common.features.allow_screen_content_tools;
    cur_cpi->common.features.allow_intrabc =
        first_cpi->common.features.allow_intrabc;
    cur_cpi->use_screen_content_tools = first_cpi->use_screen_content_tools;
    cur_cpi->is_screen_content_type = first_cpi->is_screen_content_type;
  }
}

AV1_COMP *av1_get_parallel_frame_enc_data(AV1_PRIMARY *const ppi,
                                          AV1_COMP_DATA *const first_cpi_data) {
  int cpi_idx = 0;

  // Loop over parallel_cpi to find the cpi that processed the current
  // gf_frame_index ahead of time.
  for (int i = 1; i < ppi->num_fp_contexts; i++) {
    if (ppi->cpi->gf_frame_index == ppi->parallel_cpi[i]->gf_frame_index) {
      cpi_idx = i;
      break;
    }
  }

  assert(cpi_idx > 0);
  assert(!ppi->parallel_cpi[cpi_idx]->common.show_existing_frame);

  // Release the previously-used frame-buffer.
  if (ppi->cpi->common.cur_frame != NULL) {
    --ppi->cpi->common.cur_frame->ref_count;
    ppi->cpi->common.cur_frame = NULL;
  }

  // Swap the appropriate parallel_cpi with the parallel_cpi[0].
  ppi->cpi = ppi->parallel_cpi[cpi_idx];
  ppi->parallel_cpi[cpi_idx] = ppi->parallel_cpi[0];
  ppi->parallel_cpi[0] = ppi->cpi;

  // Copy appropriate parallel_frames_data to local data.
  {
    AV1_COMP_DATA *data = &ppi->parallel_frames_data[cpi_idx - 1];
    assert(data->frame_size > 0);
    if (data->frame_size > first_cpi_data->cx_data_sz) {
      aom_internal_error(&ppi->error, AOM_CODEC_ERROR,
                         "first_cpi_data->cx_data buffer full");
    }

    first_cpi_data->lib_flags = data->lib_flags;
    first_cpi_data->ts_frame_start = data->ts_frame_start;
    first_cpi_data->ts_frame_end = data->ts_frame_end;
    memcpy(first_cpi_data->cx_data, data->cx_data, data->frame_size);
    first_cpi_data->frame_size = data->frame_size;
    if (ppi->cpi->common.show_frame) {
      first_cpi_data->pop_lookahead = 1;
    }
  }

  return ppi->cpi;
}

// Initialises frames belonging to a parallel encode set.
int av1_init_parallel_frame_context(const AV1_COMP_DATA *const first_cpi_data,
                                    AV1_PRIMARY *const ppi,
                                    int *ref_buffers_used_map) {
  AV1_COMP *const first_cpi = ppi->cpi;
  GF_GROUP *const gf_group = &ppi->gf_group;
  int gf_index_start = first_cpi->gf_frame_index;
  assert(gf_group->frame_parallel_level[gf_index_start] == 1);
  int parallel_frame_count = 0;
  int cur_frame_num = first_cpi->common.current_frame.frame_number;
  int show_frame_count = first_cpi->frame_index_set.show_frame_count;
  int frames_since_key = first_cpi->rc.frames_since_key;
  int frames_to_key = first_cpi->rc.frames_to_key;
  int frames_to_fwd_kf = first_cpi->rc.frames_to_fwd_kf;
  int cur_frame_disp = cur_frame_num + gf_group->arf_src_offset[gf_index_start];
  const FIRSTPASS_STATS *stats_in = first_cpi->twopass_frame.stats_in;

  assert(*ref_buffers_used_map == 0);

  // Release the previously used frame-buffer by a frame_parallel_level 1 frame.
  if (first_cpi->common.cur_frame != NULL) {
    --first_cpi->common.cur_frame->ref_count;
    first_cpi->common.cur_frame = NULL;
  }

  RefFrameMapPair ref_frame_map_pairs[REF_FRAMES];
  RefFrameMapPair first_ref_frame_map_pairs[REF_FRAMES];
  init_ref_map_pair(first_cpi, first_ref_frame_map_pairs);
  memcpy(ref_frame_map_pairs, first_ref_frame_map_pairs,
         sizeof(RefFrameMapPair) * REF_FRAMES);

  // Store the reference refresh index of frame_parallel_level 1 frame in a
  // parallel encode set of lower layer frames.
  if (gf_group->update_type[gf_index_start] == INTNL_ARF_UPDATE) {
    first_cpi->ref_refresh_index = av1_calc_refresh_idx_for_intnl_arf(
        first_cpi, ref_frame_map_pairs, gf_index_start);
    assert(first_cpi->ref_refresh_index != INVALID_IDX &&
           first_cpi->ref_refresh_index < REF_FRAMES);
    first_cpi->refresh_idx_available = true;
    // Update ref_frame_map_pairs.
    ref_frame_map_pairs[first_cpi->ref_refresh_index].disp_order =
        gf_group->display_idx[gf_index_start];
    ref_frame_map_pairs[first_cpi->ref_refresh_index].pyr_level =
        gf_group->layer_depth[gf_index_start];
  }

  // Set do_frame_data_update flag as false for frame_parallel_level 1 frame.
  first_cpi->do_frame_data_update = false;
  if (gf_group->arf_src_offset[gf_index_start] == 0) {
    first_cpi->time_stamps.prev_ts_start = ppi->ts_start_last_show_frame;
    first_cpi->time_stamps.prev_ts_end = ppi->ts_end_last_show_frame;
  }

  av1_get_ref_frames(first_ref_frame_map_pairs, cur_frame_disp, first_cpi,
                     gf_index_start, 1, first_cpi->common.remapped_ref_idx);

  scale_references_fpmt(first_cpi, ref_buffers_used_map);
  parallel_frame_count++;

  // Iterate through the GF_GROUP to find the remaining frame_parallel_level 2
  // frames which are part of the current parallel encode set and initialize the
  // required cpi elements.
  for (int i = gf_index_start + 1; i < gf_group->size; i++) {
    // Update frame counters if previous frame was show frame or show existing
    // frame.
    if (gf_group->arf_src_offset[i - 1] == 0) {
      cur_frame_num++;
      show_frame_count++;
      if (frames_to_fwd_kf <= 0)
        frames_to_fwd_kf = first_cpi->oxcf.kf_cfg.fwd_kf_dist;
      if (frames_to_key) {
        frames_since_key++;
        frames_to_key--;
        frames_to_fwd_kf--;
      }
      stats_in++;
    }
    cur_frame_disp = cur_frame_num + gf_group->arf_src_offset[i];
    if (gf_group->frame_parallel_level[i] == 2) {
      AV1_COMP *cur_cpi = ppi->parallel_cpi[parallel_frame_count];
      AV1_COMP_DATA *cur_cpi_data =
          &ppi->parallel_frames_data[parallel_frame_count - 1];
      cur_cpi->gf_frame_index = i;
      cur_cpi->framerate = first_cpi->framerate;
      cur_cpi->common.current_frame.frame_number = cur_frame_num;
      cur_cpi->common.current_frame.frame_type = gf_group->frame_type[i];
      cur_cpi->frame_index_set.show_frame_count = show_frame_count;
      cur_cpi->rc.frames_since_key = frames_since_key;
      cur_cpi->rc.frames_to_key = frames_to_key;
      cur_cpi->rc.frames_to_fwd_kf = frames_to_fwd_kf;
      cur_cpi->rc.active_worst_quality = first_cpi->rc.active_worst_quality;
      cur_cpi->rc.avg_frame_bandwidth = first_cpi->rc.avg_frame_bandwidth;
      cur_cpi->rc.max_frame_bandwidth = first_cpi->rc.max_frame_bandwidth;
      cur_cpi->rc.min_frame_bandwidth = first_cpi->rc.min_frame_bandwidth;
      cur_cpi->rc.intervals_till_gf_calculate_due =
          first_cpi->rc.intervals_till_gf_calculate_due;
      cur_cpi->mv_search_params.max_mv_magnitude =
          first_cpi->mv_search_params.max_mv_magnitude;
      if (gf_group->update_type[cur_cpi->gf_frame_index] == INTNL_ARF_UPDATE) {
        cur_cpi->common.lf.mode_ref_delta_enabled = 1;
      }
      cur_cpi->do_frame_data_update = false;
      // Initialize prev_ts_start and prev_ts_end for show frame(s) and show
      // existing frame(s).
      if (gf_group->arf_src_offset[i] == 0) {
        // Choose source of prev frame.
        int src_index = gf_group->src_offset[i];
        struct lookahead_entry *prev_source = av1_lookahead_peek(
            ppi->lookahead, src_index - 1, cur_cpi->compressor_stage);
        // Save timestamps of prev frame.
        cur_cpi->time_stamps.prev_ts_start = prev_source->ts_start;
        cur_cpi->time_stamps.prev_ts_end = prev_source->ts_end;
      }
      cur_cpi->time_stamps.first_ts_start =
          first_cpi->time_stamps.first_ts_start;

      memcpy(cur_cpi->common.ref_frame_map, first_cpi->common.ref_frame_map,
             sizeof(first_cpi->common.ref_frame_map));
      cur_cpi_data->lib_flags = 0;
      cur_cpi_data->timestamp_ratio = first_cpi_data->timestamp_ratio;
      cur_cpi_data->flush = first_cpi_data->flush;
      cur_cpi_data->frame_size = 0;
      if (gf_group->update_type[gf_index_start] == INTNL_ARF_UPDATE) {
        // If the first frame in a parallel encode set is INTNL_ARF_UPDATE
        // frame, initialize lib_flags of frame_parallel_level 2 frame in the
        // set with that of frame_parallel_level 1 frame.
        cur_cpi_data->lib_flags = first_cpi_data->lib_flags;
        // Store the reference refresh index of frame_parallel_level 2 frame in
        // a parallel encode set of lower layer frames.
        cur_cpi->ref_refresh_index =
            av1_calc_refresh_idx_for_intnl_arf(cur_cpi, ref_frame_map_pairs, i);
        cur_cpi->refresh_idx_available = true;
        // Skip the reference frame which will be refreshed by
        // frame_parallel_level 1 frame in a parallel encode set of lower layer
        // frames.
        cur_cpi->ref_idx_to_skip = first_cpi->ref_refresh_index;
      } else {
        cur_cpi->ref_idx_to_skip = INVALID_IDX;
        cur_cpi->ref_refresh_index = INVALID_IDX;
        cur_cpi->refresh_idx_available = false;
      }
      cur_cpi->twopass_frame.stats_in = stats_in;

      av1_get_ref_frames(first_ref_frame_map_pairs, cur_frame_disp, cur_cpi, i,
                         1, cur_cpi->common.remapped_ref_idx);
      scale_references_fpmt(cur_cpi, ref_buffers_used_map);
      parallel_frame_count++;
    }

    // Set do_frame_data_update to true for the last frame_parallel_level 2
    // frame in the current parallel encode set.
    if (i == (gf_group->size - 1) ||
        (gf_group->frame_parallel_level[i + 1] == 0 &&
         (gf_group->update_type[i + 1] == ARF_UPDATE ||
          gf_group->update_type[i + 1] == INTNL_ARF_UPDATE)) ||
        gf_group->frame_parallel_level[i + 1] == 1) {
      ppi->parallel_cpi[parallel_frame_count - 1]->do_frame_data_update = true;
      break;
    }
  }

  increment_scaled_ref_counts_fpmt(first_cpi->common.buffer_pool,
                                   *ref_buffers_used_map);

  // Return the number of frames in the parallel encode set.
  return parallel_frame_count;
}

int av1_get_preview_raw_frame(AV1_COMP *cpi, YV12_BUFFER_CONFIG *dest) {
  AV1_COMMON *cm = &cpi->common;
  if (!cm->show_frame) {
    return -1;
  } else {
    int ret;
    if (cm->cur_frame != NULL && !cpi->oxcf.algo_cfg.skip_postproc_filtering) {
      *dest = cm->cur_frame->buf;
      dest->y_width = cm->width;
      dest->y_height = cm->height;
      dest->uv_width = cm->width >> cm->seq_params->subsampling_x;
      dest->uv_height = cm->height >> cm->seq_params->subsampling_y;
      ret = 0;
    } else {
      ret = -1;
    }
    return ret;
  }
}

int av1_get_last_show_frame(AV1_COMP *cpi, YV12_BUFFER_CONFIG *frame) {
  if (cpi->last_show_frame_buf == NULL ||
      cpi->oxcf.algo_cfg.skip_postproc_filtering)
    return -1;

  *frame = cpi->last_show_frame_buf->buf;
  return 0;
}

aom_codec_err_t av1_copy_new_frame_enc(AV1_COMMON *cm,
                                       YV12_BUFFER_CONFIG *new_frame,
                                       YV12_BUFFER_CONFIG *sd) {
  const int num_planes = av1_num_planes(cm);
  if (!equal_dimensions_and_border(new_frame, sd))
    aom_internal_error(cm->error, AOM_CODEC_ERROR,
                       "Incorrect buffer dimensions");
  else
    aom_yv12_copy_frame(new_frame, sd, num_planes);

  return cm->error->error_code;
}

int av1_set_internal_size(AV1EncoderConfig *const oxcf,
                          ResizePendingParams *resize_pending_params,
                          AOM_SCALING_MODE horiz_mode,
                          AOM_SCALING_MODE vert_mode) {
  int hr = 0, hs = 0, vr = 0, vs = 0;

  // Checks for invalid AOM_SCALING_MODE values.
  if (horiz_mode > AOME_ONETHREE || vert_mode > AOME_ONETHREE) return -1;

  Scale2Ratio(horiz_mode, &hr, &hs);
  Scale2Ratio(vert_mode, &vr, &vs);

  // always go to the next whole number
  resize_pending_params->width = (hs - 1 + oxcf->frm_dim_cfg.width * hr) / hs;
  resize_pending_params->height = (vs - 1 + oxcf->frm_dim_cfg.height * vr) / vs;

  if (horiz_mode != AOME_NORMAL || vert_mode != AOME_NORMAL) {
    oxcf->resize_cfg.resize_mode = RESIZE_FIXED;
    oxcf->algo_cfg.enable_tpl_model = 0;
  }
  return 0;
}

int av1_get_quantizer(AV1_COMP *cpi) {
  return cpi->common.quant_params.base_qindex;
}

int av1_convert_sect5obus_to_annexb(uint8_t *buffer, size_t buffer_size,
                                    size_t *frame_size) {
  assert(*frame_size <= buffer_size);
  size_t output_size = 0;
  size_t remaining_size = *frame_size;
  uint8_t *buff_ptr = buffer;

  // go through each OBUs
  while (remaining_size > 0) {
    uint8_t saved_obu_header[2];
    uint64_t obu_payload_size;
    size_t length_of_payload_size;
    size_t length_of_obu_size;
    const uint32_t obu_header_size = (buff_ptr[0] >> 2) & 0x1 ? 2 : 1;
    size_t obu_bytes_read = obu_header_size;  // bytes read for current obu

    // save the obu header (1 or 2 bytes)
    memcpy(saved_obu_header, buff_ptr, obu_header_size);
    // clear the obu_has_size_field
    saved_obu_header[0] &= ~0x2;

    // get the payload_size and length of payload_size
    if (aom_uleb_decode(buff_ptr + obu_header_size,
                        remaining_size - obu_header_size, &obu_payload_size,
                        &length_of_payload_size) != 0) {
      return AOM_CODEC_ERROR;
    }
    obu_bytes_read += length_of_payload_size;

    // calculate the length of size of the obu header plus payload
    const uint64_t obu_size = obu_header_size + obu_payload_size;
    length_of_obu_size = aom_uleb_size_in_bytes(obu_size);

    if (length_of_obu_size + obu_header_size >
        buffer_size - output_size - (remaining_size - obu_bytes_read)) {
      return AOM_CODEC_ERROR;
    }
    // move the rest of data to new location
    memmove(buff_ptr + length_of_obu_size + obu_header_size,
            buff_ptr + obu_bytes_read, remaining_size - obu_bytes_read);
    obu_bytes_read += (size_t)obu_payload_size;

    // write the new obu size
    size_t coded_obu_size;
    if (aom_uleb_encode(obu_size, length_of_obu_size, buff_ptr,
                        &coded_obu_size) != 0 ||
        coded_obu_size != length_of_obu_size) {
      return AOM_CODEC_ERROR;
    }

    // write the saved (modified) obu_header following obu size
    memcpy(buff_ptr + length_of_obu_size, saved_obu_header, obu_header_size);

    remaining_size -= obu_bytes_read;
    buff_ptr += length_of_obu_size + (size_t)obu_size;
    output_size += length_of_obu_size + (size_t)obu_size;
  }

  *frame_size = output_size;
  return AOM_CODEC_OK;
}

static void rtc_set_updates_ref_frame_config(
    ExtRefreshFrameFlagsInfo *const ext_refresh_frame_flags,
    RTC_REF *const rtc_ref) {
  ext_refresh_frame_flags->update_pending = 1;
  ext_refresh_frame_flags->last_frame = rtc_ref->refresh[rtc_ref->ref_idx[0]];
  ext_refresh_frame_flags->golden_frame = rtc_ref->refresh[rtc_ref->ref_idx[3]];
  ext_refresh_frame_flags->bwd_ref_frame =
      rtc_ref->refresh[rtc_ref->ref_idx[4]];
  ext_refresh_frame_flags->alt2_ref_frame =
      rtc_ref->refresh[rtc_ref->ref_idx[5]];
  ext_refresh_frame_flags->alt_ref_frame =
      rtc_ref->refresh[rtc_ref->ref_idx[6]];
  rtc_ref->non_reference_frame = 1;
  for (int i = 0; i < REF_FRAMES; i++) {
    if (rtc_ref->refresh[i] == 1) {
      rtc_ref->non_reference_frame = 0;
      break;
    }
  }
}

static int rtc_set_references_external_ref_frame_config(AV1_COMP *cpi) {
  // LAST_FRAME (0), LAST2_FRAME(1), LAST3_FRAME(2), GOLDEN_FRAME(3),
  // BWDREF_FRAME(4), ALTREF2_FRAME(5), ALTREF_FRAME(6).
  int ref = AOM_REFFRAME_ALL;
  for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
    if (!cpi->ppi->rtc_ref.reference[i]) ref ^= (1 << i);
  }
  return ref;
}

void av1_apply_encoding_flags(AV1_COMP *cpi, aom_enc_frame_flags_t flags) {
  // TODO(yunqingwang): For what references to use, external encoding flags
  // should be consistent with internal reference frame selection. Need to
  // ensure that there is not conflict between the two. In AV1 encoder, the
  // priority rank for 7 reference frames are: LAST, ALTREF, LAST2, LAST3,
  // GOLDEN, BWDREF, ALTREF2.

  ExternalFlags *const ext_flags = &cpi->ext_flags;
  ExtRefreshFrameFlagsInfo *const ext_refresh_frame_flags =
      &ext_flags->refresh_frame;
  ext_flags->ref_frame_flags = AOM_REFFRAME_ALL;
  if (flags &
      (AOM_EFLAG_NO_REF_LAST | AOM_EFLAG_NO_REF_LAST2 | AOM_EFLAG_NO_REF_LAST3 |
       AOM_EFLAG_NO_REF_GF | AOM_EFLAG_NO_REF_ARF | AOM_EFLAG_NO_REF_BWD |
       AOM_EFLAG_NO_REF_ARF2)) {
    int ref = AOM_REFFRAME_ALL;

    if (flags & AOM_EFLAG_NO_REF_LAST) ref ^= AOM_LAST_FLAG;
    if (flags & AOM_EFLAG_NO_REF_LAST2) ref ^= AOM_LAST2_FLAG;
    if (flags & AOM_EFLAG_NO_REF_LAST3) ref ^= AOM_LAST3_FLAG;

    if (flags & AOM_EFLAG_NO_REF_GF) ref ^= AOM_GOLD_FLAG;

    if (flags & AOM_EFLAG_NO_REF_ARF) {
      ref ^= AOM_ALT_FLAG;
      ref ^= AOM_BWD_FLAG;
      ref ^= AOM_ALT2_FLAG;
    } else {
      if (flags & AOM_EFLAG_NO_REF_BWD) ref ^= AOM_BWD_FLAG;
      if (flags & AOM_EFLAG_NO_REF_ARF2) ref ^= AOM_ALT2_FLAG;
    }

    av1_use_as_reference(&ext_flags->ref_frame_flags, ref);
  } else {
    if (cpi->ppi->rtc_ref.set_ref_frame_config) {
      int ref = rtc_set_references_external_ref_frame_config(cpi);
      av1_use_as_reference(&ext_flags->ref_frame_flags, ref);
    }
  }

  if (flags &
      (AOM_EFLAG_NO_UPD_LAST | AOM_EFLAG_NO_UPD_GF | AOM_EFLAG_NO_UPD_ARF)) {
    int upd = AOM_REFFRAME_ALL;

    // Refreshing LAST/LAST2/LAST3 is handled by 1 common flag.
    if (flags & AOM_EFLAG_NO_UPD_LAST) upd ^= AOM_LAST_FLAG;

    if (flags & AOM_EFLAG_NO_UPD_GF) upd ^= AOM_GOLD_FLAG;

    if (flags & AOM_EFLAG_NO_UPD_ARF) {
      upd ^= AOM_ALT_FLAG;
      upd ^= AOM_BWD_FLAG;
      upd ^= AOM_ALT2_FLAG;
    }

    ext_refresh_frame_flags->last_frame = (upd & AOM_LAST_FLAG) != 0;
    ext_refresh_frame_flags->golden_frame = (upd & AOM_GOLD_FLAG) != 0;
    ext_refresh_frame_flags->alt_ref_frame = (upd & AOM_ALT_FLAG) != 0;
    ext_refresh_frame_flags->bwd_ref_frame = (upd & AOM_BWD_FLAG) != 0;
    ext_refresh_frame_flags->alt2_ref_frame = (upd & AOM_ALT2_FLAG) != 0;
    ext_refresh_frame_flags->update_pending = 1;
  } else {
    if (cpi->ppi->rtc_ref.set_ref_frame_config)
      rtc_set_updates_ref_frame_config(ext_refresh_frame_flags,
                                       &cpi->ppi->rtc_ref);
    else
      ext_refresh_frame_flags->update_pending = 0;
  }

  ext_flags->use_ref_frame_mvs = cpi->oxcf.tool_cfg.enable_ref_frame_mvs &
                                 ((flags & AOM_EFLAG_NO_REF_FRAME_MVS) == 0);
  ext_flags->use_error_resilient = cpi->oxcf.tool_cfg.error_resilient_mode |
                                   ((flags & AOM_EFLAG_ERROR_RESILIENT) != 0);
  ext_flags->use_s_frame =
      cpi->oxcf.kf_cfg.enable_sframe | ((flags & AOM_EFLAG_SET_S_FRAME) != 0);
  ext_flags->use_primary_ref_none =
      (flags & AOM_EFLAG_SET_PRIMARY_REF_NONE) != 0;

  if (flags & AOM_EFLAG_NO_UPD_ENTROPY) {
    update_entropy(&ext_flags->refresh_frame_context,
                   &ext_flags->refresh_frame_context_pending, 0);
  }
}

aom_fixed_buf_t *av1_get_global_headers(AV1_PRIMARY *ppi) {
  if (!ppi) return NULL;

  uint8_t header_buf[512] = { 0 };
  const uint32_t sequence_header_size = av1_write_sequence_header_obu(
      &ppi->seq_params, &header_buf[0], sizeof(header_buf));
  assert(sequence_header_size <= sizeof(header_buf));
  if (sequence_header_size == 0) return NULL;

  const size_t obu_header_size = 1;
  const size_t size_field_size = aom_uleb_size_in_bytes(sequence_header_size);
  const size_t payload_offset = obu_header_size + size_field_size;

  if (payload_offset + sequence_header_size > sizeof(header_buf)) return NULL;
  memmove(&header_buf[payload_offset], &header_buf[0], sequence_header_size);

  if (av1_write_obu_header(&ppi->level_params, &ppi->cpi->frame_header_count,
                           OBU_SEQUENCE_HEADER,
                           ppi->seq_params.has_nonzero_operating_point_idc,
                           /*is_layer_specific_obu=*/false, 0,
                           &header_buf[0]) != obu_header_size) {
    return NULL;
  }

  size_t coded_size_field_size = 0;
  if (aom_uleb_encode(sequence_header_size, size_field_size,
                      &header_buf[obu_header_size],
                      &coded_size_field_size) != 0) {
    return NULL;
  }
  assert(coded_size_field_size == size_field_size);

  aom_fixed_buf_t *global_headers =
      (aom_fixed_buf_t *)malloc(sizeof(*global_headers));
  if (!global_headers) return NULL;

  const size_t global_header_buf_size =
      obu_header_size + size_field_size + sequence_header_size;

  global_headers->buf = malloc(global_header_buf_size);
  if (!global_headers->buf) {
    free(global_headers);
    return NULL;
  }

  memcpy(global_headers->buf, &header_buf[0], global_header_buf_size);
  global_headers->sz = global_header_buf_size;
  return global_headers;
}
