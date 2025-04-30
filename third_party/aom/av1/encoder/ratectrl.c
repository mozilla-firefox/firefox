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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aom_dsp/aom_dsp_common.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/mem.h"
#include "aom_ports/aom_once.h"

#include "av1/common/alloccommon.h"
#include "av1/encoder/aq_cyclicrefresh.h"
#include "av1/common/common.h"
#include "av1/common/entropymode.h"
#include "av1/common/quant_common.h"
#include "av1/common/seg_common.h"

#include "av1/encoder/encodemv.h"
#include "av1/encoder/encoder_utils.h"
#include "av1/encoder/encode_strategy.h"
#include "av1/encoder/gop_structure.h"
#include "av1/encoder/mcomp.h"
#include "av1/encoder/random.h"
#include "av1/encoder/ratectrl.h"

#include "config/aom_dsp_rtcd.h"

#define USE_UNRESTRICTED_Q_IN_CQ_MODE 0

// Max rate target for 1080P and below encodes under normal circumstances
// (1920 * 1080 / (16 * 16)) * MAX_MB_RATE bits per MB
#define MAX_MB_RATE 250
#define MAXRATE_1080P 2025000

#define MIN_BPB_FACTOR 0.005
#define MAX_BPB_FACTOR 50

#define SUPERRES_QADJ_PER_DENOM_KEYFRAME_SOLO 0
#define SUPERRES_QADJ_PER_DENOM_KEYFRAME 2
#define SUPERRES_QADJ_PER_DENOM_ARFFRAME 0

#define FRAME_OVERHEAD_BITS 200
#define ASSIGN_MINQ_TABLE(bit_depth, name)                   \
  do {                                                       \
    switch (bit_depth) {                                     \
      case AOM_BITS_8: name = name##_8; break;               \
      case AOM_BITS_10: name = name##_10; break;             \
      case AOM_BITS_12: name = name##_12; break;             \
      default:                                               \
        assert(0 &&                                          \
               "bit_depth should be AOM_BITS_8, AOM_BITS_10" \
               " or AOM_BITS_12");                           \
        name = NULL;                                         \
    }                                                        \
  } while (0)

// Tables relating active max Q to active min Q
static int kf_low_motion_minq_8[QINDEX_RANGE];
static int kf_high_motion_minq_8[QINDEX_RANGE];
static int arfgf_low_motion_minq_8[QINDEX_RANGE];
static int arfgf_high_motion_minq_8[QINDEX_RANGE];
static int inter_minq_8[QINDEX_RANGE];
static int rtc_minq_8[QINDEX_RANGE];

static int kf_low_motion_minq_10[QINDEX_RANGE];
static int kf_high_motion_minq_10[QINDEX_RANGE];
static int arfgf_low_motion_minq_10[QINDEX_RANGE];
static int arfgf_high_motion_minq_10[QINDEX_RANGE];
static int inter_minq_10[QINDEX_RANGE];
static int rtc_minq_10[QINDEX_RANGE];
static int kf_low_motion_minq_12[QINDEX_RANGE];
static int kf_high_motion_minq_12[QINDEX_RANGE];
static int arfgf_low_motion_minq_12[QINDEX_RANGE];
static int arfgf_high_motion_minq_12[QINDEX_RANGE];
static int inter_minq_12[QINDEX_RANGE];
static int rtc_minq_12[QINDEX_RANGE];

static int gf_high = 2400;
static int gf_low = 300;
#ifdef STRICT_RC
static int kf_high = 3200;
#else
static int kf_high = 5000;
#endif
static int kf_low = 400;

// How many times less pixels there are to encode given the current scaling.
// Temporary replacement for rcf_mult and rate_thresh_mult.
static double resize_rate_factor(const FrameDimensionCfg *const frm_dim_cfg,
                                 int width, int height) {
  return (double)(frm_dim_cfg->width * frm_dim_cfg->height) / (width * height);
}

// Functions to compute the active minq lookup table entries based on a
// formulaic approach to facilitate easier adjustment of the Q tables.
// The formulae were derived from computing a 3rd order polynomial best
// fit to the original data (after plotting real maxq vs minq (not q index))
static int get_minq_index(double maxq, double x3, double x2, double x1,
                          aom_bit_depth_t bit_depth) {
  const double minqtarget = AOMMIN(((x3 * maxq + x2) * maxq + x1) * maxq, maxq);

  // Special case handling to deal with the step from q2.0
  // down to lossless mode represented by q 1.0.
  if (minqtarget <= 2.0) return 0;

  return av1_find_qindex(minqtarget, bit_depth, 0, QINDEX_RANGE - 1);
}

static void init_minq_luts(int *kf_low_m, int *kf_high_m, int *arfgf_low,
                           int *arfgf_high, int *inter, int *rtc,
                           aom_bit_depth_t bit_depth) {
  int i;
  for (i = 0; i < QINDEX_RANGE; i++) {
    const double maxq = av1_convert_qindex_to_q(i, bit_depth);
    kf_low_m[i] = get_minq_index(maxq, 0.000001, -0.0004, 0.150, bit_depth);
    kf_high_m[i] = get_minq_index(maxq, 0.0000021, -0.00125, 0.45, bit_depth);
    arfgf_low[i] = get_minq_index(maxq, 0.0000015, -0.0009, 0.30, bit_depth);
    arfgf_high[i] = get_minq_index(maxq, 0.0000021, -0.00125, 0.55, bit_depth);
    inter[i] = get_minq_index(maxq, 0.00000271, -0.00113, 0.90, bit_depth);
    rtc[i] = get_minq_index(maxq, 0.00000271, -0.00113, 0.70, bit_depth);
  }
}

static void rc_init_minq_luts(void) {
  init_minq_luts(kf_low_motion_minq_8, kf_high_motion_minq_8,
                 arfgf_low_motion_minq_8, arfgf_high_motion_minq_8,
                 inter_minq_8, rtc_minq_8, AOM_BITS_8);
  init_minq_luts(kf_low_motion_minq_10, kf_high_motion_minq_10,
                 arfgf_low_motion_minq_10, arfgf_high_motion_minq_10,
                 inter_minq_10, rtc_minq_10, AOM_BITS_10);
  init_minq_luts(kf_low_motion_minq_12, kf_high_motion_minq_12,
                 arfgf_low_motion_minq_12, arfgf_high_motion_minq_12,
                 inter_minq_12, rtc_minq_12, AOM_BITS_12);
}

void av1_rc_init_minq_luts(void) { aom_once(rc_init_minq_luts); }

// These functions use formulaic calculations to make playing with the
// quantizer tables easier. If necessary they can be replaced by lookup
// tables if and when things settle down in the experimental bitstream
double av1_convert_qindex_to_q(int qindex, aom_bit_depth_t bit_depth) {
  // Convert the index to a real Q value (scaled down to match old Q values)
  switch (bit_depth) {
    case AOM_BITS_8: return av1_ac_quant_QTX(qindex, 0, bit_depth) / 4.0;
    case AOM_BITS_10: return av1_ac_quant_QTX(qindex, 0, bit_depth) / 16.0;
    case AOM_BITS_12: return av1_ac_quant_QTX(qindex, 0, bit_depth) / 64.0;
    default:
      assert(0 && "bit_depth should be AOM_BITS_8, AOM_BITS_10 or AOM_BITS_12");
      return -1.0;
  }
}

int av1_convert_q_to_qindex(double q, aom_bit_depth_t bit_depth) {
  int qindex = MINQ;

  // Find the first qindex that matches or exceeds q.
  // Note: this operation can also be done with a binary search, as
  // av1_convert_qindex_to_q() is monotonically increasing with respect to
  // increasing qindex.
  while (qindex < MAXQ && av1_convert_qindex_to_q(qindex, bit_depth) < q) {
    qindex++;
  }

  return qindex;
}

// Gets the appropriate bpmb enumerator based on the frame and content type
static int get_bpmb_enumerator(FRAME_TYPE frame_type,
                               const int is_screen_content_type) {
  int enumerator;

  if (is_screen_content_type) {
    enumerator = (frame_type == KEY_FRAME) ? 1000000 : 750000;
  } else {
    enumerator = (frame_type == KEY_FRAME) ? 2000000 : 1500000;
  }

  return enumerator;
}

static int get_init_ratio(double sse) { return (int)(300000 / sse); }

// Adjustment based on spatial content and last encoded keyframe.
// Allow for increase in enumerator to reduce overshoot.
static int adjust_rtc_keyframe(const RATE_CONTROL *rc, int enumerator) {
  // Don't adjust if most of the image is flat.
  if (rc->perc_spatial_flat_blocks > 70) return enumerator;
  if (rc->last_encoded_size_keyframe == 0 ||
      rc->frames_since_scene_change < rc->frames_since_key) {
    // Very first frame, or if scene change happened after last keyframe.
    if (rc->frame_spatial_variance > 1000 ||
        (rc->frame_spatial_variance > 500 && rc->perc_spatial_flat_blocks == 0))
      return enumerator << 3;
    else if (rc->frame_spatial_variance > 500 &&
             rc->perc_spatial_flat_blocks < 10)
      return enumerator << 2;
    else if (rc->frame_spatial_variance > 400)
      return enumerator << 1;
  } else if (rc->frames_since_scene_change >= rc->frames_since_key) {
    // There was no scene change before previous encoded keyframe, so
    // use the last_encoded/target_size_keyframe.
    if (rc->last_encoded_size_keyframe > 4 * rc->last_target_size_keyframe &&
        rc->frame_spatial_variance > 500)
      return enumerator << 3;
    else if (rc->last_encoded_size_keyframe >
                 2 * rc->last_target_size_keyframe &&
             rc->frame_spatial_variance > 200)
      return enumerator << 2;
    else if (rc->last_encoded_size_keyframe > rc->last_target_size_keyframe)
      return enumerator << 1;
  }
  return enumerator;
}

int av1_rc_bits_per_mb(const AV1_COMP *cpi, FRAME_TYPE frame_type, int qindex,
                       double correction_factor, int accurate_estimate) {
  const AV1_COMMON *const cm = &cpi->common;
  const int is_screen_content_type = cpi->is_screen_content_type;
  const aom_bit_depth_t bit_depth = cm->seq_params->bit_depth;
  const double q = av1_convert_qindex_to_q(qindex, bit_depth);
  int enumerator = get_bpmb_enumerator(frame_type, is_screen_content_type);

  assert(correction_factor <= MAX_BPB_FACTOR &&
         correction_factor >= MIN_BPB_FACTOR);

  if (cpi->oxcf.rc_cfg.mode == AOM_CBR && frame_type != KEY_FRAME &&
      accurate_estimate && cpi->rec_sse != UINT64_MAX) {
    const int mbs = cm->mi_params.MBs;
    const double sse_sqrt =
        (double)((int)sqrt((double)(cpi->rec_sse)) << BPER_MB_NORMBITS) /
        (double)mbs;
    const int ratio = (cpi->rc.bit_est_ratio == 0) ? get_init_ratio(sse_sqrt)
                                                   : cpi->rc.bit_est_ratio;
    // Clamp the enumerator to lower the q fluctuations.
    enumerator = AOMMIN(AOMMAX((int)(ratio * sse_sqrt), 20000), 170000);
  } else if (cpi->oxcf.rc_cfg.mode == AOM_CBR && frame_type == KEY_FRAME &&
             cpi->sf.rt_sf.rc_adjust_keyframe && bit_depth == 8 &&
             cpi->oxcf.rc_cfg.max_intra_bitrate_pct > 0 &&
             cpi->svc.spatial_layer_id == 0) {
    enumerator = adjust_rtc_keyframe(&cpi->rc, enumerator);
  }
  // q based adjustment to baseline enumerator
  return (int)(enumerator * correction_factor / q);
}

int av1_estimate_bits_at_q(const AV1_COMP *cpi, int q,
                           double correction_factor) {
  const AV1_COMMON *const cm = &cpi->common;
  const FRAME_TYPE frame_type = cm->current_frame.frame_type;
  const int mbs = cm->mi_params.MBs;
  const int bpm =
      (int)(av1_rc_bits_per_mb(cpi, frame_type, q, correction_factor,
                               cpi->sf.hl_sf.accurate_bit_estimate));
  return AOMMAX(FRAME_OVERHEAD_BITS,
                (int)((uint64_t)bpm * mbs) >> BPER_MB_NORMBITS);
}

static int clamp_pframe_target_size(const AV1_COMP *const cpi, int64_t target,
                                    FRAME_UPDATE_TYPE frame_update_type) {
  const RATE_CONTROL *rc = &cpi->rc;
  const RateControlCfg *const rc_cfg = &cpi->oxcf.rc_cfg;
  const int min_frame_target =
      AOMMAX(rc->min_frame_bandwidth, rc->avg_frame_bandwidth >> 5);
  // Clip the frame target to the minimum setup value.
  if (frame_update_type == OVERLAY_UPDATE ||
      frame_update_type == INTNL_OVERLAY_UPDATE) {
    // If there is an active ARF at this location use the minimum
    // bits on this frame even if it is a constructed arf.
    // The active maximum quantizer insures that an appropriate
    // number of bits will be spent if needed for constructed ARFs.
    target = min_frame_target;
  } else if (target < min_frame_target) {
    target = min_frame_target;
  }

  // Clip the frame target to the maximum allowed value.
  if (target > rc->max_frame_bandwidth) target = rc->max_frame_bandwidth;
  if (rc_cfg->max_inter_bitrate_pct) {
    const int64_t max_rate =
        (int64_t)rc->avg_frame_bandwidth * rc_cfg->max_inter_bitrate_pct / 100;
    target = AOMMIN(target, max_rate);
  }

  return (int)target;
}

static int clamp_iframe_target_size(const AV1_COMP *const cpi, int64_t target) {
  const RATE_CONTROL *rc = &cpi->rc;
  const RateControlCfg *const rc_cfg = &cpi->oxcf.rc_cfg;
  if (rc_cfg->max_intra_bitrate_pct) {
    const int64_t max_rate =
        (int64_t)rc->avg_frame_bandwidth * rc_cfg->max_intra_bitrate_pct / 100;
    target = AOMMIN(target, max_rate);
  }
  if (target > rc->max_frame_bandwidth) target = rc->max_frame_bandwidth;
  return (int)target;
}

// Update the buffer level for higher temporal layers, given the encoded current
// temporal layer.
static void update_layer_buffer_level(SVC *svc, int encoded_frame_size,
                                      bool is_screen) {
  const int current_temporal_layer = svc->temporal_layer_id;
  for (int i = current_temporal_layer + 1; i < svc->number_temporal_layers;
       ++i) {
    const int layer =
        LAYER_IDS_TO_IDX(svc->spatial_layer_id, i, svc->number_temporal_layers);
    LAYER_CONTEXT *lc = &svc->layer_context[layer];
    PRIMARY_RATE_CONTROL *lp_rc = &lc->p_rc;
    lp_rc->bits_off_target +=
        (int)round(lc->target_bandwidth / lc->framerate) - encoded_frame_size;
    // Clip buffer level to maximum buffer size for the layer.
    lp_rc->bits_off_target =
        AOMMIN(lp_rc->bits_off_target, lp_rc->maximum_buffer_size);
    lp_rc->buffer_level = lp_rc->bits_off_target;

    // For screen-content mode: don't let buffer level go below threshold,
    // given here as -rc->maximum_ buffer_size, to allow buffer to come back
    // up sooner after slide change with big overshoot.
    if (is_screen) {
      lp_rc->bits_off_target =
          AOMMAX(lp_rc->bits_off_target, -lp_rc->maximum_buffer_size);
      lp_rc->buffer_level = lp_rc->bits_off_target;
    }
  }
}
// Update the buffer level: leaky bucket model.
static void update_buffer_level(AV1_COMP *cpi, int encoded_frame_size) {
  const AV1_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;
  PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;

  // Non-viewable frames are a special case and are treated as pure overhead.
  if (!cm->show_frame)
    p_rc->bits_off_target -= encoded_frame_size;
  else
    p_rc->bits_off_target += rc->avg_frame_bandwidth - encoded_frame_size;

  // Clip the buffer level to the maximum specified buffer size.
  p_rc->bits_off_target =
      AOMMIN(p_rc->bits_off_target, p_rc->maximum_buffer_size);
  // For screen-content mode: don't let buffer level go below threshold,
  // given here as -rc->maximum_ buffer_size, to allow buffer to come back
  // up sooner after slide change with big overshoot.
  if (cpi->oxcf.tune_cfg.content == AOM_CONTENT_SCREEN)
    p_rc->bits_off_target =
        AOMMAX(p_rc->bits_off_target, -p_rc->maximum_buffer_size);
  p_rc->buffer_level = p_rc->bits_off_target;

  if (cpi->ppi->use_svc)
    update_layer_buffer_level(&cpi->svc, encoded_frame_size,
                              cpi->oxcf.tune_cfg.content == AOM_CONTENT_SCREEN);

#if CONFIG_FPMT_TEST
  /* The variable temp_buffer_level is introduced for quality
   * simulation purpose, it retains the value previous to the parallel
   * encode frames. The variable is updated based on the update flag.
   *
   * If there exist show_existing_frames between parallel frames, then to
   * retain the temp state do not update it. */
  int show_existing_between_parallel_frames =
      (cpi->ppi->gf_group.update_type[cpi->gf_frame_index] ==
           INTNL_OVERLAY_UPDATE &&
       cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index + 1] == 2);

  if (cpi->do_frame_data_update && !show_existing_between_parallel_frames &&
      cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE) {
    p_rc->temp_buffer_level = p_rc->buffer_level;
  }
#endif
}

int av1_rc_get_default_min_gf_interval(int width, int height,
                                       double framerate) {
  // Assume we do not need any constraint lower than 4K 20 fps
  static const double factor_safe = 3840 * 2160 * 20.0;
  const double factor = (double)width * height * framerate;
  const int default_interval =
      clamp((int)(framerate * 0.125), MIN_GF_INTERVAL, MAX_GF_INTERVAL);

  if (factor <= factor_safe)
    return default_interval;
  else
    return AOMMAX(default_interval,
                  (int)(MIN_GF_INTERVAL * factor / factor_safe + 0.5));
  // Note this logic makes:
  // 4K24: 5
  // 4K30: 6
  // 4K60: 12
}

// Note get_default_max_gf_interval() requires the min_gf_interval to
// be passed in to ensure that the max_gf_interval returned is at least as big
// as that.
static int get_default_max_gf_interval(double framerate, int min_gf_interval) {
  int interval = AOMMIN(MAX_GF_INTERVAL, (int)(framerate * 0.75));
  interval += (interval & 0x01);  // Round to even value
  interval = AOMMAX(MAX_GF_INTERVAL, interval);
  return AOMMAX(interval, min_gf_interval);
}

void av1_primary_rc_init(const AV1EncoderConfig *oxcf,
                         PRIMARY_RATE_CONTROL *p_rc) {
  const RateControlCfg *const rc_cfg = &oxcf->rc_cfg;

  int worst_allowed_q = rc_cfg->worst_allowed_q;

  int min_gf_interval = oxcf->gf_cfg.min_gf_interval;
  int max_gf_interval = oxcf->gf_cfg.max_gf_interval;
  if (min_gf_interval == 0)
    min_gf_interval = av1_rc_get_default_min_gf_interval(
        oxcf->frm_dim_cfg.width, oxcf->frm_dim_cfg.height,
        oxcf->input_cfg.init_framerate);
  if (max_gf_interval == 0)
    max_gf_interval = get_default_max_gf_interval(
        oxcf->input_cfg.init_framerate, min_gf_interval);
  p_rc->baseline_gf_interval = (min_gf_interval + max_gf_interval) / 2;
  p_rc->this_key_frame_forced = 0;
  p_rc->next_key_frame_forced = 0;
  p_rc->ni_frames = 0;

  p_rc->tot_q = 0.0;
  p_rc->total_actual_bits = 0;
  p_rc->total_target_bits = 0;
  p_rc->buffer_level = p_rc->starting_buffer_level;

  if (oxcf->target_seq_level_idx[0] < SEQ_LEVELS) {
    worst_allowed_q = 255;
  }
  if (oxcf->pass == AOM_RC_ONE_PASS && rc_cfg->mode == AOM_CBR) {
    p_rc->avg_frame_qindex[KEY_FRAME] = worst_allowed_q;
    p_rc->avg_frame_qindex[INTER_FRAME] = worst_allowed_q;
  } else {
    p_rc->avg_frame_qindex[KEY_FRAME] =
        (worst_allowed_q + rc_cfg->best_allowed_q) / 2;
    p_rc->avg_frame_qindex[INTER_FRAME] =
        (worst_allowed_q + rc_cfg->best_allowed_q) / 2;
  }
  p_rc->avg_q = av1_convert_qindex_to_q(rc_cfg->worst_allowed_q,
                                        oxcf->tool_cfg.bit_depth);
  p_rc->last_q[KEY_FRAME] = rc_cfg->best_allowed_q;
  p_rc->last_q[INTER_FRAME] = rc_cfg->worst_allowed_q;

  for (int i = 0; i < RATE_FACTOR_LEVELS; ++i) {
    p_rc->rate_correction_factors[i] = 0.7;
  }
  p_rc->rate_correction_factors[KF_STD] = 1.0;
  p_rc->bits_off_target = p_rc->starting_buffer_level;

  p_rc->rolling_target_bits = AOMMAX(
      1, (int)(oxcf->rc_cfg.target_bandwidth / oxcf->input_cfg.init_framerate));
  p_rc->rolling_actual_bits = AOMMAX(
      1, (int)(oxcf->rc_cfg.target_bandwidth / oxcf->input_cfg.init_framerate));
}

void av1_rc_init(const AV1EncoderConfig *oxcf, RATE_CONTROL *rc) {
  const RateControlCfg *const rc_cfg = &oxcf->rc_cfg;

  rc->frames_since_key = 8;  // Sensible default for first frame.
  rc->frames_to_fwd_kf = oxcf->kf_cfg.fwd_kf_dist;

  rc->frames_till_gf_update_due = 0;
  rc->ni_av_qi = rc_cfg->worst_allowed_q;
  rc->ni_tot_qi = 0;

  rc->min_gf_interval = oxcf->gf_cfg.min_gf_interval;
  rc->max_gf_interval = oxcf->gf_cfg.max_gf_interval;
  if (rc->min_gf_interval == 0)
    rc->min_gf_interval = av1_rc_get_default_min_gf_interval(
        oxcf->frm_dim_cfg.width, oxcf->frm_dim_cfg.height,
        oxcf->input_cfg.init_framerate);
  if (rc->max_gf_interval == 0)
    rc->max_gf_interval = get_default_max_gf_interval(
        oxcf->input_cfg.init_framerate, rc->min_gf_interval);
  rc->avg_frame_low_motion = 0;

  rc->resize_state = ORIG;
  rc->resize_avg_qp = 0;
  rc->resize_buffer_underflow = 0;
  rc->resize_count = 0;
  rc->rtc_external_ratectrl = 0;
  rc->frame_level_fast_extra_bits = 0;
  rc->use_external_qp_one_pass = 0;
  rc->percent_blocks_inactive = 0;
  rc->force_max_q = 0;
  rc->postencode_drop = 0;
  rc->frames_since_scene_change = 0;
}

