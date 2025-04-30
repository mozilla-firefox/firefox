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
#include <stdbool.h>
#include <string.h>

#include "config/aom_dsp_rtcd.h"
#include "config/aom_scale_rtcd.h"

#include "aom/aom_integer.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/reconinter.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/ethread.h"
#include "av1/encoder/pickcdef.h"
#include "av1/encoder/mcomp.h"

// Get primary and secondary filter strength for the given strength index and
// search method
static inline void get_cdef_filter_strengths(CDEF_PICK_METHOD pick_method,
                                             int *pri_strength,
                                             int *sec_strength,
                                             int strength_idx) {
  const int tot_sec_filter =
      (pick_method == CDEF_FAST_SEARCH_LVL5)
          ? REDUCED_SEC_STRENGTHS_LVL5
          : ((pick_method >= CDEF_FAST_SEARCH_LVL3) ? REDUCED_SEC_STRENGTHS_LVL3
                                                    : CDEF_SEC_STRENGTHS);
  const int pri_idx = strength_idx / tot_sec_filter;
  const int sec_idx = strength_idx % tot_sec_filter;
  *pri_strength = pri_idx;
  *sec_strength = sec_idx;
  if (pick_method == CDEF_FULL_SEARCH) return;

  switch (pick_method) {
    case CDEF_FAST_SEARCH_LVL1:
      assert(pri_idx < REDUCED_PRI_STRENGTHS_LVL1);
      *pri_strength = priconv_lvl1[pri_idx];
      break;
    case CDEF_FAST_SEARCH_LVL2:
      assert(pri_idx < REDUCED_PRI_STRENGTHS_LVL2);
      *pri_strength = priconv_lvl2[pri_idx];
      break;
    case CDEF_FAST_SEARCH_LVL3:
      assert(pri_idx < REDUCED_PRI_STRENGTHS_LVL2);
      assert(sec_idx < REDUCED_SEC_STRENGTHS_LVL3);
      *pri_strength = priconv_lvl2[pri_idx];
      *sec_strength = secconv_lvl3[sec_idx];
      break;
    case CDEF_FAST_SEARCH_LVL4:
      assert(pri_idx < REDUCED_PRI_STRENGTHS_LVL4);
      assert(sec_idx < REDUCED_SEC_STRENGTHS_LVL3);
      *pri_strength = priconv_lvl4[pri_idx];
      *sec_strength = secconv_lvl3[sec_idx];
      break;
    case CDEF_FAST_SEARCH_LVL5:
      assert(pri_idx < REDUCED_PRI_STRENGTHS_LVL4);
      assert(sec_idx < REDUCED_SEC_STRENGTHS_LVL5);
      *pri_strength = priconv_lvl5[pri_idx];
      *sec_strength = secconv_lvl5[sec_idx];
      break;
    default: assert(0 && "Invalid CDEF search method");
  }
}

// Store CDEF filter strength calculated from strength index for given search
// method
#define STORE_CDEF_FILTER_STRENGTH(cdef_strength, pick_method, strength_idx) \
  do {                                                                       \
    get_cdef_filter_strengths((pick_method), &pri_strength, &sec_strength,   \
                              (strength_idx));                               \
    cdef_strength = pri_strength * CDEF_SEC_STRENGTHS + sec_strength;        \
  } while (0)

/* Search for the best strength to add as an option, knowing we
   already selected nb_strengths options. */
static uint64_t search_one(int *lev, int nb_strengths,
                           uint64_t mse[][TOTAL_STRENGTHS], int sb_count,
                           CDEF_PICK_METHOD pick_method) {
  uint64_t tot_mse[TOTAL_STRENGTHS];
  const int total_strengths = nb_cdef_strengths[pick_method];
  int i, j;
  uint64_t best_tot_mse = (uint64_t)1 << 63;
  int best_id = 0;
  memset(tot_mse, 0, sizeof(tot_mse));
  for (i = 0; i < sb_count; i++) {
    int gi;
    uint64_t best_mse = (uint64_t)1 << 63;
    /* Find best mse among already selected options. */
    for (gi = 0; gi < nb_strengths; gi++) {
      if (mse[i][lev[gi]] < best_mse) {
        best_mse = mse[i][lev[gi]];
      }
    }
    /* Find best mse when adding each possible new option. */
    for (j = 0; j < total_strengths; j++) {
      uint64_t best = best_mse;
      if (mse[i][j] < best) best = mse[i][j];
      tot_mse[j] += best;
    }
  }
  for (j = 0; j < total_strengths; j++) {
    if (tot_mse[j] < best_tot_mse) {
      best_tot_mse = tot_mse[j];
      best_id = j;
    }
  }
  lev[nb_strengths] = best_id;
  return best_tot_mse;
}

/* Search for the best luma+chroma strength to add as an option, knowing we
   already selected nb_strengths options. */
static uint64_t search_one_dual(int *lev0, int *lev1, int nb_strengths,
                                uint64_t (**mse)[TOTAL_STRENGTHS], int sb_count,
                                CDEF_PICK_METHOD pick_method) {
  uint64_t tot_mse[TOTAL_STRENGTHS][TOTAL_STRENGTHS];
  int i, j;
  uint64_t best_tot_mse = (uint64_t)1 << 63;
  int best_id0 = 0;
  int best_id1 = 0;
  const int total_strengths = nb_cdef_strengths[pick_method];
  memset(tot_mse, 0, sizeof(tot_mse));
  for (i = 0; i < sb_count; i++) {
    int gi;
    uint64_t best_mse = (uint64_t)1 << 63;
    /* Find best mse among already selected options. */
    for (gi = 0; gi < nb_strengths; gi++) {
      uint64_t curr = mse[0][i][lev0[gi]];
      curr += mse[1][i][lev1[gi]];
      if (curr < best_mse) {
        best_mse = curr;
      }
    }
    /* Find best mse when adding each possible new option. */
    for (j = 0; j < total_strengths; j++) {
      int k;
      for (k = 0; k < total_strengths; k++) {
        uint64_t best = best_mse;
        uint64_t curr = mse[0][i][j];
        curr += mse[1][i][k];
        if (curr < best) best = curr;
        tot_mse[j][k] += best;
      }
    }
  }
  for (j = 0; j < total_strengths; j++) {
    int k;
    for (k = 0; k < total_strengths; k++) {
      if (tot_mse[j][k] < best_tot_mse) {
        best_tot_mse = tot_mse[j][k];
        best_id0 = j;
        best_id1 = k;
      }
    }
  }
  lev0[nb_strengths] = best_id0;
  lev1[nb_strengths] = best_id1;
  return best_tot_mse;
}

