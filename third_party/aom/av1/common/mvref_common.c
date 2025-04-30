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

#include <stdlib.h>

#include "av1/common/mvref_common.h"
#include "av1/common/warped_motion.h"

// Although we assign 32 bit integers, all the values are strictly under 14
// bits.
static const int div_mult[32] = { 0,    16384, 8192, 5461, 4096, 3276, 2730,
                                  2340, 2048,  1820, 1638, 1489, 1365, 1260,
                                  1170, 1092,  1024, 963,  910,  862,  819,
                                  780,  744,   712,  682,  655,  630,  606,
                                  585,  564,   546,  528 };

// TODO(jingning): Consider the use of lookup table for (num / den)
// altogether.
static inline void get_mv_projection(MV *output, MV ref, int num, int den) {
  den = AOMMIN(den, MAX_FRAME_DISTANCE);
  num = num > 0 ? AOMMIN(num, MAX_FRAME_DISTANCE)
                : AOMMAX(num, -MAX_FRAME_DISTANCE);
  const int mv_row =
      ROUND_POWER_OF_TWO_SIGNED(ref.row * num * div_mult[den], 14);
  const int mv_col =
      ROUND_POWER_OF_TWO_SIGNED(ref.col * num * div_mult[den], 14);
  const int clamp_max = MV_UPP - 1;
  const int clamp_min = MV_LOW + 1;
  output->row = (int16_t)clamp(mv_row, clamp_min, clamp_max);
  output->col = (int16_t)clamp(mv_col, clamp_min, clamp_max);
}

void av1_copy_frame_mvs(const AV1_COMMON *const cm,
                        const MB_MODE_INFO *const mi, int mi_row, int mi_col,
                        int x_mis, int y_mis) {
  const int frame_mvs_stride = ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, 1);
  MV_REF *frame_mvs =
      cm->cur_frame->mvs + (mi_row >> 1) * frame_mvs_stride + (mi_col >> 1);
  x_mis = ROUND_POWER_OF_TWO(x_mis, 1);
  y_mis = ROUND_POWER_OF_TWO(y_mis, 1);
  int w, h;

  for (h = 0; h < y_mis; h++) {
    MV_REF *mv = frame_mvs;
    for (w = 0; w < x_mis; w++) {
      mv->ref_frame = NONE_FRAME;
      mv->mv.as_int = 0;

      for (int idx = 0; idx < 2; ++idx) {
        MV_REFERENCE_FRAME ref_frame = mi->ref_frame[idx];
        if (ref_frame > INTRA_FRAME) {
          int8_t ref_idx = cm->ref_frame_side[ref_frame];
          if (ref_idx) continue;
          if ((abs(mi->mv[idx].as_mv.row) > REFMVS_LIMIT) ||
              (abs(mi->mv[idx].as_mv.col) > REFMVS_LIMIT))
            continue;
          mv->ref_frame = ref_frame;
          mv->mv.as_int = mi->mv[idx].as_int;
        }
      }
      mv++;
    }
    frame_mvs += frame_mvs_stride;
  }
}

static inline void add_ref_mv_candidate(
    const MB_MODE_INFO *const candidate, const MV_REFERENCE_FRAME rf[2],
    uint8_t *refmv_count, uint8_t *ref_match_count, uint8_t *newmv_count,
    CANDIDATE_MV *ref_mv_stack, uint16_t *ref_mv_weight,
    int_mv *gm_mv_candidates, const WarpedMotionParams *gm_params,
    uint16_t weight) {
  if (!is_inter_block(candidate)) return;
  assert(weight % 2 == 0);
  int index, ref;

  if (rf[1] == NONE_FRAME) {
    // single reference frame
    for (ref = 0; ref < 2; ++ref) {
      if (candidate->ref_frame[ref] == rf[0]) {
        const int is_gm_block =
            is_global_mv_block(candidate, gm_params[rf[0]].wmtype);
        const int_mv this_refmv =
            is_gm_block ? gm_mv_candidates[0] : get_block_mv(candidate, ref);
        for (index = 0; index < *refmv_count; ++index) {
          if (ref_mv_stack[index].this_mv.as_int == this_refmv.as_int) {
            ref_mv_weight[index] += weight;
            break;
          }
        }

        // Add a new item to the list.
        if (index == *refmv_count && *refmv_count < MAX_REF_MV_STACK_SIZE) {
          ref_mv_stack[index].this_mv = this_refmv;
          ref_mv_weight[index] = weight;
          ++(*refmv_count);
        }
        if (have_newmv_in_inter_mode(candidate->mode)) ++*newmv_count;
        ++*ref_match_count;
      }
    }
  } else {
    // compound reference frame
    if (candidate->ref_frame[0] == rf[0] && candidate->ref_frame[1] == rf[1]) {
      int_mv this_refmv[2];

      for (ref = 0; ref < 2; ++ref) {
        if (is_global_mv_block(candidate, gm_params[rf[ref]].wmtype))
          this_refmv[ref] = gm_mv_candidates[ref];
        else
          this_refmv[ref] = get_block_mv(candidate, ref);
      }

      for (index = 0; index < *refmv_count; ++index) {
        if ((ref_mv_stack[index].this_mv.as_int == this_refmv[0].as_int) &&
            (ref_mv_stack[index].comp_mv.as_int == this_refmv[1].as_int)) {
          ref_mv_weight[index] += weight;
          break;
        }
      }

      // Add a new item to the list.
      if (index == *refmv_count && *refmv_count < MAX_REF_MV_STACK_SIZE) {
        ref_mv_stack[index].this_mv = this_refmv[0];
        ref_mv_stack[index].comp_mv = this_refmv[1];
        ref_mv_weight[index] = weight;
        ++(*refmv_count);
      }
      if (have_newmv_in_inter_mode(candidate->mode)) ++*newmv_count;
      ++*ref_match_count;
    }
  }
}

static inline void scan_row_mbmi(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                                 int mi_col, const MV_REFERENCE_FRAME rf[2],
                                 int row_offset, CANDIDATE_MV *ref_mv_stack,
                                 uint16_t *ref_mv_weight, uint8_t *refmv_count,
                                 uint8_t *ref_match_count, uint8_t *newmv_count,
                                 int_mv *gm_mv_candidates, int max_row_offset,
                                 int *processed_rows) {
  int end_mi = AOMMIN(xd->width, cm->mi_params.mi_cols - mi_col);
  end_mi = AOMMIN(end_mi, mi_size_wide[BLOCK_64X64]);
  const int width_8x8 = mi_size_wide[BLOCK_8X8];
  const int width_16x16 = mi_size_wide[BLOCK_16X16];
  int col_offset = 0;
  // TODO(jingning): Revisit this part after cb4x4 is stable.
  if (abs(row_offset) > 1) {
    col_offset = 1;
    if ((mi_col & 0x01) && xd->width < width_8x8) --col_offset;
  }
  const int use_step_16 = (xd->width >= 16);
  MB_MODE_INFO **const candidate_mi0 = xd->mi + row_offset * xd->mi_stride;

  for (int i = 0; i < end_mi;) {
    const MB_MODE_INFO *const candidate = candidate_mi0[col_offset + i];
    const int candidate_bsize = candidate->bsize;
    const int n4_w = mi_size_wide[candidate_bsize];
    int len = AOMMIN(xd->width, n4_w);
    if (use_step_16)
      len = AOMMAX(width_16x16, len);
    else if (abs(row_offset) > 1)
      len = AOMMAX(len, width_8x8);

    uint16_t weight = 2;
    if (xd->width >= width_8x8 && xd->width <= n4_w) {
      uint16_t inc = AOMMIN(-max_row_offset + row_offset + 1,
                            mi_size_high[candidate_bsize]);
      // Obtain range used in weight calculation.
      weight = AOMMAX(weight, inc);
      // Update processed rows.
      *processed_rows = inc - row_offset - 1;
    }

    add_ref_mv_candidate(candidate, rf, refmv_count, ref_match_count,
                         newmv_count, ref_mv_stack, ref_mv_weight,
                         gm_mv_candidates, cm->global_motion, len * weight);

    i += len;
  }
}