static bool check_buffer_below_thresh(AV1_COMP *cpi, int64_t buffer_level,
                                      int drop_mark) {
  SVC *svc = &cpi->svc;
  if (!cpi->ppi->use_svc || cpi->svc.number_spatial_layers == 1 ||
      cpi->svc.framedrop_mode == AOM_LAYER_DROP) {
    return (buffer_level <= drop_mark);
  } else {
    // For SVC in the AOM_FULL_SUPERFRAME_DROP): the condition on
    // buffer is checked on current and upper spatial layers.
    for (int i = svc->spatial_layer_id; i < svc->number_spatial_layers; ++i) {
      const int layer = LAYER_IDS_TO_IDX(i, svc->temporal_layer_id,
                                         svc->number_temporal_layers);
      LAYER_CONTEXT *lc = &svc->layer_context[layer];
      PRIMARY_RATE_CONTROL *lrc = &lc->p_rc;
      // Exclude check for layer whose bitrate is 0.
      if (lc->target_bandwidth > 0) {
        const int drop_thresh = cpi->oxcf.rc_cfg.drop_frames_water_mark;
        const int drop_mark_layer =
            (int)(drop_thresh * lrc->optimal_buffer_level / 100);
        if (lrc->buffer_level <= drop_mark_layer) return true;
      }
    }
    return false;
  }
}

int av1_rc_drop_frame(AV1_COMP *cpi) {
  const AV1EncoderConfig *oxcf = &cpi->oxcf;
  RATE_CONTROL *const rc = &cpi->rc;
  PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
#if CONFIG_FPMT_TEST
  const int simulate_parallel_frame =
      cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0 &&
      cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE;
  int64_t buffer_level =
      simulate_parallel_frame ? p_rc->temp_buffer_level : p_rc->buffer_level;
#else
  int64_t buffer_level = p_rc->buffer_level;
#endif
  // Never drop on key frame, or for frame whose base layer is key.
  // If drop_count_consec hits or exceeds max_consec_drop then don't drop.
  if (cpi->common.current_frame.frame_type == KEY_FRAME ||
      (cpi->ppi->use_svc &&
       cpi->svc.layer_context[cpi->svc.temporal_layer_id].is_key_frame) ||
      !oxcf->rc_cfg.drop_frames_water_mark ||
      (rc->max_consec_drop > 0 &&
       rc->drop_count_consec >= rc->max_consec_drop)) {
    return 0;
  } else {
    SVC *svc = &cpi->svc;
    // In the full_superframe framedrop mode for svc, if the previous spatial
    // layer was dropped, drop the current spatial layer.
    if (cpi->ppi->use_svc && svc->spatial_layer_id > 0 &&
        svc->drop_spatial_layer[svc->spatial_layer_id - 1] &&
        svc->framedrop_mode == AOM_FULL_SUPERFRAME_DROP)
      return 1;
    // -1 is passed here for drop_mark since we are checking if
    // buffer goes below 0 (<= -1).
    if (check_buffer_below_thresh(cpi, buffer_level, -1)) {
      // Always drop if buffer is below 0.
      rc->drop_count_consec++;
      return 1;
    } else {
      // If buffer is below drop_mark, for now just drop every other frame
      // (starting with the next frame) until it increases back over drop_mark.
      const int drop_mark = (int)(oxcf->rc_cfg.drop_frames_water_mark *
                                  p_rc->optimal_buffer_level / 100);
      const bool buffer_below_thresh =
          check_buffer_below_thresh(cpi, buffer_level, drop_mark);
      if (!buffer_below_thresh && rc->decimation_factor > 0) {
        --rc->decimation_factor;
      } else if (buffer_below_thresh && rc->decimation_factor == 0) {
        rc->decimation_factor = 1;
      }
      if (rc->decimation_factor > 0) {
        if (rc->decimation_count > 0) {
          --rc->decimation_count;
          rc->drop_count_consec++;
          return 1;
        } else {
          rc->decimation_count = rc->decimation_factor;
          return 0;
        }
      } else {
        rc->decimation_count = 0;
        return 0;
      }
    }
  }
}

static int adjust_q_cbr(const AV1_COMP *cpi, int q, int active_worst_quality,
                        int width, int height) {
  const RATE_CONTROL *const rc = &cpi->rc;
  const PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  const AV1_COMMON *const cm = &cpi->common;
  const SVC *const svc = &cpi->svc;
  const RefreshFrameInfo *const refresh_frame = &cpi->refresh_frame;
  // Flag to indicate previous frame has overshoot, and buffer level
  // for current frame is low (less than ~half of optimal). For such
  // (inter) frames, if the source_sad is non-zero, relax the max_delta_up
  // and clamp applied below.
  const bool overshoot_buffer_low =
      cpi->rc.rc_1_frame == -1 && rc->frame_source_sad > 1000 &&
      p_rc->buffer_level < (p_rc->optimal_buffer_level >> 1) &&
      rc->frames_since_key > 4;
  int max_delta_down;
  int max_delta_up = overshoot_buffer_low ? 120 : 20;
  const int change_avg_frame_bandwidth =
      abs(rc->avg_frame_bandwidth - rc->prev_avg_frame_bandwidth) >
      0.1 * (rc->avg_frame_bandwidth);

  // Set the maximum adjustment down for Q for this frame.
  if (cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ &&
      cpi->cyclic_refresh->apply_cyclic_refresh) {
    // For static screen type content limit the Q drop till the start of the
    // next refresh cycle.
    if (cpi->is_screen_content_type &&
        (cpi->cyclic_refresh->sb_index > cpi->cyclic_refresh->last_sb_index)) {
      max_delta_down = AOMMIN(8, AOMMAX(1, rc->q_1_frame / 32));
    } else {
      max_delta_down = AOMMIN(16, AOMMAX(1, rc->q_1_frame / 8));
    }
    if (!cpi->ppi->use_svc && cpi->is_screen_content_type) {
      // Link max_delta_up to max_delta_down and buffer status.
      if (p_rc->buffer_level > p_rc->optimal_buffer_level) {
        max_delta_up = AOMMAX(4, max_delta_down);
      } else if (!overshoot_buffer_low) {
        max_delta_up = AOMMAX(8, max_delta_down);
      }
    }
  } else {
    max_delta_down = (cpi->is_screen_content_type)
                         ? AOMMIN(8, AOMMAX(1, rc->q_1_frame / 16))
                         : AOMMIN(16, AOMMAX(1, rc->q_1_frame / 8));
  }
  // For screen static content with stable buffer level: relax the
  // limit on max_delta_down and apply bias qp, based on buffer fullness.
  // Only for high speeds levels for now to avoid bdrate regression.
  if (cpi->sf.rt_sf.rc_faster_convergence_static == 1 &&
      cpi->sf.rt_sf.check_scene_detection && rc->frame_source_sad == 0 &&
      rc->static_since_last_scene_change &&
      p_rc->buffer_level > (p_rc->optimal_buffer_level >> 1) &&
      cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ &&
      cpi->cyclic_refresh->counter_encode_maxq_scene_change > 4) {
    int qp_delta = 32;
    int qp_bias = 16;
    if (p_rc->buffer_level > p_rc->optimal_buffer_level) {
      qp_delta = 60;
      qp_bias = 32;
    }
    if (cpi->rc.rc_1_frame == 1) q = q - qp_bias;
    max_delta_down = AOMMAX(max_delta_down, qp_delta);
    max_delta_up = AOMMIN(max_delta_up, 4);
  }

  // If resolution changes or avg_frame_bandwidth significantly changed,
  // then set this flag to indicate change in target bits per macroblock.
  const int change_target_bits_mb =
      cm->prev_frame &&
      (width != cm->prev_frame->width || height != cm->prev_frame->height ||
       change_avg_frame_bandwidth);
  // Apply some control/clamp to QP under certain conditions.
  // Delay the use of the clamping for svc until after num_temporal_layers,
  // to make they have been set for each temporal layer.
  // Check for rc->q_1/2_frame > 0 in case they have not been set due to
  // dropped frames.
  if (!frame_is_intra_only(cm) && rc->frames_since_key > 1 &&
      rc->q_1_frame > 0 && rc->q_2_frame > 0 &&
      (!cpi->ppi->use_svc ||
       svc->current_superframe > (unsigned int)svc->number_temporal_layers) &&
      !change_target_bits_mb && !cpi->rc.rtc_external_ratectrl &&
      (!cpi->oxcf.rc_cfg.gf_cbr_boost_pct ||
       !(refresh_frame->alt_ref_frame || refresh_frame->golden_frame))) {
    // If in the previous two frames we have seen both overshoot and undershoot
    // clamp Q between the two.
    if (rc->rc_1_frame * rc->rc_2_frame == -1 &&
        rc->q_1_frame != rc->q_2_frame && !overshoot_buffer_low) {
      int qclamp = clamp(q, AOMMIN(rc->q_1_frame, rc->q_2_frame),
                         AOMMAX(rc->q_1_frame, rc->q_2_frame));
      // If the previous frame had overshoot and the current q needs to
      // increase above the clamped value, reduce the clamp for faster reaction
      // to overshoot.
      if (cpi->rc.rc_1_frame == -1 && q > qclamp && rc->frames_since_key > 10)
        q = (q + qclamp) >> 1;
      else
        q = qclamp;
    }
    // Adjust Q base on source content change from scene detection.
    if (cpi->sf.rt_sf.check_scene_detection && rc->prev_avg_source_sad > 0 &&
        rc->frames_since_key > 10 && rc->frame_source_sad > 0 &&
        !cpi->rc.rtc_external_ratectrl) {
      const int bit_depth = cm->seq_params->bit_depth;
      double delta =
          (double)rc->avg_source_sad / (double)rc->prev_avg_source_sad - 1.0;
      // Push Q downwards if content change is decreasing and buffer level
      // is stable (at least 1/4-optimal level), so not overshooting. Do so
      // only for high Q to avoid excess overshoot.
      // Else reduce decrease in Q from previous frame if content change is
      // increasing and buffer is below max (so not undershooting).
      if (delta < 0.0 &&
          p_rc->buffer_level > (p_rc->optimal_buffer_level >> 2) &&
          q > (rc->worst_quality >> 1)) {
        double q_adj_factor = 1.0 + 0.5 * tanh(4.0 * delta);
        double q_val = av1_convert_qindex_to_q(q, bit_depth);
        q += av1_compute_qdelta(rc, q_val, q_val * q_adj_factor, bit_depth);
      } else if (rc->q_1_frame - q > 0 && delta > 0.1 &&
                 p_rc->buffer_level < AOMMIN(p_rc->maximum_buffer_size,
                                             p_rc->optimal_buffer_level << 1)) {
        q = (3 * q + rc->q_1_frame) >> 2;
      }
    }
    // Limit the decrease in Q from previous frame.
    if (rc->q_1_frame - q > max_delta_down) q = rc->q_1_frame - max_delta_down;
    // Limit the increase in Q from previous frame.
    else if (q - rc->q_1_frame > max_delta_up)
      q = rc->q_1_frame + max_delta_up;
  }
  // Adjustment for temporal layers.
  if (svc->number_temporal_layers > 1 && svc->spatial_layer_id == 0 &&
      !change_target_bits_mb && !cpi->rc.rtc_external_ratectrl &&
      cpi->oxcf.resize_cfg.resize_mode != RESIZE_DYNAMIC) {
    if (svc->temporal_layer_id > 0) {
      // Constrain enhancement relative to the previous base TL0.
      // Get base temporal layer TL0.
      const int layer = LAYER_IDS_TO_IDX(0, 0, svc->number_temporal_layers);
      LAYER_CONTEXT *lc = &svc->layer_context[layer];
      // lc->rc.avg_frame_bandwidth and lc->p_rc.last_q correspond to the
      // last TL0 frame.
      const int last_qindex_tl0 =
          rc->frames_since_key < svc->number_temporal_layers
              ? lc->p_rc.last_q[KEY_FRAME]
              : lc->p_rc.last_q[INTER_FRAME];
      if (rc->avg_frame_bandwidth < lc->rc.avg_frame_bandwidth &&
          q < last_qindex_tl0 - 4)
        q = last_qindex_tl0 - 4;
    } else if (cpi->svc.temporal_layer_id == 0 && !frame_is_intra_only(cm) &&
               p_rc->buffer_level > (p_rc->optimal_buffer_level >> 2) &&
               rc->frame_source_sad < 100000) {
      // Push base TL0 Q down if buffer is stable and frame_source_sad
      // is below threshold.
      int delta = (svc->number_temporal_layers == 2) ? 4 : 10;
      q = q - delta;
    }
  }
  // For non-svc (single layer): if resolution has increased push q closer
  // to the active_worst to avoid excess overshoot.
  if (!cpi->ppi->use_svc && cm->prev_frame &&
      (width * height > 1.5 * cm->prev_frame->width * cm->prev_frame->height))
    q = (q + active_worst_quality) >> 1;
  // For single layer RPS: Bias Q based on distance of closest reference.
  if (cpi->ppi->rtc_ref.bias_recovery_frame) {
    const int min_dist = av1_svc_get_min_ref_dist(cpi);
    q = q - AOMMIN(min_dist, 20);
  }
  return AOMMAX(AOMMIN(q, cpi->rc.worst_quality), cpi->rc.best_quality);
}

static const RATE_FACTOR_LEVEL rate_factor_levels[FRAME_UPDATE_TYPES] = {
  KF_STD,        // KF_UPDATE
  INTER_NORMAL,  // LF_UPDATE
  GF_ARF_STD,    // GF_UPDATE
  GF_ARF_STD,    // ARF_UPDATE
  INTER_NORMAL,  // OVERLAY_UPDATE
  INTER_NORMAL,  // INTNL_OVERLAY_UPDATE
  GF_ARF_LOW,    // INTNL_ARF_UPDATE
};

static RATE_FACTOR_LEVEL get_rate_factor_level(const GF_GROUP *const gf_group,
                                               int gf_frame_index) {
  const FRAME_UPDATE_TYPE update_type = gf_group->update_type[gf_frame_index];
  assert(update_type < FRAME_UPDATE_TYPES);
  return rate_factor_levels[update_type];
}

/*!\brief Gets a rate vs Q correction factor
 *
 * This function returns the current value of a correction factor used to
 * dynamically adjust the relationship between Q and the expected number
 * of bits for the frame.
 *
 * \ingroup rate_control
 * \param[in]   cpi                   Top level encoder instance structure
 * \param[in]   width                 Frame width
 * \param[in]   height                Frame height
 *
 * \return Returns a correction factor for the current frame
 */
static double get_rate_correction_factor(const AV1_COMP *cpi, int width,
                                         int height) {
  const RATE_CONTROL *const rc = &cpi->rc;
  const PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  const RefreshFrameInfo *const refresh_frame = &cpi->refresh_frame;
  double rcf;
  double rate_correction_factors_kfstd;
  double rate_correction_factors_gfarfstd;
  double rate_correction_factors_internormal;

  rate_correction_factors_kfstd =
      (cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0)
          ? rc->frame_level_rate_correction_factors[KF_STD]
          : p_rc->rate_correction_factors[KF_STD];
  rate_correction_factors_gfarfstd =
      (cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0)
          ? rc->frame_level_rate_correction_factors[GF_ARF_STD]
          : p_rc->rate_correction_factors[GF_ARF_STD];
  rate_correction_factors_internormal =
      (cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0)
          ? rc->frame_level_rate_correction_factors[INTER_NORMAL]
          : p_rc->rate_correction_factors[INTER_NORMAL];

  if (cpi->common.current_frame.frame_type == KEY_FRAME) {
    rcf = rate_correction_factors_kfstd;
  } else if (is_stat_consumption_stage(cpi)) {
    const RATE_FACTOR_LEVEL rf_lvl =
        get_rate_factor_level(&cpi->ppi->gf_group, cpi->gf_frame_index);
    double rate_correction_factors_rflvl =
        (cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0)
            ? rc->frame_level_rate_correction_factors[rf_lvl]
            : p_rc->rate_correction_factors[rf_lvl];
    rcf = rate_correction_factors_rflvl;
  } else {
    if ((refresh_frame->alt_ref_frame || refresh_frame->golden_frame) &&
        !rc->is_src_frame_alt_ref && !cpi->ppi->use_svc &&
        (cpi->oxcf.rc_cfg.mode != AOM_CBR ||
         cpi->oxcf.rc_cfg.gf_cbr_boost_pct > 20))
      rcf = rate_correction_factors_gfarfstd;
    else
      rcf = rate_correction_factors_internormal;
  }
  rcf *= resize_rate_factor(&cpi->oxcf.frm_dim_cfg, width, height);
  return fclamp(rcf, MIN_BPB_FACTOR, MAX_BPB_FACTOR);
}

/*!\brief Sets a rate vs Q correction factor
 *
 * This function updates the current value of a correction factor used to
 * dynamically adjust the relationship between Q and the expected number
 * of bits for the frame.
 *
 * \ingroup rate_control
 * \param[in]   cpi                   Top level encoder instance structure
 * \param[in]   is_encode_stage       Indicates if recode loop or post-encode
 * \param[in]   factor                New correction factor
 * \param[in]   width                 Frame width
 * \param[in]   height                Frame height
 *
 * \remark Updates the rate correction factor for the
 *         current frame type in cpi->rc.
 */
static void set_rate_correction_factor(AV1_COMP *cpi, int is_encode_stage,
                                       double factor, int width, int height) {
  RATE_CONTROL *const rc = &cpi->rc;
  PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  const RefreshFrameInfo *const refresh_frame = &cpi->refresh_frame;
  int update_default_rcf = 1;
  // Normalize RCF to account for the size-dependent scaling factor.
  factor /= resize_rate_factor(&cpi->oxcf.frm_dim_cfg, width, height);

  factor = fclamp(factor, MIN_BPB_FACTOR, MAX_BPB_FACTOR);

  if (cpi->common.current_frame.frame_type == KEY_FRAME) {
    p_rc->rate_correction_factors[KF_STD] = factor;
  } else if (is_stat_consumption_stage(cpi)) {
    const RATE_FACTOR_LEVEL rf_lvl =
        get_rate_factor_level(&cpi->ppi->gf_group, cpi->gf_frame_index);
    if (is_encode_stage &&
        cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0) {
      rc->frame_level_rate_correction_factors[rf_lvl] = factor;
      update_default_rcf = 0;
    }
    if (update_default_rcf) p_rc->rate_correction_factors[rf_lvl] = factor;
  } else {
    if ((refresh_frame->alt_ref_frame || refresh_frame->golden_frame) &&
        !rc->is_src_frame_alt_ref && !cpi->ppi->use_svc &&
        (cpi->oxcf.rc_cfg.mode != AOM_CBR ||
         cpi->oxcf.rc_cfg.gf_cbr_boost_pct > 20)) {
      p_rc->rate_correction_factors[GF_ARF_STD] = factor;
    } else {
      if (is_encode_stage &&
          cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0) {
        rc->frame_level_rate_correction_factors[INTER_NORMAL] = factor;
        update_default_rcf = 0;
      }
      if (update_default_rcf)
        p_rc->rate_correction_factors[INTER_NORMAL] = factor;
    }
  }
}

void av1_rc_update_rate_correction_factors(AV1_COMP *cpi, int is_encode_stage,
                                           int width, int height) {
  const AV1_COMMON *const cm = &cpi->common;
  double correction_factor = 1.0;
  double rate_correction_factor =
      get_rate_correction_factor(cpi, width, height);
  double adjustment_limit;
  int projected_size_based_on_q = 0;
  int cyclic_refresh_active =
      cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ && cpi->common.seg.enabled;

  // Do not update the rate factors for arf overlay frames.
  if (cpi->rc.is_src_frame_alt_ref) return;

  // Don't update rate correction factors here on scene changes as
  // it is already reset in av1_encodedframe_overshoot_cbr(),
  // but reset variables related to previous frame q and size.
  // Note that the counter of frames since the last scene change
  // is only valid when cyclic refresh mode is enabled and that
  // this break out only applies to scene changes that are not
  // recorded as INTRA only key frames.
  // Note that av1_encodedframe_overshoot_cbr() is only entered
  // if cpi->sf.rt_sf.overshoot_detection_cbr == FAST_DETECTION_MAXQ
  // and cpi->rc.high_source_sad = 1.
  if ((cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ) &&
      (cpi->sf.rt_sf.overshoot_detection_cbr == FAST_DETECTION_MAXQ) &&
      cpi->rc.high_source_sad &&
      (cpi->cyclic_refresh->counter_encode_maxq_scene_change == 0) &&
      !frame_is_intra_only(cm) && !cpi->ppi->use_svc) {
    cpi->rc.q_2_frame = cm->quant_params.base_qindex;
    cpi->rc.q_1_frame = cm->quant_params.base_qindex;
    cpi->rc.rc_2_frame = 0;
    cpi->rc.rc_1_frame = 0;
    return;
  }

  // Clear down mmx registers to allow floating point in what follows

  // Work out how big we would have expected the frame to be at this Q given
  // the current correction factor.
  // Stay in double to avoid int overflow when values are large
  if (cyclic_refresh_active) {
    projected_size_based_on_q =
        av1_cyclic_refresh_estimate_bits_at_q(cpi, rate_correction_factor);
  } else {
    projected_size_based_on_q = av1_estimate_bits_at_q(
        cpi, cm->quant_params.base_qindex, rate_correction_factor);
  }
  // Work out a size correction factor.
  if (projected_size_based_on_q > FRAME_OVERHEAD_BITS)
    correction_factor = (double)cpi->rc.projected_frame_size /
                        (double)projected_size_based_on_q;

  // Clamp correction factor to prevent anything too extreme
  correction_factor = AOMMAX(correction_factor, 0.25);

  cpi->rc.q_2_frame = cpi->rc.q_1_frame;
  cpi->rc.q_1_frame = cm->quant_params.base_qindex;
  cpi->rc.rc_2_frame = cpi->rc.rc_1_frame;
  if (correction_factor > 1.1)
    cpi->rc.rc_1_frame = -1;
  else if (correction_factor < 0.9)
    cpi->rc.rc_1_frame = 1;
  else
    cpi->rc.rc_1_frame = 0;

  // Decide how heavily to dampen the adjustment
  if (correction_factor > 0.0) {
    if (cpi->is_screen_content_type) {
      adjustment_limit =
          0.25 + 0.5 * AOMMIN(0.5, fabs(log10(correction_factor)));
    } else {
      adjustment_limit =
          0.25 + 0.75 * AOMMIN(0.5, fabs(log10(correction_factor)));
    }
  } else {
    adjustment_limit = 0.75;
  }

  // Adjustment to delta Q and number of blocks updated in cyclic refresh
  // based on over or under shoot of target in current frame.
  if (cyclic_refresh_active && cpi->rc.this_frame_target > 0) {
    CYCLIC_REFRESH *const cr = cpi->cyclic_refresh;
    if (correction_factor > 1.25) {
      cr->percent_refresh_adjustment =
          AOMMAX(cr->percent_refresh_adjustment - 1, -5);
      cr->rate_ratio_qdelta_adjustment =
          AOMMAX(cr->rate_ratio_qdelta_adjustment - 0.05, -0.0);
    } else if (correction_factor < 0.5) {
      cr->percent_refresh_adjustment =
          AOMMIN(cr->percent_refresh_adjustment + 1, 5);
      cr->rate_ratio_qdelta_adjustment =
          AOMMIN(cr->rate_ratio_qdelta_adjustment + 0.05, 0.25);
    }
  }

  if (correction_factor > 1.01) {
    // We are not already at the worst allowable quality
    correction_factor = (1.0 + ((correction_factor - 1.0) * adjustment_limit));
    rate_correction_factor = rate_correction_factor * correction_factor;
    // Keep rate_correction_factor within limits
    if (rate_correction_factor > MAX_BPB_FACTOR)
      rate_correction_factor = MAX_BPB_FACTOR;
  } else if (correction_factor < 0.99) {
    // We are not already at the best allowable quality
    correction_factor = 1.0 / correction_factor;
    correction_factor = (1.0 + ((correction_factor - 1.0) * adjustment_limit));
    correction_factor = 1.0 / correction_factor;

    rate_correction_factor = rate_correction_factor * correction_factor;

    // Keep rate_correction_factor within limits
    if (rate_correction_factor < MIN_BPB_FACTOR)
      rate_correction_factor = MIN_BPB_FACTOR;
  }

  set_rate_correction_factor(cpi, is_encode_stage, rate_correction_factor,
                             width, height);
}

