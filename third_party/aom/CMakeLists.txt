#
# Copyright (c) 2016, Alliance for Open Media. All rights reserved.
#
# This source code is subject to the terms of the BSD 2 Clause License and the
# Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License was
# not distributed with this source code in the LICENSE file, you can obtain it
# at www.aomedia.org/license/software. If the Alliance for Open Media Patent
# License 1.0 was not distributed with this source code in the PATENTS file, you
# can obtain it at www.aomedia.org/license/patent.
#
cmake_minimum_required(VERSION 3.16)

set(AOM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(AOM_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}")
if("${AOM_ROOT}" STREQUAL "${AOM_CONFIG_DIR}")
  message(
    FATAL_ERROR "Building from within the aom source tree is not supported.\n"
                "Hint: Run these commands\n"
                "$ rm -rf CMakeCache.txt CMakeFiles\n"
                "$ mkdir -p ../aom_build\n" "$ cd ../aom_build\n"
                "And re-run CMake from the aom_build directory.")
endif()

# VERSION is the release version of the library. It should be updated at the
# same time as the LT_* variables.
project(AOM LANGUAGES C CXX VERSION 3.12.0)

# GENERATED source property global visibility.
if(POLICY CMP0118)
  cmake_policy(SET CMP0118 NEW)
endif()

if(NOT EMSCRIPTEN)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE
        "Release"
        CACHE STRING "Build type: Debug, Release, RelWithDebInfo or MinSizeRel"
              FORCE)
  endif()
endif()

if(MSVC AND MSVC_VERSION LESS 1920)
  message(
    WARNING
      "MSVC versions prior to 2019 (v16) are not supported and may generate"
      " incorrect code!")
endif()

# Library version info. Update LT_CURRENT, LT_REVISION and LT_AGE when making a
# public release by following the guidelines in the libtool document:
# https://www.gnu.org/software/libtool/manual/libtool.html#Updating-version-info
#
# c=<current>, r=<revision>, a=<age>
#
# libtool generates a .so file as .so.[c-a].a.r, while -version-info c:r:a is
# passed to libtool.
#
# We set SO_FILE_VERSION = [c-a].a.r
#
# The VERSION number in project() should be updated when these variables are.
set(LT_CURRENT 15)
set(LT_REVISION 0)
set(LT_AGE 12)
math(EXPR SO_VERSION "${LT_CURRENT} - ${LT_AGE}")
set(SO_FILE_VERSION "${SO_VERSION}.${LT_AGE}.${LT_REVISION}")
unset(LT_CURRENT)
unset(LT_REVISION)
unset(LT_AGE)

# Enable generators like Xcode and Visual Studio to place projects in folders.
set_property(GLOBAL PROPERTY USE_FOLDERS TRUE)

include("${AOM_ROOT}/build/cmake/aom_configure.cmake")
if(CONFIG_THREE_PASS)
  include("${AOM_ROOT}/common/ivf_dec.cmake")
endif()
include("${AOM_ROOT}/aom_dsp/aom_dsp.cmake")
include("${AOM_ROOT}/aom_mem/aom_mem.cmake")
include("${AOM_ROOT}/aom_ports/aom_ports.cmake")
include("${AOM_ROOT}/aom_scale/aom_scale.cmake")
include("${AOM_ROOT}/aom_util/aom_util.cmake")
include("${AOM_ROOT}/av1/av1.cmake")
include("${AOM_ROOT}/build/cmake/aom_install.cmake")
include("${AOM_ROOT}/build/cmake/sanitizers.cmake")
include("${AOM_ROOT}/build/cmake/util.cmake")
include("${AOM_ROOT}/test/test.cmake")

list(APPEND AOM_RTCD_SOURCES
            "${AOM_CONFIG_DIR}/config/aom_dsp_rtcd.h"
            "${AOM_CONFIG_DIR}/config/aom_scale_rtcd.h"
            "${AOM_CONFIG_DIR}/config/av1_rtcd.h"
            "${AOM_ROOT}/aom_dsp/aom_dsp_rtcd_defs.pl"
            "${AOM_ROOT}/aom_dsp/aom_dsp_rtcd.c"
            "${AOM_ROOT}/aom_scale/aom_scale_rtcd.pl"
            "${AOM_ROOT}/aom_scale/aom_scale_rtcd.c"
            "${AOM_ROOT}/av1/common/av1_rtcd_defs.pl"
            "${AOM_ROOT}/av1/common/av1_rtcd.c"
            "${AOM_ROOT}/build/cmake/rtcd.pl")

list(APPEND AOM_LIBWEBM_SOURCES
            "${AOM_ROOT}/third_party/libwebm/common/hdr_util.cc"
            "${AOM_ROOT}/third_party/libwebm/common/hdr_util.h"
            "${AOM_ROOT}/third_party/libwebm/common/webmids.h"
            "${AOM_ROOT}/third_party/libwebm/mkvmuxer/mkvmuxer.cc"
            "${AOM_ROOT}/third_party/libwebm/mkvmuxer/mkvmuxer.h"
            "${AOM_ROOT}/third_party/libwebm/mkvmuxer/mkvmuxertypes.h"
            "${AOM_ROOT}/third_party/libwebm/mkvmuxer/mkvmuxerutil.cc"
            "${AOM_ROOT}/third_party/libwebm/mkvmuxer/mkvmuxerutil.h"
            "${AOM_ROOT}/third_party/libwebm/mkvmuxer/mkvwriter.cc"
            "${AOM_ROOT}/third_party/libwebm/mkvmuxer/mkvwriter.h"
            "${AOM_ROOT}/third_party/libwebm/mkvparser/mkvparser.cc"
            "${AOM_ROOT}/third_party/libwebm/mkvparser/mkvparser.h"
            "${AOM_ROOT}/third_party/libwebm/mkvparser/mkvreader.cc"
            "${AOM_ROOT}/third_party/libwebm/mkvparser/mkvreader.h")