static inline void scan_col_mbmi(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                                 int mi_row, const MV_REFERENCE_FRAME rf[2],
                                 int col_offset, CANDIDATE_MV *ref_mv_stack,
                                 uint16_t *ref_mv_weight, uint8_t *refmv_count,
                                 uint8_t *ref_match_count, uint8_t *newmv_count,
                                 int_mv *gm_mv_candidates, int max_col_offset,
                                 int *processed_cols) {
  int end_mi = AOMMIN(xd->height, cm->mi_params.mi_rows - mi_row);
  end_mi = AOMMIN(end_mi, mi_size_high[BLOCK_64X64]);
  const int n8_h_8 = mi_size_high[BLOCK_8X8];
  const int n8_h_16 = mi_size_high[BLOCK_16X16];
  int i;
  int row_offset = 0;
  if (abs(col_offset) > 1) {
    row_offset = 1;
    if ((mi_row & 0x01) && xd->height < n8_h_8) --row_offset;
  }
  const int use_step_16 = (xd->height >= 16);

  for (i = 0; i < end_mi;) {
    const MB_MODE_INFO *const candidate =
        xd->mi[(row_offset + i) * xd->mi_stride + col_offset];
    const int candidate_bsize = candidate->bsize;
    const int n4_h = mi_size_high[candidate_bsize];
    int len = AOMMIN(xd->height, n4_h);
    if (use_step_16)
      len = AOMMAX(n8_h_16, len);
    else if (abs(col_offset) > 1)
      len = AOMMAX(len, n8_h_8);

    int weight = 2;
    if (xd->height >= n8_h_8 && xd->height <= n4_h) {
      int inc = AOMMIN(-max_col_offset + col_offset + 1,
                       mi_size_wide[candidate_bsize]);
      // Obtain range used in weight calculation.
      weight = AOMMAX(weight, inc);
      // Update processed cols.
      *processed_cols = inc - col_offset - 1;
    }

    add_ref_mv_candidate(candidate, rf, refmv_count, ref_match_count,
                         newmv_count, ref_mv_stack, ref_mv_weight,
                         gm_mv_candidates, cm->global_motion, len * weight);

    i += len;
  }
}

static inline void scan_blk_mbmi(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                                 const int mi_row, const int mi_col,
                                 const MV_REFERENCE_FRAME rf[2], int row_offset,
                                 int col_offset, CANDIDATE_MV *ref_mv_stack,
                                 uint16_t *ref_mv_weight,
                                 uint8_t *ref_match_count, uint8_t *newmv_count,
                                 int_mv *gm_mv_candidates,
                                 uint8_t *refmv_count) {
  const TileInfo *const tile = &xd->tile;
  POSITION mi_pos;

  mi_pos.row = row_offset;
  mi_pos.col = col_offset;

  if (is_inside(tile, mi_col, mi_row, &mi_pos)) {
    const MB_MODE_INFO *const candidate =
        xd->mi[mi_pos.row * xd->mi_stride + mi_pos.col];
    const int len = mi_size_wide[BLOCK_8X8];

    add_ref_mv_candidate(candidate, rf, refmv_count, ref_match_count,
                         newmv_count, ref_mv_stack, ref_mv_weight,
                         gm_mv_candidates, cm->global_motion, 2 * len);
  }  // Analyze a single 8x8 block motion information.
}

static int has_top_right(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                         int mi_row, int mi_col, int bs) {
  const int sb_mi_size = mi_size_wide[cm->seq_params->sb_size];
  const int mask_row = mi_row & (sb_mi_size - 1);
  const int mask_col = mi_col & (sb_mi_size - 1);

  if (bs > mi_size_wide[BLOCK_64X64]) return 0;

  // In a split partition all apart from the bottom right has a top right
  int has_tr = !((mask_row & bs) && (mask_col & bs));

  // bs > 0 and bs is a power of 2
  assert(bs > 0 && !(bs & (bs - 1)));

  // For each 4x4 group of blocks, when the bottom right is decoded the blocks
  // to the right have not been decoded therefore the bottom right does
  // not have a top right
  while (bs < sb_mi_size) {
    if (mask_col & bs) {
      if ((mask_col & (2 * bs)) && (mask_row & (2 * bs))) {
        has_tr = 0;
        break;
      }
    } else {
      break;
    }
    bs <<= 1;
  }

  // In a VERTICAL or VERTICAL_4 partition, all partition before the last one
  // always have a top right (as the block above will have been decoded).
  if (xd->width < xd->height) {
    if (!xd->is_last_vertical_rect) has_tr = 1;
  }

  // In a HORIZONTAL or HORIZONTAL_4 partition, partitions after the first one
  // never have a top right (as the block to the right won't have been decoded).
  if (xd->width > xd->height) {
    if (!xd->is_first_horizontal_rect) has_tr = 0;
  }

  // The bottom left square of a Vertical A (in the old format) does
  // not have a top right as it is decoded before the right hand
  // rectangle of the partition
  if (xd->mi[0]->partition == PARTITION_VERT_A) {
    if (xd->width == xd->height)
      if (mask_row & bs) has_tr = 0;
  }

  return has_tr;
}

static int check_sb_border(const int mi_row, const int mi_col,
                           const int row_offset, const int col_offset) {
  const int sb_mi_size = mi_size_wide[BLOCK_64X64];
  const int row = mi_row & (sb_mi_size - 1);
  const int col = mi_col & (sb_mi_size - 1);

  if (row + row_offset < 0 || row + row_offset >= sb_mi_size ||
      col + col_offset < 0 || col + col_offset >= sb_mi_size)
    return 0;

  return 1;
}

static int add_tpl_ref_mv(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                          int mi_row, int mi_col, MV_REFERENCE_FRAME ref_frame,
                          int blk_row, int blk_col, int_mv *gm_mv_candidates,
                          uint8_t *const refmv_count,
                          CANDIDATE_MV ref_mv_stack[MAX_REF_MV_STACK_SIZE],
                          uint16_t ref_mv_weight[MAX_REF_MV_STACK_SIZE],
                          int16_t *mode_context) {
  POSITION mi_pos;
  mi_pos.row = (mi_row & 0x01) ? blk_row : blk_row + 1;
  mi_pos.col = (mi_col & 0x01) ? blk_col : blk_col + 1;

  if (!is_inside(&xd->tile, mi_col, mi_row, &mi_pos)) return 0;

  const TPL_MV_REF *prev_frame_mvs =
      cm->tpl_mvs +
      ((mi_row + mi_pos.row) >> 1) * (cm->mi_params.mi_stride >> 1) +
      ((mi_col + mi_pos.col) >> 1);
  if (prev_frame_mvs->mfmv0.as_int == INVALID_MV) return 0;

  MV_REFERENCE_FRAME rf[2];
  av1_set_ref_frame(rf, ref_frame);

  const uint16_t weight_unit = 1;  // mi_size_wide[BLOCK_8X8];
  const int cur_frame_index = cm->cur_frame->order_hint;
  const RefCntBuffer *const buf_0 = get_ref_frame_buf(cm, rf[0]);
  const int frame0_index = buf_0->order_hint;
  const int cur_offset_0 = get_relative_dist(&cm->seq_params->order_hint_info,
                                             cur_frame_index, frame0_index);
  int idx;
  const int allow_high_precision_mv = cm->features.allow_high_precision_mv;
  const int force_integer_mv = cm->features.cur_frame_force_integer_mv;

  int_mv this_refmv;
  get_mv_projection(&this_refmv.as_mv, prev_frame_mvs->mfmv0.as_mv,
                    cur_offset_0, prev_frame_mvs->ref_frame_offset);
  lower_mv_precision(&this_refmv.as_mv, allow_high_precision_mv,
                     force_integer_mv);

  if (rf[1] == NONE_FRAME) {
    if (blk_row == 0 && blk_col == 0) {
      if (abs(this_refmv.as_mv.row - gm_mv_candidates[0].as_mv.row) >= 16 ||
          abs(this_refmv.as_mv.col - gm_mv_candidates[0].as_mv.col) >= 16)
        mode_context[ref_frame] |= (1 << GLOBALMV_OFFSET);
    }

    for (idx = 0; idx < *refmv_count; ++idx)
      if (this_refmv.as_int == ref_mv_stack[idx].this_mv.as_int) break;

    if (idx < *refmv_count) ref_mv_weight[idx] += 2 * weight_unit;

    if (idx == *refmv_count && *refmv_count < MAX_REF_MV_STACK_SIZE) {
      ref_mv_stack[idx].this_mv.as_int = this_refmv.as_int;
      ref_mv_weight[idx] = 2 * weight_unit;
      ++(*refmv_count);
    }
  } else {
    // Process compound inter mode
    const RefCntBuffer *const buf_1 = get_ref_frame_buf(cm, rf[1]);
    const int frame1_index = buf_1->order_hint;
    const int cur_offset_1 = get_relative_dist(&cm->seq_params->order_hint_info,
                                               cur_frame_index, frame1_index);
    int_mv comp_refmv;
    get_mv_projection(&comp_refmv.as_mv, prev_frame_mvs->mfmv0.as_mv,
                      cur_offset_1, prev_frame_mvs->ref_frame_offset);
    lower_mv_precision(&comp_refmv.as_mv, allow_high_precision_mv,
                       force_integer_mv);

    if (blk_row == 0 && blk_col == 0) {
      if (abs(this_refmv.as_mv.row - gm_mv_candidates[0].as_mv.row) >= 16 ||
          abs(this_refmv.as_mv.col - gm_mv_candidates[0].as_mv.col) >= 16 ||
          abs(comp_refmv.as_mv.row - gm_mv_candidates[1].as_mv.row) >= 16 ||
          abs(comp_refmv.as_mv.col - gm_mv_candidates[1].as_mv.col) >= 16)
        mode_context[ref_frame] |= (1 << GLOBALMV_OFFSET);
    }

    for (idx = 0; idx < *refmv_count; ++idx) {
      if (this_refmv.as_int == ref_mv_stack[idx].this_mv.as_int &&
          comp_refmv.as_int == ref_mv_stack[idx].comp_mv.as_int)
        break;
    }

    if (idx < *refmv_count) ref_mv_weight[idx] += 2 * weight_unit;

    if (idx == *refmv_count && *refmv_count < MAX_REF_MV_STACK_SIZE) {
      ref_mv_stack[idx].this_mv.as_int = this_refmv.as_int;
      ref_mv_stack[idx].comp_mv.as_int = comp_refmv.as_int;
      ref_mv_weight[idx] = 2 * weight_unit;
      ++(*refmv_count);
    }
  }

  return 1;
}