// Calculate rate for the given 'q'.
static int get_bits_per_mb(const AV1_COMP *cpi, int use_cyclic_refresh,
                           double correction_factor, int q) {
  const AV1_COMMON *const cm = &cpi->common;
  return use_cyclic_refresh
             ? av1_cyclic_refresh_rc_bits_per_mb(cpi, q, correction_factor)
             : av1_rc_bits_per_mb(cpi, cm->current_frame.frame_type, q,
                                  correction_factor,
                                  cpi->sf.hl_sf.accurate_bit_estimate);
}

/*!\brief Searches for a Q index value predicted to give an average macro
 * block rate closest to the target value.
 *
 * Similar to find_qindex_by_rate() function, but returns a q index with a
 * rate just above or below the desired rate, depending on which of the two
 * rates is closer to the desired rate.
 * Also, respects the selected aq_mode when computing the rate.
 *
 * \ingroup rate_control
 * \param[in]   desired_bits_per_mb   Target bits per mb
 * \param[in]   cpi                   Top level encoder instance structure
 * \param[in]   correction_factor     Current Q to rate correction factor
 * \param[in]   best_qindex           Min allowed Q value.
 * \param[in]   worst_qindex          Max allowed Q value.
 *
 * \return Returns a correction factor for the current frame
 */
