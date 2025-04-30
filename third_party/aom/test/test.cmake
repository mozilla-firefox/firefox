#
# Copyright (c) 2017, Alliance for Open Media. All rights reserved.
#
# This source code is subject to the terms of the BSD 2 Clause License and the
# Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License was
# not distributed with this source code in the LICENSE file, you can obtain it
# at www.aomedia.org/license/software. If the Alliance for Open Media Patent
# License 1.0 was not distributed with this source code in the PATENTS file, you
# can obtain it at www.aomedia.org/license/patent.
#
if(AOM_TEST_TEST_CMAKE_)
  return()
endif() # AOM_TEST_TEST_CMAKE_
set(AOM_TEST_TEST_CMAKE_ 1)

include(ProcessorCount)

include("${AOM_ROOT}/test/test_data_util.cmake")

set(AOM_UNIT_TEST_DATA_LIST_FILE "${AOM_ROOT}/test/test-data.sha1")
set(AOM_IDE_TEST_FOLDER "test")
set(AOM_IDE_TESTDATA_FOLDER "testdata")

# Appends |AOM_TEST_SOURCE_VARS| with |src_list_name| at the caller's scope.
# This collects all variables containing libaom test source files.
function(add_to_libaom_test_srcs src_list_name)
  list(APPEND AOM_TEST_SOURCE_VARS ${src_list_name})
  set(AOM_TEST_SOURCE_VARS "${AOM_TEST_SOURCE_VARS}" PARENT_SCOPE)
endfunction()

list(APPEND AOM_UNIT_TEST_WRAPPER_SOURCES "${AOM_ROOT}/test/test_libaom.cc")
add_to_libaom_test_srcs(AOM_UNIT_TEST_WRAPPER_SOURCES)

list(APPEND AOM_UNIT_TEST_COMMON_SOURCES
            "${AOM_ROOT}/test/acm_random.h"
            "${AOM_ROOT}/test/aom_image_test.cc"
            "${AOM_ROOT}/test/aom_integer_test.cc"
            "${AOM_ROOT}/test/av1_config_test.cc"
            "${AOM_ROOT}/test/av1_key_value_api_test.cc"
            "${AOM_ROOT}/test/block_test.cc"
            "${AOM_ROOT}/test/codec_factory.h"
            "${AOM_ROOT}/test/function_equivalence_test.h"
            "${AOM_ROOT}/test/log2_test.cc"
            "${AOM_ROOT}/test/md5_helper.h"
            "${AOM_ROOT}/test/register_state_check.h"
            "${AOM_ROOT}/test/test_vectors.cc"
            "${AOM_ROOT}/test/test_vectors.h"
            "${AOM_ROOT}/test/transform_test_base.h"
            "${AOM_ROOT}/test/util.h"
            "${AOM_ROOT}/test/video_source.h")
add_to_libaom_test_srcs(AOM_UNIT_TEST_COMMON_SOURCES)

list(APPEND AOM_UNIT_TEST_DECODER_SOURCES "${AOM_ROOT}/test/decode_api_test.cc"
            "${AOM_ROOT}/test/decode_scalability_test.cc"
            "${AOM_ROOT}/test/external_frame_buffer_test.cc"
            "${AOM_ROOT}/test/invalid_file_test.cc"
            "${AOM_ROOT}/test/test_vector_test.cc"
            "${AOM_ROOT}/test/ivf_video_source.h")
add_to_libaom_test_srcs(AOM_UNIT_TEST_DECODER_SOURCES)

list(APPEND AOM_UNIT_TEST_ENCODER_SOURCES
            "${AOM_ROOT}/test/active_map_test.cc"
            "${AOM_ROOT}/test/aq_segment_test.cc"
            "${AOM_ROOT}/test/av1_external_partition_test.cc"
            "${AOM_ROOT}/test/avif_progressive_test.cc"
            "${AOM_ROOT}/test/borders_test.cc"
            "${AOM_ROOT}/test/cpu_speed_test.cc"
            "${AOM_ROOT}/test/cpu_used_firstpass_test.cc"
            "${AOM_ROOT}/test/datarate_test.cc"
            "${AOM_ROOT}/test/datarate_test.h"
            "${AOM_ROOT}/test/deltaq_mode_test.cc"
            "${AOM_ROOT}/test/dropframe_encode_test.cc"
            "${AOM_ROOT}/test/svc_datarate_test.cc"
            "${AOM_ROOT}/test/encode_api_test.cc"
            "${AOM_ROOT}/test/encode_small_width_height_test.cc"
            "${AOM_ROOT}/test/encode_test_driver.cc"
            "${AOM_ROOT}/test/encode_test_driver.h"
            "${AOM_ROOT}/test/end_to_end_psnr_test.cc"
            "${AOM_ROOT}/test/forced_max_frame_width_height_test.cc"
            "${AOM_ROOT}/test/force_key_frame_test.cc"
            "${AOM_ROOT}/test/gf_pyr_height_test.cc"
            "${AOM_ROOT}/test/rt_end_to_end_test.cc"
            "${AOM_ROOT}/test/allintra_end_to_end_test.cc"
            "${AOM_ROOT}/test/loopfilter_control_test.cc"
            "${AOM_ROOT}/test/frame_size_tests.cc"
            "${AOM_ROOT}/test/horz_superres_test.cc"
            "${AOM_ROOT}/test/i420_video_source.h"
            "${AOM_ROOT}/test/level_test.cc"
            "${AOM_ROOT}/test/monochrome_test.cc"
            "${AOM_ROOT}/test/postproc_filters_test.cc"
            "${AOM_ROOT}/test/resize_test.cc"
            "${AOM_ROOT}/test/scalability_test.cc"
            "${AOM_ROOT}/test/sharpness_test.cc"
            "${AOM_ROOT}/test/y4m_test.cc"
            "${AOM_ROOT}/test/y4m_video_source.h"
            "${AOM_ROOT}/test/yuv_video_source.h"
            "${AOM_ROOT}/test/time_stamp_test.cc")