static inline void process_compound_ref_mv_candidate(
    const MB_MODE_INFO *const candidate, const AV1_COMMON *const cm,
    const MV_REFERENCE_FRAME *const rf, int_mv ref_id[2][2],
    int ref_id_count[2], int_mv ref_diff[2][2], int ref_diff_count[2]) {
  for (int rf_idx = 0; rf_idx < 2; ++rf_idx) {
    MV_REFERENCE_FRAME can_rf = candidate->ref_frame[rf_idx];

    for (int cmp_idx = 0; cmp_idx < 2; ++cmp_idx) {
      if (can_rf == rf[cmp_idx] && ref_id_count[cmp_idx] < 2) {
        ref_id[cmp_idx][ref_id_count[cmp_idx]] = candidate->mv[rf_idx];
        ++ref_id_count[cmp_idx];
      } else if (can_rf > INTRA_FRAME && ref_diff_count[cmp_idx] < 2) {
        int_mv this_mv = candidate->mv[rf_idx];
        if (cm->ref_frame_sign_bias[can_rf] !=
            cm->ref_frame_sign_bias[rf[cmp_idx]]) {
          this_mv.as_mv.row = -this_mv.as_mv.row;
          this_mv.as_mv.col = -this_mv.as_mv.col;
        }
        ref_diff[cmp_idx][ref_diff_count[cmp_idx]] = this_mv;
        ++ref_diff_count[cmp_idx];
      }
    }
  }
}

static inline void process_single_ref_mv_candidate(
    const MB_MODE_INFO *const candidate, const AV1_COMMON *const cm,
    MV_REFERENCE_FRAME ref_frame, uint8_t *const refmv_count,
    CANDIDATE_MV ref_mv_stack[MAX_REF_MV_STACK_SIZE],
    uint16_t ref_mv_weight[MAX_REF_MV_STACK_SIZE]) {
  for (int rf_idx = 0; rf_idx < 2; ++rf_idx) {
    if (candidate->ref_frame[rf_idx] > INTRA_FRAME) {
      int_mv this_mv = candidate->mv[rf_idx];
      if (cm->ref_frame_sign_bias[candidate->ref_frame[rf_idx]] !=
          cm->ref_frame_sign_bias[ref_frame]) {
        this_mv.as_mv.row = -this_mv.as_mv.row;
        this_mv.as_mv.col = -this_mv.as_mv.col;
      }
      int stack_idx;
      for (stack_idx = 0; stack_idx < *refmv_count; ++stack_idx) {
        const int_mv stack_mv = ref_mv_stack[stack_idx].this_mv;
        if (this_mv.as_int == stack_mv.as_int) break;
      }

      if (stack_idx == *refmv_count) {
        ref_mv_stack[stack_idx].this_mv = this_mv;

        // TODO(jingning): Set an arbitrary small number here. The weight
        // doesn't matter as long as it is properly initialized.
        ref_mv_weight[stack_idx] = 2;
        ++(*refmv_count);
      }
    }
  }
}