static int find_closest_qindex_by_rate(int desired_bits_per_mb,
                                       const AV1_COMP *cpi,
                                       double correction_factor,
                                       int best_qindex, int worst_qindex) {
  const int use_cyclic_refresh = cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ &&
                                 cpi->cyclic_refresh->apply_cyclic_refresh;

  // Find 'qindex' based on 'desired_bits_per_mb'.
  assert(best_qindex <= worst_qindex);
  int low = best_qindex;
  int high = worst_qindex;
  while (low < high) {
    const int mid = (low + high) >> 1;
    const int mid_bits_per_mb =
        get_bits_per_mb(cpi, use_cyclic_refresh, correction_factor, mid);
    if (mid_bits_per_mb > desired_bits_per_mb) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  assert(low == high);

  // Calculate rate difference of this q index from the desired rate.
  const int curr_q = low;
  const int curr_bits_per_mb =
      get_bits_per_mb(cpi, use_cyclic_refresh, correction_factor, curr_q);
  const int curr_bit_diff = (curr_bits_per_mb <= desired_bits_per_mb)
                                ? desired_bits_per_mb - curr_bits_per_mb
                                : INT_MAX;
  assert((curr_bit_diff != INT_MAX && curr_bit_diff >= 0) ||
         curr_q == worst_qindex);

  // Calculate rate difference for previous q index too.
  const int prev_q = curr_q - 1;
  int prev_bit_diff;
  if (curr_bit_diff == INT_MAX || curr_q == best_qindex) {
    prev_bit_diff = INT_MAX;
  } else {
    const int prev_bits_per_mb =
        get_bits_per_mb(cpi, use_cyclic_refresh, correction_factor, prev_q);
    assert(prev_bits_per_mb > desired_bits_per_mb);
    prev_bit_diff = prev_bits_per_mb - desired_bits_per_mb;
  }

  // Pick one of the two q indices, depending on which one has rate closer to
  // the desired rate.
  return (curr_bit_diff <= prev_bit_diff) ? curr_q : prev_q;
}

int av1_rc_regulate_q(const AV1_COMP *cpi, int target_bits_per_frame,
                      int active_best_quality, int active_worst_quality,
                      int width, int height) {
  const int MBs = av1_get_MBs(width, height);
  const double correction_factor =
      get_rate_correction_factor(cpi, width, height);
  const int target_bits_per_mb =
      (int)(((uint64_t)target_bits_per_frame << BPER_MB_NORMBITS) / MBs);

  int q =
      find_closest_qindex_by_rate(target_bits_per_mb, cpi, correction_factor,
                                  active_best_quality, active_worst_quality);
  if (cpi->oxcf.rc_cfg.mode == AOM_CBR && has_no_stats_stage(cpi))
    return adjust_q_cbr(cpi, q, active_worst_quality, width, height);

  return q;
}

static int get_active_quality(int q, int gfu_boost, int low, int high,
                              int *low_motion_minq, int *high_motion_minq) {
  if (gfu_boost > high) {
    return low_motion_minq[q];
  } else if (gfu_boost < low) {
    return high_motion_minq[q];
  } else {
    const int gap = high - low;
    const int offset = high - gfu_boost;
    const int qdiff = high_motion_minq[q] - low_motion_minq[q];
    const int adjustment = ((offset * qdiff) + (gap >> 1)) / gap;
    return low_motion_minq[q] + adjustment;
  }
}

static int get_kf_active_quality(const PRIMARY_RATE_CONTROL *const p_rc, int q,
                                 aom_bit_depth_t bit_depth) {
  int *kf_low_motion_minq;
  int *kf_high_motion_minq;
  ASSIGN_MINQ_TABLE(bit_depth, kf_low_motion_minq);
  ASSIGN_MINQ_TABLE(bit_depth, kf_high_motion_minq);
  return get_active_quality(q, p_rc->kf_boost, kf_low, kf_high,
                            kf_low_motion_minq, kf_high_motion_minq);
}

static int get_gf_active_quality_no_rc(int gfu_boost, int q,
                                       aom_bit_depth_t bit_depth) {
  int *arfgf_low_motion_minq;
  int *arfgf_high_motion_minq;
  ASSIGN_MINQ_TABLE(bit_depth, arfgf_low_motion_minq);
  ASSIGN_MINQ_TABLE(bit_depth, arfgf_high_motion_minq);
  return get_active_quality(q, gfu_boost, gf_low, gf_high,
                            arfgf_low_motion_minq, arfgf_high_motion_minq);
}

static int get_gf_active_quality(const PRIMARY_RATE_CONTROL *const p_rc, int q,
                                 aom_bit_depth_t bit_depth) {
  return get_gf_active_quality_no_rc(p_rc->gfu_boost, q, bit_depth);
}

static int get_gf_high_motion_quality(int q, aom_bit_depth_t bit_depth) {
  int *arfgf_high_motion_minq;
  ASSIGN_MINQ_TABLE(bit_depth, arfgf_high_motion_minq);
  return arfgf_high_motion_minq[q];
}

static int calc_active_worst_quality_no_stats_vbr(const AV1_COMP *cpi) {
  const RATE_CONTROL *const rc = &cpi->rc;
  const PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  const RefreshFrameInfo *const refresh_frame = &cpi->refresh_frame;
  const unsigned int curr_frame = cpi->common.current_frame.frame_number;
  int active_worst_quality;
  int last_q_key_frame;
  int last_q_inter_frame;
#if CONFIG_FPMT_TEST
  const int simulate_parallel_frame =
      cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0 &&
      cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE;
  last_q_key_frame = simulate_parallel_frame ? p_rc->temp_last_q[KEY_FRAME]
                                             : p_rc->last_q[KEY_FRAME];
  last_q_inter_frame = simulate_parallel_frame ? p_rc->temp_last_q[INTER_FRAME]
                                               : p_rc->last_q[INTER_FRAME];
#else
  last_q_key_frame = p_rc->last_q[KEY_FRAME];
  last_q_inter_frame = p_rc->last_q[INTER_FRAME];
#endif

  if (cpi->common.current_frame.frame_type == KEY_FRAME) {
    active_worst_quality =
        curr_frame == 0 ? rc->worst_quality : last_q_key_frame * 2;
  } else {
    if (!rc->is_src_frame_alt_ref &&
        (refresh_frame->golden_frame || refresh_frame->bwd_ref_frame ||
         refresh_frame->alt_ref_frame)) {
      active_worst_quality =
          curr_frame == 1 ? last_q_key_frame * 5 / 4 : last_q_inter_frame;
    } else {
      active_worst_quality =
          curr_frame == 1 ? last_q_key_frame * 2 : last_q_inter_frame * 2;
    }
  }
  return AOMMIN(active_worst_quality, rc->worst_quality);
}

// Adjust active_worst_quality level based on buffer level.
static int calc_active_worst_quality_no_stats_cbr(const AV1_COMP *cpi) {
  // Adjust active_worst_quality: If buffer is above the optimal/target level,
  // bring active_worst_quality down depending on fullness of buffer.
  // If buffer is below the optimal level, let the active_worst_quality go from
  // ambient Q (at buffer = optimal level) to worst_quality level
  // (at buffer = critical level).
  const AV1_COMMON *const cm = &cpi->common;
  const RATE_CONTROL *rc = &cpi->rc;
  const PRIMARY_RATE_CONTROL *p_rc = &cpi->ppi->p_rc;
  const SVC *const svc = &cpi->svc;
  unsigned int num_frames_weight_key = 5 * cpi->svc.number_temporal_layers;
  // Buffer level below which we push active_worst to worst_quality.
  int64_t critical_level = p_rc->optimal_buffer_level >> 3;
  int64_t buff_lvl_step = 0;
  int adjustment = 0;
  int active_worst_quality;
  int ambient_qp;
  if (frame_is_intra_only(cm)) return rc->worst_quality;
  // For ambient_qp we use minimum of avg_frame_qindex[KEY_FRAME/INTER_FRAME]
  // for the first few frames following key frame. These are both initialized
  // to worst_quality and updated with (3/4, 1/4) average in postencode_update.
  // So for first few frames following key, the qp of that key frame is weighted
  // into the active_worst_quality setting. For SVC the key frame should
  // correspond to layer (0, 0), so use that for layer context.
  int avg_qindex_key = p_rc->avg_frame_qindex[KEY_FRAME];
  if (svc->number_temporal_layers > 1) {
    int layer = LAYER_IDS_TO_IDX(0, 0, svc->number_temporal_layers);
    const LAYER_CONTEXT *lc = &svc->layer_context[layer];
    const PRIMARY_RATE_CONTROL *const lp_rc = &lc->p_rc;
    avg_qindex_key =
        AOMMIN(lp_rc->avg_frame_qindex[KEY_FRAME], lp_rc->last_q[KEY_FRAME]);
  }
  if (svc->temporal_layer_id > 0 &&
      rc->frames_since_key < 2 * svc->number_temporal_layers) {
    ambient_qp = avg_qindex_key;
  } else {
    ambient_qp =
        (cm->current_frame.frame_number < num_frames_weight_key)
            ? AOMMIN(p_rc->avg_frame_qindex[INTER_FRAME], avg_qindex_key)
            : p_rc->avg_frame_qindex[INTER_FRAME];
  }
  ambient_qp = AOMMIN(rc->worst_quality, ambient_qp);

  if (p_rc->buffer_level > p_rc->optimal_buffer_level) {
    // Adjust down.
    int max_adjustment_down;  // Maximum adjustment down for Q

    if (cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ && !cpi->ppi->use_svc &&
        (cpi->oxcf.tune_cfg.content == AOM_CONTENT_SCREEN)) {
      active_worst_quality = AOMMIN(rc->worst_quality, ambient_qp);
      max_adjustment_down = AOMMIN(4, active_worst_quality / 16);
    } else {
      active_worst_quality = AOMMIN(rc->worst_quality, ambient_qp * 5 / 4);
      max_adjustment_down = active_worst_quality / 3;
    }

    if (max_adjustment_down) {
      buff_lvl_step =
          ((p_rc->maximum_buffer_size - p_rc->optimal_buffer_level) /
           max_adjustment_down);
      if (buff_lvl_step)
        adjustment = (int)((p_rc->buffer_level - p_rc->optimal_buffer_level) /
                           buff_lvl_step);
      active_worst_quality -= adjustment;
    }
  } else if (p_rc->buffer_level > critical_level) {
    // Adjust up from ambient Q.
    active_worst_quality = AOMMIN(rc->worst_quality, ambient_qp);
    if (critical_level) {
      buff_lvl_step = (p_rc->optimal_buffer_level - critical_level);
      if (buff_lvl_step) {
        adjustment = (int)((rc->worst_quality - ambient_qp) *
                           (p_rc->optimal_buffer_level - p_rc->buffer_level) /
                           buff_lvl_step);
      }
      active_worst_quality += adjustment;
    }
  } else {
    // Set to worst_quality if buffer is below critical level.
    active_worst_quality = rc->worst_quality;
  }
  return active_worst_quality;
}

// Calculate the active_best_quality level.
static int calc_active_best_quality_no_stats_cbr(const AV1_COMP *cpi,
                                                 int active_worst_quality,
                                                 int width, int height) {
  const AV1_COMMON *const cm = &cpi->common;
  const RATE_CONTROL *const rc = &cpi->rc;
  const PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  const RefreshFrameInfo *const refresh_frame = &cpi->refresh_frame;
  const CurrentFrame *const current_frame = &cm->current_frame;
  int *rtc_minq;
  const int bit_depth = cm->seq_params->bit_depth;
  int active_best_quality = rc->best_quality;
  ASSIGN_MINQ_TABLE(bit_depth, rtc_minq);

  if (frame_is_intra_only(cm)) {
    // Handle the special case for key frames forced when we have reached
    // the maximum key frame interval. Here force the Q to a range
    // based on the ambient Q to reduce the risk of popping.
    if (p_rc->this_key_frame_forced) {
      int qindex = p_rc->last_boosted_qindex;
      double last_boosted_q = av1_convert_qindex_to_q(qindex, bit_depth);
      int delta_qindex = av1_compute_qdelta(rc, last_boosted_q,
                                            (last_boosted_q * 0.75), bit_depth);
      active_best_quality = AOMMAX(qindex + delta_qindex, rc->best_quality);
    } else if (current_frame->frame_number > 0) {
      // not first frame of one pass and kf_boost is set
      double q_adj_factor = 1.0;
      double q_val;
      active_best_quality = get_kf_active_quality(
          p_rc, p_rc->avg_frame_qindex[KEY_FRAME], bit_depth);
      // Allow somewhat lower kf minq with small image formats.
      if ((width * height) <= (352 * 288)) {
        q_adj_factor -= 0.25;
      }
      // Convert the adjustment factor to a qindex delta
      // on active_best_quality.
      q_val = av1_convert_qindex_to_q(active_best_quality, bit_depth);
      active_best_quality +=
          av1_compute_qdelta(rc, q_val, q_val * q_adj_factor, bit_depth);
    }
  } else if (!rc->is_src_frame_alt_ref && !cpi->ppi->use_svc &&
             cpi->oxcf.rc_cfg.gf_cbr_boost_pct &&
             (refresh_frame->golden_frame || refresh_frame->alt_ref_frame)) {
    // Use the lower of active_worst_quality and recent
    // average Q as basis for GF/ARF best Q limit unless last frame was
    // a key frame.
    int q = active_worst_quality;
    if (rc->frames_since_key > 1 &&
        p_rc->avg_frame_qindex[INTER_FRAME] < active_worst_quality) {
      q = p_rc->avg_frame_qindex[INTER_FRAME];
    }
    active_best_quality = get_gf_active_quality(p_rc, q, bit_depth);
  } else {
    // Use the lower of active_worst_quality and recent/average Q.
    FRAME_TYPE frame_type =
        (current_frame->frame_number > 1) ? INTER_FRAME : KEY_FRAME;
    if (p_rc->avg_frame_qindex[frame_type] < active_worst_quality)
      active_best_quality = rtc_minq[p_rc->avg_frame_qindex[frame_type]];
    else
      active_best_quality = rtc_minq[active_worst_quality];
  }
  return active_best_quality;
}

#if RT_PASSIVE_STRATEGY
static int get_q_passive_strategy(const AV1_COMP *const cpi,
                                  const int q_candidate, const int threshold) {
  const AV1_COMMON *const cm = &cpi->common;
  const PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  const CurrentFrame *const current_frame = &cm->current_frame;
  int sum = 0;
  int count = 0;
  int i = 1;
  while (i < MAX_Q_HISTORY) {
    int frame_id = current_frame->frame_number - i;
    if (frame_id <= 0) break;
    sum += p_rc->q_history[frame_id % MAX_Q_HISTORY];
    ++count;
    ++i;
  }
  if (count > 0) {
    const int avg_q = sum / count;
    if (abs(avg_q - q_candidate) <= threshold) return avg_q;
  }
  return q_candidate;
}
#endif  // RT_PASSIVE_STRATEGY

/*!\brief Picks q and q bounds given CBR rate control parameters in \c cpi->rc.
 *
 * Handles the special case when using:
 * - Constant bit-rate mode: \c cpi->oxcf.rc_cfg.mode == \ref AOM_CBR, and
 * - 1-pass encoding without LAP (look-ahead processing), so 1st pass stats are
 * NOT available.
 *
 * \ingroup rate_control
 * \param[in]       cpi          Top level encoder structure
 * \param[in]       width        Coded frame width
 * \param[in]       height       Coded frame height
 * \param[out]      bottom_index Bottom bound for q index (best quality)
 * \param[out]      top_index    Top bound for q index (worst quality)
 * \return Returns selected q index to be used for encoding this frame.
 */
static int rc_pick_q_and_bounds_no_stats_cbr(const AV1_COMP *cpi, int width,
                                             int height, int *bottom_index,
                                             int *top_index) {
  const AV1_COMMON *const cm = &cpi->common;
  const RATE_CONTROL *const rc = &cpi->rc;
  const PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  const CurrentFrame *const current_frame = &cm->current_frame;
  int q;
  int active_worst_quality = calc_active_worst_quality_no_stats_cbr(cpi);
  int active_best_quality = calc_active_best_quality_no_stats_cbr(
      cpi, active_worst_quality, width, height);
  assert(has_no_stats_stage(cpi));
  assert(cpi->oxcf.rc_cfg.mode == AOM_CBR);

  // Clip the active best and worst quality values to limits
  active_best_quality =
      clamp(active_best_quality, rc->best_quality, rc->worst_quality);
  active_worst_quality =
      clamp(active_worst_quality, active_best_quality, rc->worst_quality);

  *top_index = active_worst_quality;
  *bottom_index = active_best_quality;

  // Limit Q range for the adaptive loop.
  if (current_frame->frame_type == KEY_FRAME && !p_rc->this_key_frame_forced &&
      current_frame->frame_number != 0) {
    int qdelta = 0;
    qdelta = av1_compute_qdelta_by_rate(cpi, current_frame->frame_type,
                                        active_worst_quality, 2.0);
    *top_index = active_worst_quality + qdelta;
    *top_index = AOMMAX(*top_index, *bottom_index);
  }

  q = av1_rc_regulate_q(cpi, rc->this_frame_target, active_best_quality,
                        active_worst_quality, width, height);
#if RT_PASSIVE_STRATEGY
  if (current_frame->frame_type != KEY_FRAME &&
      cpi->oxcf.tune_cfg.content == AOM_CONTENT_SCREEN) {
    q = get_q_passive_strategy(cpi, q, 50);
  }
#endif  // RT_PASSIVE_STRATEGY
  if (q > *top_index) {
    // Special case when we are targeting the max allowed rate
    if (rc->this_frame_target >= rc->max_frame_bandwidth)
      *top_index = q;
    else
      q = *top_index;
  }

  assert(*top_index <= rc->worst_quality && *top_index >= rc->best_quality);
  assert(*bottom_index <= rc->worst_quality &&
         *bottom_index >= rc->best_quality);
  assert(q <= rc->worst_quality && q >= rc->best_quality);
  return q;
}

static int gf_group_pyramid_level(const GF_GROUP *gf_group, int gf_index) {
  return gf_group->layer_depth[gf_index];
}

static int get_active_cq_level(const RATE_CONTROL *rc,
                               const PRIMARY_RATE_CONTROL *p_rc,
                               const AV1EncoderConfig *const oxcf,
                               int intra_only, aom_superres_mode superres_mode,
                               int superres_denom) {
  const RateControlCfg *const rc_cfg = &oxcf->rc_cfg;
  static const double cq_adjust_threshold = 0.1;
  int active_cq_level = rc_cfg->cq_level;
  if (rc_cfg->mode == AOM_CQ || rc_cfg->mode == AOM_Q) {
    // printf("Superres %d %d %d = %d\n", superres_denom, intra_only,
    //        rc->frames_to_key, !(intra_only && rc->frames_to_key <= 1));
    if ((superres_mode == AOM_SUPERRES_QTHRESH ||
         superres_mode == AOM_SUPERRES_AUTO) &&
        superres_denom != SCALE_NUMERATOR) {
      int mult = SUPERRES_QADJ_PER_DENOM_KEYFRAME_SOLO;
      if (intra_only && rc->frames_to_key <= 1) {
        mult = 0;
      } else if (intra_only) {
        mult = SUPERRES_QADJ_PER_DENOM_KEYFRAME;
      } else {
        mult = SUPERRES_QADJ_PER_DENOM_ARFFRAME;
      }
      active_cq_level = AOMMAX(
          active_cq_level - ((superres_denom - SCALE_NUMERATOR) * mult), 0);
    }
  }
  if (rc_cfg->mode == AOM_CQ && p_rc->total_target_bits > 0) {
    const double x = (double)p_rc->total_actual_bits / p_rc->total_target_bits;
    if (x < cq_adjust_threshold) {
      active_cq_level = (int)(active_cq_level * x / cq_adjust_threshold);
    }
  }
  return active_cq_level;
}

/*!\brief Picks q and q bounds given non-CBR rate control params in \c cpi->rc.
 *
 * Handles the special case when using:
 * - Any rate control other than constant bit-rate mode:
 * \c cpi->oxcf.rc_cfg.mode != \ref AOM_CBR, and
 * - 1-pass encoding without LAP (look-ahead processing), so 1st pass stats are
 * NOT available.
 *
 * \ingroup rate_control
 * \param[in]       cpi          Top level encoder structure
 * \param[in]       width        Coded frame width
 * \param[in]       height       Coded frame height
 * \param[out]      bottom_index Bottom bound for q index (best quality)
 * \param[out]      top_index    Top bound for q index (worst quality)
 * \return Returns selected q index to be used for encoding this frame.
 */
static int rc_pick_q_and_bounds_no_stats(const AV1_COMP *cpi, int width,
                                         int height, int *bottom_index,
                                         int *top_index) {
  const AV1_COMMON *const cm = &cpi->common;
  const RATE_CONTROL *const rc = &cpi->rc;
  const PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  const CurrentFrame *const current_frame = &cm->current_frame;
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  const RefreshFrameInfo *const refresh_frame = &cpi->refresh_frame;
  const enum aom_rc_mode rc_mode = oxcf->rc_cfg.mode;

  assert(has_no_stats_stage(cpi));
  assert(rc_mode == AOM_VBR ||
         (!USE_UNRESTRICTED_Q_IN_CQ_MODE && rc_mode == AOM_CQ) ||
         rc_mode == AOM_Q);

  const int cq_level =
      get_active_cq_level(rc, p_rc, oxcf, frame_is_intra_only(cm),
                          cpi->superres_mode, cm->superres_scale_denominator);
  const int bit_depth = cm->seq_params->bit_depth;

  int active_best_quality;
  int active_worst_quality = calc_active_worst_quality_no_stats_vbr(cpi);
  int q;
  int *inter_minq;
  ASSIGN_MINQ_TABLE(bit_depth, inter_minq);

  if (frame_is_intra_only(cm)) {
    if (rc_mode == AOM_Q) {
      const int qindex = cq_level;
      const double q_val = av1_convert_qindex_to_q(qindex, bit_depth);
      const int delta_qindex =
          av1_compute_qdelta(rc, q_val, q_val * 0.25, bit_depth);
      active_best_quality = AOMMAX(qindex + delta_qindex, rc->best_quality);
    } else if (p_rc->this_key_frame_forced) {
#if CONFIG_FPMT_TEST
      const int simulate_parallel_frame =
          cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0 &&
          cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE;
      int qindex = simulate_parallel_frame ? p_rc->temp_last_boosted_qindex
                                           : p_rc->last_boosted_qindex;
#else
      int qindex = p_rc->last_boosted_qindex;
#endif
      const double last_boosted_q = av1_convert_qindex_to_q(qindex, bit_depth);
      const int delta_qindex = av1_compute_qdelta(
          rc, last_boosted_q, last_boosted_q * 0.75, bit_depth);
      active_best_quality = AOMMAX(qindex + delta_qindex, rc->best_quality);
    } else {  // not first frame of one pass and kf_boost is set
      double q_adj_factor = 1.0;

      active_best_quality = get_kf_active_quality(
          p_rc, p_rc->avg_frame_qindex[KEY_FRAME], bit_depth);

      // Allow somewhat lower kf minq with small image formats.
      if ((width * height) <= (352 * 288)) {
        q_adj_factor -= 0.25;
      }

      // Convert the adjustment factor to a qindex delta on active_best_quality.
      {
        const double q_val =
            av1_convert_qindex_to_q(active_best_quality, bit_depth);
        active_best_quality +=
            av1_compute_qdelta(rc, q_val, q_val * q_adj_factor, bit_depth);
      }
    }
  } else if (!rc->is_src_frame_alt_ref &&
             (refresh_frame->golden_frame || refresh_frame->alt_ref_frame)) {
    // Use the lower of active_worst_quality and recent
    // average Q as basis for GF/ARF best Q limit unless last frame was
    // a key frame.
    q = (rc->frames_since_key > 1 &&
         p_rc->avg_frame_qindex[INTER_FRAME] < active_worst_quality)
            ? p_rc->avg_frame_qindex[INTER_FRAME]
            : p_rc->avg_frame_qindex[KEY_FRAME];
    // For constrained quality don't allow Q less than the cq level
    if (rc_mode == AOM_CQ) {
      if (q < cq_level) q = cq_level;
      active_best_quality = get_gf_active_quality(p_rc, q, bit_depth);
      // Constrained quality use slightly lower active best.
      active_best_quality = active_best_quality * 15 / 16;
    } else if (rc_mode == AOM_Q) {
      const int qindex = cq_level;
      const double q_val = av1_convert_qindex_to_q(qindex, bit_depth);
      const int delta_qindex =
          (refresh_frame->alt_ref_frame)
              ? av1_compute_qdelta(rc, q_val, q_val * 0.40, bit_depth)
              : av1_compute_qdelta(rc, q_val, q_val * 0.50, bit_depth);
      active_best_quality = AOMMAX(qindex + delta_qindex, rc->best_quality);
    } else {
      active_best_quality = get_gf_active_quality(p_rc, q, bit_depth);
    }
  } else {
    if (rc_mode == AOM_Q) {
      const int qindex = cq_level;
      const double q_val = av1_convert_qindex_to_q(qindex, bit_depth);
      const double delta_rate[FIXED_GF_INTERVAL] = { 0.50, 1.0, 0.85, 1.0,
                                                     0.70, 1.0, 0.85, 1.0 };
      const int delta_qindex = av1_compute_qdelta(
          rc, q_val,
          q_val * delta_rate[current_frame->frame_number % FIXED_GF_INTERVAL],
          bit_depth);
      active_best_quality = AOMMAX(qindex + delta_qindex, rc->best_quality);
    } else {
      // Use the lower of active_worst_quality and recent/average Q.
      active_best_quality =
          (current_frame->frame_number > 1)
              ? inter_minq[p_rc->avg_frame_qindex[INTER_FRAME]]
              : inter_minq[p_rc->avg_frame_qindex[KEY_FRAME]];
      // For the constrained quality mode we don't want
      // q to fall below the cq level.
      if ((rc_mode == AOM_CQ) && (active_best_quality < cq_level)) {
        active_best_quality = cq_level;
      }
    }
  }

  // Clip the active best and worst quality values to limits
  active_best_quality =
      clamp(active_best_quality, rc->best_quality, rc->worst_quality);
  active_worst_quality =
      clamp(active_worst_quality, active_best_quality, rc->worst_quality);

  *top_index = active_worst_quality;
  *bottom_index = active_best_quality;

  // Limit Q range for the adaptive loop.
  {
    int qdelta = 0;
    if (current_frame->frame_type == KEY_FRAME &&
        !p_rc->this_key_frame_forced && current_frame->frame_number != 0) {
      qdelta = av1_compute_qdelta_by_rate(cpi, current_frame->frame_type,
                                          active_worst_quality, 2.0);
    } else if (!rc->is_src_frame_alt_ref &&
               (refresh_frame->golden_frame || refresh_frame->alt_ref_frame)) {
      qdelta = av1_compute_qdelta_by_rate(cpi, current_frame->frame_type,
                                          active_worst_quality, 1.75);
    }
    *top_index = active_worst_quality + qdelta;
    *top_index = AOMMAX(*top_index, *bottom_index);
  }

  if (rc_mode == AOM_Q) {
    q = active_best_quality;
    // Special case code to try and match quality with forced key frames
  } else if ((current_frame->frame_type == KEY_FRAME) &&
             p_rc->this_key_frame_forced) {
#if CONFIG_FPMT_TEST
    const int simulate_parallel_frame =
        cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0 &&
        cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE;
    q = simulate_parallel_frame ? p_rc->temp_last_boosted_qindex
                                : p_rc->last_boosted_qindex;
#else
    q = p_rc->last_boosted_qindex;
#endif
  } else {
    q = av1_rc_regulate_q(cpi, rc->this_frame_target, active_best_quality,
                          active_worst_quality, width, height);
    if (q > *top_index) {
      // Special case when we are targeting the max allowed rate
      if (rc->this_frame_target >= rc->max_frame_bandwidth)
        *top_index = q;
      else
        q = *top_index;
    }
  }

  assert(*top_index <= rc->worst_quality && *top_index >= rc->best_quality);
  assert(*bottom_index <= rc->worst_quality &&
         *bottom_index >= rc->best_quality);
  assert(q <= rc->worst_quality && q >= rc->best_quality);
  return q;
}

static const double arf_layer_deltas[MAX_ARF_LAYERS + 1] = { 2.50, 2.00, 1.75,
                                                             1.50, 1.25, 1.15,
                                                             1.0 };
static int frame_type_qdelta(const AV1_COMP *cpi, int q) {
  const GF_GROUP *const gf_group = &cpi->ppi->gf_group;
  const RATE_FACTOR_LEVEL rf_lvl =
      get_rate_factor_level(gf_group, cpi->gf_frame_index);
  const FRAME_TYPE frame_type = gf_group->frame_type[cpi->gf_frame_index];
  const int arf_layer = AOMMIN(gf_group->layer_depth[cpi->gf_frame_index], 6);
  const double rate_factor =
      (rf_lvl == INTER_NORMAL) ? 1.0 : arf_layer_deltas[arf_layer];

  return av1_compute_qdelta_by_rate(cpi, frame_type, q, rate_factor);
}

// This unrestricted Q selection on CQ mode is useful when testing new features,
// but may lead to Q being out of range on current RC restrictions
#if USE_UNRESTRICTED_Q_IN_CQ_MODE
static int rc_pick_q_and_bounds_no_stats_cq(const AV1_COMP *cpi, int width,
                                            int height, int *bottom_index,
                                            int *top_index) {
  const AV1_COMMON *const cm = &cpi->common;
  const RATE_CONTROL *const rc = &cpi->rc;
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  const int cq_level =
      get_active_cq_level(rc, oxcf, frame_is_intra_only(cm), cpi->superres_mode,
                          cm->superres_scale_denominator);
  const int bit_depth = cm->seq_params->bit_depth;
  const int q = (int)av1_convert_qindex_to_q(cq_level, bit_depth);
  (void)width;
  (void)height;
  assert(has_no_stats_stage(cpi));
  assert(cpi->oxcf.rc_cfg.mode == AOM_CQ);

  *top_index = q;
  *bottom_index = q;

  return q;
}
#endif  // USE_UNRESTRICTED_Q_IN_CQ_MODE

#define STATIC_MOTION_THRESH 95
static void get_intra_q_and_bounds(const AV1_COMP *cpi, int width, int height,
                                   int *active_best, int *active_worst,
                                   int cq_level) {
  const AV1_COMMON *const cm = &cpi->common;
  const RATE_CONTROL *const rc = &cpi->rc;
  const PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  int active_best_quality;
  int active_worst_quality = *active_worst;
  const int bit_depth = cm->seq_params->bit_depth;

  if (rc->frames_to_key <= 1 && oxcf->rc_cfg.mode == AOM_Q) {
    // If the next frame is also a key frame or the current frame is the
    // only frame in the sequence in AOM_Q mode, just use the cq_level
    // as q.
    active_best_quality = cq_level;
    active_worst_quality = cq_level;
  } else if (p_rc->this_key_frame_forced) {
    // Handle the special case for key frames forced when we have reached
    // the maximum key frame interval. Here force the Q to a range
    // based on the ambient Q to reduce the risk of popping.
    double last_boosted_q;
    int delta_qindex;
    int qindex;
#if CONFIG_FPMT_TEST
    const int simulate_parallel_frame =
        cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0 &&
        cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE;
    int last_boosted_qindex = simulate_parallel_frame
                                  ? p_rc->temp_last_boosted_qindex
                                  : p_rc->last_boosted_qindex;
#else
    int last_boosted_qindex = p_rc->last_boosted_qindex;
#endif
    if (is_stat_consumption_stage_twopass(cpi) &&
        cpi->ppi->twopass.last_kfgroup_zeromotion_pct >= STATIC_MOTION_THRESH) {
      qindex = AOMMIN(p_rc->last_kf_qindex, last_boosted_qindex);
      active_best_quality = qindex;
      last_boosted_q = av1_convert_qindex_to_q(qindex, bit_depth);
      delta_qindex = av1_compute_qdelta(rc, last_boosted_q,
                                        last_boosted_q * 1.25, bit_depth);
      active_worst_quality =
          AOMMIN(qindex + delta_qindex, active_worst_quality);
    } else {
      qindex = last_boosted_qindex;
      last_boosted_q = av1_convert_qindex_to_q(qindex, bit_depth);
      delta_qindex = av1_compute_qdelta(rc, last_boosted_q,
                                        last_boosted_q * 0.50, bit_depth);
      active_best_quality = AOMMAX(qindex + delta_qindex, rc->best_quality);
    }
  } else {
    // Not forced keyframe.
    double q_adj_factor = 1.0;
    double q_val;

    // Baseline value derived from active_worst_quality and kf boost.
    active_best_quality =
        get_kf_active_quality(p_rc, active_worst_quality, bit_depth);
    if (cpi->is_screen_content_type) {
      active_best_quality /= 2;
    }

    if (is_stat_consumption_stage_twopass(cpi) &&
        cpi->ppi->twopass.kf_zeromotion_pct >= STATIC_KF_GROUP_THRESH) {
      active_best_quality /= 3;
    }

    // Allow somewhat lower kf minq with small image formats.
    if ((width * height) <= (352 * 288)) {
      q_adj_factor -= 0.25;
    }

    // Make a further adjustment based on the kf zero motion measure.
    if (is_stat_consumption_stage_twopass(cpi))
      q_adj_factor +=
          0.05 - (0.001 * (double)cpi->ppi->twopass.kf_zeromotion_pct);

    // Convert the adjustment factor to a qindex delta
    // on active_best_quality.
    q_val = av1_convert_qindex_to_q(active_best_quality, bit_depth);
    active_best_quality +=
        av1_compute_qdelta(rc, q_val, q_val * q_adj_factor, bit_depth);

    // Tweak active_best_quality for AOM_Q mode when superres is on, as this
    // will be used directly as 'q' later.
    if (oxcf->rc_cfg.mode == AOM_Q &&
        (cpi->superres_mode == AOM_SUPERRES_QTHRESH ||
         cpi->superres_mode == AOM_SUPERRES_AUTO) &&
        cm->superres_scale_denominator != SCALE_NUMERATOR) {
      active_best_quality =
          AOMMAX(active_best_quality -
                     ((cm->superres_scale_denominator - SCALE_NUMERATOR) *
                      SUPERRES_QADJ_PER_DENOM_KEYFRAME),
                 0);
    }
  }
  *active_best = active_best_quality;
  *active_worst = active_worst_quality;
}

static void adjust_active_best_and_worst_quality(const AV1_COMP *cpi,
                                                 const int is_intrl_arf_boost,
                                                 int *active_worst,
                                                 int *active_best) {
  const AV1_COMMON *const cm = &cpi->common;
  const RATE_CONTROL *const rc = &cpi->rc;
  const PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  int active_best_quality = *active_best;
  int active_worst_quality = *active_worst;
#if CONFIG_FPMT_TEST
#endif
  // Extension to max or min Q if undershoot or overshoot is outside
  // the permitted range.
  if (cpi->oxcf.rc_cfg.mode != AOM_Q) {
#if CONFIG_FPMT_TEST
    const int simulate_parallel_frame =
        cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0 &&
        cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE;
    const int extend_minq = simulate_parallel_frame
                                ? p_rc->temp_extend_minq
                                : cpi->ppi->twopass.extend_minq;
    const int extend_maxq = simulate_parallel_frame
                                ? p_rc->temp_extend_maxq
                                : cpi->ppi->twopass.extend_maxq;
    const RefreshFrameInfo *const refresh_frame = &cpi->refresh_frame;
    if (frame_is_intra_only(cm) ||
        (!rc->is_src_frame_alt_ref &&
         (refresh_frame->golden_frame || is_intrl_arf_boost ||
          refresh_frame->alt_ref_frame))) {
      active_best_quality -= extend_minq;
      active_worst_quality += (extend_maxq / 2);
    } else {
      active_best_quality -= extend_minq / 2;
      active_worst_quality += extend_maxq;
    }
#else
    (void)is_intrl_arf_boost;
    active_best_quality -= cpi->ppi->twopass.extend_minq / 8;
    active_worst_quality += cpi->ppi->twopass.extend_maxq / 4;
#endif
  }

#ifndef STRICT_RC
  // Static forced key frames Q restrictions dealt with elsewhere.
  if (!(frame_is_intra_only(cm)) || !p_rc->this_key_frame_forced ||
      (cpi->ppi->twopass.last_kfgroup_zeromotion_pct < STATIC_MOTION_THRESH)) {
    const int qdelta = frame_type_qdelta(cpi, active_worst_quality);
    active_worst_quality =
        AOMMAX(active_worst_quality + qdelta, active_best_quality);
  }
#endif

  // Modify active_best_quality for downscaled normal frames.
  if (av1_frame_scaled(cm) && !frame_is_kf_gf_arf(cpi)) {
    int qdelta = av1_compute_qdelta_by_rate(cpi, cm->current_frame.frame_type,
                                            active_best_quality, 2.0);
    active_best_quality =
        AOMMAX(active_best_quality + qdelta, rc->best_quality);
  }

  active_best_quality =
      clamp(active_best_quality, rc->best_quality, rc->worst_quality);
  active_worst_quality =
      clamp(active_worst_quality, active_best_quality, rc->worst_quality);

  *active_best = active_best_quality;
  *active_worst = active_worst_quality;
}

/*!\brief Gets a Q value to use  for the current frame
 *
 *
 * Selects a Q value from a permitted range that we estimate
 * will result in approximately the target number of bits.
 *
 * \ingroup rate_control
 * \param[in]   cpi                   Top level encoder instance structure
 * \param[in]   width                 Width of frame
 * \param[in]   height                Height of frame
 * \param[in]   active_worst_quality  Max Q allowed
 * \param[in]   active_best_quality   Min Q allowed
 *
 * \return The suggested Q for this frame.
 */
static int get_q(const AV1_COMP *cpi, const int width, const int height,
                 const int active_worst_quality,
                 const int active_best_quality) {
  const AV1_COMMON *const cm = &cpi->common;
  const RATE_CONTROL *const rc = &cpi->rc;
  const PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  int q;
#if CONFIG_FPMT_TEST
  const int simulate_parallel_frame =
      cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0 &&
      cpi->ppi->fpmt_unit_test_cfg;
  int last_boosted_qindex = simulate_parallel_frame
                                ? p_rc->temp_last_boosted_qindex
                                : p_rc->last_boosted_qindex;
#else
  int last_boosted_qindex = p_rc->last_boosted_qindex;
#endif

  if (cpi->oxcf.rc_cfg.mode == AOM_Q ||
      (frame_is_intra_only(cm) && !p_rc->this_key_frame_forced &&
       cpi->ppi->twopass.kf_zeromotion_pct >= STATIC_KF_GROUP_THRESH &&
       rc->frames_to_key > 1)) {
    q = active_best_quality;
    // Special case code to try and match quality with forced key frames.
  } else if (frame_is_intra_only(cm) && p_rc->this_key_frame_forced) {
    // If static since last kf use better of last boosted and last kf q.
    if (cpi->ppi->twopass.last_kfgroup_zeromotion_pct >= STATIC_MOTION_THRESH) {
      q = AOMMIN(p_rc->last_kf_qindex, last_boosted_qindex);
    } else {
      q = AOMMIN(last_boosted_qindex,
                 (active_best_quality + active_worst_quality) / 2);
    }
    q = clamp(q, active_best_quality, active_worst_quality);
  } else {
    q = av1_rc_regulate_q(cpi, rc->this_frame_target, active_best_quality,
                          active_worst_quality, width, height);
    if (q > active_worst_quality) {
      // Special case when we are targeting the max allowed rate.
      if (rc->this_frame_target < rc->max_frame_bandwidth) {
        q = active_worst_quality;
      }
    }
    q = AOMMAX(q, active_best_quality);
  }
  return q;
}

// Returns |active_best_quality| for an inter frame.
// The |active_best_quality| depends on different rate control modes:
// VBR, Q, CQ, CBR.
// The returning active_best_quality could further be adjusted in
// adjust_active_best_and_worst_quality().
static int get_active_best_quality(const AV1_COMP *const cpi,
                                   const int active_worst_quality,
                                   const int cq_level, const int gf_index) {
  const AV1_COMMON *const cm = &cpi->common;
  const int bit_depth = cm->seq_params->bit_depth;
  const RATE_CONTROL *const rc = &cpi->rc;
  const PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  const RefreshFrameInfo *const refresh_frame = &cpi->refresh_frame;
  const GF_GROUP *gf_group = &cpi->ppi->gf_group;
  const enum aom_rc_mode rc_mode = oxcf->rc_cfg.mode;
  int *inter_minq;
  ASSIGN_MINQ_TABLE(bit_depth, inter_minq);
  int active_best_quality = 0;
  const int is_intrl_arf_boost =
      gf_group->update_type[gf_index] == INTNL_ARF_UPDATE;
  int is_leaf_frame =
      !(gf_group->update_type[gf_index] == ARF_UPDATE ||
        gf_group->update_type[gf_index] == GF_UPDATE || is_intrl_arf_boost);

  // TODO(jingning): Consider to rework this hack that covers issues incurred
  // in lightfield setting.
  if (cm->tiles.large_scale) {
    is_leaf_frame = !(refresh_frame->golden_frame ||
                      refresh_frame->alt_ref_frame || is_intrl_arf_boost);
  }
  const int is_overlay_frame = rc->is_src_frame_alt_ref;

  if (is_leaf_frame || is_overlay_frame) {
    if (rc_mode == AOM_Q) return cq_level;

    active_best_quality = inter_minq[active_worst_quality];
    // For the constrained quality mode we don't want
    // q to fall below the cq level.
    if ((rc_mode == AOM_CQ) && (active_best_quality < cq_level)) {
      active_best_quality = cq_level;
    }
    return active_best_quality;
  }

  // Determine active_best_quality for frames that are not leaf or overlay.
  int q = active_worst_quality;
  // Use the lower of active_worst_quality and recent
  // average Q as basis for GF/ARF best Q limit unless last frame was
  // a key frame.
  if (rc->frames_since_key > 1 &&
      p_rc->avg_frame_qindex[INTER_FRAME] < active_worst_quality) {
    q = p_rc->avg_frame_qindex[INTER_FRAME];
  }
  if (rc_mode == AOM_CQ && q < cq_level) q = cq_level;
  active_best_quality = get_gf_active_quality(p_rc, q, bit_depth);
  // Constrained quality use slightly lower active best.
  if (rc_mode == AOM_CQ) active_best_quality = active_best_quality * 15 / 16;
  const int min_boost = get_gf_high_motion_quality(q, bit_depth);
  const int boost = min_boost - active_best_quality;
  active_best_quality = min_boost - (int)(boost * p_rc->arf_boost_factor);
  if (!is_intrl_arf_boost) return active_best_quality;

  if (rc_mode == AOM_Q || rc_mode == AOM_CQ) active_best_quality = p_rc->arf_q;
  int this_height = gf_group_pyramid_level(gf_group, gf_index);
  while (this_height > 1) {
    active_best_quality = (active_best_quality + active_worst_quality + 1) / 2;
    --this_height;
  }
  return active_best_quality;
}

static int rc_pick_q_and_bounds_q_mode(const AV1_COMP *cpi, int width,
                                       int height, int gf_index,
                                       int *bottom_index, int *top_index) {
  const AV1_COMMON *const cm = &cpi->common;
  const RATE_CONTROL *const rc = &cpi->rc;
  const PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  const int cq_level =
      get_active_cq_level(rc, p_rc, oxcf, frame_is_intra_only(cm),
                          cpi->superres_mode, cm->superres_scale_denominator);
  int active_best_quality = 0;
  int active_worst_quality = rc->active_worst_quality;
  int q;

  if (frame_is_intra_only(cm)) {
    get_intra_q_and_bounds(cpi, width, height, &active_best_quality,
                           &active_worst_quality, cq_level);
  } else {
    //  Active best quality limited by previous layer.
    active_best_quality =
        get_active_best_quality(cpi, active_worst_quality, cq_level, gf_index);
  }

  if (cq_level > 0) active_best_quality = AOMMAX(1, active_best_quality);

  *top_index = active_worst_quality;
  *bottom_index = active_best_quality;

  *top_index = AOMMAX(*top_index, rc->best_quality);
  *top_index = AOMMIN(*top_index, rc->worst_quality);

  *bottom_index = AOMMAX(*bottom_index, rc->best_quality);
  *bottom_index = AOMMIN(*bottom_index, rc->worst_quality);

  q = active_best_quality;

  q = AOMMAX(q, rc->best_quality);
  q = AOMMIN(q, rc->worst_quality);

  assert(*top_index <= rc->worst_quality && *top_index >= rc->best_quality);
  assert(*bottom_index <= rc->worst_quality &&
         *bottom_index >= rc->best_quality);
  assert(q <= rc->worst_quality && q >= rc->best_quality);

  return q;
}

/*!\brief Picks q and q bounds given rate control parameters in \c cpi->rc.
 *
 * Handles the general cases not covered by
 * \ref rc_pick_q_and_bounds_no_stats_cbr() and
 * \ref rc_pick_q_and_bounds_no_stats()
 *
 * \ingroup rate_control
 * \param[in]       cpi          Top level encoder structure
 * \param[in]       width        Coded frame width
 * \param[in]       height       Coded frame height
 * \param[in]       gf_index     Index of this frame in the golden frame group
 * \param[out]      bottom_index Bottom bound for q index (best quality)
 * \param[out]      top_index    Top bound for q index (worst quality)
 * \return Returns selected q index to be used for encoding this frame.
 */
static int rc_pick_q_and_bounds(const AV1_COMP *cpi, int width, int height,
                                int gf_index, int *bottom_index,
                                int *top_index) {
  const AV1_COMMON *const cm = &cpi->common;
  const RATE_CONTROL *const rc = &cpi->rc;
  const PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  const RefreshFrameInfo *const refresh_frame = &cpi->refresh_frame;
  const GF_GROUP *gf_group = &cpi->ppi->gf_group;
  assert(IMPLIES(has_no_stats_stage(cpi),
                 cpi->oxcf.rc_cfg.mode == AOM_Q &&
                     gf_group->update_type[gf_index] != ARF_UPDATE));
  const int cq_level =
      get_active_cq_level(rc, p_rc, oxcf, frame_is_intra_only(cm),
                          cpi->superres_mode, cm->superres_scale_denominator);

  if (oxcf->rc_cfg.mode == AOM_Q) {
    return rc_pick_q_and_bounds_q_mode(cpi, width, height, gf_index,
                                       bottom_index, top_index);
  }

  int active_best_quality = 0;
  int active_worst_quality = rc->active_worst_quality;
  int q;

  const int is_intrl_arf_boost =
      gf_group->update_type[gf_index] == INTNL_ARF_UPDATE;

  if (frame_is_intra_only(cm)) {
    get_intra_q_and_bounds(cpi, width, height, &active_best_quality,
                           &active_worst_quality, cq_level);
#ifdef STRICT_RC
    active_best_quality = 0;
#endif
  } else {
    //  Active best quality limited by previous layer.
    const int pyramid_level = gf_group_pyramid_level(gf_group, gf_index);

    if ((pyramid_level <= 1) || (pyramid_level > MAX_ARF_LAYERS)) {
      active_best_quality = get_active_best_quality(cpi, active_worst_quality,
                                                    cq_level, gf_index);
    } else {
#if CONFIG_FPMT_TEST
      const int simulate_parallel_frame =
          cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0 &&
          cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE;
      int local_active_best_quality =
          simulate_parallel_frame
              ? p_rc->temp_active_best_quality[pyramid_level - 1]
              : p_rc->active_best_quality[pyramid_level - 1];
      active_best_quality = local_active_best_quality + 1;
#else
      active_best_quality = p_rc->active_best_quality[pyramid_level - 1] + 1;
#endif

      active_best_quality = AOMMIN(active_best_quality, active_worst_quality);
#ifdef STRICT_RC
      active_best_quality += (active_worst_quality - active_best_quality) / 16;
#else
      active_best_quality += (active_worst_quality - active_best_quality) / 2;
#endif
    }

    // For alt_ref and GF frames (including internal arf frames) adjust the
    // worst allowed quality as well. This insures that even on hard
    // sections we don't clamp the Q at the same value for arf frames and
    // leaf (non arf) frames. This is important to the TPL model which assumes
    // Q drops with each arf level.
    if (!(rc->is_src_frame_alt_ref) &&
        (refresh_frame->golden_frame || refresh_frame->alt_ref_frame ||
         is_intrl_arf_boost)) {
      active_worst_quality =
          (active_best_quality + (3 * active_worst_quality) + 2) / 4;
    }
  }

  adjust_active_best_and_worst_quality(
      cpi, is_intrl_arf_boost, &active_worst_quality, &active_best_quality);
  q = get_q(cpi, width, height, active_worst_quality, active_best_quality);

  // Special case when we are targeting the max allowed rate.
  if (rc->this_frame_target >= rc->max_frame_bandwidth &&
      q > active_worst_quality) {
    active_worst_quality = q;
  }

  *top_index = active_worst_quality;
  *bottom_index = active_best_quality;

  assert(*top_index <= rc->worst_quality && *top_index >= rc->best_quality);
  assert(*bottom_index <= rc->worst_quality &&
         *bottom_index >= rc->best_quality);
  assert(q <= rc->worst_quality && q >= rc->best_quality);

  return q;
}

static void rc_compute_variance_onepass_rt(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  YV12_BUFFER_CONFIG const *const unscaled_src = cpi->unscaled_source;
  if (unscaled_src == NULL) return;

  const uint8_t *src_y = unscaled_src->y_buffer;
  const int src_ystride = unscaled_src->y_stride;
  const YV12_BUFFER_CONFIG *yv12 = get_ref_frame_yv12_buf(cm, LAST_FRAME);
  const uint8_t *pre_y = yv12->buffers[0];
  const int pre_ystride = yv12->strides[0];

  // TODO(yunqing): support scaled reference frames.
  if (cpi->scaled_ref_buf[LAST_FRAME - 1]) return;

  for (int i = 0; i < 2; ++i) {
    if (unscaled_src->widths[i] != yv12->widths[i] ||
        unscaled_src->heights[i] != yv12->heights[i]) {
      return;
    }
  }

  const int num_mi_cols = cm->mi_params.mi_cols;
  const int num_mi_rows = cm->mi_params.mi_rows;
  const BLOCK_SIZE bsize = BLOCK_64X64;
  int num_samples = 0;
  // sse is computed on 64x64 blocks
  const int sb_size_by_mb = (cm->seq_params->sb_size == BLOCK_128X128)
                                ? (cm->seq_params->mib_size >> 1)
                                : cm->seq_params->mib_size;
  const int sb_cols = (num_mi_cols + sb_size_by_mb - 1) / sb_size_by_mb;
  const int sb_rows = (num_mi_rows + sb_size_by_mb - 1) / sb_size_by_mb;

  uint64_t fsse = 0;
  cpi->rec_sse = 0;

  for (int sbi_row = 0; sbi_row < sb_rows; ++sbi_row) {
    for (int sbi_col = 0; sbi_col < sb_cols; ++sbi_col) {
      unsigned int sse;
      uint8_t src[64 * 64] = { 0 };
      // Apply 4x4 block averaging/denoising on source frame.
      for (int i = 0; i < 64; i += 4) {
        for (int j = 0; j < 64; j += 4) {
          const unsigned int avg =
              aom_avg_4x4(src_y + i * src_ystride + j, src_ystride);

          for (int m = 0; m < 4; ++m) {
            for (int n = 0; n < 4; ++n) src[i * 64 + j + m * 64 + n] = avg;
          }
        }
      }

      cpi->ppi->fn_ptr[bsize].vf(src, 64, pre_y, pre_ystride, &sse);
      fsse += sse;
      num_samples++;
      src_y += 64;
      pre_y += 64;
    }
    src_y += (src_ystride << 6) - (sb_cols << 6);
    pre_y += (pre_ystride << 6) - (sb_cols << 6);
  }
  assert(num_samples > 0);
  // Ensure rec_sse > 0
  if (num_samples > 0) cpi->rec_sse = fsse > 0 ? fsse : 1;
}

int av1_rc_pick_q_and_bounds(AV1_COMP *cpi, int width, int height, int gf_index,
                             int *bottom_index, int *top_index) {
  PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  int q;
  // TODO(sarahparker) merge no-stats vbr and altref q computation
  // with rc_pick_q_and_bounds().
  const GF_GROUP *gf_group = &cpi->ppi->gf_group;
  if ((cpi->oxcf.rc_cfg.mode != AOM_Q ||
       gf_group->update_type[gf_index] == ARF_UPDATE) &&
      has_no_stats_stage(cpi)) {
    if (cpi->oxcf.rc_cfg.mode == AOM_CBR) {
      // TODO(yunqing): the results could be used for encoder optimization.
      cpi->rec_sse = UINT64_MAX;
      if (cpi->sf.hl_sf.accurate_bit_estimate &&
          cpi->common.current_frame.frame_type != KEY_FRAME)
        rc_compute_variance_onepass_rt(cpi);

      q = rc_pick_q_and_bounds_no_stats_cbr(cpi, width, height, bottom_index,
                                            top_index);
      // preserve copy of active worst quality selected.
      cpi->rc.active_worst_quality = *top_index;

#if USE_UNRESTRICTED_Q_IN_CQ_MODE
    } else if (cpi->oxcf.rc_cfg.mode == AOM_CQ) {
      q = rc_pick_q_and_bounds_no_stats_cq(cpi, width, height, bottom_index,
                                           top_index);
#endif  // USE_UNRESTRICTED_Q_IN_CQ_MODE
    } else {
      q = rc_pick_q_and_bounds_no_stats(cpi, width, height, bottom_index,
                                        top_index);
    }
  } else {
    q = rc_pick_q_and_bounds(cpi, width, height, gf_index, bottom_index,
                             top_index);
  }
  if (gf_group->update_type[gf_index] == ARF_UPDATE) p_rc->arf_q = q;

  return q;
}

void av1_rc_compute_frame_size_bounds(const AV1_COMP *cpi, int frame_target,
                                      int *frame_under_shoot_limit,
                                      int *frame_over_shoot_limit) {
  if (cpi->oxcf.rc_cfg.mode == AOM_Q) {
    *frame_under_shoot_limit = 0;
    *frame_over_shoot_limit = INT_MAX;
  } else {
    // For very small rate targets where the fractional adjustment
    // may be tiny make sure there is at least a minimum range.
    assert(cpi->sf.hl_sf.recode_tolerance <= 100);
    const int tolerance = (int)AOMMAX(
        100, ((int64_t)cpi->sf.hl_sf.recode_tolerance * frame_target) / 100);
    *frame_under_shoot_limit = AOMMAX(frame_target - tolerance, 0);
    *frame_over_shoot_limit = (int)AOMMIN((int64_t)frame_target + tolerance,
                                          cpi->rc.max_frame_bandwidth);
  }
}

void av1_rc_set_frame_target(AV1_COMP *cpi, int target, int width, int height) {
  const AV1_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;

  rc->this_frame_target = target;

  // Modify frame size target when down-scaled.
  if (av1_frame_scaled(cm) && cpi->oxcf.rc_cfg.mode != AOM_CBR) {
    rc->this_frame_target = saturate_cast_double_to_int(
        rc->this_frame_target *
        resize_rate_factor(&cpi->oxcf.frm_dim_cfg, width, height));
  }

  // Target rate per SB64 (including partial SB64s.
  const int64_t sb64_target_rate =
      ((int64_t)rc->this_frame_target << 12) / (width * height);
  rc->sb64_target_rate = (int)AOMMIN(sb64_target_rate, INT_MAX);
}

static void update_alt_ref_frame_stats(AV1_COMP *cpi) {
  // this frame refreshes means next frames don't unless specified by user
  RATE_CONTROL *const rc = &cpi->rc;
  rc->frames_since_golden = 0;
}

static void update_golden_frame_stats(AV1_COMP *cpi) {
  RATE_CONTROL *const rc = &cpi->rc;

  // Update the Golden frame usage counts.
  if (cpi->refresh_frame.golden_frame || rc->is_src_frame_alt_ref) {
    rc->frames_since_golden = 0;
  } else if (cpi->common.show_frame) {
    rc->frames_since_golden++;
  }
}

void av1_rc_postencode_update(AV1_COMP *cpi, uint64_t bytes_used) {
  const AV1_COMMON *const cm = &cpi->common;
  const CurrentFrame *const current_frame = &cm->current_frame;
  RATE_CONTROL *const rc = &cpi->rc;
  PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  const GF_GROUP *const gf_group = &cpi->ppi->gf_group;
  const RefreshFrameInfo *const refresh_frame = &cpi->refresh_frame;

  const int is_intrnl_arf =
      gf_group->update_type[cpi->gf_frame_index] == INTNL_ARF_UPDATE;

  const int qindex = cm->quant_params.base_qindex;

#if RT_PASSIVE_STRATEGY
  const int frame_number = current_frame->frame_number % MAX_Q_HISTORY;
  p_rc->q_history[frame_number] = qindex;
#endif  // RT_PASSIVE_STRATEGY

  // Update rate control heuristics
  rc->projected_frame_size = (int)(bytes_used << 3);

  // Post encode loop adjustment of Q prediction.
  av1_rc_update_rate_correction_factors(cpi, 0, cm->width, cm->height);

  // Update bit estimation ratio.
  if (cpi->oxcf.rc_cfg.mode == AOM_CBR &&
      cm->current_frame.frame_type != KEY_FRAME &&
      cpi->sf.hl_sf.accurate_bit_estimate) {
    const double q = av1_convert_qindex_to_q(cm->quant_params.base_qindex,
                                             cm->seq_params->bit_depth);
    const int this_bit_est_ratio =
        (int)(rc->projected_frame_size * q / sqrt((double)cpi->rec_sse));
    cpi->rc.bit_est_ratio =
        cpi->rc.bit_est_ratio == 0
            ? this_bit_est_ratio
            : (7 * cpi->rc.bit_est_ratio + this_bit_est_ratio) / 8;
  }

  // Keep a record of last Q and ambient average Q.
  if (current_frame->frame_type == KEY_FRAME) {
    p_rc->last_q[KEY_FRAME] = qindex;
    p_rc->avg_frame_qindex[KEY_FRAME] =
        ROUND_POWER_OF_TWO(3 * p_rc->avg_frame_qindex[KEY_FRAME] + qindex, 2);
    if (cpi->svc.spatial_layer_id == 0) {
      rc->last_encoded_size_keyframe = rc->projected_frame_size;
      rc->last_target_size_keyframe = rc->this_frame_target;
    }
  } else {
    if ((cpi->ppi->use_svc && cpi->oxcf.rc_cfg.mode == AOM_CBR) ||
        cpi->rc.rtc_external_ratectrl ||
        (!rc->is_src_frame_alt_ref &&
         !(refresh_frame->golden_frame || is_intrnl_arf ||
           refresh_frame->alt_ref_frame))) {
      p_rc->last_q[INTER_FRAME] = qindex;
      p_rc->avg_frame_qindex[INTER_FRAME] = ROUND_POWER_OF_TWO(
          3 * p_rc->avg_frame_qindex[INTER_FRAME] + qindex, 2);
      p_rc->ni_frames++;
      p_rc->tot_q += av1_convert_qindex_to_q(qindex, cm->seq_params->bit_depth);
      p_rc->avg_q = p_rc->tot_q / p_rc->ni_frames;
      // Calculate the average Q for normal inter frames (not key or GFU
      // frames).
      rc->ni_tot_qi += qindex;
      rc->ni_av_qi = rc->ni_tot_qi / p_rc->ni_frames;
    }
  }
  // Keep record of last boosted (KF/GF/ARF) Q value.
  // If the current frame is coded at a lower Q then we also update it.
  // If all mbs in this group are skipped only update if the Q value is
  // better than that already stored.
  // This is used to help set quality in forced key frames to reduce popping
  if ((qindex < p_rc->last_boosted_qindex) ||
      (current_frame->frame_type == KEY_FRAME) ||
      (!p_rc->constrained_gf_group &&
       (refresh_frame->alt_ref_frame || is_intrnl_arf ||
        (refresh_frame->golden_frame && !rc->is_src_frame_alt_ref)))) {
    p_rc->last_boosted_qindex = qindex;
  }
  if (current_frame->frame_type == KEY_FRAME) p_rc->last_kf_qindex = qindex;

  update_buffer_level(cpi, rc->projected_frame_size);
  rc->prev_avg_frame_bandwidth = rc->avg_frame_bandwidth;

  // Rolling monitors of whether we are over or underspending used to help
  // regulate min and Max Q in two pass.
  if (av1_frame_scaled(cm))
    rc->this_frame_target = saturate_cast_double_to_int(
        rc->this_frame_target /
        resize_rate_factor(&cpi->oxcf.frm_dim_cfg, cm->width, cm->height));
  if (current_frame->frame_type != KEY_FRAME) {
    p_rc->rolling_target_bits = (int)ROUND_POWER_OF_TWO_64(
        (int64_t)p_rc->rolling_target_bits * 3 + rc->this_frame_target, 2);
    p_rc->rolling_actual_bits = (int)ROUND_POWER_OF_TWO_64(
        (int64_t)p_rc->rolling_actual_bits * 3 + rc->projected_frame_size, 2);
  }

  // Actual bits spent
  p_rc->total_actual_bits += rc->projected_frame_size;
  p_rc->total_target_bits += cm->show_frame ? rc->avg_frame_bandwidth : 0;

  if (is_altref_enabled(cpi->oxcf.gf_cfg.lag_in_frames,
                        cpi->oxcf.gf_cfg.enable_auto_arf) &&
      refresh_frame->alt_ref_frame &&
      (current_frame->frame_type != KEY_FRAME && !frame_is_sframe(cm)))
    // Update the alternate reference frame stats as appropriate.
    update_alt_ref_frame_stats(cpi);
  else
    // Update the Golden frame stats as appropriate.
    update_golden_frame_stats(cpi);

#if CONFIG_FPMT_TEST
  /*The variables temp_avg_frame_qindex, temp_last_q, temp_avg_q,
   * temp_last_boosted_qindex are introduced only for quality simulation
   * purpose, it retains the value previous to the parallel encode frames. The
   * variables are updated based on the update flag.
   *
   * If there exist show_existing_frames between parallel frames, then to
   * retain the temp state do not update it. */
  int show_existing_between_parallel_frames =
      (cpi->ppi->gf_group.update_type[cpi->gf_frame_index] ==
           INTNL_OVERLAY_UPDATE &&
       cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index + 1] == 2);

  if (cpi->do_frame_data_update && !show_existing_between_parallel_frames &&
      cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE) {
    for (int i = 0; i < FRAME_TYPES; i++) {
      p_rc->temp_last_q[i] = p_rc->last_q[i];
    }
    p_rc->temp_avg_q = p_rc->avg_q;
    p_rc->temp_last_boosted_qindex = p_rc->last_boosted_qindex;
    p_rc->temp_total_actual_bits = p_rc->total_actual_bits;
    p_rc->temp_projected_frame_size = rc->projected_frame_size;
    for (int i = 0; i < RATE_FACTOR_LEVELS; i++)
      p_rc->temp_rate_correction_factors[i] = p_rc->rate_correction_factors[i];
  }
#endif
  if (current_frame->frame_type == KEY_FRAME) {
    rc->frames_since_key = 0;
    rc->frames_since_scene_change = 0;
  }
  if (cpi->refresh_frame.golden_frame)
    rc->frame_num_last_gf_refresh = current_frame->frame_number;
  rc->prev_coded_width = cm->width;
  rc->prev_coded_height = cm->height;
  rc->frame_number_encoded++;
  rc->prev_frame_is_dropped = 0;
  rc->drop_count_consec = 0;
}