add_to_libaom_test_srcs(AOM_UNIT_TEST_ENCODER_SOURCES)

list(APPEND AOM_ENCODE_PERF_TEST_SOURCES "${AOM_ROOT}/test/encode_perf_test.cc")
list(APPEND AOM_UNIT_TEST_WEBM_SOURCES "${AOM_ROOT}/test/webm_video_source.h")
add_to_libaom_test_srcs(AOM_UNIT_TEST_WEBM_SOURCES)
list(APPEND AOM_TEST_INTRA_PRED_SPEED_SOURCES
            "${AOM_ROOT}/test/test_intra_pred_speed.cc")

if(CONFIG_AV1_DECODER)
  list(APPEND AOM_UNIT_TEST_COMMON_SOURCES
              "${AOM_ROOT}/test/decode_test_driver.cc"
              "${AOM_ROOT}/test/decode_test_driver.h")
endif()

if(CONFIG_INTERNAL_STATS AND CONFIG_AV1_HIGHBITDEPTH)
  list(APPEND AOM_UNIT_TEST_COMMON_SOURCES
              "${AOM_ROOT}/test/hbd_metrics_test.cc")
endif()

list(APPEND AOM_DECODE_PERF_TEST_SOURCES "${AOM_ROOT}/test/decode_perf_test.cc")

if(CONFIG_REALTIME_ONLY)
  list(REMOVE_ITEM AOM_UNIT_TEST_ENCODER_SOURCES
                   "${AOM_ROOT}/test/allintra_end_to_end_test.cc"
                   "${AOM_ROOT}/test/av1_external_partition_test.cc"
                   "${AOM_ROOT}/test/avif_progressive_test.cc"
                   "${AOM_ROOT}/test/borders_test.cc"
                   "${AOM_ROOT}/test/cpu_speed_test.cc"
                   "${AOM_ROOT}/test/cpu_used_firstpass_test.cc"
                   "${AOM_ROOT}/test/deltaq_mode_test.cc"
                   "${AOM_ROOT}/test/dropframe_encode_test.cc"
                   "${AOM_ROOT}/test/end_to_end_psnr_test.cc"
                   "${AOM_ROOT}/test/force_key_frame_test.cc"
                   "${AOM_ROOT}/test/gf_pyr_height_test.cc"
                   "${AOM_ROOT}/test/horz_superres_test.cc"
                   "${AOM_ROOT}/test/level_test.cc"
                   "${AOM_ROOT}/test/postproc_filters_test.cc"
                   "${AOM_ROOT}/test/sharpness_test.cc")
endif()