list(APPEND AOM_LIBYUV_SOURCES
            "${AOM_ROOT}/third_party/libyuv/include/libyuv/basic_types.h"
            "${AOM_ROOT}/third_party/libyuv/include/libyuv/convert.h"
            "${AOM_ROOT}/third_party/libyuv/include/libyuv/convert_argb.h"
            "${AOM_ROOT}/third_party/libyuv/include/libyuv/convert_from.h"
            "${AOM_ROOT}/third_party/libyuv/include/libyuv/cpu_id.h"
            "${AOM_ROOT}/third_party/libyuv/include/libyuv/planar_functions.h"
            "${AOM_ROOT}/third_party/libyuv/include/libyuv/rotate.h"
            "${AOM_ROOT}/third_party/libyuv/include/libyuv/row.h"
            "${AOM_ROOT}/third_party/libyuv/include/libyuv/scale.h"
            "${AOM_ROOT}/third_party/libyuv/include/libyuv/scale_row.h"
            "${AOM_ROOT}/third_party/libyuv/source/convert_argb.cc"
            "${AOM_ROOT}/third_party/libyuv/source/cpu_id.cc"
            "${AOM_ROOT}/third_party/libyuv/source/planar_functions.cc"
            "${AOM_ROOT}/third_party/libyuv/source/row_any.cc"
            "${AOM_ROOT}/third_party/libyuv/source/row_common.cc"
            "${AOM_ROOT}/third_party/libyuv/source/row_gcc.cc"
            "${AOM_ROOT}/third_party/libyuv/source/row_mips.cc"
            "${AOM_ROOT}/third_party/libyuv/source/row_neon.cc"
            "${AOM_ROOT}/third_party/libyuv/source/row_neon64.cc"
            "${AOM_ROOT}/third_party/libyuv/source/row_win.cc"
            "${AOM_ROOT}/third_party/libyuv/source/scale.cc"
            "${AOM_ROOT}/third_party/libyuv/source/scale_any.cc"
            "${AOM_ROOT}/third_party/libyuv/source/scale_common.cc"
            "${AOM_ROOT}/third_party/libyuv/source/scale_gcc.cc"
            "${AOM_ROOT}/third_party/libyuv/source/scale_mips.cc"
            "${AOM_ROOT}/third_party/libyuv/source/scale_neon.cc"
            "${AOM_ROOT}/third_party/libyuv/source/scale_neon64.cc"
            "${AOM_ROOT}/third_party/libyuv/source/scale_win.cc"
            "${AOM_ROOT}/third_party/libyuv/source/scale_uv.cc")

list(APPEND AOM_SOURCES
            "${AOM_CONFIG_DIR}/config/aom_config.c"
            "${AOM_CONFIG_DIR}/config/aom_config.h"
            "${AOM_ROOT}/aom/aom.h"
            "${AOM_ROOT}/aom/aom_codec.h"
            "${AOM_ROOT}/aom/aom_decoder.h"
            "${AOM_ROOT}/aom/aom_encoder.h"
            "${AOM_ROOT}/aom/aom_external_partition.h"
            "${AOM_ROOT}/aom/aom_frame_buffer.h"
            "${AOM_ROOT}/aom/aom_image.h"
            "${AOM_ROOT}/aom/aom_integer.h"
            "${AOM_ROOT}/aom/aomcx.h"
            "${AOM_ROOT}/aom/aomdx.h"
            "${AOM_ROOT}/aom/internal/aom_codec_internal.h"
            "${AOM_ROOT}/aom/internal/aom_image_internal.h"
            "${AOM_ROOT}/aom/src/aom_codec.c"
            "${AOM_ROOT}/aom/src/aom_decoder.c"
            "${AOM_ROOT}/aom/src/aom_encoder.c"
            "${AOM_ROOT}/aom/src/aom_image.c"
            "${AOM_ROOT}/aom/src/aom_integer.c")

list(APPEND AOM_COMMON_APP_UTIL_SOURCES
            "${AOM_ROOT}/av1/arg_defs.c"
            "${AOM_ROOT}/av1/arg_defs.h"
            "${AOM_ROOT}/common/args_helper.c"
            "${AOM_ROOT}/common/args_helper.h"
            "${AOM_ROOT}/common/args.c"
            "${AOM_ROOT}/common/args.h"
            "${AOM_ROOT}/common/av1_config.c"
            "${AOM_ROOT}/common/av1_config.h"
            "${AOM_ROOT}/common/md5_utils.c"
            "${AOM_ROOT}/common/md5_utils.h"
            "${AOM_ROOT}/common/tools_common.c"
            "${AOM_ROOT}/common/tools_common.h"
            "${AOM_ROOT}/common/video_common.h"
            "${AOM_ROOT}/common/rawenc.c"
            "${AOM_ROOT}/common/rawenc.h"
            "${AOM_ROOT}/common/y4menc.c"
            "${AOM_ROOT}/common/y4menc.h"
            "${AOM_ROOT}/common/ivfdec.c"
            "${AOM_ROOT}/common/ivfdec.h")

list(APPEND AOM_DECODER_APP_UTIL_SOURCES "${AOM_ROOT}/common/obudec.c"
            "${AOM_ROOT}/common/obudec.h" "${AOM_ROOT}/common/video_reader.c"
            "${AOM_ROOT}/common/video_reader.h")

list(APPEND AOM_ENCODER_APP_UTIL_SOURCES
            "${AOM_ROOT}/common/ivfenc.c"
            "${AOM_ROOT}/common/ivfenc.h"
            "${AOM_ROOT}/common/video_writer.c"
            "${AOM_ROOT}/common/video_writer.h"
            "${AOM_ROOT}/common/warnings.c"
            "${AOM_ROOT}/common/warnings.h"
            "${AOM_ROOT}/common/y4minput.c"
            "${AOM_ROOT}/common/y4minput.h"
            "${AOM_ROOT}/examples/encoder_util.h"
            "${AOM_ROOT}/examples/encoder_util.c"
            "${AOM_ROOT}/examples/multilayer_metadata.h"
            "${AOM_ROOT}/examples/multilayer_metadata.cc")

list(APPEND AOM_ENCODER_STATS_SOURCES "${AOM_ROOT}/stats/aomstats.c"
            "${AOM_ROOT}/stats/aomstats.h" "${AOM_ROOT}/stats/rate_hist.c"
            "${AOM_ROOT}/stats/rate_hist.h")

list(APPEND AOM_VERSION_SOURCES "${AOM_CONFIG_DIR}/config/aom_version.h")

list(APPEND AOM_WEBM_DECODER_SOURCES "${AOM_ROOT}/common/webmdec.cc"
            "${AOM_ROOT}/common/webmdec.h")

list(APPEND AOM_WEBM_ENCODER_SOURCES "${AOM_ROOT}/common/webmenc.cc"
            "${AOM_ROOT}/common/webmenc.h")

include_directories(${AOM_ROOT} ${AOM_CONFIG_DIR} ${AOM_ROOT}/apps
                    ${AOM_ROOT}/common ${AOM_ROOT}/examples ${AOM_ROOT}/stats)

# Targets
add_library(aom_version ${AOM_VERSION_SOURCES})
add_no_op_source_file_to_target(aom_version c)
add_custom_command(OUTPUT "${AOM_CONFIG_DIR}/config/aom_version.h"
                   COMMAND ${CMAKE_COMMAND} ARGS
                           -DAOM_CONFIG_DIR=${AOM_CONFIG_DIR}
                           -DAOM_ROOT=${AOM_ROOT}
                           -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                           -DPERL_EXECUTABLE=${PERL_EXECUTABLE} -P
                           "${AOM_ROOT}/build/cmake/version.cmake"
                   COMMENT "Writing aom_version.h"
                   VERBATIM)

add_custom_target(aom_version_check
                  COMMAND ${CMAKE_COMMAND}
                          -DAOM_CONFIG_DIR=${AOM_CONFIG_DIR}
                          -DAOM_ROOT=${AOM_ROOT}
                          -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                          -DPERL_EXECUTABLE=${PERL_EXECUTABLE} -P
                          "${AOM_ROOT}/build/cmake/version.cmake"
                  COMMENT "Updating version info if necessary."
                  VERBATIM)