void av1_rc_postencode_update_drop_frame(AV1_COMP *cpi) {
  // Update buffer level with zero size, update frame counters, and return.
  update_buffer_level(cpi, 0);
  cpi->rc.rc_2_frame = 0;
  cpi->rc.rc_1_frame = 0;
  cpi->rc.prev_avg_frame_bandwidth = cpi->rc.avg_frame_bandwidth;
  cpi->rc.prev_coded_width = cpi->common.width;
  cpi->rc.prev_coded_height = cpi->common.height;
  cpi->rc.prev_frame_is_dropped = 1;
  // On a scene/slide change for dropped frame: reset the avg_source_sad to 0,
  // otherwise the avg_source_sad can get too large and subsequent frames
  // may miss the scene/slide detection.
  if (cpi->rc.high_source_sad) cpi->rc.avg_source_sad = 0;
  if (cpi->ppi->use_svc && cpi->svc.number_spatial_layers > 1) {
    cpi->svc.last_layer_dropped[cpi->svc.spatial_layer_id] = true;
    cpi->svc.drop_spatial_layer[cpi->svc.spatial_layer_id] = true;
  }
  if (cpi->svc.spatial_layer_id == cpi->svc.number_spatial_layers - 1) {
    cpi->svc.prev_number_spatial_layers = cpi->svc.number_spatial_layers;
  }
}