if(NOT BUILD_SHARED_LIBS)
  list(APPEND AOM_UNIT_TEST_COMMON_SOURCES
              "${AOM_ROOT}/test/aom_mem_test.cc"
              "${AOM_ROOT}/test/av1_common_int_test.cc"
              "${AOM_ROOT}/test/av1_scale_test.cc"
              "${AOM_ROOT}/test/cdef_test.cc"
              "${AOM_ROOT}/test/cfl_test.cc"
              "${AOM_ROOT}/test/convolve_test.cc"
              "${AOM_ROOT}/test/hiprec_convolve_test.cc"
              "${AOM_ROOT}/test/hiprec_convolve_test_util.cc"
              "${AOM_ROOT}/test/hiprec_convolve_test_util.h"
              "${AOM_ROOT}/test/intrabc_test.cc"
              "${AOM_ROOT}/test/intrapred_test.cc"
              "${AOM_ROOT}/test/lpf_test.cc"
              "${AOM_ROOT}/test/scan_test.cc"
              "${AOM_ROOT}/test/selfguided_filter_test.cc"
              "${AOM_ROOT}/test/simd_cmp_impl.inc"
              "${AOM_ROOT}/test/simd_impl.h")

  if(CONFIG_REALTIME_ONLY AND NOT CONFIG_AV1_DECODER)
    list(REMOVE_ITEM AOM_UNIT_TEST_COMMON_SOURCES "${AOM_ROOT}/test/cfl_test.cc"
                     "${AOM_ROOT}/test/hiprec_convolve_test.cc"
                     "${AOM_ROOT}/test/hiprec_convolve_test_util.cc"
                     "${AOM_ROOT}/test/hiprec_convolve_test_util.h")
  endif()

  if(HAVE_SSE2)
    list(APPEND AOM_UNIT_TEST_COMMON_INTRIN_SSE2
                "${AOM_ROOT}/test/simd_cmp_sse2.cc")
    add_to_libaom_test_srcs(AOM_UNIT_TEST_COMMON_INTRIN_SSE2)
  endif()

  if(HAVE_SSSE3)
    list(APPEND AOM_UNIT_TEST_COMMON_INTRIN_SSSE3
                "${AOM_ROOT}/test/simd_cmp_ssse3.cc")
    add_to_libaom_test_srcs(AOM_UNIT_TEST_COMMON_INTRIN_SSSE3)
  endif()

  if(HAVE_SSE4_1)
    list(APPEND AOM_UNIT_TEST_COMMON_INTRIN_SSE4_1
                "${AOM_ROOT}/test/simd_cmp_sse4.cc")
    add_to_libaom_test_srcs(AOM_UNIT_TEST_COMMON_INTRIN_SSE4_1)
  endif()

  if(HAVE_AVX2)
    list(APPEND AOM_UNIT_TEST_COMMON_INTRIN_AVX2
                "${AOM_ROOT}/test/simd_cmp_avx2.cc")
    add_to_libaom_test_srcs(AOM_UNIT_TEST_COMMON_INTRIN_AVX2)
  endif()

  list(APPEND AOM_UNIT_TEST_ENCODER_SOURCES
              "${AOM_ROOT}/test/arf_freq_test.cc"
              "${AOM_ROOT}/test/av1_convolve_test.cc"
              "${AOM_ROOT}/test/av1_fwd_txfm1d_test.cc"
              "${AOM_ROOT}/test/av1_fwd_txfm2d_test.cc"
              "${AOM_ROOT}/test/av1_get_qmlevel_test.cc"
              "${AOM_ROOT}/test/av1_inv_txfm1d_test.cc"
              "${AOM_ROOT}/test/av1_inv_txfm2d_test.cc"
              "${AOM_ROOT}/test/av1_k_means_test.cc"
              "${AOM_ROOT}/test/av1_nn_predict_test.cc"
              "${AOM_ROOT}/test/av1_round_shift_array_test.cc"
              "${AOM_ROOT}/test/av1_softmax_test.cc"
              "${AOM_ROOT}/test/av1_txfm_test.cc"
              "${AOM_ROOT}/test/av1_txfm_test.h"
              "${AOM_ROOT}/test/av1_wedge_utils_test.cc"
              "${AOM_ROOT}/test/avg_test.cc"
              "${AOM_ROOT}/test/blend_a64_mask_1d_test.cc"
              "${AOM_ROOT}/test/blend_a64_mask_test.cc"
              "${AOM_ROOT}/test/comp_mask_pred_test.cc"
              "${AOM_ROOT}/test/disflow_test.cc"
              "${AOM_ROOT}/test/encodemb_test.cc"
              "${AOM_ROOT}/test/encodetxb_test.cc"
              "${AOM_ROOT}/test/end_to_end_qmpsnr_test.cc"
              "${AOM_ROOT}/test/end_to_end_ssim_test.cc"
              "${AOM_ROOT}/test/error_block_test.cc"
              "${AOM_ROOT}/test/fdct4x4_test.cc"
              "${AOM_ROOT}/test/fft_test.cc"
              "${AOM_ROOT}/test/firstpass_test.cc"
              "${AOM_ROOT}/test/frame_resize_test.cc"
              "${AOM_ROOT}/test/fwht4x4_test.cc"
              "${AOM_ROOT}/test/hadamard_test.cc"
              "${AOM_ROOT}/test/horver_correlation_test.cc"
              "${AOM_ROOT}/test/masked_sad_test.cc"
              "${AOM_ROOT}/test/masked_variance_test.cc"
              "${AOM_ROOT}/test/metadata_test.cc"
              "${AOM_ROOT}/test/minmax_test.cc"
              "${AOM_ROOT}/test/motion_vector_test.cc"
              "${AOM_ROOT}/test/mv_cost_test.cc"
              "${AOM_ROOT}/test/obmc_sad_test.cc"
              "${AOM_ROOT}/test/obmc_variance_test.cc"
              "${AOM_ROOT}/test/pickrst_test.cc"
              "${AOM_ROOT}/test/reconinter_test.cc"
              "${AOM_ROOT}/test/sad_test.cc"
              "${AOM_ROOT}/test/subtract_test.cc"
              "${AOM_ROOT}/test/sum_squares_test.cc"
              "${AOM_ROOT}/test/sse_sum_test.cc"
              "${AOM_ROOT}/test/variance_test.cc"
              "${AOM_ROOT}/test/warp_filter_test.cc"
              "${AOM_ROOT}/test/warp_filter_test_util.cc"
              "${AOM_ROOT}/test/warp_filter_test_util.h"
              "${AOM_ROOT}/test/webmenc_test.cc"
              "${AOM_ROOT}/test/wiener_test.cc")

  if(NOT CONFIG_REALTIME_ONLY)
    list(APPEND AOM_UNIT_TEST_ENCODER_INTRIN_SSE4_1
                "${AOM_ROOT}/test/corner_match_test.cc")
  endif()

  if(CONFIG_ACCOUNTING)
    list(APPEND AOM_UNIT_TEST_COMMON_SOURCES
                "${AOM_ROOT}/test/accounting_test.cc")
  endif()

  if(CONFIG_AV1_DECODER AND CONFIG_AV1_ENCODER)
    list(APPEND AOM_UNIT_TEST_COMMON_SOURCES
                "${AOM_ROOT}/test/altref_test.cc"
                "${AOM_ROOT}/test/av1_encoder_parms_get_to_decoder.cc"
                "${AOM_ROOT}/test/av1_ext_tile_test.cc"
                "${AOM_ROOT}/test/binary_codes_test.cc"
                "${AOM_ROOT}/test/boolcoder_test.cc"
                "${AOM_ROOT}/test/cnn_test.cc"
                "${AOM_ROOT}/test/decode_multithreaded_test.cc"
                "${AOM_ROOT}/test/divu_small_test.cc"
                "${AOM_ROOT}/test/dr_prediction_test.cc"
                "${AOM_ROOT}/test/ec_test.cc"
                "${AOM_ROOT}/test/error_resilience_test.cc"
                "${AOM_ROOT}/test/ethread_test.cc"
                "${AOM_ROOT}/test/film_grain_table_test.cc"
                "${AOM_ROOT}/test/kf_test.cc"
                "${AOM_ROOT}/test/lossless_test.cc"
                "${AOM_ROOT}/test/noise_model_test.cc"
                "${AOM_ROOT}/test/quant_test.cc"
                "${AOM_ROOT}/test/rd_test.cc"
                "${AOM_ROOT}/test/sb_multipass_test.cc"
                "${AOM_ROOT}/test/sb_qp_sweep_test.cc"
                "${AOM_ROOT}/test/screen_content_test.cc"
                "${AOM_ROOT}/test/segment_binarization_sync.cc"
                "${AOM_ROOT}/test/still_picture_test.cc"
                "${AOM_ROOT}/test/temporal_filter_test.cc"
                "${AOM_ROOT}/test/tile_config_test.cc"
                "${AOM_ROOT}/test/tile_independence_test.cc"
                "${AOM_ROOT}/test/tpl_model_test.cc")
    if(CONFIG_AV1_HIGHBITDEPTH)
      list(APPEND AOM_UNIT_TEST_COMMON_SOURCES
                  "${AOM_ROOT}/test/coding_path_sync.cc")
    endif()
  endif()
  if(CONFIG_REALTIME_ONLY)
    list(REMOVE_ITEM AOM_UNIT_TEST_COMMON_SOURCES
                     "${AOM_ROOT}/test/altref_test.cc"
                     "${AOM_ROOT}/test/av1_encoder_parms_get_to_decoder.cc"
                     "${AOM_ROOT}/test/av1_ext_tile_test.cc"
                     "${AOM_ROOT}/test/binary_codes_test.cc"
                     "${AOM_ROOT}/test/cnn_test.cc"
                     "${AOM_ROOT}/test/decode_multithreaded_test.cc"
                     "${AOM_ROOT}/test/error_resilience_test.cc"
                     "${AOM_ROOT}/test/film_grain_table_test.cc"
                     "${AOM_ROOT}/test/kf_test.cc"
                     "${AOM_ROOT}/test/lossless_test.cc"
                     "${AOM_ROOT}/test/noise_model_test.cc"
                     "${AOM_ROOT}/test/sb_multipass_test.cc"
                     "${AOM_ROOT}/test/sb_qp_sweep_test.cc"
                     "${AOM_ROOT}/test/selfguided_filter_test.cc"
                     "${AOM_ROOT}/test/screen_content_test.cc"
                     "${AOM_ROOT}/test/still_picture_test.cc"
                     "${AOM_ROOT}/test/tile_independence_test.cc"
                     "${AOM_ROOT}/test/tpl_model_test.cc")
  endif()

  if(CONFIG_FPMT_TEST AND (NOT CONFIG_REALTIME_ONLY))
    list(APPEND AOM_UNIT_TEST_COMMON_SOURCES
                "${AOM_ROOT}/test/frame_parallel_enc_test.cc")
  endif()

  if(HAVE_SSE2)
    list(APPEND AOM_UNIT_TEST_COMMON_SOURCES
                "${AOM_ROOT}/test/simd_sse2_test.cc")
  endif()

  if(HAVE_SSSE3)
    list(APPEND AOM_UNIT_TEST_COMMON_SOURCES
                "${AOM_ROOT}/test/simd_ssse3_test.cc")
  endif()

  if(HAVE_SSE4_1)
    list(APPEND AOM_UNIT_TEST_COMMON_SOURCES
                "${AOM_ROOT}/test/simd_sse4_test.cc")
  endif()

  if(HAVE_SSE4_1 OR HAVE_NEON)
    list(APPEND AOM_UNIT_TEST_COMMON_SOURCES
                "${AOM_ROOT}/test/filterintra_test.cc")

    list(APPEND AOM_UNIT_TEST_ENCODER_SOURCES
                "${AOM_ROOT}/test/av1_highbd_iht_test.cc")
  endif()

  if(HAVE_AVX2)
    list(APPEND AOM_UNIT_TEST_COMMON_SOURCES
                "${AOM_ROOT}/test/simd_avx2_test.cc")
  endif()

  if(CONFIG_AV1_TEMPORAL_DENOISING AND (HAVE_SSE2 OR HAVE_NEON))
    list(APPEND AOM_UNIT_TEST_ENCODER_SOURCES
                "${AOM_ROOT}/test/av1_temporal_denoiser_test.cc")
  endif()

  if(CONFIG_AV1_HIGHBITDEPTH)
    list(APPEND AOM_UNIT_TEST_ENCODER_INTRIN_SSE4_1
                "${AOM_ROOT}/test/av1_quantize_test.cc")
  endif()

  if(HAVE_SSE2 OR HAVE_NEON)
    list(APPEND AOM_UNIT_TEST_ENCODER_SOURCES
                "${AOM_ROOT}/test/quantize_func_test.cc")
  endif()

  if(HAVE_SSE4_1)
    list(APPEND AOM_UNIT_TEST_ENCODER_SOURCES
                "${AOM_ROOT}/test/av1_convolve_scale_test.cc"
                "${AOM_ROOT}/test/av1_horz_only_frame_superres_test.cc"
                "${AOM_ROOT}/test/intra_edge_test.cc")
  endif()

  if(HAVE_NEON)
    list(APPEND AOM_UNIT_TEST_ENCODER_SOURCES
                "${AOM_ROOT}/test/av1_convolve_scale_test.cc"
                "${AOM_ROOT}/test/av1_horz_only_frame_superres_test.cc"
                "${AOM_ROOT}/test/intra_edge_test.cc")
  endif()

  if(HAVE_SSE4_2 OR HAVE_ARM_CRC32)
    list(APPEND AOM_UNIT_TEST_ENCODER_SOURCES "${AOM_ROOT}/test/hash_test.cc")
  endif()

  if(CONFIG_REALTIME_ONLY)
    list(REMOVE_ITEM AOM_UNIT_TEST_ENCODER_SOURCES
                     "${AOM_ROOT}/test/disflow_test.cc"
                     "${AOM_ROOT}/test/end_to_end_qmpsnr_test.cc"
                     "${AOM_ROOT}/test/end_to_end_ssim_test.cc"
                     "${AOM_ROOT}/test/firstpass_test.cc"
                     "${AOM_ROOT}/test/motion_vector_test.cc"
                     "${AOM_ROOT}/test/obmc_sad_test.cc"
                     "${AOM_ROOT}/test/obmc_variance_test.cc"
                     "${AOM_ROOT}/test/pickrst_test.cc"
                     "${AOM_ROOT}/test/warp_filter_test.cc"
                     "${AOM_ROOT}/test/warp_filter_test_util.cc"
                     "${AOM_ROOT}/test/warp_filter_test_util.h"
                     "${AOM_ROOT}/test/wiener_test.cc")
  endif()