if(BUILD_SHARED_LIBS AND NOT MSVC)
  # Generate version file immediately for non-MSVC shared builds: The version
  # string is needed for the aom target.
  execute_process(COMMAND ${CMAKE_COMMAND}
                          -DAOM_CONFIG_DIR=${AOM_CONFIG_DIR}
                          -DAOM_ROOT=${AOM_ROOT}
                          -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                          -DPERL_EXECUTABLE=${PERL_EXECUTABLE} -P
                          "${AOM_ROOT}/build/cmake/version.cmake")
endif()

add_dependencies(aom_version aom_version_check)

# TODO(tomfinegan): Move rtcd target setup where it belongs for each rtcd
# source.
add_rtcd_build_step("${AOM_ROOT}/aom_dsp/aom_dsp_rtcd_defs.pl"
                    "${AOM_CONFIG_DIR}/config/aom_dsp_rtcd.h"
                    "${AOM_ROOT}/aom_dsp/aom_dsp_rtcd.c" "aom_dsp_rtcd")
add_rtcd_build_step("${AOM_ROOT}/aom_scale/aom_scale_rtcd.pl"
                    "${AOM_CONFIG_DIR}/config/aom_scale_rtcd.h"
                    "${AOM_ROOT}/aom_scale/aom_scale_rtcd.c" "aom_scale_rtcd")
add_rtcd_build_step("${AOM_ROOT}/av1/common/av1_rtcd_defs.pl"
                    "${AOM_CONFIG_DIR}/config/av1_rtcd.h"
                    "${AOM_ROOT}/av1/common/av1_rtcd.c" "av1_rtcd")

add_library(aom_rtcd OBJECT ${AOM_RTCD_SOURCES})
add_dependencies(aom_rtcd aom_version)

if(ENABLE_EXAMPLES)
  add_library(aom_encoder_stats OBJECT ${AOM_ENCODER_STATS_SOURCES})
  set(AOM_LIB_TARGETS ${AOM_LIB_TARGETS} aom_encoder_stats)
endif()

# Xcode generator cannot take a library composed solely of objects. See
# https://gitlab.kitware.com/cmake/cmake/-/issues/17500
if(XCODE)
  set(target_objs_aom ${AOM_SOURCES})
else()
  add_library(aom_obj OBJECT ${AOM_SOURCES})
  set(AOM_LIB_TARGETS ${AOM_LIB_TARGETS} aom_obj)
  set(target_objs_aom $<TARGET_OBJECTS:aom_obj>)
endif()
add_library(aom ${target_objs_aom} $<TARGET_OBJECTS:aom_rtcd>)
target_include_directories(aom PUBLIC $<INSTALL_INTERFACE:include>)

if(BUILD_SHARED_LIBS)
  add_library(aom_static STATIC ${target_objs_aom} $<TARGET_OBJECTS:aom_rtcd>)
  target_include_directories(aom_static PUBLIC $<INSTALL_INTERFACE:include>)
  set_target_properties(aom_static PROPERTIES OUTPUT_NAME aom)
  if(MSVC OR (WIN32 AND NOT MINGW))
    # Fix race condition between the import library and the static library.
    # Affects MSVC in all three flavors (stock, clang-cl, LLVM -- the latter
    # sets MSVC and MINGW both to FALSE).
    set_target_properties(aom PROPERTIES ARCHIVE_OUTPUT_NAME "aom_dll")
  endif()

  if(NOT MSVC)
    # Extract version string and set VERSION/SOVERSION for the aom target.
    extract_version_string("${AOM_CONFIG_DIR}/config/aom_version.h"
                           aom_version_triple)

    # Strip any trailing version information, if present.
    string(FIND "${aom_version_triple}" "-" dash_pos)
    if(NOT dash_pos EQUAL -1)
      string(SUBSTRING "${aom_version_triple}" 0 ${dash_pos} aom_version_triple)
    endif()

    # cmake-format: off
    # VERSION is embedded in the .so file name.
    # libaom.so -> libaom.so.SOVERSION
    # libaom.so.SOVERSION -> libaom.so.VERSION
    # libaom.so.VERSION
    # cmake-format: on
    set_target_properties(aom PROPERTIES SOVERSION ${SO_VERSION})
    set_target_properties(aom PROPERTIES VERSION ${SO_FILE_VERSION})
  endif()
endif()

if(NOT WIN32 AND NOT APPLE)
  target_link_libraries(aom ${AOM_LIB_LINK_TYPE} m)
  if(BUILD_SHARED_LIBS)
    target_link_libraries(aom_static ${AOM_LIB_LINK_TYPE} m)
  endif()
endif()

