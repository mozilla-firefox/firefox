/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include "av1/arg_defs.h"

static const struct arg_enum_list test_decode_enum[] = {
  { "off", TEST_DECODE_OFF },
  { "fatal", TEST_DECODE_FATAL },
  { "warn", TEST_DECODE_WARN },
  { NULL, 0 }
};

static const struct arg_enum_list bitdepth_enum[] = {
  { "8", AOM_BITS_8 }, { "10", AOM_BITS_10 }, { "12", AOM_BITS_12 }, { NULL, 0 }
};

#if CONFIG_WEBM_IO
static const struct arg_enum_list stereo_mode_enum[] = {
  { "mono", STEREO_FORMAT_MONO },
  { "left-right", STEREO_FORMAT_LEFT_RIGHT },
  { "bottom-top", STEREO_FORMAT_BOTTOM_TOP },
  { "top-bottom", STEREO_FORMAT_TOP_BOTTOM },
  { "right-left", STEREO_FORMAT_RIGHT_LEFT },
  { NULL, 0 }
};
#endif

static const struct arg_enum_list end_usage_enum[] = { { "vbr", AOM_VBR },
                                                       { "cbr", AOM_CBR },
                                                       { "cq", AOM_CQ },
                                                       { "q", AOM_Q },
                                                       { NULL, 0 } };

static const struct arg_enum_list tuning_enum[] = {
  { "psnr", AOM_TUNE_PSNR },
  { "ssim", AOM_TUNE_SSIM },
  { "vmaf_with_preprocessing", AOM_TUNE_VMAF_WITH_PREPROCESSING },
  { "vmaf_without_preprocessing", AOM_TUNE_VMAF_WITHOUT_PREPROCESSING },
  { "vmaf", AOM_TUNE_VMAF_MAX_GAIN },
  { "vmaf_neg", AOM_TUNE_VMAF_NEG_MAX_GAIN },
  { "butteraugli", AOM_TUNE_BUTTERAUGLI },
  { "vmaf_saliency_map", AOM_TUNE_VMAF_SALIENCY_MAP },
  { "iq", AOM_TUNE_IQ },
  { "ssimulacra2", AOM_TUNE_SSIMULACRA2 },
  { NULL, 0 }
};

static const struct arg_enum_list dist_metric_enum[] = {
  { "psnr", AOM_DIST_METRIC_PSNR },
  { "qm-psnr", AOM_DIST_METRIC_QM_PSNR },
  { NULL, 0 }
};

#if CONFIG_AV1_ENCODER
static const struct arg_enum_list timing_info_enum[] = {
  { "unspecified", AOM_TIMING_UNSPECIFIED },
  { "constant", AOM_TIMING_EQUAL },
  { "model", AOM_TIMING_DEC_MODEL },
  { NULL, 0 }
};

static const struct arg_enum_list superblock_size_enum[] = {
  { "dynamic", AOM_SUPERBLOCK_SIZE_DYNAMIC },
  { "64", AOM_SUPERBLOCK_SIZE_64X64 },
  { "128", AOM_SUPERBLOCK_SIZE_128X128 },
  { NULL, 0 }
};

static const struct arg_enum_list matrix_coefficients_enum[] = {
  { "identity", AOM_CICP_MC_IDENTITY },
  { "bt709", AOM_CICP_MC_BT_709 },
  { "unspecified", AOM_CICP_MC_UNSPECIFIED },
  { "fcc73", AOM_CICP_MC_FCC },
  { "bt470bg", AOM_CICP_MC_BT_470_B_G },
  { "bt601", AOM_CICP_MC_BT_601 },
  { "smpte240", AOM_CICP_CP_SMPTE_240 },
  { "ycgco", AOM_CICP_MC_SMPTE_YCGCO },
  { "bt2020ncl", AOM_CICP_MC_BT_2020_NCL },
  { "bt2020cl", AOM_CICP_MC_BT_2020_CL },
  { "smpte2085", AOM_CICP_MC_SMPTE_2085 },
  { "chromncl", AOM_CICP_MC_CHROMAT_NCL },
  { "chromcl", AOM_CICP_MC_CHROMAT_CL },
  { "ictcp", AOM_CICP_MC_ICTCP },
  { NULL, 0 }
};

static const struct arg_enum_list chroma_sample_position_enum[] = {
  { "unknown", AOM_CSP_UNKNOWN },
  { "vertical", AOM_CSP_VERTICAL },
  { "colocated", AOM_CSP_COLOCATED },
  { NULL, 0 }
};

static const struct arg_enum_list tune_content_enum[] = {
  { "default", AOM_CONTENT_DEFAULT },
  { "screen", AOM_CONTENT_SCREEN },
  { "film", AOM_CONTENT_FILM },
  { NULL, 0 }
};