static inline void setup_ref_mv_list(
    const AV1_COMMON *cm, const MACROBLOCKD *xd, MV_REFERENCE_FRAME ref_frame,
    uint8_t *const refmv_count,
    CANDIDATE_MV ref_mv_stack[MAX_REF_MV_STACK_SIZE],
    uint16_t ref_mv_weight[MAX_REF_MV_STACK_SIZE],
    int_mv mv_ref_list[MAX_MV_REF_CANDIDATES], int_mv *gm_mv_candidates,
    int mi_row, int mi_col, int16_t *mode_context) {
  const int bs = AOMMAX(xd->width, xd->height);
  const int has_tr = has_top_right(cm, xd, mi_row, mi_col, bs);
  MV_REFERENCE_FRAME rf[2];

  const TileInfo *const tile = &xd->tile;
  int max_row_offset = 0, max_col_offset = 0;
  const int row_adj = (xd->height < mi_size_high[BLOCK_8X8]) && (mi_row & 0x01);
  const int col_adj = (xd->width < mi_size_wide[BLOCK_8X8]) && (mi_col & 0x01);
  int processed_rows = 0;
  int processed_cols = 0;

  av1_set_ref_frame(rf, ref_frame);
  mode_context[ref_frame] = 0;
  *refmv_count = 0;

  // Find valid maximum row/col offset.
  if (xd->up_available) {
    max_row_offset = -(MVREF_ROW_COLS << 1) + row_adj;

    if (xd->height < mi_size_high[BLOCK_8X8])
      max_row_offset = -(2 << 1) + row_adj;

    max_row_offset = find_valid_row_offset(tile, mi_row, max_row_offset);
  }

  if (xd->left_available) {
    max_col_offset = -(MVREF_ROW_COLS << 1) + col_adj;

    if (xd->width < mi_size_wide[BLOCK_8X8])
      max_col_offset = -(2 << 1) + col_adj;

    max_col_offset = find_valid_col_offset(tile, mi_col, max_col_offset);
  }

  uint8_t col_match_count = 0;
  uint8_t row_match_count = 0;
  uint8_t newmv_count = 0;

  // Scan the first above row mode info. row_offset = -1;
  if (abs(max_row_offset) >= 1)
    scan_row_mbmi(cm, xd, mi_col, rf, -1, ref_mv_stack, ref_mv_weight,
                  refmv_count, &row_match_count, &newmv_count, gm_mv_candidates,
                  max_row_offset, &processed_rows);
  // Scan the first left column mode info. col_offset = -1;
  if (abs(max_col_offset) >= 1)
    scan_col_mbmi(cm, xd, mi_row, rf, -1, ref_mv_stack, ref_mv_weight,
                  refmv_count, &col_match_count, &newmv_count, gm_mv_candidates,
                  max_col_offset, &processed_cols);
  // Check top-right boundary
  if (has_tr)
    scan_blk_mbmi(cm, xd, mi_row, mi_col, rf, -1, xd->width, ref_mv_stack,
                  ref_mv_weight, &row_match_count, &newmv_count,
                  gm_mv_candidates, refmv_count);

  const uint8_t nearest_match = (row_match_count > 0) + (col_match_count > 0);
  const uint8_t nearest_refmv_count = *refmv_count;

  // TODO(yunqing): for comp_search, do it for all 3 cases.
  for (int idx = 0; idx < nearest_refmv_count; ++idx)
    ref_mv_weight[idx] += REF_CAT_LEVEL;

  if (cm->features.allow_ref_frame_mvs) {
    int is_available = 0;
    const int voffset = AOMMAX(mi_size_high[BLOCK_8X8], xd->height);
    const int hoffset = AOMMAX(mi_size_wide[BLOCK_8X8], xd->width);
    const int blk_row_end = AOMMIN(xd->height, mi_size_high[BLOCK_64X64]);
    const int blk_col_end = AOMMIN(xd->width, mi_size_wide[BLOCK_64X64]);

    const int tpl_sample_pos[3][2] = {
      { voffset, -2 },
      { voffset, hoffset },
      { voffset - 2, hoffset },
    };
    const int allow_extension = (xd->height >= mi_size_high[BLOCK_8X8]) &&
                                (xd->height < mi_size_high[BLOCK_64X64]) &&
                                (xd->width >= mi_size_wide[BLOCK_8X8]) &&
                                (xd->width < mi_size_wide[BLOCK_64X64]);

    const int step_h = (xd->height >= mi_size_high[BLOCK_64X64])
                           ? mi_size_high[BLOCK_16X16]
                           : mi_size_high[BLOCK_8X8];
    const int step_w = (xd->width >= mi_size_wide[BLOCK_64X64])
                           ? mi_size_wide[BLOCK_16X16]
                           : mi_size_wide[BLOCK_8X8];

    for (int blk_row = 0; blk_row < blk_row_end; blk_row += step_h) {
      for (int blk_col = 0; blk_col < blk_col_end; blk_col += step_w) {
        int ret = add_tpl_ref_mv(cm, xd, mi_row, mi_col, ref_frame, blk_row,
                                 blk_col, gm_mv_candidates, refmv_count,
                                 ref_mv_stack, ref_mv_weight, mode_context);
        if (blk_row == 0 && blk_col == 0) is_available = ret;
      }
    }

    if (is_available == 0) mode_context[ref_frame] |= (1 << GLOBALMV_OFFSET);

    for (int i = 0; i < 3 && allow_extension; ++i) {
      const int blk_row = tpl_sample_pos[i][0];
      const int blk_col = tpl_sample_pos[i][1];

      if (!check_sb_border(mi_row, mi_col, blk_row, blk_col)) continue;
      add_tpl_ref_mv(cm, xd, mi_row, mi_col, ref_frame, blk_row, blk_col,
                     gm_mv_candidates, refmv_count, ref_mv_stack, ref_mv_weight,
                     mode_context);
    }
  }

  uint8_t dummy_newmv_count = 0;

  // Scan the second outer area.
  scan_blk_mbmi(cm, xd, mi_row, mi_col, rf, -1, -1, ref_mv_stack, ref_mv_weight,
                &row_match_count, &dummy_newmv_count, gm_mv_candidates,
                refmv_count);

  for (int idx = 2; idx <= MVREF_ROW_COLS; ++idx) {
    const int row_offset = -(idx << 1) + 1 + row_adj;
    const int col_offset = -(idx << 1) + 1 + col_adj;

    if (abs(row_offset) <= abs(max_row_offset) &&
        abs(row_offset) > processed_rows)
      scan_row_mbmi(cm, xd, mi_col, rf, row_offset, ref_mv_stack, ref_mv_weight,
                    refmv_count, &row_match_count, &dummy_newmv_count,
                    gm_mv_candidates, max_row_offset, &processed_rows);

    if (abs(col_offset) <= abs(max_col_offset) &&
        abs(col_offset) > processed_cols)
      scan_col_mbmi(cm, xd, mi_row, rf, col_offset, ref_mv_stack, ref_mv_weight,
                    refmv_count, &col_match_count, &dummy_newmv_count,
                    gm_mv_candidates, max_col_offset, &processed_cols);
  }

  const uint8_t ref_match_count = (row_match_count > 0) + (col_match_count > 0);

  switch (nearest_match) {
    case 0:
      if (ref_match_count >= 1) mode_context[ref_frame] |= 1;
      if (ref_match_count == 1)
        mode_context[ref_frame] |= (1 << REFMV_OFFSET);
      else if (ref_match_count >= 2)
        mode_context[ref_frame] |= (2 << REFMV_OFFSET);
      break;
    case 1:
      mode_context[ref_frame] |= (newmv_count > 0) ? 2 : 3;
      if (ref_match_count == 1)
        mode_context[ref_frame] |= (3 << REFMV_OFFSET);
      else if (ref_match_count >= 2)
        mode_context[ref_frame] |= (4 << REFMV_OFFSET);
      break;
    case 2:
    default:
      if (newmv_count >= 1)
        mode_context[ref_frame] |= 4;
      else
        mode_context[ref_frame] |= 5;

      mode_context[ref_frame] |= (5 << REFMV_OFFSET);
      break;
  }

  // Rank the likelihood and assign nearest and near mvs.
  int len = nearest_refmv_count;
  while (len > 0) {
    int nr_len = 0;
    for (int idx = 1; idx < len; ++idx) {
      if (ref_mv_weight[idx - 1] < ref_mv_weight[idx]) {
        const CANDIDATE_MV tmp_mv = ref_mv_stack[idx - 1];
        const uint16_t tmp_ref_mv_weight = ref_mv_weight[idx - 1];
        ref_mv_stack[idx - 1] = ref_mv_stack[idx];
        ref_mv_stack[idx] = tmp_mv;
        ref_mv_weight[idx - 1] = ref_mv_weight[idx];
        ref_mv_weight[idx] = tmp_ref_mv_weight;
        nr_len = idx;
      }
    }
    len = nr_len;
  }

  len = *refmv_count;
  while (len > nearest_refmv_count) {
    int nr_len = nearest_refmv_count;
    for (int idx = nearest_refmv_count + 1; idx < len; ++idx) {
      if (ref_mv_weight[idx - 1] < ref_mv_weight[idx]) {
        const CANDIDATE_MV tmp_mv = ref_mv_stack[idx - 1];
        const uint16_t tmp_ref_mv_weight = ref_mv_weight[idx - 1];
        ref_mv_stack[idx - 1] = ref_mv_stack[idx];
        ref_mv_stack[idx] = tmp_mv;
        ref_mv_weight[idx - 1] = ref_mv_weight[idx];
        ref_mv_weight[idx] = tmp_ref_mv_weight;
        nr_len = idx;
      }
    }
    len = nr_len;
  }

  int mi_width = AOMMIN(mi_size_wide[BLOCK_64X64], xd->width);
  mi_width = AOMMIN(mi_width, cm->mi_params.mi_cols - mi_col);
  int mi_height = AOMMIN(mi_size_high[BLOCK_64X64], xd->height);
  mi_height = AOMMIN(mi_height, cm->mi_params.mi_rows - mi_row);
  const int mi_size = AOMMIN(mi_width, mi_height);
  if (rf[1] > NONE_FRAME) {
    // TODO(jingning, yunqing): Refactor and consolidate the compound and
    // single reference frame modes. Reduce unnecessary redundancy.
    if (*refmv_count < MAX_MV_REF_CANDIDATES) {
      int_mv ref_id[2][2], ref_diff[2][2];
      int ref_id_count[2] = { 0 }, ref_diff_count[2] = { 0 };

      for (int idx = 0; abs(max_row_offset) >= 1 && idx < mi_size;) {
        const MB_MODE_INFO *const candidate = xd->mi[-xd->mi_stride + idx];
        process_compound_ref_mv_candidate(
            candidate, cm, rf, ref_id, ref_id_count, ref_diff, ref_diff_count);
        idx += mi_size_wide[candidate->bsize];
      }

      for (int idx = 0; abs(max_col_offset) >= 1 && idx < mi_size;) {
        const MB_MODE_INFO *const candidate = xd->mi[idx * xd->mi_stride - 1];
        process_compound_ref_mv_candidate(
            candidate, cm, rf, ref_id, ref_id_count, ref_diff, ref_diff_count);
        idx += mi_size_high[candidate->bsize];
      }

      // Build up the compound mv predictor
      int_mv comp_list[MAX_MV_REF_CANDIDATES][2];

      for (int idx = 0; idx < 2; ++idx) {
        int comp_idx = 0;
        for (int list_idx = 0;
             list_idx < ref_id_count[idx] && comp_idx < MAX_MV_REF_CANDIDATES;
             ++list_idx, ++comp_idx)
          comp_list[comp_idx][idx] = ref_id[idx][list_idx];
        for (int list_idx = 0;
             list_idx < ref_diff_count[idx] && comp_idx < MAX_MV_REF_CANDIDATES;
             ++list_idx, ++comp_idx)
          comp_list[comp_idx][idx] = ref_diff[idx][list_idx];
        for (; comp_idx < MAX_MV_REF_CANDIDATES; ++comp_idx)
          comp_list[comp_idx][idx] = gm_mv_candidates[idx];
      }

      if (*refmv_count) {
        assert(*refmv_count == 1);
        if (comp_list[0][0].as_int == ref_mv_stack[0].this_mv.as_int &&
            comp_list[0][1].as_int == ref_mv_stack[0].comp_mv.as_int) {
          ref_mv_stack[*refmv_count].this_mv = comp_list[1][0];
          ref_mv_stack[*refmv_count].comp_mv = comp_list[1][1];
        } else {
          ref_mv_stack[*refmv_count].this_mv = comp_list[0][0];
          ref_mv_stack[*refmv_count].comp_mv = comp_list[0][1];
        }
        ref_mv_weight[*refmv_count] = 2;
        ++*refmv_count;
      } else {
        for (int idx = 0; idx < MAX_MV_REF_CANDIDATES; ++idx) {
          ref_mv_stack[*refmv_count].this_mv = comp_list[idx][0];
          ref_mv_stack[*refmv_count].comp_mv = comp_list[idx][1];
          ref_mv_weight[*refmv_count] = 2;
          ++*refmv_count;
        }
      }
    }

    assert(*refmv_count >= 2);

    for (int idx = 0; idx < *refmv_count; ++idx) {
      clamp_mv_ref(&ref_mv_stack[idx].this_mv.as_mv, xd->width << MI_SIZE_LOG2,
                   xd->height << MI_SIZE_LOG2, xd);
      clamp_mv_ref(&ref_mv_stack[idx].comp_mv.as_mv, xd->width << MI_SIZE_LOG2,
                   xd->height << MI_SIZE_LOG2, xd);
    }
  } else {
    // Handle single reference frame extension
    for (int idx = 0; abs(max_row_offset) >= 1 && idx < mi_size &&
                      *refmv_count < MAX_MV_REF_CANDIDATES;) {
      const MB_MODE_INFO *const candidate = xd->mi[-xd->mi_stride + idx];
      process_single_ref_mv_candidate(candidate, cm, ref_frame, refmv_count,
                                      ref_mv_stack, ref_mv_weight);
      idx += mi_size_wide[candidate->bsize];
    }

    for (int idx = 0; abs(max_col_offset) >= 1 && idx < mi_size &&
                      *refmv_count < MAX_MV_REF_CANDIDATES;) {
      const MB_MODE_INFO *const candidate = xd->mi[idx * xd->mi_stride - 1];
      process_single_ref_mv_candidate(candidate, cm, ref_frame, refmv_count,
                                      ref_mv_stack, ref_mv_weight);
      idx += mi_size_high[candidate->bsize];
    }

    for (int idx = 0; idx < *refmv_count; ++idx) {
      clamp_mv_ref(&ref_mv_stack[idx].this_mv.as_mv, xd->width << MI_SIZE_LOG2,
                   xd->height << MI_SIZE_LOG2, xd);
    }

    if (mv_ref_list != NULL) {
      for (int idx = *refmv_count; idx < MAX_MV_REF_CANDIDATES; ++idx)
        mv_ref_list[idx].as_int = gm_mv_candidates[0].as_int;

      for (int idx = 0; idx < AOMMIN(MAX_MV_REF_CANDIDATES, *refmv_count);
           ++idx) {
        mv_ref_list[idx].as_int = ref_mv_stack[idx].this_mv.as_int;
      }
    }
  }
}

