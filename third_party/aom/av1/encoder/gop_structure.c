/*
 * Copyright (c) 2019, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <stdint.h>

#include "av1/common/blockd.h"
#include "config/aom_config.h"
#include "config/aom_scale_rtcd.h"

#include "aom/aom_codec.h"
#include "aom/aom_encoder.h"

#include "av1/common/av1_common_int.h"

#include "av1/encoder/encoder.h"
#include "av1/encoder/firstpass.h"
#include "av1/encoder/gop_structure.h"
#include "av1/encoder/pass2_strategy.h"

// This function sets gf_group->frame_parallel_level for LF_UPDATE frames based
// on the value of parallel_frame_count.
static void set_frame_parallel_level(int *frame_parallel_level,
                                     int *parallel_frame_count,
                                     int max_parallel_frames) {
  assert(*parallel_frame_count > 0);
  // parallel_frame_count > 1 indicates subsequent frame(s) in the current
  // parallel encode set.
  *frame_parallel_level = 1 + (*parallel_frame_count > 1);
  // Update the count of no. of parallel frames.
  (*parallel_frame_count)++;
  if (*parallel_frame_count > max_parallel_frames) *parallel_frame_count = 1;
}

// This function sets gf_group->src_offset based on frame_parallel_level.
// Outputs are gf_group->src_offset and first_frame_index
static void set_src_offset(GF_GROUP *const gf_group, int *first_frame_index,
                           int cur_frame_idx, int frame_ind) {
  if (gf_group->frame_parallel_level[frame_ind] > 0) {
    if (gf_group->frame_parallel_level[frame_ind] == 1) {
      *first_frame_index = cur_frame_idx;
    }

    // Obtain the offset of the frame at frame_ind in the lookahead queue by
    // subtracting the display order hints of the current frame from the display
    // order hint of the first frame in parallel encoding set (at
    // first_frame_index).
    gf_group->src_offset[frame_ind] =
        (cur_frame_idx + gf_group->arf_src_offset[frame_ind]) -
        *first_frame_index;
  }
}

// Sets the GF_GROUP params for LF_UPDATE frames.
static inline void set_params_for_leaf_frames(
    const TWO_PASS *twopass, const TWO_PASS_FRAME *twopass_frame,
    const PRIMARY_RATE_CONTROL *p_rc, FRAME_INFO *frame_info,
    GF_GROUP *const gf_group, int *cur_frame_idx, int *frame_ind,
    int *parallel_frame_count, int max_parallel_frames,
    int do_frame_parallel_encode, int *first_frame_index, int *cur_disp_index,
    int layer_depth, int start, int end) {
  gf_group->update_type[*frame_ind] = LF_UPDATE;
  gf_group->arf_src_offset[*frame_ind] = 0;
  gf_group->cur_frame_idx[*frame_ind] = *cur_frame_idx;
  gf_group->layer_depth[*frame_ind] = MAX_ARF_LAYERS;
  gf_group->frame_type[*frame_ind] = INTER_FRAME;
  gf_group->refbuf_state[*frame_ind] = REFBUF_UPDATE;
  gf_group->max_layer_depth = AOMMAX(gf_group->max_layer_depth, layer_depth);
  gf_group->display_idx[*frame_ind] = (*cur_disp_index);
  gf_group->arf_boost[*frame_ind] =
      av1_calc_arf_boost(twopass, twopass_frame, p_rc, frame_info, start,
                         end - start, 0, NULL, NULL, 0);
  ++(*cur_disp_index);

  // Set the level of parallelism for the LF_UPDATE frame.
  if (do_frame_parallel_encode) {
    set_frame_parallel_level(&gf_group->frame_parallel_level[*frame_ind],
                             parallel_frame_count, max_parallel_frames);
    // Set LF_UPDATE frames as non-reference frames.
    gf_group->is_frame_non_ref[*frame_ind] = true;
  }
  set_src_offset(gf_group, first_frame_index, *cur_frame_idx, *frame_ind);

  ++(*frame_ind);
  ++(*cur_frame_idx);
}

// Sets the GF_GROUP params for INTNL_OVERLAY_UPDATE frames.
static inline void set_params_for_intnl_overlay_frames(
    GF_GROUP *const gf_group, int *cur_frame_idx, int *frame_ind,
    int *first_frame_index, int *cur_disp_index, int layer_depth) {
  gf_group->update_type[*frame_ind] = INTNL_OVERLAY_UPDATE;
  gf_group->arf_src_offset[*frame_ind] = 0;
  gf_group->cur_frame_idx[*frame_ind] = *cur_frame_idx;
  gf_group->layer_depth[*frame_ind] = layer_depth;
  gf_group->frame_type[*frame_ind] = INTER_FRAME;
  gf_group->refbuf_state[*frame_ind] = REFBUF_UPDATE;
  gf_group->display_idx[*frame_ind] = (*cur_disp_index);
  ++(*cur_disp_index);

  set_src_offset(gf_group, first_frame_index, *cur_frame_idx, *frame_ind);
  ++(*frame_ind);
  ++(*cur_frame_idx);
}

// Sets the GF_GROUP params for INTNL_ARF_UPDATE frames.
static inline void set_params_for_internal_arfs(
    const TWO_PASS *twopass, const TWO_PASS_FRAME *twopass_frame,
    const PRIMARY_RATE_CONTROL *p_rc, FRAME_INFO *frame_info,
    GF_GROUP *const gf_group, int *cur_frame_idx, int *frame_ind,
    int *parallel_frame_count, int max_parallel_frames,
    int do_frame_parallel_encode, int *first_frame_index, int depth_thr,
    int *cur_disp_idx, int layer_depth, int arf_src_offset, int offset,
    int f_frames, int b_frames) {
  gf_group->update_type[*frame_ind] = INTNL_ARF_UPDATE;
  gf_group->arf_src_offset[*frame_ind] = arf_src_offset;
  gf_group->cur_frame_idx[*frame_ind] = *cur_frame_idx;
  gf_group->layer_depth[*frame_ind] = layer_depth;
  gf_group->frame_type[*frame_ind] = INTER_FRAME;
  gf_group->refbuf_state[*frame_ind] = REFBUF_UPDATE;
  gf_group->display_idx[*frame_ind] =
      (*cur_disp_idx) + gf_group->arf_src_offset[*frame_ind];
  gf_group->arf_boost[*frame_ind] =
      av1_calc_arf_boost(twopass, twopass_frame, p_rc, frame_info, offset,
                         f_frames, b_frames, NULL, NULL, 0);

  if (do_frame_parallel_encode) {
    if (depth_thr != INT_MAX) {
      assert(depth_thr == 3 || depth_thr == 4);
      assert(IMPLIES(depth_thr == 3, layer_depth == 4));
      assert(IMPLIES(depth_thr == 4, layer_depth == 5));
      // Set frame_parallel_level of the first frame in the given layer to 1.
      if (gf_group->layer_depth[(*frame_ind) - 1] != layer_depth) {
        gf_group->frame_parallel_level[*frame_ind] = 1;
      } else {
        // Set frame_parallel_level of the consecutive frame in the same given
        // layer to 2.
        assert(gf_group->frame_parallel_level[(*frame_ind) - 1] == 1);
        gf_group->frame_parallel_level[*frame_ind] = 2;
        // Store the display order hints of the past 2 INTNL_ARF_UPDATE
        // frames which would not have been displayed at the time of the encode
        // of current frame.
        gf_group->skip_frame_refresh[*frame_ind][0] =
            gf_group->display_idx[(*frame_ind) - 1];
        gf_group->skip_frame_refresh[*frame_ind][1] =
            gf_group->display_idx[(*frame_ind) - 2];
        // Set the display_idx of frame_parallel_level 1 frame in
        // gf_group->skip_frame_as_ref.
        gf_group->skip_frame_as_ref[*frame_ind] =
            gf_group->display_idx[(*frame_ind) - 1];
      }
    }
    // If max_parallel_frames is not exceeded and if the frame will not be
    // temporally filtered, encode the next internal ARF frame in parallel.
    if (*parallel_frame_count > 1 &&
        *parallel_frame_count <= max_parallel_frames) {
      if (gf_group->arf_src_offset[*frame_ind] < TF_LOOKAHEAD_IDX_THR)
        gf_group->frame_parallel_level[*frame_ind] = 2;
      *parallel_frame_count = 1;
    }
  }
  set_src_offset(gf_group, first_frame_index, *cur_frame_idx, *frame_ind);
  ++(*frame_ind);
}

// Set parameters for frames between 'start' and 'end' (excluding both).
static void set_multi_layer_params_for_fp(
    const TWO_PASS *twopass, const TWO_PASS_FRAME *twopass_frame,
    GF_GROUP *const gf_group, const PRIMARY_RATE_CONTROL *p_rc,
    RATE_CONTROL *rc, FRAME_INFO *frame_info, int start, int end,
    int *cur_frame_idx, int *frame_ind, int *parallel_frame_count,
    int max_parallel_frames, int do_frame_parallel_encode,
    int *first_frame_index, int depth_thr, int *cur_disp_idx, int layer_depth) {
  const int num_frames_to_process = end - start;

  // Either we are at the last level of the pyramid, or we don't have enough
  // frames between 'l' and 'r' to create one more level.
  if (layer_depth > gf_group->max_layer_depth_allowed ||
      num_frames_to_process < 3) {
    // Leaf nodes.
    while (start < end) {
      set_params_for_leaf_frames(twopass, twopass_frame, p_rc, frame_info,
                                 gf_group, cur_frame_idx, frame_ind,
                                 parallel_frame_count, max_parallel_frames,
                                 do_frame_parallel_encode, first_frame_index,
                                 cur_disp_idx, layer_depth, start, end);
      ++start;
    }
  } else {
    const int m = (start + end - 1) / 2;

    // Internal ARF.
    int arf_src_offset = m - start;
    set_params_for_internal_arfs(
        twopass, twopass_frame, p_rc, frame_info, gf_group, cur_frame_idx,
        frame_ind, parallel_frame_count, max_parallel_frames,
        do_frame_parallel_encode, first_frame_index, INT_MAX, cur_disp_idx,
        layer_depth, arf_src_offset, m, end - m, m - start);

    // If encode reordering is enabled, configure the multi-layers accordingly
    // and return. For e.g., the encode order for gf-interval 16 after
    // reordering would be 0-> 16-> 8-> 4-> 2-> 6-> 1-> 3-> 5-> 7-> 12-> 10->
    // 14-> 9-> 11-> 13-> 15.
    if (layer_depth >= depth_thr) {
      int m1 = (m + start - 1) / 2;
      int m2 = (m + 1 + end) / 2;
      int arf_src_offsets[2] = { m1 - start, m2 - start };
      // Parameters to compute arf_boost.
      int offset[2] = { m1, m2 };
      int f_frames[2] = { m - m1, end - m2 };
      int b_frames[2] = { m1 - start, m2 - (m + 1) };

      // Set GF_GROUP params for INTNL_ARF_UPDATE frames which are reordered.
      for (int i = 0; i < 2; i++) {
        set_params_for_internal_arfs(
            twopass, twopass_frame, p_rc, frame_info, gf_group, cur_frame_idx,
            frame_ind, parallel_frame_count, max_parallel_frames,
            do_frame_parallel_encode, first_frame_index, depth_thr,
            cur_disp_idx, layer_depth + 1, arf_src_offsets[i], offset[i],
            f_frames[i], b_frames[i]);
      }

      // Initialize the start and end indices to configure LF_UPDATE frames.
      int start_idx[4] = { start, m1 + 1, m + 1, end - 1 };
      int end_idx[4] = { m1, m, m2, end };
      int layer_depth_for_intnl_overlay[4] = { layer_depth + 1, layer_depth,
                                               layer_depth + 1, INVALID_IDX };

      // Set GF_GROUP params for the rest of LF_UPDATE and INTNL_OVERLAY_UPDATE
      // frames after reordering.
      for (int i = 0; i < 4; i++) {
        set_multi_layer_params_for_fp(
            twopass, twopass_frame, gf_group, p_rc, rc, frame_info,
            start_idx[i], end_idx[i], cur_frame_idx, frame_ind,
            parallel_frame_count, max_parallel_frames, do_frame_parallel_encode,
            first_frame_index, depth_thr, cur_disp_idx, layer_depth + 2);
        if (layer_depth_for_intnl_overlay[i] != INVALID_IDX)
          set_params_for_intnl_overlay_frames(
              gf_group, cur_frame_idx, frame_ind, first_frame_index,
              cur_disp_idx, layer_depth_for_intnl_overlay[i]);
      }
      return;
    }

    // Frames displayed before this internal ARF.
    set_multi_layer_params_for_fp(
        twopass, twopass_frame, gf_group, p_rc, rc, frame_info, start, m,
        cur_frame_idx, frame_ind, parallel_frame_count, max_parallel_frames,
        do_frame_parallel_encode, first_frame_index, depth_thr, cur_disp_idx,
        layer_depth + 1);

    // Overlay for internal ARF.
    set_params_for_intnl_overlay_frames(gf_group, cur_frame_idx, frame_ind,
                                        first_frame_index, cur_disp_idx,
                                        layer_depth);

    // Frames displayed after this internal ARF.
    set_multi_layer_params_for_fp(
        twopass, twopass_frame, gf_group, p_rc, rc, frame_info, m + 1, end,
        cur_frame_idx, frame_ind, parallel_frame_count, max_parallel_frames,
        do_frame_parallel_encode, first_frame_index, depth_thr, cur_disp_idx,
        layer_depth + 1);
  }
}

// Structure for bookkeeping start, end and display indices to configure
// INTNL_ARF_UPDATE frames.
typedef struct {
  int start;
  int end;
  int display_index;
} FRAME_REORDER_INFO;

// Updates the stats required to configure the GF_GROUP.
static inline void fill_arf_frame_stats(FRAME_REORDER_INFO *arf_frame_stats,
                                        int arf_frame_index, int display_idx,
                                        int start, int end) {
  arf_frame_stats[arf_frame_index].start = start;
  arf_frame_stats[arf_frame_index].end = end;
  arf_frame_stats[arf_frame_index].display_index = display_idx;
}

// Sets GF_GROUP params for INTNL_ARF_UPDATE frames. Also populates
// doh_gf_index_map and arf_frame_stats.
static inline void set_params_for_internal_arfs_in_gf14(
    GF_GROUP *const gf_group, FRAME_REORDER_INFO *arf_frame_stats,
    int *cur_frame_idx, int *cur_disp_idx, int *frame_ind,
    int *count_arf_frames, int *doh_gf_index_map, int start, int end,
    int layer_depth, int layer_with_parallel_encodes) {
  int index = (start + end - 1) / 2;
  gf_group->update_type[*frame_ind] = INTNL_ARF_UPDATE;
  gf_group->arf_src_offset[*frame_ind] = index - 1;
  gf_group->cur_frame_idx[*frame_ind] = *cur_frame_idx;
  gf_group->layer_depth[*frame_ind] = layer_depth;
  gf_group->frame_type[*frame_ind] = INTER_FRAME;
  gf_group->refbuf_state[*frame_ind] = REFBUF_UPDATE;
  gf_group->display_idx[*frame_ind] =
      (*cur_disp_idx) + gf_group->arf_src_offset[*frame_ind];

  // Update the display index of the current frame with its gf index.
  doh_gf_index_map[index] = *frame_ind;
  if (layer_with_parallel_encodes) {
    assert(layer_depth == 4);
    // Set frame_parallel_level of the first frame in the given layer depth
    // to 1.
    if (gf_group->layer_depth[(*frame_ind) - 1] != layer_depth) {
      gf_group->frame_parallel_level[*frame_ind] = 1;
    } else {
      // Set frame_parallel_level of the consecutive frame in the same given
      // layer depth to 2.
      assert(gf_group->frame_parallel_level[(*frame_ind) - 1] == 1);
      gf_group->frame_parallel_level[*frame_ind] = 2;
      // Set the display_idx of frame_parallel_level 1 frame in
      // gf_group->skip_frame_as_ref.
      gf_group->skip_frame_as_ref[*frame_ind] =
          gf_group->display_idx[(*frame_ind) - 1];
    }
  }
  ++(*frame_ind);

  // Update arf_frame_stats.
  fill_arf_frame_stats(arf_frame_stats, *count_arf_frames, index, start, end);
  ++(*count_arf_frames);
}

// Sets GF_GROUP params for all INTNL_ARF_UPDATE frames in the given layer
// dpeth.
static inline void set_params_for_cur_layer_frames(
    GF_GROUP *const gf_group, FRAME_REORDER_INFO *arf_frame_stats,
    int *cur_frame_idx, int *cur_disp_idx, int *frame_ind,
    int *count_arf_frames, int *doh_gf_index_map, int num_dir, int node_start,
    int node_end, int layer_depth) {
  assert(num_dir < 3);
  int start, end;
  // Iterate through the nodes in the previous layer depth.
  for (int i = node_start; i < node_end; i++) {
    // For each node, check if a frame can be coded as INTNL_ARF_UPDATE frame on
    // either direction.
    for (int dir = 0; dir < num_dir; dir++) {
      // Checks for a frame to the left of current node.
      if (dir == 0) {
        start = arf_frame_stats[i].start;
        end = arf_frame_stats[i].display_index;
      } else {
        // Checks for a frame to the right of current node.
        start = arf_frame_stats[i].display_index + 1;
        end = arf_frame_stats[i].end;
      }
      const int num_frames_to_process = end - start;
      // Checks if a frame can be coded as INTNL_ARF_UPDATE frame. If
      // num_frames_to_process is less than 3, then there are not enough frames
      // between 'start' and 'end' to create another level.
      if (num_frames_to_process >= 3) {
        // Flag to indicate the lower layer depths for which parallel encoding
        // is enabled. Currently enabled for layer 4 frames.
        int layer_with_parallel_encodes = layer_depth == 4;
        set_params_for_internal_arfs_in_gf14(
            gf_group, arf_frame_stats, cur_frame_idx, cur_disp_idx, frame_ind,
            count_arf_frames, doh_gf_index_map, start, end, layer_depth,
            layer_with_parallel_encodes);
      }
    }
  }
}

// Configures multi-layers of the GF_GROUP when consecutive encode of frames in
// the same layer depth is enbaled.
static inline void set_multi_layer_params_for_gf14(
    const TWO_PASS *twopass, const TWO_PASS_FRAME *twopass_frame,
    const PRIMARY_RATE_CONTROL *p_rc, FRAME_INFO *frame_info,
    GF_GROUP *const gf_group, FRAME_REORDER_INFO *arf_frame_stats,
    int *cur_frame_idx, int *frame_ind, int *count_arf_frames,
    int *doh_gf_index_map, int *parallel_frame_count, int *first_frame_index,
    int *cur_disp_index, int gf_interval, int layer_depth,
    int max_parallel_frames) {
  assert(layer_depth == 2);
  assert(gf_group->max_layer_depth_allowed >= 4);
  int layer, node_start, node_end = 0;
  // Maximum layer depth excluding LF_UPDATE frames is 4 since applicable only
  // for gf-interval 14.
  const int max_layer_depth = 4;
  // Iterate through each layer depth starting from 2 till 'max_layer_depth'.
  for (layer = layer_depth; layer <= max_layer_depth; layer++) {
    // 'node_start' and 'node_end' indicate the number of nodes from the
    // previous layer depth to be considered. It also corresponds to the indices
    // of arf_frame_stats.
    node_start = node_end;
    node_end = (*count_arf_frames);
    // 'num_dir' indicates the number of directions to traverse w.r.t. a given
    // node in order to choose an INTNL_ARF_UPDATE frame. Layer depth 2 would
    // have only one frame and hence needs to traverse only in the left
    // direction w.r.t the node in the previous layer.
    int num_dir = layer == 2 ? 1 : 2;
    set_params_for_cur_layer_frames(gf_group, arf_frame_stats, cur_frame_idx,
                                    cur_disp_index, frame_ind, count_arf_frames,
                                    doh_gf_index_map, num_dir, node_start,
                                    node_end, layer);
  }

  for (int i = 1; i < gf_interval; i++) {
    // Since doh_gf_index_map is already populated for all INTNL_ARF_UPDATE
    // frames in the GF_GROUP, any frame with INVALID_IDX would correspond to an
    // LF_UPDATE frame.
    if (doh_gf_index_map[i] == INVALID_IDX) {
      // LF_UPDATE frames.
      // TODO(Remya): Correct start and end parameters passed to
      // set_params_for_leaf_frames() once encode reordering for gf-interval 14
      // is enbaled for parallel encode of lower layer frames.
      set_params_for_leaf_frames(
          twopass, twopass_frame, p_rc, frame_info, gf_group, cur_frame_idx,
          frame_ind, parallel_frame_count, max_parallel_frames, 1,
          first_frame_index, cur_disp_index, layer, 0, 0);
    } else {
      // In order to obtain the layer depths of INTNL_OVERLAY_UPDATE frames, get
      // the gf index of corresponding INTNL_ARF_UPDATE frames.
      int intnl_arf_index = doh_gf_index_map[i];
      int ld = gf_group->layer_depth[intnl_arf_index];
      set_params_for_intnl_overlay_frames(gf_group, cur_frame_idx, frame_ind,
                                          first_frame_index, cur_disp_index,
                                          ld);
    }
  }
}

// Set parameters for frames between 'start' and 'end' (excluding both).
static void set_multi_layer_params(
    const TWO_PASS *twopass, const TWO_PASS_FRAME *twopass_frame,
    GF_GROUP *const gf_group, const PRIMARY_RATE_CONTROL *p_rc,
    RATE_CONTROL *rc, FRAME_INFO *frame_info, int start, int end,
    int *cur_frame_idx, int *frame_ind, int *parallel_frame_count,
    int max_parallel_frames, int do_frame_parallel_encode,
    int *first_frame_index, int *cur_disp_idx, int layer_depth) {
  const int num_frames_to_process = end - start;

  // Either we are at the last level of the pyramid, or we don't have enough
  // frames between 'l' and 'r' to create one more level.
  if (layer_depth > gf_group->max_layer_depth_allowed ||
      num_frames_to_process < 3) {
    // Leaf nodes.
    while (start < end) {
      gf_group->update_type[*frame_ind] = LF_UPDATE;
      gf_group->arf_src_offset[*frame_ind] = 0;
      gf_group->cur_frame_idx[*frame_ind] = *cur_frame_idx;
      gf_group->display_idx[*frame_ind] = *cur_disp_idx;
      gf_group->layer_depth[*frame_ind] = MAX_ARF_LAYERS;
      gf_group->arf_boost[*frame_ind] =
          av1_calc_arf_boost(twopass, twopass_frame, p_rc, frame_info, start,
                             end - start, 0, NULL, NULL, 0);
      gf_group->frame_type[*frame_ind] = INTER_FRAME;
      gf_group->refbuf_state[*frame_ind] = REFBUF_UPDATE;
      gf_group->max_layer_depth =
          AOMMAX(gf_group->max_layer_depth, layer_depth);
      // Set the level of parallelism for the LF_UPDATE frame.
      if (do_frame_parallel_encode) {
        set_frame_parallel_level(&gf_group->frame_parallel_level[*frame_ind],
                                 parallel_frame_count, max_parallel_frames);
        // Set LF_UPDATE frames as non-reference frames.
        gf_group->is_frame_non_ref[*frame_ind] = true;
      }
      set_src_offset(gf_group, first_frame_index, *cur_frame_idx, *frame_ind);
      ++(*frame_ind);
      ++(*cur_frame_idx);
      ++(*cur_disp_idx);
      ++start;
    }
  } else {
    const int m = (start + end - 1) / 2;

    // Internal ARF.
    gf_group->update_type[*frame_ind] = INTNL_ARF_UPDATE;
    gf_group->arf_src_offset[*frame_ind] = m - start;
    gf_group->cur_frame_idx[*frame_ind] = *cur_frame_idx;
    gf_group->display_idx[*frame_ind] =
        *cur_disp_idx + gf_group->arf_src_offset[*frame_ind];
    gf_group->layer_depth[*frame_ind] = layer_depth;
    gf_group->frame_type[*frame_ind] = INTER_FRAME;
    gf_group->refbuf_state[*frame_ind] = REFBUF_UPDATE;

    if (do_frame_parallel_encode) {
      // If max_parallel_frames is not exceeded and if the frame will not be
      // temporally filtered, encode the next internal ARF frame in parallel.
      if (*parallel_frame_count > 1 &&
          *parallel_frame_count <= max_parallel_frames) {
        if (gf_group->arf_src_offset[*frame_ind] < TF_LOOKAHEAD_IDX_THR)
          gf_group->frame_parallel_level[*frame_ind] = 2;
        *parallel_frame_count = 1;
      }
    }
    set_src_offset(gf_group, first_frame_index, *cur_frame_idx, *frame_ind);

    // Get the boost factor for intermediate ARF frames.
    gf_group->arf_boost[*frame_ind] =
        av1_calc_arf_boost(twopass, twopass_frame, p_rc, frame_info, m, end - m,
                           m - start, NULL, NULL, 0);
    ++(*frame_ind);

    // Frames displayed before this internal ARF.
    set_multi_layer_params(twopass, twopass_frame, gf_group, p_rc, rc,
                           frame_info, start, m, cur_frame_idx, frame_ind,
                           parallel_frame_count, max_parallel_frames,
                           do_frame_parallel_encode, first_frame_index,
                           cur_disp_idx, layer_depth + 1);

    // Overlay for internal ARF.
    gf_group->update_type[*frame_ind] = INTNL_OVERLAY_UPDATE;
    gf_group->arf_src_offset[*frame_ind] = 0;
    gf_group->cur_frame_idx[*frame_ind] = *cur_frame_idx;
    gf_group->display_idx[*frame_ind] = *cur_disp_idx;
    gf_group->arf_boost[*frame_ind] = 0;
    gf_group->layer_depth[*frame_ind] = layer_depth;
    gf_group->frame_type[*frame_ind] = INTER_FRAME;
    gf_group->refbuf_state[*frame_ind] = REFBUF_UPDATE;

    set_src_offset(gf_group, first_frame_index, *cur_frame_idx, *frame_ind);
    ++(*frame_ind);
    ++(*cur_frame_idx);
    ++(*cur_disp_idx);

    // Frames displayed after this internal ARF.
    set_multi_layer_params(twopass, twopass_frame, gf_group, p_rc, rc,
                           frame_info, m + 1, end, cur_frame_idx, frame_ind,
                           parallel_frame_count, max_parallel_frames,
                           do_frame_parallel_encode, first_frame_index,
                           cur_disp_idx, layer_depth + 1);
  }
}

static int construct_multi_layer_gf_structure(
    AV1_COMP *cpi, TWO_PASS *twopass, GF_GROUP *const gf_group,
    RATE_CONTROL *rc, FRAME_INFO *const frame_info, int baseline_gf_interval,
    FRAME_UPDATE_TYPE first_frame_update_type) {
  PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  // TODO(angiebird): Why do we need "-1" here?
  const int gf_interval = baseline_gf_interval - 1;
  int frame_index = 0;
  int cur_frame_index = 0;

  // Set the display order hint for the first frame in the GF_GROUP.
  int cur_disp_index = (first_frame_update_type == KF_UPDATE)
                           ? 0
                           : cpi->common.current_frame.frame_number;

  // Initialize gf_group->frame_parallel_level, gf_group->is_frame_non_ref,
  // gf_group->src_offset and gf_group->is_frame_dropped with 0.
  memset(gf_group->frame_parallel_level, 0,
         sizeof(gf_group->frame_parallel_level));
  memset(gf_group->is_frame_non_ref, 0, sizeof(gf_group->is_frame_non_ref));
  memset(gf_group->src_offset, 0, sizeof(gf_group->src_offset));
  memset(gf_group->is_frame_dropped, 0, sizeof(gf_group->is_frame_dropped));
  // Initialize gf_group->skip_frame_refresh and gf_group->skip_frame_as_ref
  // with INVALID_IDX.
  memset(gf_group->skip_frame_refresh, INVALID_IDX,
         sizeof(gf_group->skip_frame_refresh));
  memset(gf_group->skip_frame_as_ref, INVALID_IDX,
         sizeof(gf_group->skip_frame_as_ref));

  int kf_decomp = cpi->oxcf.kf_cfg.enable_keyframe_filtering > 1;
  // This is a patch that fixes https://crbug.com/aomedia/3163
  // enable_keyframe_filtering > 1 will introduce an extra overlay frame at
  // key frame location. However when
  // baseline_gf_interval == MAX_STATIC_GF_GROUP_LENGTH, we can't
  // afford to have an extra overlay frame. Otherwise, the gf_group->size will
  // become MAX_STATIC_GF_GROUP_LENGTH + 1, which causes memory error.
  // A cheap solution is to turn of kf_decomp here.
  // TODO(angiebird): Find a systematic way to solve this issue.
  if (baseline_gf_interval == MAX_STATIC_GF_GROUP_LENGTH) {
    kf_decomp = 0;
  }
  if (first_frame_update_type == KF_UPDATE) {
    gf_group->update_type[frame_index] = kf_decomp ? ARF_UPDATE : KF_UPDATE;
    gf_group->arf_src_offset[frame_index] = 0;
    gf_group->cur_frame_idx[frame_index] = cur_frame_index;
    gf_group->layer_depth[frame_index] = 0;
    gf_group->frame_type[frame_index] = KEY_FRAME;
    gf_group->refbuf_state[frame_index] = REFBUF_RESET;
    gf_group->max_layer_depth = 0;
    gf_group->display_idx[frame_index] = cur_disp_index;
    if (!kf_decomp) cur_disp_index++;
    ++frame_index;

    if (kf_decomp) {
      gf_group->update_type[frame_index] = OVERLAY_UPDATE;
      gf_group->arf_src_offset[frame_index] = 0;
      gf_group->cur_frame_idx[frame_index] = cur_frame_index;
      gf_group->layer_depth[frame_index] = 0;
      gf_group->frame_type[frame_index] = INTER_FRAME;
      gf_group->refbuf_state[frame_index] = REFBUF_UPDATE;
      gf_group->max_layer_depth = 0;
      gf_group->display_idx[frame_index] = cur_disp_index;
      cur_disp_index++;
      ++frame_index;
    }
    cur_frame_index++;
  }

  if (first_frame_update_type == GF_UPDATE) {
    gf_group->update_type[frame_index] = GF_UPDATE;
    gf_group->arf_src_offset[frame_index] = 0;
    gf_group->cur_frame_idx[frame_index] = cur_frame_index;
    gf_group->layer_depth[frame_index] = 0;
    gf_group->frame_type[frame_index] = INTER_FRAME;
    gf_group->refbuf_state[frame_index] = REFBUF_UPDATE;
    gf_group->max_layer_depth = 0;
    gf_group->display_idx[frame_index] = cur_disp_index;
    cur_disp_index++;
    ++frame_index;
    ++cur_frame_index;
  }

  // ALTREF.
  const int use_altref = gf_group->max_layer_depth_allowed > 0;
  int is_fwd_kf = rc->frames_to_fwd_kf == gf_interval;

  const int sframe_dist = cpi->oxcf.kf_cfg.sframe_dist;
  const int sframe_mode = cpi->oxcf.kf_cfg.sframe_mode;
  const int sframe_enabled = (sframe_mode > 0) && (sframe_dist > 0);

  if (sframe_enabled) {
    switch (sframe_mode) {
      case 1: gf_group->is_sframe_due = use_altref; break;
      case 2:
        gf_group->is_sframe_due |=
            (frame_index && !(frame_index % sframe_dist));
        break;
    }
  }

  if (use_altref) {
    gf_group->update_type[frame_index] = ARF_UPDATE;
    gf_group->arf_src_offset[frame_index] = gf_interval - cur_frame_index;
    gf_group->cur_frame_idx[frame_index] = cur_frame_index;
    gf_group->layer_depth[frame_index] = 1;
    gf_group->arf_boost[frame_index] = cpi->ppi->p_rc.gfu_boost;
    gf_group->frame_type[frame_index] = is_fwd_kf                 ? KEY_FRAME
                                        : gf_group->is_sframe_due ? S_FRAME
                                                                  : INTER_FRAME;
    gf_group->is_sframe_due =
        sframe_enabled && !(gf_group->frame_type[frame_index] == S_FRAME);
    gf_group->refbuf_state[frame_index] = REFBUF_UPDATE;
    gf_group->max_layer_depth = 1;
    gf_group->arf_index = frame_index;
    gf_group->display_idx[frame_index] =
        cur_disp_index + gf_group->arf_src_offset[frame_index];
    ++frame_index;
  } else {
    gf_group->arf_index = -1;
  }

  // Flag to indicate if multi-layer configuration is complete.
  int is_multi_layer_configured = 0;

  // Running count of no. of frames that is part of a given parallel
  // encode set in a gf_group. Value of 1 indicates no parallel encode.
  int parallel_frame_count = 1;
  // Enable parallel encode of frames if gf_group has a multi-layer pyramid
  // structure with minimum 4 layers.
  int do_frame_parallel_encode = (cpi->ppi->num_fp_contexts > 1 && use_altref &&
                                  gf_group->max_layer_depth_allowed >= 4);

  int first_frame_index = cur_frame_index;
  if (do_frame_parallel_encode) {
    // construct_multi_layer_gf_structure() takes the input parameter
    // 'gf_interval' as p_rc->baseline_gf_interval - 1 . Below code computes the
    // actual GF_GROUP length by compensating for this offset.
    int actual_gf_length = ((first_frame_update_type == KF_UPDATE) ||
                            (first_frame_update_type == GF_UPDATE))
                               ? gf_interval
                               : gf_interval + 1;

    // In order to facilitate parallel encoding of frames in lower layer depths,
    // encode reordering is done. Currently encode reordering is enabled only
    // for gf-intervals 16 and 32. NOTE: Since the buffer holding the
    // reference frames is of size 8 (ref_frame_map[REF_FRAMES]), there is a
    // limitation on the number of hidden frames possible at any given point and
    // hence the reordering is enabled only for gf-intervals 16 and 32.
    // Disabling encode reordering for gf-interval 14 since some cross-frame
    // dependencies related to temporal filtering for FPMT is currently not
    // handled.
    int disable_gf14_reorder = 1;
    if (actual_gf_length == 14 && !disable_gf14_reorder) {
      // This array holds the gf index of INTNL_ARF_UPDATE frames in the slot
      // corresponding to their display order hint. This is used while
      // configuring the LF_UPDATE frames and INTNL_OVERLAY_UPDATE frames.
      int doh_gf_index_map[FIXED_GF_INTERVAL];
      // Initialize doh_gf_index_map with INVALID_IDX.
      memset(&doh_gf_index_map[0], INVALID_IDX,
             (sizeof(doh_gf_index_map[0]) * FIXED_GF_INTERVAL));

      FRAME_REORDER_INFO arf_frame_stats[REF_FRAMES - 1];
      // Store the stats corresponding to layer 1 frame.
      fill_arf_frame_stats(arf_frame_stats, 0, actual_gf_length, 1,
                           actual_gf_length);
      int count_arf_frames = 1;

      // Sets multi-layer params for gf-interval 14 to consecutively encode
      // frames in the same layer depth, i.e., encode order would be 0-> 14->
      // 7-> 3-> 10-> 5-> 12-> 1-> 2-> 4-> 6-> 8-> 9-> 11-> 13.
      // TODO(Remya): Set GF_GROUP param 'arf_boost' for all frames.
      set_multi_layer_params_for_gf14(
          twopass, &cpi->twopass_frame, p_rc, frame_info, gf_group,
          arf_frame_stats, &cur_frame_index, &frame_index, &count_arf_frames,
          doh_gf_index_map, &parallel_frame_count, &first_frame_index,
          &cur_disp_index, actual_gf_length, use_altref + 1,
          cpi->ppi->num_fp_contexts);

      // Set gf_group->skip_frame_refresh.
      for (int i = 0; i < actual_gf_length; i++) {
        int count = 0;
        if (gf_group->update_type[i] == INTNL_ARF_UPDATE) {
          for (int j = 0; j < i; j++) {
            // Store the display order hint of the frames which would not
            // have been displayed at the encode call of frame 'i'.
            if ((gf_group->display_idx[j] < gf_group->display_idx[i]) &&
                gf_group->update_type[j] == INTNL_ARF_UPDATE) {
              gf_group->skip_frame_refresh[i][count++] =
                  gf_group->display_idx[j];
            }
          }
        }
      }
    } else {
      // Set layer depth threshold for reordering as per the gf length.
      int depth_thr = (actual_gf_length == 16)   ? 3
                      : (actual_gf_length == 32) ? 4
                                                 : INT_MAX;

      set_multi_layer_params_for_fp(
          twopass, &cpi->twopass_frame, gf_group, p_rc, rc, frame_info,
          cur_frame_index, gf_interval, &cur_frame_index, &frame_index,
          &parallel_frame_count, cpi->ppi->num_fp_contexts,
          do_frame_parallel_encode, &first_frame_index, depth_thr,
          &cur_disp_index, use_altref + 1);
    }
    is_multi_layer_configured = 1;
  }

  // Rest of the frames.
  if (!is_multi_layer_configured)
    set_multi_layer_params(twopass, &cpi->twopass_frame, gf_group, p_rc, rc,
                           frame_info, cur_frame_index, gf_interval,
                           &cur_frame_index, &frame_index,
                           &parallel_frame_count, cpi->ppi->num_fp_contexts,
                           do_frame_parallel_encode, &first_frame_index,
                           &cur_disp_index, use_altref + 1);

  if (use_altref) {
    gf_group->update_type[frame_index] = OVERLAY_UPDATE;
    gf_group->arf_src_offset[frame_index] = 0;
    gf_group->cur_frame_idx[frame_index] = cur_frame_index;
    gf_group->layer_depth[frame_index] = MAX_ARF_LAYERS;
    gf_group->arf_boost[frame_index] = NORMAL_BOOST;
    gf_group->frame_type[frame_index] = INTER_FRAME;
    gf_group->refbuf_state[frame_index] =
        is_fwd_kf ? REFBUF_RESET : REFBUF_UPDATE;
    gf_group->display_idx[frame_index] = cur_disp_index;
    ++frame_index;
  } else {
    for (; cur_frame_index <= gf_interval; ++cur_frame_index) {
      gf_group->update_type[frame_index] = LF_UPDATE;
      gf_group->arf_src_offset[frame_index] = 0;
      gf_group->cur_frame_idx[frame_index] = cur_frame_index;
      gf_group->layer_depth[frame_index] = MAX_ARF_LAYERS;
      gf_group->arf_boost[frame_index] = NORMAL_BOOST;
      gf_group->frame_type[frame_index] = INTER_FRAME;
      gf_group->refbuf_state[frame_index] = REFBUF_UPDATE;
      gf_group->max_layer_depth = AOMMAX(gf_group->max_layer_depth, 2);
      set_src_offset(gf_group, &first_frame_index, cur_frame_index,
                     frame_index);
      gf_group->display_idx[frame_index] = cur_disp_index;
      cur_disp_index++;
      ++frame_index;
    }
  }
  if (do_frame_parallel_encode) {
    // Iterate through the gf_group and reset frame_parallel_level to 0 in case
    // a frame is marked as frame_parallel_level 1 with no subsequent
    // frame_parallel_level 2 frame(s).
    int level1_frame_idx = INT_MAX;
    int level2_frame_count = 0;
    for (int frame_idx = 0; frame_idx < frame_index; frame_idx++) {
      if (gf_group->frame_parallel_level[frame_idx] == 1) {
        // Set frame_parallel_level to 0 if only one frame is present in a
        // parallel encode set.
        if (level1_frame_idx != INT_MAX && !level2_frame_count)
          gf_group->frame_parallel_level[level1_frame_idx] = 0;
        // Book-keep frame_idx of frame_parallel_level 1 frame and reset the
        // count of frame_parallel_level 2 frames in the corresponding parallel
        // encode set.
        level1_frame_idx = frame_idx;
        level2_frame_count = 0;
      }
      if (gf_group->frame_parallel_level[frame_idx] == 2) level2_frame_count++;
    }
    // If frame_parallel_level is set to 1 for the last LF_UPDATE
    // frame in the gf_group, reset it to zero since there are no subsequent
    // frames in the gf_group.
    if (gf_group->frame_parallel_level[frame_index - 2] == 1) {
      assert(gf_group->update_type[frame_index - 2] == LF_UPDATE);
      gf_group->frame_parallel_level[frame_index - 2] = 0;
    }
  }

  for (int gf_idx = frame_index; gf_idx < MAX_STATIC_GF_GROUP_LENGTH;
       ++gf_idx) {
    gf_group->update_type[gf_idx] = LF_UPDATE;
    gf_group->arf_src_offset[gf_idx] = 0;
    gf_group->cur_frame_idx[gf_idx] = gf_idx;
    gf_group->layer_depth[gf_idx] = MAX_ARF_LAYERS;
    gf_group->arf_boost[gf_idx] = NORMAL_BOOST;
    gf_group->frame_type[gf_idx] = INTER_FRAME;
    gf_group->refbuf_state[gf_idx] = REFBUF_UPDATE;
    gf_group->max_layer_depth = AOMMAX(gf_group->max_layer_depth, 2);
  }

  return frame_index;
}

static void set_ld_layer_depth(GF_GROUP *gf_group, int gop_length) {
  int log_gop_length = 0;
  while ((1 << log_gop_length) < gop_length) {
    ++log_gop_length;
  }

  for (int gf_index = 0; gf_index < gf_group->size; ++gf_index) {
    int count = 0;
    // Find the trailing zeros
    for (; count < MAX_ARF_LAYERS; ++count) {
      if ((gf_index >> count) & 0x01) break;
    }
    gf_group->layer_depth[gf_index] = AOMMAX(log_gop_length - count, 0);
  }
  gf_group->max_layer_depth = AOMMIN(log_gop_length, MAX_ARF_LAYERS);
}

void av1_gop_setup_structure(AV1_COMP *cpi) {
  RATE_CONTROL *const rc = &cpi->rc;
  PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  GF_GROUP *const gf_group = &cpi->ppi->gf_group;
  TWO_PASS *const twopass = &cpi->ppi->twopass;
  FRAME_INFO *const frame_info = &cpi->frame_info;
  const int key_frame = rc->frames_since_key == 0;
  FRAME_UPDATE_TYPE first_frame_update_type = ARF_UPDATE;

  if (key_frame) {
    first_frame_update_type = KF_UPDATE;
    if (cpi->oxcf.kf_max_pyr_height != -1) {
      gf_group->max_layer_depth_allowed = AOMMIN(
          cpi->oxcf.kf_max_pyr_height, gf_group->max_layer_depth_allowed);
    }
  } else if (!cpi->ppi->gf_state.arf_gf_boost_lst) {
    first_frame_update_type = GF_UPDATE;
  }

  gf_group->size = construct_multi_layer_gf_structure(
      cpi, twopass, gf_group, rc, frame_info, p_rc->baseline_gf_interval,
      first_frame_update_type);

  if (gf_group->max_layer_depth_allowed == 0)
    set_ld_layer_depth(gf_group, p_rc->baseline_gf_interval);
}

int av1_gop_check_forward_keyframe(const GF_GROUP *gf_group,
                                   int gf_frame_index) {
  return gf_group->frame_type[gf_frame_index] == KEY_FRAME &&
         gf_group->refbuf_state[gf_frame_index] == REFBUF_UPDATE;
}

int av1_gop_is_second_arf(const GF_GROUP *gf_group, int gf_frame_index) {
  const int arf_src_offset = gf_group->arf_src_offset[gf_frame_index];
  // TODO(angiebird): when gf_group->size == 32, it's possble to
  // have "two" second arf. Check if this is acceptable.
  if (gf_group->update_type[gf_frame_index] == INTNL_ARF_UPDATE &&
      arf_src_offset >= TF_LOOKAHEAD_IDX_THR) {
    return 1;
  }
  return 0;
}