/* Search for the set of strengths that minimizes mse. */
static uint64_t joint_strength_search(int *best_lev, int nb_strengths,
                                      uint64_t mse[][TOTAL_STRENGTHS],
                                      int sb_count,
                                      CDEF_PICK_METHOD pick_method) {
  uint64_t best_tot_mse;
  int fast = (pick_method >= CDEF_FAST_SEARCH_LVL1 &&
              pick_method <= CDEF_FAST_SEARCH_LVL5);
  int i;
  best_tot_mse = (uint64_t)1 << 63;
  /* Greedy search: add one strength options at a time. */
  for (i = 0; i < nb_strengths; i++) {
    best_tot_mse = search_one(best_lev, i, mse, sb_count, pick_method);
  }
  /* Trying to refine the greedy search by reconsidering each
     already-selected option. */
  if (!fast) {
    for (i = 0; i < 4 * nb_strengths; i++) {
      int j;
      for (j = 0; j < nb_strengths - 1; j++) best_lev[j] = best_lev[j + 1];
      best_tot_mse =
          search_one(best_lev, nb_strengths - 1, mse, sb_count, pick_method);
    }
  }
  return best_tot_mse;
}

/* Search for the set of luma+chroma strengths that minimizes mse. */
static uint64_t joint_strength_search_dual(int *best_lev0, int *best_lev1,
                                           int nb_strengths,
                                           uint64_t (**mse)[TOTAL_STRENGTHS],
                                           int sb_count,
                                           CDEF_PICK_METHOD pick_method) {
  uint64_t best_tot_mse;
  int i;
  best_tot_mse = (uint64_t)1 << 63;
  /* Greedy search: add one strength options at a time. */
  for (i = 0; i < nb_strengths; i++) {
    best_tot_mse =
        search_one_dual(best_lev0, best_lev1, i, mse, sb_count, pick_method);
  }
  /* Trying to refine the greedy search by reconsidering each
     already-selected option. */
  for (i = 0; i < 4 * nb_strengths; i++) {
    int j;
    for (j = 0; j < nb_strengths - 1; j++) {
      best_lev0[j] = best_lev0[j + 1];
      best_lev1[j] = best_lev1[j + 1];
    }
    best_tot_mse = search_one_dual(best_lev0, best_lev1, nb_strengths - 1, mse,
                                   sb_count, pick_method);
  }
  return best_tot_mse;
}

static inline void init_src_params(int *src_stride, int *width, int *height,
                                   int *width_log2, int *height_log2,
                                   BLOCK_SIZE bsize) {
  *src_stride = block_size_wide[bsize];
  *width = block_size_wide[bsize];
  *height = block_size_high[bsize];
  *width_log2 = MI_SIZE_LOG2 + mi_size_wide_log2[bsize];
  *height_log2 = MI_SIZE_LOG2 + mi_size_high_log2[bsize];
}
#if CONFIG_AV1_HIGHBITDEPTH
/* Compute MSE only on the blocks we filtered. */
static uint64_t compute_cdef_dist_highbd(void *dst, int dstride, uint16_t *src,
                                         cdef_list *dlist, int cdef_count,
                                         BLOCK_SIZE bsize, int coeff_shift,
                                         int row, int col) {
  assert(bsize == BLOCK_4X4 || bsize == BLOCK_4X8 || bsize == BLOCK_8X4 ||
         bsize == BLOCK_8X8);
  uint64_t sum = 0;
  int bi, bx, by;
  uint16_t *dst16 = CONVERT_TO_SHORTPTR((uint8_t *)dst);
  uint16_t *dst_buff = &dst16[row * dstride + col];
  int src_stride, width, height, width_log2, height_log2;
  init_src_params(&src_stride, &width, &height, &width_log2, &height_log2,
                  bsize);
  for (bi = 0; bi < cdef_count; bi++) {
    by = dlist[bi].by;
    bx = dlist[bi].bx;
    sum += aom_mse_wxh_16bit_highbd(
        &dst_buff[(by << height_log2) * dstride + (bx << width_log2)], dstride,
        &src[bi << (height_log2 + width_log2)], src_stride, width, height);
  }
  return sum >> 2 * coeff_shift;
}
#endif

// Checks dual and quad block processing is applicable for block widths 8 and 4
// respectively.
static inline int is_dual_or_quad_applicable(cdef_list *dlist, int width,
                                             int cdef_count, int bi, int iter) {
  assert(width == 8 || width == 4);
  const int blk_offset = (width == 8) ? 1 : 3;
  if ((iter + blk_offset) >= cdef_count) return 0;

  if (dlist[bi].by == dlist[bi + blk_offset].by &&
      dlist[bi].bx + blk_offset == dlist[bi + blk_offset].bx)
    return 1;

  return 0;
}

static uint64_t compute_cdef_dist(void *dst, int dstride, uint16_t *src,
                                  cdef_list *dlist, int cdef_count,
                                  BLOCK_SIZE bsize, int coeff_shift, int row,
                                  int col) {
  assert(bsize == BLOCK_4X4 || bsize == BLOCK_4X8 || bsize == BLOCK_8X4 ||
         bsize == BLOCK_8X8);
  uint64_t sum = 0;
  int bi, bx, by;
  int iter = 0;
  int inc = 1;
  uint8_t *dst8 = (uint8_t *)dst;
  uint8_t *dst_buff = &dst8[row * dstride + col];
  int src_stride, width, height, width_log2, height_log2;
  init_src_params(&src_stride, &width, &height, &width_log2, &height_log2,
                  bsize);

  const int num_blks = 16 / width;
  for (bi = 0; bi < cdef_count; bi += inc) {
    by = dlist[bi].by;
    bx = dlist[bi].bx;
    uint16_t *src_tmp = &src[bi << (height_log2 + width_log2)];
    uint8_t *dst_tmp =
        &dst_buff[(by << height_log2) * dstride + (bx << width_log2)];

    if (is_dual_or_quad_applicable(dlist, width, cdef_count, bi, iter)) {
      sum += aom_mse_16xh_16bit(dst_tmp, dstride, src_tmp, width, height);
      iter += num_blks;
      inc = num_blks;
    } else {
      sum += aom_mse_wxh_16bit(dst_tmp, dstride, src_tmp, src_stride, width,
                               height);
      iter += 1;
      inc = 1;
    }
  }

  return sum >> 2 * coeff_shift;
}

