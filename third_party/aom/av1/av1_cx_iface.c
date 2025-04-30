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
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "config/aom_config.h"
#include "config/aom_version.h"

#include "aom/aomcx.h"
#include "aom/aom_encoder.h"
#include "aom/aom_external_partition.h"
#include "aom/aom_image.h"
#include "aom/internal/aom_codec_internal.h"
#include "aom_dsp/flow_estimation/flow_estimation.h"
#include "aom_mem/aom_mem.h"
#include "aom_scale/yv12config.h"
#include "aom_util/aom_pthread.h"

#include "av1/av1_cx_iface.h"
#include "av1/av1_iface_common.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/enums.h"
#include "av1/common/quant_common.h"
#include "av1/common/scale.h"
#include "av1/encoder/bitstream.h"
#include "av1/encoder/enc_enums.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/encoder_alloc.h"
#include "av1/encoder/encoder_utils.h"
#include "av1/encoder/ethread.h"
#include "av1/encoder/external_partition.h"
#include "av1/encoder/firstpass.h"
#include "av1/encoder/lookahead.h"
#include "av1/encoder/rc_utils.h"
#include "av1/arg_defs.h"

#include "common/args_helper.h"

struct av1_extracfg {
  int cpu_used;
  unsigned int enable_auto_alt_ref;
  unsigned int enable_auto_bwd_ref;
  unsigned int noise_sensitivity;
  unsigned int sharpness;
  unsigned int static_thresh;
  unsigned int row_mt;
  unsigned int fp_mt;
  unsigned int tile_columns;  // log2 number of tile columns
  unsigned int tile_rows;     // log2 number of tile rows
  unsigned int auto_tiles;
  unsigned int enable_tpl_model;
  unsigned int enable_keyframe_filtering;
  unsigned int arnr_max_frames;
  unsigned int arnr_strength;
  unsigned int min_gf_interval;
  unsigned int max_gf_interval;
  unsigned int gf_min_pyr_height;
  unsigned int gf_max_pyr_height;
  aom_tune_metric tuning;
  const char *vmaf_model_path;
  const char *partition_info_path;
  unsigned int enable_rate_guide_deltaq;
  const char *rate_distribution_info;
  aom_dist_metric dist_metric;
  unsigned int cq_level;  // constrained quality level
  unsigned int rc_max_intra_bitrate_pct;
  unsigned int rc_max_inter_bitrate_pct;
  unsigned int gf_cbr_boost_pct;
  unsigned int lossless;
  unsigned int enable_cdef;
  unsigned int enable_restoration;
  unsigned int force_video_mode;
  unsigned int enable_obmc;
  unsigned int disable_trellis_quant;
  unsigned int enable_qm;
  unsigned int qm_y;
  unsigned int qm_u;
  unsigned int qm_v;
  unsigned int qm_min;
  unsigned int qm_max;
  unsigned int num_tg;
  unsigned int mtu_size;

  aom_timing_info_type_t timing_info_type;
  unsigned int frame_parallel_decoding_mode;
  int enable_dual_filter;
  unsigned int enable_chroma_deltaq;
  AQ_MODE aq_mode;
  DELTAQ_MODE deltaq_mode;
  int deltaq_strength;
  int deltalf_mode;
  unsigned int frame_periodic_boost;
  aom_tune_content content;
  aom_color_primaries_t color_primaries;
  aom_transfer_characteristics_t transfer_characteristics;
  aom_matrix_coefficients_t matrix_coefficients;
  aom_chroma_sample_position_t chroma_sample_position;
  int color_range;
  int render_width;
  int render_height;
  aom_superblock_size_t superblock_size;
  unsigned int single_tile_decoding;
  int error_resilient_mode;
  int s_frame_mode;

  int film_grain_test_vector;
  const char *film_grain_table_filename;
  unsigned int motion_vector_unit_test;
#if CONFIG_FPMT_TEST
  unsigned int fpmt_unit_test;
#endif
  unsigned int cdf_update_mode;
  int enable_rect_partitions;    // enable rectangular partitions for sequence
  int enable_ab_partitions;      // enable AB partitions for sequence
  int enable_1to4_partitions;    // enable 1:4 and 4:1 partitions for sequence
  int min_partition_size;        // min partition size [4,8,16,32,64,128]
  int max_partition_size;        // max partition size [4,8,16,32,64,128]
  int enable_intra_edge_filter;  // enable intra-edge filter for sequence
  int enable_order_hint;         // enable order hint for sequence
  int enable_tx64;               // enable 64-pt transform usage for sequence
  int enable_flip_idtx;          // enable flip and identity transform types
  int enable_rect_tx;        // enable rectangular transform usage for sequence
  int enable_dist_wtd_comp;  // enable dist wtd compound for sequence
  int max_reference_frames;  // maximum number of references per frame
  int enable_reduced_reference_set;  // enable reduced set of references
  int enable_ref_frame_mvs;          // sequence level
  int allow_ref_frame_mvs;           // frame level
  int enable_masked_comp;            // enable masked compound for sequence
  int enable_onesided_comp;          // enable one sided compound for sequence
  int enable_interintra_comp;        // enable interintra compound for sequence
  int enable_smooth_interintra;      // enable smooth interintra mode usage
  int enable_diff_wtd_comp;          // enable diff-wtd compound usage
  int enable_interinter_wedge;       // enable interinter-wedge compound usage
  int enable_interintra_wedge;       // enable interintra-wedge compound usage
  int enable_global_motion;          // enable global motion usage for sequence
  int enable_warped_motion;          // sequence level
  int allow_warped_motion;           // frame level
  int enable_filter_intra;           // enable filter intra for sequence
  int enable_smooth_intra;           // enable smooth intra modes for sequence
  int enable_paeth_intra;            // enable Paeth intra mode for sequence
  int enable_cfl_intra;              // enable CFL uv intra mode for sequence
  int enable_directional_intra;      // enable directional modes for sequence
  int enable_diagonal_intra;  // enable D45 to D203 intra modes for sequence
  int enable_superres;
  int enable_overlay;  // enable overlay for filtered arf frames
  int enable_palette;
  int enable_intrabc;
  int enable_angle_delta;
#if CONFIG_DENOISE
  float noise_level;
  int noise_block_size;
  int enable_dnl_denoising;
#endif

  unsigned int enable_low_complexity_decode;
  unsigned int chroma_subsampling_x;
  unsigned int chroma_subsampling_y;
  int reduced_tx_type_set;
  int use_intra_dct_only;
  int use_inter_dct_only;
  int use_intra_default_tx_only;
  int enable_tx_size_search;
  int quant_b_adapt;
  unsigned int vbr_corpus_complexity_lap;
  AV1_LEVEL target_seq_level_idx[MAX_NUM_OPERATING_POINTS];
  // Bit mask to specify which tier each of the 32 possible operating points
  // conforms to.
  unsigned int tier_mask;
  // min_cr / 100 is the target minimum compression ratio for each frame.
  unsigned int min_cr;
  COST_UPDATE_TYPE coeff_cost_upd_freq;
  COST_UPDATE_TYPE mode_cost_upd_freq;
  COST_UPDATE_TYPE mv_cost_upd_freq;
  COST_UPDATE_TYPE dv_cost_upd_freq;
  unsigned int ext_tile_debug;
  unsigned int sb_multipass_unit_test;
  // Total number of passes. If this number is -1, then we assume passes = 1 or
  // 2 (passes = 1 if pass == AOM_RC_ONE_PASS and passes = 2 otherwise).
  int passes;
  int fwd_kf_dist;

  LOOPFILTER_CONTROL loopfilter_control;
  // Indicates if the application of post-processing filters should be skipped
  // on reconstructed frame.
  unsigned int skip_postproc_filtering;
  // the name of the second pass output file when passes > 2
  const char *two_pass_output;
  const char *second_pass_log;
  // Automatically determine whether to disable several intra tools
  // when "--deltaq-mode=3" is true.
  // Default as 0.
  // When set to 1, the encoder will analyze the reconstruction quality
  // as compared to the source image in the preprocessing pass.
  // If the recontruction quality is considered high enough, we disable
  // the following intra coding tools, for better encoding speed:
  // "--enable_smooth_intra",
  // "--enable_paeth_intra",
  // "--enable_cfl_intra",
  // "--enable_diagonal_intra".
  int auto_intra_tools_off;
  int strict_level_conformance;
  int kf_max_pyr_height;
  int sb_qp_sweep;
};

#if !CONFIG_REALTIME_ONLY
static const struct av1_extracfg default_extra_cfg = {
  0,              // cpu_used
  1,              // enable_auto_alt_ref
  0,              // enable_auto_bwd_ref
  0,              // noise_sensitivity
  0,              // sharpness
  0,              // static_thresh
  1,              // row_mt
  0,              // fp_mt
  0,              // tile_columns
  0,              // tile_rows
  0,              // auto_tiles
  1,              // enable_tpl_model
  1,              // enable_keyframe_filtering
  7,              // arnr_max_frames
  5,              // arnr_strength
  0,              // min_gf_interval; 0 -> default decision
  0,              // max_gf_interval; 0 -> default decision
  0,              // gf_min_pyr_height
  5,              // gf_max_pyr_height
  AOM_TUNE_PSNR,  // tuning
  "/usr/local/share/model/vmaf_v0.6.1.json",  // VMAF model path
  ".",                                        // partition info path
  0,                                          // enable rate guide deltaq
  "./rate_map.txt",                           // rate distribution input
  AOM_DIST_METRIC_PSNR,                       // dist_metric
  10,                                         // cq_level
  0,                                          // rc_max_intra_bitrate_pct
  0,                                          // rc_max_inter_bitrate_pct
  0,                                          // gf_cbr_boost_pct
  0,                                          // lossless
  1,                                          // enable_cdef
  1,                                          // enable_restoration
  0,                                          // force_video_mode
  1,                                          // enable_obmc
  3,                                          // disable_trellis_quant
  0,                                          // enable_qm
  DEFAULT_QM_Y,                               // qm_y
  DEFAULT_QM_U,                               // qm_u
  DEFAULT_QM_V,                               // qm_v
  DEFAULT_QM_FIRST,                           // qm_min
  DEFAULT_QM_LAST,                            // qm_max
  1,                                          // max number of tile groups
  0,                                          // mtu_size
  AOM_TIMING_UNSPECIFIED,       // No picture timing signaling in bitstream
  0,                            // frame_parallel_decoding_mode
  1,                            // enable dual filter
  0,                            // enable delta quant in chroma planes
  NO_AQ,                        // aq_mode
  DELTA_Q_OBJECTIVE,            // deltaq_mode
  100,                          // deltaq_strength
  0,                            // delta lf mode
  0,                            // frame_periodic_boost
  AOM_CONTENT_DEFAULT,          // content
  AOM_CICP_CP_UNSPECIFIED,      // CICP color primaries
  AOM_CICP_TC_UNSPECIFIED,      // CICP transfer characteristics
  AOM_CICP_MC_UNSPECIFIED,      // CICP matrix coefficients
  AOM_CSP_UNKNOWN,              // chroma sample position
  0,                            // color range
  0,                            // render width
  0,                            // render height
  AOM_SUPERBLOCK_SIZE_DYNAMIC,  // superblock_size
  1,                            // this depends on large_scale_tile.
  0,                            // error_resilient_mode off by default.
  0,                            // s_frame_mode off by default.
  0,                            // film_grain_test_vector
  NULL,                         // film_grain_table_filename
  0,                            // motion_vector_unit_test
#if CONFIG_FPMT_TEST
  0,  // fpmt_unit_test
#endif
  1,    // CDF update mode
  1,    // enable rectangular partitions
  1,    // enable ab shape partitions
  1,    // enable 1:4 and 4:1 partitions
  4,    // min_partition_size
  128,  // max_partition_size
  1,    // enable intra edge filter
  1,    // frame order hint
  1,    // enable 64-pt transform usage
  1,    // enable flip and identity transform
  1,    // enable rectangular transform usage
  1,    // dist-wtd compound
  7,    // max_reference_frames
  0,    // enable_reduced_reference_set
  1,    // enable_ref_frame_mvs sequence level
  1,    // allow ref_frame_mvs frame level
  1,    // enable masked compound at sequence level
  1,    // enable one sided compound at sequence level
  1,    // enable interintra compound at sequence level
  1,    // enable smooth interintra mode
  1,    // enable difference-weighted compound
  1,    // enable interinter wedge compound
  1,    // enable interintra wedge compound
  1,    // enable_global_motion usage
  1,    // enable_warped_motion at sequence level
  1,    // allow_warped_motion at frame level
  1,    // enable filter intra at sequence level
  1,    // enable smooth intra modes usage for sequence
  1,    // enable Paeth intra mode usage for sequence
  1,    // enable CFL uv intra mode usage for sequence
  1,    // enable directional intra mode usage for sequence
  1,    // enable D45 to D203 intra mode usage for sequence
  1,    // superres
  1,    // enable overlay
  1,    // enable palette
  1,    // enable intrabc
  1,    // enable angle delta
#if CONFIG_DENOISE
  0,   // noise_level
  32,  // noise_block_size
  1,   // enable_dnl_denoising
#endif

  0,  // enable_low_complexity_decode
  0,  // chroma_subsampling_x
  0,  // chroma_subsampling_y
  0,  // reduced_tx_type_set
  0,  // use_intra_dct_only
  0,  // use_inter_dct_only
  0,  // use_intra_default_tx_only
  1,  // enable_tx_size_search
  0,  // quant_b_adapt
  0,  // vbr_corpus_complexity_lap
  {
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
  },               // target_seq_level_idx
  0,               // tier_mask
  0,               // min_cr
  COST_UPD_SB,     // coeff_cost_upd_freq
  COST_UPD_SB,     // mode_cost_upd_freq
  COST_UPD_SB,     // mv_cost_upd_freq
  COST_UPD_SB,     // dv_cost_upd_freq
  0,               // ext_tile_debug
  0,               // sb_multipass_unit_test
  -1,              // passes
  -1,              // fwd_kf_dist
  LOOPFILTER_ALL,  // loopfilter_control
  0,               // skip_postproc_filtering
  NULL,            // two_pass_output
  NULL,            // second_pass_log
  0,               // auto_intra_tools_off
  0,               // strict_level_conformance
  -1,              // kf_max_pyr_height
  0,               // sb_qp_sweep
};
#else
// Some settings are changed for realtime only build.
static const struct av1_extracfg default_extra_cfg = {
  10,             // cpu_used
  1,              // enable_auto_alt_ref
  0,              // enable_auto_bwd_ref
  0,              // noise_sensitivity
  0,              // sharpness
  0,              // static_thresh
  1,              // row_mt
  0,              // fp_mt
  0,              // tile_columns
  0,              // tile_rows
  0,              // auto_tiles
  0,              // enable_tpl_model
  0,              // enable_keyframe_filtering
  7,              // arnr_max_frames
  5,              // arnr_strength
  0,              // min_gf_interval; 0 -> default decision
  0,              // max_gf_interval; 0 -> default decision
  0,              // gf_min_pyr_height
  5,              // gf_max_pyr_height
  AOM_TUNE_PSNR,  // tuning
  "/usr/local/share/model/vmaf_v0.6.1.json",  // VMAF model path
  ".",                                        // partition info path
  0,                                          // enable rate guide deltaq
  "./rate_map.txt",                           // rate distribution input
  AOM_DIST_METRIC_PSNR,                       // dist_metric
  10,                                         // cq_level
  300,                                        // rc_max_intra_bitrate_pct
  0,                                          // rc_max_inter_bitrate_pct
  0,                                          // gf_cbr_boost_pct
  0,                                          // lossless
  1,                                          // enable_cdef
  0,                                          // enable_restoration
  0,                                          // force_video_mode
  0,                                          // enable_obmc
  3,                                          // disable_trellis_quant
  0,                                          // enable_qm
  DEFAULT_QM_Y,                               // qm_y
  DEFAULT_QM_U,                               // qm_u
  DEFAULT_QM_V,                               // qm_v
  DEFAULT_QM_FIRST,                           // qm_min
  DEFAULT_QM_LAST,                            // qm_max
  1,                                          // max number of tile groups
  0,                                          // mtu_size
  AOM_TIMING_UNSPECIFIED,       // No picture timing signaling in bitstream
  0,                            // frame_parallel_decoding_mode
  0,                            // enable dual filter
  0,                            // enable delta quant in chroma planes
  CYCLIC_REFRESH_AQ,            // aq_mode
  NO_DELTA_Q,                   // deltaq_mode
  100,                          // deltaq_strength
  0,                            // delta lf mode
  0,                            // frame_periodic_boost
  AOM_CONTENT_DEFAULT,          // content
  AOM_CICP_CP_UNSPECIFIED,      // CICP color primaries
  AOM_CICP_TC_UNSPECIFIED,      // CICP transfer characteristics
  AOM_CICP_MC_UNSPECIFIED,      // CICP matrix coefficients
  AOM_CSP_UNKNOWN,              // chroma sample position
  0,                            // color range
  0,                            // render width
  0,                            // render height
  AOM_SUPERBLOCK_SIZE_DYNAMIC,  // superblock_size
  1,                            // this depends on large_scale_tile.
  0,                            // error_resilient_mode off by default.
  0,                            // s_frame_mode off by default.
  0,                            // film_grain_test_vector
  NULL,                         // film_grain_table_filename
  0,                            // motion_vector_unit_test
#if CONFIG_FPMT_TEST
  0,                            // fpmt_unit_test
#endif
  1,                            // CDF update mode
  0,                            // enable rectangular partitions
  0,                            // enable ab shape partitions
  0,                            // enable 1:4 and 4:1 partitions
  4,                            // min_partition_size
  128,                          // max_partition_size
  0,                            // enable intra edge filter
  0,                            // frame order hint
  0,                            // enable 64-pt transform usage
  1,                            // enable flip and identity transform
  1,                            // enable rectangular transform usage
  0,                            // dist-wtd compound
  3,                            // max_reference_frames
  0,                            // enable_reduced_reference_set
  0,                            // enable_ref_frame_mvs sequence level
  0,                            // allow ref_frame_mvs frame level
  0,                            // enable masked compound at sequence level
  0,                            // enable one sided compound at sequence level
  0,                            // enable interintra compound at sequence level
  0,                            // enable smooth interintra mode
  0,                            // enable difference-weighted compound
  0,                            // enable interinter wedge compound
  0,                            // enable interintra wedge compound
  0,                            // enable_global_motion usage
  0,                            // enable_warped_motion at sequence level
  0,                            // allow_warped_motion at frame level
  0,                            // enable filter intra at sequence level
  0,                            // enable smooth intra modes usage for sequence
  0,                            // enable Paeth intra mode usage for sequence
  0,                            // enable CFL uv intra mode usage for sequence
  1,   // enable directional intra mode usage for sequence
  1,   // enable D45 to D203 intra mode usage for sequence
  0,   // superres
  0,   // enable overlay
  1,   // enable palette
  0,   // enable intrabc
  0,   // enable angle delta
#if CONFIG_DENOISE
  0,   // noise_level
  32,  // noise_block_size
  1,   // enable_dnl_denoising
#endif

  0,  // enable_low_complexity_decode
  0,  // chroma_subsampling_x
  0,  // chroma_subsampling_y
  0,  // reduced_tx_type_set
  0,  // use_intra_dct_only
  0,  // use_inter_dct_only
  1,  // use_intra_default_tx_only
  1,  // enable_tx_size_search
  0,  // quant_b_adapt
  0,  // vbr_corpus_complexity_lap
  {
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
  },               // target_seq_level_idx
  0,               // tier_mask
  0,               // min_cr
  COST_UPD_OFF,    // coeff_cost_upd_freq
  COST_UPD_OFF,    // mode_cost_upd_freq
  COST_UPD_OFF,    // mv_cost_upd_freq
  COST_UPD_OFF,    // dv_cost_upd_freq
  0,               // ext_tile_debug
  0,               // sb_multipass_unit_test
  -1,              // passes
  -1,              // fwd_kf_dist
  LOOPFILTER_ALL,  // loopfilter_control
  0,               // skip_postproc_filtering
  NULL,            // two_pass_output
  NULL,            // second_pass_log
  0,               // auto_intra_tools_off
  0,               // strict_level_conformance
  -1,              // kf_max_pyr_height
  0,               // sb_qp_sweep
};
#endif

struct aom_codec_alg_priv {
  aom_codec_priv_t base;
  aom_codec_enc_cfg_t cfg;
  struct av1_extracfg extra_cfg;
  aom_rational64_t timestamp_ratio;
  aom_codec_pts_t pts_offset;
  unsigned char pts_offset_initialized;
  AV1EncoderConfig oxcf;
  AV1_PRIMARY *ppi;
  unsigned char *cx_data;
  size_t cx_data_sz;
  size_t pending_cx_data_sz;
  aom_image_t preview_img;
  aom_enc_frame_flags_t next_frame_flags;
  aom_codec_pkt_list_decl(256) pkt_list;
  unsigned int fixed_kf_cntr;
  // BufferPool that holds all reference frames.
  BufferPool *buffer_pool;

  // lookahead instance variables
  BufferPool *buffer_pool_lap;
  FIRSTPASS_STATS *frame_stats_buffer;
  // Number of stats buffers required for look ahead
  int num_lap_buffers;
  STATS_BUFFER_CTX stats_buf_context;
  bool monochrome_on_init;
};

static inline int gcd(int64_t a, int b) {
  int remainder;
  while (b > 0) {
    remainder = (int)(a % b);
    a = b;
    b = remainder;
  }

  return (int)a;
}

static void reduce_ratio(aom_rational64_t *ratio) {
  const int denom = gcd(ratio->num, ratio->den);
  ratio->num /= denom;
  ratio->den /= denom;
}

// Called by encoder_encode() only. Must not be called by encoder_init()
// because the `error` paramerer will be destroyed by aom_codec_enc_init_ver()
// after encoder_init() returns an error. See the "IMPORTANT" comment in
// aom_codec_enc_init_ver().
static aom_codec_err_t update_error_state(
    aom_codec_alg_priv_t *ctx, const struct aom_internal_error_info *error) {
  const aom_codec_err_t res = error->error_code;

  if (res != AOM_CODEC_OK)
    ctx->base.err_detail = error->has_detail ? error->detail : NULL;

  return res;
}

// This function deep copies a string src to *dst. For default string we will
// use a string literal, and otherwise we will allocate memory for the string.
static aom_codec_err_t allocate_and_set_string(const char *src,
                                               const char *default_src,
                                               const char **dst,
                                               char *err_detail) {
  if (!src) {
    snprintf(err_detail, ARG_ERR_MSG_MAX_LEN,
             "Null pointer given to a string parameter.");
    return AOM_CODEC_INVALID_PARAM;
  }
  if (*dst && strcmp(src, *dst) == 0) return AOM_CODEC_OK;
  // If the input is exactly the same as default, we will use the string
  // literal, so do not free here.
  if (*dst != default_src) {
    aom_free((void *)*dst);
  }

  if (default_src && strcmp(src, default_src) == 0) {
    // default_src should be a string literal
    *dst = default_src;
  } else {
    size_t len = strlen(src) + 1;
    char *tmp = aom_malloc(len * sizeof(*tmp));
    if (!tmp) {
      snprintf(err_detail, ARG_ERR_MSG_MAX_LEN,
               "Failed to allocate memory for copying parameters.");
      return AOM_CODEC_MEM_ERROR;
    }
    memcpy(tmp, src, len);
    *dst = tmp;
  }
  return 0;
}

#undef ERROR
#define ERROR(str)                  \
  do {                              \
    ctx->base.err_detail = str;     \
    return AOM_CODEC_INVALID_PARAM; \
  } while (0)