void av1_find_mv_refs(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                      MB_MODE_INFO *mi, MV_REFERENCE_FRAME ref_frame,
                      uint8_t ref_mv_count[MODE_CTX_REF_FRAMES],
                      CANDIDATE_MV ref_mv_stack[][MAX_REF_MV_STACK_SIZE],
                      uint16_t ref_mv_weight[][MAX_REF_MV_STACK_SIZE],
                      int_mv mv_ref_list[][MAX_MV_REF_CANDIDATES],
                      int_mv *global_mvs, int16_t *mode_context) {
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  int_mv gm_mv[2];

  if (ref_frame == INTRA_FRAME) {
    gm_mv[0].as_int = gm_mv[1].as_int = 0;
    if (global_mvs != NULL) {
      global_mvs[ref_frame].as_int = INVALID_MV;
    }
  } else {
    const BLOCK_SIZE bsize = mi->bsize;
    const int allow_high_precision_mv = cm->features.allow_high_precision_mv;
    const int force_integer_mv = cm->features.cur_frame_force_integer_mv;
    if (ref_frame < REF_FRAMES) {
      gm_mv[0] = gm_get_motion_vector(&cm->global_motion[ref_frame],
                                      allow_high_precision_mv, bsize, mi_col,
                                      mi_row, force_integer_mv);
      gm_mv[1].as_int = 0;
      if (global_mvs != NULL) global_mvs[ref_frame] = gm_mv[0];
    } else {
      MV_REFERENCE_FRAME rf[2];
      av1_set_ref_frame(rf, ref_frame);
      gm_mv[0] = gm_get_motion_vector(&cm->global_motion[rf[0]],
                                      allow_high_precision_mv, bsize, mi_col,
                                      mi_row, force_integer_mv);
      gm_mv[1] = gm_get_motion_vector(&cm->global_motion[rf[1]],
                                      allow_high_precision_mv, bsize, mi_col,
                                      mi_row, force_integer_mv);
    }
  }

  setup_ref_mv_list(cm, xd, ref_frame, &ref_mv_count[ref_frame],
                    ref_mv_stack[ref_frame], ref_mv_weight[ref_frame],
                    mv_ref_list ? mv_ref_list[ref_frame] : NULL, gm_mv, mi_row,
                    mi_col, mode_context);
}

void av1_find_best_ref_mvs(int allow_hp, int_mv *mvlist, int_mv *nearest_mv,
                           int_mv *near_mv, int is_integer) {
  int i;
  // Make sure all the candidates are properly clamped etc
  for (i = 0; i < MAX_MV_REF_CANDIDATES; ++i) {
    lower_mv_precision(&mvlist[i].as_mv, allow_hp, is_integer);
  }
  *nearest_mv = mvlist[0];
  *near_mv = mvlist[1];
}

void av1_setup_frame_buf_refs(AV1_COMMON *cm) {
  cm->cur_frame->order_hint = cm->current_frame.order_hint;
  cm->cur_frame->display_order_hint = cm->current_frame.display_order_hint;
  cm->cur_frame->pyramid_level = cm->current_frame.pyramid_level;
  cm->cur_frame->filter_level[0] = -1;
  cm->cur_frame->filter_level[1] = -1;
  MV_REFERENCE_FRAME ref_frame;
  for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ++ref_frame) {
    const RefCntBuffer *const buf = get_ref_frame_buf(cm, ref_frame);
    if (buf != NULL) {
      cm->cur_frame->ref_order_hints[ref_frame - LAST_FRAME] = buf->order_hint;
      cm->cur_frame->ref_display_order_hint[ref_frame - LAST_FRAME] =
          buf->display_order_hint;
    }
  }
}

void av1_setup_frame_sign_bias(AV1_COMMON *cm) {
  MV_REFERENCE_FRAME ref_frame;
  for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ++ref_frame) {
    const RefCntBuffer *const buf = get_ref_frame_buf(cm, ref_frame);
    if (cm->seq_params->order_hint_info.enable_order_hint && buf != NULL) {
      const int ref_order_hint = buf->order_hint;
      cm->ref_frame_sign_bias[ref_frame] =
          (get_relative_dist(&cm->seq_params->order_hint_info, ref_order_hint,
                             (int)cm->current_frame.order_hint) <= 0)
              ? 0
              : 1;
    } else {
      cm->ref_frame_sign_bias[ref_frame] = 0;
    }
  }
}

#define MAX_OFFSET_WIDTH 64
#define MAX_OFFSET_HEIGHT 0

static int get_block_position(AV1_COMMON *cm, int *mi_r, int *mi_c, int blk_row,
                              int blk_col, MV mv, int sign_bias) {
  const int base_blk_row = (blk_row >> 3) << 3;
  const int base_blk_col = (blk_col >> 3) << 3;

  const int row_offset = (mv.row >= 0) ? (mv.row >> (4 + MI_SIZE_LOG2))
                                       : -((-mv.row) >> (4 + MI_SIZE_LOG2));

  const int col_offset = (mv.col >= 0) ? (mv.col >> (4 + MI_SIZE_LOG2))
                                       : -((-mv.col) >> (4 + MI_SIZE_LOG2));

  const int row =
      (sign_bias == 1) ? blk_row - row_offset : blk_row + row_offset;
  const int col =
      (sign_bias == 1) ? blk_col - col_offset : blk_col + col_offset;

  if (row < 0 || row >= (cm->mi_params.mi_rows >> 1) || col < 0 ||
      col >= (cm->mi_params.mi_cols >> 1))
    return 0;

  if (row < base_blk_row - (MAX_OFFSET_HEIGHT >> 3) ||
      row >= base_blk_row + 8 + (MAX_OFFSET_HEIGHT >> 3) ||
      col < base_blk_col - (MAX_OFFSET_WIDTH >> 3) ||
      col >= base_blk_col + 8 + (MAX_OFFSET_WIDTH >> 3))
    return 0;

  *mi_r = row;
  *mi_c = col;

  return 1;
}