// Fill the boundary regions of the block with CDEF_VERY_LARGE, only if the
// region is outside frame boundary
static inline void fill_borders_for_fbs_on_frame_boundary(
    uint16_t *inbuf, int hfilt_size, int vfilt_size,
    bool is_fb_on_frm_left_boundary, bool is_fb_on_frm_right_boundary,
    bool is_fb_on_frm_top_boundary, bool is_fb_on_frm_bottom_boundary) {
  if (!is_fb_on_frm_left_boundary && !is_fb_on_frm_right_boundary &&
      !is_fb_on_frm_top_boundary && !is_fb_on_frm_bottom_boundary)
    return;
  if (is_fb_on_frm_bottom_boundary) {
    // Fill bottom region of the block
    const int buf_offset =
        (vfilt_size + CDEF_VBORDER) * CDEF_BSTRIDE + CDEF_HBORDER;
    fill_rect(&inbuf[buf_offset], CDEF_BSTRIDE, CDEF_VBORDER, hfilt_size,
              CDEF_VERY_LARGE);
  }
  if (is_fb_on_frm_bottom_boundary || is_fb_on_frm_left_boundary) {
    const int buf_offset = (vfilt_size + CDEF_VBORDER) * CDEF_BSTRIDE;
    // Fill bottom-left region of the block
    fill_rect(&inbuf[buf_offset], CDEF_BSTRIDE, CDEF_VBORDER, CDEF_HBORDER,
              CDEF_VERY_LARGE);
  }
  if (is_fb_on_frm_bottom_boundary || is_fb_on_frm_right_boundary) {
    const int buf_offset =
        (vfilt_size + CDEF_VBORDER) * CDEF_BSTRIDE + hfilt_size + CDEF_HBORDER;
    // Fill bottom-right region of the block
    fill_rect(&inbuf[buf_offset], CDEF_BSTRIDE, CDEF_VBORDER, CDEF_HBORDER,
              CDEF_VERY_LARGE);
  }
  if (is_fb_on_frm_top_boundary) {
    // Fill top region of the block
    fill_rect(&inbuf[CDEF_HBORDER], CDEF_BSTRIDE, CDEF_VBORDER, hfilt_size,
              CDEF_VERY_LARGE);
  }
  if (is_fb_on_frm_top_boundary || is_fb_on_frm_left_boundary) {
    // Fill top-left region of the block
    fill_rect(inbuf, CDEF_BSTRIDE, CDEF_VBORDER, CDEF_HBORDER, CDEF_VERY_LARGE);
  }
  if (is_fb_on_frm_top_boundary || is_fb_on_frm_right_boundary) {
    const int buf_offset = hfilt_size + CDEF_HBORDER;
    // Fill top-right region of the block
    fill_rect(&inbuf[buf_offset], CDEF_BSTRIDE, CDEF_VBORDER, CDEF_HBORDER,
              CDEF_VERY_LARGE);
  }
  if (is_fb_on_frm_left_boundary) {
    const int buf_offset = CDEF_VBORDER * CDEF_BSTRIDE;
    // Fill left region of the block
    fill_rect(&inbuf[buf_offset], CDEF_BSTRIDE, vfilt_size, CDEF_HBORDER,
              CDEF_VERY_LARGE);
  }
  if (is_fb_on_frm_right_boundary) {
    const int buf_offset = CDEF_VBORDER * CDEF_BSTRIDE;
    // Fill right region of the block
    fill_rect(&inbuf[buf_offset + hfilt_size + CDEF_HBORDER], CDEF_BSTRIDE,
              vfilt_size, CDEF_HBORDER, CDEF_VERY_LARGE);
  }
}

// Calculate the number of 8x8/4x4 filter units for which SSE can be calculated
// after CDEF filtering in single function call
static AOM_FORCE_INLINE int get_error_calc_width_in_filt_units(
    cdef_list *dlist, int cdef_count, int bi, int subsampling_x,
    int subsampling_y) {
  // TODO(Ranjit): Extend the optimization for 422
  if (subsampling_x != subsampling_y) return 1;

  // Combining more blocks seems to increase encode time due to increase in
  // control code
  if (bi + 3 < cdef_count && dlist[bi].by == dlist[bi + 3].by &&
      dlist[bi].bx + 3 == dlist[bi + 3].bx) {
    /* Calculate error for four 8x8/4x4 blocks using 32x8/16x4 block specific
     * logic if y co-ordinates match and x co-ordinates are
     * separated by 3 for first and fourth 8x8/4x4 blocks in dlist[]. */
    return 4;
  }
  if (bi + 1 < cdef_count && dlist[bi].by == dlist[bi + 1].by &&
      dlist[bi].bx + 1 == dlist[bi + 1].bx) {
    /* Calculate error for two 8x8/4x4 blocks using 16x8/8x4 block specific
     * logic if their y co-ordinates match and x co-ordinates are
     * separated by 1 for first and second 8x8/4x4 blocks in dlist[]. */
    return 2;
  }
  return 1;
}