#define RANGE_CHECK(p, memb, lo, hi)                   \
  do {                                                 \
    if (!((p)->memb >= (lo) && (p)->memb <= (hi)))     \
      ERROR(#memb " out of range [" #lo ".." #hi "]"); \
  } while (0)

#define RANGE_CHECK_HI(p, memb, hi)                                     \
  do {                                                                  \
    if (!((p)->memb <= (hi))) ERROR(#memb " out of range [.." #hi "]"); \
  } while (0)

#define RANGE_CHECK_BOOL(p, memb)                                     \
  do {                                                                \
    if (!!((p)->memb) != (p)->memb) ERROR(#memb " expected boolean"); \
  } while (0)

static aom_codec_err_t validate_config(aom_codec_alg_priv_t *ctx,
                                       const aom_codec_enc_cfg_t *cfg,
                                       const struct av1_extracfg *extra_cfg) {
  RANGE_CHECK(cfg, g_w, 1, 65536);                        // 16 bits available
  RANGE_CHECK(cfg, g_h, 1, 65536);                        // 16 bits available
  RANGE_CHECK_HI(cfg, g_forced_max_frame_width, 65536);   // 16 bits available
  RANGE_CHECK_HI(cfg, g_forced_max_frame_height, 65536);  // 16 bits available
  if (cfg->g_forced_max_frame_width) {
    RANGE_CHECK_HI(cfg, g_w, cfg->g_forced_max_frame_width);
  }
  if (cfg->g_forced_max_frame_height) {
    RANGE_CHECK_HI(cfg, g_h, cfg->g_forced_max_frame_height);
  }
  // To avoid integer overflows when multiplying width by height (or values
  // derived from width and height) using the int type, impose a maximum frame
  // area (width * height) of 2^30.
  const unsigned int max_frame_width =
      cfg->g_forced_max_frame_width ? cfg->g_forced_max_frame_width : cfg->g_w;
  const unsigned int max_frame_height = cfg->g_forced_max_frame_height
                                            ? cfg->g_forced_max_frame_height
                                            : cfg->g_h;
  const int64_t max_frame_area = (int64_t)max_frame_width * max_frame_height;
  if (max_frame_area > (1 << 30)) {
    ERROR("max_frame_area out of range [..2^30]");
  }
  RANGE_CHECK(cfg, g_timebase.den, 1, 1000000000);
  RANGE_CHECK(cfg, g_timebase.num, 1, 1000000000);
  RANGE_CHECK_HI(cfg, g_profile, MAX_PROFILES - 1);

  RANGE_CHECK_HI(cfg, rc_target_bitrate, 2000000);
  RANGE_CHECK_HI(cfg, rc_max_quantizer, 63);
  RANGE_CHECK_HI(cfg, rc_min_quantizer, cfg->rc_max_quantizer);
  RANGE_CHECK_BOOL(extra_cfg, lossless);
  RANGE_CHECK_HI(extra_cfg, aq_mode, AQ_MODE_COUNT - 1);
  RANGE_CHECK_HI(extra_cfg, deltaq_mode, DELTA_Q_MODE_COUNT - 1);

  if (cfg->g_usage != ALLINTRA &&
      extra_cfg->deltaq_mode == DELTA_Q_VARIANCE_BOOST) {
    ERROR("Variance Boost (deltaq_mode = 6) can only be set in all intra mode");
  }

  RANGE_CHECK_HI(extra_cfg, deltalf_mode, 1);
  RANGE_CHECK_HI(extra_cfg, frame_periodic_boost, 1);
#if CONFIG_REALTIME_ONLY
  RANGE_CHECK(cfg, g_usage, AOM_USAGE_REALTIME, AOM_USAGE_REALTIME);
#else
  RANGE_CHECK_HI(cfg, g_usage, AOM_USAGE_ALL_INTRA);
#endif
  RANGE_CHECK_HI(cfg, g_threads, MAX_NUM_THREADS);
  RANGE_CHECK(cfg, rc_end_usage, AOM_VBR, AOM_Q);
  RANGE_CHECK_HI(cfg, rc_undershoot_pct, 100);
  RANGE_CHECK_HI(cfg, rc_overshoot_pct, 100);
  RANGE_CHECK_HI(cfg, rc_2pass_vbr_bias_pct, 100);
  RANGE_CHECK(cfg, kf_mode, AOM_KF_DISABLED, AOM_KF_AUTO);
  RANGE_CHECK_HI(cfg, rc_dropframe_thresh, 100);
  RANGE_CHECK(cfg, g_pass, AOM_RC_ONE_PASS, AOM_RC_THIRD_PASS);
  RANGE_CHECK_HI(cfg, g_lag_in_frames, MAX_LAG_BUFFERS);
  if (cfg->g_usage == AOM_USAGE_ALL_INTRA) {
    RANGE_CHECK_HI(cfg, g_lag_in_frames, 0);
    RANGE_CHECK_HI(cfg, kf_max_dist, 0);
  }
  RANGE_CHECK_HI(extra_cfg, min_gf_interval, MAX_LAG_BUFFERS - 1);
  RANGE_CHECK_HI(extra_cfg, max_gf_interval, MAX_LAG_BUFFERS - 1);
  if (extra_cfg->max_gf_interval > 0) {
    RANGE_CHECK(extra_cfg, max_gf_interval,
                AOMMAX(2, extra_cfg->min_gf_interval), (MAX_LAG_BUFFERS - 1));
  }
  RANGE_CHECK_HI(extra_cfg, gf_min_pyr_height, 5);
  RANGE_CHECK_HI(extra_cfg, gf_max_pyr_height, 5);
  if (extra_cfg->gf_min_pyr_height > extra_cfg->gf_max_pyr_height) {
    ERROR(
        "gf_min_pyr_height must be less than or equal to "
        "gf_max_pyramid_height");
  }

  RANGE_CHECK_HI(cfg, rc_resize_mode, RESIZE_MODES - 1);
  RANGE_CHECK(cfg, rc_resize_denominator, SCALE_NUMERATOR,
              SCALE_NUMERATOR << 1);
  RANGE_CHECK(cfg, rc_resize_kf_denominator, SCALE_NUMERATOR,
              SCALE_NUMERATOR << 1);
  RANGE_CHECK_HI(cfg, rc_superres_mode, AOM_SUPERRES_AUTO);
  RANGE_CHECK(cfg, rc_superres_denominator, SCALE_NUMERATOR,
              SCALE_NUMERATOR << 1);
  RANGE_CHECK(cfg, rc_superres_kf_denominator, SCALE_NUMERATOR,
              SCALE_NUMERATOR << 1);
  RANGE_CHECK(cfg, rc_superres_qthresh, 1, 63);
  RANGE_CHECK(cfg, rc_superres_kf_qthresh, 1, 63);
  RANGE_CHECK_HI(extra_cfg, cdf_update_mode, 2);

  RANGE_CHECK_HI(extra_cfg, motion_vector_unit_test, 2);
#if CONFIG_FPMT_TEST
  RANGE_CHECK_HI(extra_cfg, fpmt_unit_test, 1);
#endif
  RANGE_CHECK_HI(extra_cfg, sb_multipass_unit_test, 1);
  RANGE_CHECK_HI(extra_cfg, ext_tile_debug, 1);
  RANGE_CHECK_HI(extra_cfg, enable_auto_alt_ref, 1);
  RANGE_CHECK_HI(extra_cfg, enable_auto_bwd_ref, 2);
  RANGE_CHECK(extra_cfg, cpu_used, 0,
              (cfg->g_usage == AOM_USAGE_REALTIME) ? 12 : 9);
  RANGE_CHECK_HI(extra_cfg, noise_sensitivity, 6);
  RANGE_CHECK(extra_cfg, superblock_size, AOM_SUPERBLOCK_SIZE_64X64,
              AOM_SUPERBLOCK_SIZE_DYNAMIC);
  RANGE_CHECK_HI(cfg, large_scale_tile, 1);
  RANGE_CHECK_HI(extra_cfg, single_tile_decoding, 1);
  RANGE_CHECK_HI(extra_cfg, enable_rate_guide_deltaq, 1);

  RANGE_CHECK_HI(extra_cfg, row_mt, 1);
  RANGE_CHECK_HI(extra_cfg, fp_mt, 1);

  RANGE_CHECK_HI(extra_cfg, tile_columns, 6);
  RANGE_CHECK_HI(extra_cfg, tile_rows, 6);
  RANGE_CHECK_HI(extra_cfg, auto_tiles, 1);

  RANGE_CHECK_HI(cfg, monochrome, 1);

  if (cfg->large_scale_tile && extra_cfg->aq_mode)
    ERROR(
        "Adaptive quantization are not supported in large scale tile "
        "coding.");

  RANGE_CHECK_HI(extra_cfg, sharpness, 7);
  RANGE_CHECK_HI(extra_cfg, arnr_max_frames, 15);
  RANGE_CHECK_HI(extra_cfg, arnr_strength, 6);
  RANGE_CHECK_HI(extra_cfg, cq_level, 63);
  RANGE_CHECK(cfg, g_bit_depth, AOM_BITS_8, AOM_BITS_12);
  RANGE_CHECK(cfg, g_input_bit_depth, 8, 12);
  RANGE_CHECK(extra_cfg, content, AOM_CONTENT_DEFAULT, AOM_CONTENT_INVALID - 1);

  if (cfg->g_pass >= AOM_RC_SECOND_PASS) {
    const size_t packet_sz = sizeof(FIRSTPASS_STATS);
    const int n_packets = (int)(cfg->rc_twopass_stats_in.sz / packet_sz);
    const FIRSTPASS_STATS *stats;

    if (cfg->rc_twopass_stats_in.buf == NULL)
      ERROR("rc_twopass_stats_in.buf not set.");

    if (cfg->rc_twopass_stats_in.sz % packet_sz)
      ERROR("rc_twopass_stats_in.sz indicates truncated packet.");

    if (cfg->rc_twopass_stats_in.sz < 2 * packet_sz)
      ERROR("rc_twopass_stats_in requires at least two packets.");

    stats =
        (const FIRSTPASS_STATS *)cfg->rc_twopass_stats_in.buf + n_packets - 1;

    if ((int)(stats->count + 0.5) != n_packets - 1)
      ERROR("rc_twopass_stats_in missing EOS stats packet");
  }

  if (extra_cfg->passes != -1 && cfg->g_pass == AOM_RC_ONE_PASS &&
      extra_cfg->passes != 1) {
    ERROR("One pass encoding but passes != 1.");
  }

  if (extra_cfg->passes != -1 && (int)cfg->g_pass > extra_cfg->passes) {
    ERROR("Current pass is larger than total number of passes.");
  }

  if (cfg->g_profile == (unsigned int)PROFILE_1 && cfg->monochrome) {
    ERROR("Monochrome is not supported in profile 1");
  }

  if (cfg->g_profile <= (unsigned int)PROFILE_1 &&
      cfg->g_bit_depth > AOM_BITS_10) {
    ERROR("Codec bit-depth 12 not supported in profile < 2");
  }
  if (cfg->g_profile <= (unsigned int)PROFILE_1 &&
      cfg->g_input_bit_depth > 10) {
    ERROR("Source bit-depth 12 not supported in profile < 2");
  }

  if (cfg->rc_end_usage == AOM_Q) {
    RANGE_CHECK_HI(cfg, use_fixed_qp_offsets, 1);
  } else {
    if (cfg->use_fixed_qp_offsets > 0) {
      ERROR("--use_fixed_qp_offsets can only be used with --end-usage=q");
    }
  }

  RANGE_CHECK(extra_cfg, color_primaries, AOM_CICP_CP_BT_709,
              AOM_CICP_CP_EBU_3213);  // Need to check range more precisely to
                                      // check for reserved values?
  RANGE_CHECK(extra_cfg, transfer_characteristics, AOM_CICP_TC_BT_709,
              AOM_CICP_TC_HLG);
  RANGE_CHECK(extra_cfg, matrix_coefficients, AOM_CICP_MC_IDENTITY,
              AOM_CICP_MC_ICTCP);
  RANGE_CHECK(extra_cfg, color_range, 0, 1);

  /* Average corpus complexity is supported only in the case of single pass
   * VBR*/
  if (cfg->g_pass == AOM_RC_ONE_PASS && cfg->rc_end_usage == AOM_VBR)
    RANGE_CHECK_HI(extra_cfg, vbr_corpus_complexity_lap,
                   MAX_VBR_CORPUS_COMPLEXITY);
  else if (extra_cfg->vbr_corpus_complexity_lap != 0)
    ERROR(
        "VBR corpus complexity is supported only in the case of single pass "
        "VBR mode.");

#if !CONFIG_TUNE_BUTTERAUGLI
  if (extra_cfg->tuning == AOM_TUNE_BUTTERAUGLI) {
    ERROR(
        "This error may be related to the wrong configuration options: try to "
        "set -DCONFIG_TUNE_BUTTERAUGLI=1 at the time CMake is run.");
  }
#endif

#if !CONFIG_TUNE_VMAF
  if (extra_cfg->tuning >= AOM_TUNE_VMAF_WITH_PREPROCESSING &&
      extra_cfg->tuning <= AOM_TUNE_VMAF_NEG_MAX_GAIN) {
    ERROR(
        "This error may be related to the wrong configuration options: try to "
        "set -DCONFIG_TUNE_VMAF=1 at the time CMake is run.");
  }
#endif

  RANGE_CHECK(extra_cfg, tuning, AOM_TUNE_PSNR, AOM_TUNE_SSIMULACRA2);

  RANGE_CHECK(extra_cfg, dist_metric, AOM_DIST_METRIC_PSNR,
              AOM_DIST_METRIC_QM_PSNR);

  RANGE_CHECK(extra_cfg, timing_info_type, AOM_TIMING_UNSPECIFIED,
              AOM_TIMING_DEC_MODEL);

  RANGE_CHECK(extra_cfg, film_grain_test_vector, 0, 16);

  if (extra_cfg->lossless) {
    if (extra_cfg->aq_mode != 0)
      ERROR("Only --aq_mode=0 can be used with --lossless=1.");
    if (extra_cfg->enable_chroma_deltaq)
      ERROR("Only --enable_chroma_deltaq=0 can be used with --lossless=1.");
  }

  RANGE_CHECK(extra_cfg, max_reference_frames, 3, 7);
  RANGE_CHECK(extra_cfg, enable_reduced_reference_set, 0, 1);

  RANGE_CHECK_HI(extra_cfg, enable_low_complexity_decode, 1);
  RANGE_CHECK_HI(extra_cfg, chroma_subsampling_x, 1);
  RANGE_CHECK_HI(extra_cfg, chroma_subsampling_y, 1);
  // 6.4.2 Color config semantics
  // If matrix_coefficients is equal to MC_IDENTITY, it is a requirement of
  // bitstream conformance that subsampling_x is equal to 0 and subsampling_y
  // is equal to 0.
  if (extra_cfg->matrix_coefficients == AOM_CICP_MC_IDENTITY &&
      (extra_cfg->chroma_subsampling_x != 0 ||
       extra_cfg->chroma_subsampling_y != 0)) {
    ERROR("Subsampling must be 0 with AOM_CICP_MC_IDENTITY.");
  }

  RANGE_CHECK_HI(extra_cfg, disable_trellis_quant, 3);
  RANGE_CHECK(extra_cfg, coeff_cost_upd_freq, 0, 3);
  RANGE_CHECK(extra_cfg, mode_cost_upd_freq, 0, 3);
  RANGE_CHECK(extra_cfg, mv_cost_upd_freq, 0, 3);
  RANGE_CHECK(extra_cfg, dv_cost_upd_freq, 0, 3);

  RANGE_CHECK(extra_cfg, min_partition_size, 4, 128);
  RANGE_CHECK(extra_cfg, max_partition_size, 4, 128);
  RANGE_CHECK_HI(extra_cfg, min_partition_size, extra_cfg->max_partition_size);

  for (int i = 0; i < MAX_NUM_OPERATING_POINTS; ++i) {
    const int level_idx = extra_cfg->target_seq_level_idx[i];
    if (!is_valid_seq_level_idx(level_idx) &&
        level_idx != SEQ_LEVEL_KEEP_STATS) {
      ERROR("Target sequence level index is invalid");
    }
  }

  RANGE_CHECK(extra_cfg, deltaq_strength, 0, 1000);
  RANGE_CHECK_HI(extra_cfg, loopfilter_control, 3);
  RANGE_CHECK_BOOL(extra_cfg, skip_postproc_filtering);
  RANGE_CHECK_HI(extra_cfg, enable_cdef, 3);
  RANGE_CHECK_BOOL(extra_cfg, auto_intra_tools_off);
  RANGE_CHECK_BOOL(extra_cfg, strict_level_conformance);
  RANGE_CHECK_BOOL(extra_cfg, sb_qp_sweep);

  RANGE_CHECK(extra_cfg, kf_max_pyr_height, -1, 5);
  if (extra_cfg->kf_max_pyr_height != -1 &&
      extra_cfg->kf_max_pyr_height < (int)extra_cfg->gf_min_pyr_height) {
    ERROR(
        "The value of kf-max-pyr-height should not be smaller than "
        "gf-min-pyr-height");
  }

  return AOM_CODEC_OK;
}

static aom_codec_err_t validate_img(aom_codec_alg_priv_t *ctx,
                                    const aom_image_t *img) {
  switch (img->fmt) {
    case AOM_IMG_FMT_YV12:
    case AOM_IMG_FMT_NV12:
    case AOM_IMG_FMT_I420:
    case AOM_IMG_FMT_YV1216:
    case AOM_IMG_FMT_I42016: break;
    case AOM_IMG_FMT_I444:
    case AOM_IMG_FMT_I44416:
      if (ctx->cfg.g_profile == (unsigned int)PROFILE_0 &&
          !ctx->cfg.monochrome) {
        ERROR("Invalid image format. I444 images not supported in profile.");
      }
      break;
    case AOM_IMG_FMT_I422:
    case AOM_IMG_FMT_I42216:
      if (ctx->cfg.g_profile != (unsigned int)PROFILE_2) {
        ERROR("Invalid image format. I422 images not supported in profile.");
      }
      break;
    default:
      ERROR(
          "Invalid image format. Only YV12, NV12, I420, I422, I444 images are "
          "supported.");
      break;
  }

  if (img->d_w != ctx->cfg.g_w || img->d_h != ctx->cfg.g_h)
    ERROR("Image size must match encoder init configuration size");

  // 6.4.2 Color config semantics
  // If matrix_coefficients is equal to MC_IDENTITY, it is a requirement of
  // bitstream conformance that subsampling_x is equal to 0 and subsampling_y
  // is equal to 0.
  if (ctx->oxcf.color_cfg.matrix_coefficients == AOM_CICP_MC_IDENTITY &&
      (img->x_chroma_shift != 0 || img->y_chroma_shift != 0)) {
    ERROR("Subsampling must be 0 with AOM_CICP_MC_IDENTITY.");
  }

#if CONFIG_TUNE_BUTTERAUGLI
  if (ctx->extra_cfg.tuning == AOM_TUNE_BUTTERAUGLI) {
    if (img->bit_depth > 8) {
      ERROR("Only 8 bit depth images supported in tune=butteraugli mode.");
    }
    if (img->mc != 0 && img->mc != AOM_CICP_MC_BT_709 &&
        img->mc != AOM_CICP_MC_BT_601 && img->mc != AOM_CICP_MC_BT_470_B_G) {
      ERROR(
          "Only BT.709 and BT.601 matrix coefficients supported in "
          "tune=butteraugli mode. Identity matrix is treated as BT.601.");
    }
  }
#endif

  return AOM_CODEC_OK;
}

static int get_image_bps(const aom_image_t *img) {
  switch (img->fmt) {
    case AOM_IMG_FMT_YV12:
    case AOM_IMG_FMT_NV12:
    case AOM_IMG_FMT_I420: return 12;
    case AOM_IMG_FMT_I422: return 16;
    case AOM_IMG_FMT_I444: return 24;
    case AOM_IMG_FMT_YV1216:
    case AOM_IMG_FMT_I42016: return 24;
    case AOM_IMG_FMT_I42216: return 32;
    case AOM_IMG_FMT_I44416: return 48;
    default: assert(0 && "Invalid image format"); break;
  }
  return 0;
}

// Set appropriate options to disable frame super-resolution.
static void disable_superres(SuperResCfg *const superres_cfg) {
  superres_cfg->superres_mode = AOM_SUPERRES_NONE;
  superres_cfg->superres_scale_denominator = SCALE_NUMERATOR;
  superres_cfg->superres_kf_scale_denominator = SCALE_NUMERATOR;
  superres_cfg->superres_qthresh = 255;
  superres_cfg->superres_kf_qthresh = 255;
}

static void set_auto_tiles(TileConfig *const tile_cfg, unsigned int width,
                           unsigned int height, unsigned int threads) {
  int tile_cols_log2 = 0;
  int tile_rows_log2 = 0;
  if (threads < 2) return;
  // Avoid small tiles because they are particularly bad for coding.
  // Use no more tiles than the number of threads. Aim for one tile per
  // thread. Using more than one thread inside one tile could be less
  // efficient. Using more tiles than the number of threads would result
  // in a compression penalty without much benefit.
  const uint32_t kMinTileArea = 128 * 128;
  const uint32_t kMaxTiles = 32;
  uint32_t frame_area = width * height;
  uint32_t tiles = (frame_area + kMinTileArea - 1) / kMinTileArea;
  if (tiles > kMaxTiles) {
    tiles = kMaxTiles;
  }
  if (tiles > threads) {
    tiles = threads;
  }
  int tiles_log2 = (int)log2(tiles);
  // If the frame width is equal or greater than the height, use more tile
  // columns than tile rows.
  if (width >= height) {
    tile_cols_log2 = (tiles_log2 + 1) / 2;
    tile_rows_log2 = tiles_log2 - tile_cols_log2;
  } else {
    tile_rows_log2 = (tiles_log2 + 1) / 2;
    tile_cols_log2 = tiles_log2 - tile_rows_log2;
  }
  tile_cfg->tile_columns = tile_cols_log2;
  tile_cfg->tile_rows = tile_rows_log2;
}

static void update_default_encoder_config(const cfg_options_t *cfg,
                                          struct av1_extracfg *extra_cfg) {
  extra_cfg->enable_cdef = (cfg->disable_cdef == 0) ? 1 : 0;
  extra_cfg->enable_restoration = (cfg->disable_lr == 0);
  extra_cfg->superblock_size =
      (cfg->super_block_size == 64)    ? AOM_SUPERBLOCK_SIZE_64X64
      : (cfg->super_block_size == 128) ? AOM_SUPERBLOCK_SIZE_128X128
                                       : AOM_SUPERBLOCK_SIZE_DYNAMIC;
  extra_cfg->enable_warped_motion = (cfg->disable_warp_motion == 0);
  extra_cfg->enable_dist_wtd_comp = (cfg->disable_dist_wtd_comp == 0);
  extra_cfg->enable_diff_wtd_comp = (cfg->disable_diff_wtd_comp == 0);
  extra_cfg->enable_dual_filter = (cfg->disable_dual_filter == 0);
  extra_cfg->enable_angle_delta = (cfg->disable_intra_angle_delta == 0);
  extra_cfg->enable_rect_partitions = (cfg->disable_rect_partition_type == 0);
  extra_cfg->enable_ab_partitions = (cfg->disable_ab_partition_type == 0);
  extra_cfg->enable_1to4_partitions = (cfg->disable_1to4_partition_type == 0);
  extra_cfg->max_partition_size = cfg->max_partition_size;
  extra_cfg->min_partition_size = cfg->min_partition_size;
  extra_cfg->enable_intra_edge_filter = (cfg->disable_intra_edge_filter == 0);
  extra_cfg->enable_tx64 = (cfg->disable_tx_64x64 == 0);
  extra_cfg->enable_flip_idtx = (cfg->disable_flip_idtx == 0);
  extra_cfg->enable_masked_comp = (cfg->disable_masked_comp == 0);
  extra_cfg->enable_interintra_comp = (cfg->disable_inter_intra_comp == 0);
  extra_cfg->enable_smooth_interintra = (cfg->disable_smooth_inter_intra == 0);
  extra_cfg->enable_interinter_wedge = (cfg->disable_inter_inter_wedge == 0);
  extra_cfg->enable_interintra_wedge = (cfg->disable_inter_intra_wedge == 0);
  extra_cfg->enable_global_motion = (cfg->disable_global_motion == 0);
  extra_cfg->enable_filter_intra = (cfg->disable_filter_intra == 0);
  extra_cfg->enable_smooth_intra = (cfg->disable_smooth_intra == 0);
  extra_cfg->enable_paeth_intra = (cfg->disable_paeth_intra == 0);
  extra_cfg->enable_cfl_intra = (cfg->disable_cfl == 0);
  extra_cfg->enable_obmc = (cfg->disable_obmc == 0);
  extra_cfg->enable_palette = (cfg->disable_palette == 0);
  extra_cfg->enable_intrabc = (cfg->disable_intrabc == 0);
  extra_cfg->disable_trellis_quant = cfg->disable_trellis_quant;
  extra_cfg->allow_ref_frame_mvs = (cfg->disable_ref_frame_mv == 0);
  extra_cfg->enable_ref_frame_mvs = (cfg->disable_ref_frame_mv == 0);
  extra_cfg->enable_onesided_comp = (cfg->disable_one_sided_comp == 0);
  extra_cfg->enable_reduced_reference_set = cfg->reduced_reference_set;
  extra_cfg->reduced_tx_type_set = cfg->reduced_tx_type_set;
}

static void set_encoder_config(AV1EncoderConfig *oxcf,
                               const aom_codec_enc_cfg_t *cfg,
                               struct av1_extracfg *extra_cfg) {
  if (cfg->encoder_cfg.init_by_cfg_file) {
    update_default_encoder_config(&cfg->encoder_cfg, extra_cfg);
  }

  TuneCfg *const tune_cfg = &oxcf->tune_cfg;
  FrameDimensionCfg *const frm_dim_cfg = &oxcf->frm_dim_cfg;
  TileConfig *const tile_cfg = &oxcf->tile_cfg;
  ResizeCfg *const resize_cfg = &oxcf->resize_cfg;
  GFConfig *const gf_cfg = &oxcf->gf_cfg;
  PartitionCfg *const part_cfg = &oxcf->part_cfg;
  IntraModeCfg *const intra_mode_cfg = &oxcf->intra_mode_cfg;
  TxfmSizeTypeCfg *const txfm_cfg = &oxcf->txfm_cfg;
  CompoundTypeCfg *const comp_type_cfg = &oxcf->comp_type_cfg;
  SuperResCfg *const superres_cfg = &oxcf->superres_cfg;
  KeyFrameCfg *const kf_cfg = &oxcf->kf_cfg;
  DecoderModelCfg *const dec_model_cfg = &oxcf->dec_model_cfg;
  RateControlCfg *const rc_cfg = &oxcf->rc_cfg;
  QuantizationCfg *const q_cfg = &oxcf->q_cfg;
  ColorCfg *const color_cfg = &oxcf->color_cfg;
  InputCfg *const input_cfg = &oxcf->input_cfg;
  AlgoCfg *const algo_cfg = &oxcf->algo_cfg;
  ToolCfg *const tool_cfg = &oxcf->tool_cfg;

  oxcf->profile = cfg->g_profile;
  oxcf->max_threads = (int)cfg->g_threads;

  switch (cfg->g_usage) {
    case AOM_USAGE_REALTIME: oxcf->mode = REALTIME; break;
    case AOM_USAGE_ALL_INTRA: oxcf->mode = ALLINTRA; break;
    default: oxcf->mode = GOOD; break;
  }

  // Set frame-dimension related configuration.
  frm_dim_cfg->width = cfg->g_w;
  frm_dim_cfg->height = cfg->g_h;
  frm_dim_cfg->forced_max_frame_width = cfg->g_forced_max_frame_width;
  frm_dim_cfg->forced_max_frame_height = cfg->g_forced_max_frame_height;
  frm_dim_cfg->render_width = extra_cfg->render_width;
  frm_dim_cfg->render_height = extra_cfg->render_height;

  // Set input video related configuration.
  input_cfg->input_bit_depth = cfg->g_input_bit_depth;
  // guess a frame rate if out of whack, use 30
  input_cfg->init_framerate = (double)cfg->g_timebase.den / cfg->g_timebase.num;
  if (cfg->g_pass >= AOM_RC_SECOND_PASS) {
    const size_t packet_sz = sizeof(FIRSTPASS_STATS);
    const int n_packets = (int)(cfg->rc_twopass_stats_in.sz / packet_sz);
    input_cfg->limit = n_packets - 1;
  } else {
    input_cfg->limit = cfg->g_limit;
  }
  input_cfg->chroma_subsampling_x = extra_cfg->chroma_subsampling_x;
  input_cfg->chroma_subsampling_y = extra_cfg->chroma_subsampling_y;
  if (input_cfg->init_framerate > 180) {
    input_cfg->init_framerate = 30;
    dec_model_cfg->timing_info_present = 0;
  }

  // Set Decoder model configuration.
  if (extra_cfg->timing_info_type == AOM_TIMING_EQUAL ||
      extra_cfg->timing_info_type == AOM_TIMING_DEC_MODEL) {
    dec_model_cfg->timing_info_present = 1;
    dec_model_cfg->timing_info.num_units_in_display_tick = cfg->g_timebase.num;
    dec_model_cfg->timing_info.time_scale = cfg->g_timebase.den;
    dec_model_cfg->timing_info.num_ticks_per_picture = 1;
  } else {
    dec_model_cfg->timing_info_present = 0;
  }
  if (extra_cfg->timing_info_type == AOM_TIMING_EQUAL) {
    dec_model_cfg->timing_info.equal_picture_interval = 1;
    dec_model_cfg->decoder_model_info_present_flag = 0;
    dec_model_cfg->display_model_info_present_flag = 1;
  } else if (extra_cfg->timing_info_type == AOM_TIMING_DEC_MODEL) {
    dec_model_cfg->num_units_in_decoding_tick = cfg->g_timebase.num;
    dec_model_cfg->timing_info.equal_picture_interval = 0;
    dec_model_cfg->decoder_model_info_present_flag = 1;
    dec_model_cfg->display_model_info_present_flag = 1;
  }

  oxcf->pass = cfg->g_pass;
  // For backward compatibility, assume that if extra_cfg->passes==-1, then
  // passes = 1 or 2.
  if (extra_cfg->passes == -1) {
    if (cfg->g_pass == AOM_RC_ONE_PASS) {
      oxcf->passes = 1;
    } else {
      oxcf->passes = 2;
    }
  } else {
    oxcf->passes = extra_cfg->passes;
  }

  // Set Rate Control configuration.
  rc_cfg->max_intra_bitrate_pct = extra_cfg->rc_max_intra_bitrate_pct;
  rc_cfg->max_inter_bitrate_pct = extra_cfg->rc_max_inter_bitrate_pct;
  rc_cfg->gf_cbr_boost_pct = extra_cfg->gf_cbr_boost_pct;
  rc_cfg->mode = cfg->rc_end_usage;
  rc_cfg->min_cr = extra_cfg->min_cr;
  rc_cfg->best_allowed_q =
      extra_cfg->lossless ? 0 : av1_quantizer_to_qindex(cfg->rc_min_quantizer);
  rc_cfg->worst_allowed_q =
      extra_cfg->lossless ? 0 : av1_quantizer_to_qindex(cfg->rc_max_quantizer);
  rc_cfg->cq_level = av1_quantizer_to_qindex(extra_cfg->cq_level);
  rc_cfg->under_shoot_pct = cfg->rc_undershoot_pct;
  rc_cfg->over_shoot_pct = cfg->rc_overshoot_pct;
  rc_cfg->maximum_buffer_size_ms = cfg->rc_buf_sz;
  rc_cfg->starting_buffer_level_ms = cfg->rc_buf_initial_sz;
  rc_cfg->optimal_buffer_level_ms = cfg->rc_buf_optimal_sz;
  // Convert target bandwidth from Kbit/s to Bit/s
  rc_cfg->target_bandwidth = 1000 * cfg->rc_target_bitrate;
  rc_cfg->drop_frames_water_mark = cfg->rc_dropframe_thresh;
  rc_cfg->vbr_corpus_complexity_lap = extra_cfg->vbr_corpus_complexity_lap;
  rc_cfg->vbrbias = cfg->rc_2pass_vbr_bias_pct;
  rc_cfg->vbrmin_section = cfg->rc_2pass_vbr_minsection_pct;
  rc_cfg->vbrmax_section = cfg->rc_2pass_vbr_maxsection_pct;

  // Set Toolset related configuration.
  tool_cfg->bit_depth = cfg->g_bit_depth;
  tool_cfg->cdef_control = (CDEF_CONTROL)extra_cfg->enable_cdef;
  tool_cfg->enable_restoration =
      (cfg->g_usage == AOM_USAGE_REALTIME) ? 0 : extra_cfg->enable_restoration;
  tool_cfg->force_video_mode = extra_cfg->force_video_mode;
  tool_cfg->enable_palette = extra_cfg->enable_palette;
  // FIXME(debargha): Should this be:
  // tool_cfg->enable_ref_frame_mvs  = extra_cfg->allow_ref_frame_mvs &
  //                                         extra_cfg->enable_order_hint ?
  // Disallow using temporal MVs while large_scale_tile = 1.
  tool_cfg->enable_ref_frame_mvs =
      extra_cfg->allow_ref_frame_mvs && !cfg->large_scale_tile;
  tool_cfg->superblock_size = extra_cfg->superblock_size;
  tool_cfg->enable_monochrome = cfg->monochrome;
  tool_cfg->full_still_picture_hdr = cfg->full_still_picture_hdr != 0;
  tool_cfg->enable_dual_filter = extra_cfg->enable_dual_filter;
  tool_cfg->enable_order_hint = extra_cfg->enable_order_hint;
  tool_cfg->enable_interintra_comp = extra_cfg->enable_interintra_comp;
  tool_cfg->ref_frame_mvs_present =
      extra_cfg->enable_ref_frame_mvs & extra_cfg->enable_order_hint;

  // Explicitly disable global motion in a few cases:
  // * For realtime mode, we never search global motion, and disabling
  //   it here prevents later code from allocating buffers we don't need
  // * For large scale tile mode, some of the intended use cases expect
  //   all frame headers to be identical. This breaks if global motion is
  //   used, since global motion data is stored in the frame header.
  //   eg, see test/lightfield_test.sh, which checks that all frame headers
  //   are the same.
  tool_cfg->enable_global_motion = extra_cfg->enable_global_motion &&
                                   cfg->g_usage != AOM_USAGE_REALTIME &&
                                   !cfg->large_scale_tile;

  tool_cfg->error_resilient_mode =
      cfg->g_error_resilient | extra_cfg->error_resilient_mode;
  tool_cfg->frame_parallel_decoding_mode =
      extra_cfg->frame_parallel_decoding_mode;

  // Set Quantization related configuration.
  q_cfg->using_qm = extra_cfg->enable_qm;
  q_cfg->qm_minlevel = extra_cfg->qm_min;
  q_cfg->qm_maxlevel = extra_cfg->qm_max;
  q_cfg->quant_b_adapt = extra_cfg->quant_b_adapt;
  q_cfg->enable_chroma_deltaq = extra_cfg->enable_chroma_deltaq;
  q_cfg->aq_mode = extra_cfg->aq_mode;
  q_cfg->deltaq_mode = extra_cfg->deltaq_mode;
  q_cfg->deltaq_strength = extra_cfg->deltaq_strength;
  q_cfg->use_fixed_qp_offsets =
      cfg->use_fixed_qp_offsets && (rc_cfg->mode == AOM_Q);
  q_cfg->enable_hdr_deltaq =
      (q_cfg->deltaq_mode == DELTA_Q_HDR) &&
      (cfg->g_bit_depth == AOM_BITS_10) &&
      (extra_cfg->color_primaries == AOM_CICP_CP_BT_2020);

  tool_cfg->enable_deltalf_mode =
      (q_cfg->deltaq_mode != NO_DELTA_Q) && extra_cfg->deltalf_mode;

  // Set cost update frequency configuration.
  oxcf->cost_upd_freq.coeff = (COST_UPDATE_TYPE)extra_cfg->coeff_cost_upd_freq;
  oxcf->cost_upd_freq.mode = (COST_UPDATE_TYPE)extra_cfg->mode_cost_upd_freq;
  // Avoid MV cost update for allintra encoding mode.
  oxcf->cost_upd_freq.mv = (cfg->kf_max_dist != 0)
                               ? (COST_UPDATE_TYPE)extra_cfg->mv_cost_upd_freq
                               : COST_UPD_OFF;
  oxcf->cost_upd_freq.dv = (COST_UPDATE_TYPE)extra_cfg->dv_cost_upd_freq;

  // Set frame resize mode configuration.
  resize_cfg->resize_mode = (RESIZE_MODE)cfg->rc_resize_mode;
  resize_cfg->resize_scale_denominator = (uint8_t)cfg->rc_resize_denominator;
  resize_cfg->resize_kf_scale_denominator =
      (uint8_t)cfg->rc_resize_kf_denominator;
  if (resize_cfg->resize_mode == RESIZE_FIXED &&
      resize_cfg->resize_scale_denominator == SCALE_NUMERATOR &&
      resize_cfg->resize_kf_scale_denominator == SCALE_NUMERATOR)
    resize_cfg->resize_mode = RESIZE_NONE;

  // Set encoder algorithm related configuration.
  algo_cfg->enable_overlay = extra_cfg->enable_overlay;
  algo_cfg->disable_trellis_quant = extra_cfg->disable_trellis_quant;
  algo_cfg->sharpness = extra_cfg->sharpness;
  algo_cfg->arnr_max_frames = extra_cfg->arnr_max_frames;
  algo_cfg->arnr_strength = extra_cfg->arnr_strength;
  algo_cfg->cdf_update_mode = (uint8_t)extra_cfg->cdf_update_mode;
  // TODO(any): Fix and Enable TPL for resize-mode > 0
  algo_cfg->enable_tpl_model =
      resize_cfg->resize_mode ? 0 : extra_cfg->enable_tpl_model;
  algo_cfg->loopfilter_control = extra_cfg->loopfilter_control;
  algo_cfg->skip_postproc_filtering = extra_cfg->skip_postproc_filtering;

  // Set two-pass stats configuration.
  oxcf->twopass_stats_in = cfg->rc_twopass_stats_in;

  if (extra_cfg->two_pass_output)
    oxcf->two_pass_output = extra_cfg->two_pass_output;

  oxcf->second_pass_log = extra_cfg->second_pass_log;

  // Set Key frame configuration.
  kf_cfg->fwd_kf_enabled = cfg->fwd_kf_enabled;
  kf_cfg->auto_key =
      cfg->kf_mode == AOM_KF_AUTO && cfg->kf_min_dist != cfg->kf_max_dist;
  kf_cfg->key_freq_min = cfg->kf_min_dist;
  kf_cfg->key_freq_max = cfg->kf_max_dist;
  kf_cfg->sframe_dist = cfg->sframe_dist;
  kf_cfg->sframe_mode = cfg->sframe_mode;
  kf_cfg->enable_sframe = extra_cfg->s_frame_mode;
  kf_cfg->enable_keyframe_filtering = extra_cfg->enable_keyframe_filtering;
  kf_cfg->fwd_kf_dist = extra_cfg->fwd_kf_dist;
  // Disable key frame filtering in all intra mode.
  if (cfg->kf_max_dist == 0) {
    kf_cfg->enable_keyframe_filtering = 0;
  }
  kf_cfg->enable_intrabc = extra_cfg->enable_intrabc;

  oxcf->speed = extra_cfg->cpu_used;
  // TODO(yunqingwang, any) In REALTIME mode, 1080p performance at speed 5 & 6
  // is quite bad. Force to use speed 7 for now. Will investigate it when we
  // work on rd path optimization later.
  if (oxcf->mode == REALTIME && AOMMIN(cfg->g_w, cfg->g_h) >= 1080 &&
      oxcf->speed < 7)
    oxcf->speed = 7;

  // Now, low complexity decode mode is only supported for good-quality
  // encoding speed 1 to 3. This can be further modified if needed.
  oxcf->enable_low_complexity_decode =
      extra_cfg->enable_low_complexity_decode &&
      cfg->g_usage == AOM_USAGE_GOOD_QUALITY && oxcf->speed >= 1 &&
      oxcf->speed <= 3;

  // Set Color related configuration.
  color_cfg->color_primaries = extra_cfg->color_primaries;
  color_cfg->transfer_characteristics = extra_cfg->transfer_characteristics;
  color_cfg->matrix_coefficients = extra_cfg->matrix_coefficients;
  color_cfg->color_range = extra_cfg->color_range;
  color_cfg->chroma_sample_position = extra_cfg->chroma_sample_position;

  // Set Group of frames configuration.
  // Force lag_in_frames to 0 for REALTIME mode
  gf_cfg->lag_in_frames = (oxcf->mode == REALTIME)
                              ? 0
                              : clamp(cfg->g_lag_in_frames, 0, MAX_LAG_BUFFERS);
  gf_cfg->enable_auto_arf = extra_cfg->enable_auto_alt_ref;
  gf_cfg->enable_auto_brf = extra_cfg->enable_auto_bwd_ref;
  gf_cfg->min_gf_interval = extra_cfg->min_gf_interval;
  gf_cfg->max_gf_interval = extra_cfg->max_gf_interval;
  gf_cfg->gf_min_pyr_height = extra_cfg->gf_min_pyr_height;
  gf_cfg->gf_max_pyr_height = extra_cfg->gf_max_pyr_height;

  // Set tune related configuration.
  tune_cfg->tuning = extra_cfg->tuning;
  tune_cfg->vmaf_model_path = extra_cfg->vmaf_model_path;
  tune_cfg->content = extra_cfg->content;
  if (cfg->large_scale_tile) {
    tune_cfg->film_grain_test_vector = 0;
    tune_cfg->film_grain_table_filename = NULL;
  } else {
    tune_cfg->film_grain_test_vector = extra_cfg->film_grain_test_vector;
    tune_cfg->film_grain_table_filename = extra_cfg->film_grain_table_filename;
  }
  tune_cfg->dist_metric = extra_cfg->dist_metric;
#if CONFIG_DENOISE
  oxcf->noise_level = extra_cfg->noise_level;
  oxcf->noise_block_size = extra_cfg->noise_block_size;
  oxcf->enable_dnl_denoising = extra_cfg->enable_dnl_denoising;
#endif

#if CONFIG_AV1_TEMPORAL_DENOISING
  // Temporal denoiser is for nonrd pickmode so disable it for speed < 7.
  // Also disable it for speed 7 for now since it needs to be modified for
  // the check_partition_merge_mode feature.
  if (cfg->g_bit_depth == AOM_BITS_8 && oxcf->speed > 7) {
    oxcf->noise_sensitivity = extra_cfg->noise_sensitivity;
  } else {
    oxcf->noise_sensitivity = 0;
  }
#endif
  // Set Tile related configuration.
  tile_cfg->num_tile_groups = extra_cfg->num_tg;
  // In large-scale tile encoding mode, num_tile_groups is always 1.
  if (cfg->large_scale_tile) tile_cfg->num_tile_groups = 1;
  tile_cfg->mtu = extra_cfg->mtu_size;
  tile_cfg->enable_large_scale_tile = cfg->large_scale_tile;
  tile_cfg->enable_single_tile_decoding =
      (tile_cfg->enable_large_scale_tile) ? extra_cfg->single_tile_decoding : 0;
  if (extra_cfg->auto_tiles) {
    set_auto_tiles(tile_cfg, cfg->g_w, cfg->g_h, cfg->g_threads);
    extra_cfg->tile_columns = tile_cfg->tile_columns;
    extra_cfg->tile_rows = tile_cfg->tile_rows;
  } else {
    tile_cfg->tile_columns = extra_cfg->tile_columns;
    tile_cfg->tile_rows = extra_cfg->tile_rows;
  }
  tile_cfg->tile_width_count = AOMMIN(cfg->tile_width_count, MAX_TILE_COLS);
  tile_cfg->tile_height_count = AOMMIN(cfg->tile_height_count, MAX_TILE_ROWS);
  for (int i = 0; i < tile_cfg->tile_width_count; i++) {
    tile_cfg->tile_widths[i] = cfg->tile_widths[i];
  }
  for (int i = 0; i < tile_cfg->tile_height_count; i++) {
    tile_cfg->tile_heights[i] = cfg->tile_heights[i];
  }
  tile_cfg->enable_ext_tile_debug = extra_cfg->ext_tile_debug;

  if (tile_cfg->enable_large_scale_tile) {
    // The superblock_size can only be AOM_SUPERBLOCK_SIZE_64X64 or
    // AOM_SUPERBLOCK_SIZE_128X128 while tile_cfg->enable_large_scale_tile = 1.
    // If superblock_size = AOM_SUPERBLOCK_SIZE_DYNAMIC, hard set it to
    // AOM_SUPERBLOCK_SIZE_64X64(default value in large_scale_tile).
    if (extra_cfg->superblock_size != AOM_SUPERBLOCK_SIZE_64X64 &&
        extra_cfg->superblock_size != AOM_SUPERBLOCK_SIZE_128X128)
      tool_cfg->superblock_size = AOM_SUPERBLOCK_SIZE_64X64;
  }

  // Set reference frame related configuration.
  oxcf->ref_frm_cfg.max_reference_frames = extra_cfg->max_reference_frames;
  oxcf->ref_frm_cfg.enable_reduced_reference_set =
      extra_cfg->enable_reduced_reference_set;
  oxcf->ref_frm_cfg.enable_onesided_comp = extra_cfg->enable_onesided_comp;

  oxcf->row_mt = extra_cfg->row_mt;
  oxcf->fp_mt = extra_cfg->fp_mt;

  // Set motion mode related configuration.
  oxcf->motion_mode_cfg.enable_obmc = extra_cfg->enable_obmc;
  oxcf->motion_mode_cfg.enable_warped_motion = extra_cfg->enable_warped_motion;
#if !CONFIG_REALTIME_ONLY
  if (cfg->g_usage == AOM_USAGE_REALTIME && oxcf->speed >= 7 &&
      oxcf->tune_cfg.content == AOM_CONTENT_SCREEN) {
    // TODO(marpan): warped motion is causing a crash for RT mode with screen
    // in nonrd (speed >= 7), for non-realtime build.
    // Re-enable/allow when the issue is fixed.
    oxcf->motion_mode_cfg.enable_warped_motion = 0;
    oxcf->motion_mode_cfg.allow_warped_motion = 0;
  } else {
    oxcf->motion_mode_cfg.allow_warped_motion =
        (extra_cfg->allow_warped_motion & extra_cfg->enable_warped_motion);
  }
#else
  oxcf->motion_mode_cfg.allow_warped_motion =
      (cfg->g_usage == AOM_USAGE_REALTIME && oxcf->speed >= 7)
          ? false
          : (extra_cfg->allow_warped_motion & extra_cfg->enable_warped_motion);
#endif

  // Set partition related configuration.
  part_cfg->enable_rect_partitions = extra_cfg->enable_rect_partitions;
  part_cfg->enable_ab_partitions = extra_cfg->enable_ab_partitions;
  part_cfg->enable_1to4_partitions = extra_cfg->enable_1to4_partitions;
  part_cfg->min_partition_size = extra_cfg->min_partition_size;
  part_cfg->max_partition_size = extra_cfg->max_partition_size;

  // Set intra mode configuration.
  intra_mode_cfg->enable_angle_delta = extra_cfg->enable_angle_delta;
  intra_mode_cfg->enable_intra_edge_filter =
      extra_cfg->enable_intra_edge_filter;
  intra_mode_cfg->enable_filter_intra = extra_cfg->enable_filter_intra;
  intra_mode_cfg->enable_smooth_intra = extra_cfg->enable_smooth_intra;
  intra_mode_cfg->enable_paeth_intra = extra_cfg->enable_paeth_intra;
  intra_mode_cfg->enable_cfl_intra = extra_cfg->enable_cfl_intra;
  intra_mode_cfg->enable_directional_intra =
      extra_cfg->enable_directional_intra;
  intra_mode_cfg->enable_diagonal_intra = extra_cfg->enable_diagonal_intra;
  intra_mode_cfg->auto_intra_tools_off = extra_cfg->auto_intra_tools_off;

  // Set transform size/type configuration.
  txfm_cfg->enable_tx64 = extra_cfg->enable_tx64;
  txfm_cfg->enable_flip_idtx = extra_cfg->enable_flip_idtx;
  txfm_cfg->enable_rect_tx = extra_cfg->enable_rect_tx;
  txfm_cfg->reduced_tx_type_set = extra_cfg->reduced_tx_type_set;
  txfm_cfg->use_intra_dct_only = extra_cfg->use_intra_dct_only;
  txfm_cfg->use_inter_dct_only = extra_cfg->use_inter_dct_only;
  txfm_cfg->use_intra_default_tx_only = extra_cfg->use_intra_default_tx_only;
  txfm_cfg->enable_tx_size_search = extra_cfg->enable_tx_size_search;

  // Set compound type configuration.
  comp_type_cfg->enable_dist_wtd_comp =
      extra_cfg->enable_dist_wtd_comp & extra_cfg->enable_order_hint;
  comp_type_cfg->enable_masked_comp = extra_cfg->enable_masked_comp;
  comp_type_cfg->enable_diff_wtd_comp =
      extra_cfg->enable_masked_comp & extra_cfg->enable_diff_wtd_comp;
  comp_type_cfg->enable_interinter_wedge =
      extra_cfg->enable_masked_comp & extra_cfg->enable_interinter_wedge;
  comp_type_cfg->enable_smooth_interintra =
      extra_cfg->enable_interintra_comp && extra_cfg->enable_smooth_interintra;
  comp_type_cfg->enable_interintra_wedge =
      extra_cfg->enable_interintra_comp & extra_cfg->enable_interintra_wedge;

  // Set Super-resolution mode configuration.
  if (extra_cfg->lossless || cfg->large_scale_tile) {
    disable_superres(superres_cfg);
  } else {
    superres_cfg->superres_mode = cfg->rc_superres_mode;
    superres_cfg->superres_scale_denominator =
        (uint8_t)cfg->rc_superres_denominator;
    superres_cfg->superres_kf_scale_denominator =
        (uint8_t)cfg->rc_superres_kf_denominator;
    superres_cfg->superres_qthresh =
        av1_quantizer_to_qindex(cfg->rc_superres_qthresh);
    superres_cfg->superres_kf_qthresh =
        av1_quantizer_to_qindex(cfg->rc_superres_kf_qthresh);
    if (superres_cfg->superres_mode == AOM_SUPERRES_FIXED &&
        superres_cfg->superres_scale_denominator == SCALE_NUMERATOR &&
        superres_cfg->superres_kf_scale_denominator == SCALE_NUMERATOR) {
      disable_superres(superres_cfg);
    }
    if (superres_cfg->superres_mode == AOM_SUPERRES_QTHRESH &&
        superres_cfg->superres_qthresh == 255 &&
        superres_cfg->superres_kf_qthresh == 255) {
      disable_superres(superres_cfg);
    }
  }

  superres_cfg->enable_superres =
      (superres_cfg->superres_mode != AOM_SUPERRES_NONE) &&
      extra_cfg->enable_superres;
  if (!superres_cfg->enable_superres) {
    disable_superres(superres_cfg);
  }

  if (input_cfg->limit == 1) {
    // still picture mode, display model and timing is meaningless
    dec_model_cfg->display_model_info_present_flag = 0;
    dec_model_cfg->timing_info_present = 0;
  }

  oxcf->save_as_annexb = cfg->save_as_annexb;

  // Set unit test related configuration.
  oxcf->unit_test_cfg.motion_vector_unit_test =
      extra_cfg->motion_vector_unit_test;
  oxcf->unit_test_cfg.sb_multipass_unit_test =
      extra_cfg->sb_multipass_unit_test;

  oxcf->border_in_pixels =
      av1_get_enc_border_size(av1_is_resize_needed(oxcf),
                              (oxcf->kf_cfg.key_freq_max == 0), BLOCK_128X128);
  memcpy(oxcf->target_seq_level_idx, extra_cfg->target_seq_level_idx,
         sizeof(oxcf->target_seq_level_idx));
  oxcf->tier_mask = extra_cfg->tier_mask;

  oxcf->partition_info_path = extra_cfg->partition_info_path;

  oxcf->enable_rate_guide_deltaq = extra_cfg->enable_rate_guide_deltaq;
  oxcf->rate_distribution_info = extra_cfg->rate_distribution_info;

  oxcf->strict_level_conformance = extra_cfg->strict_level_conformance;

  oxcf->kf_max_pyr_height = extra_cfg->kf_max_pyr_height;

  oxcf->sb_qp_sweep = extra_cfg->sb_qp_sweep;
}

AV1EncoderConfig av1_get_encoder_config(const aom_codec_enc_cfg_t *cfg) {
  AV1EncoderConfig oxcf;
  struct av1_extracfg extra_cfg = default_extra_cfg;
  set_encoder_config(&oxcf, cfg, &extra_cfg);
  return oxcf;
}

static aom_codec_err_t encoder_set_config(aom_codec_alg_priv_t *ctx,
                                          const aom_codec_enc_cfg_t *cfg) {
  aom_codec_err_t res;
  int force_key = 0;

  if (cfg->g_w != ctx->cfg.g_w || cfg->g_h != ctx->cfg.g_h) {
    if (cfg->g_lag_in_frames > 1 || cfg->g_pass != AOM_RC_ONE_PASS)
      ERROR("Cannot change width or height after initialization");
    // Note: function encoder_set_config() is allowed to be called multiple
    // times. However, when the original frame width or height is less than two
    // times of the new frame width or height, a forced key frame should be
    // used (for the case of single spatial layer, since otherwise a previous
    // encoded frame at a lower layer may be the desired reference). To make
    // sure the correct detection of a forced key frame, we need
    // to update the frame width and height only when the actual encoding is
    // performed. cpi->last_coded_width and cpi->last_coded_height are used to
    // track the actual coded frame size.
    if (ctx->ppi->cpi->svc.number_spatial_layers == 1 &&
        ctx->ppi->cpi->last_coded_width && ctx->ppi->cpi->last_coded_height &&
        (!valid_ref_frame_size(ctx->ppi->cpi->last_coded_width,
                               ctx->ppi->cpi->last_coded_height, cfg->g_w,
                               cfg->g_h) ||
         ((int)cfg->g_w > ctx->ppi->cpi->last_coded_width) ||
         ((int)cfg->g_h > ctx->ppi->cpi->last_coded_height))) {
      force_key = 1;
    }
  }

  if (ctx->monochrome_on_init && cfg->monochrome == 0) {
    // TODO(aomedia:3465): Allow this case to work without requiring re-init
    // of encoder.
    ERROR("Cannot change to monochrome = 0 after init with monochrome");
  }

  // Prevent increasing lag_in_frames. This check is stricter than it needs
  // to be -- the limit is not increasing past the first lag_in_frames
  // value, but we don't track the initial config, only the last successful
  // config.
  if (cfg->g_lag_in_frames > ctx->cfg.g_lag_in_frames)
    ERROR("Cannot increase lag_in_frames");
  // Prevent changing lag_in_frames if Lookahead Processing is enabled
  if (cfg->g_lag_in_frames != ctx->cfg.g_lag_in_frames &&
      ctx->num_lap_buffers > 0)
    ERROR("Cannot change lag_in_frames if LAP is enabled");

  res = validate_config(ctx, cfg, &ctx->extra_cfg);

  if (res == AOM_CODEC_OK) {
    ctx->cfg = *cfg;
    set_encoder_config(&ctx->oxcf, &ctx->cfg, &ctx->extra_cfg);
    // On profile change, request a key frame
    force_key |= ctx->ppi->seq_params.profile != ctx->oxcf.profile;
    bool is_sb_size_changed = false;
    av1_change_config_seq(ctx->ppi, &ctx->oxcf, &is_sb_size_changed);
    for (int i = 0; i < ctx->ppi->num_fp_contexts; i++) {
      av1_change_config(ctx->ppi->parallel_cpi[i], &ctx->oxcf,
                        is_sb_size_changed);
    }
    if (ctx->ppi->cpi_lap != NULL) {
      av1_change_config(ctx->ppi->cpi_lap, &ctx->oxcf, is_sb_size_changed);
    }
  }

  if (force_key) ctx->next_frame_flags |= AOM_EFLAG_FORCE_KF;

  return res;
}

static aom_fixed_buf_t *encoder_get_global_headers(aom_codec_alg_priv_t *ctx) {
  return av1_get_global_headers(ctx->ppi);
}

static aom_codec_err_t ctrl_get_quantizer(aom_codec_alg_priv_t *ctx,
                                          va_list args) {
  int *const arg = va_arg(args, int *);
  if (arg == NULL) return AOM_CODEC_INVALID_PARAM;
  *arg = av1_get_quantizer(ctx->ppi->cpi);
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_get_quantizer64(aom_codec_alg_priv_t *ctx,
                                            va_list args) {
  int *const arg = va_arg(args, int *);
  if (arg == NULL) return AOM_CODEC_INVALID_PARAM;
  *arg = av1_qindex_to_quantizer(av1_get_quantizer(ctx->ppi->cpi));
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_get_loopfilter_level(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  int *const arg = va_arg(args, int *);
  if (arg == NULL) return AOM_CODEC_INVALID_PARAM;
  *arg = ctx->ppi->cpi->common.lf.filter_level[0];
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_get_baseline_gf_interval(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  int *const arg = va_arg(args, int *);
  if (arg == NULL) return AOM_CODEC_INVALID_PARAM;
  *arg = ctx->ppi->p_rc.baseline_gf_interval;
  return AOM_CODEC_OK;
}

static aom_codec_err_t update_encoder_cfg(aom_codec_alg_priv_t *ctx) {
  set_encoder_config(&ctx->oxcf, &ctx->cfg, &ctx->extra_cfg);
  av1_check_fpmt_config(ctx->ppi, &ctx->oxcf);
  bool is_sb_size_changed = false;
  av1_change_config_seq(ctx->ppi, &ctx->oxcf, &is_sb_size_changed);
  for (int i = 0; i < ctx->ppi->num_fp_contexts; i++) {
    AV1_COMP *const cpi = ctx->ppi->parallel_cpi[i];
    struct aom_internal_error_info *const error = cpi->common.error;
    if (setjmp(error->jmp)) {
      error->setjmp = 0;
      return error->error_code;
    }
    error->setjmp = 1;
    av1_change_config(cpi, &ctx->oxcf, is_sb_size_changed);
    error->setjmp = 0;
  }
  if (ctx->ppi->cpi_lap != NULL) {
    AV1_COMP *const cpi_lap = ctx->ppi->cpi_lap;
    struct aom_internal_error_info *const error = cpi_lap->common.error;
    if (setjmp(error->jmp)) {
      error->setjmp = 0;
      return error->error_code;
    }
    error->setjmp = 1;
    av1_change_config(cpi_lap, &ctx->oxcf, is_sb_size_changed);
    error->setjmp = 0;
  }
  return AOM_CODEC_OK;
}

static aom_codec_err_t update_extra_cfg(aom_codec_alg_priv_t *ctx,
                                        const struct av1_extracfg *extra_cfg) {
  const aom_codec_err_t res = validate_config(ctx, &ctx->cfg, extra_cfg);
  if (res == AOM_CODEC_OK) {
    ctx->extra_cfg = *extra_cfg;
    return update_encoder_cfg(ctx);
  }
  return res;
}

static aom_codec_err_t ctrl_set_cpuused(aom_codec_alg_priv_t *ctx,
                                        va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.cpu_used = CAST(AOME_SET_CPUUSED, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_auto_alt_ref(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_auto_alt_ref = CAST(AOME_SET_ENABLEAUTOALTREF, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_auto_bwd_ref(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_auto_bwd_ref = CAST(AOME_SET_ENABLEAUTOBWDREF, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_noise_sensitivity(aom_codec_alg_priv_t *ctx,
                                                  va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.noise_sensitivity = CAST(AV1E_SET_NOISE_SENSITIVITY, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_sharpness(aom_codec_alg_priv_t *ctx,
                                          va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.sharpness = CAST(AOME_SET_SHARPNESS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_static_thresh(aom_codec_alg_priv_t *ctx,
                                              va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.static_thresh = CAST(AOME_SET_STATIC_THRESHOLD, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_row_mt(aom_codec_alg_priv_t *ctx,
                                       va_list args) {
  unsigned int row_mt = CAST(AV1E_SET_ROW_MT, args);
  if (row_mt == ctx->extra_cfg.row_mt) return AOM_CODEC_OK;
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.row_mt = row_mt;
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_tile_columns(aom_codec_alg_priv_t *ctx,
                                             va_list args) {
  // If the control AUTO_TILES is used (set to 1) then don't override
  // the tile_columns set via the AUTO_TILES control.
  if (ctx->extra_cfg.auto_tiles) {
    ERROR("AUTO_TILES is set so AV1E_SET_TILE_COLUMNS should not be called.");
  }
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  unsigned int tile_columns = CAST(AV1E_SET_TILE_COLUMNS, args);
  if (tile_columns == extra_cfg.tile_columns) return AOM_CODEC_OK;
  extra_cfg.tile_columns = tile_columns;
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_tile_rows(aom_codec_alg_priv_t *ctx,
                                          va_list args) {
  // If the control AUTO_TILES is used (set to 1) then don't override
  // the tile_rows set via the AUTO_TILES control.
  if (ctx->extra_cfg.auto_tiles) {
    ERROR("AUTO_TILES is set so AV1E_SET_TILE_ROWS should not be called.");
  }
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  unsigned int tile_rows = CAST(AV1E_SET_TILE_ROWS, args);
  if (tile_rows == extra_cfg.tile_rows) return AOM_CODEC_OK;
  extra_cfg.tile_rows = tile_rows;
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_tpl_model(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  const unsigned int tpl_model_arg = CAST(AV1E_SET_ENABLE_TPL_MODEL, args);
#if CONFIG_REALTIME_ONLY
  if (tpl_model_arg) {
    ERROR("TPL model can't be turned on in realtime only build.");
  }
#endif
  extra_cfg.enable_tpl_model = tpl_model_arg;
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_keyframe_filtering(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_keyframe_filtering =
      CAST(AV1E_SET_ENABLE_KEYFRAME_FILTERING, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_arnr_max_frames(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.arnr_max_frames = CAST(AOME_SET_ARNR_MAXFRAMES, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_arnr_strength(aom_codec_alg_priv_t *ctx,
                                              va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.arnr_strength = CAST(AOME_SET_ARNR_STRENGTH, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t handle_tuning(aom_codec_alg_priv_t *ctx,
                                     struct av1_extracfg *extra_cfg) {
  if (extra_cfg->tuning == AOM_TUNE_IQ ||
      extra_cfg->tuning == AOM_TUNE_SSIMULACRA2) {
    if (ctx->cfg.g_usage != AOM_USAGE_ALL_INTRA) return AOM_CODEC_INCAPABLE;
    // Enable QMs as they've been found to be beneficial for images, when used
    // with alternative QM formulas:
    // - aom_get_qmlevel_allintra()
    // - aom_get_qmlevel_luma_ssimulacra2()
    // - aom_get_qmlevel_444_chroma()
    extra_cfg->enable_qm = 1;
    extra_cfg->qm_min = QM_FIRST_IQ_SSIMULACRA2;
    extra_cfg->qm_max = QM_LAST_IQ_SSIMULACRA2;
    // We can turn on loop filter sharpness, as frames do not have to serve as
    // references to others.
    extra_cfg->sharpness = 7;
    // Using the QM-PSNR metric was found to be beneficial for images (over the
    // default PSNR metric), as it correlates better with subjective image
    // quality consistency and better SSIMULACRA 2 scores.
    extra_cfg->dist_metric = AOM_DIST_METRIC_QM_PSNR;
    // CDEF_ALL has been found to blur images at medium and high quality
    // qindexes, so let's use a version that adapts CDEF strength on frame
    // qindexes. CDEF_ADAPTIVE strengths look like this for varying qindexes:
    // - CDEF off:          0 -  32
    // - Reduced strength: 33 - 220
    // - Full strength:   221 - 255
    extra_cfg->enable_cdef = CDEF_ADAPTIVE;
    // Enable chroma deltaq so the encoder can factor in chroma subsampling and
    // adjust chroma quality when necessary.
    extra_cfg->enable_chroma_deltaq = 1;
    // Enable "Variance Boost" deltaq mode, optimized for images.
    extra_cfg->deltaq_mode = DELTA_Q_VARIANCE_BOOST;
  }
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_set_tuning(aom_codec_alg_priv_t *ctx,
                                       va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.tuning = CAST(AOME_SET_TUNING, args);
  aom_codec_err_t err = handle_tuning(ctx, &extra_cfg);
  if (err != AOM_CODEC_OK) return err;
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_cq_level(aom_codec_alg_priv_t *ctx,
                                         va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.cq_level = CAST(AOME_SET_CQ_LEVEL, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_rc_max_intra_bitrate_pct(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.rc_max_intra_bitrate_pct =
      CAST(AOME_SET_MAX_INTRA_BITRATE_PCT, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_rc_max_inter_bitrate_pct(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.rc_max_inter_bitrate_pct =
      CAST(AOME_SET_MAX_INTER_BITRATE_PCT, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_rc_gf_cbr_boost_pct(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.gf_cbr_boost_pct = CAST(AV1E_SET_GF_CBR_BOOST_PCT, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_lossless(aom_codec_alg_priv_t *ctx,
                                         va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.lossless = CAST(AV1E_SET_LOSSLESS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_cdef(aom_codec_alg_priv_t *ctx,
                                            va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_cdef = CAST(AV1E_SET_ENABLE_CDEF, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_restoration(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  const unsigned int restoration_arg = CAST(AV1E_SET_ENABLE_RESTORATION, args);
#if CONFIG_REALTIME_ONLY
  if (restoration_arg) {
    ERROR("Restoration can't be turned on in realtime only build.");
  }
#endif
  extra_cfg.enable_restoration = restoration_arg;
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_force_video_mode(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.force_video_mode = CAST(AV1E_SET_FORCE_VIDEO_MODE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_obmc(aom_codec_alg_priv_t *ctx,
                                            va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  const unsigned int obmc_arg = CAST(AV1E_SET_ENABLE_OBMC, args);
#if CONFIG_REALTIME_ONLY
  if (obmc_arg) {
    ERROR("OBMC can't be enabled in realtime only build.");
  }
#endif
  extra_cfg.enable_obmc = obmc_arg;
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_disable_trellis_quant(aom_codec_alg_priv_t *ctx,
                                                      va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.disable_trellis_quant = CAST(AV1E_SET_DISABLE_TRELLIS_QUANT, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_qm(aom_codec_alg_priv_t *ctx,
                                          va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_qm = CAST(AV1E_SET_ENABLE_QM, args);
#if !CONFIG_QUANT_MATRIX
  if (extra_cfg.enable_qm) {
    ERROR("QM can't be enabled with CONFIG_QUANT_MATRIX=0.");
  }
#endif
  return update_extra_cfg(ctx, &extra_cfg);
}
static aom_codec_err_t ctrl_set_qm_y(aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.qm_y = CAST(AV1E_SET_QM_Y, args);
  return update_extra_cfg(ctx, &extra_cfg);
}
static aom_codec_err_t ctrl_set_qm_u(aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.qm_u = CAST(AV1E_SET_QM_U, args);
  return update_extra_cfg(ctx, &extra_cfg);
}
static aom_codec_err_t ctrl_set_qm_v(aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.qm_v = CAST(AV1E_SET_QM_V, args);
  return update_extra_cfg(ctx, &extra_cfg);
}
static aom_codec_err_t ctrl_set_qm_min(aom_codec_alg_priv_t *ctx,
                                       va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.qm_min = CAST(AV1E_SET_QM_MIN, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_qm_max(aom_codec_alg_priv_t *ctx,
                                       va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.qm_max = CAST(AV1E_SET_QM_MAX, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_num_tg(aom_codec_alg_priv_t *ctx,
                                       va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.num_tg = CAST(AV1E_SET_NUM_TG, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_mtu(aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.mtu_size = CAST(AV1E_SET_MTU, args);
  return update_extra_cfg(ctx, &extra_cfg);
}
static aom_codec_err_t ctrl_set_timing_info_type(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.timing_info_type = CAST(AV1E_SET_TIMING_INFO_TYPE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_low_complexity_decode(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_low_complexity_decode =
      CAST(AV1E_SET_ENABLE_LOW_COMPLEXITY_DECODE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_dual_filter(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_dual_filter = CAST(AV1E_SET_ENABLE_DUAL_FILTER, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_chroma_deltaq(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_chroma_deltaq = CAST(AV1E_SET_ENABLE_CHROMA_DELTAQ, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_rect_partitions(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_rect_partitions =
      CAST(AV1E_SET_ENABLE_RECT_PARTITIONS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_ab_partitions(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_ab_partitions = CAST(AV1E_SET_ENABLE_AB_PARTITIONS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_1to4_partitions(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_1to4_partitions =
      CAST(AV1E_SET_ENABLE_1TO4_PARTITIONS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_min_partition_size(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.min_partition_size = CAST(AV1E_SET_MIN_PARTITION_SIZE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_max_partition_size(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.max_partition_size = CAST(AV1E_SET_MAX_PARTITION_SIZE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_intra_edge_filter(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_intra_edge_filter =
      CAST(AV1E_SET_ENABLE_INTRA_EDGE_FILTER, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_order_hint(aom_codec_alg_priv_t *ctx,
                                                  va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_order_hint = CAST(AV1E_SET_ENABLE_ORDER_HINT, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_tx64(aom_codec_alg_priv_t *ctx,
                                            va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_tx64 = CAST(AV1E_SET_ENABLE_TX64, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_flip_idtx(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_flip_idtx = CAST(AV1E_SET_ENABLE_FLIP_IDTX, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_rect_tx(aom_codec_alg_priv_t *ctx,
                                               va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_rect_tx = CAST(AV1E_SET_ENABLE_RECT_TX, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_dist_wtd_comp(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_dist_wtd_comp = CAST(AV1E_SET_ENABLE_DIST_WTD_COMP, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_max_reference_frames(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.max_reference_frames = CAST(AV1E_SET_MAX_REFERENCE_FRAMES, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_reduced_reference_set(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_reduced_reference_set =
      CAST(AV1E_SET_REDUCED_REFERENCE_SET, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_ref_frame_mvs(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_ref_frame_mvs = CAST(AV1E_SET_ENABLE_REF_FRAME_MVS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_allow_ref_frame_mvs(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.allow_ref_frame_mvs = CAST(AV1E_SET_ALLOW_REF_FRAME_MVS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_masked_comp(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_masked_comp = CAST(AV1E_SET_ENABLE_MASKED_COMP, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_onesided_comp(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_onesided_comp = CAST(AV1E_SET_ENABLE_ONESIDED_COMP, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_interintra_comp(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_interintra_comp =
      CAST(AV1E_SET_ENABLE_INTERINTRA_COMP, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_smooth_interintra(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_smooth_interintra =
      CAST(AV1E_SET_ENABLE_SMOOTH_INTERINTRA, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_diff_wtd_comp(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_diff_wtd_comp = CAST(AV1E_SET_ENABLE_DIFF_WTD_COMP, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_interinter_wedge(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_interinter_wedge =
      CAST(AV1E_SET_ENABLE_INTERINTER_WEDGE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_interintra_wedge(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_interintra_wedge =
      CAST(AV1E_SET_ENABLE_INTERINTRA_WEDGE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_global_motion(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  const int global_motion_arg = CAST(AV1E_SET_ENABLE_GLOBAL_MOTION, args);
#if CONFIG_REALTIME_ONLY
  if (global_motion_arg) {
    ERROR("Global motion can't be enabled in realtime only build.");
  }
#endif
  extra_cfg.enable_global_motion = global_motion_arg;
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_warped_motion(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  const int warped_motion_arg = CAST(AV1E_SET_ENABLE_WARPED_MOTION, args);
#if CONFIG_REALTIME_ONLY
  if (warped_motion_arg) {
    ERROR("Warped motion can't be enabled in realtime only build.");
  }
#endif
  extra_cfg.enable_warped_motion = warped_motion_arg;
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_allow_warped_motion(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.allow_warped_motion = CAST(AV1E_SET_ALLOW_WARPED_MOTION, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_filter_intra(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_filter_intra = CAST(AV1E_SET_ENABLE_FILTER_INTRA, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_smooth_intra(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_smooth_intra = CAST(AV1E_SET_ENABLE_SMOOTH_INTRA, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_directional_intra(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_directional_intra =
      CAST(AV1E_SET_ENABLE_DIRECTIONAL_INTRA, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_diagonal_intra(aom_codec_alg_priv_t *ctx,
                                                      va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_diagonal_intra = CAST(AV1E_SET_ENABLE_DIAGONAL_INTRA, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_paeth_intra(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_paeth_intra = CAST(AV1E_SET_ENABLE_PAETH_INTRA, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_cfl_intra(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_cfl_intra = CAST(AV1E_SET_ENABLE_CFL_INTRA, args);
#if CONFIG_REALTIME_ONLY
  if (extra_cfg.enable_cfl_intra) {
    ERROR("cfl can't be turned on in realtime only build.");
  }
#endif
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_superres(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_superres = CAST(AV1E_SET_ENABLE_SUPERRES, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_overlay(aom_codec_alg_priv_t *ctx,
                                               va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_overlay = CAST(AV1E_SET_ENABLE_OVERLAY, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_palette(aom_codec_alg_priv_t *ctx,
                                               va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_palette = CAST(AV1E_SET_ENABLE_PALETTE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_intrabc(aom_codec_alg_priv_t *ctx,
                                               va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_intrabc = CAST(AV1E_SET_ENABLE_INTRABC, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_angle_delta(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_angle_delta = CAST(AV1E_SET_ENABLE_ANGLE_DELTA, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_error_resilient_mode(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.error_resilient_mode = CAST(AV1E_SET_ERROR_RESILIENT_MODE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_s_frame_mode(aom_codec_alg_priv_t *ctx,
                                             va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.s_frame_mode = CAST(AV1E_SET_S_FRAME_MODE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_frame_parallel_decoding_mode(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.frame_parallel_decoding_mode =
      CAST(AV1E_SET_FRAME_PARALLEL_DECODING, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_single_tile_decoding(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.single_tile_decoding = CAST(AV1E_SET_SINGLE_TILE_DECODING, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_aq_mode(aom_codec_alg_priv_t *ctx,
                                        va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.aq_mode = CAST(AV1E_SET_AQ_MODE, args);

  // Skip AQ mode if using fixed QP for current frame.
  if (ctx->ppi->cpi->rc.use_external_qp_one_pass) extra_cfg.aq_mode = 0;
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_reduced_tx_type_set(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.reduced_tx_type_set = CAST(AV1E_SET_REDUCED_TX_TYPE_SET, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_intra_dct_only(aom_codec_alg_priv_t *ctx,
                                               va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.use_intra_dct_only = CAST(AV1E_SET_INTRA_DCT_ONLY, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_inter_dct_only(aom_codec_alg_priv_t *ctx,
                                               va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.use_inter_dct_only = CAST(AV1E_SET_INTER_DCT_ONLY, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_intra_default_tx_only(aom_codec_alg_priv_t *ctx,
                                                      va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.use_intra_default_tx_only =
      CAST(AV1E_SET_INTRA_DEFAULT_TX_ONLY, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_tx_size_search(aom_codec_alg_priv_t *ctx,
                                                      va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_tx_size_search = CAST(AV1E_SET_ENABLE_TX_SIZE_SEARCH, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_quant_b_adapt(aom_codec_alg_priv_t *ctx,
                                              va_list args) {
#if CONFIG_REALTIME_ONLY
  (void)ctx;
  (void)args;
  return AOM_CODEC_INCAPABLE;
#else
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.quant_b_adapt = CAST(AV1E_SET_QUANT_B_ADAPT, args);
  return update_extra_cfg(ctx, &extra_cfg);
#endif
}

static aom_codec_err_t ctrl_set_vbr_corpus_complexity_lap(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.vbr_corpus_complexity_lap =
      CAST(AV1E_SET_VBR_CORPUS_COMPLEXITY_LAP, args);
  return update_extra_cfg(ctx, &extra_cfg);
}
static aom_codec_err_t ctrl_set_coeff_cost_upd_freq(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.coeff_cost_upd_freq = CAST(AV1E_SET_COEFF_COST_UPD_FREQ, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_mode_cost_upd_freq(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.mode_cost_upd_freq = CAST(AV1E_SET_MODE_COST_UPD_FREQ, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_mv_cost_upd_freq(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.mv_cost_upd_freq = CAST(AV1E_SET_MV_COST_UPD_FREQ, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_dv_cost_upd_freq(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.dv_cost_upd_freq = CAST(AV1E_SET_DV_COST_UPD_FREQ, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_vmaf_model_path(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  const char *str = CAST(AV1E_SET_VMAF_MODEL_PATH, args);
  const aom_codec_err_t ret = allocate_and_set_string(
      str, default_extra_cfg.vmaf_model_path, &extra_cfg.vmaf_model_path,
      ctx->ppi->error.detail);
  if (ret != AOM_CODEC_OK) return ret;
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_partition_info_path(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  const char *str = CAST(AV1E_SET_PARTITION_INFO_PATH, args);
  const aom_codec_err_t ret = allocate_and_set_string(
      str, default_extra_cfg.partition_info_path,
      &extra_cfg.partition_info_path, ctx->ppi->error.detail);
  if (ret != AOM_CODEC_OK) return ret;
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_enable_rate_guide_deltaq(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_rate_guide_deltaq =
      CAST(AV1E_ENABLE_RATE_GUIDE_DELTAQ, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_rate_distribution_info(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  const char *str = CAST(AV1E_SET_RATE_DISTRIBUTION_INFO, args);
  const aom_codec_err_t ret = allocate_and_set_string(
      str, default_extra_cfg.rate_distribution_info,
      &extra_cfg.rate_distribution_info, ctx->ppi->error.detail);
  if (ret != AOM_CODEC_OK) return ret;
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_film_grain_test_vector(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.film_grain_test_vector =
      CAST(AV1E_SET_FILM_GRAIN_TEST_VECTOR, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_film_grain_table(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  const char *str = CAST(AV1E_SET_FILM_GRAIN_TABLE, args);
  if (str == NULL) {
    // this parameter allows NULL as its value
    extra_cfg.film_grain_table_filename = str;
  } else {
#if CONFIG_REALTIME_ONLY
    ERROR("film_grain removed from realtime only build.");
#endif
    const aom_codec_err_t ret = allocate_and_set_string(
        str, default_extra_cfg.film_grain_table_filename,
        &extra_cfg.film_grain_table_filename, ctx->ppi->error.detail);
    if (ret != AOM_CODEC_OK) return ret;
  }
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_denoise_noise_level(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
#if !CONFIG_DENOISE
  (void)ctx;
  (void)args;
  return AOM_CODEC_INCAPABLE;
#else
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.noise_level =
      ((float)CAST(AV1E_SET_DENOISE_NOISE_LEVEL, args)) / 10.0f;
  return update_extra_cfg(ctx, &extra_cfg);
#endif
}

static aom_codec_err_t ctrl_set_denoise_block_size(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
#if !CONFIG_DENOISE
  (void)ctx;
  (void)args;
  return AOM_CODEC_INCAPABLE;
#else
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.noise_block_size = CAST(AV1E_SET_DENOISE_BLOCK_SIZE, args);
  return update_extra_cfg(ctx, &extra_cfg);
#endif
}

static aom_codec_err_t ctrl_set_enable_dnl_denoising(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
#if !CONFIG_DENOISE
  (void)ctx;
  (void)args;
  return AOM_CODEC_INCAPABLE;
#else
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_dnl_denoising = CAST(AV1E_SET_ENABLE_DNL_DENOISING, args);
  return update_extra_cfg(ctx, &extra_cfg);
#endif
}

static aom_codec_err_t ctrl_set_deltaq_mode(aom_codec_alg_priv_t *ctx,
                                            va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  const DELTAQ_MODE deltaq_arg = CAST(AV1E_SET_DELTAQ_MODE, args);
#if CONFIG_REALTIME_ONLY
  if (deltaq_arg > NO_DELTA_Q) {
    ERROR("Delta Q mode can't be enabled in realtime only build.");
  }
#endif
  extra_cfg.deltaq_mode = deltaq_arg;
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_deltaq_strength(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.deltaq_strength = CAST(AV1E_SET_DELTAQ_STRENGTH, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_deltalf_mode(aom_codec_alg_priv_t *ctx,
                                             va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.deltalf_mode = CAST(AV1E_SET_DELTALF_MODE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_min_gf_interval(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.min_gf_interval = CAST(AV1E_SET_MIN_GF_INTERVAL, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_max_gf_interval(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.max_gf_interval = CAST(AV1E_SET_MAX_GF_INTERVAL, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_gf_min_pyr_height(aom_codec_alg_priv_t *ctx,
                                                  va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.gf_min_pyr_height = CAST(AV1E_SET_GF_MIN_PYRAMID_HEIGHT, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_gf_max_pyr_height(aom_codec_alg_priv_t *ctx,
                                                  va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.gf_max_pyr_height = CAST(AV1E_SET_GF_MAX_PYRAMID_HEIGHT, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_frame_periodic_boost(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.frame_periodic_boost = CAST(AV1E_SET_FRAME_PERIODIC_BOOST, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_enable_motion_vector_unit_test(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.motion_vector_unit_test =
      CAST(AV1E_ENABLE_MOTION_VECTOR_UNIT_TEST, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_enable_fpmt_unit_test(aom_codec_alg_priv_t *ctx,
                                                  va_list args) {
#if !CONFIG_FPMT_TEST
  (void)args;
  (void)ctx;
  return AOM_CODEC_INCAPABLE;
#else
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.fpmt_unit_test = CAST(AV1E_SET_FP_MT_UNIT_TEST, args);
  ctx->ppi->fpmt_unit_test_cfg = (extra_cfg.fpmt_unit_test == 1)
                                     ? PARALLEL_ENCODE
                                     : PARALLEL_SIMULATION_ENCODE;
  return update_extra_cfg(ctx, &extra_cfg);
#endif
}

static aom_codec_err_t ctrl_enable_ext_tile_debug(aom_codec_alg_priv_t *ctx,
                                                  va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.ext_tile_debug = CAST(AV1E_ENABLE_EXT_TILE_DEBUG, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_target_seq_level_idx(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  const int val = CAST(AV1E_SET_TARGET_SEQ_LEVEL_IDX, args);
  const int level = val % 100;
  const int operating_point_idx = val / 100;
  if (operating_point_idx < 0 ||
      operating_point_idx >= MAX_NUM_OPERATING_POINTS) {
    char *const err_string = ctx->ppi->error.detail;
    snprintf(err_string, ARG_ERR_MSG_MAX_LEN,
             "Invalid operating point index: %d", operating_point_idx);
    ctx->base.err_detail = err_string;
    return AOM_CODEC_INVALID_PARAM;
  }
  extra_cfg.target_seq_level_idx[operating_point_idx] = (AV1_LEVEL)level;
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_tier_mask(aom_codec_alg_priv_t *ctx,
                                          va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.tier_mask = CAST(AV1E_SET_TIER_MASK, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_min_cr(aom_codec_alg_priv_t *ctx,
                                       va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.min_cr = CAST(AV1E_SET_MIN_CR, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_enable_sb_multipass_unit_test(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.sb_multipass_unit_test =
      CAST(AV1E_ENABLE_SB_MULTIPASS_UNIT_TEST, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_enable_sb_qp_sweep(aom_codec_alg_priv_t *ctx,
                                               va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.sb_qp_sweep = CAST(AV1E_ENABLE_SB_QP_SWEEP, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_external_partition(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  AV1_COMP *const cpi = ctx->ppi->cpi;
  aom_ext_part_funcs_t funcs = *CAST(AV1E_SET_EXTERNAL_PARTITION, args);
  aom_ext_part_config_t config;
  // TODO(chengchen): verify the sb_size has been set at this point.
  config.superblock_size = cpi->common.seq_params->sb_size;
  const aom_codec_err_t status =
      av1_ext_part_create(funcs, config, &cpi->ext_part_controller);
  return status;
}

static aom_codec_err_t ctrl_set_loopfilter_control(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.loopfilter_control = CAST(AV1E_SET_LOOPFILTER_CONTROL, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_skip_postproc_filtering(
    aom_codec_alg_priv_t *ctx, va_list args) {
  // Skipping the application of post-processing filters is allowed only
  // for ALLINTRA mode.
  if (ctx->cfg.g_usage != AOM_USAGE_ALL_INTRA) return AOM_CODEC_INCAPABLE;
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.skip_postproc_filtering =
      CAST(AV1E_SET_SKIP_POSTPROC_FILTERING, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_rtc_external_rc(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  ctx->ppi->cpi->rc.rtc_external_ratectrl =
      CAST(AV1E_SET_RTC_EXTERNAL_RC, args);
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_set_quantizer_one_pass(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  const int qp = CAST(AV1E_SET_QUANTIZER_ONE_PASS, args);

  if (qp < 0 || qp > 63) return AOM_CODEC_INVALID_PARAM;

  aom_codec_enc_cfg_t *cfg = &ctx->cfg;
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  cfg->rc_min_quantizer = cfg->rc_max_quantizer = qp;
  extra_cfg.aq_mode = 0;
  ctx->ppi->cpi->rc.use_external_qp_one_pass = 1;

  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_bitrate_one_pass_cbr(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  AV1_PRIMARY *const ppi = ctx->ppi;
  AV1_COMP *const cpi = ppi->cpi;
  AV1EncoderConfig *oxcf = &cpi->oxcf;
  if (!is_one_pass_rt_params(cpi) || oxcf->rc_cfg.mode != AOM_CBR ||
      cpi->ppi->use_svc || ppi->num_fp_contexts != 1 || ppi->cpi_lap != NULL) {
    return AOM_CODEC_INVALID_PARAM;
  }
  const int new_bitrate = CAST(AV1E_SET_BITRATE_ONE_PASS_CBR, args);
  ctx->cfg.rc_target_bitrate = new_bitrate;
  oxcf->rc_cfg.target_bandwidth = new_bitrate * 1000;
  set_primary_rc_buffer_sizes(oxcf, ppi);
  av1_new_framerate(cpi, cpi->framerate);
  check_reset_rc_flag(cpi);
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_set_max_consec_frame_drop_cbr(
    aom_codec_alg_priv_t *ctx, va_list args) {
  AV1_PRIMARY *const ppi = ctx->ppi;
  AV1_COMP *const cpi = ppi->cpi;
  const int max_consec_drop = CAST(AV1E_SET_MAX_CONSEC_FRAME_DROP_CBR, args);
  if (max_consec_drop < 0) return AOM_CODEC_INVALID_PARAM;
  cpi->rc.max_consec_drop = max_consec_drop;
  cpi->rc.drop_count_consec = 0;
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_set_max_consec_frame_drop_ms_cbr(
    aom_codec_alg_priv_t *ctx, va_list args) {
  AV1_PRIMARY *const ppi = ctx->ppi;
  AV1_COMP *const cpi = ppi->cpi;
  const int max_consec_drop_ms =
      CAST(AV1E_SET_MAX_CONSEC_FRAME_DROP_MS_CBR, args);
  if (max_consec_drop_ms < 0) return AOM_CODEC_INVALID_PARAM;
  // max_consec_drop_ms will be converted to frame units inside encoder
  // based on framerate (which can change dynamically).
  ctx->oxcf.rc_cfg.max_consec_drop_ms = max_consec_drop_ms;
  cpi->rc.drop_count_consec = 0;
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_set_svc_frame_drop_mode(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  AV1_PRIMARY *const ppi = ctx->ppi;
  AV1_COMP *const cpi = ppi->cpi;
  cpi->svc.framedrop_mode = CAST(AV1E_SET_SVC_FRAME_DROP_MODE, args);
  if (cpi->svc.framedrop_mode != AOM_LAYER_DROP &&
      cpi->svc.framedrop_mode != AOM_FULL_SUPERFRAME_DROP)
    return AOM_CODEC_INVALID_PARAM;
  else
    return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_set_auto_tiles(aom_codec_alg_priv_t *ctx,
                                           va_list args) {
  unsigned int auto_tiles = CAST(AV1E_SET_AUTO_TILES, args);
  if (auto_tiles == ctx->extra_cfg.auto_tiles) return AOM_CODEC_OK;
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.auto_tiles = auto_tiles;
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_postencode_drop_rtc(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  AV1_PRIMARY *const ppi = ctx->ppi;
  AV1_COMP *const cpi = ppi->cpi;
  int enable_postencode_drop = CAST(AV1E_SET_POSTENCODE_DROP_RTC, args);
  if (enable_postencode_drop > 1 || enable_postencode_drop < 0)
    return AOM_CODEC_INVALID_PARAM;
  cpi->rc.postencode_drop = enable_postencode_drop;
  return AOM_CODEC_OK;
}

#if !CONFIG_REALTIME_ONLY
static aom_codec_err_t create_stats_buffer(FIRSTPASS_STATS **frame_stats_buffer,
                                           STATS_BUFFER_CTX *stats_buf_context,
                                           int num_lap_buffers) {
  aom_codec_err_t res = AOM_CODEC_OK;

  int size = get_stats_buf_size(num_lap_buffers, MAX_LAG_BUFFERS);
  *frame_stats_buffer =
      (FIRSTPASS_STATS *)aom_calloc(size, sizeof(FIRSTPASS_STATS));
  if (*frame_stats_buffer == NULL) return AOM_CODEC_MEM_ERROR;

  stats_buf_context->stats_in_start = *frame_stats_buffer;
  stats_buf_context->stats_in_end = stats_buf_context->stats_in_start;
  stats_buf_context->stats_in_buf_end =
      stats_buf_context->stats_in_start + size;

  stats_buf_context->total_left_stats = aom_calloc(1, sizeof(FIRSTPASS_STATS));
  if (stats_buf_context->total_left_stats == NULL) return AOM_CODEC_MEM_ERROR;
  av1_twopass_zero_stats(stats_buf_context->total_left_stats);
  stats_buf_context->total_stats = aom_calloc(1, sizeof(FIRSTPASS_STATS));
  if (stats_buf_context->total_stats == NULL) return AOM_CODEC_MEM_ERROR;
  av1_twopass_zero_stats(stats_buf_context->total_stats);
  return res;
}
#endif

aom_codec_err_t av1_create_context_and_bufferpool(AV1_PRIMARY *ppi,
                                                  AV1_COMP **p_cpi,
                                                  BufferPool **p_buffer_pool,
                                                  const AV1EncoderConfig *oxcf,
                                                  COMPRESSOR_STAGE stage,
                                                  int lap_lag_in_frames) {
  aom_codec_err_t res = AOM_CODEC_OK;
  BufferPool *buffer_pool = *p_buffer_pool;

  if (buffer_pool == NULL) {
    buffer_pool = (BufferPool *)aom_calloc(1, sizeof(BufferPool));
    if (buffer_pool == NULL) return AOM_CODEC_MEM_ERROR;
    buffer_pool->num_frame_bufs =
        (oxcf->mode == ALLINTRA) ? FRAME_BUFFERS_ALLINTRA : FRAME_BUFFERS;
    buffer_pool->frame_bufs = (RefCntBuffer *)aom_calloc(
        buffer_pool->num_frame_bufs, sizeof(*buffer_pool->frame_bufs));
    if (buffer_pool->frame_bufs == NULL) {
      buffer_pool->num_frame_bufs = 0;
      aom_free(buffer_pool);
      return AOM_CODEC_MEM_ERROR;
    }
#if CONFIG_MULTITHREAD
    if (pthread_mutex_init(&buffer_pool->pool_mutex, NULL)) {
      aom_free(buffer_pool->frame_bufs);
      buffer_pool->frame_bufs = NULL;
      buffer_pool->num_frame_bufs = 0;
      aom_free(buffer_pool);
      return AOM_CODEC_MEM_ERROR;
    }
#endif
    *p_buffer_pool = buffer_pool;
  }
  *p_cpi =
      av1_create_compressor(ppi, oxcf, buffer_pool, stage, lap_lag_in_frames);
  if (*p_cpi == NULL) res = AOM_CODEC_MEM_ERROR;

  return res;
}

static aom_codec_err_t ctrl_set_fp_mt(aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.fp_mt = CAST(AV1E_SET_FP_MT, args);
  const aom_codec_err_t result = update_extra_cfg(ctx, &extra_cfg);
  int num_fp_contexts = 1;
  if (ctx->ppi->num_fp_contexts == 1) {
    num_fp_contexts =
        av1_compute_num_fp_contexts(ctx->ppi, &ctx->ppi->parallel_cpi[0]->oxcf);
    if (num_fp_contexts > 1) {
      int i;
      for (i = 1; i < num_fp_contexts; i++) {
        int res = av1_create_context_and_bufferpool(
            ctx->ppi, &ctx->ppi->parallel_cpi[i], &ctx->buffer_pool, &ctx->oxcf,
            ENCODE_STAGE, -1);
        if (res != AOM_CODEC_OK) {
          return res;
        }
#if !CONFIG_REALTIME_ONLY
        ctx->ppi->parallel_cpi[i]->twopass_frame.stats_in =
            ctx->ppi->twopass.stats_buf_ctx->stats_in_start;
#endif
      }
    }
  }
  ctx->ppi->num_fp_contexts = num_fp_contexts;
  return result;
}

static aom_codec_err_t ctrl_set_auto_intra_tools_off(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.auto_intra_tools_off = CAST(AV1E_SET_AUTO_INTRA_TOOLS_OFF, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t encoder_init(aom_codec_ctx_t *ctx) {
  aom_codec_err_t res = AOM_CODEC_OK;

  if (ctx->priv == NULL) {
    aom_codec_alg_priv_t *const priv = aom_calloc(1, sizeof(*priv));
    if (priv == NULL) return AOM_CODEC_MEM_ERROR;

    ctx->priv = (aom_codec_priv_t *)priv;
    ctx->priv->init_flags = ctx->init_flags;

    // Update the reference to the config structure to an internal copy.
    assert(ctx->config.enc);
    priv->cfg = *ctx->config.enc;
    ctx->config.enc = &priv->cfg;

    priv->extra_cfg = default_extra_cfg;
    // Special handling:
    // By default, if omitted: --enable-cdef=1, --qm-min=5, and --qm-max=9
    // Here we set its default values to 0, 4, and 10 respectively when
    // --allintra is turned on.
    // However, if users set --enable-cdef, --qm-min, or --qm-max, either from
    // the command line or aom_codec_control(), the encoder still respects it.
    if (priv->cfg.g_usage == AOM_USAGE_ALL_INTRA) {
      // CDEF has been found to blur images, so it's disabled in all-intra mode
      priv->extra_cfg.enable_cdef = 0;
      // These QM min/max values have been found to be beneficial for images,
      // when used with an alternative QM formula (see
      // aom_get_qmlevel_allintra()).
      // These values could also be beneficial for other usage modes, but
      // further testing is required.
      priv->extra_cfg.qm_min = DEFAULT_QM_FIRST_ALLINTRA;
      priv->extra_cfg.qm_max = DEFAULT_QM_LAST_ALLINTRA;
    }
    av1_initialize_enc(priv->cfg.g_usage, priv->cfg.rc_end_usage);

    res = validate_config(priv, &priv->cfg, &priv->extra_cfg);

    if (res == AOM_CODEC_OK) {
      int *num_lap_buffers = &priv->num_lap_buffers;
      int lap_lag_in_frames = 0;
      *num_lap_buffers = 0;
      priv->timestamp_ratio.den = priv->cfg.g_timebase.den;
      priv->timestamp_ratio.num =
          (int64_t)priv->cfg.g_timebase.num * TICKS_PER_SEC;
      reduce_ratio(&priv->timestamp_ratio);

      set_encoder_config(&priv->oxcf, &priv->cfg, &priv->extra_cfg);
      if (priv->oxcf.rc_cfg.mode != AOM_CBR &&
          priv->oxcf.pass == AOM_RC_ONE_PASS && priv->oxcf.mode == GOOD) {
        // Enable look ahead - enabled for AOM_Q, AOM_CQ, AOM_VBR
        *num_lap_buffers =
            AOMMIN((int)priv->cfg.g_lag_in_frames,
                   AOMMIN(MAX_LAP_BUFFERS, priv->oxcf.kf_cfg.key_freq_max +
                                               SCENE_CUT_KEY_TEST_INTERVAL));
        if ((int)priv->cfg.g_lag_in_frames - (*num_lap_buffers) >=
            LAP_LAG_IN_FRAMES) {
          lap_lag_in_frames = LAP_LAG_IN_FRAMES;
        }
      }
      priv->oxcf.use_highbitdepth =
          (ctx->init_flags & AOM_CODEC_USE_HIGHBITDEPTH) ? 1 : 0;

      priv->monochrome_on_init = priv->cfg.monochrome;

      priv->ppi = av1_create_primary_compressor(&priv->pkt_list.head,
                                                *num_lap_buffers, &priv->oxcf);
      if (!priv->ppi) return AOM_CODEC_MEM_ERROR;

#if !CONFIG_REALTIME_ONLY
      res = create_stats_buffer(&priv->frame_stats_buffer,
                                &priv->stats_buf_context, *num_lap_buffers);
      if (res != AOM_CODEC_OK) return res;

      assert(MAX_LAP_BUFFERS >= MAX_LAG_BUFFERS);
      int size = get_stats_buf_size(*num_lap_buffers, MAX_LAG_BUFFERS);
      for (int i = 0; i < size; i++)
        priv->ppi->twopass.frame_stats_arr[i] = &priv->frame_stats_buffer[i];

      priv->ppi->twopass.stats_buf_ctx = &priv->stats_buf_context;
#endif

      assert(priv->ppi->num_fp_contexts >= 1);
      res = av1_create_context_and_bufferpool(
          priv->ppi, &priv->ppi->parallel_cpi[0], &priv->buffer_pool,
          &priv->oxcf, ENCODE_STAGE, -1);
      if (res != AOM_CODEC_OK) {
        priv->base.err_detail = "av1_create_context_and_bufferpool() failed";
        return res;
      }
#if !CONFIG_REALTIME_ONLY
      priv->ppi->parallel_cpi[0]->twopass_frame.stats_in =
          priv->ppi->twopass.stats_buf_ctx->stats_in_start;
#endif
      priv->ppi->cpi = priv->ppi->parallel_cpi[0];

      // Create another compressor if look ahead is enabled
      if (res == AOM_CODEC_OK && *num_lap_buffers) {
        res = av1_create_context_and_bufferpool(
            priv->ppi, &priv->ppi->cpi_lap, &priv->buffer_pool_lap, &priv->oxcf,
            LAP_STAGE, clamp(lap_lag_in_frames, 0, MAX_LAG_BUFFERS));
      }
    }
  }

  return res;
}

void av1_destroy_context_and_bufferpool(AV1_COMP *cpi,
                                        BufferPool **p_buffer_pool) {
  av1_remove_compressor(cpi);
  if (*p_buffer_pool) {
    av1_free_ref_frame_buffers(*p_buffer_pool);
#if CONFIG_MULTITHREAD
    pthread_mutex_destroy(&(*p_buffer_pool)->pool_mutex);
#endif
    aom_free(*p_buffer_pool);
    *p_buffer_pool = NULL;
  }
}

static void destroy_stats_buffer(STATS_BUFFER_CTX *stats_buf_context,
                                 FIRSTPASS_STATS *frame_stats_buffer) {
  aom_free(stats_buf_context->total_left_stats);
  aom_free(stats_buf_context->total_stats);
  aom_free(frame_stats_buffer);
}

static void check_and_free_string(const char *default_str, const char **ptr) {
  if (*ptr == default_str) {
    // Default should be a literal. Do not free.
    return;
  }
  aom_free((void *)*ptr);
  *ptr = NULL;
}

static void destroy_extra_config(struct av1_extracfg *extra_cfg) {
#if CONFIG_TUNE_VMAF
  check_and_free_string(default_extra_cfg.vmaf_model_path,
                        &extra_cfg->vmaf_model_path);
#endif
  check_and_free_string(default_extra_cfg.two_pass_output,
                        &extra_cfg->two_pass_output);
  check_and_free_string(default_extra_cfg.two_pass_output,
                        &extra_cfg->second_pass_log);
  check_and_free_string(default_extra_cfg.partition_info_path,
                        &extra_cfg->partition_info_path);
  check_and_free_string(default_extra_cfg.rate_distribution_info,
                        &extra_cfg->rate_distribution_info);
  check_and_free_string(default_extra_cfg.film_grain_table_filename,
                        &extra_cfg->film_grain_table_filename);
}

static aom_codec_err_t encoder_destroy(aom_codec_alg_priv_t *ctx) {
  free(ctx->cx_data);
  destroy_extra_config(&ctx->extra_cfg);

  if (ctx->ppi) {
    AV1_PRIMARY *ppi = ctx->ppi;
    for (int i = 0; i < MAX_PARALLEL_FRAMES - 1; i++) {
      if (ppi->parallel_frames_data[i].cx_data) {
        free(ppi->parallel_frames_data[i].cx_data);
      }
    }
#if CONFIG_ENTROPY_STATS
    print_entropy_stats(ppi);
#endif
#if CONFIG_INTERNAL_STATS
    print_internal_stats(ppi);
#endif

    for (int i = 0; i < MAX_PARALLEL_FRAMES; i++) {
      av1_destroy_context_and_bufferpool(ppi->parallel_cpi[i],
                                         &ctx->buffer_pool);
    }
    ppi->cpi = NULL;

    if (ppi->cpi_lap) {
      av1_destroy_context_and_bufferpool(ppi->cpi_lap, &ctx->buffer_pool_lap);
    }
    av1_remove_primary_compressor(ppi);
  }
  destroy_stats_buffer(&ctx->stats_buf_context, ctx->frame_stats_buffer);
  aom_free(ctx);
  return AOM_CODEC_OK;
}

static aom_codec_frame_flags_t get_frame_pkt_flags(const AV1_COMP *cpi,
                                                   unsigned int lib_flags) {
  aom_codec_frame_flags_t flags = lib_flags << 16;
  if (lib_flags & FRAMEFLAGS_KEY) flags |= AOM_FRAME_IS_KEY;
  if (lib_flags & FRAMEFLAGS_INTRAONLY) flags |= AOM_FRAME_IS_INTRAONLY;
  if (lib_flags & FRAMEFLAGS_SWITCH) flags |= AOM_FRAME_IS_SWITCH;
  if (lib_flags & FRAMEFLAGS_ERROR_RESILIENT)
    flags |= AOM_FRAME_IS_ERROR_RESILIENT;
  if (cpi->droppable) flags |= AOM_FRAME_IS_DROPPABLE;

  return flags;
}

static inline int get_src_border_in_pixels(AV1_COMP *cpi, BLOCK_SIZE sb_size) {
  if (cpi->oxcf.mode != REALTIME || av1_is_resize_needed(&cpi->oxcf))
    return cpi->oxcf.border_in_pixels;

  const int sb_size_in_pixels_log2 = mi_size_wide_log2[sb_size] + MI_SIZE_LOG2;
  const int sb_aligned_width =
      ALIGN_POWER_OF_TWO(cpi->oxcf.frm_dim_cfg.width, sb_size_in_pixels_log2);
  const int sb_aligned_height =
      ALIGN_POWER_OF_TWO(cpi->oxcf.frm_dim_cfg.height, sb_size_in_pixels_log2);
  // Align the border pixels to a multiple of 32.
  const int border_pixels_width =
      ALIGN_POWER_OF_TWO(sb_aligned_width - cpi->oxcf.frm_dim_cfg.width, 5);
  const int border_pixels_height =
      ALIGN_POWER_OF_TWO(sb_aligned_height - cpi->oxcf.frm_dim_cfg.height, 5);
  const int border_in_pixels =
      AOMMAX(AOMMAX(border_pixels_width, border_pixels_height), 32);
  return border_in_pixels;
}

// TODO(Mufaddal): Check feasibility of abstracting functions related to LAP
// into a separate function.
static aom_codec_err_t encoder_encode(aom_codec_alg_priv_t *ctx,
                                      const aom_image_t *img,
                                      aom_codec_pts_t pts,
                                      unsigned long duration,
                                      aom_enc_frame_flags_t enc_flags) {
  const size_t kMinCompressedSize = 8192;
  volatile aom_codec_err_t res = AOM_CODEC_OK;
  AV1_PRIMARY *const ppi = ctx->ppi;
  volatile aom_codec_pts_t ptsvol = pts;
  AV1_COMP_DATA cpi_data = { 0 };

  cpi_data.timestamp_ratio = &ctx->timestamp_ratio;
  cpi_data.flush = !img;
  // LAP context
  AV1_COMP *cpi_lap = ppi->cpi_lap;
  if (ppi->cpi == NULL) return AOM_CODEC_INVALID_PARAM;

  ppi->cpi->last_coded_width = ppi->cpi->oxcf.frm_dim_cfg.width;
  ppi->cpi->last_coded_height = ppi->cpi->oxcf.frm_dim_cfg.height;

  if (ppi->lap_enabled && cpi_lap == NULL &&
      ppi->cpi->oxcf.pass == AOM_RC_ONE_PASS)
    return AOM_CODEC_INVALID_PARAM;

  if (img != NULL) {
    res = validate_img(ctx, img);
    if (res == AOM_CODEC_OK) {
      const uint64_t uncompressed_frame_sz64 =
          (uint64_t)ALIGN_POWER_OF_TWO_UNSIGNED(ctx->cfg.g_w, 5) *
          ALIGN_POWER_OF_TWO_UNSIGNED(ctx->cfg.g_h, 5) * get_image_bps(img) / 8;
#if UINT64_MAX > SIZE_MAX
      if (uncompressed_frame_sz64 > SIZE_MAX) return AOM_CODEC_MEM_ERROR;
#endif
      const size_t uncompressed_frame_sz = (size_t)uncompressed_frame_sz64;

      // Due to the presence of no-show frames, the ctx->cx_data buffer holds
      // compressed data corresponding to multiple frames. As no-show frames are
      // not possible for all intra frame encoding with no forward key frames,
      // the buffer is allocated with a smaller size in this case.
      //
      // For pseudo random input, the compressed frame size is seen to exceed
      // the uncompressed frame size, but is less than 2 times the uncompressed
      // frame size. Hence the size of the buffer is chosen as 2 times the
      // uncompressed frame size.
      int multiplier = 8;
      if (ppi->cpi->oxcf.kf_cfg.key_freq_max == 0 &&
          !ppi->cpi->oxcf.kf_cfg.fwd_kf_enabled)
        multiplier = 2;
      if (uncompressed_frame_sz > SIZE_MAX / multiplier)
        return AOM_CODEC_MEM_ERROR;
      size_t data_sz = uncompressed_frame_sz * multiplier;
      if (data_sz < kMinCompressedSize) data_sz = kMinCompressedSize;
      if (ctx->cx_data == NULL || ctx->cx_data_sz < data_sz) {
        ctx->cx_data_sz = data_sz;
        free(ctx->cx_data);
        ctx->cx_data = (unsigned char *)malloc(ctx->cx_data_sz);
        if (ctx->cx_data == NULL) {
          ctx->cx_data_sz = 0;
          return AOM_CODEC_MEM_ERROR;
        }
      }
      for (int i = 0; i < ppi->num_fp_contexts - 1; i++) {
        if (ppi->parallel_frames_data[i].cx_data == NULL ||
            ppi->parallel_frames_data[i].cx_data_sz < data_sz) {
          ppi->parallel_frames_data[i].cx_data_sz = data_sz;
          free(ppi->parallel_frames_data[i].cx_data);
          ppi->parallel_frames_data[i].frame_size = 0;
          ppi->parallel_frames_data[i].cx_data =
              (unsigned char *)malloc(ppi->parallel_frames_data[i].cx_data_sz);
          if (ppi->parallel_frames_data[i].cx_data == NULL) {
            ppi->parallel_frames_data[i].cx_data_sz = 0;
            return AOM_CODEC_MEM_ERROR;
          }
        }
      }
    }
  }

  aom_codec_pkt_list_init(&ctx->pkt_list);

  volatile aom_enc_frame_flags_t flags = enc_flags;

  // The jmp_buf is valid only for the duration of the function that calls
  // setjmp(). Therefore, this function must reset the 'setjmp' field to 0
  // before it returns.
  if (setjmp(ppi->error.jmp)) {
    ppi->error.setjmp = 0;
    res = update_error_state(ctx, &ppi->error);
    return res;
  }
  ppi->error.setjmp = 1;

  if (ppi->use_svc && ppi->cpi->svc.use_flexible_mode == 0 && flags == 0)
    av1_set_svc_fixed_mode(ppi->cpi);

  // Note(yunqing): While applying encoding flags, always start from enabling
  // all, and then modifying according to the flags. Previous frame's flags are
  // overwritten.
  av1_apply_encoding_flags(ppi->cpi, flags);
  if (cpi_lap != NULL) {
    av1_apply_encoding_flags(cpi_lap, flags);
  }

#if CONFIG_TUNE_VMAF
  if (ctx->extra_cfg.tuning >= AOM_TUNE_VMAF_WITH_PREPROCESSING &&
      ctx->extra_cfg.tuning <= AOM_TUNE_VMAF_NEG_MAX_GAIN) {
    aom_init_vmaf_model(&ppi->cpi->vmaf_info.vmaf_model,
                        ppi->cpi->oxcf.tune_cfg.vmaf_model_path);
  }
#endif

  // Handle fixed keyframe intervals
  if (is_stat_generation_stage(ppi->cpi) || is_one_pass_rt_params(ppi->cpi)) {
    if (ctx->cfg.kf_mode == AOM_KF_AUTO &&
        ctx->cfg.kf_min_dist == ctx->cfg.kf_max_dist) {
      if (ppi->cpi->common.spatial_layer_id == 0 &&
          ++ctx->fixed_kf_cntr > ctx->cfg.kf_min_dist) {
        flags |= AOM_EFLAG_FORCE_KF;
        ctx->fixed_kf_cntr = 1;
      }
    }
  }

  if (res == AOM_CODEC_OK) {
    AV1_COMP *cpi = ppi->cpi;

    // Set up internal flags
    if (ctx->base.init_flags & AOM_CODEC_USE_PSNR) ppi->b_calculate_psnr = 1;

    if (img != NULL) {
      if (!ctx->pts_offset_initialized) {
        ctx->pts_offset = ptsvol;
        ctx->pts_offset_initialized = 1;
      }
      if (ptsvol < ctx->pts_offset) {
        aom_internal_error(&ppi->error, AOM_CODEC_INVALID_PARAM,
                           "pts is smaller than initial pts");
      }
      ptsvol -= ctx->pts_offset;
      if (ptsvol > INT64_MAX / cpi_data.timestamp_ratio->num) {
        aom_internal_error(
            &ppi->error, AOM_CODEC_INVALID_PARAM,
            "conversion of relative pts to ticks would overflow");
      }
      int64_t src_time_stamp =
          timebase_units_to_ticks(cpi_data.timestamp_ratio, ptsvol);
#if ULONG_MAX > INT64_MAX
      if (duration > INT64_MAX) {
        aom_internal_error(&ppi->error, AOM_CODEC_INVALID_PARAM,
                           "duration is too big");
      }
#endif
      if (ptsvol > INT64_MAX - (int64_t)duration) {
        aom_internal_error(&ppi->error, AOM_CODEC_INVALID_PARAM,
                           "relative pts + duration is too big");
      }
      aom_codec_pts_t pts_end = ptsvol + (int64_t)duration;
      if (pts_end > INT64_MAX / cpi_data.timestamp_ratio->num) {
        aom_internal_error(
            &ppi->error, AOM_CODEC_INVALID_PARAM,
            "conversion of relative pts + duration to ticks would overflow");
      }
      int64_t src_end_time_stamp =
          timebase_units_to_ticks(cpi_data.timestamp_ratio, pts_end);

      YV12_BUFFER_CONFIG sd;
      res = image2yuvconfig(img, &sd);
      // When generating a monochrome stream, make |sd| a monochrome image.
      if (ctx->cfg.monochrome) {
        sd.u_buffer = sd.v_buffer = NULL;
        sd.uv_stride = 0;
        sd.monochrome = 1;
      }
      int use_highbitdepth = (sd.flags & YV12_FLAG_HIGHBITDEPTH) != 0;
      int subsampling_x = sd.subsampling_x;
      int subsampling_y = sd.subsampling_y;

      if (!ppi->lookahead) {
        int lag_in_frames = cpi_lap != NULL ? cpi_lap->oxcf.gf_cfg.lag_in_frames
                                            : cpi->oxcf.gf_cfg.lag_in_frames;
        AV1EncoderConfig *oxcf = &cpi->oxcf;
        const BLOCK_SIZE sb_size = av1_select_sb_size(
            oxcf, oxcf->frm_dim_cfg.width, oxcf->frm_dim_cfg.height,
            ppi->number_spatial_layers);
        oxcf->border_in_pixels =
            av1_get_enc_border_size(av1_is_resize_needed(oxcf),
                                    oxcf->kf_cfg.key_freq_max == 0, sb_size);
        for (int i = 0; i < ppi->num_fp_contexts; i++) {
          ppi->parallel_cpi[i]->oxcf.border_in_pixels = oxcf->border_in_pixels;
        }

        const int src_border_in_pixels = get_src_border_in_pixels(cpi, sb_size);
        ppi->lookahead = av1_lookahead_init(
            cpi->oxcf.frm_dim_cfg.width, cpi->oxcf.frm_dim_cfg.height,
            subsampling_x, subsampling_y, use_highbitdepth, lag_in_frames,
            src_border_in_pixels, cpi->common.features.byte_alignment,
            ctx->num_lap_buffers, (cpi->oxcf.kf_cfg.key_freq_max == 0),
            cpi->alloc_pyramid);
      }
      if (!ppi->lookahead)
        aom_internal_error(&ppi->error, AOM_CODEC_MEM_ERROR,
                           "Failed to allocate lag buffers");
      for (int i = 0; i < ppi->num_fp_contexts; i++) {
        aom_codec_err_t err =
            av1_check_initial_width(ppi->parallel_cpi[i], use_highbitdepth,
                                    subsampling_x, subsampling_y);
        if (err != AOM_CODEC_OK) {
          aom_internal_error(&ppi->error, err,
                             "av1_check_initial_width() failed");
        }
      }
      if (cpi_lap != NULL) {
        aom_codec_err_t err = av1_check_initial_width(
            cpi_lap, use_highbitdepth, subsampling_x, subsampling_y);
        if (err != AOM_CODEC_OK) {
          aom_internal_error(&ppi->error, err,
                             "av1_check_initial_width() failed");
        }
      }

      // Store the original flags in to the frame buffer. Will extract the
      // key frame flag when we actually encode this frame.
      if (av1_receive_raw_frame(cpi, flags | ctx->next_frame_flags, &sd,
                                src_time_stamp, src_end_time_stamp)) {
        res = update_error_state(ctx, cpi->common.error);
      }
      ctx->next_frame_flags = 0;
    }

    cpi_data.cx_data = ctx->cx_data;
    cpi_data.cx_data_sz = ctx->cx_data_sz;

    /* Any pending invisible frames? */
    if (ctx->pending_cx_data_sz) {
      cpi_data.cx_data += ctx->pending_cx_data_sz;
      cpi_data.cx_data_sz -= ctx->pending_cx_data_sz;

      /* TODO: this is a minimal check, the underlying codec doesn't respect
       * the buffer size anyway.
       */
      if (cpi_data.cx_data_sz < ctx->cx_data_sz / 2) {
        aom_internal_error(&ppi->error, AOM_CODEC_ERROR,
                           "Compressed data buffer too small");
      }
    }

    int is_frame_visible = 0;
    int has_no_show_keyframe = 0;
    int num_workers = 0;

    if (cpi->oxcf.pass == AOM_RC_FIRST_PASS) {
#if !CONFIG_REALTIME_ONLY
      num_workers = ppi->p_mt_info.num_mod_workers[MOD_FP] =
          av1_fp_compute_num_enc_workers(cpi);
#endif
    } else {
      av1_compute_num_workers_for_mt(cpi);
      num_workers = av1_get_max_num_workers(cpi);
    }
    if (num_workers > 1 && ppi->p_mt_info.num_workers < num_workers) {
      // Obtain the maximum no. of frames that can be supported in a parallel
      // encode set.
      if (is_stat_consumption_stage(cpi)) {
        ppi->num_fp_contexts = av1_compute_num_fp_contexts(ppi, &cpi->oxcf);
      }
      if (ppi->p_mt_info.num_workers > 0) {
        av1_terminate_workers(ppi);
        free_thread_data(ppi);
        aom_free(ppi->p_mt_info.tile_thr_data);
        ppi->p_mt_info.tile_thr_data = NULL;
        aom_free(ppi->p_mt_info.workers);
        ppi->p_mt_info.workers = NULL;
        ppi->p_mt_info.num_workers = 0;
        for (int j = 0; j < ppi->num_fp_contexts; j++) {
          aom_free(ppi->parallel_cpi[j]->td.tctx);
          ppi->parallel_cpi[j]->td.tctx = NULL;
        }
      }
      av1_create_workers(ppi, num_workers);
      av1_init_tile_thread_data(ppi, cpi->oxcf.pass == AOM_RC_FIRST_PASS);
    }

    // Re-allocate thread data if workers for encoder multi-threading stage
    // exceeds prev_num_enc_workers.
    const int num_enc_workers =
        av1_get_num_mod_workers_for_alloc(&ppi->p_mt_info, MOD_ENC);
    if (ppi->p_mt_info.prev_num_enc_workers < num_enc_workers &&
        num_enc_workers <= ppi->p_mt_info.num_workers) {
      free_thread_data(ppi);
      for (int j = 0; j < ppi->num_fp_contexts; j++) {
        aom_free(ppi->parallel_cpi[j]->td.tctx);
        ppi->parallel_cpi[j]->td.tctx = NULL;
      }
      av1_init_tile_thread_data(ppi, cpi->oxcf.pass == AOM_RC_FIRST_PASS);
    }

    for (int i = 0; i < ppi->num_fp_contexts; i++) {
      av1_init_frame_mt(ppi, ppi->parallel_cpi[i]);
    }
    if (cpi_lap != NULL) {
      av1_init_frame_mt(ppi, cpi_lap);
    }
#if CONFIG_MULTITHREAD
    if (ppi->p_mt_info.num_workers > 1) {
      for (int i = 0; i < ppi->num_fp_contexts; i++) {
        av1_init_mt_sync(ppi->parallel_cpi[i],
                         ppi->parallel_cpi[i]->oxcf.pass == AOM_RC_FIRST_PASS);
      }
      if (cpi_lap != NULL) {
        av1_init_mt_sync(cpi_lap, 1);
      }
    }
#endif  // CONFIG_MULTITHREAD

    // Call for LAP stage
    if (cpi_lap != NULL) {
      AV1_COMP_DATA cpi_lap_data = { 0 };
      cpi_lap_data.flush = !img;
      cpi_lap_data.timestamp_ratio = &ctx->timestamp_ratio;
      const int status = av1_get_compressed_data(cpi_lap, &cpi_lap_data);
      if (status > AOM_CODEC_OK) {
        aom_internal_error_copy(&ppi->error, cpi_lap->common.error);
      }
      av1_post_encode_updates(cpi_lap, &cpi_lap_data);
    }

    // Recalculate the maximum number of frames that can be encoded in
    // parallel at the beginning of sub gop.
    if (is_stat_consumption_stage(cpi) && ppi->gf_group.size > 0 &&
        cpi->gf_frame_index == ppi->gf_group.size) {
      ppi->num_fp_contexts = av1_compute_num_fp_contexts(ppi, &cpi->oxcf);
    }

    // Get the next visible frame. Invisible frames get packed with the next
    // visible frame.
    while (cpi_data.cx_data_sz >= ctx->cx_data_sz / 2 && !is_frame_visible) {
      int simulate_parallel_frame = 0;
      int status = -1;
      cpi->do_frame_data_update = true;
      cpi->ref_idx_to_skip = INVALID_IDX;
      cpi->ref_refresh_index = INVALID_IDX;
      cpi->refresh_idx_available = false;

#if CONFIG_FPMT_TEST
      simulate_parallel_frame =
          cpi->ppi->fpmt_unit_test_cfg == PARALLEL_SIMULATION_ENCODE ? 1 : 0;
      if (simulate_parallel_frame) {
        if (ppi->num_fp_contexts > 1 && ppi->gf_group.size > 1) {
          if (cpi->gf_frame_index < ppi->gf_group.size) {
            calc_frame_data_update_flag(&ppi->gf_group, cpi->gf_frame_index,
                                        &cpi->do_frame_data_update);
          }
        }
        status = av1_get_compressed_data(cpi, &cpi_data);
      }

#endif  // CONFIG_FPMT_TEST
      if (!simulate_parallel_frame) {
        if (ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] == 0) {
          status = av1_get_compressed_data(cpi, &cpi_data);
        } else if (ppi->gf_group.frame_parallel_level[cpi->gf_frame_index] ==
                   1) {
          // In case of an error, longjmp() would be invoked and hence "status"
          // is set to AOM_CODEC_OK here.
          av1_compress_parallel_frames(ppi, &cpi_data);
          status = AOM_CODEC_OK;
        } else {
          // No possibility of failures from this function and hence "status" is
          // set to AOM_CODEC_OK here.
          cpi = av1_get_parallel_frame_enc_data(ppi, &cpi_data);
          status = AOM_CODEC_OK;
        }
      }
      if (status == -1) break;
      if (status != AOM_CODEC_OK) {
        aom_internal_error_copy(&ppi->error, cpi->common.error);
      }
      if (ppi->num_fp_contexts > 0 && frame_is_intra_only(&cpi->common)) {
        av1_init_sc_decisions(ppi);
      }

      ppi->seq_params_locked = 1;
      av1_post_encode_updates(cpi, &cpi_data);

#if CONFIG_ENTROPY_STATS
      if (ppi->cpi->oxcf.pass != 1 && !cpi->common.show_existing_frame)
        av1_accumulate_frame_counts(&ppi->aggregate_fc, &cpi->counts);
#endif
#if CONFIG_INTERNAL_STATS
      if (ppi->cpi->oxcf.pass != 1) {
        ppi->total_time_compress_data += cpi->time_compress_data;
        ppi->total_recode_hits += cpi->frame_recode_hits;
        ppi->total_bytes += (uint64_t)cpi->bytes;
        for (int i = 0; i < MAX_MODES; i++) {
          ppi->total_mode_chosen_counts[i] += cpi->mode_chosen_counts[i];
        }
      }
#endif  // CONFIG_INTERNAL_STATS

      if (!cpi_data.frame_size) continue;
      assert(cpi_data.cx_data != NULL && cpi_data.cx_data_sz != 0);
      if (cpi_data.frame_size > cpi_data.cx_data_sz) {
        aom_internal_error(&ppi->error, AOM_CODEC_ERROR,
                           "cpi_data.cx_data buffer overflow");
      }
      const int write_temporal_delimiter =
          !cpi->common.spatial_layer_id && !ctx->pending_cx_data_sz;

      if (write_temporal_delimiter) {
        uint32_t obu_header_size = 1;
        const uint32_t obu_payload_size = 0;
        const size_t length_field_size =
            aom_uleb_size_in_bytes(obu_payload_size);

        const size_t move_offset = obu_header_size + length_field_size;
        assert(ctx->cx_data_sz == cpi_data.cx_data_sz);
        if (move_offset > ctx->cx_data_sz - cpi_data.frame_size) {
          aom_internal_error(&ppi->error, AOM_CODEC_ERROR,
                             "ctx->cx_data buffer full");
        }
        memmove(ctx->cx_data + move_offset, ctx->cx_data, cpi_data.frame_size);
        obu_header_size = av1_write_obu_header(
            &ppi->level_params, &cpi->frame_header_count,
            OBU_TEMPORAL_DELIMITER,
            ppi->seq_params.has_nonzero_operating_point_idc,
            /*is_layer_specific_obu=*/false, 0, ctx->cx_data);
        if (obu_header_size != 1) {
          aom_internal_error(&ppi->error, AOM_CODEC_ERROR, NULL);
        }

        // OBUs are preceded/succeeded by an unsigned leb128 coded integer.
        if (av1_write_uleb_obu_size(obu_payload_size,
                                    ctx->cx_data + obu_header_size,
                                    length_field_size) != AOM_CODEC_OK) {
          aom_internal_error(&ppi->error, AOM_CODEC_ERROR, NULL);
        }

        cpi_data.frame_size += move_offset;
      }

      if (ctx->oxcf.save_as_annexb) {
        if (av1_convert_sect5obus_to_annexb(
                cpi_data.cx_data, cpi_data.cx_data_sz, &cpi_data.frame_size) !=
            AOM_CODEC_OK) {
          aom_internal_error(&ppi->error, AOM_CODEC_ERROR, NULL);
        }

        // B_PRIME (add frame size)
        const size_t length_field_size =
            aom_uleb_size_in_bytes(cpi_data.frame_size);
        if (length_field_size > cpi_data.cx_data_sz - cpi_data.frame_size) {
          aom_internal_error(&ppi->error, AOM_CODEC_ERROR,
                             "cpi_data.cx_data buffer full");
        }
        memmove(cpi_data.cx_data + length_field_size, cpi_data.cx_data,
                cpi_data.frame_size);
        if (av1_write_uleb_obu_size(cpi_data.frame_size, cpi_data.cx_data,
                                    length_field_size) != AOM_CODEC_OK) {
          aom_internal_error(&ppi->error, AOM_CODEC_ERROR, NULL);
        }
        cpi_data.frame_size += length_field_size;
      }

      ctx->pending_cx_data_sz += cpi_data.frame_size;

      cpi_data.cx_data += cpi_data.frame_size;
      cpi_data.cx_data_sz -= cpi_data.frame_size;

      is_frame_visible = cpi->common.show_frame;

      has_no_show_keyframe |=
          (!is_frame_visible &&
           cpi->common.current_frame.frame_type == KEY_FRAME);
    }
    if (is_frame_visible) {
      // Add the frame packet to the list of returned packets.
      aom_codec_cx_pkt_t pkt;

      // decrement frames_left counter
      ppi->frames_left = AOMMAX(0, ppi->frames_left - 1);
      if (ctx->oxcf.save_as_annexb) {
        //  B_PRIME (add TU size)
        size_t tu_size = ctx->pending_cx_data_sz;
        const size_t length_field_size = aom_uleb_size_in_bytes(tu_size);
        if (tu_size > ctx->cx_data_sz) {
          aom_internal_error(&ppi->error, AOM_CODEC_ERROR,
                             "ctx->cx_data buffer overflow");
        }
        if (length_field_size > ctx->cx_data_sz - tu_size) {
          aom_internal_error(&ppi->error, AOM_CODEC_ERROR,
                             "ctx->cx_data buffer full");
        }
        memmove(ctx->cx_data + length_field_size, ctx->cx_data, tu_size);
        if (av1_write_uleb_obu_size(tu_size, ctx->cx_data, length_field_size) !=
            AOM_CODEC_OK) {
          aom_internal_error(&ppi->error, AOM_CODEC_ERROR, NULL);
        }
        ctx->pending_cx_data_sz += length_field_size;
      }

      pkt.kind = AOM_CODEC_CX_FRAME_PKT;

      pkt.data.frame.buf = ctx->cx_data;
      pkt.data.frame.sz = ctx->pending_cx_data_sz;
      pkt.data.frame.partition_id = -1;
      pkt.data.frame.vis_frame_size = cpi_data.frame_size;

      pkt.data.frame.pts = ticks_to_timebase_units(cpi_data.timestamp_ratio,
                                                   cpi_data.ts_frame_start) +
                           ctx->pts_offset;
      pkt.data.frame.flags = get_frame_pkt_flags(cpi, cpi_data.lib_flags);
      if (has_no_show_keyframe) {
        // If one of the invisible frames in the packet is a keyframe, set
        // the delayed random access point flag.
        pkt.data.frame.flags |= AOM_FRAME_IS_DELAYED_RANDOM_ACCESS_POINT;
      }
      const int64_t duration64 = ticks_to_timebase_units(
          cpi_data.timestamp_ratio,
          cpi_data.ts_frame_end - cpi_data.ts_frame_start);
      if (duration64 > UINT32_MAX) {
        aom_internal_error(&ppi->error, AOM_CODEC_ERROR, NULL);
      }
      pkt.data.frame.duration = (uint32_t)duration64;

      aom_codec_pkt_list_add(&ctx->pkt_list.head, &pkt);

      ctx->pending_cx_data_sz = 0;
    }
  }

  ppi->error.setjmp = 0;
  return res;
}

static const aom_codec_cx_pkt_t *encoder_get_cxdata(aom_codec_alg_priv_t *ctx,
                                                    aom_codec_iter_t *iter) {
  return aom_codec_pkt_list_get(&ctx->pkt_list.head, iter);
}

static aom_codec_err_t ctrl_set_reference(aom_codec_alg_priv_t *ctx,
                                          va_list args) {
  av1_ref_frame_t *const frame = va_arg(args, av1_ref_frame_t *);

  if (frame != NULL) {
    YV12_BUFFER_CONFIG sd;

    image2yuvconfig(&frame->img, &sd);
    av1_set_reference_enc(ctx->ppi->cpi, frame->idx, &sd);
    return AOM_CODEC_OK;
  } else {
    return AOM_CODEC_INVALID_PARAM;
  }
}

static aom_codec_err_t ctrl_copy_reference(aom_codec_alg_priv_t *ctx,
                                           va_list args) {
  if (ctx->ppi->cpi->oxcf.algo_cfg.skip_postproc_filtering)
    return AOM_CODEC_INCAPABLE;
  av1_ref_frame_t *const frame = va_arg(args, av1_ref_frame_t *);

  if (frame != NULL) {
    YV12_BUFFER_CONFIG sd;

    image2yuvconfig(&frame->img, &sd);
    av1_copy_reference_enc(ctx->ppi->cpi, frame->idx, &sd);
    return AOM_CODEC_OK;
  } else {
    return AOM_CODEC_INVALID_PARAM;
  }
}

static aom_codec_err_t ctrl_get_reference(aom_codec_alg_priv_t *ctx,
                                          va_list args) {
  if (ctx->ppi->cpi->oxcf.algo_cfg.skip_postproc_filtering)
    return AOM_CODEC_INCAPABLE;
  av1_ref_frame_t *const frame = va_arg(args, av1_ref_frame_t *);

  if (frame != NULL) {
    YV12_BUFFER_CONFIG *fb = get_ref_frame(&ctx->ppi->cpi->common, frame->idx);
    if (fb == NULL) return AOM_CODEC_ERROR;

    yuvconfig2image(&frame->img, fb, NULL);
    return AOM_CODEC_OK;
  } else {
    return AOM_CODEC_INVALID_PARAM;
  }
}

static aom_codec_err_t ctrl_get_new_frame_image(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  aom_image_t *const new_img = va_arg(args, aom_image_t *);

  if (new_img != NULL) {
    YV12_BUFFER_CONFIG new_frame;

    if (av1_get_last_show_frame(ctx->ppi->cpi, &new_frame) == 0) {
      yuvconfig2image(new_img, &new_frame, NULL);
      return AOM_CODEC_OK;
    } else {
      return AOM_CODEC_ERROR;
    }
  } else {
    return AOM_CODEC_INVALID_PARAM;
  }
}

static aom_codec_err_t ctrl_copy_new_frame_image(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  aom_image_t *const new_img = va_arg(args, aom_image_t *);

  if (new_img != NULL) {
    YV12_BUFFER_CONFIG new_frame;

    if (av1_get_last_show_frame(ctx->ppi->cpi, &new_frame) == 0) {
      YV12_BUFFER_CONFIG sd;
      image2yuvconfig(new_img, &sd);
      return av1_copy_new_frame_enc(&ctx->ppi->cpi->common, &new_frame, &sd);
    } else {
      return AOM_CODEC_ERROR;
    }
  } else {
    return AOM_CODEC_INVALID_PARAM;
  }
}

static aom_image_t *encoder_get_preview(aom_codec_alg_priv_t *ctx) {
  YV12_BUFFER_CONFIG sd;

  if (av1_get_preview_raw_frame(ctx->ppi->cpi, &sd) == 0) {
    yuvconfig2image(&ctx->preview_img, &sd, NULL);
    return &ctx->preview_img;
  } else {
    return NULL;
  }
}

static aom_codec_err_t ctrl_use_reference(aom_codec_alg_priv_t *ctx,
                                          va_list args) {
  const int reference_flag = va_arg(args, int);

  av1_use_as_reference(&ctx->ppi->cpi->ext_flags.ref_frame_flags,
                       reference_flag);
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_set_roi_map(aom_codec_alg_priv_t *ctx,
                                        va_list args) {
  (void)ctx;
  (void)args;

  // TODO(yaowu): Need to re-implement and test for AV1.
  return AOM_CODEC_INVALID_PARAM;
}

static aom_codec_err_t ctrl_set_active_map(aom_codec_alg_priv_t *ctx,
                                           va_list args) {
  aom_active_map_t *const map = va_arg(args, aom_active_map_t *);

  if (map) {
    if (!av1_set_active_map(ctx->ppi->cpi, map->active_map, (int)map->rows,
                            (int)map->cols))
      return AOM_CODEC_OK;
    else
      return AOM_CODEC_INVALID_PARAM;
  } else {
    return AOM_CODEC_INVALID_PARAM;
  }
}

static aom_codec_err_t ctrl_get_active_map(aom_codec_alg_priv_t *ctx,
                                           va_list args) {
  aom_active_map_t *const map = va_arg(args, aom_active_map_t *);

  if (map) {
    if (!av1_get_active_map(ctx->ppi->cpi, map->active_map, (int)map->rows,
                            (int)map->cols))
      return AOM_CODEC_OK;
    else
      return AOM_CODEC_INVALID_PARAM;
  } else {
    return AOM_CODEC_INVALID_PARAM;
  }
}

static aom_codec_err_t ctrl_set_scale_mode(aom_codec_alg_priv_t *ctx,
                                           va_list args) {
  aom_scaling_mode_t *const mode = va_arg(args, aom_scaling_mode_t *);

  if (mode) {
    AV1EncoderConfig *const oxcf =
        ctx->ppi->seq_params_locked ? &ctx->ppi->cpi->oxcf : &ctx->oxcf;
    const int res =
        av1_set_internal_size(oxcf, &ctx->ppi->cpi->resize_pending_params,
                              mode->h_scaling_mode, mode->v_scaling_mode);
    if (res == 0) {
      // update_encoder_cfg() is somewhat costly and this control may be called
      // multiple times, so update_encoder_cfg() is only called to ensure frame
      // and superblock sizes are updated before they're fixed by the first
      // encode call.
      if (ctx->ppi->seq_params_locked) {
        av1_check_fpmt_config(ctx->ppi, &ctx->ppi->cpi->oxcf);
        return AOM_CODEC_OK;
      }
      return update_encoder_cfg(ctx);
    }
    return AOM_CODEC_INVALID_PARAM;
  } else {
    return AOM_CODEC_INVALID_PARAM;
  }
}

static aom_codec_err_t ctrl_set_spatial_layer_id(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  const int spatial_layer_id = va_arg(args, int);
  if (spatial_layer_id >= MAX_NUM_SPATIAL_LAYERS)
    return AOM_CODEC_INVALID_PARAM;
  ctx->ppi->cpi->common.spatial_layer_id = spatial_layer_id;
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_set_number_spatial_layers(aom_codec_alg_priv_t *ctx,
                                                      va_list args) {
  const int number_spatial_layers = va_arg(args, int);
  if (number_spatial_layers > MAX_NUM_SPATIAL_LAYERS)
    return AOM_CODEC_INVALID_PARAM;
  ctx->ppi->number_spatial_layers = number_spatial_layers;
  // update_encoder_cfg() is somewhat costly and this control may be called
  // multiple times, so update_encoder_cfg() is only called to ensure frame and
  // superblock sizes are updated before they're fixed by the first encode
  // call.
  if (!ctx->ppi->seq_params_locked) {
    return update_encoder_cfg(ctx);
  }
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_set_layer_id(aom_codec_alg_priv_t *ctx,
                                         va_list args) {
  aom_svc_layer_id_t *const data = va_arg(args, aom_svc_layer_id_t *);
  ctx->ppi->cpi->common.spatial_layer_id = data->spatial_layer_id;
  ctx->ppi->cpi->common.temporal_layer_id = data->temporal_layer_id;
  ctx->ppi->cpi->svc.spatial_layer_id = data->spatial_layer_id;
  ctx->ppi->cpi->svc.temporal_layer_id = data->temporal_layer_id;
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_set_svc_params(aom_codec_alg_priv_t *ctx,
                                           va_list args) {
  AV1_PRIMARY *const ppi = ctx->ppi;
  AV1_COMP *const cpi = ppi->cpi;
  aom_svc_params_t *const params = va_arg(args, aom_svc_params_t *);
  int64_t target_bandwidth = 0;
  ppi->number_spatial_layers = params->number_spatial_layers;
  ppi->number_temporal_layers = params->number_temporal_layers;
  cpi->svc.number_spatial_layers = params->number_spatial_layers;
  cpi->svc.number_temporal_layers = params->number_temporal_layers;
  if (ppi->number_spatial_layers > 1 || ppi->number_temporal_layers > 1) {
    unsigned int sl, tl;
    ctx->ppi->use_svc = 1;
    const int num_layers =
        ppi->number_spatial_layers * ppi->number_temporal_layers;
    for (int layer = 0; layer < num_layers; ++layer) {
      if (params->max_quantizers[layer] > 63 ||
          params->min_quantizers[layer] < 0 ||
          params->min_quantizers[layer] > params->max_quantizers[layer]) {
        return AOM_CODEC_INVALID_PARAM;
      }
    }
    if (!av1_alloc_layer_context(cpi, num_layers)) return AOM_CODEC_MEM_ERROR;

    for (sl = 0; sl < ppi->number_spatial_layers; ++sl) {
      for (tl = 0; tl < ppi->number_temporal_layers; ++tl) {
        const int layer = LAYER_IDS_TO_IDX(sl, tl, ppi->number_temporal_layers);
        LAYER_CONTEXT *lc = &cpi->svc.layer_context[layer];
        lc->max_q = params->max_quantizers[layer];
        lc->min_q = params->min_quantizers[layer];
        lc->scaling_factor_num = AOMMAX(1, params->scaling_factor_num[sl]);
        lc->scaling_factor_den = AOMMAX(1, params->scaling_factor_den[sl]);
        const int layer_target_bitrate = params->layer_target_bitrate[layer];
        if (layer_target_bitrate > INT_MAX / 1000) {
          lc->layer_target_bitrate = INT_MAX;
        } else {
          lc->layer_target_bitrate = 1000 * layer_target_bitrate;
        }
        lc->framerate_factor = params->framerate_factor[tl];
        if (tl == ppi->number_temporal_layers - 1)
          target_bandwidth += lc->layer_target_bitrate;
      }
    }

    if (ppi->seq_params_locked) {
      AV1EncoderConfig *const oxcf = &cpi->oxcf;
      // Keep ctx->oxcf in sync in case further codec controls are made prior
      // to encoding.
      ctx->oxcf.rc_cfg.target_bandwidth = oxcf->rc_cfg.target_bandwidth =
          target_bandwidth;
      set_primary_rc_buffer_sizes(oxcf, ppi);
      av1_update_layer_context_change_config(cpi, target_bandwidth);
      check_reset_rc_flag(cpi);
    } else {
      // Note av1_init_layer_context() relies on cpi->oxcf. The order of that
      // call and the ones in the other half of this block (which
      // update_encoder_cfg() transitively makes) is important. So we keep
      // ctx->oxcf and cpi->oxcf in sync here as update_encoder_cfg() will
      // overwrite cpi->oxcf with ctx->oxcf.
      ctx->oxcf.rc_cfg.target_bandwidth = cpi->oxcf.rc_cfg.target_bandwidth =
          target_bandwidth;
      SequenceHeader *const seq_params = &ppi->seq_params;
      seq_params->operating_points_cnt_minus_1 =
          ppi->number_spatial_layers * ppi->number_temporal_layers - 1;

      av1_init_layer_context(cpi);
      // update_encoder_cfg() is somewhat costly and this control may be called
      // multiple times, so update_encoder_cfg() is only called to ensure frame
      // and superblock sizes are updated before they're fixed by the first
      // encode call.
      return update_encoder_cfg(ctx);
    }
  } else if (!ppi->seq_params_locked) {
    // Ensure frame and superblock sizes are updated.
    return update_encoder_cfg(ctx);
  }
  av1_check_fpmt_config(ctx->ppi, &ctx->ppi->cpi->oxcf);
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_set_svc_ref_frame_config(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  AV1_COMP *const cpi = ctx->ppi->cpi;
  aom_svc_ref_frame_config_t *const data =
      va_arg(args, aom_svc_ref_frame_config_t *);
  cpi->ppi->rtc_ref.set_ref_frame_config = 1;
  for (unsigned int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    if (data->reference[i] != 0 && data->reference[i] != 1)
      return AOM_CODEC_INVALID_PARAM;
    if (data->ref_idx[i] > 7 || data->ref_idx[i] < 0)
      return AOM_CODEC_INVALID_PARAM;
    cpi->ppi->rtc_ref.reference[i] = data->reference[i];
    cpi->ppi->rtc_ref.ref_idx[i] = data->ref_idx[i];
  }
  for (unsigned int i = 0; i < REF_FRAMES; ++i) {
    if (data->refresh[i] != 0 && data->refresh[i] != 1)
      return AOM_CODEC_INVALID_PARAM;
    cpi->ppi->rtc_ref.refresh[i] = data->refresh[i];
  }
  cpi->svc.use_flexible_mode = 1;
  cpi->svc.ksvc_fixed_mode = 0;
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_set_svc_ref_frame_comp_pred(
    aom_codec_alg_priv_t *ctx, va_list args) {
  AV1_COMP *const cpi = ctx->ppi->cpi;
  aom_svc_ref_frame_comp_pred_t *const data =
      va_arg(args, aom_svc_ref_frame_comp_pred_t *);
  cpi->ppi->rtc_ref.ref_frame_comp[0] = data->use_comp_pred[0];
  cpi->ppi->rtc_ref.ref_frame_comp[1] = data->use_comp_pred[1];
  cpi->ppi->rtc_ref.ref_frame_comp[2] = data->use_comp_pred[2];
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_set_tune_content(aom_codec_alg_priv_t *ctx,
                                             va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.content = CAST(AV1E_SET_TUNE_CONTENT, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_cdf_update_mode(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.cdf_update_mode = CAST(AV1E_SET_CDF_UPDATE_MODE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_color_primaries(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.color_primaries = CAST(AV1E_SET_COLOR_PRIMARIES, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_transfer_characteristics(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.transfer_characteristics =
      CAST(AV1E_SET_TRANSFER_CHARACTERISTICS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_matrix_coefficients(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.matrix_coefficients = CAST(AV1E_SET_MATRIX_COEFFICIENTS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_chroma_sample_position(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.chroma_sample_position =
      CAST(AV1E_SET_CHROMA_SAMPLE_POSITION, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_color_range(aom_codec_alg_priv_t *ctx,
                                            va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.color_range = CAST(AV1E_SET_COLOR_RANGE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_render_size(aom_codec_alg_priv_t *ctx,
                                            va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  int *const render_size = va_arg(args, int *);
  extra_cfg.render_width = render_size[0];
  extra_cfg.render_height = render_size[1];
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_superblock_size(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.superblock_size = CAST(AV1E_SET_SUPERBLOCK_SIZE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_chroma_subsampling_x(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.chroma_subsampling_x = CAST(AV1E_SET_CHROMA_SUBSAMPLING_X, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_chroma_subsampling_y(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.chroma_subsampling_y = CAST(AV1E_SET_CHROMA_SUBSAMPLING_Y, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t encoder_set_option(aom_codec_alg_priv_t *ctx,
                                          const char *name, const char *value) {
  if (ctx == NULL || name == NULL || value == NULL)
    return AOM_CODEC_INVALID_PARAM;
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  // Used to mock the argv with just one string "--{name}={value}"
  char *argv[2] = { NULL, "" };
  size_t len = strlen(name) + strlen(value) + 4;
  char *const err_string = ctx->ppi->error.detail;

#if __STDC_VERSION__ >= 201112L
  // We use the keyword _Static_assert because clang-cl does not allow the
  // convenience macro static_assert to be used in function scope. See
  // https://bugs.llvm.org/show_bug.cgi?id=48904.
  _Static_assert(sizeof(ctx->ppi->error.detail) >= ARG_ERR_MSG_MAX_LEN,
                 "The size of the err_msg buffer for arg_match_helper must be "
                 "at least ARG_ERR_MSG_MAX_LEN");
#else
  assert(sizeof(ctx->ppi->error.detail) >= ARG_ERR_MSG_MAX_LEN);
#endif

  argv[0] = aom_malloc(len * sizeof(argv[1][0]));
  if (!argv[0]) return AOM_CODEC_MEM_ERROR;
  snprintf(argv[0], len, "--%s=%s", name, value);
  struct arg arg;
  aom_codec_err_t err = AOM_CODEC_OK;

  int match = 1;
  if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_keyframe_filtering,
                       argv, err_string)) {
    extra_cfg.enable_keyframe_filtering =
        arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.min_gf_interval, argv,
                              err_string)) {
    extra_cfg.min_gf_interval = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.max_gf_interval, argv,
                              err_string)) {
    extra_cfg.max_gf_interval = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.gf_min_pyr_height,
                              argv, err_string)) {
    extra_cfg.gf_min_pyr_height = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.gf_max_pyr_height,
                              argv, err_string)) {
    extra_cfg.gf_max_pyr_height = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.cpu_used_av1, argv,
                              err_string)) {
    extra_cfg.cpu_used = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.auto_altref, argv,
                              err_string)) {
    extra_cfg.enable_auto_alt_ref = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.noise_sens, argv,
                              err_string)) {
    extra_cfg.noise_sensitivity = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.sharpness, argv,
                              err_string)) {
    extra_cfg.sharpness = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.static_thresh, argv,
                              err_string)) {
    extra_cfg.static_thresh = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.rowmtarg, argv,
                              err_string)) {
    extra_cfg.row_mt = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.fpmtarg, argv,
                              err_string)) {
    extra_cfg.fp_mt = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.tile_cols, argv,
                              err_string)) {
    extra_cfg.tile_columns = arg_parse_uint_helper(&arg, err_string);
    if (extra_cfg.auto_tiles) {
      snprintf(err_string, ARG_ERR_MSG_MAX_LEN,
               "Cannot set tile-cols because auto-tiles is already set.");
      err = AOM_CODEC_INVALID_PARAM;
    }
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.tile_rows, argv,
                              err_string)) {
    extra_cfg.tile_rows = arg_parse_uint_helper(&arg, err_string);
    if (extra_cfg.auto_tiles) {
      snprintf(err_string, ARG_ERR_MSG_MAX_LEN,
               "Cannot set tile-rows because auto-tiles is already set.");
      err = AOM_CODEC_INVALID_PARAM;
    }
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.auto_tiles, argv,
                              err_string)) {
    extra_cfg.auto_tiles = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_tpl_model,
                              argv, err_string)) {
    extra_cfg.enable_tpl_model = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.arnr_maxframes, argv,
                              err_string)) {
    extra_cfg.arnr_max_frames = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.arnr_strength, argv,
                              err_string)) {
    extra_cfg.arnr_strength = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.tune_metric, argv,
                              err_string)) {
    extra_cfg.tuning = arg_parse_enum_helper(&arg, err_string);
    err = handle_tuning(ctx, &extra_cfg);
  }
#if CONFIG_TUNE_VMAF
  else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.vmaf_model_path, argv,
                            err_string)) {
    err = allocate_and_set_string(value, default_extra_cfg.vmaf_model_path,
                                  &extra_cfg.vmaf_model_path, err_string);
  }
#endif
  else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.partition_info_path,
                            argv, err_string)) {
    err = allocate_and_set_string(value, default_extra_cfg.partition_info_path,
                                  &extra_cfg.partition_info_path, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_rate_guide_deltaq,
                              argv, err_string)) {
    extra_cfg.enable_rate_guide_deltaq =
        arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.rate_distribution_info,
                              argv, err_string)) {
    err =
        allocate_and_set_string(value, default_extra_cfg.rate_distribution_info,
                                &extra_cfg.rate_distribution_info, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.dist_metric, argv,
                              err_string)) {
    extra_cfg.dist_metric = arg_parse_enum_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.cq_level, argv,
                              err_string)) {
    extra_cfg.cq_level = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.max_intra_rate_pct,
                              argv, err_string)) {
    extra_cfg.rc_max_intra_bitrate_pct =
        arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.max_inter_rate_pct,
                              argv, err_string)) {
    extra_cfg.rc_max_inter_bitrate_pct =
        arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.gf_cbr_boost_pct,
                              argv, err_string)) {
    extra_cfg.gf_cbr_boost_pct = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.lossless, argv,
                              err_string)) {
    extra_cfg.lossless = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_cdef, argv,
                              err_string)) {
    extra_cfg.enable_cdef = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_restoration,
                              argv, err_string)) {
    extra_cfg.enable_restoration = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.force_video_mode,
                              argv, err_string)) {
    extra_cfg.force_video_mode = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_obmc, argv,
                              err_string)) {
    extra_cfg.enable_obmc = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.disable_trellis_quant,
                              argv, err_string)) {
    extra_cfg.disable_trellis_quant = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_qm, argv,
                              err_string)) {
    extra_cfg.enable_qm = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.qm_max, argv,
                              err_string)) {
    extra_cfg.qm_max = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.qm_min, argv,
                              err_string)) {
    extra_cfg.qm_min = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.num_tg, argv,
                              err_string)) {
    extra_cfg.num_tg = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.mtu_size, argv,
                              err_string)) {
    extra_cfg.mtu_size = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.timing_info, argv,
                              err_string)) {
    extra_cfg.timing_info_type = arg_parse_enum_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.frame_parallel_decoding,
                              argv, err_string)) {
    extra_cfg.frame_parallel_decoding_mode =
        arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_dual_filter,
                              argv, err_string)) {
    extra_cfg.enable_dual_filter = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_chroma_deltaq,
                              argv, err_string)) {
    extra_cfg.enable_chroma_deltaq = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.aq_mode, argv,
                              err_string)) {
    extra_cfg.aq_mode = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.deltaq_mode, argv,
                              err_string)) {
    extra_cfg.deltaq_mode = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.deltaq_strength, argv,
                              err_string)) {
    extra_cfg.deltaq_strength = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.deltalf_mode, argv,
                              err_string)) {
    extra_cfg.deltalf_mode = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.frame_periodic_boost,
                              argv, err_string)) {
    extra_cfg.frame_periodic_boost = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.tune_content, argv,
                              err_string)) {
    extra_cfg.content = arg_parse_enum_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.input_color_primaries,
                              argv, err_string)) {
    extra_cfg.color_primaries = arg_parse_enum_helper(&arg, err_string);
  } else if (arg_match_helper(
                 &arg, &g_av1_codec_arg_defs.input_transfer_characteristics,
                 argv, err_string)) {
    extra_cfg.transfer_characteristics =
        arg_parse_enum_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.input_matrix_coefficients,
                              argv, err_string)) {
    extra_cfg.matrix_coefficients = arg_parse_enum_helper(&arg, err_string);
  } else if (arg_match_helper(
                 &arg, &g_av1_codec_arg_defs.input_chroma_sample_position, argv,
                 err_string)) {
    extra_cfg.chroma_sample_position = arg_parse_enum_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.superblock_size, argv,
                              err_string)) {
    extra_cfg.superblock_size = arg_parse_enum_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.error_resilient_mode,
                              argv, err_string)) {
    extra_cfg.error_resilient_mode = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.sframe_mode, argv,
                              err_string)) {
    extra_cfg.s_frame_mode = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.film_grain_test, argv,
                              err_string)) {
    extra_cfg.film_grain_test_vector = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.film_grain_table,
                              argv, err_string)) {
    if (value == NULL) {
      // this parameter allows NULL as its value
      extra_cfg.film_grain_table_filename = value;
    } else {
      err = allocate_and_set_string(
          value, default_extra_cfg.film_grain_table_filename,
          &extra_cfg.film_grain_table_filename, err_string);
    }
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.cdf_update_mode, argv,
                              err_string)) {
    extra_cfg.cdf_update_mode = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_rect_partitions,
                              argv, err_string)) {
    extra_cfg.enable_rect_partitions = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_ab_partitions,
                              argv, err_string)) {
    extra_cfg.enable_ab_partitions = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_1to4_partitions,
                              argv, err_string)) {
    extra_cfg.enable_1to4_partitions = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.min_partition_size,
                              argv, err_string)) {
    extra_cfg.min_partition_size = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.max_partition_size,
                              argv, err_string)) {
    extra_cfg.max_partition_size = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_intra_edge_filter,
                              argv, err_string)) {
    extra_cfg.enable_intra_edge_filter =
        arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_order_hint,
                              argv, err_string)) {
    extra_cfg.enable_order_hint = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_tx64, argv,
                              err_string)) {
    extra_cfg.enable_tx64 = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_flip_idtx,
                              argv, err_string)) {
    extra_cfg.enable_flip_idtx = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_rect_tx, argv,
                              err_string)) {
    extra_cfg.enable_rect_tx = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_dist_wtd_comp,
                              argv, err_string)) {
    extra_cfg.enable_dist_wtd_comp = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.max_reference_frames,
                              argv, err_string)) {
    extra_cfg.max_reference_frames = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.reduced_reference_set,
                              argv, err_string)) {
    extra_cfg.enable_reduced_reference_set =
        arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_ref_frame_mvs,
                              argv, err_string)) {
    extra_cfg.enable_ref_frame_mvs = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_masked_comp,
                              argv, err_string)) {
    extra_cfg.enable_masked_comp = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_onesided_comp,
                              argv, err_string)) {
    extra_cfg.enable_onesided_comp = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_interintra_comp,
                              argv, err_string)) {
    extra_cfg.enable_interintra_comp = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_smooth_interintra,
                              argv, err_string)) {
    extra_cfg.enable_smooth_interintra = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_diff_wtd_comp,
                              argv, err_string)) {
    extra_cfg.enable_diff_wtd_comp = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_interinter_wedge,
                              argv, err_string)) {
    extra_cfg.enable_interinter_wedge = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_interintra_wedge,
                              argv, err_string)) {
    extra_cfg.enable_interintra_wedge = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_global_motion,
                              argv, err_string)) {
    extra_cfg.enable_global_motion = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_warped_motion,
                              argv, err_string)) {
    extra_cfg.enable_warped_motion = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_filter_intra,
                              argv, err_string)) {
    extra_cfg.enable_filter_intra = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_smooth_intra,
                              argv, err_string)) {
    extra_cfg.enable_smooth_intra = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_paeth_intra,
                              argv, err_string)) {
    extra_cfg.enable_paeth_intra = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_cfl_intra,
                              argv, err_string)) {
    extra_cfg.enable_cfl_intra = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_directional_intra,
                              argv, err_string)) {
    extra_cfg.enable_directional_intra = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_diagonal_intra,
                              argv, err_string)) {
    extra_cfg.enable_diagonal_intra = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_overlay, argv,
                              err_string)) {
    extra_cfg.enable_overlay = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_palette, argv,
                              err_string)) {
    extra_cfg.enable_palette = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_intrabc, argv,
                              err_string)) {
    extra_cfg.enable_intrabc = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_angle_delta,
                              argv, err_string)) {
    extra_cfg.enable_angle_delta = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(
                 &arg, &g_av1_codec_arg_defs.enable_low_complexity_decode, argv,
                 err_string)) {
    extra_cfg.enable_low_complexity_decode =
        arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.reduced_tx_type_set,
                              argv, err_string)) {
    extra_cfg.reduced_tx_type_set = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.use_intra_dct_only,
                              argv, err_string)) {
    extra_cfg.use_intra_dct_only = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.use_inter_dct_only,
                              argv, err_string)) {
    extra_cfg.use_inter_dct_only = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.use_intra_default_tx_only,
                              argv, err_string)) {
    extra_cfg.use_intra_default_tx_only =
        arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.quant_b_adapt, argv,
                              err_string)) {
    extra_cfg.quant_b_adapt = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.vbr_corpus_complexity_lap,
                              argv, err_string)) {
    extra_cfg.vbr_corpus_complexity_lap =
        arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.set_tier_mask, argv,
                              err_string)) {
    extra_cfg.tier_mask = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.set_min_cr, argv,
                              err_string)) {
    extra_cfg.min_cr = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.coeff_cost_upd_freq,
                              argv, err_string)) {
    extra_cfg.coeff_cost_upd_freq = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.mode_cost_upd_freq,
                              argv, err_string)) {
    extra_cfg.mode_cost_upd_freq = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.mv_cost_upd_freq,
                              argv, err_string)) {
    extra_cfg.mv_cost_upd_freq = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.dv_cost_upd_freq,
                              argv, err_string)) {
    extra_cfg.dv_cost_upd_freq = arg_parse_uint_helper(&arg, err_string);
  }
#if CONFIG_DENOISE
  else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.denoise_noise_level,
                            argv, err_string)) {
    extra_cfg.noise_level =
        (float)arg_parse_int_helper(&arg, err_string) / 10.0f;
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.denoise_block_size,
                              argv, err_string)) {
    extra_cfg.noise_block_size = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_dnl_denoising,
                              argv, err_string)) {
    extra_cfg.enable_dnl_denoising = arg_parse_uint_helper(&arg, err_string);
  }
#endif
  else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.target_seq_level_idx,
                            argv, err_string)) {
    const int val = arg_parse_int_helper(&arg, err_string);
    const int level = val % 100;
    const int operating_point_idx = val / 100;
    if (operating_point_idx < 0 ||
        operating_point_idx >= MAX_NUM_OPERATING_POINTS) {
      snprintf(err_string, ARG_ERR_MSG_MAX_LEN,
               "Invalid operating point index: %d", operating_point_idx);
      err = AOM_CODEC_INVALID_PARAM;
    } else {
      extra_cfg.target_seq_level_idx[operating_point_idx] = (AV1_LEVEL)level;
    }
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.input_chroma_subsampling_x,
                              argv, err_string)) {
    extra_cfg.chroma_subsampling_x = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.input_chroma_subsampling_y,
                              argv, err_string)) {
    extra_cfg.chroma_subsampling_y = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.passes, argv,
                              err_string)) {
    extra_cfg.passes = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.fwd_kf_dist, argv,
                              err_string)) {
    extra_cfg.fwd_kf_dist = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.two_pass_output, argv,
                              err_string)) {
    err = allocate_and_set_string(value, default_extra_cfg.two_pass_output,
                                  &extra_cfg.two_pass_output, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.second_pass_log, argv,
                              err_string)) {
    err = allocate_and_set_string(value, default_extra_cfg.second_pass_log,
                                  &extra_cfg.second_pass_log, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.loopfilter_control,
                              argv, err_string)) {
    extra_cfg.loopfilter_control = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.auto_intra_tools_off,
                              argv, err_string)) {
    extra_cfg.auto_intra_tools_off = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.strict_level_conformance,
                              argv, err_string)) {
    extra_cfg.strict_level_conformance = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.sb_qp_sweep, argv,
                              err_string)) {
    extra_cfg.sb_qp_sweep = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.kf_max_pyr_height,
                              argv, err_string)) {
    extra_cfg.kf_max_pyr_height = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.tile_width, argv,
                              err_string)) {
    ctx->cfg.tile_width_count = arg_parse_list_helper(
        &arg, ctx->cfg.tile_widths, MAX_TILE_WIDTHS, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.tile_height, argv,
                              err_string)) {
    ctx->cfg.tile_height_count = arg_parse_list_helper(
        &arg, ctx->cfg.tile_heights, MAX_TILE_HEIGHTS, err_string);
  } else {
    match = 0;
    snprintf(err_string, ARG_ERR_MSG_MAX_LEN, "Cannot find aom option %s",
             name);
  }
  aom_free(argv[0]);

  if (err != AOM_CODEC_OK) {
    ctx->base.err_detail = err_string;
    return err;
  }

  if (strlen(err_string) != 0) {
    ctx->base.err_detail = err_string;
    return AOM_CODEC_INVALID_PARAM;
  }

  ctx->base.err_detail = NULL;

  if (!match) {
    return AOM_CODEC_INVALID_PARAM;
  }
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_get_seq_level_idx(aom_codec_alg_priv_t *ctx,
                                              va_list args) {
  int *const arg = va_arg(args, int *);
  if (arg == NULL) return AOM_CODEC_INVALID_PARAM;
  return av1_get_seq_level_idx(&ctx->ppi->seq_params, &ctx->ppi->level_params,
                               arg);
}

static aom_codec_err_t ctrl_get_target_seq_level_idx(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  int *const arg = va_arg(args, int *);
  if (arg == NULL) return AOM_CODEC_INVALID_PARAM;
  return av1_get_target_seq_level_idx(&ctx->ppi->seq_params,
                                      &ctx->ppi->level_params, arg);
}

static aom_codec_err_t ctrl_get_num_operating_points(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  int *const arg = va_arg(args, int *);
  if (arg == NULL) return AOM_CODEC_INVALID_PARAM;
  *arg = ctx->ppi->seq_params.operating_points_cnt_minus_1 + 1;
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_get_luma_cdef_strength(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  int *arg = va_arg(args, int *);
  AV1_COMMON const *cm = &ctx->ppi->cpi->common;
  if (arg == NULL) return AOM_CODEC_INVALID_PARAM;
  memcpy(arg, cm->cdef_info.cdef_strengths, CDEF_MAX_STRENGTHS * sizeof(*arg));

  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_get_high_motion_content_screen_rtc(
    aom_codec_alg_priv_t *ctx, va_list args) {
  int *arg = va_arg(args, int *);
  AV1_COMP *const cpi = ctx->ppi->cpi;
  if (arg == NULL) return AOM_CODEC_INVALID_PARAM;
  *arg = cpi->rc.high_motion_content_screen_rtc;
  return AOM_CODEC_OK;
}

static aom_codec_ctrl_fn_map_t encoder_ctrl_maps[] = {
  { AV1_COPY_REFERENCE, ctrl_copy_reference },
  { AOME_USE_REFERENCE, ctrl_use_reference },

  // Setters
  { AV1_SET_REFERENCE, ctrl_set_reference },
  { AOME_SET_ROI_MAP, ctrl_set_roi_map },
  { AOME_SET_ACTIVEMAP, ctrl_set_active_map },
  { AOME_SET_SCALEMODE, ctrl_set_scale_mode },
  { AOME_SET_SPATIAL_LAYER_ID, ctrl_set_spatial_layer_id },
  { AOME_SET_CPUUSED, ctrl_set_cpuused },
  { AOME_SET_ENABLEAUTOALTREF, ctrl_set_enable_auto_alt_ref },
  { AOME_SET_ENABLEAUTOBWDREF, ctrl_set_enable_auto_bwd_ref },
  { AOME_SET_SHARPNESS, ctrl_set_sharpness },
  { AOME_SET_STATIC_THRESHOLD, ctrl_set_static_thresh },
  { AV1E_SET_ROW_MT, ctrl_set_row_mt },
  { AV1E_SET_FP_MT, ctrl_set_fp_mt },
  { AV1E_SET_TILE_COLUMNS, ctrl_set_tile_columns },
  { AV1E_SET_TILE_ROWS, ctrl_set_tile_rows },
  { AV1E_SET_ENABLE_TPL_MODEL, ctrl_set_enable_tpl_model },
  { AV1E_SET_ENABLE_KEYFRAME_FILTERING, ctrl_set_enable_keyframe_filtering },
  { AOME_SET_ARNR_MAXFRAMES, ctrl_set_arnr_max_frames },
  { AOME_SET_ARNR_STRENGTH, ctrl_set_arnr_strength },
  { AOME_SET_TUNING, ctrl_set_tuning },
  { AOME_SET_CQ_LEVEL, ctrl_set_cq_level },
  { AOME_SET_MAX_INTRA_BITRATE_PCT, ctrl_set_rc_max_intra_bitrate_pct },
  { AOME_SET_NUMBER_SPATIAL_LAYERS, ctrl_set_number_spatial_layers },
  { AV1E_SET_MAX_INTER_BITRATE_PCT, ctrl_set_rc_max_inter_bitrate_pct },
  { AV1E_SET_GF_CBR_BOOST_PCT, ctrl_set_rc_gf_cbr_boost_pct },
  { AV1E_SET_LOSSLESS, ctrl_set_lossless },
  { AV1E_SET_ENABLE_CDEF, ctrl_set_enable_cdef },
  { AV1E_SET_ENABLE_RESTORATION, ctrl_set_enable_restoration },
  { AV1E_SET_FORCE_VIDEO_MODE, ctrl_set_force_video_mode },
  { AV1E_SET_ENABLE_OBMC, ctrl_set_enable_obmc },
  { AV1E_SET_DISABLE_TRELLIS_QUANT, ctrl_set_disable_trellis_quant },
  { AV1E_SET_ENABLE_QM, ctrl_set_enable_qm },
  { AV1E_SET_QM_Y, ctrl_set_qm_y },
  { AV1E_SET_QM_U, ctrl_set_qm_u },
  { AV1E_SET_QM_V, ctrl_set_qm_v },
  { AV1E_SET_QM_MIN, ctrl_set_qm_min },
  { AV1E_SET_QM_MAX, ctrl_set_qm_max },
  { AV1E_SET_NUM_TG, ctrl_set_num_tg },
  { AV1E_SET_MTU, ctrl_set_mtu },
  { AV1E_SET_TIMING_INFO_TYPE, ctrl_set_timing_info_type },
  { AV1E_SET_FRAME_PARALLEL_DECODING, ctrl_set_frame_parallel_decoding_mode },
  { AV1E_SET_ERROR_RESILIENT_MODE, ctrl_set_error_resilient_mode },
  { AV1E_SET_S_FRAME_MODE, ctrl_set_s_frame_mode },
  { AV1E_SET_ENABLE_RECT_PARTITIONS, ctrl_set_enable_rect_partitions },
  { AV1E_SET_ENABLE_AB_PARTITIONS, ctrl_set_enable_ab_partitions },
  { AV1E_SET_ENABLE_1TO4_PARTITIONS, ctrl_set_enable_1to4_partitions },
  { AV1E_SET_MIN_PARTITION_SIZE, ctrl_set_min_partition_size },
  { AV1E_SET_MAX_PARTITION_SIZE, ctrl_set_max_partition_size },
  { AV1E_SET_ENABLE_DUAL_FILTER, ctrl_set_enable_dual_filter },
  { AV1E_SET_ENABLE_CHROMA_DELTAQ, ctrl_set_enable_chroma_deltaq },
  { AV1E_SET_ENABLE_INTRA_EDGE_FILTER, ctrl_set_enable_intra_edge_filter },
  { AV1E_SET_ENABLE_ORDER_HINT, ctrl_set_enable_order_hint },
  { AV1E_SET_ENABLE_TX64, ctrl_set_enable_tx64 },
  { AV1E_SET_ENABLE_FLIP_IDTX, ctrl_set_enable_flip_idtx },
  { AV1E_SET_ENABLE_RECT_TX, ctrl_set_enable_rect_tx },
  { AV1E_SET_ENABLE_DIST_WTD_COMP, ctrl_set_enable_dist_wtd_comp },
  { AV1E_SET_MAX_REFERENCE_FRAMES, ctrl_set_max_reference_frames },
  { AV1E_SET_REDUCED_REFERENCE_SET, ctrl_set_enable_reduced_reference_set },
  { AV1E_SET_ENABLE_REF_FRAME_MVS, ctrl_set_enable_ref_frame_mvs },
  { AV1E_SET_ALLOW_REF_FRAME_MVS, ctrl_set_allow_ref_frame_mvs },
  { AV1E_SET_ENABLE_MASKED_COMP, ctrl_set_enable_masked_comp },
  { AV1E_SET_ENABLE_ONESIDED_COMP, ctrl_set_enable_onesided_comp },
  { AV1E_SET_ENABLE_INTERINTRA_COMP, ctrl_set_enable_interintra_comp },
  { AV1E_SET_ENABLE_SMOOTH_INTERINTRA, ctrl_set_enable_smooth_interintra },
  { AV1E_SET_ENABLE_DIFF_WTD_COMP, ctrl_set_enable_diff_wtd_comp },
  { AV1E_SET_ENABLE_INTERINTER_WEDGE, ctrl_set_enable_interinter_wedge },
  { AV1E_SET_ENABLE_INTERINTRA_WEDGE, ctrl_set_enable_interintra_wedge },
  { AV1E_SET_ENABLE_GLOBAL_MOTION, ctrl_set_enable_global_motion },
  { AV1E_SET_ENABLE_WARPED_MOTION, ctrl_set_enable_warped_motion },
  { AV1E_SET_ALLOW_WARPED_MOTION, ctrl_set_allow_warped_motion },
  { AV1E_SET_ENABLE_FILTER_INTRA, ctrl_set_enable_filter_intra },
  { AV1E_SET_ENABLE_SMOOTH_INTRA, ctrl_set_enable_smooth_intra },
  { AV1E_SET_ENABLE_PAETH_INTRA, ctrl_set_enable_paeth_intra },
  { AV1E_SET_ENABLE_CFL_INTRA, ctrl_set_enable_cfl_intra },
  { AV1E_SET_ENABLE_DIRECTIONAL_INTRA, ctrl_set_enable_directional_intra },
  { AV1E_SET_ENABLE_DIAGONAL_INTRA, ctrl_set_enable_diagonal_intra },
  { AV1E_SET_ENABLE_SUPERRES, ctrl_set_enable_superres },
  { AV1E_SET_ENABLE_OVERLAY, ctrl_set_enable_overlay },
  { AV1E_SET_ENABLE_PALETTE, ctrl_set_enable_palette },
  { AV1E_SET_ENABLE_INTRABC, ctrl_set_enable_intrabc },
  { AV1E_SET_ENABLE_ANGLE_DELTA, ctrl_set_enable_angle_delta },
  { AV1E_SET_AQ_MODE, ctrl_set_aq_mode },
  { AV1E_SET_REDUCED_TX_TYPE_SET, ctrl_set_reduced_tx_type_set },
  { AV1E_SET_INTRA_DCT_ONLY, ctrl_set_intra_dct_only },
  { AV1E_SET_INTER_DCT_ONLY, ctrl_set_inter_dct_only },
  { AV1E_SET_INTRA_DEFAULT_TX_ONLY, ctrl_set_intra_default_tx_only },
  { AV1E_SET_QUANT_B_ADAPT, ctrl_set_quant_b_adapt },
  { AV1E_SET_COEFF_COST_UPD_FREQ, ctrl_set_coeff_cost_upd_freq },
  { AV1E_SET_MODE_COST_UPD_FREQ, ctrl_set_mode_cost_upd_freq },
  { AV1E_SET_MV_COST_UPD_FREQ, ctrl_set_mv_cost_upd_freq },
  { AV1E_SET_DELTAQ_MODE, ctrl_set_deltaq_mode },
  { AV1E_SET_DELTAQ_STRENGTH, ctrl_set_deltaq_strength },
  { AV1E_SET_DELTALF_MODE, ctrl_set_deltalf_mode },
  { AV1E_SET_FRAME_PERIODIC_BOOST, ctrl_set_frame_periodic_boost },
  { AV1E_SET_TUNE_CONTENT, ctrl_set_tune_content },
  { AV1E_SET_CDF_UPDATE_MODE, ctrl_set_cdf_update_mode },
  { AV1E_SET_COLOR_PRIMARIES, ctrl_set_color_primaries },
  { AV1E_SET_TRANSFER_CHARACTERISTICS, ctrl_set_transfer_characteristics },
  { AV1E_SET_MATRIX_COEFFICIENTS, ctrl_set_matrix_coefficients },
  { AV1E_SET_CHROMA_SAMPLE_POSITION, ctrl_set_chroma_sample_position },
  { AV1E_SET_COLOR_RANGE, ctrl_set_color_range },
  { AV1E_SET_NOISE_SENSITIVITY, ctrl_set_noise_sensitivity },
  { AV1E_SET_MIN_GF_INTERVAL, ctrl_set_min_gf_interval },
  { AV1E_SET_MAX_GF_INTERVAL, ctrl_set_max_gf_interval },
  { AV1E_SET_GF_MIN_PYRAMID_HEIGHT, ctrl_set_gf_min_pyr_height },
  { AV1E_SET_GF_MAX_PYRAMID_HEIGHT, ctrl_set_gf_max_pyr_height },
  { AV1E_SET_RENDER_SIZE, ctrl_set_render_size },
  { AV1E_SET_SUPERBLOCK_SIZE, ctrl_set_superblock_size },
  { AV1E_SET_SINGLE_TILE_DECODING, ctrl_set_single_tile_decoding },
  { AV1E_SET_VMAF_MODEL_PATH, ctrl_set_vmaf_model_path },
  { AV1E_SET_PARTITION_INFO_PATH, ctrl_set_partition_info_path },
  { AV1E_ENABLE_RATE_GUIDE_DELTAQ, ctrl_enable_rate_guide_deltaq },
  { AV1E_SET_RATE_DISTRIBUTION_INFO, ctrl_set_rate_distribution_info },
  { AV1E_SET_FILM_GRAIN_TEST_VECTOR, ctrl_set_film_grain_test_vector },
  { AV1E_SET_FILM_GRAIN_TABLE, ctrl_set_film_grain_table },
  { AV1E_SET_DENOISE_NOISE_LEVEL, ctrl_set_denoise_noise_level },
  { AV1E_SET_DENOISE_BLOCK_SIZE, ctrl_set_denoise_block_size },
  { AV1E_SET_ENABLE_DNL_DENOISING, ctrl_set_enable_dnl_denoising },
  { AV1E_ENABLE_MOTION_VECTOR_UNIT_TEST, ctrl_enable_motion_vector_unit_test },
  { AV1E_SET_FP_MT_UNIT_TEST, ctrl_enable_fpmt_unit_test },
  { AV1E_ENABLE_EXT_TILE_DEBUG, ctrl_enable_ext_tile_debug },
  { AV1E_SET_TARGET_SEQ_LEVEL_IDX, ctrl_set_target_seq_level_idx },
  { AV1E_SET_TIER_MASK, ctrl_set_tier_mask },
  { AV1E_SET_MIN_CR, ctrl_set_min_cr },
  { AV1E_SET_SVC_LAYER_ID, ctrl_set_layer_id },
  { AV1E_SET_SVC_PARAMS, ctrl_set_svc_params },
  { AV1E_SET_SVC_REF_FRAME_CONFIG, ctrl_set_svc_ref_frame_config },
  { AV1E_SET_SVC_REF_FRAME_COMP_PRED, ctrl_set_svc_ref_frame_comp_pred },
  { AV1E_SET_VBR_CORPUS_COMPLEXITY_LAP, ctrl_set_vbr_corpus_complexity_lap },
  { AV1E_ENABLE_SB_MULTIPASS_UNIT_TEST, ctrl_enable_sb_multipass_unit_test },
  { AV1E_ENABLE_SB_QP_SWEEP, ctrl_enable_sb_qp_sweep },
  { AV1E_SET_DV_COST_UPD_FREQ, ctrl_set_dv_cost_upd_freq },
  { AV1E_SET_EXTERNAL_PARTITION, ctrl_set_external_partition },
  { AV1E_SET_ENABLE_TX_SIZE_SEARCH, ctrl_set_enable_tx_size_search },
  { AV1E_SET_LOOPFILTER_CONTROL, ctrl_set_loopfilter_control },
  { AV1E_SET_SKIP_POSTPROC_FILTERING, ctrl_set_skip_postproc_filtering },
  { AV1E_SET_AUTO_INTRA_TOOLS_OFF, ctrl_set_auto_intra_tools_off },
  { AV1E_SET_RTC_EXTERNAL_RC, ctrl_set_rtc_external_rc },
  { AV1E_SET_QUANTIZER_ONE_PASS, ctrl_set_quantizer_one_pass },
  { AV1E_SET_BITRATE_ONE_PASS_CBR, ctrl_set_bitrate_one_pass_cbr },
  { AV1E_SET_MAX_CONSEC_FRAME_DROP_CBR, ctrl_set_max_consec_frame_drop_cbr },
  { AV1E_SET_SVC_FRAME_DROP_MODE, ctrl_set_svc_frame_drop_mode },
  { AV1E_SET_AUTO_TILES, ctrl_set_auto_tiles },
  { AV1E_SET_POSTENCODE_DROP_RTC, ctrl_set_postencode_drop_rtc },
  { AV1E_SET_MAX_CONSEC_FRAME_DROP_MS_CBR,
    ctrl_set_max_consec_frame_drop_ms_cbr },
  { AV1E_SET_ENABLE_LOW_COMPLEXITY_DECODE,
    ctrl_set_enable_low_complexity_decode },

  // Getters
  { AOME_GET_LAST_QUANTIZER, ctrl_get_quantizer },
  { AOME_GET_LAST_QUANTIZER_64, ctrl_get_quantizer64 },
  { AOME_GET_LOOPFILTER_LEVEL, ctrl_get_loopfilter_level },
  { AV1_GET_REFERENCE, ctrl_get_reference },
  { AV1E_GET_ACTIVEMAP, ctrl_get_active_map },
  { AV1_GET_NEW_FRAME_IMAGE, ctrl_get_new_frame_image },
  { AV1_COPY_NEW_FRAME_IMAGE, ctrl_copy_new_frame_image },
  { AV1E_SET_CHROMA_SUBSAMPLING_X, ctrl_set_chroma_subsampling_x },
  { AV1E_SET_CHROMA_SUBSAMPLING_Y, ctrl_set_chroma_subsampling_y },
  { AV1E_GET_SEQ_LEVEL_IDX, ctrl_get_seq_level_idx },
  { AV1E_GET_BASELINE_GF_INTERVAL, ctrl_get_baseline_gf_interval },
  { AV1E_GET_TARGET_SEQ_LEVEL_IDX, ctrl_get_target_seq_level_idx },
  { AV1E_GET_NUM_OPERATING_POINTS, ctrl_get_num_operating_points },
  { AV1E_GET_LUMA_CDEF_STRENGTH, ctrl_get_luma_cdef_strength },
  { AV1E_GET_HIGH_MOTION_CONTENT_SCREEN_RTC,
    ctrl_get_high_motion_content_screen_rtc },

  CTRL_MAP_END,
};

static const aom_codec_enc_cfg_t encoder_usage_cfg[] = {
#if !CONFIG_REALTIME_ONLY
  {
      // NOLINT
      AOM_USAGE_GOOD_QUALITY,  // g_usage - non-realtime usage
      0,                       // g_threads
      0,                       // g_profile

      320,         // g_w
      240,         // g_h
      0,           // g_limit
      0,           // g_forced_max_frame_width
      0,           // g_forced_max_frame_height
      AOM_BITS_8,  // g_bit_depth
      8,           // g_input_bit_depth

      { 1, 30 },  // g_timebase

      0,  // g_error_resilient

      AOM_RC_ONE_PASS,  // g_pass

      35,  // g_lag_in_frames

      0,                // rc_dropframe_thresh
      RESIZE_NONE,      // rc_resize_mode
      SCALE_NUMERATOR,  // rc_resize_denominator
      SCALE_NUMERATOR,  // rc_resize_kf_denominator

      AOM_SUPERRES_NONE,  // rc_superres_mode
      SCALE_NUMERATOR,    // rc_superres_denominator
      SCALE_NUMERATOR,    // rc_superres_kf_denominator
      63,                 // rc_superres_qthresh
      32,                 // rc_superres_kf_qthresh

      AOM_VBR,      // rc_end_usage
      { NULL, 0 },  // rc_twopass_stats_in
      { NULL, 0 },  // rc_firstpass_mb_stats_in
      256,          // rc_target_bitrate
      0,            // rc_min_quantizer
      63,           // rc_max_quantizer
      25,           // rc_undershoot_pct
      25,           // rc_overshoot_pct

      6000,  // rc_buf_sz
      4000,  // rc_buf_initial_sz
      5000,  // rc_buf_optimal_sz

      50,    // rc_2pass_vbr_bias_pct
      0,     // rc_2pass_vbr_minsection_pct
      2000,  // rc_2pass_vbr_maxsection_pct

      // keyframing settings (kf)
      0,                       // fwd_kf_enabled
      AOM_KF_AUTO,             // kf_mode
      0,                       // kf_min_dist
      9999,                    // kf_max_dist
      0,                       // sframe_dist
      1,                       // sframe_mode
      0,                       // large_scale_tile
      0,                       // monochrome
      0,                       // full_still_picture_hdr
      0,                       // save_as_annexb
      0,                       // tile_width_count
      0,                       // tile_height_count
      { 0 },                   // tile_widths
      { 0 },                   // tile_heights
      0,                       // use_fixed_qp_offsets
      { -1, -1, -1, -1, -1 },  // fixed_qp_offsets
      { 0, 128, 128, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // encoder_cfg
  },
#endif  // !CONFIG_REALTIME_ONLY
  {
      // NOLINT
      AOM_USAGE_REALTIME,  // g_usage - real-time usage
      0,                   // g_threads
      0,                   // g_profile

      320,         // g_w
      240,         // g_h
      0,           // g_limit
      0,           // g_forced_max_frame_width
      0,           // g_forced_max_frame_height
      AOM_BITS_8,  // g_bit_depth
      8,           // g_input_bit_depth

      { 1, 30 },  // g_timebase

      0,  // g_error_resilient

      AOM_RC_ONE_PASS,  // g_pass

      0,  // g_lag_in_frames

      0,                // rc_dropframe_thresh
      RESIZE_NONE,      // rc_resize_mode
      SCALE_NUMERATOR,  // rc_resize_denominator
      SCALE_NUMERATOR,  // rc_resize_kf_denominator

      AOM_SUPERRES_NONE,  // rc_superres_mode
      SCALE_NUMERATOR,    // rc_superres_denominator
      SCALE_NUMERATOR,    // rc_superres_kf_denominator
      63,                 // rc_superres_qthresh
      32,                 // rc_superres_kf_qthresh

      AOM_CBR,      // rc_end_usage
      { NULL, 0 },  // rc_twopass_stats_in
      { NULL, 0 },  // rc_firstpass_mb_stats_in
      256,          // rc_target_bitrate
      0,            // rc_min_quantizer
      63,           // rc_max_quantizer
      50,           // rc_undershoot_pct
      50,           // rc_overshoot_pct

      1000,  // rc_buf_sz
      600,   // rc_buf_initial_sz
      600,   // rc_buf_optimal_sz

      50,    // rc_2pass_vbr_bias_pct
      0,     // rc_2pass_vbr_minsection_pct
      2000,  // rc_2pass_vbr_maxsection_pct

      // keyframing settings (kf)
      0,                       // fwd_kf_enabled
      AOM_KF_AUTO,             // kf_mode
      0,                       // kf_min_dist
      9999,                    // kf_max_dist
      0,                       // sframe_dist
      1,                       // sframe_mode
      0,                       // large_scale_tile
      0,                       // monochrome
      0,                       // full_still_picture_hdr
      0,                       // save_as_annexb
      0,                       // tile_width_count
      0,                       // tile_height_count
      { 0 },                   // tile_widths
      { 0 },                   // tile_heights
      0,                       // use_fixed_qp_offsets
      { -1, -1, -1, -1, -1 },  // fixed_qp_offsets
      { 0, 128, 128, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // encoder_cfg
  },
#if !CONFIG_REALTIME_ONLY
  {
      // NOLINT
      AOM_USAGE_ALL_INTRA,  // g_usage - all intra usage
      0,                    // g_threads
      0,                    // g_profile

      320,         // g_w
      240,         // g_h
      0,           // g_limit
      0,           // g_forced_max_frame_width
      0,           // g_forced_max_frame_height
      AOM_BITS_8,  // g_bit_depth
      8,           // g_input_bit_depth

      { 1, 30 },  // g_timebase

      0,  // g_error_resilient

      AOM_RC_ONE_PASS,  // g_pass

      0,  // g_lag_in_frames

      0,                // rc_dropframe_thresh
      RESIZE_NONE,      // rc_resize_mode
      SCALE_NUMERATOR,  // rc_resize_denominator
      SCALE_NUMERATOR,  // rc_resize_kf_denominator

      AOM_SUPERRES_NONE,  // rc_superres_mode
      SCALE_NUMERATOR,    // rc_superres_denominator
      SCALE_NUMERATOR,    // rc_superres_kf_denominator
      63,                 // rc_superres_qthresh
      32,                 // rc_superres_kf_qthresh

      AOM_Q,        // rc_end_usage
      { NULL, 0 },  // rc_twopass_stats_in
      { NULL, 0 },  // rc_firstpass_mb_stats_in
      256,          // rc_target_bitrate
      0,            // rc_min_quantizer
      63,           // rc_max_quantizer
      25,           // rc_undershoot_pct
      25,           // rc_overshoot_pct

      6000,  // rc_buf_sz
      4000,  // rc_buf_initial_sz
      5000,  // rc_buf_optimal_sz

      50,    // rc_2pass_vbr_bias_pct
      0,     // rc_2pass_vbr_minsection_pct
      2000,  // rc_2pass_vbr_maxsection_pct

      // keyframing settings (kf)
      0,                       // fwd_kf_enabled
      AOM_KF_DISABLED,         // kf_mode
      0,                       // kf_min_dist
      0,                       // kf_max_dist
      0,                       // sframe_dist
      1,                       // sframe_mode
      0,                       // large_scale_tile
      0,                       // monochrome
      0,                       // full_still_picture_hdr
      0,                       // save_as_annexb
      0,                       // tile_width_count
      0,                       // tile_height_count
      { 0 },                   // tile_widths
      { 0 },                   // tile_heights
      0,                       // use_fixed_qp_offsets
      { -1, -1, -1, -1, -1 },  // fixed_qp_offsets
      { 0, 128, 128, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // encoder_cfg
  },
#endif  // !CONFIG_REALTIME_ONLY
};

// This data structure and function are exported in aom/aomcx.h
#ifndef VERSION_STRING
#define VERSION_STRING
#endif
aom_codec_iface_t aom_codec_av1_cx_algo = {
  "AOMedia Project AV1 Encoder" VERSION_STRING,
  AOM_CODEC_INTERNAL_ABI_VERSION,
  (CONFIG_AV1_HIGHBITDEPTH ? AOM_CODEC_CAP_HIGHBITDEPTH : 0) |
      AOM_CODEC_CAP_ENCODER | AOM_CODEC_CAP_PSNR,  // aom_codec_caps_t
  encoder_init,                                    // aom_codec_init_fn_t
  encoder_destroy,                                 // aom_codec_destroy_fn_t
  encoder_ctrl_maps,                               // aom_codec_ctrl_fn_map_t
  {
      // NOLINT
      NULL,  // aom_codec_peek_si_fn_t
      NULL,  // aom_codec_get_si_fn_t
      NULL,  // aom_codec_decode_fn_t
      NULL,  // aom_codec_get_frame_fn_t
      NULL   // aom_codec_set_fb_fn_t
  },
  {
      // NOLINT
      NELEMENTS(encoder_usage_cfg),  // cfg_count
      encoder_usage_cfg,             // aom_codec_enc_cfg_t
      encoder_encode,                // aom_codec_encode_fn_t
      encoder_get_cxdata,            // aom_codec_get_cx_data_fn_t
      encoder_set_config,            // aom_codec_enc_config_set_fn_t
      encoder_get_global_headers,    // aom_codec_get_global_headers_fn_t
      encoder_get_preview            // aom_codec_get_preview_frame_fn_t
  },
  encoder_set_option  // aom_codec_set_option_fn_t
};

aom_codec_iface_t *aom_codec_av1_cx(void) { return &aom_codec_av1_cx_algo; }