int av1_find_qindex(double desired_q, aom_bit_depth_t bit_depth,
                    int best_qindex, int worst_qindex) {
  assert(best_qindex <= worst_qindex);
  int low = best_qindex;
  int high = worst_qindex;
  while (low < high) {
    const int mid = (low + high) >> 1;
    const double mid_q = av1_convert_qindex_to_q(mid, bit_depth);
    if (mid_q < desired_q) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  assert(low == high);
  assert(av1_convert_qindex_to_q(low, bit_depth) >= desired_q ||
         low == worst_qindex);
  return low;
}

int av1_compute_qdelta(const RATE_CONTROL *rc, double qstart, double qtarget,
                       aom_bit_depth_t bit_depth) {
  const int start_index =
      av1_find_qindex(qstart, bit_depth, rc->best_quality, rc->worst_quality);
  const int target_index =
      av1_find_qindex(qtarget, bit_depth, rc->best_quality, rc->worst_quality);
  return target_index - start_index;
}

// Find q_index for the desired_bits_per_mb, within [best_qindex, worst_qindex],
// assuming 'correction_factor' is 1.0.
// To be precise, 'q_index' is the smallest integer, for which the corresponding
// bits per mb <= desired_bits_per_mb.
// If no such q index is found, returns 'worst_qindex'.
static int find_qindex_by_rate(const AV1_COMP *const cpi,
                               int desired_bits_per_mb, FRAME_TYPE frame_type,
                               int best_qindex, int worst_qindex) {
  assert(best_qindex <= worst_qindex);
  int low = best_qindex;
  int high = worst_qindex;
  while (low < high) {
    const int mid = (low + high) >> 1;
    const int mid_bits_per_mb =
        av1_rc_bits_per_mb(cpi, frame_type, mid, 1.0, 0);
    if (mid_bits_per_mb > desired_bits_per_mb) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  assert(low == high);
  assert(av1_rc_bits_per_mb(cpi, frame_type, low, 1.0, 0) <=
             desired_bits_per_mb ||
         low == worst_qindex);
  return low;
}

int av1_compute_qdelta_by_rate(const AV1_COMP *cpi, FRAME_TYPE frame_type,
                               int qindex, double rate_target_ratio) {
  const RATE_CONTROL *rc = &cpi->rc;

  // Look up the current projected bits per block for the base index
  const int base_bits_per_mb =
      av1_rc_bits_per_mb(cpi, frame_type, qindex, 1.0, 0);

  // Find the target bits per mb based on the base value and given ratio.
  const int target_bits_per_mb = (int)(rate_target_ratio * base_bits_per_mb);

  const int target_index = find_qindex_by_rate(
      cpi, target_bits_per_mb, frame_type, rc->best_quality, rc->worst_quality);
  return target_index - qindex;
}

static void set_gf_interval_range(const AV1_COMP *const cpi,
                                  RATE_CONTROL *const rc) {
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;

  // Special case code for 1 pass fixed Q mode tests
  if ((has_no_stats_stage(cpi)) && (oxcf->rc_cfg.mode == AOM_Q)) {
    rc->max_gf_interval = oxcf->gf_cfg.max_gf_interval;
    rc->min_gf_interval = oxcf->gf_cfg.min_gf_interval;
    rc->static_scene_max_gf_interval = rc->min_gf_interval + 1;
  } else {
    // Set Maximum gf/arf interval
    rc->max_gf_interval = oxcf->gf_cfg.max_gf_interval;
    rc->min_gf_interval = oxcf->gf_cfg.min_gf_interval;
    if (rc->min_gf_interval == 0)
      rc->min_gf_interval = av1_rc_get_default_min_gf_interval(
          oxcf->frm_dim_cfg.width, oxcf->frm_dim_cfg.height, cpi->framerate);
    if (rc->max_gf_interval == 0)
      rc->max_gf_interval =
          get_default_max_gf_interval(cpi->framerate, rc->min_gf_interval);
    /*
     * Extended max interval for genuinely static scenes like slide shows.
     * The no.of.stats available in the case of LAP is limited,
     * hence setting to max_gf_interval.
     */
    if (cpi->ppi->lap_enabled)
      rc->static_scene_max_gf_interval = rc->max_gf_interval + 1;
    else
      rc->static_scene_max_gf_interval = MAX_STATIC_GF_GROUP_LENGTH;

    if (rc->max_gf_interval > rc->static_scene_max_gf_interval)
      rc->max_gf_interval = rc->static_scene_max_gf_interval;

    // Clamp min to max
    rc->min_gf_interval = AOMMIN(rc->min_gf_interval, rc->max_gf_interval);
  }
}

void av1_rc_update_framerate(AV1_COMP *cpi, int width, int height) {
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  RATE_CONTROL *const rc = &cpi->rc;
  const int MBs = av1_get_MBs(width, height);

  rc->avg_frame_bandwidth = saturate_cast_double_to_int(
      round(oxcf->rc_cfg.target_bandwidth / cpi->framerate));

  int64_t vbr_min_bits =
      (int64_t)rc->avg_frame_bandwidth * oxcf->rc_cfg.vbrmin_section / 100;
  vbr_min_bits = AOMMIN(vbr_min_bits, INT_MAX);

  rc->min_frame_bandwidth = AOMMAX((int)vbr_min_bits, FRAME_OVERHEAD_BITS);

  // A maximum bitrate for a frame is defined.
  // The baseline for this aligns with HW implementations that
  // can support decode of 1080P content up to a bitrate of MAX_MB_RATE bits
  // per 16x16 MB (averaged over a frame). However this limit is extended if
  // a very high rate is given on the command line or the rate cannot
  // be achieved because of a user specified max q (e.g. when the user
  // specifies lossless encode.
  int64_t vbr_max_bits =
      (int64_t)rc->avg_frame_bandwidth * oxcf->rc_cfg.vbrmax_section / 100;
  vbr_max_bits = AOMMIN(vbr_max_bits, INT_MAX);

  rc->max_frame_bandwidth =
      AOMMAX(AOMMAX((MBs * MAX_MB_RATE), MAXRATE_1080P), (int)vbr_max_bits);

  set_gf_interval_range(cpi, rc);
}

#define VBR_PCT_ADJUSTMENT_LIMIT 50
// For VBR...adjustment to the frame target based on error from previous frames
static void vbr_rate_correction(AV1_COMP *cpi, int *this_frame_target) {
  RATE_CONTROL *const rc = &cpi->rc;
  PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
#if CONFIG_FPMT_TEST
  const int simulate_parallel_frame =
      cpi->ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] > 0 &&
      cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE;
  int64_t vbr_bits_off_target = simulate_parallel_frame
                                    ? cpi->ppi->p_rc.temp_vbr_bits_off_target
                                    : p_rc->vbr_bits_off_target;
#else
  int64_t vbr_bits_off_target = p_rc->vbr_bits_off_target;
#endif
  int64_t frame_target = *this_frame_target;

  const double stats_count =
      cpi->ppi->twopass.stats_buf_ctx->total_stats != NULL
          ? cpi->ppi->twopass.stats_buf_ctx->total_stats->count
          : 0.0;
  const int frame_window =
      (int)AOMMIN(16, stats_count - cpi->common.current_frame.frame_number);
  assert(VBR_PCT_ADJUSTMENT_LIMIT <= 100);
  if (frame_window > 0) {
    const int64_t max_delta =
        AOMMIN(llabs((vbr_bits_off_target / frame_window)),
               (frame_target * VBR_PCT_ADJUSTMENT_LIMIT) / 100);

    // vbr_bits_off_target > 0 means we have extra bits to spend
    // vbr_bits_off_target < 0 we are currently overshooting
    frame_target += (vbr_bits_off_target >= 0) ? max_delta : -max_delta;
  }

#if CONFIG_FPMT_TEST
  int64_t vbr_bits_off_target_fast =
      simulate_parallel_frame ? cpi->ppi->p_rc.temp_vbr_bits_off_target_fast
                              : p_rc->vbr_bits_off_target_fast;
#endif
  // Fast redistribution of bits arising from massive local undershoot.
  // Don't do it for kf,arf,gf or overlay frames.
  if (!frame_is_kf_gf_arf(cpi) &&
#if CONFIG_FPMT_TEST
      vbr_bits_off_target_fast &&
#else
      p_rc->vbr_bits_off_target_fast &&
#endif
      !rc->is_src_frame_alt_ref) {
    int64_t one_frame_bits = AOMMAX(rc->avg_frame_bandwidth, frame_target);
    int64_t fast_extra_bits;
#if CONFIG_FPMT_TEST
    fast_extra_bits = AOMMIN(vbr_bits_off_target_fast, one_frame_bits);
    fast_extra_bits =
        AOMMIN(fast_extra_bits,
               AOMMAX(one_frame_bits / 8, vbr_bits_off_target_fast / 8));
#else
    fast_extra_bits = AOMMIN(p_rc->vbr_bits_off_target_fast, one_frame_bits);
    fast_extra_bits =
        AOMMIN(fast_extra_bits,
               AOMMAX(one_frame_bits / 8, p_rc->vbr_bits_off_target_fast / 8));
#endif
    fast_extra_bits = AOMMIN(fast_extra_bits, INT_MAX);
    if (fast_extra_bits > 0) {
      // Update frame_target only if additional bits are available from
      // local undershoot.
      frame_target += fast_extra_bits;
    }
    // Store the fast_extra_bits of the frame and reduce it from
    // vbr_bits_off_target_fast during postencode stage.
    rc->frame_level_fast_extra_bits = (int)fast_extra_bits;
    // Retaining the condition to update during postencode stage since
    // fast_extra_bits are calculated based on vbr_bits_off_target_fast.
    cpi->do_update_vbr_bits_off_target_fast = 1;
  }

  // Clamp the target for the frame to the maximum allowed for one frame.
  *this_frame_target = (int)AOMMIN(frame_target, INT_MAX);
}

void av1_set_target_rate(AV1_COMP *cpi, int width, int height) {
  RATE_CONTROL *const rc = &cpi->rc;
  int target_rate = rc->base_frame_target;

  // Correction to rate target based on prior over or under shoot.
  if (cpi->oxcf.rc_cfg.mode == AOM_VBR || cpi->oxcf.rc_cfg.mode == AOM_CQ)
    vbr_rate_correction(cpi, &target_rate);
  av1_rc_set_frame_target(cpi, target_rate, width, height);
}

int av1_calc_pframe_target_size_one_pass_vbr(
    const AV1_COMP *const cpi, FRAME_UPDATE_TYPE frame_update_type) {
  static const int af_ratio = 10;
  const RATE_CONTROL *const rc = &cpi->rc;
  const PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  int64_t target;
#if USE_ALTREF_FOR_ONE_PASS
  if (frame_update_type == KF_UPDATE || frame_update_type == GF_UPDATE ||
      frame_update_type == ARF_UPDATE) {
    target = ((int64_t)rc->avg_frame_bandwidth * p_rc->baseline_gf_interval *
              af_ratio) /
             (p_rc->baseline_gf_interval + af_ratio - 1);
  } else {
    target = ((int64_t)rc->avg_frame_bandwidth * p_rc->baseline_gf_interval) /
             (p_rc->baseline_gf_interval + af_ratio - 1);
  }
#else
  target = rc->avg_frame_bandwidth;
#endif
  return clamp_pframe_target_size(cpi, target, frame_update_type);
}

int av1_calc_iframe_target_size_one_pass_vbr(const AV1_COMP *const cpi) {
  static const int kf_ratio = 25;
  const RATE_CONTROL *rc = &cpi->rc;
  const int64_t target = (int64_t)rc->avg_frame_bandwidth * kf_ratio;
  return clamp_iframe_target_size(cpi, target);
}

int av1_calc_pframe_target_size_one_pass_cbr(
    const AV1_COMP *cpi, FRAME_UPDATE_TYPE frame_update_type) {
  const AV1EncoderConfig *oxcf = &cpi->oxcf;
  const RATE_CONTROL *rc = &cpi->rc;
  const PRIMARY_RATE_CONTROL *p_rc = &cpi->ppi->p_rc;
  const RateControlCfg *rc_cfg = &oxcf->rc_cfg;
  const int64_t diff = p_rc->optimal_buffer_level - p_rc->buffer_level;
  const int64_t one_pct_bits = 1 + p_rc->optimal_buffer_level / 100;
  int min_frame_target =
      AOMMAX(rc->avg_frame_bandwidth >> 4, FRAME_OVERHEAD_BITS);
  int64_t target;

  if (rc_cfg->gf_cbr_boost_pct) {
    const int af_ratio_pct = rc_cfg->gf_cbr_boost_pct + 100;
    if (frame_update_type == GF_UPDATE || frame_update_type == OVERLAY_UPDATE) {
      target = ((int64_t)rc->avg_frame_bandwidth * p_rc->baseline_gf_interval *
                af_ratio_pct) /
               (p_rc->baseline_gf_interval * 100 + af_ratio_pct - 100);
    } else {
      target = ((int64_t)rc->avg_frame_bandwidth * p_rc->baseline_gf_interval *
                100) /
               (p_rc->baseline_gf_interval * 100 + af_ratio_pct - 100);
    }
  } else {
    target = rc->avg_frame_bandwidth;
  }
  if (cpi->ppi->use_svc) {
    // Note that for layers, avg_frame_bandwidth is the cumulative
    // per-frame-bandwidth. For the target size of this frame, use the
    // layer average frame size (i.e., non-cumulative per-frame-bw).
    int layer =
        LAYER_IDS_TO_IDX(cpi->svc.spatial_layer_id, cpi->svc.temporal_layer_id,
                         cpi->svc.number_temporal_layers);
    const LAYER_CONTEXT *lc = &cpi->svc.layer_context[layer];
    target = lc->avg_frame_size;
    min_frame_target = AOMMAX(lc->avg_frame_size >> 4, FRAME_OVERHEAD_BITS);
  }
  if (diff > 0) {
    // Lower the target bandwidth for this frame.
    const int pct_low =
        (int)AOMMIN(diff / one_pct_bits, rc_cfg->under_shoot_pct);
    target -= (target * pct_low) / 200;
  } else if (diff < 0) {
    // Increase the target bandwidth for this frame.
    const int pct_high =
        (int)AOMMIN(-diff / one_pct_bits, rc_cfg->over_shoot_pct);
    target += (target * pct_high) / 200;
  }
  if (rc_cfg->max_inter_bitrate_pct) {
    const int64_t max_rate =
        (int64_t)rc->avg_frame_bandwidth * rc_cfg->max_inter_bitrate_pct / 100;
    target = AOMMIN(target, max_rate);
  }
  if (target > INT_MAX) target = INT_MAX;
  return AOMMAX(min_frame_target, (int)target);
}

int av1_calc_iframe_target_size_one_pass_cbr(const AV1_COMP *cpi) {
  const RATE_CONTROL *rc = &cpi->rc;
  const PRIMARY_RATE_CONTROL *p_rc = &cpi->ppi->p_rc;
  int64_t target;
  if (cpi->common.current_frame.frame_number == 0) {
    target = ((p_rc->starting_buffer_level / 2) > INT_MAX)
                 ? INT_MAX
                 : (int)(p_rc->starting_buffer_level / 2);
    if (cpi->svc.number_temporal_layers > 1 && target < (INT_MAX >> 2)) {
      target = target << AOMMIN(2, (cpi->svc.number_temporal_layers - 1));
    }
  } else {
    int kf_boost = 32;
    double framerate = cpi->framerate;

    kf_boost = AOMMAX(kf_boost, (int)round(2 * framerate - 16));
    if (rc->frames_since_key < framerate / 2) {
      kf_boost = (int)(kf_boost * rc->frames_since_key / (framerate / 2));
    }
    target = ((int64_t)(16 + kf_boost) * rc->avg_frame_bandwidth) >> 4;
  }
  return clamp_iframe_target_size(cpi, target);
}

static void set_golden_update(AV1_COMP *const cpi) {
  RATE_CONTROL *const rc = &cpi->rc;
  PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  int divisor = 10;
  if (cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ)
    divisor = cpi->cyclic_refresh->percent_refresh;

  // Set minimum gf_interval for GF update to a multiple of the refresh period,
  // with some max limit. Depending on past encoding stats, GF flag may be
  // reset and update may not occur until next baseline_gf_interval.
  const int gf_length_mult[2] = { 8, 4 };
  if (divisor > 0)
    p_rc->baseline_gf_interval =
        AOMMIN(gf_length_mult[cpi->sf.rt_sf.gf_length_lvl] * (100 / divisor),
               MAX_GF_INTERVAL_RT);
  else
    p_rc->baseline_gf_interval = FIXED_GF_INTERVAL_RT;
  if (rc->avg_frame_low_motion && rc->avg_frame_low_motion < 40)
    p_rc->baseline_gf_interval = 16;
}

static void set_baseline_gf_interval(AV1_COMP *cpi, FRAME_TYPE frame_type) {
  RATE_CONTROL *const rc = &cpi->rc;
  PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  GF_GROUP *const gf_group = &cpi->ppi->gf_group;

  set_golden_update(cpi);

  if (p_rc->baseline_gf_interval > rc->frames_to_key &&
      cpi->oxcf.kf_cfg.auto_key)
    p_rc->baseline_gf_interval = rc->frames_to_key;
  p_rc->gfu_boost = DEFAULT_GF_BOOST_RT;
  p_rc->constrained_gf_group =
      (p_rc->baseline_gf_interval >= rc->frames_to_key &&
       cpi->oxcf.kf_cfg.auto_key)
          ? 1
          : 0;
  rc->frames_till_gf_update_due = p_rc->baseline_gf_interval;
  cpi->gf_frame_index = 0;
  // SVC does not use GF as periodic boost.
  // TODO(marpan): Find better way to disable this for SVC.
  if (cpi->ppi->use_svc) {
    SVC *const svc = &cpi->svc;
    p_rc->baseline_gf_interval = MAX_STATIC_GF_GROUP_LENGTH - 1;
    p_rc->gfu_boost = 1;
    p_rc->constrained_gf_group = 0;
    rc->frames_till_gf_update_due = p_rc->baseline_gf_interval;
    for (int layer = 0;
         layer < svc->number_spatial_layers * svc->number_temporal_layers;
         ++layer) {
      LAYER_CONTEXT *const lc = &svc->layer_context[layer];
      lc->p_rc.baseline_gf_interval = p_rc->baseline_gf_interval;
      lc->p_rc.gfu_boost = p_rc->gfu_boost;
      lc->p_rc.constrained_gf_group = p_rc->constrained_gf_group;
      lc->rc.frames_till_gf_update_due = rc->frames_till_gf_update_due;
      lc->group_index = 0;
    }
  }
  gf_group->size = p_rc->baseline_gf_interval;
  gf_group->update_type[0] = (frame_type == KEY_FRAME) ? KF_UPDATE : GF_UPDATE;
  gf_group->refbuf_state[cpi->gf_frame_index] =
      (frame_type == KEY_FRAME) ? REFBUF_RESET : REFBUF_UPDATE;
}

void av1_adjust_gf_refresh_qp_one_pass_rt(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;
  RTC_REF *const rtc_ref = &cpi->ppi->rtc_ref;
  const int resize_pending = is_frame_resize_pending(cpi);
  if (!resize_pending && !rc->high_source_sad) {
    // Check if we should disable GF refresh (if period is up),
    // or force a GF refresh update (if we are at least halfway through
    // period) based on QP. Look into add info on segment deltaq.
    PRIMARY_RATE_CONTROL *p_rc = &cpi->ppi->p_rc;
    const int avg_qp = p_rc->avg_frame_qindex[INTER_FRAME];
    const int allow_gf_update =
        rc->frames_till_gf_update_due <= (p_rc->baseline_gf_interval - 10);
    int gf_update_changed = 0;
    int thresh = 87;
    if ((cm->current_frame.frame_number - cpi->rc.frame_num_last_gf_refresh) <
            FIXED_GF_INTERVAL_RT &&
        rc->frames_till_gf_update_due == 1 &&
        cm->quant_params.base_qindex > avg_qp) {
      // Disable GF refresh since QP is above the running average QP.
      rtc_ref->refresh[rtc_ref->gld_idx_1layer] = 0;
      gf_update_changed = 1;
      cpi->refresh_frame.golden_frame = 0;
    } else if (allow_gf_update &&
               ((cm->quant_params.base_qindex < thresh * avg_qp / 100) ||
                (rc->avg_frame_low_motion && rc->avg_frame_low_motion < 20))) {
      // Force refresh since QP is well below average QP or this is a high
      // motion frame.
      rtc_ref->refresh[rtc_ref->gld_idx_1layer] = 1;
      gf_update_changed = 1;
      cpi->refresh_frame.golden_frame = 1;
    }
    if (gf_update_changed) {
      set_baseline_gf_interval(cpi, INTER_FRAME);
      int refresh_mask = 0;
      for (unsigned int i = 0; i < INTER_REFS_PER_FRAME; i++) {
        int ref_frame_map_idx = rtc_ref->ref_idx[i];
        refresh_mask |= rtc_ref->refresh[ref_frame_map_idx]
                        << ref_frame_map_idx;
      }
      cm->current_frame.refresh_frame_flags = refresh_mask;
    }
  }
}

/*!\brief Setup the reference prediction structure for 1 pass real-time
 *
 * Set the reference prediction structure for 1 layer.
 * Current structure is to use 3 references (LAST, GOLDEN, ALTREF),
 * where ALT_REF always behind current by lag_alt frames, and GOLDEN is
 * either updated on LAST with period baseline_gf_interval (fixed slot)
 * or always behind current by lag_gld (gld_fixed_slot = 0, lag_gld <= 7).
 *
 * \ingroup rate_control
 * \param[in]       cpi          Top level encoder structure
 * \param[in]       gf_update    Flag to indicate if GF is updated
 *
 * \remark Nothing is returned. Instead the settings for the prediction
 * structure are set in \c cpi-ext_flags; and the buffer slot index
 * (for each of 7 references) and refresh flags (for each of the 8 slots)
 * are set in \c cpi->svc.ref_idx[] and \c cpi->svc.refresh[].
 */
void av1_set_rtc_reference_structure_one_layer(AV1_COMP *cpi, int gf_update) {
  AV1_COMMON *const cm = &cpi->common;
  ExternalFlags *const ext_flags = &cpi->ext_flags;
  RATE_CONTROL *const rc = &cpi->rc;
  ExtRefreshFrameFlagsInfo *const ext_refresh_frame_flags =
      &ext_flags->refresh_frame;
  RTC_REF *const rtc_ref = &cpi->ppi->rtc_ref;
  unsigned int frame_number = (cpi->oxcf.rc_cfg.drop_frames_water_mark)
                                  ? rc->frame_number_encoded
                                  : cm->current_frame.frame_number;
  unsigned int lag_alt = 4;
  int last_idx = 0;
  int last_idx_refresh = 0;
  int gld_idx = 0;
  int alt_ref_idx = 0;
  int last2_idx = 0;
  ext_refresh_frame_flags->update_pending = 1;
  ext_flags->ref_frame_flags = 0;
  ext_refresh_frame_flags->last_frame = 1;
  ext_refresh_frame_flags->golden_frame = 0;
  ext_refresh_frame_flags->alt_ref_frame = 0;
  // Decide altref lag adaptively for rt
  if (cpi->sf.rt_sf.sad_based_adp_altref_lag) {
    lag_alt = 6;
    const uint64_t th_frame_sad[4][3] = {
      { 18000, 18000, 18000 },  // HDRES CPU 9
      { 25000, 25000, 25000 },  // MIDRES CPU 9
      { 40000, 30000, 20000 },  // HDRES CPU 10
      { 30000, 25000, 20000 }   // MIDRES CPU 10
    };
    int th_idx = cpi->sf.rt_sf.sad_based_adp_altref_lag - 1;
    assert(th_idx < 4);
    if (rc->avg_source_sad > th_frame_sad[th_idx][0])
      lag_alt = 3;
    else if (rc->avg_source_sad > th_frame_sad[th_idx][1])
      lag_alt = 4;
    else if (rc->avg_source_sad > th_frame_sad[th_idx][2])
      lag_alt = 5;
  }
  // This defines the reference structure for 1 layer (non-svc) RTC encoding.
  // To avoid the internal/default reference structure for non-realtime
  // overwriting this behavior, we use the "svc" ref parameters from the
  // external control SET_SVC_REF_FRAME_CONFIG.
  // TODO(marpan): rename that control and the related internal parameters
  // to rtc_ref.
  for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) rtc_ref->ref_idx[i] = 7;
  for (int i = 0; i < REF_FRAMES; ++i) rtc_ref->refresh[i] = 0;
  // Set the reference frame flags.
  ext_flags->ref_frame_flags ^= AOM_LAST_FLAG;
  if (!cpi->sf.rt_sf.force_only_last_ref) {
    ext_flags->ref_frame_flags ^= AOM_ALT_FLAG;
    ext_flags->ref_frame_flags ^= AOM_GOLD_FLAG;
    if (cpi->sf.rt_sf.ref_frame_comp_nonrd[1])
      ext_flags->ref_frame_flags ^= AOM_LAST2_FLAG;
  }
  const int sh = 6;
  // Moving index slot for last: 0 - (sh - 1).
  if (frame_number > 1) last_idx = ((frame_number - 1) % sh);
  // Moving index for refresh of last: one ahead for next frame.
  last_idx_refresh = (frame_number % sh);
  gld_idx = 6;

  // Moving index for alt_ref, lag behind LAST by lag_alt frames.
  if (frame_number > lag_alt) alt_ref_idx = ((frame_number - lag_alt) % sh);
  if (cpi->sf.rt_sf.ref_frame_comp_nonrd[1]) {
    // Moving index for LAST2, lag behind LAST by 2 frames.
    if (frame_number > 2) last2_idx = ((frame_number - 2) % sh);
  }
  rtc_ref->ref_idx[0] = last_idx;          // LAST
  rtc_ref->ref_idx[1] = last_idx_refresh;  // LAST2 (for refresh of last).
  if (cpi->sf.rt_sf.ref_frame_comp_nonrd[1]) {
    rtc_ref->ref_idx[1] = last2_idx;         // LAST2
    rtc_ref->ref_idx[2] = last_idx_refresh;  // LAST3 (for refresh of last).
  }
  rtc_ref->ref_idx[3] = gld_idx;      // GOLDEN
  rtc_ref->ref_idx[6] = alt_ref_idx;  // ALT_REF
  // Refresh this slot, which will become LAST on next frame.
  rtc_ref->refresh[last_idx_refresh] = 1;
  // Update GOLDEN on period for fixed slot case.
  if (gf_update && cm->current_frame.frame_type != KEY_FRAME) {
    ext_refresh_frame_flags->golden_frame = 1;
    rtc_ref->refresh[gld_idx] = 1;
  }
  rtc_ref->gld_idx_1layer = gld_idx;
  // Set the flag to reduce the number of reference frame buffers used.
  // This assumes that slot 7 is never used.
  cpi->rt_reduce_num_ref_buffers = 1;
  cpi->rt_reduce_num_ref_buffers &= (rtc_ref->ref_idx[0] < 7);
  cpi->rt_reduce_num_ref_buffers &= (rtc_ref->ref_idx[1] < 7);
  cpi->rt_reduce_num_ref_buffers &= (rtc_ref->ref_idx[3] < 7);
  cpi->rt_reduce_num_ref_buffers &= (rtc_ref->ref_idx[6] < 7);
  if (cpi->sf.rt_sf.ref_frame_comp_nonrd[1])
    cpi->rt_reduce_num_ref_buffers &= (rtc_ref->ref_idx[2] < 7);
}