// Returns the block error after CDEF filtering for a given strength
static inline uint64_t get_filt_error(
    const CdefSearchCtx *cdef_search_ctx, const struct macroblockd_plane *pd,
    cdef_list *dlist, int dir[CDEF_NBLOCKS][CDEF_NBLOCKS], int *dirinit,
    int var[CDEF_NBLOCKS][CDEF_NBLOCKS], uint16_t *in, uint8_t *ref_buffer,
    int ref_stride, int row, int col, int pri_strength, int sec_strength,
    int cdef_count, int pli, int coeff_shift, BLOCK_SIZE bs) {
  uint64_t curr_sse = 0;
  const BLOCK_SIZE plane_bsize =
      get_plane_block_size(bs, pd->subsampling_x, pd->subsampling_y);
  const int bw_log2 = 3 - pd->subsampling_x;
  const int bh_log2 = 3 - pd->subsampling_y;

  // TODO(Ranjit): Extend this optimization for HBD
  if (!cdef_search_ctx->use_highbitdepth) {
    // If all 8x8/4x4 blocks in CDEF block need to be filtered, calculate the
    // error at CDEF block level
    const int tot_blk_count =
        (block_size_wide[plane_bsize] * block_size_high[plane_bsize]) >>
        (bw_log2 + bh_log2);
    if (cdef_count == tot_blk_count) {
      // Calculate the offset in the buffer based on block position
      const FULLPEL_MV this_mv = { row, col };
      const int buf_offset = get_offset_from_fullmv(&this_mv, ref_stride);
      if (pri_strength == 0 && sec_strength == 0) {
        // When CDEF strength is zero, filtering is not applied. Hence
        // error is calculated between source and unfiltered pixels
        curr_sse =
            aom_sse(&ref_buffer[buf_offset], ref_stride,
                    get_buf_from_fullmv(&pd->dst, &this_mv), pd->dst.stride,
                    block_size_wide[plane_bsize], block_size_high[plane_bsize]);
      } else {
        DECLARE_ALIGNED(32, uint8_t, tmp_dst8[1 << (MAX_SB_SIZE_LOG2 * 2)]);

        av1_cdef_filter_fb(tmp_dst8, NULL, (1 << MAX_SB_SIZE_LOG2), in,
                           cdef_search_ctx->xdec[pli],
                           cdef_search_ctx->ydec[pli], dir, dirinit, var, pli,
                           dlist, cdef_count, pri_strength,
                           sec_strength + (sec_strength == 3),
                           cdef_search_ctx->damping, coeff_shift);
        curr_sse =
            aom_sse(&ref_buffer[buf_offset], ref_stride, tmp_dst8,
                    (1 << MAX_SB_SIZE_LOG2), block_size_wide[plane_bsize],
                    block_size_high[plane_bsize]);
      }
    } else {
      // If few 8x8/4x4 blocks in CDEF block need to be filtered, filtering
      // functions produce 8-bit output and the error is calculated in 8-bit
      // domain
      if (pri_strength == 0 && sec_strength == 0) {
        int num_error_calc_filt_units = 1;
        for (int bi = 0; bi < cdef_count; bi = bi + num_error_calc_filt_units) {
          const uint8_t by = dlist[bi].by;
          const uint8_t bx = dlist[bi].bx;
          const int16_t by_pos = (by << bh_log2);
          const int16_t bx_pos = (bx << bw_log2);
          // Calculate the offset in the buffer based on block position
          const FULLPEL_MV this_mv = { row + by_pos, col + bx_pos };
          const int buf_offset = get_offset_from_fullmv(&this_mv, ref_stride);
          num_error_calc_filt_units = get_error_calc_width_in_filt_units(
              dlist, cdef_count, bi, pd->subsampling_x, pd->subsampling_y);
          curr_sse += aom_sse(
              &ref_buffer[buf_offset], ref_stride,
              get_buf_from_fullmv(&pd->dst, &this_mv), pd->dst.stride,
              num_error_calc_filt_units * (1 << bw_log2), (1 << bh_log2));
        }
      } else {
        DECLARE_ALIGNED(32, uint8_t, tmp_dst8[1 << (MAX_SB_SIZE_LOG2 * 2)]);
        av1_cdef_filter_fb(tmp_dst8, NULL, (1 << MAX_SB_SIZE_LOG2), in,
                           cdef_search_ctx->xdec[pli],
                           cdef_search_ctx->ydec[pli], dir, dirinit, var, pli,
                           dlist, cdef_count, pri_strength,
                           sec_strength + (sec_strength == 3),
                           cdef_search_ctx->damping, coeff_shift);
        int num_error_calc_filt_units = 1;
        for (int bi = 0; bi < cdef_count; bi = bi + num_error_calc_filt_units) {
          const uint8_t by = dlist[bi].by;
          const uint8_t bx = dlist[bi].bx;
          const int16_t by_pos = (by << bh_log2);
          const int16_t bx_pos = (bx << bw_log2);
          // Calculate the offset in the buffer based on block position
          const FULLPEL_MV this_mv = { row + by_pos, col + bx_pos };
          const FULLPEL_MV tmp_buf_pos = { by_pos, bx_pos };
          const int buf_offset = get_offset_from_fullmv(&this_mv, ref_stride);
          const int tmp_buf_offset =
              get_offset_from_fullmv(&tmp_buf_pos, (1 << MAX_SB_SIZE_LOG2));
          num_error_calc_filt_units = get_error_calc_width_in_filt_units(
              dlist, cdef_count, bi, pd->subsampling_x, pd->subsampling_y);
          curr_sse += aom_sse(
              &ref_buffer[buf_offset], ref_stride, &tmp_dst8[tmp_buf_offset],
              (1 << MAX_SB_SIZE_LOG2),
              num_error_calc_filt_units * (1 << bw_log2), (1 << bh_log2));
        }
      }
    }
  } else {
    DECLARE_ALIGNED(32, uint16_t, tmp_dst[1 << (MAX_SB_SIZE_LOG2 * 2)]);

    av1_cdef_filter_fb(NULL, tmp_dst, CDEF_BSTRIDE, in,
                       cdef_search_ctx->xdec[pli], cdef_search_ctx->ydec[pli],
                       dir, dirinit, var, pli, dlist, cdef_count, pri_strength,
                       sec_strength + (sec_strength == 3),
                       cdef_search_ctx->damping, coeff_shift);
    curr_sse = cdef_search_ctx->compute_cdef_dist_fn(
        ref_buffer, ref_stride, tmp_dst, dlist, cdef_count,
        cdef_search_ctx->bsize[pli], coeff_shift, row, col);
  }
  return curr_sse;
}