if(CONFIG_AV1_ENCODER)
  list(APPEND AOM_AV1_RC_SOURCES "${AOM_ROOT}/av1/ratectrl_rtc.h"
              "${AOM_ROOT}/av1/ratectrl_rtc.cc")
  add_library(aom_av1_rc ${AOM_AV1_RC_SOURCES})
  # aom_av1_rc calls libaom's internal functions, so it must be linked with the
  # libaom static library.
  if(BUILD_SHARED_LIBS)
    target_link_libraries(aom_av1_rc ${AOM_LIB_LINK_TYPE} aom_static)
    # TODO: https://aomedia.issues.chromium.org/391715078 - This condition can
    # be removed after aom_av1_rc restricts its symbol visibility.
    if(CYGWIN OR MINGW)
      target_link_options(aom_av1_rc ${AOM_LIB_LINK_TYPE}
                          LINKER:--allow-multiple-definition)
    endif()
  else()
    target_link_libraries(aom_av1_rc ${AOM_LIB_LINK_TYPE} aom)
  endif()
  if(BUILD_SHARED_LIBS)
    # On Windows, global symbols are not exported from a DLL by default. Enable
    # the WINDOWS_EXPORT_ALL_SYMBOLS property to export all global symbols from
    # the aom_av1_rc DLL on Windows, to match the default behavior on other
    # platforms.
    set_target_properties(aom_av1_rc PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    # The aom_av1_rc library and its header "av1/ratectrl_rtc.h" are not
    # installed by the "install" command, so we don't need to worry about
    # versioning the aom_av1_rc shared library. If we start to install the
    # aom_av1_rc library, the library should be versioned.
  endif()
  if(NOT WIN32 AND NOT APPLE)
    target_link_libraries(aom_av1_rc ${AOM_LIB_LINK_TYPE} m)
  endif()
  set_target_properties(aom_av1_rc PROPERTIES LINKER_LANGUAGE CXX)
endif()

# List of object and static library targets.
set(AOM_LIB_TARGETS ${AOM_LIB_TARGETS} aom_rtcd aom_mem aom_scale aom)
if(CONFIG_AV1_ENCODER)
  set(AOM_LIB_TARGETS ${AOM_LIB_TARGETS} aom_av1_rc)
endif()
if(BUILD_SHARED_LIBS)
  set(AOM_LIB_TARGETS ${AOM_LIB_TARGETS} aom_static)
endif()

# Setup dependencies.
if(CONFIG_THREE_PASS)
  setup_ivf_dec_targets()
endif()
setup_aom_dsp_targets()
setup_aom_mem_targets()
setup_aom_ports_targets()
setup_aom_util_targets()
setup_aom_scale_targets()
setup_av1_targets()

# Make all library targets depend on aom_rtcd to make sure it builds first.
foreach(aom_lib ${AOM_LIB_TARGETS})
  if(NOT "${aom_lib}" STREQUAL "aom_rtcd")
    add_dependencies(${aom_lib} aom_rtcd)
  endif()
endforeach()

# Generate a C file containing the function usage_exit(). Users of the
# aom_common_app_util library must define this function. This is a convenience
# to allow omission of the function from applications that might want to use
# other pieces of the util support without defining usage_exit().
file(WRITE "${AOM_GEN_SRC_DIR}/usage_exit.c"
     "#include <stdlib.h>\n\n#include \"common/tools_common.h\"\n\n"
     "void usage_exit(void) { exit(EXIT_FAILURE); }\n")

#
# Application and application support targets.
#
if(ENABLE_EXAMPLES OR ENABLE_TESTS OR ENABLE_TOOLS)
  add_library(aom_common_app_util OBJECT ${AOM_COMMON_APP_UTIL_SOURCES})
  add_library(aom_usage_exit OBJECT "${AOM_GEN_SRC_DIR}/usage_exit.c")
  set_property(TARGET ${example} PROPERTY FOLDER examples)
  if(CONFIG_AV1_DECODER)
    add_library(aom_decoder_app_util OBJECT ${AOM_DECODER_APP_UTIL_SOURCES})
    set_property(TARGET ${example} PROPERTY FOLDER examples)
    # obudec depends on internal headers that require *rtcd.h
    add_dependencies(aom_decoder_app_util aom_rtcd)
  endif()
  if(CONFIG_AV1_ENCODER)
    add_library(aom_encoder_app_util OBJECT ${AOM_ENCODER_APP_UTIL_SOURCES})
    set_property(TARGET ${example} PROPERTY FOLDER examples)
  endif()
endif()

if(CONFIG_AV1_DECODER AND ENABLE_EXAMPLES)
  add_executable(aomdec "${AOM_ROOT}/apps/aomdec.c"
                        $<TARGET_OBJECTS:aom_common_app_util>
                        $<TARGET_OBJECTS:aom_decoder_app_util>)
  add_executable(decode_to_md5 "${AOM_ROOT}/examples/decode_to_md5.c"
                               $<TARGET_OBJECTS:aom_common_app_util>
                               $<TARGET_OBJECTS:aom_decoder_app_util>)
  add_executable(decode_with_drops "${AOM_ROOT}/examples/decode_with_drops.c"
                                   $<TARGET_OBJECTS:aom_common_app_util>
                                   $<TARGET_OBJECTS:aom_decoder_app_util>)
  add_executable(simple_decoder "${AOM_ROOT}/examples/simple_decoder.c"
                                $<TARGET_OBJECTS:aom_common_app_util>
                                $<TARGET_OBJECTS:aom_decoder_app_util>)
  add_executable(scalable_decoder "${AOM_ROOT}/examples/scalable_decoder.c"
                                  $<TARGET_OBJECTS:aom_common_app_util>
                                  $<TARGET_OBJECTS:aom_decoder_app_util>)

  if(CONFIG_ANALYZER)
    add_executable(analyzer "${AOM_ROOT}/examples/analyzer.cc"
                            $<TARGET_OBJECTS:aom_common_app_util>
                            $<TARGET_OBJECTS:aom_decoder_app_util>)
    target_link_libraries(analyzer ${AOM_LIB_LINK_TYPE} ${wxWidgets_LIBRARIES})
    list(APPEND AOM_APP_TARGETS analyzer)
    list(APPEND AOM_DECODER_EXAMPLE_TARGETS analyzer)
  endif()

  if(CONFIG_INSPECTION)
    add_executable(inspect "${AOM_ROOT}/examples/inspect.c"
                           $<TARGET_OBJECTS:aom_common_app_util>
                           $<TARGET_OBJECTS:aom_decoder_app_util>)
    list(APPEND AOM_DECODER_EXAMPLE_TARGETS inspect)

    if(EMSCRIPTEN)
      add_preproc_definition(_POSIX_SOURCE)
      append_link_flag_to_target("inspect" "--emrun")
      append_link_flag_to_target("inspect" "-s USE_PTHREADS=0")
      append_link_flag_to_target("inspect" "-s WASM=1")
      append_link_flag_to_target("inspect" "-s MODULARIZE=1")
      append_link_flag_to_target("inspect" "-s ALLOW_MEMORY_GROWTH=1")
      append_link_flag_to_target(
        "inspect" "-s \'EXTRA_EXPORTED_RUNTIME_METHODS=[\"UTF8ToString\"]\'")
      append_link_flag_to_target("inspect"
                                 "-s EXPORT_NAME=\"\'DecoderModule\'\"")
      append_link_flag_to_target("inspect" "--memory-init-file 0")

      if("${CMAKE_BUILD_TYPE}" STREQUAL "")

        # Default to -O3 when no build type is specified.
        append_compiler_flag("-O3")
      endif()

      em_link_post_js(inspect "${AOM_ROOT}/tools/inspect-post.js")
    endif()
  endif()

  # Maintain a list of decoder example targets.
  list(APPEND AOM_DECODER_EXAMPLE_TARGETS aomdec decode_to_md5 decode_with_drops
              scalable_decoder simple_decoder)

  # Add decoder examples to the app targets list.
  list(APPEND AOM_APP_TARGETS ${AOM_DECODER_EXAMPLE_TARGETS})
endif()

if(CONFIG_LIBYUV OR CONFIG_TUNE_BUTTERAUGLI)
  add_library(yuv OBJECT ${AOM_LIBYUV_SOURCES})
  if(NOT MSVC)
    target_compile_options(yuv PRIVATE -Wno-shadow)
    # Many functions in libyuv trigger this warning when enabled with gcc and
    # clang.
    is_flag_present(AOM_CXX_FLAGS "-Wmissing-declarations" flag_present)
    if(flag_present)
      target_compile_options(yuv PRIVATE -Wno-missing-declarations)
    endif()
    # Many functions in libyuv trigger this warning when enabled with clang.
    is_flag_present(AOM_CXX_FLAGS "-Wmissing-prototypes" flag_present)
    if(flag_present)
      target_compile_options(yuv PRIVATE -Wno-missing-prototypes)
    endif()
  endif()
  include_directories("${AOM_ROOT}/third_party/libyuv/include")
endif()

if(CONFIG_AV1_ENCODER)
  if(ENABLE_EXAMPLES)
    add_executable(aomenc "${AOM_ROOT}/apps/aomenc.c"
                          $<TARGET_OBJECTS:aom_common_app_util>
                          $<TARGET_OBJECTS:aom_encoder_app_util>
                          $<TARGET_OBJECTS:aom_encoder_stats>)
    add_executable(lossless_encoder "${AOM_ROOT}/examples/lossless_encoder.c"
                                    $<TARGET_OBJECTS:aom_common_app_util>
                                    $<TARGET_OBJECTS:aom_encoder_app_util>)
    add_executable(set_maps "${AOM_ROOT}/examples/set_maps.c"
                            $<TARGET_OBJECTS:aom_common_app_util>
                            $<TARGET_OBJECTS:aom_encoder_app_util>)
    add_executable(simple_encoder "${AOM_ROOT}/examples/simple_encoder.c"
                                  $<TARGET_OBJECTS:aom_common_app_util>
                                  $<TARGET_OBJECTS:aom_encoder_app_util>)
    add_executable(twopass_encoder "${AOM_ROOT}/examples/twopass_encoder.c"
                                   $<TARGET_OBJECTS:aom_common_app_util>
                                   $<TARGET_OBJECTS:aom_encoder_app_util>)
    if(NOT BUILD_SHARED_LIBS AND NOT CONFIG_REALTIME_ONLY)
      add_executable(noise_model "${AOM_ROOT}/examples/noise_model.c"
                                 $<TARGET_OBJECTS:aom_common_app_util>
                                 $<TARGET_OBJECTS:aom_encoder_app_util>)
      add_executable(photon_noise_table
                     "${AOM_ROOT}/examples/photon_noise_table.c"
                     $<TARGET_OBJECTS:aom_common_app_util>
                     $<TARGET_OBJECTS:aom_encoder_app_util>)
    endif()
    add_executable(scalable_encoder "${AOM_ROOT}/examples/scalable_encoder.c"
                                    $<TARGET_OBJECTS:aom_common_app_util>
                                    $<TARGET_OBJECTS:aom_encoder_app_util>)
    add_executable(svc_encoder_rtc "${AOM_ROOT}/examples/svc_encoder_rtc.cc"
                                   $<TARGET_OBJECTS:aom_common_app_util>
                                   $<TARGET_OBJECTS:aom_encoder_app_util>)
    target_link_libraries(svc_encoder_rtc ${AOM_LIB_LINK_TYPE} aom_av1_rc)

    # Maintain a list of encoder example targets.
    list(APPEND AOM_ENCODER_EXAMPLE_TARGETS aomenc lossless_encoder set_maps
                simple_encoder scalable_encoder svc_encoder_rtc twopass_encoder)
    if(NOT BUILD_SHARED_LIBS AND NOT CONFIG_REALTIME_ONLY)
      list(APPEND AOM_ENCODER_EXAMPLE_TARGETS noise_model photon_noise_table)
    endif()

  endif()

  if(ENABLE_TOOLS)
    if(CONFIG_ENTROPY_STATS AND NOT BUILD_SHARED_LIBS)

      # TODO(tomfinegan): Sort out why a simple link command with
      # aom_entropy_optimizer.c won't work on macos, but dragging in all the
      # helper machinery allows the link to succeed.
      add_executable(aom_entropy_optimizer
                     "${AOM_ROOT}/tools/aom_entropy_optimizer.c"
                     $<TARGET_OBJECTS:aom_common_app_util>
                     $<TARGET_OBJECTS:aom_encoder_app_util>
                     $<TARGET_OBJECTS:aom_usage_exit>)

      # Maintain a list of encoder tool targets.
      list(APPEND AOM_ENCODER_TOOL_TARGETS aom_entropy_optimizer)
    endif()
  endif()

  # Add encoder examples and tools to the targets list.
  list(APPEND AOM_APP_TARGETS ${AOM_ENCODER_EXAMPLE_TARGETS}
              ${AOM_ENCODER_TOOL_TARGETS})

  if(CONFIG_TUNE_BUTTERAUGLI)
    find_package(PkgConfig)
    # Use find_library() with STATIC_LINK_JXL for static build since
    # pkg_check_modules() with LIBJXL_STATIC is not working.
    if(STATIC_LINK_JXL OR NOT PKG_CONFIG_FOUND)
      find_library(LIBJXL_LIBRARIES libjxl.a)
      find_library(LIBHWY_LIBRARIES libhwy.a)
      find_library(LIBSKCMS_LIBRARIES libskcms.a)
      find_library(LIBBROTLICOMMON_LIBRARIES libbrotlicommon-static.a)
      find_library(LIBBROTLIENC_LIBRARIES libbrotlienc-static.a)
      find_library(LIBBROTLIDEC_LIBRARIES libbrotlidec-static.a)
      find_path(LIBJXL_INCLUDE_DIRS butteraugli.h PATH_SUFFIXES jxl)
      if(LIBJXL_LIBRARIES
         AND LIBHWY_LIBRARIES
         AND LIBSKCMS_LIBRARIES
         AND LIBBROTLICOMMON_LIBRARIES
         AND LIBBROTLIENC_LIBRARIES
         AND LIBBROTLIDEC_LIBRARIES
         AND LIBJXL_INCLUDE_DIRS)
        message(STATUS "Found JXL library: ${LIBJXL_LIBRARIES} "
                       "${LIBHWY_LIBRARIES} ${LIBSKCMS_LIBRARIES} "
                       "${LIBBROTLICOMMON_LIBRARIES} ${LIBBROTLIENC_LIBRARIES}"
                       "${LIBBROTLIDEC_LIBRARIES}")
        message(STATUS "Found JXL include: ${LIBJXL_INCLUDE_DIRS}")
      else()
        message(FATAL_ERROR "JXL library not found.")
      endif()
      target_link_libraries(aom
                            PRIVATE ${LIBJXL_LIBRARIES} ${LIBHWY_LIBRARIES}
                                    ${LIBSKCMS_LIBRARIES}
                                    ${LIBBROTLIENC_LIBRARIES}
                                    ${LIBBROTLIDEC_LIBRARIES}
                                    ${LIBBROTLICOMMON_LIBRARIES})
      target_include_directories(aom_dsp_encoder PRIVATE ${LIBJXL_INCLUDE_DIRS})
    else()
      pkg_check_modules(LIBJXL REQUIRED libjxl)
      target_link_libraries(aom PRIVATE ${LIBJXL_LDFLAGS})
      target_include_directories(aom_dsp_encoder PRIVATE ${LIBJXL_INCLUDE_DIRS})
      if(LIBJXL_CFLAGS)
        append_compiler_flag("${LIBJXL_CFLAGS}")
      endif()
      pkg_check_modules(LIBHWY REQUIRED libhwy)
      target_link_libraries(aom PRIVATE ${LIBHWY_LDFLAGS})
      target_include_directories(aom_dsp_encoder
                                 PRIVATE ${LIBLIBHWY_INCLUDE_DIRS})
      if(LIBHWY_CFLAGS)
        append_compiler_flag("${LIBHWY_CFLAGS}")
      endif()
    endif()

    set_target_properties(aom PROPERTIES LINKER_LANGUAGE CXX)
    if(BUILD_SHARED_LIBS)
      set_target_properties(aom_static PROPERTIES LINKER_LANGUAGE CXX)
    endif()

    list(APPEND AOM_LIB_TARGETS yuv)
    target_sources(aom PRIVATE $<TARGET_OBJECTS:yuv>)
    if(BUILD_SHARED_LIBS)
      target_sources(aom_static PRIVATE $<TARGET_OBJECTS:yuv>)
    endif()
  endif()

  if(CONFIG_TFLITE)
    include(FetchContent)

    set(TFLITE_TAG "v2.6.1")

    message(STATUS "Fetching TFLite ${TFLITE_TAG}...")

    # static linking makes life with TFLite much easier
    set(TFLITE_C_BUILD_SHARED_LIBS OFF)

    # We don't care about comparing against these delegates (yet), and disabling
    # it reduces compile time meaningfully
    set(TFLITE_ENABLE_RUY OFF)
    set(TFLITE_ENABLE_XNNPACK OFF)

    fetchcontent_declare(tflite
                         GIT_REPOSITORY https://github.com/tensorflow/tensorflow
                         GIT_TAG ${TFLITE_TAG}
                         GIT_SHALLOW TRUE)

    fetchcontent_getproperties(tflite)
    if(NOT tflite_POPULATED)
      fetchcontent_populate(tflite)
      # Some of the subprojects (e.g. Eigen) are very noisy and emit status
      # messages all the time. Temporary ignore status messages while adding
      # this to silence it. Ugly but effective.
      set(OLD_CMAKE_MESSAGE_LOG_LEVEL ${CMAKE_MESSAGE_LOG_LEVEL})
      set(CMAKE_MESSAGE_LOG_LEVEL WARNING)
      add_subdirectory(${tflite_SOURCE_DIR}/tensorflow/lite/c
                       ${tflite_BINARY_DIR})
      set(CMAKE_MESSAGE_LOG_LEVEL ${OLD_CMAKE_MESSAGE_LOG_LEVEL})
    endif()

    # Disable some noisy warnings in tflite
    target_compile_options(tensorflow-lite PRIVATE -w)

    # tensorflowlite_c is implicitly declared by this FetchContent
    include_directories(${tflite_SOURCE_DIR})
    target_link_libraries(aom PRIVATE tensorflow-lite)
  endif()

  if(CONFIG_TUNE_VMAF)
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(VMAF REQUIRED libvmaf)
      if(BUILD_SHARED_LIBS)
        target_link_libraries(aom_static PRIVATE ${VMAF_LDFLAGS})
      endif()
      target_link_libraries(aom PRIVATE ${VMAF_LDFLAGS})
      target_include_directories(aom_dsp_encoder PRIVATE ${VMAF_INCLUDE_DIRS})
      if(VMAF_CFLAGS)
        foreach(flag "${VMAF_CFLAGS}")
          append_compiler_flag("${flag}")
        endforeach()
      endif()
    else()
      message(FATAL_ERROR "CONFIG_TUNE_VMAF error: pkg-config not found.")
    endif()
    set_target_properties(aom PROPERTIES LINKER_LANGUAGE CXX)
    if(BUILD_SHARED_LIBS)
      set_target_properties(aom_static PROPERTIES LINKER_LANGUAGE CXX)
    endif()
  endif()
endif()

if(ENABLE_EXAMPLES)

  # Maintain a separate variable listing only the examples to facilitate
  # installation of example programs into an examples sub directory of
  # $AOM_DIST_DIR/bin when building the dist target.
  list(APPEND AOM_EXAMPLE_TARGETS ${AOM_DECODER_EXAMPLE_TARGETS}
              ${AOM_ENCODER_EXAMPLE_TARGETS})
endif()

if(ENABLE_TOOLS)
  if(CONFIG_AV1_DECODER)
    add_executable(dump_obu "${AOM_ROOT}/tools/dump_obu.cc"
                            "${AOM_ROOT}/tools/obu_parser.cc"
                            "${AOM_ROOT}/tools/obu_parser.h"
                            $<TARGET_OBJECTS:aom_common_app_util>
                            $<TARGET_OBJECTS:aom_decoder_app_util>
                            $<TARGET_OBJECTS:aom_usage_exit>)

    list(APPEND AOM_TOOL_TARGETS dump_obu)
    list(APPEND AOM_APP_TARGETS dump_obu)

    # Maintain a separate variable listing only the examples to facilitate
    # installation of example programs into an tools sub directory of
    # $AOM_DIST_DIR/bin when building the dist target.
    list(APPEND AOM_TOOL_TARGETS ${AOM_DECODER_TOOL_TARGETS}
                ${AOM_ENCODER_TOOL_TARGETS})
  endif()
endif()

if(ENABLE_EXAMPLES AND CONFIG_AV1_DECODER AND CONFIG_AV1_ENCODER)
  add_executable(aom_cx_set_ref "${AOM_ROOT}/examples/aom_cx_set_ref.c"
                                $<TARGET_OBJECTS:aom_common_app_util>
                                $<TARGET_OBJECTS:aom_encoder_app_util>)
  list(APPEND AOM_EXAMPLE_TARGETS aom_cx_set_ref)
  list(APPEND AOM_APP_TARGETS aom_cx_set_ref)
endif()

if(ENABLE_EXAMPLES AND CONFIG_AV1_ENCODER)
  add_executable(lightfield_encoder "${AOM_ROOT}/examples/lightfield_encoder.c"
                                    $<TARGET_OBJECTS:aom_common_app_util>
                                    $<TARGET_OBJECTS:aom_encoder_app_util>)
  list(APPEND AOM_EXAMPLE_TARGETS lightfield_encoder)
  list(APPEND AOM_APP_TARGETS lightfield_encoder)
endif()

if(ENABLE_EXAMPLES AND CONFIG_AV1_DECODER)
  add_executable(lightfield_tile_list_decoder
                 "${AOM_ROOT}/examples/lightfield_tile_list_decoder.c"
                 $<TARGET_OBJECTS:aom_common_app_util>
                 $<TARGET_OBJECTS:aom_decoder_app_util>)
  list(APPEND AOM_EXAMPLE_TARGETS lightfield_tile_list_decoder)
  list(APPEND AOM_APP_TARGETS lightfield_tile_list_decoder)
endif()

if(ENABLE_EXAMPLES AND CONFIG_AV1_DECODER)
  add_executable(lightfield_decoder "${AOM_ROOT}/examples/lightfield_decoder.c"
                                    $<TARGET_OBJECTS:aom_common_app_util>
                                    $<TARGET_OBJECTS:aom_decoder_app_util>)
  list(APPEND AOM_EXAMPLE_TARGETS lightfield_decoder)
  list(APPEND AOM_APP_TARGETS lightfield_decoder)
endif()

if(ENABLE_EXAMPLES AND CONFIG_AV1_ENCODER AND CONFIG_AV1_DECODER)
  add_executable(lightfield_bitstream_parsing
                 "${AOM_ROOT}/examples/lightfield_bitstream_parsing.c"
                 $<TARGET_OBJECTS:aom_common_app_util>
                 $<TARGET_OBJECTS:aom_encoder_app_util>
                 $<TARGET_OBJECTS:aom_decoder_app_util>)
  list(APPEND AOM_EXAMPLE_TARGETS lightfield_bitstream_parsing)
  list(APPEND AOM_APP_TARGETS lightfield_bitstream_parsing)
endif()

foreach(aom_app ${AOM_APP_TARGETS})
  target_link_libraries(${aom_app} ${AOM_LIB_LINK_TYPE} aom)
endforeach()

if(ENABLE_EXAMPLES OR ENABLE_TESTS OR ENABLE_TOOLS)
  if(CONFIG_LIBYUV)
    # Add to existing targets.
    foreach(aom_app ${AOM_APP_TARGETS})
      target_sources(${aom_app} PRIVATE $<TARGET_OBJECTS:yuv>)
      set_property(TARGET ${aom_app} PROPERTY LINKER_LANGUAGE CXX)
    endforeach()
  endif()

  if(CONFIG_WEBM_IO)
    add_library(webm OBJECT ${AOM_LIBWEBM_SOURCES})
    include_directories("${AOM_ROOT}/third_party/libwebm")
    target_compile_definitions(webm PRIVATE __STDC_CONSTANT_MACROS)
    target_compile_definitions(webm PRIVATE __STDC_LIMIT_MACROS)

    if(NOT MSVC)
      target_compile_options(webm PRIVATE -Wno-shadow)
    endif()

    # Add to existing targets.
    if(CONFIG_AV1_DECODER)
      target_sources(aom_decoder_app_util PRIVATE ${AOM_WEBM_DECODER_SOURCES})
    endif()

    if(CONFIG_AV1_ENCODER)
      target_sources(aom_encoder_app_util PRIVATE ${AOM_WEBM_ENCODER_SOURCES})
    endif()

    foreach(aom_app ${AOM_APP_TARGETS})
      target_sources(${aom_app} PRIVATE $<TARGET_OBJECTS:webm>)
      set_property(TARGET ${aom_app} PROPERTY LINKER_LANGUAGE CXX)
    endforeach()
  endif()
endif()

if(ENABLE_TESTS)

  # Create test_libaom target and the targets it depends on.
  setup_aom_test_targets()
endif()

if(HAVE_PTHREAD_H AND CONFIG_MULTITHREAD)
  find_package(Threads)
  target_link_libraries(aom ${AOM_LIB_LINK_TYPE} Threads::Threads)
  if(BUILD_SHARED_LIBS)
    target_link_libraries(aom_static ${AOM_LIB_LINK_TYPE} Threads::Threads)
  endif()
endif()

if(XCODE)

  # TODO(tomfinegan): Make sure target has no C++ files before doing this as
  # it's not necessary in that case.
  if(CONFIG_LIBYUV OR CONFIG_WEBM_IO)

    # The Xcode generator does not obey LINKER_LANGUAGE. Because of the issue
    # what looks like a C++ file needs to be in any target that Xcode will link
    # when the target contains a C++ dependency. Without this Xcode will try to
    # link with the C linker, which always ends badly when a dependency actually
    # includes C++.

    # Note: LINKER_LANGUAGE is explicitly set to C++ for all targets touched
    # here, it really is the Xcode generator's fault, or just a deficiency in
    # Xcode itself.
    foreach(aom_app ${AOM_APP_TARGETS})
      add_no_op_source_file_to_target("${aom_app}" "cc")
    endforeach()
  endif()
endif()

if(ENABLE_EXAMPLES AND "${CMAKE_GENERATOR}" MATCHES "Makefiles$")

  # For historical purposes place the example binaries in the example directory.
  file(MAKE_DIRECTORY "${AOM_CONFIG_DIR}/examples")

  foreach(target ${AOM_EXAMPLE_TARGETS})
    if(NOT "${target}" MATCHES "aomdec\|aomenc")
      set_target_properties(${target}
                            PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                       "${AOM_CONFIG_DIR}/examples")
    endif()
  endforeach()

  if(ENABLE_TOOLS AND AOM_TOOL_TARGETS)

    # The same expectation is true for tool targets.
    file(MAKE_DIRECTORY "${AOM_CONFIG_DIR}/tools")
    set_target_properties(${AOM_TOOL_TARGETS}
                          PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                     "${AOM_CONFIG_DIR}/tools")
  endif()
endif()

if(BUILD_SHARED_LIBS)
  # Don't use -Wl,-z,defs with Clang's sanitizers.
  #
  # Clang's AddressSanitizer documentation says "When linking shared libraries,
  # the AddressSanitizer run-time is not linked, so -Wl,-z,defs may cause link
  # errors (don't use it with AddressSanitizer)." See
  # https://clang.llvm.org/docs/AddressSanitizer.html#usage. Similarly, see
  # https://clang.llvm.org/docs/MemorySanitizer.html#usage.
  if(NOT
     (APPLE OR CYGWIN OR WIN32)
     AND NOT (CMAKE_C_COMPILER_ID MATCHES "Clang" AND SANITIZE))
    # The -z defs linker option reports unresolved symbol references from object
    # files when building a shared library.
    if("${CMAKE_VERSION}" VERSION_LESS "3.13")
      # target_link_options() is not available before CMake 3.13.
      target_link_libraries(aom PRIVATE -Wl,-z,defs)
    else()
      target_link_options(aom PRIVATE LINKER:-z,defs)
    endif()
  endif()

  include("${AOM_ROOT}/build/cmake/exports.cmake")
  setup_exports_target()
endif()

# Handle user supplied compile and link flags last to ensure they're obeyed.
set_user_flags()

# Aomedia documentation rule.
set(DOXYGEN_VERSION_VALUE 0)
if(ENABLE_DOCS)
  find_package(Doxygen)
  if(DOXYGEN_FOUND)
    # Check if Doxygen version is >= minimum required version(i.e. 1.8.10).
    set(MINIMUM_DOXYGEN_VERSION 1008010)

    if(DOXYGEN_VERSION)
      # Strip SHA1 from version string if present.
      string(REGEX
             REPLACE "^([0-9]+\\.[0-9]+\\.[0-9]+).*" "\\1" DOXYGEN_VERSION
                     ${DOXYGEN_VERSION})
      # Replace dots with semicolons to create a list.
      string(REGEX REPLACE "\\." ";" DOXYGEN_VERSION_LIST ${DOXYGEN_VERSION})
      # Parse version components from the list.
      list(GET DOXYGEN_VERSION_LIST 0 DOXYGEN_MAJOR)
      list(GET DOXYGEN_VERSION_LIST 1 DOXYGEN_MINOR)
      list(GET DOXYGEN_VERSION_LIST 2 DOXYGEN_PATCH)
    endif()

    # Construct a version value for comparison.
    math(EXPR DOXYGEN_MAJOR "${DOXYGEN_MAJOR}*1000000")
    math(EXPR DOXYGEN_MINOR "${DOXYGEN_MINOR}*1000")
    math(EXPR DOXYGEN_VERSION_VALUE
         "${DOXYGEN_MAJOR} + ${DOXYGEN_MINOR} + ${DOXYGEN_PATCH}")

    if(${DOXYGEN_VERSION_VALUE} LESS ${MINIMUM_DOXYGEN_VERSION})
      set(DOXYGEN_FOUND NO)
    endif()
  endif()

  if(DOXYGEN_FOUND)
    include("${AOM_ROOT}/docs.cmake")
    setup_documentation_targets()
  else()
    message(
      "--- Cannot find doxygen(version 1.8.10 or newer), ENABLE_DOCS turned off."
      )
    set(ENABLE_DOCS OFF)
  endif()
endif()

# Aomedia dist rule.
if(CONFIG_AV1_DECODER AND ENABLE_EXAMPLES)
  list(APPEND AOM_DIST_APPS $<TARGET_FILE:aomdec>)
endif()
if(CONFIG_AV1_ENCODER AND ENABLE_EXAMPLES)
  list(APPEND AOM_DIST_APPS $<TARGET_FILE:aomenc>)
endif()

if(ENABLE_EXAMPLES)
  foreach(example ${AOM_EXAMPLE_TARGETS})
    list(APPEND AOM_DIST_EXAMPLES $<TARGET_FILE:${example}>)
    set_property(TARGET ${example} PROPERTY FOLDER examples)
  endforeach()
endif()

if(ENABLE_TOOLS)
  foreach(tool ${AOM_TOOL_TARGETS})
    list(APPEND AOM_DIST_TOOLS $<TARGET_FILE:${tool}>)
    set_property(TARGET ${tool} PROPERTY FOLDER tools)
  endforeach()
endif()

if(NOT AOM_DIST_DIR)
  set(AOM_DIST_DIR "${AOM_CONFIG_DIR}/dist")
endif()

add_custom_target(dist
                  COMMAND ${CMAKE_COMMAND}
                          -DAOM_ROOT=${AOM_ROOT}
                          -DAOM_CONFIG_DIR=${AOM_CONFIG_DIR}
                          -DAOM_DIST_DIR=${AOM_DIST_DIR}
                          -DAOM_DIST_APPS="${AOM_DIST_APPS}"
                          -DAOM_DIST_EXAMPLES="${AOM_DIST_EXAMPLES}"
                          -DAOM_DIST_TOOLS="${AOM_DIST_TOOLS}"
                          -DAOM_DIST_INCLUDES="${AOM_INSTALL_INCS}"
                          -DAOM_DIST_LIBS=$<TARGET_FILE:aom>
                          -DENABLE_DOCS=${ENABLE_DOCS} -P
                          "${AOM_ROOT}/build/cmake/dist.cmake"
                  DEPENDS ${AOM_INSTALL_BINS} ${AOM_INSTALL_LIBS}
                          ${AOM_INSTALL_INCS} ${AOM_EXAMPLE_TARGETS}
                          ${AOM_TOOL_TARGETS})

if(ENABLE_DOCS)
  add_dependencies(dist docs)
endif()

# Collect all variables containing libaom source files.
get_cmake_property(all_cmake_vars VARIABLES)
foreach(var ${all_cmake_vars})
  if("${var}" MATCHES "SOURCES$\|_INTRIN_\|_ASM_"
     AND NOT "${var}" MATCHES "DOXYGEN\|LIBYUV\|_PKG_\|TEST"
     AND NOT "${var}" MATCHES "_ASM_NASM\|_ASM_COMPILER")
    list(APPEND aom_source_vars ${var})
  endif()
endforeach()

if(NOT CONFIG_AV1_DECODER)
  list(FILTER aom_source_vars EXCLUDE REGEX "_DECODER_")
endif()

# Libaom_srcs.txt generation.
set(libaom_srcs_txt_file "${AOM_CONFIG_DIR}/libaom_srcs.txt")
file(WRITE "${libaom_srcs_txt_file}" "# This file is generated. DO NOT EDIT.\n")

# Static source file list first.
foreach(aom_source_var ${aom_source_vars})
  foreach(file ${${aom_source_var}})
    if(NOT "${file}" MATCHES "${AOM_CONFIG_DIR}")
      string(REPLACE "${AOM_ROOT}/" "" file "${file}")
      if(NOT CONFIG_AV1_DECODER AND "${file}" MATCHES "aom_decoder")
        continue()
      endif()
      file(APPEND "${libaom_srcs_txt_file}" "${file}\n")
    endif()
  endforeach()
endforeach()

file(APPEND "${libaom_srcs_txt_file}"
     "# Files below this line are generated by the libaom build system.\n")
foreach(aom_source_var ${aom_source_vars})
  foreach(file ${${aom_source_var}})
    if("${file}" MATCHES "${AOM_CONFIG_DIR}")
      string(REPLACE "${AOM_CONFIG_DIR}/" "" file "${file}")
      file(APPEND "${libaom_srcs_txt_file}" "${file}\n")
    endif()
  endforeach()
endforeach()

# Libaom_srcs.gni generation.
set(libaom_srcs_gni_file "${AOM_CONFIG_DIR}/libaom_srcs.gni")
file(WRITE "${libaom_srcs_gni_file}" "# This file is generated. DO NOT EDIT.\n")

foreach(aom_source_var ${aom_source_vars})
  if("${${aom_source_var}}" MATCHES "${AOM_ROOT}")
    string(TOLOWER ${aom_source_var} aom_source_var_lowercase)
    file(APPEND "${libaom_srcs_gni_file}" "\n${aom_source_var_lowercase} = [\n")
  endif()

  foreach(file ${${aom_source_var}})
    if(NOT "${file}" MATCHES "${AOM_CONFIG_DIR}")
      string(REPLACE "${AOM_ROOT}" "//third_party/libaom/source/libaom" file
                     "${file}")
      if(NOT CONFIG_AV1_DECODER AND "${file}" MATCHES "aom_decoder")
        continue()
      endif()
      file(APPEND "${libaom_srcs_gni_file}" "  \"${file}\",\n")
    endif()
  endforeach()

  if("${${aom_source_var}}" MATCHES "${AOM_ROOT}")
    file(APPEND "${libaom_srcs_gni_file}" "]\n")
  endif()
endforeach()

file(APPEND "${libaom_srcs_gni_file}"
     "\n# Files below this line are generated by the libaom build system.\n")

foreach(aom_source_var ${aom_source_vars})
  if("${${aom_source_var}}" MATCHES "${AOM_CONFIG_DIR}")
    string(TOLOWER ${aom_source_var} aom_source_var_lowercase)
    file(APPEND "${libaom_srcs_gni_file}"
         "\n${aom_source_var_lowercase}_gen = [\n")
  endif()
  foreach(file ${${aom_source_var}})
    if(NOT "${file}" MATCHES "${AOM_ROOT}")
      string(REPLACE "${AOM_CONFIG_DIR}" "//third_party/libaom/source/libaom"
                     file "${file}")
      file(APPEND "${libaom_srcs_gni_file}" "  \"${file}\",\n")
    endif()
  endforeach()

  if("${${aom_source_var}}" MATCHES "${AOM_CONFIG_DIR}")
    file(APPEND "${libaom_srcs_gni_file}" "]\n")
  endif()
endforeach()

# Generate aom.pc and setup install rule.
setup_aom_install_targets()