endif()

if(ENABLE_EXAMPLES)
  list(APPEND AOM_UNIT_TEST_ENCODER_SOURCES
              "${AOM_ROOT}/test/multilayer_metadata_test.cc")
endif()

if(CONFIG_AV1_ENCODER AND ENABLE_TESTS)
  list(APPEND AOM_RC_TEST_SOURCES "${AOM_ROOT}/test/codec_factory.h"
              "${AOM_ROOT}/test/decode_test_driver.cc"
              "${AOM_ROOT}/test/decode_test_driver.h"
              "${AOM_ROOT}/test/encode_test_driver.cc"
              "${AOM_ROOT}/test/encode_test_driver.h"
              "${AOM_ROOT}/test/i420_video_source.h"
              "${AOM_ROOT}/test/ratectrl_rtc_test.cc"
              "${AOM_ROOT}/test/test_aom_rc.cc" "${AOM_ROOT}/test/util.h")
  if(CONFIG_THREE_PASS)
    # Add the dependencies of "${AOM_ROOT}/common/ivfdec.c".
    list(APPEND AOM_RC_TEST_SOURCES "${AOM_ROOT}/common/tools_common.c"
                "${AOM_ROOT}/common/tools_common.h"
                "${AOM_GEN_SRC_DIR}/usage_exit.c")
  endif()