// Calculates MSE at block level.
// Inputs:
//   cdef_search_ctx: Pointer to the structure containing parameters related to
//   CDEF search context.
//   fbr: Row index in units of 64x64 block
//   fbc: Column index in units of 64x64 block
// Returns:
//   Nothing will be returned. Contents of cdef_search_ctx will be modified.
void av1_cdef_mse_calc_block(CdefSearchCtx *cdef_search_ctx,
                             struct aom_internal_error_info *error_info,
                             int fbr, int fbc, int sb_count) {
  // TODO(aomedia:3276): Pass error_info to the low-level functions as required
  // in future to handle error propagation.
  (void)error_info;
  const CommonModeInfoParams *const mi_params = cdef_search_ctx->mi_params;
  const YV12_BUFFER_CONFIG *ref = cdef_search_ctx->ref;
  const int coeff_shift = cdef_search_ctx->coeff_shift;
  const int *mi_wide_l2 = cdef_search_ctx->mi_wide_l2;
  const int *mi_high_l2 = cdef_search_ctx->mi_high_l2;

  // Declare and initialize the temporary buffers.
  DECLARE_ALIGNED(32, uint16_t, inbuf[CDEF_INBUF_SIZE]);
  cdef_list dlist[MI_SIZE_128X128 * MI_SIZE_128X128];
  int dir[CDEF_NBLOCKS][CDEF_NBLOCKS] = { { 0 } };
  int var[CDEF_NBLOCKS][CDEF_NBLOCKS] = { { 0 } };
  uint16_t *const in = inbuf + CDEF_VBORDER * CDEF_BSTRIDE + CDEF_HBORDER;
  int nhb = AOMMIN(MI_SIZE_64X64, mi_params->mi_cols - MI_SIZE_64X64 * fbc);
  int nvb = AOMMIN(MI_SIZE_64X64, mi_params->mi_rows - MI_SIZE_64X64 * fbr);
  int hb_step = 1, vb_step = 1;
  BLOCK_SIZE bs;

  const MB_MODE_INFO *const mbmi =
      mi_params->mi_grid_base[MI_SIZE_64X64 * fbr * mi_params->mi_stride +
                              MI_SIZE_64X64 * fbc];

  uint8_t *ref_buffer[MAX_MB_PLANE] = { ref->y_buffer, ref->u_buffer,
                                        ref->v_buffer };
  int ref_stride[MAX_MB_PLANE] = { ref->y_stride, ref->uv_stride,
                                   ref->uv_stride };

  if (mbmi->bsize == BLOCK_128X128 || mbmi->bsize == BLOCK_128X64 ||
      mbmi->bsize == BLOCK_64X128) {
    bs = mbmi->bsize;
    if (bs == BLOCK_128X128 || bs == BLOCK_128X64) {
      nhb = AOMMIN(MI_SIZE_128X128, mi_params->mi_cols - MI_SIZE_64X64 * fbc);
      hb_step = 2;
    }
    if (bs == BLOCK_128X128 || bs == BLOCK_64X128) {
      nvb = AOMMIN(MI_SIZE_128X128, mi_params->mi_rows - MI_SIZE_64X64 * fbr);
      vb_step = 2;
    }
  } else {
    bs = BLOCK_64X64;
  }
  // Get number of 8x8 blocks which are not skip. Cdef processing happens for
  // 8x8 blocks which are not skip.
  const int cdef_count = av1_cdef_compute_sb_list(
      mi_params, fbr * MI_SIZE_64X64, fbc * MI_SIZE_64X64, dlist, bs);
  const bool is_fb_on_frm_left_boundary = (fbc == 0);
  const bool is_fb_on_frm_right_boundary =
      (fbc + hb_step == cdef_search_ctx->nhfb);
  const bool is_fb_on_frm_top_boundary = (fbr == 0);
  const bool is_fb_on_frm_bottom_boundary =
      (fbr + vb_step == cdef_search_ctx->nvfb);
  const int yoff = CDEF_VBORDER * (!is_fb_on_frm_top_boundary);
  const int xoff = CDEF_HBORDER * (!is_fb_on_frm_left_boundary);
  int dirinit = 0;
  for (int pli = 0; pli < cdef_search_ctx->num_planes; pli++) {
    /* We avoid filtering the pixels for which some of the pixels to
    average are outside the frame. We could change the filter instead,
    but it would add special cases for any future vectorization. */
    const int hfilt_size = (nhb << mi_wide_l2[pli]);
    const int vfilt_size = (nvb << mi_high_l2[pli]);
    const int ysize =
        vfilt_size + CDEF_VBORDER * (!is_fb_on_frm_bottom_boundary) + yoff;
    const int xsize =
        hfilt_size + CDEF_HBORDER * (!is_fb_on_frm_right_boundary) + xoff;
    const int row = fbr * MI_SIZE_64X64 << mi_high_l2[pli];
    const int col = fbc * MI_SIZE_64X64 << mi_wide_l2[pli];
    struct macroblockd_plane pd = cdef_search_ctx->plane[pli];
    cdef_search_ctx->copy_fn(&in[(-yoff * CDEF_BSTRIDE - xoff)], CDEF_BSTRIDE,
                             pd.dst.buf, row - yoff, col - xoff, pd.dst.stride,
                             ysize, xsize);
    fill_borders_for_fbs_on_frame_boundary(
        inbuf, hfilt_size, vfilt_size, is_fb_on_frm_left_boundary,
        is_fb_on_frm_right_boundary, is_fb_on_frm_top_boundary,
        is_fb_on_frm_bottom_boundary);
    for (int gi = 0; gi < cdef_search_ctx->total_strengths; gi++) {
      int pri_strength, sec_strength;
      get_cdef_filter_strengths(cdef_search_ctx->pick_method, &pri_strength,
                                &sec_strength, gi);
      const uint64_t curr_mse = get_filt_error(
          cdef_search_ctx, &pd, dlist, dir, &dirinit, var, in, ref_buffer[pli],
          ref_stride[pli], row, col, pri_strength, sec_strength, cdef_count,
          pli, coeff_shift, bs);
      if (pli < 2)
        cdef_search_ctx->mse[pli][sb_count][gi] = curr_mse;
      else
        cdef_search_ctx->mse[1][sb_count][gi] += curr_mse;
    }
  }
  cdef_search_ctx->sb_index[sb_count] =
      MI_SIZE_64X64 * fbr * mi_params->mi_stride + MI_SIZE_64X64 * fbc;
}

// MSE calculation at frame level.
// Inputs:
//   cdef_search_ctx: Pointer to the structure containing parameters related to
//   CDEF search context.
// Returns:
//   Nothing will be returned. Contents of cdef_search_ctx will be modified.
static void cdef_mse_calc_frame(CdefSearchCtx *cdef_search_ctx,
                                struct aom_internal_error_info *error_info) {
  // Loop over each sb.
  for (int fbr = 0; fbr < cdef_search_ctx->nvfb; ++fbr) {
    for (int fbc = 0; fbc < cdef_search_ctx->nhfb; ++fbc) {
      // Checks if cdef processing can be skipped for particular sb.
      if (cdef_sb_skip(cdef_search_ctx->mi_params, fbr, fbc)) continue;
      // Calculate mse for each sb and store the relevant sb index.
      av1_cdef_mse_calc_block(cdef_search_ctx, error_info, fbr, fbc,
                              cdef_search_ctx->sb_count);
      cdef_search_ctx->sb_count++;
    }
  }
}