// Returns whether the 64x64 block is active or inactive: used
// by the scene detection, which is over 64x64 blocks.
static int set_block_is_active(unsigned char *const active_map_4x4, int mi_cols,
                               int mi_rows, int sbi_col, int sbi_row) {
  int num_4x4 = 16;
  int r = sbi_row << 4;
  int c = sbi_col << 4;
  const int row_max = AOMMIN(num_4x4, mi_rows - r);
  const int col_max = AOMMIN(num_4x4, mi_cols - c);
  // Active map is set for 16x16 blocks, so only need to
  // check over16x16,
  for (int x = 0; x < row_max; x += 4) {
    for (int y = 0; y < col_max; y += 4) {
      if (active_map_4x4[(r + x) * mi_cols + (c + y)] == AM_SEGMENT_ID_ACTIVE)
        return 1;
    }
  }
  return 0;
}

// Returns the best sad for column or row motion of the superblock.
static unsigned int estimate_scroll_motion(
    const AV1_COMP *cpi, uint8_t *src_buf, uint8_t *last_src_buf,
    int src_stride, int ref_stride, BLOCK_SIZE bsize, int pos_col, int pos_row,
    int *best_intmv_col, int *best_intmv_row, int sw_col, int sw_row) {
  const AV1_COMMON *const cm = &cpi->common;
  const int bw = block_size_wide[bsize];
  const int bh = block_size_high[bsize];
  const int full_search = 1;
  // Keep border a multiple of 16.
  const int border = (cpi->oxcf.border_in_pixels >> 4) << 4;
  int search_size_width = sw_col;
  int search_size_height = sw_row;
  // Adjust based on boundary.
  if ((pos_col - search_size_width < -border) ||
      (pos_col + search_size_width > cm->width + border))
    search_size_width = border;
  if ((pos_row - search_size_height < -border) ||
      (pos_row + search_size_height > cm->height + border))
    search_size_height = border;
  const uint8_t *ref_buf;
  const int row_norm_factor = mi_size_high_log2[bsize] + 1;
  const int col_norm_factor = 3 + (bw >> 5);
  const int ref_buf_width = (search_size_width << 1) + bw;
  const int ref_buf_height = (search_size_height << 1) + bh;
  int16_t *hbuf = (int16_t *)aom_malloc(ref_buf_width * sizeof(*hbuf));
  int16_t *vbuf = (int16_t *)aom_malloc(ref_buf_height * sizeof(*vbuf));
  int16_t *src_hbuf = (int16_t *)aom_malloc(bw * sizeof(*src_hbuf));
  int16_t *src_vbuf = (int16_t *)aom_malloc(bh * sizeof(*src_vbuf));
  if (!hbuf || !vbuf || !src_hbuf || !src_vbuf) {
    aom_free(hbuf);
    aom_free(vbuf);
    aom_free(src_hbuf);
    aom_free(src_vbuf);
    aom_internal_error(cm->error, AOM_CODEC_MEM_ERROR,
                       "Failed to allocate hbuf, vbuf, src_hbuf, or src_vbuf");
  }
  // Set up prediction 1-D reference set for rows.
  ref_buf = last_src_buf - search_size_width;
  aom_int_pro_row(hbuf, ref_buf, ref_stride, ref_buf_width, bh,
                  row_norm_factor);
  // Set up prediction 1-D reference set for cols
  ref_buf = last_src_buf - search_size_height * ref_stride;
  aom_int_pro_col(vbuf, ref_buf, ref_stride, bw, ref_buf_height,
                  col_norm_factor);
  // Set up src 1-D reference set
  aom_int_pro_row(src_hbuf, src_buf, src_stride, bw, bh, row_norm_factor);
  aom_int_pro_col(src_vbuf, src_buf, src_stride, bw, bh, col_norm_factor);
  unsigned int best_sad;
  int best_sad_col, best_sad_row;
  // Find the best match per 1-D search
  *best_intmv_col =
      av1_vector_match(hbuf, src_hbuf, mi_size_wide_log2[bsize],
                       search_size_width, full_search, &best_sad_col);
  *best_intmv_row =
      av1_vector_match(vbuf, src_vbuf, mi_size_high_log2[bsize],
                       search_size_height, full_search, &best_sad_row);
  if (best_sad_col < best_sad_row) {
    *best_intmv_row = 0;
    best_sad = best_sad_col;
  } else {
    *best_intmv_col = 0;
    best_sad = best_sad_row;
  }
  aom_free(hbuf);
  aom_free(vbuf);
  aom_free(src_hbuf);
  aom_free(src_vbuf);
  return best_sad;
}

/*!\brief Check for scene detection, for 1 pass real-time mode.
 *
 * Compute average source sad (temporal sad: between current source and
 * previous source) over a subset of superblocks. Use this is detect big changes
 * in content and set the \c cpi->rc.high_source_sad flag.
 *
 * \ingroup rate_control
 * \param[in]       cpi          Top level encoder structure
 * \param[in]       frame_input  Current and last input source frames
 *
 * \remark Nothing is returned. Instead the flag \c cpi->rc.high_source_sad
 * is set if scene change is detected, and \c cpi->rc.avg_source_sad is updated.
 */
static void rc_scene_detection_onepass_rt(AV1_COMP *cpi,
                                          const EncodeFrameInput *frame_input) {
  AV1_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;
  YV12_BUFFER_CONFIG const *const unscaled_src = frame_input->source;
  YV12_BUFFER_CONFIG const *const unscaled_last_src = frame_input->last_source;
  uint8_t *src_y;
  int src_ystride;
  int src_width;
  int src_height;
  uint8_t *last_src_y;
  int last_src_ystride;
  int last_src_width;
  int last_src_height;
  int width = cm->width;
  int height = cm->height;
  if (cpi->svc.number_spatial_layers > 1) {
    width = cpi->oxcf.frm_dim_cfg.width;
    height = cpi->oxcf.frm_dim_cfg.height;
  }
  if (width != cm->render_width || height != cm->render_height ||
      unscaled_src == NULL || unscaled_last_src == NULL) {
    aom_free(cpi->src_sad_blk_64x64);
    cpi->src_sad_blk_64x64 = NULL;
  }
  if (unscaled_src == NULL || unscaled_last_src == NULL) return;
  src_y = unscaled_src->y_buffer;
  src_ystride = unscaled_src->y_stride;
  src_width = unscaled_src->y_width;
  src_height = unscaled_src->y_height;
  last_src_y = unscaled_last_src->y_buffer;
  last_src_ystride = unscaled_last_src->y_stride;
  last_src_width = unscaled_last_src->y_width;
  last_src_height = unscaled_last_src->y_height;
  if (src_width != last_src_width || src_height != last_src_height) {
    aom_free(cpi->src_sad_blk_64x64);
    cpi->src_sad_blk_64x64 = NULL;
    return;
  }
  rc->high_source_sad = 0;
  rc->percent_blocks_with_motion = 0;
  rc->max_block_source_sad = 0;
  rc->prev_avg_source_sad = rc->avg_source_sad;
  int num_mi_cols = cm->mi_params.mi_cols;
  int num_mi_rows = cm->mi_params.mi_rows;
  if (cpi->svc.number_spatial_layers > 1) {
    num_mi_cols = cpi->svc.mi_cols_full_resoln;
    num_mi_rows = cpi->svc.mi_rows_full_resoln;
  }
  int num_zero_temp_sad = 0;
  uint32_t min_thresh =
      (cpi->oxcf.tune_cfg.content == AOM_CONTENT_SCREEN) ? 8000 : 10000;
  if (cpi->sf.rt_sf.higher_thresh_scene_detection) {
    min_thresh = cm->width * cm->height <= 320 * 240 && cpi->framerate < 10.0
                     ? 50000
                     : 100000;
  }
  const BLOCK_SIZE bsize = BLOCK_64X64;
  // Loop over sub-sample of frame, compute average sad over 64x64 blocks.
  uint64_t avg_sad = 0;
  uint64_t tmp_sad = 0;
  int num_samples = 0;
  const int thresh =
      ((cm->width * cm->height <= 320 * 240 && cpi->framerate < 10.0) ||
       (cpi->oxcf.tune_cfg.content == AOM_CONTENT_SCREEN))
          ? 5
          : 6;
  // SAD is computed on 64x64 blocks
  const int sb_size_by_mb = (cm->seq_params->sb_size == BLOCK_128X128)
                                ? (cm->seq_params->mib_size >> 1)
                                : cm->seq_params->mib_size;
  const int sb_cols = (num_mi_cols + sb_size_by_mb - 1) / sb_size_by_mb;
  const int sb_rows = (num_mi_rows + sb_size_by_mb - 1) / sb_size_by_mb;
  uint64_t sum_sq_thresh = 10000;  // sum = sqrt(thresh / 64*64)) ~1.5
  int num_low_var_high_sumdiff = 0;
  int light_change = 0;
  // Flag to check light change or not.
  const int check_light_change = 0;
  // TODO(marpan): There seems some difference along the bottom border when
  // using the source_last_tl0 for last_source (used for temporal layers or
  // when previous frame is dropped).
  // Remove this border parameter when issue is resolved: difference is that
  // non-zero sad exists along bottom border even though source is static.
  const int border =
      rc->prev_frame_is_dropped || cpi->svc.number_temporal_layers > 1;
  // Store blkwise SAD for later use
  if (width == cm->render_width && height == cm->render_height) {
    if (cpi->src_sad_blk_64x64 == NULL) {
      CHECK_MEM_ERROR(cm, cpi->src_sad_blk_64x64,
                      (uint64_t *)aom_calloc(sb_cols * sb_rows,
                                             sizeof(*cpi->src_sad_blk_64x64)));
    }
  }
  const CommonModeInfoParams *const mi_params = &cpi->common.mi_params;
  const int mi_cols = mi_params->mi_cols;
  const int mi_rows = mi_params->mi_rows;
  unsigned char *const active_map_4x4 = cpi->active_map.map;
  // Avoid bottom and right border.
  for (int sbi_row = 0; sbi_row < sb_rows - border; ++sbi_row) {
    for (int sbi_col = 0; sbi_col < sb_cols; ++sbi_col) {
      int block_is_active = 1;
      if (cpi->active_map.enabled && rc->percent_blocks_inactive > 0) {
        block_is_active = set_block_is_active(active_map_4x4, mi_cols, mi_rows,
                                              sbi_col, sbi_row);
      }
      if (block_is_active) {
        tmp_sad = cpi->ppi->fn_ptr[bsize].sdf(src_y, src_ystride, last_src_y,
                                              last_src_ystride);
      } else {
        tmp_sad = 0;
      }
      if (cpi->src_sad_blk_64x64 != NULL)
        cpi->src_sad_blk_64x64[sbi_col + sbi_row * sb_cols] = tmp_sad;
      if (check_light_change) {
        unsigned int sse, variance;
        variance = cpi->ppi->fn_ptr[bsize].vf(src_y, src_ystride, last_src_y,
                                              last_src_ystride, &sse);
        // Note: sse - variance = ((sum * sum) >> 12)
        // Detect large lighting change.
        if (variance < (sse >> 1) && (sse - variance) > sum_sq_thresh) {
          num_low_var_high_sumdiff++;
        }
      }
      avg_sad += tmp_sad;
      num_samples++;
      if (tmp_sad == 0) num_zero_temp_sad++;
      if (tmp_sad > rc->max_block_source_sad)
        rc->max_block_source_sad = tmp_sad;

      src_y += 64;
      last_src_y += 64;
    }
    src_y += (src_ystride << 6) - (sb_cols << 6);
    last_src_y += (last_src_ystride << 6) - (sb_cols << 6);
  }
  if (check_light_change && num_samples > 0 &&
      num_low_var_high_sumdiff > (num_samples >> 1))
    light_change = 1;
  if (num_samples > 0) avg_sad = avg_sad / num_samples;
  // Set high_source_sad flag if we detect very high increase in avg_sad
  // between current and previous frame value(s). Use minimum threshold
  // for cases where there is small change from content that is completely
  // static.
  if (!light_change &&
      avg_sad >
          AOMMAX(min_thresh, (unsigned int)(rc->avg_source_sad * thresh)) &&
      rc->frames_since_key > 1 + cpi->svc.number_spatial_layers &&
      num_zero_temp_sad < 3 * (num_samples >> 2))
    rc->high_source_sad = 1;
  else
    rc->high_source_sad = 0;
  rc->avg_source_sad = (3 * rc->avg_source_sad + avg_sad) >> 2;
  rc->frame_source_sad = avg_sad;
  if (num_samples > 0)
    rc->percent_blocks_with_motion =
        ((num_samples - num_zero_temp_sad) * 100) / num_samples;
  if (rc->frame_source_sad > 0) rc->static_since_last_scene_change = 0;
  if (rc->high_source_sad) {
    cpi->rc.frames_since_scene_change = 0;
    rc->static_since_last_scene_change = 1;
  }
  // Update the high_motion_content_screen_rtc flag on TL0. Avoid the update
  // if too many consecutive frame drops occurred.
  const uint64_t thresh_high_motion = 9 * 64 * 64;
  if (cpi->svc.temporal_layer_id == 0 && rc->drop_count_consec < 3) {
    cpi->rc.high_motion_content_screen_rtc = 0;
    if (cpi->oxcf.speed >= 11 &&
        cpi->oxcf.tune_cfg.content == AOM_CONTENT_SCREEN &&
        rc->percent_blocks_with_motion > 40 &&
        rc->prev_avg_source_sad > thresh_high_motion &&
        rc->avg_source_sad > thresh_high_motion &&
        rc->avg_frame_low_motion < 60 && unscaled_src->y_width >= 1280 &&
        unscaled_src->y_height >= 720) {
      cpi->rc.high_motion_content_screen_rtc = 1;
      // Compute fast coarse/global motion for 128x128 superblock centered
      // at middle of frame, and one to the upper left and one to lower right.
      // to determine if motion is scroll. Only test 3 points (pts) for now.
      // TODO(marpan): Only allow for 8 bit-depth for now.
      if (cm->seq_params->bit_depth == 8) {
        const int sw_row = (cpi->rc.frame_source_sad > 20000) ? 512 : 192;
        const int sw_col = (cpi->rc.frame_source_sad > 20000) ? 512 : 160;
        const int num_pts =
            unscaled_src->y_width * unscaled_src->y_height >= 1920 * 1080 ? 3
                                                                          : 1;
        for (int pts = 0; pts < num_pts; pts++) {
          // fac and shift are used to move the center block for the other
          // two points (pts).
          int fac = 1;
          int shift = 1;
          if (pts == 1) {
            fac = 1;
            shift = 2;
          } else if (pts == 2) {
            fac = 3;
            shift = 2;
          }
          int pos_col = (fac * unscaled_src->y_width >> shift) - 64;
          int pos_row = (fac * unscaled_src->y_height >> shift) - 64;
          pos_col = AOMMAX(sw_col,
                           AOMMIN(unscaled_src->y_width - sw_col - 1, pos_col));
          pos_row = AOMMAX(
              sw_row, AOMMIN(unscaled_src->y_height - sw_row - 1, pos_row));
          if (pos_col >= 0 && pos_col < unscaled_src->y_width - 64 &&
              pos_row >= 0 && pos_row < unscaled_src->y_height - 64) {
            src_y = unscaled_src->y_buffer + pos_row * src_ystride + pos_col;
            last_src_y = unscaled_last_src->y_buffer +
                         pos_row * last_src_ystride + pos_col;
            int best_intmv_col = 0;
            int best_intmv_row = 0;
            unsigned int y_sad = estimate_scroll_motion(
                cpi, src_y, last_src_y, src_ystride, last_src_ystride,
                BLOCK_128X128, pos_col, pos_row, &best_intmv_col,
                &best_intmv_row, sw_col, sw_row);
            unsigned int sad_thresh =
                (abs(best_intmv_col) > 150 || abs(best_intmv_row) > 150) ? 300
                                                                         : 150;
            if (y_sad < sad_thresh &&
                (abs(best_intmv_col) > 16 || abs(best_intmv_row) > 16)) {
              cpi->rc.high_motion_content_screen_rtc = 0;
              break;
            }
          }
        }
      }
    }
    // Pass the flag value to all layer frames.
    if (cpi->svc.number_spatial_layers > 1 ||
        cpi->svc.number_temporal_layers > 1) {
      SVC *svc = &cpi->svc;
      for (int sl = 0; sl < svc->number_spatial_layers; ++sl) {
        for (int tl = 1; tl < svc->number_temporal_layers; ++tl) {
          const int layer =
              LAYER_IDS_TO_IDX(sl, tl, svc->number_temporal_layers);
          LAYER_CONTEXT *lc = &svc->layer_context[layer];
          RATE_CONTROL *lrc = &lc->rc;
          lrc->high_motion_content_screen_rtc =
              rc->high_motion_content_screen_rtc;
        }
      }
    }
  }
  // Scene detection is only on base SLO, and using full/original resolution.
  // Pass the state to the upper spatial layers.
  if (cpi->svc.number_spatial_layers > 1) {
    SVC *svc = &cpi->svc;
    for (int sl = 0; sl < svc->number_spatial_layers; ++sl) {
      int tl = svc->temporal_layer_id;
      const int layer = LAYER_IDS_TO_IDX(sl, tl, svc->number_temporal_layers);
      LAYER_CONTEXT *lc = &svc->layer_context[layer];
      RATE_CONTROL *lrc = &lc->rc;
      lrc->high_source_sad = rc->high_source_sad;
      lrc->frame_source_sad = rc->frame_source_sad;
      lrc->avg_source_sad = rc->avg_source_sad;
      lrc->percent_blocks_with_motion = rc->percent_blocks_with_motion;
      lrc->max_block_source_sad = rc->max_block_source_sad;
    }
  }
}

// This is used as a reference when computing the source variance.
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

/*!\brief Compute spatial activity for frame,  1 pass real-time mode.
 *
 * Compute average spatial activity/variance for source frame over a
 * subset of superblocks.
 *
 * \ingroup rate_control
 * \param[in]       cpi          Top level encoder structure
 * \param[in]       src_y        Input source buffer for y channel.
 * \param[in]       src_ystride  Input source stride for y channel.
 *
 * \remark Nothing is returned. Instead the average spatial variance
 * computed is stored in flag \c cpi->rc.frame_spatial_variance.
 */
static void rc_spatial_act_onepass_rt(AV1_COMP *cpi, uint8_t *src_y,
                                      int src_ystride) {
  AV1_COMMON *const cm = &cpi->common;
  int num_mi_cols = cm->mi_params.mi_cols;
  int num_mi_rows = cm->mi_params.mi_rows;
  const BLOCK_SIZE bsize = BLOCK_64X64;
  // Loop over sub-sample of frame, compute average over 64x64 blocks.
  uint64_t avg_variance = 0;
  int num_samples = 0;
  int num_zero_var_blocks = 0;
  cpi->rc.perc_spatial_flat_blocks = 0;
  const int sb_size_by_mb = (cm->seq_params->sb_size == BLOCK_128X128)
                                ? (cm->seq_params->mib_size >> 1)
                                : cm->seq_params->mib_size;
  const int sb_cols = (num_mi_cols + sb_size_by_mb - 1) / sb_size_by_mb;
  const int sb_rows = (num_mi_rows + sb_size_by_mb - 1) / sb_size_by_mb;
  for (int sbi_row = 0; sbi_row < sb_rows; ++sbi_row) {
    for (int sbi_col = 0; sbi_col < sb_cols; ++sbi_col) {
      unsigned int sse;
      const unsigned int var =
          cpi->ppi->fn_ptr[bsize].vf(src_y, src_ystride, AV1_VAR_OFFS, 0, &sse);
      avg_variance += var;
      num_samples++;
      if (var == 0) num_zero_var_blocks++;
      src_y += 64;
    }
    src_y += (src_ystride << 6) - (sb_cols << 6);
  }
  if (num_samples > 0) {
    cpi->rc.perc_spatial_flat_blocks = 100 * num_zero_var_blocks / num_samples;
    avg_variance = avg_variance / num_samples;
  }
  cpi->rc.frame_spatial_variance = avg_variance >> 12;
}

/*!\brief Set the GF baseline interval for 1 pass real-time mode.
 *
 *
 * \ingroup rate_control
 * \param[in]       cpi          Top level encoder structure
 * \param[in]       frame_type   frame type
 *
 * \return Return GF update flag, and update the \c cpi->rc with
 * the next GF interval settings.
 */
static int set_gf_interval_update_onepass_rt(AV1_COMP *cpi,
                                             FRAME_TYPE frame_type) {
  RATE_CONTROL *const rc = &cpi->rc;
  int gf_update = 0;
  const int resize_pending = is_frame_resize_pending(cpi);
  // GF update based on frames_till_gf_update_due, also
  // force update on resize pending frame or for scene change.
  if ((resize_pending || rc->high_source_sad ||
       rc->frames_till_gf_update_due == 0) &&
      cpi->svc.temporal_layer_id == 0 && cpi->svc.spatial_layer_id == 0) {
    set_baseline_gf_interval(cpi, frame_type);
    gf_update = 1;
  }
  return gf_update;
}

static void resize_reset_rc(AV1_COMP *cpi, int resize_width, int resize_height,
                            int prev_width, int prev_height) {
  RATE_CONTROL *const rc = &cpi->rc;
  PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  SVC *const svc = &cpi->svc;
  int target_bits_per_frame;
  int active_worst_quality;
  int qindex;
  double tot_scale_change = (double)(resize_width * resize_height) /
                            (double)(prev_width * prev_height);
  // Disable the skip mv search for svc on resize frame.
  svc->skip_mvsearch_last = 0;
  svc->skip_mvsearch_gf = 0;
  svc->skip_mvsearch_altref = 0;
  // Reset buffer level to optimal, update target size.
  p_rc->buffer_level = p_rc->optimal_buffer_level;
  p_rc->bits_off_target = p_rc->optimal_buffer_level;
  rc->this_frame_target =
      av1_calc_pframe_target_size_one_pass_cbr(cpi, INTER_FRAME);
  target_bits_per_frame = rc->this_frame_target;
  if (tot_scale_change > 4.0)
    p_rc->avg_frame_qindex[INTER_FRAME] = rc->worst_quality;
  else if (tot_scale_change > 1.0)
    p_rc->avg_frame_qindex[INTER_FRAME] =
        (p_rc->avg_frame_qindex[INTER_FRAME] + rc->worst_quality) >> 1;
  active_worst_quality = calc_active_worst_quality_no_stats_cbr(cpi);
  qindex = av1_rc_regulate_q(cpi, target_bits_per_frame, rc->best_quality,
                             active_worst_quality, resize_width, resize_height);
  // If resize is down, check if projected q index is close to worst_quality,
  // and if so, reduce the rate correction factor (since likely can afford
  // lower q for resized frame).
  if (tot_scale_change < 1.0 && qindex > 90 * rc->worst_quality / 100)
    p_rc->rate_correction_factors[INTER_NORMAL] *= 0.85;
  // If resize is back up: check if projected q index is too much above the
  // previous index, and if so, reduce the rate correction factor
  // (since prefer to keep q for resized frame at least closet to previous q).
  // Also check if projected qindex is close to previous qindex, if so
  // increase correction factor (to push qindex higher and avoid overshoot).
  if (tot_scale_change >= 1.0) {
    if (tot_scale_change < 4.0 &&
        qindex > 130 * p_rc->last_q[INTER_FRAME] / 100)
      p_rc->rate_correction_factors[INTER_NORMAL] *= 0.8;
    if (qindex <= 120 * p_rc->last_q[INTER_FRAME] / 100)
      p_rc->rate_correction_factors[INTER_NORMAL] *= 1.5;
  }
  if (svc->number_temporal_layers > 1) {
    // Apply the same rate control reset to all temporal layers.
    for (int tl = 0; tl < svc->number_temporal_layers; tl++) {
      LAYER_CONTEXT *lc = NULL;
      lc = &svc->layer_context[svc->spatial_layer_id *
                                   svc->number_temporal_layers +
                               tl];
      lc->rc.resize_state = rc->resize_state;
      lc->p_rc.buffer_level = lc->p_rc.optimal_buffer_level;
      lc->p_rc.bits_off_target = lc->p_rc.optimal_buffer_level;
      lc->p_rc.rate_correction_factors[INTER_NORMAL] =
          p_rc->rate_correction_factors[INTER_NORMAL];
      lc->p_rc.avg_frame_qindex[INTER_FRAME] =
          p_rc->avg_frame_qindex[INTER_FRAME];
    }
  }
}