endif()

if(ENABLE_TESTS)
  if(BUILD_SHARED_LIBS AND APPLE) # Silence an RPATH warning.
    set(CMAKE_MACOSX_RPATH 1)
  endif()

  add_library(
    aom_gtest STATIC
    "${AOM_ROOT}/third_party/googletest/src/googletest/src/gtest-all.cc")
  set_property(TARGET aom_gtest PROPERTY FOLDER ${AOM_IDE_TEST_FOLDER})
  # There are -Wundef warnings in the gtest headers. Tell the compiler to treat
  # the gtest include directories as system include directories and suppress
  # compiler warnings in the gtest headers.
  target_include_directories(
    aom_gtest SYSTEM
    PUBLIC "${AOM_ROOT}/third_party/googletest/src/googletest/include"
    PRIVATE "${AOM_ROOT}/third_party/googletest/src/googletest")

  # The definition of GTEST_HAS_PTHREAD must be public, since it's checked by
  # interface headers, not just by the implementation.
  if(NOT (MSVC OR WIN32))
    if(CONFIG_MULTITHREAD AND CMAKE_USE_PTHREADS_INIT)
      target_compile_definitions(aom_gtest PUBLIC GTEST_HAS_PTHREAD=1)
    else()
      target_compile_definitions(aom_gtest PUBLIC GTEST_HAS_PTHREAD=0)
    endif()
  endif()