// Allocates memory for members of CdefSearchCtx.
// Inputs:
//   cdef_search_ctx: Pointer to the structure containing parameters
//   related to CDEF search context.
// Returns:
//   Nothing will be returned. Contents of cdef_search_ctx will be modified.
static void cdef_alloc_data(AV1_COMMON *cm, CdefSearchCtx *cdef_search_ctx) {
  const int nvfb = cdef_search_ctx->nvfb;
  const int nhfb = cdef_search_ctx->nhfb;
  CHECK_MEM_ERROR(
      cm, cdef_search_ctx->sb_index,
      aom_malloc(nvfb * nhfb * sizeof(cdef_search_ctx->sb_index[0])));
  cdef_search_ctx->sb_count = 0;
  CHECK_MEM_ERROR(cm, cdef_search_ctx->mse[0],
                  aom_malloc(sizeof(**cdef_search_ctx->mse) * nvfb * nhfb));
  CHECK_MEM_ERROR(cm, cdef_search_ctx->mse[1],
                  aom_malloc(sizeof(**cdef_search_ctx->mse) * nvfb * nhfb));
}

// Deallocates the memory allocated for members of CdefSearchCtx.
// Inputs:
//   cdef_search_ctx: Pointer to the structure containing parameters
//   related to CDEF search context.
// Returns:
//   Nothing will be returned.
void av1_cdef_dealloc_data(CdefSearchCtx *cdef_search_ctx) {
  if (cdef_search_ctx) {
    aom_free(cdef_search_ctx->mse[0]);
    cdef_search_ctx->mse[0] = NULL;
    aom_free(cdef_search_ctx->mse[1]);
    cdef_search_ctx->mse[1] = NULL;
    aom_free(cdef_search_ctx->sb_index);
    cdef_search_ctx->sb_index = NULL;
  }
}

// Initialize the parameters related to CDEF search context.
// Inputs:
//   frame: Pointer to compressed frame buffer
//   ref: Pointer to the frame buffer holding the source frame
//   cm: Pointer to top level common structure
//   xd: Pointer to common current coding block structure
//   cdef_search_ctx: Pointer to the structure containing parameters related to
//   CDEF search context.
//   pick_method: Search method used to select CDEF parameters
// Returns:
//   Nothing will be returned. Contents of cdef_search_ctx will be modified.
static inline void cdef_params_init(const YV12_BUFFER_CONFIG *frame,
                                    const YV12_BUFFER_CONFIG *ref,
                                    AV1_COMMON *cm, MACROBLOCKD *xd,
                                    CdefSearchCtx *cdef_search_ctx,
                                    CDEF_PICK_METHOD pick_method) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int num_planes = av1_num_planes(cm);
  cdef_search_ctx->mi_params = &cm->mi_params;
  cdef_search_ctx->ref = ref;
  cdef_search_ctx->nvfb =
      (mi_params->mi_rows + MI_SIZE_64X64 - 1) / MI_SIZE_64X64;
  cdef_search_ctx->nhfb =
      (mi_params->mi_cols + MI_SIZE_64X64 - 1) / MI_SIZE_64X64;
  cdef_search_ctx->coeff_shift = AOMMAX(cm->seq_params->bit_depth - 8, 0);
  cdef_search_ctx->damping = 3 + (cm->quant_params.base_qindex >> 6);
  cdef_search_ctx->total_strengths = nb_cdef_strengths[pick_method];
  cdef_search_ctx->num_planes = num_planes;
  cdef_search_ctx->pick_method = pick_method;
  cdef_search_ctx->sb_count = 0;
  cdef_search_ctx->use_highbitdepth = cm->seq_params->use_highbitdepth;
  av1_setup_dst_planes(xd->plane, cm->seq_params->sb_size, frame, 0, 0, 0,
                       num_planes);
  // Initialize plane wise information.
  for (int pli = 0; pli < num_planes; pli++) {
    cdef_search_ctx->xdec[pli] = xd->plane[pli].subsampling_x;
    cdef_search_ctx->ydec[pli] = xd->plane[pli].subsampling_y;
    cdef_search_ctx->bsize[pli] =
        cdef_search_ctx->ydec[pli]
            ? (cdef_search_ctx->xdec[pli] ? BLOCK_4X4 : BLOCK_8X4)
            : (cdef_search_ctx->xdec[pli] ? BLOCK_4X8 : BLOCK_8X8);
    cdef_search_ctx->mi_wide_l2[pli] =
        MI_SIZE_LOG2 - xd->plane[pli].subsampling_x;
    cdef_search_ctx->mi_high_l2[pli] =
        MI_SIZE_LOG2 - xd->plane[pli].subsampling_y;
    cdef_search_ctx->plane[pli] = xd->plane[pli];
  }
  // Function pointer initialization.
#if CONFIG_AV1_HIGHBITDEPTH
  if (cm->seq_params->use_highbitdepth) {
    cdef_search_ctx->copy_fn = av1_cdef_copy_sb8_16_highbd;
    cdef_search_ctx->compute_cdef_dist_fn = compute_cdef_dist_highbd;
  } else {
    cdef_search_ctx->copy_fn = av1_cdef_copy_sb8_16_lowbd;
    cdef_search_ctx->compute_cdef_dist_fn = compute_cdef_dist;
  }
#else
  cdef_search_ctx->copy_fn = av1_cdef_copy_sb8_16_lowbd;
  cdef_search_ctx->compute_cdef_dist_fn = compute_cdef_dist;
#endif
}