// Note: motion_filed_projection finds motion vectors of current frame's
// reference frame, and projects them to current frame. To make it clear,
// let's call current frame's reference frame as start frame.
// Call Start frame's reference frames as reference frames.
// Call ref_offset as frame distances between start frame and its reference
// frames.
static int motion_field_projection(AV1_COMMON *cm,
                                   MV_REFERENCE_FRAME start_frame, int dir) {
  TPL_MV_REF *tpl_mvs_base = cm->tpl_mvs;
  int ref_offset[REF_FRAMES] = { 0 };

  const RefCntBuffer *const start_frame_buf =
      get_ref_frame_buf(cm, start_frame);
  if (start_frame_buf == NULL) return 0;

  if (start_frame_buf->frame_type == KEY_FRAME ||
      start_frame_buf->frame_type == INTRA_ONLY_FRAME)
    return 0;

  if (start_frame_buf->mi_rows != cm->mi_params.mi_rows ||
      start_frame_buf->mi_cols != cm->mi_params.mi_cols)
    return 0;

  const int start_frame_order_hint = start_frame_buf->order_hint;
  const unsigned int *const ref_order_hints =
      &start_frame_buf->ref_order_hints[0];
  const int cur_order_hint = cm->cur_frame->order_hint;
  int start_to_current_frame_offset = get_relative_dist(
      &cm->seq_params->order_hint_info, start_frame_order_hint, cur_order_hint);

  for (MV_REFERENCE_FRAME rf = LAST_FRAME; rf <= INTER_REFS_PER_FRAME; ++rf) {
    ref_offset[rf] = get_relative_dist(&cm->seq_params->order_hint_info,
                                       start_frame_order_hint,
                                       ref_order_hints[rf - LAST_FRAME]);
  }

  if (dir == 2) start_to_current_frame_offset = -start_to_current_frame_offset;

  MV_REF *mv_ref_base = start_frame_buf->mvs;
  const int mvs_rows = (cm->mi_params.mi_rows + 1) >> 1;
  const int mvs_cols = (cm->mi_params.mi_cols + 1) >> 1;

  for (int blk_row = 0; blk_row < mvs_rows; ++blk_row) {
    for (int blk_col = 0; blk_col < mvs_cols; ++blk_col) {
      MV_REF *mv_ref = &mv_ref_base[blk_row * mvs_cols + blk_col];
      MV fwd_mv = mv_ref->mv.as_mv;

      if (mv_ref->ref_frame > INTRA_FRAME) {
        int_mv this_mv;
        int mi_r, mi_c;
        const int ref_frame_offset = ref_offset[mv_ref->ref_frame];

        int pos_valid =
            abs(ref_frame_offset) <= MAX_FRAME_DISTANCE &&
            ref_frame_offset > 0 &&
            abs(start_to_current_frame_offset) <= MAX_FRAME_DISTANCE;

        if (pos_valid) {
          get_mv_projection(&this_mv.as_mv, fwd_mv,
                            start_to_current_frame_offset, ref_frame_offset);
          pos_valid = get_block_position(cm, &mi_r, &mi_c, blk_row, blk_col,
                                         this_mv.as_mv, dir >> 1);
        }

        if (pos_valid) {
          const int mi_offset = mi_r * (cm->mi_params.mi_stride >> 1) + mi_c;

          tpl_mvs_base[mi_offset].mfmv0.as_mv.row = fwd_mv.row;
          tpl_mvs_base[mi_offset].mfmv0.as_mv.col = fwd_mv.col;
          tpl_mvs_base[mi_offset].ref_frame_offset = ref_frame_offset;
        }
      }
    }
  }

  return 1;
}

// cm->ref_frame_side is calculated here, and will be used in
// av1_copy_frame_mvs() to affect how mvs are copied.
void av1_calculate_ref_frame_side(AV1_COMMON *cm) {
  const OrderHintInfo *const order_hint_info = &cm->seq_params->order_hint_info;

  memset(cm->ref_frame_side, 0, sizeof(cm->ref_frame_side));
  if (!order_hint_info->enable_order_hint) return;

  const int cur_order_hint = cm->cur_frame->order_hint;

  for (int ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ref_frame++) {
    const RefCntBuffer *const buf = get_ref_frame_buf(cm, ref_frame);
    int order_hint = 0;

    if (buf != NULL) order_hint = buf->order_hint;

    if (get_relative_dist(order_hint_info, order_hint, cur_order_hint) > 0)
      cm->ref_frame_side[ref_frame] = 1;
    else if (order_hint == cur_order_hint)
      cm->ref_frame_side[ref_frame] = -1;
  }
}

void av1_setup_motion_field(AV1_COMMON *cm) {
  const OrderHintInfo *const order_hint_info = &cm->seq_params->order_hint_info;

  if (!order_hint_info->enable_order_hint) return;

  TPL_MV_REF *tpl_mvs_base = cm->tpl_mvs;
  int size = ((cm->mi_params.mi_rows + MAX_MIB_SIZE) >> 1) *
             (cm->mi_params.mi_stride >> 1);
  for (int idx = 0; idx < size; ++idx) {
    tpl_mvs_base[idx].mfmv0.as_int = INVALID_MV;
    tpl_mvs_base[idx].ref_frame_offset = 0;
  }

  const int cur_order_hint = cm->cur_frame->order_hint;
  const RefCntBuffer *ref_buf[INTER_REFS_PER_FRAME];
  int ref_order_hint[INTER_REFS_PER_FRAME];

  for (int ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ref_frame++) {
    const int ref_idx = ref_frame - LAST_FRAME;
    const RefCntBuffer *const buf = get_ref_frame_buf(cm, ref_frame);
    int order_hint = 0;

    if (buf != NULL) order_hint = buf->order_hint;

    ref_buf[ref_idx] = buf;
    ref_order_hint[ref_idx] = order_hint;
  }

  int ref_stamp = MFMV_STACK_SIZE - 1;

  if (ref_buf[LAST_FRAME - LAST_FRAME] != NULL) {
    const int alt_of_lst_order_hint =
        ref_buf[LAST_FRAME - LAST_FRAME]
            ->ref_order_hints[ALTREF_FRAME - LAST_FRAME];

    const int is_lst_overlay =
        (alt_of_lst_order_hint == ref_order_hint[GOLDEN_FRAME - LAST_FRAME]);
    if (!is_lst_overlay) motion_field_projection(cm, LAST_FRAME, 2);
    --ref_stamp;
  }

  if (get_relative_dist(order_hint_info,
                        ref_order_hint[BWDREF_FRAME - LAST_FRAME],
                        cur_order_hint) > 0) {
    if (motion_field_projection(cm, BWDREF_FRAME, 0)) --ref_stamp;
  }

  if (get_relative_dist(order_hint_info,
                        ref_order_hint[ALTREF2_FRAME - LAST_FRAME],
                        cur_order_hint) > 0) {
    if (motion_field_projection(cm, ALTREF2_FRAME, 0)) --ref_stamp;
  }

  if (get_relative_dist(order_hint_info,
                        ref_order_hint[ALTREF_FRAME - LAST_FRAME],
                        cur_order_hint) > 0 &&
      ref_stamp >= 0)
    if (motion_field_projection(cm, ALTREF_FRAME, 0)) --ref_stamp;

  if (ref_stamp >= 0) motion_field_projection(cm, LAST2_FRAME, 2);
}

static inline void record_samples(const MB_MODE_INFO *mbmi, int *pts,
                                  int *pts_inref, int row_offset, int sign_r,
                                  int col_offset, int sign_c) {
  const int bw = block_size_wide[mbmi->bsize];
  const int bh = block_size_high[mbmi->bsize];
  const int x = col_offset * MI_SIZE + sign_c * bw / 2 - 1;
  const int y = row_offset * MI_SIZE + sign_r * bh / 2 - 1;

  pts[0] = GET_MV_SUBPEL(x);
  pts[1] = GET_MV_SUBPEL(y);
  pts_inref[0] = pts[0] + mbmi->mv[0].as_mv.col;
  pts_inref[1] = pts[1] + mbmi->mv[0].as_mv.row;
}

// Select samples according to the motion vector difference.
uint8_t av1_selectSamples(MV *mv, int *pts, int *pts_inref, int len,
                          BLOCK_SIZE bsize) {
  const int bw = block_size_wide[bsize];
  const int bh = block_size_high[bsize];
  const int thresh = clamp(AOMMAX(bw, bh), 16, 112);
  uint8_t ret = 0;
  assert(len <= LEAST_SQUARES_SAMPLES_MAX);

  // Only keep the samples with MV differences within threshold.
  for (int i = 0; i < len; ++i) {
    const int diff = abs(pts_inref[2 * i] - pts[2 * i] - mv->col) +
                     abs(pts_inref[2 * i + 1] - pts[2 * i + 1] - mv->row);
    if (diff > thresh) continue;
    if (ret != i) {
      memcpy(pts + 2 * ret, pts + 2 * i, 2 * sizeof(pts[0]));
      memcpy(pts_inref + 2 * ret, pts_inref + 2 * i, 2 * sizeof(pts_inref[0]));
    }
    ++ret;
  }
  // Keep at least 1 sample.
  return AOMMAX(ret, 1);
}