endif()

# Setup testdata download targets, test build targets, and test run targets. The
# libaom and app util targets must exist before this function is called.
function(setup_aom_test_targets)

  # TODO(tomfinegan): Build speed optimization. $AOM_UNIT_TEST_COMMON_SOURCES
  # and $AOM_UNIT_TEST_ENCODER_SOURCES are very large. The build of test targets
  # could be sped up (on multicore build machines) by compiling sources in each
  # list into separate object library targets, and then linking them into
  # test_libaom.
  add_library(test_aom_common OBJECT ${AOM_UNIT_TEST_COMMON_SOURCES})
  set_property(TARGET test_aom_common PROPERTY FOLDER ${AOM_IDE_TEST_FOLDER})
  add_dependencies(test_aom_common aom)
  target_link_libraries(test_aom_common ${AOM_LIB_LINK_TYPE} aom_gtest)

  if(CONFIG_AV1_DECODER)
    add_library(test_aom_decoder OBJECT ${AOM_UNIT_TEST_DECODER_SOURCES})
    set_property(TARGET test_aom_decoder PROPERTY FOLDER ${AOM_IDE_TEST_FOLDER})
    add_dependencies(test_aom_decoder aom)
    target_link_libraries(test_aom_decoder ${AOM_LIB_LINK_TYPE} aom_gtest)
  endif()

  if(CONFIG_AV1_ENCODER)
    add_library(test_aom_encoder OBJECT ${AOM_UNIT_TEST_ENCODER_SOURCES})
    set_property(TARGET test_aom_encoder PROPERTY FOLDER ${AOM_IDE_TEST_FOLDER})
    add_dependencies(test_aom_encoder aom)
    target_link_libraries(test_aom_encoder ${AOM_LIB_LINK_TYPE} aom_gtest)
  endif()

  add_executable(test_libaom ${AOM_UNIT_TEST_WRAPPER_SOURCES}
                             $<TARGET_OBJECTS:aom_common_app_util>
                             $<TARGET_OBJECTS:aom_usage_exit>
                             $<TARGET_OBJECTS:test_aom_common>)
  set_property(TARGET test_libaom PROPERTY FOLDER ${AOM_IDE_TEST_FOLDER})
  list(APPEND AOM_APP_TARGETS test_libaom)

  if(CONFIG_AV1_DECODER)
    target_sources(test_libaom PRIVATE $<TARGET_OBJECTS:aom_decoder_app_util>
                   $<TARGET_OBJECTS:test_aom_decoder>)

    if(ENABLE_DECODE_PERF_TESTS AND CONFIG_WEBM_IO)
      target_sources(test_libaom PRIVATE ${AOM_DECODE_PERF_TEST_SOURCES})
    endif()
  endif()

  if(CONFIG_AV1_ENCODER)
    target_sources(test_libaom PRIVATE $<TARGET_OBJECTS:test_aom_encoder>
                   $<TARGET_OBJECTS:aom_encoder_app_util>)

    if(ENABLE_ENCODE_PERF_TESTS)
      target_sources(test_libaom PRIVATE ${AOM_ENCODE_PERF_TEST_SOURCES})
    endif()

    if(NOT BUILD_SHARED_LIBS)
      add_executable(test_intra_pred_speed ${AOM_TEST_INTRA_PRED_SPEED_SOURCES}
                                           $<TARGET_OBJECTS:aom_common_app_util>
                                           $<TARGET_OBJECTS:aom_usage_exit>)
      set_property(TARGET test_intra_pred_speed
                   PROPERTY FOLDER ${AOM_IDE_TEST_FOLDER})
      target_link_libraries(test_intra_pred_speed ${AOM_LIB_LINK_TYPE} aom
                            aom_gtest)
      list(APPEND AOM_APP_TARGETS test_intra_pred_speed)
    endif()
  endif()

  if(CONFIG_LIBYUV)
    # link test_libaom with yuv
    target_link_libraries(test_libaom ${AOM_LIB_LINK_TYPE} aom aom_gtest yuv)
  else()
    # do not link test_libaom with yuv
    target_link_libraries(test_libaom ${AOM_LIB_LINK_TYPE} aom aom_gtest)
  endif()

  if(CONFIG_WEBM_IO)
    target_sources(test_libaom PRIVATE $<TARGET_OBJECTS:webm>)
  endif()
  if(HAVE_SSE2)
    add_intrinsics_source_to_target("-msse2" "test_libaom"
                                    "AOM_UNIT_TEST_COMMON_INTRIN_SSE2")
  endif()
  if(HAVE_SSSE3)
    add_intrinsics_source_to_target("-mssse3" "test_libaom"
                                    "AOM_UNIT_TEST_COMMON_INTRIN_SSSE3")
  endif()
  if(HAVE_SSE4_1)
    add_intrinsics_source_to_target("-msse4.1" "test_libaom"
                                    "AOM_UNIT_TEST_COMMON_INTRIN_SSE4_1")
    if(CONFIG_AV1_ENCODER)
      if(AOM_UNIT_TEST_ENCODER_INTRIN_SSE4_1)
        add_intrinsics_source_to_target("-msse4.1" "test_libaom"
                                        "AOM_UNIT_TEST_ENCODER_INTRIN_SSE4_1")
      endif()
    endif()
  endif()
  if(HAVE_AVX2)
    add_intrinsics_source_to_target("-mavx2" "test_libaom"
                                    "AOM_UNIT_TEST_COMMON_INTRIN_AVX2")
  endif()
  if(HAVE_NEON)
    add_intrinsics_source_to_target("${AOM_NEON_INTRIN_FLAG}" "test_libaom"
                                    "AOM_UNIT_TEST_COMMON_INTRIN_NEON")
  endif()

  if(ENABLE_TESTDATA)
    make_test_data_lists("${AOM_UNIT_TEST_DATA_LIST_FILE}" test_files
                         test_file_checksums)
    list(LENGTH test_files num_test_files)
    list(LENGTH test_file_checksums num_test_file_checksums)

    math(EXPR max_file_index "${num_test_files} - 1")
    foreach(test_index RANGE ${max_file_index})
      list(GET test_files ${test_index} test_file)
      list(GET test_file_checksums ${test_index} test_file_checksum)
      add_custom_target(
        testdata_${test_index}
        COMMAND ${CMAKE_COMMAND}
                -DAOM_CONFIG_DIR="${AOM_CONFIG_DIR}" -DAOM_ROOT="${AOM_ROOT}"
                -DAOM_TEST_FILE="${test_file}"
                -DAOM_TEST_CHECKSUM=${test_file_checksum} -P
                "${AOM_ROOT}/test/test_data_download_worker.cmake")
      set_property(TARGET testdata_${test_index}
                   PROPERTY FOLDER ${AOM_IDE_TESTDATA_FOLDER})
      list(APPEND testdata_targets testdata_${test_index})
    endforeach()

    # Create a custom build target for running each test data download target.
    add_custom_target(testdata)
    add_dependencies(testdata ${testdata_targets})
    set_property(TARGET testdata PROPERTY FOLDER ${AOM_IDE_TESTDATA_FOLDER})

    # Skip creation of test run targets when generating for Visual Studio and
    # Xcode unless the user explicitly requests IDE test hosting. This is done
    # to make build cycles in the IDE tolerable when the IDE command for build
    # project is used to build AOM. Default behavior in IDEs is to build all
    # targets, and the test run takes hours.
    if(((NOT MSVC) AND (NOT XCODE)) OR ENABLE_IDE_TEST_HOSTING)

      # Pick a reasonable number of targets (this controls parallelization).
      processorcount(num_test_targets)
      if(num_test_targets EQUAL 0) # Just default to 10 targets when there's no
                                   # processor count available.
        set(num_test_targets 10)
      endif()

      math(EXPR max_shard_index "${num_test_targets} - 1")
      foreach(shard_index RANGE ${max_shard_index})
        set(test_name "test_${shard_index}")
        add_custom_target(${test_name}
                          COMMAND ${CMAKE_COMMAND}
                                  -DGTEST_SHARD_INDEX=${shard_index}
                                  -DGTEST_TOTAL_SHARDS=${num_test_targets}
                                  -DTEST_LIBAOM=$<TARGET_FILE:test_libaom> -P
                                  "${AOM_ROOT}/test/test_runner.cmake"
                          DEPENDS testdata test_libaom)
        set_property(TARGET ${test_name} PROPERTY FOLDER ${AOM_IDE_TEST_FOLDER})
        list(APPEND test_targets ${test_name})
      endforeach()
      add_custom_target(runtests)
      set_property(TARGET runtests PROPERTY FOLDER ${AOM_IDE_TEST_FOLDER})
      add_dependencies(runtests ${test_targets})
    endif()
  endif()

  # Libaom_test_srcs.txt generation.
  set(libaom_test_srcs_txt_file "${AOM_CONFIG_DIR}/libaom_test_srcs.txt")
  file(WRITE "${libaom_test_srcs_txt_file}"
       "# This file is generated. DO NOT EDIT.\n")

  # Static source file list first.
  list(SORT AOM_TEST_SOURCE_VARS)
  foreach(aom_test_source_var ${AOM_TEST_SOURCE_VARS})
    if("${aom_test_source_var}" STREQUAL "${last_aom_test_source_var}")
      message(
        FATAL_ERROR
          "Duplicate AOM_TEST_SOURCE_VARS entry: ${aom_test_source_var}")
    endif()
    foreach(file ${${aom_test_source_var}})
      if(NOT "${file}" MATCHES "${AOM_CONFIG_DIR}")
        string(REPLACE "${AOM_ROOT}/" "" file "${file}")
        file(APPEND "${libaom_test_srcs_txt_file}" "${file}\n")
      endif()
    endforeach()
    set(last_aom_test_source_var ${aom_test_source_var})
  endforeach()

  # libaom_test_srcs.gni generation
  set(libaom_test_srcs_gni_file "${AOM_CONFIG_DIR}/libaom_test_srcs.gni")
  file(WRITE "${libaom_test_srcs_gni_file}"
       "# This file is generated. DO NOT EDIT.\n")

  foreach(aom_test_source_var ${AOM_TEST_SOURCE_VARS})
    string(TOLOWER "${aom_test_source_var}" aom_test_source_var_lowercase)
    file(APPEND "${libaom_test_srcs_gni_file}"
         "\n${aom_test_source_var_lowercase} = [\n")

    foreach(file ${${aom_test_source_var}})
      if(NOT "${file}" MATCHES "${AOM_CONFIG_DIR}")
        string(REPLACE "${AOM_ROOT}/" "//third_party/libaom/source/libaom/" file
                       "${file}")
        file(APPEND "${libaom_test_srcs_gni_file}" "  \"${file}\",\n")
      endif()
    endforeach()

    file(APPEND "${libaom_test_srcs_gni_file}" "]\n")
  endforeach()

  # Set up test for rc interface
  if(CONFIG_AV1_ENCODER
     AND ENABLE_TESTS
     AND CONFIG_WEBM_IO
     AND NOT CONFIG_REALTIME_ONLY)
    add_executable(test_aom_rc ${AOM_RC_TEST_SOURCES})
    target_link_libraries(test_aom_rc ${AOM_LIB_LINK_TYPE} aom_av1_rc aom_gtest)
    set_property(TARGET test_aom_rc PROPERTY FOLDER ${AOM_IDE_TEST_FOLDER})
    list(APPEND AOM_APP_TARGETS test_aom_rc)
  endif()

  set(AOM_APP_TARGETS ${AOM_APP_TARGETS} PARENT_SCOPE)
endfunction()