void av1_pick_cdef_from_qp(AV1_COMMON *const cm, int skip_cdef,
                           int is_screen_content) {
  const int bd = cm->seq_params->bit_depth;
  const int q =
      av1_ac_quant_QTX(cm->quant_params.base_qindex, 0, bd) >> (bd - 8);
  CdefInfo *const cdef_info = &cm->cdef_info;
  // Check the speed feature to avoid extra signaling.
  if (skip_cdef) {
    cdef_info->cdef_bits = 1;
    cdef_info->nb_cdef_strengths = 2;
  } else {
    cdef_info->cdef_bits = 0;
    cdef_info->nb_cdef_strengths = 1;
  }
  cdef_info->cdef_damping = 3 + (cm->quant_params.base_qindex >> 6);

  int predicted_y_f1 = 0;
  int predicted_y_f2 = 0;
  int predicted_uv_f1 = 0;
  int predicted_uv_f2 = 0;
  if (is_screen_content) {
    predicted_y_f1 =
        (int)(5.88217781e-06 * q * q + 6.10391455e-03 * q + 9.95043102e-02);
    predicted_y_f2 =
        (int)(-7.79934857e-06 * q * q + 6.58957830e-03 * q + 8.81045025e-01);
    predicted_uv_f1 =
        (int)(-6.79500136e-06 * q * q + 1.02695586e-02 * q + 1.36126802e-01);
    predicted_uv_f2 =
        (int)(-9.99613695e-08 * q * q - 1.79361339e-05 * q + 1.17022324e+0);
    predicted_y_f1 = clamp(predicted_y_f1, 0, 15);
    predicted_y_f2 = clamp(predicted_y_f2, 0, 3);
    predicted_uv_f1 = clamp(predicted_uv_f1, 0, 15);
    predicted_uv_f2 = clamp(predicted_uv_f2, 0, 3);
  } else {
    if (!frame_is_intra_only(cm)) {
      predicted_y_f1 = clamp((int)roundf(q * q * -0.0000023593946f +
                                         q * 0.0068615186f + 0.02709886f),
                             0, 15);
      predicted_y_f2 = clamp((int)roundf(q * q * -0.00000057629734f +
                                         q * 0.0013993345f + 0.03831067f),
                             0, 3);
      predicted_uv_f1 = clamp((int)roundf(q * q * -0.0000007095069f +
                                          q * 0.0034628846f + 0.00887099f),
                              0, 15);
      predicted_uv_f2 = clamp((int)roundf(q * q * 0.00000023874085f +
                                          q * 0.00028223585f + 0.05576307f),
                              0, 3);
    } else {
      predicted_y_f1 = clamp(
          (int)roundf(q * q * 0.0000033731974f + q * 0.008070594f + 0.0187634f),
          0, 15);
      predicted_y_f2 = clamp((int)roundf(q * q * 0.0000029167343f +
                                         q * 0.0027798624f + 0.0079405f),
                             0, 3);
      predicted_uv_f1 = clamp((int)roundf(q * q * -0.0000130790995f +
                                          q * 0.012892405f - 0.00748388f),
                              0, 15);
      predicted_uv_f2 = clamp((int)roundf(q * q * 0.0000032651783f +
                                          q * 0.00035520183f + 0.00228092f),
                              0, 3);
    }
  }
  cdef_info->cdef_strengths[0] =
      predicted_y_f1 * CDEF_SEC_STRENGTHS + predicted_y_f2;
  cdef_info->cdef_uv_strengths[0] =
      predicted_uv_f1 * CDEF_SEC_STRENGTHS + predicted_uv_f2;

  // mbmi->cdef_strength is already set in the encoding stage. We don't need to
  // set it again here.
  if (skip_cdef) {
    cdef_info->cdef_strengths[1] = 0;
    cdef_info->cdef_uv_strengths[1] = 0;
    return;
  }

  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int nvfb = (mi_params->mi_rows + MI_SIZE_64X64 - 1) / MI_SIZE_64X64;
  const int nhfb = (mi_params->mi_cols + MI_SIZE_64X64 - 1) / MI_SIZE_64X64;
  MB_MODE_INFO **mbmi = mi_params->mi_grid_base;
  // mbmi is NULL when real-time rate control library is used.
  if (!mbmi) return;
  for (int r = 0; r < nvfb; ++r) {
    for (int c = 0; c < nhfb; ++c) {
      MB_MODE_INFO *current_mbmi = mbmi[MI_SIZE_64X64 * c];
      current_mbmi->cdef_strength = 0;
    }
    mbmi += MI_SIZE_64X64 * mi_params->mi_stride;
  }
}