static const struct arg_enum_list transfer_characteristics_enum[] = {
  { "unspecified", AOM_CICP_CP_UNSPECIFIED },
  { "bt709", AOM_CICP_TC_BT_709 },
  { "bt470m", AOM_CICP_TC_BT_470_M },
  { "bt470bg", AOM_CICP_TC_BT_470_B_G },
  { "bt601", AOM_CICP_TC_BT_601 },
  { "smpte240", AOM_CICP_TC_SMPTE_240 },
  { "lin", AOM_CICP_TC_LINEAR },
  { "log100", AOM_CICP_TC_LOG_100 },
  { "log100sq10", AOM_CICP_TC_LOG_100_SQRT10 },
  { "iec61966", AOM_CICP_TC_IEC_61966 },
  { "bt1361", AOM_CICP_TC_BT_1361 },
  { "srgb", AOM_CICP_TC_SRGB },
  { "bt2020-10bit", AOM_CICP_TC_BT_2020_10_BIT },
  { "bt2020-12bit", AOM_CICP_TC_BT_2020_12_BIT },
  { "smpte2084", AOM_CICP_TC_SMPTE_2084 },
  { "hlg", AOM_CICP_TC_HLG },
  { "smpte428", AOM_CICP_TC_SMPTE_428 },
  { NULL, 0 }
};

static const struct arg_enum_list color_primaries_enum[] = {
  { "bt709", AOM_CICP_CP_BT_709 },
  { "unspecified", AOM_CICP_CP_UNSPECIFIED },
  { "bt601", AOM_CICP_CP_BT_601 },
  { "bt470m", AOM_CICP_CP_BT_470_M },
  { "bt470bg", AOM_CICP_CP_BT_470_B_G },
  { "smpte240", AOM_CICP_CP_SMPTE_240 },
  { "film", AOM_CICP_CP_GENERIC_FILM },
  { "bt2020", AOM_CICP_CP_BT_2020 },
  { "xyz", AOM_CICP_CP_XYZ },
  { "smpte431", AOM_CICP_CP_SMPTE_431 },
  { "smpte432", AOM_CICP_CP_SMPTE_432 },
  { "ebu3213", AOM_CICP_CP_EBU_3213 },
  { NULL, 0 }
};
#endif  // CONFIG_AV1_ENCODER