/*!\brief Check for resize based on Q, for 1 pass real-time mode.
 *
 * Check if we should resize, based on average QP from past x frames.
 * Only allow for resize at most 1/2 scale down for now, Scaling factor
 * for each step may be 3/4 or 1/2.
 *
 * \ingroup rate_control
 * \param[in]       cpi          Top level encoder structure
 *
 * \remark Return resized width/height in \c cpi->resize_pending_params,
 * and update some resize counters in \c rc.
 */
static void dynamic_resize_one_pass_cbr(AV1_COMP *cpi) {
  const AV1_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;
  PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  RESIZE_ACTION resize_action = NO_RESIZE;
  const int avg_qp_thr1 = 70;
  const int avg_qp_thr2 = 50;
  // Don't allow for resized frame to go below 160x90, resize in steps of 3/4.
  const int min_width = (160 * 4) / 3;
  const int min_height = (90 * 4) / 3;
  int down_size_on = 1;
  // Don't resize on key frame; reset the counters on key frame.
  if (cm->current_frame.frame_type == KEY_FRAME) {
    rc->resize_avg_qp = 0;
    rc->resize_count = 0;
    rc->resize_buffer_underflow = 0;
    return;
  }
  // No resizing down if frame size is below some limit.
  if ((cm->width * cm->height) < min_width * min_height) down_size_on = 0;

  // Resize based on average buffer underflow and QP over some window.
  // Ignore samples close to key frame, since QP is usually high after key.
  if (cpi->rc.frames_since_key > cpi->framerate) {
    const int window = AOMMIN(30, (int)(2 * cpi->framerate));
    rc->resize_avg_qp += p_rc->last_q[INTER_FRAME];
    if (cpi->ppi->p_rc.buffer_level <
        (int)(30 * p_rc->optimal_buffer_level / 100))
      ++rc->resize_buffer_underflow;
    ++rc->resize_count;
    // Check for resize action every "window" frames.
    if (rc->resize_count >= window) {
      int avg_qp = rc->resize_avg_qp / rc->resize_count;
      // Resize down if buffer level has underflowed sufficient amount in past
      // window, and we are at original or 3/4 of original resolution.
      // Resize back up if average QP is low, and we are currently in a resized
      // down state, i.e. 1/2 or 3/4 of original resolution.
      // Currently, use a flag to turn 3/4 resizing feature on/off.
      if (rc->resize_buffer_underflow > (rc->resize_count >> 2) &&
          down_size_on) {
        if (rc->resize_state == THREE_QUARTER) {
          resize_action = DOWN_ONEHALF;
          rc->resize_state = ONE_HALF;
        } else if (rc->resize_state == ORIG) {
          resize_action = DOWN_THREEFOUR;
          rc->resize_state = THREE_QUARTER;
        }
      } else if (rc->resize_state != ORIG &&
                 avg_qp < avg_qp_thr1 * cpi->rc.worst_quality / 100) {
        if (rc->resize_state == THREE_QUARTER ||
            avg_qp < avg_qp_thr2 * cpi->rc.worst_quality / 100) {
          resize_action = UP_ORIG;
          rc->resize_state = ORIG;
        } else if (rc->resize_state == ONE_HALF) {
          resize_action = UP_THREEFOUR;
          rc->resize_state = THREE_QUARTER;
        }
      }
      // Reset for next window measurement.
      rc->resize_avg_qp = 0;
      rc->resize_count = 0;
      rc->resize_buffer_underflow = 0;
    }
  }
  // If decision is to resize, reset some quantities, and check is we should
  // reduce rate correction factor,
  if (resize_action != NO_RESIZE) {
    int resize_width = cpi->oxcf.frm_dim_cfg.width;
    int resize_height = cpi->oxcf.frm_dim_cfg.height;
    int resize_scale_num = 1;
    int resize_scale_den = 1;
    if (resize_action == DOWN_THREEFOUR || resize_action == UP_THREEFOUR) {
      resize_scale_num = 3;
      resize_scale_den = 4;
    } else if (resize_action == DOWN_ONEHALF) {
      resize_scale_num = 1;
      resize_scale_den = 2;
    }
    resize_width = resize_width * resize_scale_num / resize_scale_den;
    resize_height = resize_height * resize_scale_num / resize_scale_den;
    resize_reset_rc(cpi, resize_width, resize_height, cm->width, cm->height);
  }
  return;
}

static inline int set_key_frame(AV1_COMP *cpi, unsigned int frame_flags) {
  RATE_CONTROL *const rc = &cpi->rc;
  AV1_COMMON *const cm = &cpi->common;
  SVC *const svc = &cpi->svc;

  // Very first frame has to be key frame.
  if (cm->current_frame.frame_number == 0) return 1;
  // Set key frame if forced by frame flags.
  if (frame_flags & FRAMEFLAGS_KEY) return 1;
  if (!cpi->ppi->use_svc) {
    // Non-SVC
    if (cpi->oxcf.kf_cfg.auto_key && rc->frames_to_key == 0) return 1;
  } else {
    // SVC
    if (svc->spatial_layer_id == 0 &&
        (cpi->oxcf.kf_cfg.auto_key &&
         (cpi->oxcf.kf_cfg.key_freq_max == 0 ||
          svc->current_superframe % cpi->oxcf.kf_cfg.key_freq_max == 0)))
      return 1;
  }

  return 0;
}

// Set to true if this frame is a recovery frame, for 1 layer RPS,
// and whether we should apply some boost (QP, adjust speed features, etc).
// Recovery frame here means frame whose closest reference suddenly
// switched from previous frame to one much further away.
// TODO(marpan): Consider adding on/off flag to SVC_REF_FRAME_CONFIG to
// allow more control for applications.
static bool set_flag_rps_bias_recovery_frame(const AV1_COMP *const cpi) {
  if (cpi->ppi->rtc_ref.set_ref_frame_config &&
      cpi->svc.number_temporal_layers == 1 &&
      cpi->svc.number_spatial_layers == 1 &&
      cpi->ppi->rtc_ref.reference_was_previous_frame) {
    int min_dist = av1_svc_get_min_ref_dist(cpi);
    // Only consider boost for this frame if its closest reference is further
    // than x frames away, using x = 4 for now.
    if (min_dist != INT_MAX && min_dist > 4) return true;
  }
  return false;
}

void av1_get_one_pass_rt_params(AV1_COMP *cpi, FRAME_TYPE *const frame_type,
                                const EncodeFrameInput *frame_input,
                                unsigned int frame_flags) {
  RATE_CONTROL *const rc = &cpi->rc;
  PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  AV1_COMMON *const cm = &cpi->common;
  GF_GROUP *const gf_group = &cpi->ppi->gf_group;
  SVC *const svc = &cpi->svc;
  ResizePendingParams *const resize_pending_params =
      &cpi->resize_pending_params;
  int target;
  const int layer =
      LAYER_IDS_TO_IDX(svc->spatial_layer_id, svc->temporal_layer_id,
                       svc->number_temporal_layers);
  if (cpi->oxcf.rc_cfg.max_consec_drop_ms > 0) {
    double framerate =
        cpi->framerate > 1 ? round(cpi->framerate) : cpi->framerate;
    rc->max_consec_drop = saturate_cast_double_to_int(
        ceil(cpi->oxcf.rc_cfg.max_consec_drop_ms * framerate / 1000));
  }
  if (cpi->ppi->use_svc) {
    av1_update_temporal_layer_framerate(cpi);
    av1_restore_layer_context(cpi);
  }
  cpi->ppi->rtc_ref.bias_recovery_frame = set_flag_rps_bias_recovery_frame(cpi);
  // Set frame type.
  if (set_key_frame(cpi, frame_flags)) {
    *frame_type = KEY_FRAME;
    p_rc->this_key_frame_forced =
        cm->current_frame.frame_number != 0 && rc->frames_to_key == 0;
    rc->frames_to_key = cpi->oxcf.kf_cfg.key_freq_max;
    p_rc->kf_boost = DEFAULT_KF_BOOST_RT;
    gf_group->update_type[cpi->gf_frame_index] = KF_UPDATE;
    gf_group->frame_type[cpi->gf_frame_index] = KEY_FRAME;
    gf_group->refbuf_state[cpi->gf_frame_index] = REFBUF_RESET;
    if (cpi->ppi->use_svc) {
      if (cm->current_frame.frame_number > 0)
        av1_svc_reset_temporal_layers(cpi, 1);
      svc->layer_context[layer].is_key_frame = 1;
    }
    rc->frame_number_encoded = 0;
    cpi->ppi->rtc_ref.non_reference_frame = 0;
    rc->static_since_last_scene_change = 0;
  } else {
    *frame_type = INTER_FRAME;
    gf_group->update_type[cpi->gf_frame_index] = LF_UPDATE;
    gf_group->frame_type[cpi->gf_frame_index] = INTER_FRAME;
    gf_group->refbuf_state[cpi->gf_frame_index] = REFBUF_UPDATE;
    if (cpi->ppi->use_svc) {
      LAYER_CONTEXT *lc = &svc->layer_context[layer];
      lc->is_key_frame =
          svc->spatial_layer_id == 0
              ? 0
              : svc->layer_context[svc->temporal_layer_id].is_key_frame;
    }
    // If the user is setting the reference structure with
    // set_ref_frame_config and did not set any references, set the
    // frame type to Intra-only.
    if (cpi->ppi->rtc_ref.set_ref_frame_config) {
      int no_references_set = 1;
      for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
        if (cpi->ppi->rtc_ref.reference[i]) {
          no_references_set = 0;
          break;
        }
      }

      // Set to intra_only_frame if no references are set.
      // The stream can start decoding on INTRA_ONLY_FRAME so long as the
      // layer with the intra_only_frame doesn't signal a reference to a slot
      // that hasn't been set yet.
      if (no_references_set) *frame_type = INTRA_ONLY_FRAME;
    }
  }
  if (cpi->active_map.enabled && cpi->rc.percent_blocks_inactive == 100) {
    rc->frame_source_sad = 0;
    rc->avg_source_sad = (3 * rc->avg_source_sad + rc->frame_source_sad) >> 2;
    rc->percent_blocks_with_motion = 0;
    rc->high_source_sad = 0;
  } else if (cpi->sf.rt_sf.check_scene_detection &&
             svc->spatial_layer_id == 0) {
    if (rc->prev_coded_width == cm->width &&
        rc->prev_coded_height == cm->height) {
      rc_scene_detection_onepass_rt(cpi, frame_input);
    } else {
      aom_free(cpi->src_sad_blk_64x64);
      cpi->src_sad_blk_64x64 = NULL;
    }
  }
  if (((*frame_type == KEY_FRAME && cpi->sf.rt_sf.rc_adjust_keyframe) ||
       (cpi->sf.rt_sf.rc_compute_spatial_var_sc && rc->high_source_sad)) &&
      svc->spatial_layer_id == 0 && cm->seq_params->bit_depth == 8 &&
      cpi->oxcf.rc_cfg.max_intra_bitrate_pct > 0)
    rc_spatial_act_onepass_rt(cpi, frame_input->source->y_buffer,
                              frame_input->source->y_stride);
  // Check for dynamic resize, for single spatial layer for now.
  // For temporal layers only check on base temporal layer.
  if (cpi->oxcf.resize_cfg.resize_mode == RESIZE_DYNAMIC) {
    if (svc->number_spatial_layers == 1 && svc->temporal_layer_id == 0)
      dynamic_resize_one_pass_cbr(cpi);
    if (rc->resize_state == THREE_QUARTER) {
      resize_pending_params->width = (3 + cpi->oxcf.frm_dim_cfg.width * 3) >> 2;
      resize_pending_params->height =
          (3 + cpi->oxcf.frm_dim_cfg.height * 3) >> 2;
    } else if (rc->resize_state == ONE_HALF) {
      resize_pending_params->width = (1 + cpi->oxcf.frm_dim_cfg.width) >> 1;
      resize_pending_params->height = (1 + cpi->oxcf.frm_dim_cfg.height) >> 1;
    } else {
      resize_pending_params->width = cpi->oxcf.frm_dim_cfg.width;
      resize_pending_params->height = cpi->oxcf.frm_dim_cfg.height;
    }
  } else if (is_frame_resize_pending(cpi)) {
    resize_reset_rc(cpi, resize_pending_params->width,
                    resize_pending_params->height, cm->width, cm->height);
  }
  if (svc->temporal_layer_id == 0) {
    rc->num_col_blscroll_last_tl0 = 0;
    rc->num_row_blscroll_last_tl0 = 0;
  }
  // Set the GF interval and update flag.
  if (!rc->rtc_external_ratectrl)
    set_gf_interval_update_onepass_rt(cpi, *frame_type);
  // Set target size.
  if (cpi->oxcf.rc_cfg.mode == AOM_CBR) {
    if (*frame_type == KEY_FRAME || *frame_type == INTRA_ONLY_FRAME) {
      target = av1_calc_iframe_target_size_one_pass_cbr(cpi);
    } else {
      target = av1_calc_pframe_target_size_one_pass_cbr(
          cpi, gf_group->update_type[cpi->gf_frame_index]);
    }
  } else {
    if (*frame_type == KEY_FRAME || *frame_type == INTRA_ONLY_FRAME) {
      target = av1_calc_iframe_target_size_one_pass_vbr(cpi);
    } else {
      target = av1_calc_pframe_target_size_one_pass_vbr(
          cpi, gf_group->update_type[cpi->gf_frame_index]);
    }
  }
  if (cpi->oxcf.rc_cfg.mode == AOM_Q)
    rc->active_worst_quality = cpi->oxcf.rc_cfg.cq_level;

  av1_rc_set_frame_target(cpi, target, cm->width, cm->height);
  rc->base_frame_target = target;
  cm->current_frame.frame_type = *frame_type;
  // For fixed mode SVC: if KSVC is enabled remove inter layer
  // prediction on spatial enhancement layer frames for frames
  // whose base is not KEY frame.
  if (cpi->ppi->use_svc && !svc->use_flexible_mode && svc->ksvc_fixed_mode &&
      svc->number_spatial_layers > 1 &&
      !svc->layer_context[layer].is_key_frame) {
    ExternalFlags *const ext_flags = &cpi->ext_flags;
    ext_flags->ref_frame_flags ^= AOM_GOLD_FLAG;
  }
}

#define CHECK_INTER_LAYER_PRED(ref_frame)                         \
  ((cpi->ref_frame_flags & av1_ref_frame_flag_list[ref_frame]) && \
   (av1_check_ref_is_low_spatial_res_super_frame(cpi, ref_frame)))

int av1_encodedframe_overshoot_cbr(AV1_COMP *cpi, int *q) {
  AV1_COMMON *const cm = &cpi->common;
  PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  double rate_correction_factor =
      cpi->ppi->p_rc.rate_correction_factors[INTER_NORMAL];
  const int target_size = cpi->rc.avg_frame_bandwidth;
  double new_correction_factor;
  int target_bits_per_mb;
  double q2;
  int enumerator;
  int inter_layer_pred_on = 0;
  int is_screen_content = (cpi->oxcf.tune_cfg.content == AOM_CONTENT_SCREEN);
  cpi->cyclic_refresh->counter_encode_maxq_scene_change = 0;
  if (cpi->svc.spatial_layer_id > 0) {
    // For spatial layers: check if inter-layer (spatial) prediction is used
    // (check if any reference is being used that is the lower spatial layer),
    inter_layer_pred_on = CHECK_INTER_LAYER_PRED(LAST_FRAME) ||
                          CHECK_INTER_LAYER_PRED(GOLDEN_FRAME) ||
                          CHECK_INTER_LAYER_PRED(ALTREF_FRAME);
  }
  // If inter-layer prediction is on: we expect to pull up the quality from
  // the lower spatial layer, so we can use a lower q.
  if (cpi->svc.spatial_layer_id > 0 && inter_layer_pred_on) {
    *q = (cpi->rc.worst_quality + *q) >> 1;
  } else {
    // For easy scene changes used lower QP, otherwise set max-q.
    // If rt_sf->compute_spatial_var_sc is enabled relax the max-q
    // condition based on frame spatial variance.
    if (cpi->sf.rt_sf.rc_compute_spatial_var_sc) {
      if (cpi->rc.frame_spatial_variance < 100) {
        *q = (cpi->rc.worst_quality + *q) >> 1;
      } else if (cpi->rc.frame_spatial_variance < 400 ||
                 (cpi->rc.frame_source_sad < 80000 &&
                  cpi->rc.frame_spatial_variance < 1000)) {
        *q = (3 * cpi->rc.worst_quality + *q) >> 2;
      } else {
        *q = cpi->rc.worst_quality;
      }
    } else {
      // Set a larger QP.
      const uint64_t sad_thr = 64 * 64 * 32;
      if (cm->width * cm->height >= 1280 * 720 &&
          (p_rc->buffer_level > (p_rc->optimal_buffer_level) >> 1) &&
          cpi->rc.avg_source_sad < sad_thr) {
        *q = (*q + cpi->rc.worst_quality) >> 1;
      } else {
        *q = (3 * cpi->rc.worst_quality + *q) >> 2;
      }
      // If we arrive here for screen content: use the max-q set by the user.
      if (is_screen_content) *q = cpi->rc.worst_quality;
    }
  }
  // Adjust avg_frame_qindex, buffer_level, and rate correction factors, as
  // these parameters will affect QP selection for subsequent frames. If they
  // have settled down to a very different (low QP) state, then not adjusting
  // them may cause next frame to select low QP and overshoot again.
  p_rc->avg_frame_qindex[INTER_FRAME] = *q;
  p_rc->buffer_level = p_rc->optimal_buffer_level;
  p_rc->bits_off_target = p_rc->optimal_buffer_level;
  // Reset rate under/over-shoot flags.
  cpi->rc.rc_1_frame = 0;
  cpi->rc.rc_2_frame = 0;
  // Adjust rate correction factor.
  target_bits_per_mb =
      (int)(((uint64_t)target_size << BPER_MB_NORMBITS) / cm->mi_params.MBs);
  // Reset rate correction factor: for now base it on target_bits_per_mb
  // and qp (==max_QP). This comes from the inverse computation of
  // av1_rc_bits_per_mb().
  q2 = av1_convert_qindex_to_q(*q, cm->seq_params->bit_depth);
  enumerator = get_bpmb_enumerator(INTER_NORMAL, is_screen_content);
  new_correction_factor = (double)target_bits_per_mb * q2 / enumerator;
  if (new_correction_factor > rate_correction_factor) {
    rate_correction_factor =
        (new_correction_factor + rate_correction_factor) / 2.0;
    if (rate_correction_factor > MAX_BPB_FACTOR)
      rate_correction_factor = MAX_BPB_FACTOR;
    cpi->ppi->p_rc.rate_correction_factors[INTER_NORMAL] =
        rate_correction_factor;
  }
  // For temporal layers: reset the rate control parameters across all
  // temporal layers. Only do it for spatial enhancement layers when
  // inter_layer_pred_on is not set (off).
  if (cpi->svc.number_temporal_layers > 1 &&
      (cpi->svc.spatial_layer_id == 0 || inter_layer_pred_on == 0)) {
    SVC *svc = &cpi->svc;
    for (int tl = 0; tl < svc->number_temporal_layers; ++tl) {
      int sl = svc->spatial_layer_id;
      const int layer = LAYER_IDS_TO_IDX(sl, tl, svc->number_temporal_layers);
      LAYER_CONTEXT *lc = &svc->layer_context[layer];
      RATE_CONTROL *lrc = &lc->rc;
      PRIMARY_RATE_CONTROL *lp_rc = &lc->p_rc;
      lp_rc->avg_frame_qindex[INTER_FRAME] = *q;
      lp_rc->buffer_level = lp_rc->optimal_buffer_level;
      lp_rc->bits_off_target = lp_rc->optimal_buffer_level;
      lrc->rc_1_frame = 0;
      lrc->rc_2_frame = 0;
      lp_rc->rate_correction_factors[INTER_NORMAL] = rate_correction_factor;
    }
  }
  return 1;
}

int av1_postencode_drop_cbr(AV1_COMP *cpi, size_t *size) {
  PRIMARY_RATE_CONTROL *const p_rc = &cpi->ppi->p_rc;
  size_t frame_size = *size << 3;
  const int64_t new_buffer_level =
      p_rc->buffer_level + cpi->rc.avg_frame_bandwidth - (int64_t)frame_size;
  // Drop if new buffer level (given the encoded frame size) goes below a
  // threshold and encoded frame size is much larger than per-frame-bandwidth.
  // If the frame is already labelled as scene change (high_source_sad = 1)
  // or the QP is close to max, then no need to drop.
  const int qp_thresh = 3 * (cpi->rc.worst_quality >> 2);
  const int64_t buffer_thresh = p_rc->optimal_buffer_level >> 2;
  if (!cpi->rc.high_source_sad && new_buffer_level < buffer_thresh &&
      frame_size > 8 * (unsigned int)cpi->rc.avg_frame_bandwidth &&
      cpi->common.quant_params.base_qindex < qp_thresh) {
    *size = 0;
    cpi->is_dropped_frame = true;
    restore_all_coding_context(cpi);
    av1_rc_postencode_update_drop_frame(cpi);
    // Force max_q on next fame. Reset some RC parameters.
    cpi->rc.force_max_q = 1;
    p_rc->avg_frame_qindex[INTER_FRAME] = cpi->rc.worst_quality;
    p_rc->buffer_level = p_rc->optimal_buffer_level;
    p_rc->bits_off_target = p_rc->optimal_buffer_level;
    cpi->rc.rc_1_frame = 0;
    cpi->rc.rc_2_frame = 0;
    if (cpi->svc.number_spatial_layers > 1 ||
        cpi->svc.number_temporal_layers > 1) {
      SVC *svc = &cpi->svc;
      // Postencode drop is only checked on base spatial layer,
      // for now if max-q is set on base we force it on all layers.
      for (int sl = 0; sl < svc->number_spatial_layers; ++sl) {
        for (int tl = 0; tl < svc->number_temporal_layers; ++tl) {
          const int layer =
              LAYER_IDS_TO_IDX(sl, tl, svc->number_temporal_layers);
          LAYER_CONTEXT *lc = &svc->layer_context[layer];
          RATE_CONTROL *lrc = &lc->rc;
          PRIMARY_RATE_CONTROL *lp_rc = &lc->p_rc;
          // Force max_q on next fame. Reset some RC parameters.
          lrc->force_max_q = 1;
          lp_rc->avg_frame_qindex[INTER_FRAME] = cpi->rc.worst_quality;
          lp_rc->buffer_level = lp_rc->optimal_buffer_level;
          lp_rc->bits_off_target = lp_rc->optimal_buffer_level;
          lrc->rc_1_frame = 0;
          lrc->rc_2_frame = 0;
        }
      }
    }
    return 1;
  }
  return 0;
}