void av1_cdef_search(AV1_COMP *cpi) {
  AV1_COMMON *cm = &cpi->common;
  CDEF_CONTROL cdef_control = cpi->oxcf.tool_cfg.cdef_control;

  assert(cdef_control != CDEF_NONE);
  // For CDEF_ADAPTIVE, turning off CDEF around qindex 32 was best for still
  // pictures
  if ((cdef_control == CDEF_REFERENCE &&
       cpi->ppi->rtc_ref.non_reference_frame) ||
      (cdef_control == CDEF_ADAPTIVE && cpi->oxcf.mode == ALLINTRA &&
       (cpi->oxcf.rc_cfg.mode == AOM_Q || cpi->oxcf.rc_cfg.mode == AOM_CQ) &&
       cpi->oxcf.rc_cfg.cq_level <= 32)) {
    CdefInfo *const cdef_info = &cm->cdef_info;
    cdef_info->nb_cdef_strengths = 1;
    cdef_info->cdef_bits = 0;
    cdef_info->cdef_strengths[0] = 0;
    cdef_info->cdef_uv_strengths[0] = 0;
    return;
  }

  // Indicate if external RC is used for testing
  const int rtc_ext_rc = cpi->rc.rtc_external_ratectrl;
  if (rtc_ext_rc) {
    av1_pick_cdef_from_qp(cm, 0, 0);
    return;
  }
  CDEF_PICK_METHOD pick_method = cpi->sf.lpf_sf.cdef_pick_method;
  if (pick_method == CDEF_PICK_FROM_Q) {
    const int use_screen_content_model =
        cm->quant_params.base_qindex >
            AOMMAX(cpi->sf.rt_sf.screen_content_cdef_filter_qindex_thresh,
                   cpi->rc.best_quality + 5) &&
        cpi->oxcf.tune_cfg.content == AOM_CONTENT_SCREEN;
    av1_pick_cdef_from_qp(cm, cpi->sf.rt_sf.skip_cdef_sb,
                          use_screen_content_model);
    return;
  }
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int damping = 3 + (cm->quant_params.base_qindex >> 6);
  const int fast = (pick_method >= CDEF_FAST_SEARCH_LVL1 &&
                    pick_method <= CDEF_FAST_SEARCH_LVL5);
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *xd = &cpi->td.mb.e_mbd;

  if (!cpi->cdef_search_ctx)
    CHECK_MEM_ERROR(cm, cpi->cdef_search_ctx,
                    aom_malloc(sizeof(*cpi->cdef_search_ctx)));
  CdefSearchCtx *cdef_search_ctx = cpi->cdef_search_ctx;

  // Initialize parameters related to CDEF search context.
  cdef_params_init(&cm->cur_frame->buf, cpi->source, cm, xd, cdef_search_ctx,
                   pick_method);
  // Allocate CDEF search context buffers.
  cdef_alloc_data(cm, cdef_search_ctx);
  // Frame level mse calculation.
  if (cpi->mt_info.num_workers > 1) {
    av1_cdef_mse_calc_frame_mt(cpi);
  } else {
    cdef_mse_calc_frame(cdef_search_ctx, cm->error);
  }

  /* Search for different number of signaling bits. */
  int nb_strength_bits = 0;
  uint64_t best_rd = UINT64_MAX;
  CdefInfo *const cdef_info = &cm->cdef_info;
  int sb_count = cdef_search_ctx->sb_count;
  uint64_t(*mse[2])[TOTAL_STRENGTHS];
  mse[0] = cdef_search_ctx->mse[0];
  mse[1] = cdef_search_ctx->mse[1];
  /* Calculate the maximum number of bits required to signal CDEF strengths at
   * block level */
  const int total_strengths = nb_cdef_strengths[pick_method];
  const int joint_strengths =
      num_planes > 1 ? total_strengths * total_strengths : total_strengths;
  const int max_signaling_bits =
      joint_strengths == 1 ? 0 : get_msb(joint_strengths - 1) + 1;
  int rdmult = cpi->td.mb.rdmult;
  for (int i = 0; i <= 3; i++) {
    if (i > max_signaling_bits) break;
    int best_lev0[CDEF_MAX_STRENGTHS] = { 0 };
    int best_lev1[CDEF_MAX_STRENGTHS] = { 0 };
    const int nb_strengths = 1 << i;
    uint64_t tot_mse;
    if (num_planes > 1) {
      tot_mse = joint_strength_search_dual(best_lev0, best_lev1, nb_strengths,
                                           mse, sb_count, pick_method);
    } else {
      tot_mse = joint_strength_search(best_lev0, nb_strengths, mse[0], sb_count,
                                      pick_method);
    }

    const int total_bits = sb_count * i + nb_strengths * CDEF_STRENGTH_BITS *
                                              (num_planes > 1 ? 2 : 1);
    const int rate_cost = av1_cost_literal(total_bits);
    const uint64_t dist = tot_mse * 16;
    const uint64_t rd = RDCOST(rdmult, rate_cost, dist);
    if (rd < best_rd) {
      best_rd = rd;
      nb_strength_bits = i;
      memcpy(cdef_info->cdef_strengths, best_lev0,
             nb_strengths * sizeof(best_lev0[0]));
      if (num_planes > 1) {
        memcpy(cdef_info->cdef_uv_strengths, best_lev1,
               nb_strengths * sizeof(best_lev1[0]));
      }
    }
  }

  cdef_info->cdef_bits = nb_strength_bits;
  cdef_info->nb_cdef_strengths = 1 << nb_strength_bits;
  for (int i = 0; i < sb_count; i++) {
    uint64_t best_mse = UINT64_MAX;
    int best_gi = 0;
    for (int gi = 0; gi < cdef_info->nb_cdef_strengths; gi++) {
      uint64_t curr = mse[0][i][cdef_info->cdef_strengths[gi]];
      if (num_planes > 1) curr += mse[1][i][cdef_info->cdef_uv_strengths[gi]];
      if (curr < best_mse) {
        best_gi = gi;
        best_mse = curr;
      }
    }
    mi_params->mi_grid_base[cdef_search_ctx->sb_index[i]]->cdef_strength =
        best_gi;
  }
  if (fast) {
    for (int j = 0; j < cdef_info->nb_cdef_strengths; j++) {
      const int luma_strength = cdef_info->cdef_strengths[j];
      const int chroma_strength = cdef_info->cdef_uv_strengths[j];
      int pri_strength, sec_strength;

      STORE_CDEF_FILTER_STRENGTH(cdef_info->cdef_strengths[j], pick_method,
                                 luma_strength);
      STORE_CDEF_FILTER_STRENGTH(cdef_info->cdef_uv_strengths[j], pick_method,
                                 chroma_strength);
    }
  }

  // For CDEF_ADAPTIVE, set primary and secondary CDEF at reduced strength for
  // qindexes 33 through 220.
  // Note 1: for odd strengths, the 0.5 discarded by ">> 1" is a significant
  // part of the strength when the strength is small, and because there are
  // few strength levels, odd strengths are reduced significantly more than a
  // half. This is intended behavior for reduced strength.
  // For example: a pri strength of 3 becomes 1, and a sec strength of 1
  // becomes 0.
  // Note 2: a (signaled) sec strength value of 3 is special as it results in an
  // actual sec strength of 4. We tried adding +1 to the sec strength 3 so it
  // maps to a reduced sec strength of 2. However, on Daala's subset1, the
  // resulting SSIMULACRA 2 scores were either exactly the same (at cpu-used 6),
  // or within noise level (at cpu-used 3). Given that there were no discernible
  // improvements, this special mapping was left out for reduced strength.
  if (cdef_control == CDEF_ADAPTIVE && cpi->oxcf.mode == ALLINTRA &&
      (cpi->oxcf.rc_cfg.mode == AOM_Q || cpi->oxcf.rc_cfg.mode == AOM_CQ) &&
      cpi->oxcf.rc_cfg.cq_level <= 220) {
    for (int j = 0; j < cdef_info->nb_cdef_strengths; j++) {
      const int luma_strength = cdef_info->cdef_strengths[j];
      const int chroma_strength = cdef_info->cdef_uv_strengths[j];

      const int new_pri_luma_strength =
          (luma_strength / CDEF_SEC_STRENGTHS) >> 1;
      const int new_sec_luma_strength =
          (luma_strength % CDEF_SEC_STRENGTHS) >> 1;
      const int new_pri_chroma_strength =
          (chroma_strength / CDEF_SEC_STRENGTHS) >> 1;
      const int new_sec_chroma_strength =
          (chroma_strength % CDEF_SEC_STRENGTHS) >> 1;

      cdef_info->cdef_strengths[j] =
          new_pri_luma_strength * CDEF_SEC_STRENGTHS + new_sec_luma_strength;
      cdef_info->cdef_uv_strengths[j] =
          new_pri_chroma_strength * CDEF_SEC_STRENGTHS +
          new_sec_chroma_strength;
    }
  }

  cdef_info->cdef_damping = damping;
  // Deallocate CDEF search context buffers.
  av1_cdef_dealloc_data(cdef_search_ctx);
}