const av1_codec_arg_definitions_t g_av1_codec_arg_defs = {
  .help = ARG_DEF(NULL, "help", 0, "Show usage options and exit"),
  .debugmode =
      ARG_DEF("D", "debug", 0, "Debug mode (makes output deterministic)"),
  .outputfile = ARG_DEF("o", "output", 1, "Output filename"),
  .use_nv12 = ARG_DEF(NULL, "nv12", 0, "Input file is NV12"),
  .use_yv12 = ARG_DEF(NULL, "yv12", 0, "Input file is YV12"),
  .use_i420 = ARG_DEF(NULL, "i420", 0, "Input file is I420 (default)"),
  .use_i422 = ARG_DEF(NULL, "i422", 0, "Input file is I422"),
  .use_i444 = ARG_DEF(NULL, "i444", 0, "Input file is I444"),
  .codecarg = ARG_DEF(NULL, "codec", 1, "Codec to use"),
  .passes = ARG_DEF("p", "passes", 1, "Number of passes (1/2/3)"),
  .pass_arg = ARG_DEF(NULL, "pass", 1, "Pass to execute (1/2/3)"),
  .fpf_name = ARG_DEF(NULL, "fpf", 1, "First pass statistics file name"),
  .limit = ARG_DEF(NULL, "limit", 1, "Stop encoding after n input frames"),
  .skip = ARG_DEF(NULL, "skip", 1, "Skip the first n input frames"),
  .good_dl = ARG_DEF(NULL, "good", 0, "Use Good Quality Deadline"),
  .rt_dl = ARG_DEF(NULL, "rt", 0, "Use Realtime Quality Deadline"),
  .ai_dl = ARG_DEF(NULL, "allintra", 0, "Use all intra mode"),
  .quietarg = ARG_DEF("q", "quiet", 0, "Do not print encode progress"),
  .verbosearg = ARG_DEF("v", "verbose", 0, "Show encoder parameters"),
  .psnrarg = ARG_DEF(
      NULL, "psnr", -1,
      "Show PSNR in status line "
      "(0: Disable PSNR status line display, 1: PSNR calculated using input "
      "bit-depth (default), 2: PSNR calculated using stream bit-depth); "
      "takes default option when arguments are not specified"),
  .use_cfg = ARG_DEF("c", "cfg", 1, "Config file to use"),
  .recontest = ARG_DEF_ENUM(NULL, "test-decode", 1,
                            "Test encode/decode mismatch", test_decode_enum),
  .framerate = ARG_DEF(NULL, "fps", 1, "Stream frame rate (rate/scale)"),
  .use_webm =
      ARG_DEF(NULL, "webm", 0, "Output WebM (default when WebM IO is enabled)"),
  .use_ivf = ARG_DEF(NULL, "ivf", 0, "Output IVF"),
  .use_obu = ARG_DEF(NULL, "obu", 0, "Output OBU"),
  .q_hist_n =
      ARG_DEF(NULL, "q-hist", 1, "Show quantizer histogram (n-buckets)"),
  .rate_hist_n =
      ARG_DEF(NULL, "rate-hist", 1, "Show rate histogram (n-buckets)"),
  .disable_warnings =
      ARG_DEF(NULL, "disable-warnings", 0,
              "Disable warnings about potentially incorrect encode settings"),
  .disable_warning_prompt =
      ARG_DEF("y", "disable-warning-prompt", 0,
              "Display warnings, but do not prompt user to continue"),
  .bitdeptharg =
      ARG_DEF_ENUM("b", "bit-depth", 1, "Bit depth for codec", bitdepth_enum),
  .inbitdeptharg = ARG_DEF(NULL, "input-bit-depth", 1, "Bit depth of input"),

  .input_chroma_subsampling_x = ARG_DEF(NULL, "input-chroma-subsampling-x", 1,
                                        "Chroma subsampling x value"),
  .input_chroma_subsampling_y = ARG_DEF(NULL, "input-chroma-subsampling-y", 1,
                                        "Chroma subsampling y value"),

  .usage = ARG_DEF("u", "usage", 1,
                   "Usage profile number to use (0: good, 1: rt, 2: allintra)"),
  .threads = ARG_DEF("t", "threads", 1, "Max number of threads to use"),
  .profile = ARG_DEF(NULL, "profile", 1, "Bitstream profile number to use"),
  .width = ARG_DEF("w", "width", 1, "Frame width"),
  .height = ARG_DEF("h", "height", 1, "Frame height"),
  .forced_max_frame_width = ARG_DEF(NULL, "forced_max_frame_width", 1,
                                    "Maximum frame width value to force"),
  .forced_max_frame_height = ARG_DEF(NULL, "forced_max_frame_height", 1,
                                     "Maximum frame height value to force"),
#if CONFIG_WEBM_IO
  .stereo_mode = ARG_DEF_ENUM(NULL, "stereo-mode", 1, "Stereo 3D video format",
                              stereo_mode_enum),
#endif
  .timebase = ARG_DEF(NULL, "timebase", 1,
                      "Output timestamp precision (fractional seconds)"),
  .global_error_resilient = ARG_DEF(NULL, "global-error-resilient", 1,
                                    "Enable global error resiliency features"),
  .lag_in_frames =
      ARG_DEF(NULL, "lag-in-frames", 1, "Max number of frames to lag"),
  .large_scale_tile = ARG_DEF(
      NULL, "large-scale-tile", 1,
      "Large scale tile coding (0: off (default), 1: on (ivf output only))"),
  .monochrome =
      ARG_DEF(NULL, "monochrome", 0, "Monochrome video (no chroma planes)"),
  .full_still_picture_hdr = ARG_DEF(NULL, "full-still-picture-hdr", 0,
                                    "Use full header for still picture"),
  .use_16bit_internal =
      ARG_DEF(NULL, "use-16bit-internal", 0, "Force use of 16-bit pipeline"),
  .dropframe_thresh =
      ARG_DEF(NULL, "drop-frame", 1, "Temporal resampling threshold (buf %)"),
  .resize_mode = ARG_DEF(
      NULL, "resize-mode", 1,
      "Frame resize mode (0: off (default), 1: fixed, 2: random, 3: dynamic)"),
  .resize_denominator =
      ARG_DEF(NULL, "resize-denominator", 1, "Frame resize denominator"),
  .resize_kf_denominator = ARG_DEF(NULL, "resize-kf-denominator", 1,
                                   "Frame resize keyframe denominator"),
  .superres_mode =
      ARG_DEF(NULL, "superres-mode", 1,
              "Frame super-resolution mode (0: disabled (default), 1: fixed, "
              "2: random, 3: qthresh, 4: auto)"),
  .superres_denominator = ARG_DEF(NULL, "superres-denominator", 1,
                                  "Frame super-resolution denominator"),
  .superres_kf_denominator =
      ARG_DEF(NULL, "superres-kf-denominator", 1,
              "Frame super-resolution keyframe denominator"),
  .superres_qthresh = ARG_DEF(NULL, "superres-qthresh", 1,
                              "Frame super-resolution qindex threshold"),
  .superres_kf_qthresh =
      ARG_DEF(NULL, "superres-kf-qthresh", 1,
              "Frame super-resolution keyframe qindex threshold"),
  .end_usage =
      ARG_DEF_ENUM(NULL, "end-usage", 1, "Rate control mode", end_usage_enum),
  .target_bitrate = ARG_DEF(NULL, "target-bitrate", 1, "Bitrate (kbps)"),
  .min_quantizer = ARG_DEF(NULL, "min-q", 1, "Minimum (best) quantizer"),
  .max_quantizer = ARG_DEF(NULL, "max-q", 1, "Maximum (worst) quantizer"),
  .undershoot_pct = ARG_DEF(NULL, "undershoot-pct", 1,
                            "Datarate undershoot (min) target (%)"),
  .overshoot_pct =
      ARG_DEF(NULL, "overshoot-pct", 1, "Datarate overshoot (max) target (%)"),
  .buf_sz = ARG_DEF(NULL, "buf-sz", 1, "Client buffer size (ms)"),
  .buf_initial_sz =
      ARG_DEF(NULL, "buf-initial-sz", 1, "Client initial buffer size (ms)"),
  .buf_optimal_sz =
      ARG_DEF(NULL, "buf-optimal-sz", 1, "Client optimal buffer size (ms)"),
  .bias_pct = ARG_DEF(NULL, "bias-pct", 1, "CBR/VBR bias (0=CBR, 100=VBR)"),
  .minsection_pct =
      ARG_DEF(NULL, "minsection-pct", 1, "GOP min bitrate (% of target)"),
  .maxsection_pct =
      ARG_DEF(NULL, "maxsection-pct", 1, "GOP max bitrate (% of target)"),
  .fwd_kf_enabled =
      ARG_DEF(NULL, "enable-fwd-kf", 1, "Enable forward reference keyframes"),
  .kf_min_dist =
      ARG_DEF(NULL, "kf-min-dist", 1, "Minimum keyframe interval (frames)"),
  .kf_max_dist =
      ARG_DEF(NULL, "kf-max-dist", 1, "Maximum keyframe interval (frames)"),
  .kf_disabled = ARG_DEF(NULL, "disable-kf", 0, "Disable keyframe placement"),
  .sframe_dist = ARG_DEF(NULL, "sframe-dist", 1, "S-Frame interval (frames)"),
  .sframe_mode =
      ARG_DEF(NULL, "sframe-mode", 1, "S-Frame insertion mode (1..2)"),
  .save_as_annexb = ARG_DEF(NULL, "annexb", 1, "Save as Annex-B"),
  .noise_sens = ARG_DEF(NULL, "noise-sensitivity", 1,
                        "Noise sensitivity (frames to blur)"),
  .sharpness =
      ARG_DEF(NULL, "sharpness", 1,
              "Bias towards block sharpness in rate-distortion optimization of "
              "transform coefficients and (in all intra mode only) reduce "
              "block edge filtering for better sharpness (0..7), default is 0"),
  .static_thresh =
      ARG_DEF(NULL, "static-thresh", 1, "Motion detection threshold"),
  .auto_altref =
      ARG_DEF(NULL, "auto-alt-ref", 1, "Enable automatic alt reference frames"),
  .arnr_maxframes =
      ARG_DEF(NULL, "arnr-maxframes", 1, "AltRef max frames (0..15)"),
  .arnr_strength =
      ARG_DEF(NULL, "arnr-strength", 1, "AltRef filter strength (0..6)"),
  .tune_metric = ARG_DEF_ENUM(NULL, "tune", 1, "Distortion metric tuned with",
                              tuning_enum),
  .dist_metric = ARG_DEF_ENUM(
      NULL, "dist-metric", 1,
      "Distortion metric to use for in-block optimization", dist_metric_enum),
  .cq_level =
      ARG_DEF(NULL, "cq-level", 1, "Constant/Constrained Quality level"),
  .max_intra_rate_pct =
      ARG_DEF(NULL, "max-intra-rate", 1, "Max I-frame bitrate (pct)"),
#if CONFIG_AV1_ENCODER
  .cpu_used_av1 = ARG_DEF(NULL, "cpu-used", 1,
                          "Speed setting (0..6 in good mode, 5..12 in realtime "
                          "mode, 0..9 in all intra mode)"),
  .rowmtarg =
      ARG_DEF(NULL, "row-mt", 1,
              "Enable row based multi-threading (0: off, 1: on (default))"),
  .fpmtarg = ARG_DEF(
      NULL, "fp-mt", 1,
      "Enable frame parallel multi-threading (0: off (default), 1: on)"),
  .tile_cols =
      ARG_DEF(NULL, "tile-columns", 1, "Number of tile columns to use, log2"),
  .tile_rows =
      ARG_DEF(NULL, "tile-rows", 1, "Number of tile rows to use, log2"),
  .auto_tiles = ARG_DEF(NULL, "auto-tiles", 1,
                        "Enable auto tiles (0: false (default), 1: true)"),
  .enable_tpl_model = ARG_DEF(NULL, "enable-tpl-model", 1,
                              "RDO based on frame temporal dependency "
                              "(0: off, 1: backward source based); "
                              "required for deltaq mode"),
  .enable_keyframe_filtering = ARG_DEF(
      NULL, "enable-keyframe-filtering", 1,
      "Apply temporal filtering on key frame "
      "(0: no filter, 1: filter without overlay (default), "
      "2: filter with overlay - experimental, may break random access in "
      "players)"),
  .tile_width = ARG_DEF(NULL, "tile-width", 1, "Tile widths (comma separated)"),
  .tile_height =
      ARG_DEF(NULL, "tile-height", 1, "Tile heights (command separated)"),
  .lossless = ARG_DEF(NULL, "lossless", 1,
                      "Lossless mode (0: false (default), 1: true)"),
  .enable_cdef = ARG_DEF(
      NULL, "enable-cdef", 1,
      "Enable the constrained directional enhancement filter (0: false, "
      "1: true (default), 2: disable for non-reference frames, 3: enable "
      "adaptively based on frame qindex)"),
  .enable_restoration = ARG_DEF(NULL, "enable-restoration", 1,
                                "Enable the loop restoration filter (0: false "
                                "(default in realtime mode), "
                                "1: true (default in non-realtime mode))"),
  .enable_rect_partitions = ARG_DEF(NULL, "enable-rect-partitions", 1,
                                    "Enable rectangular partitions "
                                    "(0: false, 1: true (default))"),
  .enable_ab_partitions =
      ARG_DEF(NULL, "enable-ab-partitions", 1,
              "Enable ab partitions (0: false, 1: true (default))"),
  .enable_1to4_partitions = ARG_DEF(NULL, "enable-1to4-partitions", 1,
                                    "Enable 1:4 and 4:1 partitions "
                                    "(0: false, 1: true (default))"),
  .min_partition_size =
      ARG_DEF(NULL, "min-partition-size", 1,
              "Set min partition size "
              "(4:4x4, 8:8x8, 16:16x16, 32:32x32, 64:64x64, 128:128x128); "
              "with 4k+ resolutions or higher speed settings, min "
              "partition size will have a minimum of 8"),
  .max_partition_size =
      ARG_DEF(NULL, "max-partition-size", 1,
              "Set max partition size "
              "(4:4x4, 8:8x8, 16:16x16, 32:32x32, 64:64x64, 128:128x128)"),
  .enable_dual_filter = ARG_DEF(NULL, "enable-dual-filter", 1,
                                "Enable dual filter "
                                "(0: false, 1: true (default))"),
  .enable_chroma_deltaq = ARG_DEF(NULL, "enable-chroma-deltaq", 1,
                                  "Enable chroma delta quant "
                                  "(0: false (default), 1: true)"),
  .enable_intra_edge_filter = ARG_DEF(NULL, "enable-intra-edge-filter", 1,
                                      "Enable intra edge filtering "
                                      "(0: false, 1: true (default))"),
  .enable_order_hint = ARG_DEF(NULL, "enable-order-hint", 1,
                               "Enable order hint "
                               "(0: false, 1: true (default))"),
  .enable_tx64 =
      ARG_DEF(NULL, "enable-tx64", 1,
              "Enable 64-pt transform (0: false, 1: true (default))"),
  .enable_flip_idtx =
      ARG_DEF(NULL, "enable-flip-idtx", 1,
              "Enable extended transform type (0: false, 1: true (default)) "
              "including FLIPADST_DCT, DCT_FLIPADST, FLIPADST_FLIPADST, "
              "ADST_FLIPADST, FLIPADST_ADST, IDTX, V_DCT, H_DCT, V_ADST, "
              "H_ADST, V_FLIPADST, H_FLIPADST"),
  .enable_rect_tx =
      ARG_DEF(NULL, "enable-rect-tx", 1,
              "Enable rectangular transform (0: false, 1: true (default))"),
  .enable_dist_wtd_comp = ARG_DEF(NULL, "enable-dist-wtd-comp", 1,
                                  "Enable distance-weighted compound "
                                  "(0: false, 1: true (default))"),
  .enable_masked_comp = ARG_DEF(NULL, "enable-masked-comp", 1,
                                "Enable masked (wedge/diff-wtd) compound "
                                "(0: false, 1: true (default))"),
  .enable_onesided_comp = ARG_DEF(NULL, "enable-onesided-comp", 1,
                                  "Enable one sided compound "
                                  "(0: false, 1: true (default))"),
  .enable_interintra_comp = ARG_DEF(NULL, "enable-interintra-comp", 1,
                                    "Enable interintra compound "
                                    "(0: false, 1: true (default))"),
  .enable_smooth_interintra = ARG_DEF(NULL, "enable-smooth-interintra", 1,
                                      "Enable smooth interintra mode "
                                      "(0: false, 1: true (default))"),
  .enable_diff_wtd_comp = ARG_DEF(NULL, "enable-diff-wtd-comp", 1,
                                  "Enable difference-weighted compound "
                                  "(0: false, 1: true (default))"),
  .enable_interinter_wedge = ARG_DEF(NULL, "enable-interinter-wedge", 1,
                                     "Enable interinter wedge compound "
                                     "(0: false, 1: true (default))"),
  .enable_interintra_wedge = ARG_DEF(NULL, "enable-interintra-wedge", 1,
                                     "Enable interintra wedge compound "
                                     "(0: false, 1: true (default))"),
  .enable_global_motion = ARG_DEF(NULL, "enable-global-motion", 1,
                                  "Enable global motion "
                                  "(0: false, 1: true (default))"),
  .enable_warped_motion = ARG_DEF(NULL, "enable-warped-motion", 1,
                                  "Enable local warped motion "
                                  "(0: false, 1: true (default))"),
  .enable_filter_intra = ARG_DEF(NULL, "enable-filter-intra", 1,
                                 "Enable filter intra prediction mode "
                                 "(0: false, 1: true (default))"),
  .enable_smooth_intra = ARG_DEF(NULL, "enable-smooth-intra", 1,
                                 "Enable smooth intra prediction modes "
                                 "(0: false, 1: true (default))"),
  .enable_paeth_intra = ARG_DEF(
      NULL, "enable-paeth-intra", 1,
      "Enable Paeth intra prediction mode (0: false, 1: true (default))"),
  .enable_cfl_intra = ARG_DEF(NULL, "enable-cfl-intra", 1,
                              "Enable chroma from luma intra prediction mode "
                              "(0: false, 1: true (default))"),
  .enable_directional_intra =
      ARG_DEF(NULL, "enable-directional-intra", 1,
              "Enable directional intra prediction modes "
              "(0: false, 1: true (default))"),
  .enable_diagonal_intra =
      ARG_DEF(NULL, "enable-diagonal-intra", 1,
              "Enable diagonal (D45 to D203) intra prediction modes, which are "
              "a subset of directional modes; has no effect if "
              "enable-directional-intra is 0 (0: false, 1: true (default))"),
  .force_video_mode = ARG_DEF(
      NULL, "force-video-mode", 1,
      "Force video mode even for a single frame (0: false (default), 1: true)"),
  .enable_obmc = ARG_DEF(NULL, "enable-obmc", 1,
                         "Enable OBMC (0: false, 1: true (default))"),
  .enable_overlay =
      ARG_DEF(NULL, "enable-overlay", 1,
              "Enable coding overlay frames (0: false, 1: true (default))"),
  .enable_palette =
      ARG_DEF(NULL, "enable-palette", 1,
              "Enable palette prediction mode (0: false, 1: true (default))"),
  .enable_intrabc = ARG_DEF(NULL, "enable-intrabc", 1,
                            "Enable intra block copy prediction mode "
                            "(0: false, 1: true (default))"),
  .enable_angle_delta =
      ARG_DEF(NULL, "enable-angle-delta", 1,
              "Enable intra angle delta (0: false, 1: true (default))"),
  .disable_trellis_quant = ARG_DEF(
      NULL, "disable-trellis-quant", 1,
      "Disable trellis optimization of quantized coefficients (0: false "
      "1: true  2: true for rd search 3: true for estimate yrd search "
      "(default))"),
  .enable_qm =
      ARG_DEF(NULL, "enable-qm", 1,
              "Enable quantisation matrices (0: false (default), 1: true)"),
  .qm_min = ARG_DEF(
      NULL, "qm-min", 1,
      "Min quant matrix flatness (0..15), default is 5 (4 for all intra mode)"),
  .qm_max = ARG_DEF(NULL, "qm-max", 1,
                    "Max quant matrix flatness (0..15), default is 9 (10 for "
                    "all intra mode)"),
  .reduced_tx_type_set = ARG_DEF(NULL, "reduced-tx-type-set", 1,
                                 "Use reduced set of transform types"),
  .use_intra_dct_only =
      ARG_DEF(NULL, "use-intra-dct-only", 1, "Use DCT only for INTRA modes"),
  .use_inter_dct_only =
      ARG_DEF(NULL, "use-inter-dct-only", 1, "Use DCT only for INTER modes"),
  .use_intra_default_tx_only =
      ARG_DEF(NULL, "use-intra-default-tx-only", 1,
              "Use Default-transform only for INTRA modes"),
  .quant_b_adapt = ARG_DEF(NULL, "quant-b-adapt", 1, "Use adaptive quantize_b"),
  .coeff_cost_upd_freq = ARG_DEF(NULL, "coeff-cost-upd-freq", 1,
                                 "Update freq for coeff costs. "
                                 "0: SB, 1: SB Row per Tile, 2: Tile, 3: Off"),
  .mode_cost_upd_freq = ARG_DEF(NULL, "mode-cost-upd-freq", 1,
                                "Update freq for mode costs. "
                                "0: SB, 1: SB Row per Tile, 2: Tile, 3: Off"),
  .mv_cost_upd_freq = ARG_DEF(NULL, "mv-cost-upd-freq", 1,
                              "Update freq for mv costs. "
                              "0: SB, 1: SB Row per Tile, 2: Tile, 3: Off"),
  .dv_cost_upd_freq = ARG_DEF(NULL, "dv-cost-upd-freq", 1,
                              "Update freq for dv costs. "
                              "0: SB, 1: SB Row per Tile, 2: Tile, 3: Off"),
  .num_tg = ARG_DEF(NULL, "num-tile-groups", 1,
                    "Maximum number of tile groups, default is 1"),
  .mtu_size =
      ARG_DEF(NULL, "mtu-size", 1,
              "MTU size for a tile group, default is 0 (no MTU targeting), "
              "overrides maximum number of tile groups"),
  .timing_info = ARG_DEF_ENUM(
      NULL, "timing-info", 1,
      "Signal timing info in the bitstream (model only works for no "
      "hidden frames, no super-res yet):",
      timing_info_enum),
#if CONFIG_TUNE_VMAF
  .vmaf_model_path =
      ARG_DEF(NULL, "vmaf-model-path", 1, "Path to the VMAF model file"),
#endif
  .partition_info_path = ARG_DEF(NULL, "partition-info-path", 1,
                                 "Partition information read and write path"),
  .enable_rate_guide_deltaq =
      ARG_DEF(NULL, "enable-rate-guide-deltaq", 1,
              "Enable rate guide deltaq (1), by default off (0). "
              "It requires --deltaq-mode=3. "
              "If turned on, it requires an input file specified "
              "by --rate-distribution-info."),
  .rate_distribution_info =
      ARG_DEF(NULL, "rate-distribution-info", 1,
              "Rate distribution information input."
              "It requires --enable-rate-guide-deltaq=1."),
  .film_grain_test = ARG_DEF(
      NULL, "film-grain-test", 1,
      "Film grain test vectors (0: none (default), 1: test-1  2: test-2, "
      "... 16: test-16)"),
  .film_grain_table = ARG_DEF(NULL, "film-grain-table", 1,
                              "Path to file containing film grain parameters"),
#if CONFIG_DENOISE
  .denoise_noise_level =
      ARG_DEF(NULL, "denoise-noise-level", 1,
              "Amount of noise (from 0 = don't denoise, to 50)"),
  .denoise_block_size = ARG_DEF(NULL, "denoise-block-size", 1,
                                "Denoise block size (default = 32)"),
  .enable_dnl_denoising = ARG_DEF(NULL, "enable-dnl-denoising", 1,
                                  "Apply denoising to the frame "
                                  "being encoded when denoise-noise-level is "
                                  "enabled (0: false, 1: true (default))"),
#endif
  .enable_ref_frame_mvs =
      ARG_DEF(NULL, "enable-ref-frame-mvs", 1,
              "Enable temporal mv prediction (default is 1)"),
  .frame_parallel_decoding =
      ARG_DEF(NULL, "frame-parallel", 1,
              "Enable frame parallel decodability features "
              "(0: false (default), 1: true)"),
  .error_resilient_mode = ARG_DEF(NULL, "error-resilient", 1,
                                  "Enable error resilient features "
                                  "(0: false (default), 1: true)"),
  .aq_mode = ARG_DEF(NULL, "aq-mode", 1,
                     "Adaptive quantization mode (0: off (default), 1: "
                     "variance 2: complexity, "
                     "3: cyclic refresh)"),
  .deltaq_mode =
      ARG_DEF(NULL, "deltaq-mode", 1,
              "Delta qindex mode (0: off, 1: deltaq objective (default), "
              "2: deltaq placeholder, 3: key frame visual quality, 4: user "
              "rating based visual quality optimization, 5: HDR video, 6: "
              "Variance Boost all intra); requires --enable-tpl-model=1"),
  .deltaq_strength = ARG_DEF(NULL, "deltaq-strength", 1,
                             "Deltaq strength for"
                             " --deltaq-mode=4 (%)"),
  .deltalf_mode = ARG_DEF(NULL, "delta-lf-mode", 1,
                          "Enable delta-lf-mode (0: off (default), 1: on)"),
  .frame_periodic_boost =
      ARG_DEF(NULL, "frame-boost", 1,
              "Enable frame periodic boost (0: off (default), 1: on)"),
  .gf_cbr_boost_pct = ARG_DEF(NULL, "gf-cbr-boost", 1,
                              "Boost for Golden Frame in CBR mode (pct)"),
  .max_inter_rate_pct =
      ARG_DEF(NULL, "max-inter-rate", 1, "Max P-frame bitrate (pct)"),
  .min_gf_interval = ARG_DEF(
      NULL, "min-gf-interval", 1,
      "Min gf/arf frame interval (default 0, indicating in-built behavior)"),
  .max_gf_interval = ARG_DEF(
      NULL, "max-gf-interval", 1,
      "Max gf/arf frame interval (default 0, indicating in-built behavior)"),
  .gf_min_pyr_height =
      ARG_DEF(NULL, "gf-min-pyr-height", 1,
              "Min height for GF group pyramid structure (0 (default) to 5)"),
  .gf_max_pyr_height = ARG_DEF(
      NULL, "gf-max-pyr-height", 1,
      "Maximum height for GF group pyramid structure (0 to 5 (default))"),
  .max_reference_frames = ARG_DEF(NULL, "max-reference-frames", 1,
                                  "Maximum number of reference frames allowed "
                                  "per frame (3 to 7 (default))"),
  .reduced_reference_set =
      ARG_DEF(NULL, "reduced-reference-set", 1,
              "Use reduced set of single and compound references (0: off "
              "(default), 1: on)"),
  .target_seq_level_idx =
      ARG_DEF(NULL, "target-seq-level-idx", 1,
              "Target sequence level index. "
              "Possible values are in the form of \"ABxy\". "
              "AB: Operating point (OP) index, "
              "xy: Target level index for the OP. "
              "E.g. \"0\" means target level index 0 (2.0) for the 0th OP, "
              "\"1019\" means target level index 19 (6.3) for the 10th OP."),
  .set_min_cr = ARG_DEF(
      NULL, "min-cr", 1,
      "Set minimum compression ratio. Take integer values. Default is 0. "
      "If non-zero, encoder will try to keep the compression ratio of "
      "each frame to be higher than the given value divided by 100."),

  .input_color_primaries = ARG_DEF_ENUM(
      NULL, "color-primaries", 1,
      "Color primaries (CICP) of input content:", color_primaries_enum),

  .input_transfer_characteristics =
      ARG_DEF_ENUM(NULL, "transfer-characteristics", 1,
                   "Transfer characteristics (CICP) of input content:",
                   transfer_characteristics_enum),

  .input_matrix_coefficients = ARG_DEF_ENUM(
      NULL, "matrix-coefficients", 1,
      "Matrix coefficients (CICP) of input content:", matrix_coefficients_enum),

  .input_chroma_sample_position =
      ARG_DEF_ENUM(NULL, "chroma-sample-position", 1,
                   "The chroma sample position when chroma 4:2:0 is signaled:",
                   chroma_sample_position_enum),

  .tune_content = ARG_DEF_ENUM(NULL, "tune-content", 1, "Tune content type",
                               tune_content_enum),

  .cdf_update_mode =
      ARG_DEF(NULL, "cdf-update-mode", 1,
              "CDF update mode for entropy coding "
              "(0: no CDF update, 1: update CDF on all frames (default), "
              "2: selectively update CDF on some frames)"),

  .superblock_size = ARG_DEF_ENUM(NULL, "sb-size", 1, "Superblock size to use",
                                  superblock_size_enum),

  .set_tier_mask =
      ARG_DEF(NULL, "set-tier-mask", 1,
              "Set bit mask to specify which tier each of the 32 possible "
              "operating points conforms to. "
              "Bit value 0 (default): Main Tier, 1: High Tier."),

  .use_fixed_qp_offsets =
      ARG_DEF(NULL, "use-fixed-qp-offsets", 1,
              "Enable fixed QP offsets for frames at different levels of the "
              "pyramid. Selected automatically from --cq-level if "
              "--fixed-qp-offsets is not provided. If this option is not "
              "specified (default), offsets are adaptively chosen by the "
              "encoder."),

  .fixed_qp_offsets = ARG_DEF(
      NULL, "fixed-qp-offsets", 1,
      "Set fixed QP offsets for frames at different levels of the "
      "pyramid. Comma-separated list of 5 offsets for keyframe, ALTREF, "
      "and 3 levels of internal alt-refs. If this option is not "
      "specified (default), offsets are adaptively chosen by the "
      "encoder."),

  .vbr_corpus_complexity_lap = ARG_DEF(
      NULL, "vbr-corpus-complexity-lap", 1,
      "Set average corpus complexity per mb for single pass VBR using lap. "
      "(0..10000), default is 0"),

  .fwd_kf_dist = ARG_DEF(NULL, "fwd-kf-dist", -1,
                         "Set distance between forward keyframes. A value of "
                         "-1 (default) means no repetitive forward keyframes."),

  .enable_tx_size_search = ARG_DEF(
      NULL, "enable-tx-size-search", 1,
      "Enable transform size search to find the best size for each block. "
      "If false, transforms always have the largest possible size "
      "(0: false, 1: true (default)). Ignored in non rd pick mode in "
      "real-time coding."),

  .loopfilter_control = ARG_DEF(
      NULL, "loopfilter-control", 1,
      "Control loop filtering "
      "(0: Loopfilter disabled for all frames, 1: Enable loopfilter for all "
      "frames (default), 2: Disable loopfilter for non-reference frames, 3: "
      "Disable loopfilter for frames with low motion)"),

  .auto_intra_tools_off =
      ARG_DEF(NULL, "auto-intra-tools-off", 1,
              "Automatically turn off several intra coding tools for all intra "
              "mode; only in effect if --deltaq-mode=3"),

  .two_pass_input =
      ARG_DEF(NULL, "two-pass-input", 1,
              "The input file for the second pass for three-pass encoding"),
  .two_pass_output = ARG_DEF(
      NULL, "two-pass-output", 1,
      "The output file for the first two passes for three-pass encoding"),
  .two_pass_width =
      ARG_DEF(NULL, "two-pass-width", 1, "The width of two-pass-input"),
  .two_pass_height =
      ARG_DEF(NULL, "two-pass-height", 1, "The height of two-pass-input"),
  .second_pass_log =
      ARG_DEF("spf", "second-pass-log", 1, "Log file from second pass"),
  .strict_level_conformance =
      ARG_DEF(NULL, "strict-level-conformance", 1,
              "When set to 1, exit the encoder when it fails to encode "
              "to a given target level"),
  .kf_max_pyr_height = ARG_DEF(
      NULL, "kf-max-pyr-height", 1,
      "Maximum height of pyramid structure used for the GOP starting with a "
      "key frame (-1 to 5). When set to -1 (default), it does not have any "
      "effect. The actual maximum pyramid height will be the minimum of this "
      "value and the value of gf_max_pyr_height."),
  .sb_qp_sweep =
      ARG_DEF(NULL, "sb-qp-sweep", 1,
              "When set to 1, enable the superblock level qp sweep for a "
              "given lambda to minimize the rdcost."),
  .enable_low_complexity_decode =
      ARG_DEF(NULL, "enable-low-complexity-decode", 1,
              "Enable low complexity decode (0: false (default), 1: true)"),
#endif  // CONFIG_AV1_ENCODER
};