// Note: Samples returned are at 1/8-pel precision
// Sample are the neighbor block center point's coordinates relative to the
// left-top pixel of current block.
uint8_t av1_findSamples(const AV1_COMMON *cm, MACROBLOCKD *xd, int *pts,
                        int *pts_inref) {
  const MB_MODE_INFO *const mbmi0 = xd->mi[0];
  const int ref_frame = mbmi0->ref_frame[0];
  const int up_available = xd->up_available;
  const int left_available = xd->left_available;
  uint8_t np = 0;
  int do_tl = 1;
  int do_tr = 1;
  const int mi_stride = xd->mi_stride;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  // scan the nearest above rows
  if (up_available) {
    const int mi_row_offset = -1;
    const MB_MODE_INFO *mbmi = xd->mi[mi_row_offset * mi_stride];
    uint8_t superblock_width = mi_size_wide[mbmi->bsize];

    if (xd->width <= superblock_width) {
      // Handle "current block width <= above block width" case.
      const int col_offset = -mi_col % superblock_width;

      if (col_offset < 0) do_tl = 0;
      if (col_offset + superblock_width > xd->width) do_tr = 0;

      if (mbmi->ref_frame[0] == ref_frame && mbmi->ref_frame[1] == NONE_FRAME) {
        record_samples(mbmi, pts, pts_inref, 0, -1, col_offset, 1);
        pts += 2;
        pts_inref += 2;
        if (++np >= LEAST_SQUARES_SAMPLES_MAX) return LEAST_SQUARES_SAMPLES_MAX;
      }
    } else {
      // Handle "current block width > above block width" case.
      for (int i = 0; i < AOMMIN(xd->width, cm->mi_params.mi_cols - mi_col);
           i += superblock_width) {
        mbmi = xd->mi[i + mi_row_offset * mi_stride];
        superblock_width = mi_size_wide[mbmi->bsize];

        if (mbmi->ref_frame[0] == ref_frame &&
            mbmi->ref_frame[1] == NONE_FRAME) {
          record_samples(mbmi, pts, pts_inref, 0, -1, i, 1);
          pts += 2;
          pts_inref += 2;
          if (++np >= LEAST_SQUARES_SAMPLES_MAX)
            return LEAST_SQUARES_SAMPLES_MAX;
        }
      }
    }
  }
  assert(np <= LEAST_SQUARES_SAMPLES_MAX);

  // scan the nearest left columns
  if (left_available) {
    const int mi_col_offset = -1;
    const MB_MODE_INFO *mbmi = xd->mi[mi_col_offset];
    uint8_t superblock_height = mi_size_high[mbmi->bsize];

    if (xd->height <= superblock_height) {
      // Handle "current block height <= above block height" case.
      const int row_offset = -mi_row % superblock_height;

      if (row_offset < 0) do_tl = 0;

      if (mbmi->ref_frame[0] == ref_frame && mbmi->ref_frame[1] == NONE_FRAME) {
        record_samples(mbmi, pts, pts_inref, row_offset, 1, 0, -1);
        pts += 2;
        pts_inref += 2;
        np++;
        if (np >= LEAST_SQUARES_SAMPLES_MAX) return LEAST_SQUARES_SAMPLES_MAX;
      }
    } else {
      // Handle "current block height > above block height" case.
      for (int i = 0; i < AOMMIN(xd->height, cm->mi_params.mi_rows - mi_row);
           i += superblock_height) {
        mbmi = xd->mi[mi_col_offset + i * mi_stride];
        superblock_height = mi_size_high[mbmi->bsize];

        if (mbmi->ref_frame[0] == ref_frame &&
            mbmi->ref_frame[1] == NONE_FRAME) {
          record_samples(mbmi, pts, pts_inref, i, 1, 0, -1);
          pts += 2;
          pts_inref += 2;
          if (++np >= LEAST_SQUARES_SAMPLES_MAX)
            return LEAST_SQUARES_SAMPLES_MAX;
        }
      }
    }
  }
  assert(np <= LEAST_SQUARES_SAMPLES_MAX);

  // Top-left block
  if (do_tl && left_available && up_available) {
    const int mi_row_offset = -1;
    const int mi_col_offset = -1;
    MB_MODE_INFO *mbmi = xd->mi[mi_col_offset + mi_row_offset * mi_stride];

    if (mbmi->ref_frame[0] == ref_frame && mbmi->ref_frame[1] == NONE_FRAME) {
      record_samples(mbmi, pts, pts_inref, 0, -1, 0, -1);
      pts += 2;
      pts_inref += 2;
      if (++np >= LEAST_SQUARES_SAMPLES_MAX) return LEAST_SQUARES_SAMPLES_MAX;
    }
  }
  assert(np <= LEAST_SQUARES_SAMPLES_MAX);

  // Top-right block
  if (do_tr &&
      has_top_right(cm, xd, mi_row, mi_col, AOMMAX(xd->width, xd->height))) {
    const POSITION trb_pos = { -1, xd->width };
    const TileInfo *const tile = &xd->tile;
    if (is_inside(tile, mi_col, mi_row, &trb_pos)) {
      const int mi_row_offset = -1;
      const int mi_col_offset = xd->width;
      const MB_MODE_INFO *mbmi =
          xd->mi[mi_col_offset + mi_row_offset * mi_stride];

      if (mbmi->ref_frame[0] == ref_frame && mbmi->ref_frame[1] == NONE_FRAME) {
        record_samples(mbmi, pts, pts_inref, 0, -1, xd->width, 1);
        if (++np >= LEAST_SQUARES_SAMPLES_MAX) return LEAST_SQUARES_SAMPLES_MAX;
      }
    }
  }
  assert(np <= LEAST_SQUARES_SAMPLES_MAX);

  return np;
}

void av1_setup_skip_mode_allowed(AV1_COMMON *cm) {
  const OrderHintInfo *const order_hint_info = &cm->seq_params->order_hint_info;
  SkipModeInfo *const skip_mode_info = &cm->current_frame.skip_mode_info;

  skip_mode_info->skip_mode_allowed = 0;
  skip_mode_info->ref_frame_idx_0 = INVALID_IDX;
  skip_mode_info->ref_frame_idx_1 = INVALID_IDX;

  if (!order_hint_info->enable_order_hint || frame_is_intra_only(cm) ||
      cm->current_frame.reference_mode == SINGLE_REFERENCE)
    return;

  const int cur_order_hint = cm->current_frame.order_hint;
  int ref_order_hints[2] = { -1, INT_MAX };
  int ref_idx[2] = { INVALID_IDX, INVALID_IDX };

  // Identify the nearest forward and backward references.
  for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    const RefCntBuffer *const buf = get_ref_frame_buf(cm, LAST_FRAME + i);
    if (buf == NULL) continue;

    const int ref_order_hint = buf->order_hint;
    if (get_relative_dist(order_hint_info, ref_order_hint, cur_order_hint) <
        0) {
      // Forward reference
      if (ref_order_hints[0] == -1 ||
          get_relative_dist(order_hint_info, ref_order_hint,
                            ref_order_hints[0]) > 0) {
        ref_order_hints[0] = ref_order_hint;
        ref_idx[0] = i;
      }
    } else if (get_relative_dist(order_hint_info, ref_order_hint,
                                 cur_order_hint) > 0) {
      // Backward reference
      if (ref_order_hints[1] == INT_MAX ||
          get_relative_dist(order_hint_info, ref_order_hint,
                            ref_order_hints[1]) < 0) {
        ref_order_hints[1] = ref_order_hint;
        ref_idx[1] = i;
      }
    }
  }

  if (ref_idx[0] != INVALID_IDX && ref_idx[1] != INVALID_IDX) {
    // == Bi-directional prediction ==
    skip_mode_info->skip_mode_allowed = 1;
    skip_mode_info->ref_frame_idx_0 = AOMMIN(ref_idx[0], ref_idx[1]);
    skip_mode_info->ref_frame_idx_1 = AOMMAX(ref_idx[0], ref_idx[1]);
  } else if (ref_idx[0] != INVALID_IDX && ref_idx[1] == INVALID_IDX) {
    // == Forward prediction only ==
    // Identify the second nearest forward reference.
    ref_order_hints[1] = -1;
    for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
      const RefCntBuffer *const buf = get_ref_frame_buf(cm, LAST_FRAME + i);
      if (buf == NULL) continue;

      const int ref_order_hint = buf->order_hint;
      if ((ref_order_hints[0] != -1 &&
           get_relative_dist(order_hint_info, ref_order_hint,
                             ref_order_hints[0]) < 0) &&
          (ref_order_hints[1] == -1 ||
           get_relative_dist(order_hint_info, ref_order_hint,
                             ref_order_hints[1]) > 0)) {
        // Second closest forward reference
        ref_order_hints[1] = ref_order_hint;
        ref_idx[1] = i;
      }
    }
    if (ref_order_hints[1] != -1) {
      skip_mode_info->skip_mode_allowed = 1;
      skip_mode_info->ref_frame_idx_0 = AOMMIN(ref_idx[0], ref_idx[1]);
      skip_mode_info->ref_frame_idx_1 = AOMMAX(ref_idx[0], ref_idx[1]);
    }
  }
}

typedef struct {
  int map_idx;        // frame map index
  RefCntBuffer *buf;  // frame buffer
  int sort_idx;       // index based on the offset to be used for sorting
} REF_FRAME_INFO;

// Compares the sort_idx fields. If they are equal, then compares the map_idx
// fields to break the tie. This ensures a stable sort.
static int compare_ref_frame_info(const void *arg_a, const void *arg_b) {
  const REF_FRAME_INFO *info_a = (REF_FRAME_INFO *)arg_a;
  const REF_FRAME_INFO *info_b = (REF_FRAME_INFO *)arg_b;

  const int sort_idx_diff = info_a->sort_idx - info_b->sort_idx;
  if (sort_idx_diff != 0) return sort_idx_diff;
  return info_a->map_idx - info_b->map_idx;
}

static inline void set_ref_frame_info(int *remapped_ref_idx, int frame_idx,
                                      REF_FRAME_INFO *ref_info) {
  assert(frame_idx >= 0 && frame_idx < INTER_REFS_PER_FRAME);

  remapped_ref_idx[frame_idx] = ref_info->map_idx;
}

void av1_set_frame_refs(AV1_COMMON *const cm, int *remapped_ref_idx,
                        int lst_map_idx, int gld_map_idx) {
  int lst_frame_sort_idx = -1;
  int gld_frame_sort_idx = -1;

  assert(cm->seq_params->order_hint_info.enable_order_hint);
  assert(cm->seq_params->order_hint_info.order_hint_bits_minus_1 >= 0);
  const int cur_order_hint = (int)cm->current_frame.order_hint;
  const int cur_frame_sort_idx =
      1 << cm->seq_params->order_hint_info.order_hint_bits_minus_1;

  REF_FRAME_INFO ref_frame_info[REF_FRAMES];
  int ref_flag_list[INTER_REFS_PER_FRAME] = { 0, 0, 0, 0, 0, 0, 0 };

  for (int i = 0; i < REF_FRAMES; ++i) {
    const int map_idx = i;

    ref_frame_info[i].map_idx = map_idx;
    ref_frame_info[i].sort_idx = -1;

    RefCntBuffer *const buf = cm->ref_frame_map[map_idx];
    ref_frame_info[i].buf = buf;

    if (buf == NULL) continue;
    // If this assertion fails, there is a reference leak.
    assert(buf->ref_count > 0);

    const int offset = (int)buf->order_hint;
    ref_frame_info[i].sort_idx =
        (offset == -1) ? -1
                       : cur_frame_sort_idx +
                             get_relative_dist(&cm->seq_params->order_hint_info,
                                               offset, cur_order_hint);
    assert(ref_frame_info[i].sort_idx >= -1);

    if (map_idx == lst_map_idx) lst_frame_sort_idx = ref_frame_info[i].sort_idx;
    if (map_idx == gld_map_idx) gld_frame_sort_idx = ref_frame_info[i].sort_idx;
  }

  // Confirm both LAST_FRAME and GOLDEN_FRAME are valid forward reference
  // frames.
  if (lst_frame_sort_idx == -1 || lst_frame_sort_idx >= cur_frame_sort_idx) {
    aom_internal_error(cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Inter frame requests a look-ahead frame as LAST");
  }
  if (gld_frame_sort_idx == -1 || gld_frame_sort_idx >= cur_frame_sort_idx) {
    aom_internal_error(cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Inter frame requests a look-ahead frame as GOLDEN");
  }

  // Sort ref frames based on their frame_offset values.
  qsort(ref_frame_info, REF_FRAMES, sizeof(REF_FRAME_INFO),
        compare_ref_frame_info);

  // Identify forward and backward reference frames.
  // Forward  reference: offset < order_hint
  // Backward reference: offset >= order_hint
  int fwd_start_idx = 0, fwd_end_idx = REF_FRAMES - 1;

  for (int i = 0; i < REF_FRAMES; i++) {
    if (ref_frame_info[i].sort_idx == -1) {
      fwd_start_idx++;
      continue;
    }

    if (ref_frame_info[i].sort_idx >= cur_frame_sort_idx) {
      fwd_end_idx = i - 1;
      break;
    }
  }

  int bwd_start_idx = fwd_end_idx + 1;
  int bwd_end_idx = REF_FRAMES - 1;

  // === Backward Reference Frames ===

  // == ALTREF_FRAME ==
  if (bwd_start_idx <= bwd_end_idx) {
    set_ref_frame_info(remapped_ref_idx, ALTREF_FRAME - LAST_FRAME,
                       &ref_frame_info[bwd_end_idx]);
    ref_flag_list[ALTREF_FRAME - LAST_FRAME] = 1;
    bwd_end_idx--;
  }

  // == BWDREF_FRAME ==
  if (bwd_start_idx <= bwd_end_idx) {
    set_ref_frame_info(remapped_ref_idx, BWDREF_FRAME - LAST_FRAME,
                       &ref_frame_info[bwd_start_idx]);
    ref_flag_list[BWDREF_FRAME - LAST_FRAME] = 1;
    bwd_start_idx++;
  }

  // == ALTREF2_FRAME ==
  if (bwd_start_idx <= bwd_end_idx) {
    set_ref_frame_info(remapped_ref_idx, ALTREF2_FRAME - LAST_FRAME,
                       &ref_frame_info[bwd_start_idx]);
    ref_flag_list[ALTREF2_FRAME - LAST_FRAME] = 1;
  }

  // === Forward Reference Frames ===

  for (int i = fwd_start_idx; i <= fwd_end_idx; ++i) {
    // == LAST_FRAME ==
    if (ref_frame_info[i].map_idx == lst_map_idx) {
      set_ref_frame_info(remapped_ref_idx, LAST_FRAME - LAST_FRAME,
                         &ref_frame_info[i]);
      ref_flag_list[LAST_FRAME - LAST_FRAME] = 1;
    }

    // == GOLDEN_FRAME ==
    if (ref_frame_info[i].map_idx == gld_map_idx) {
      set_ref_frame_info(remapped_ref_idx, GOLDEN_FRAME - LAST_FRAME,
                         &ref_frame_info[i]);
      ref_flag_list[GOLDEN_FRAME - LAST_FRAME] = 1;
    }
  }

  assert(ref_flag_list[LAST_FRAME - LAST_FRAME] == 1 &&
         ref_flag_list[GOLDEN_FRAME - LAST_FRAME] == 1);

  // == LAST2_FRAME ==
  // == LAST3_FRAME ==
  // == BWDREF_FRAME ==
  // == ALTREF2_FRAME ==
  // == ALTREF_FRAME ==

  // Set up the reference frames in the anti-chronological order.
  static const MV_REFERENCE_FRAME ref_frame_list[INTER_REFS_PER_FRAME - 2] = {
    LAST2_FRAME, LAST3_FRAME, BWDREF_FRAME, ALTREF2_FRAME, ALTREF_FRAME
  };

  int ref_idx;
  for (ref_idx = 0; ref_idx < (INTER_REFS_PER_FRAME - 2); ref_idx++) {
    const MV_REFERENCE_FRAME ref_frame = ref_frame_list[ref_idx];

    if (ref_flag_list[ref_frame - LAST_FRAME] == 1) continue;

    while (fwd_start_idx <= fwd_end_idx &&
           (ref_frame_info[fwd_end_idx].map_idx == lst_map_idx ||
            ref_frame_info[fwd_end_idx].map_idx == gld_map_idx)) {
      fwd_end_idx--;
    }
    if (fwd_start_idx > fwd_end_idx) break;

    set_ref_frame_info(remapped_ref_idx, ref_frame - LAST_FRAME,
                       &ref_frame_info[fwd_end_idx]);
    ref_flag_list[ref_frame - LAST_FRAME] = 1;

    fwd_end_idx--;
  }

  // Assign all the remaining frame(s), if any, to the earliest reference
  // frame.
  for (; ref_idx < (INTER_REFS_PER_FRAME - 2); ref_idx++) {
    const MV_REFERENCE_FRAME ref_frame = ref_frame_list[ref_idx];
    if (ref_flag_list[ref_frame - LAST_FRAME] == 1) continue;
    set_ref_frame_info(remapped_ref_idx, ref_frame - LAST_FRAME,
                       &ref_frame_info[fwd_start_idx]);
    ref_flag_list[ref_frame - LAST_FRAME] = 1;
  }

  for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
    assert(ref_flag_list[i] == 1);
  }
}
