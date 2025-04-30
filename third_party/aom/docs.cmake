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
if(AOM_DOCS_CMAKE_)
  return()
endif() # AOM_DOCS_CMAKE_
set(AOM_DOCS_CMAKE_ 1)

cmake_minimum_required(VERSION 3.16)

set(AOM_DOXYFILE "${AOM_CONFIG_DIR}/doxyfile")
set(AOM_DOXYGEN_CONFIG_TEMPLATE "libs.doxy_template")
set(AOM_DOXYGEN_OUTPUT_DIR "${AOM_CONFIG_DIR}/dox")
set(AOM_DOXYGEN_SECTIONS "av1")

set(AOM_DOXYGEN_SOURCES
    "${AOM_ROOT}/aom/aom.h"
    "${AOM_ROOT}/aom/aom_codec.h"
    "${AOM_ROOT}/aom/aom_decoder.h"
    "${AOM_ROOT}/aom/aom_encoder.h"
    "${AOM_ROOT}/aom/aom_external_partition.h"
    "${AOM_ROOT}/aom/aom_frame_buffer.h"
    "${AOM_ROOT}/aom/aom_image.h"
    "${AOM_ROOT}/aom/aom_integer.h"
    "${AOM_ROOT}/av1/common/av1_common_int.h"
    "${AOM_ROOT}/av1/common/av1_loopfilter.h"
    "${AOM_ROOT}/av1/common/blockd.h"
    "${AOM_ROOT}/av1/common/cdef.h"
    "${AOM_ROOT}/av1/common/enums.h"
    "${AOM_ROOT}/av1/common/restoration.h"
    "${AOM_ROOT}/keywords.dox"
    "${AOM_ROOT}/mainpage.dox"
    "${AOM_ROOT}/usage.dox")

if(CONFIG_AV1_DECODER)
  set(AOM_DOXYGEN_EXAMPLE_SOURCES ${AOM_DOXYGEN_EXAMPLE_SOURCES}
                                  "${AOM_ROOT}/apps/aomdec.c"
                                  "${AOM_ROOT}/examples/decode_to_md5.c"
                                  "${AOM_ROOT}/examples/decode_with_drops.c"
                                  "${AOM_ROOT}/examples/simple_decoder.c")

  set(AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS}
                                       "Full featured decoder."
                                       "Frame by frame MD5 checksum."
                                       "Drops frames while decoding."
                                       "Simplified decoder loop.")

  set(AOM_DOXYGEN_SECTIONS ${AOM_DOXYGEN_SECTIONS} "av1_decoder decoder")

  set(AOM_DOXYGEN_SOURCES ${AOM_DOXYGEN_SOURCES} "${AOM_ROOT}/aom/aomdx.h"
                          "${AOM_ROOT}/usage_dx.dox"
                          "${AOM_ROOT}/av1/decoder/decoder.h")

  if(CONFIG_ANALYZER)
    set(AOM_DOXYGEN_EXAMPLE_SOURCES ${AOM_DOXYGEN_EXAMPLE_SOURCES}
                                    "${AOM_ROOT}/examples/analyzer.cc")

    set(AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS}
                                         "Bitstream analyzer.")
  endif()

  if(CONFIG_INSPECTION)
    set(AOM_DOXYGEN_EXAMPLE_SOURCES ${AOM_DOXYGEN_EXAMPLE_SOURCES}
                                    "${AOM_ROOT}/examples/inspect.c")

    set(AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS}
                                         "Bitstream inspector.")
  endif()

  set(AOM_DOXYGEN_SOURCES ${AOM_DOXYGEN_SOURCES}
                          "${AOM_ROOT}/doc/dev_guide/av1_decoder.dox")
endif()

if(CONFIG_AV1_ENCODER)
  set(AOM_DOXYGEN_EXAMPLE_SOURCES ${AOM_DOXYGEN_EXAMPLE_SOURCES}
                                  "${AOM_ROOT}/apps/aomenc.c"
                                  "${AOM_ROOT}/examples/lossless_encoder.c"
                                  "${AOM_ROOT}/examples/set_maps.c"
                                  "${AOM_ROOT}/examples/simple_encoder.c"
                                  "${AOM_ROOT}/examples/twopass_encoder.c")

  set(AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS}
                                       "Full featured encoder."
                                       "Simplified lossless encoder."
                                       "Set active and ROI maps."
                                       "Simplified encoder loop."
                                       "Two-pass encoder loop.")

  set(AOM_DOXYGEN_EXAMPLE_SOURCES ${AOM_DOXYGEN_EXAMPLE_SOURCES}
                                  "${AOM_ROOT}/examples/scalable_encoder.c")

  set(AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS}
                                       "Scalable encoder loop.")

  set(AOM_DOXYGEN_EXAMPLE_SOURCES ${AOM_DOXYGEN_EXAMPLE_SOURCES}
                                  "${AOM_ROOT}/examples/svc_encoder_rtc.cc")

  set(AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS}
                                       "Layered encoder for RTC.")

  set(AOM_DOXYGEN_SECTIONS ${AOM_DOXYGEN_SECTIONS} "av1_encoder encoder")

  set(AOM_DOXYGEN_SOURCES ${AOM_DOXYGEN_SOURCES} "${AOM_ROOT}/aom/aomcx.h"
                          "${AOM_ROOT}/usage_cx.dox")
  set(AOM_DOXYGEN_SOURCES ${AOM_DOXYGEN_SOURCES}
                          "${AOM_ROOT}/doc/dev_guide/av1_encoder.dox")
  set(AOM_DOXYGEN_SOURCES
      ${AOM_DOXYGEN_SOURCES}
      "${AOM_ROOT}/aom_scale/yv12config.h"
      "${AOM_ROOT}/av1/encoder/bitstream.h"
      "${AOM_ROOT}/av1/encoder/block.h"
      "${AOM_ROOT}/av1/encoder/aq_cyclicrefresh.h"
      "${AOM_ROOT}/av1/encoder/encode_strategy.c"
      "${AOM_ROOT}/av1/encoder/encode_strategy.h"
      "${AOM_ROOT}/av1/encoder/encodeframe.c"
      "${AOM_ROOT}/av1/encoder/encoder.c"
      "${AOM_ROOT}/av1/encoder/encoder.h"
      "${AOM_ROOT}/av1/encoder/encodetxb.h"
      "${AOM_ROOT}/av1/encoder/firstpass.h"
      "${AOM_ROOT}/av1/encoder/gop_structure.h"
      "${AOM_ROOT}/av1/encoder/interp_search.c"
      "${AOM_ROOT}/av1/encoder/intra_mode_search.h"
      "${AOM_ROOT}/av1/encoder/intra_mode_search.c"
      "${AOM_ROOT}/av1/encoder/intra_mode_search_utils.h"
      "${AOM_ROOT}/av1/encoder/lookahead.h"
      "${AOM_ROOT}/av1/encoder/palette.h"
      "${AOM_ROOT}/av1/encoder/palette.c"
      "${AOM_ROOT}/av1/encoder/partition_search.h"
      "${AOM_ROOT}/av1/encoder/partition_search.c"
      "${AOM_ROOT}/av1/encoder/pass2_strategy.h"
      "${AOM_ROOT}/av1/encoder/pass2_strategy.c"
      "${AOM_ROOT}/av1/encoder/pickcdef.h"
      "${AOM_ROOT}/av1/encoder/picklpf.h"
      "${AOM_ROOT}/av1/encoder/pickrst.h"
      "${AOM_ROOT}/av1/encoder/ratectrl.c"
      "${AOM_ROOT}/av1/encoder/ratectrl.h"
      "${AOM_ROOT}/av1/encoder/rc_utils.h"
      "${AOM_ROOT}/av1/encoder/rdopt.h"
      "${AOM_ROOT}/av1/encoder/rdopt.c"
      "${AOM_ROOT}/av1/encoder/speed_features.h"
      "${AOM_ROOT}/av1/encoder/svc_layercontext.c"
      "${AOM_ROOT}/av1/encoder/svc_layercontext.h"
      "${AOM_ROOT}/av1/encoder/temporal_filter.h"
      "${AOM_ROOT}/av1/encoder/temporal_filter.c"
      "${AOM_ROOT}/av1/encoder/tpl_model.h"
      "${AOM_ROOT}/av1/encoder/tx_search.h"
      "${AOM_ROOT}/av1/encoder/txb_rdopt.h"
      "${AOM_ROOT}/av1/encoder/var_based_part.h"
      "${AOM_ROOT}/av1/encoder/nonrd_opt.h"
      "${AOM_ROOT}/av1/encoder/nonrd_pickmode.c")
endif()

if(CONFIG_AV1_DECODER AND CONFIG_AV1_ENCODER)
  set(AOM_DOXYGEN_EXAMPLE_SOURCES ${AOM_DOXYGEN_EXAMPLE_SOURCES}
                                  "${AOM_ROOT}/examples/aom_cx_set_ref.c")

  set(AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS}
                                       "Set encoder reference frame.")
endif()

if(CONFIG_AV1_ENCODER)
  set(AOM_DOXYGEN_EXAMPLE_SOURCES ${AOM_DOXYGEN_EXAMPLE_SOURCES}
                                  "${AOM_ROOT}/examples/lightfield_encoder.c")

  set(AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS}
                                       "Lightfield encoder example.")
endif()

if(CONFIG_AV1_DECODER)
  set(AOM_DOXYGEN_EXAMPLE_SOURCES
      ${AOM_DOXYGEN_EXAMPLE_SOURCES}
      "${AOM_ROOT}/examples/lightfield_tile_list_decoder.c")

  set(AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS}
                                       "Lightfield tile list decoder example.")
endif()

if(CONFIG_AV1_DECODER)
  set(AOM_DOXYGEN_EXAMPLE_SOURCES ${AOM_DOXYGEN_EXAMPLE_SOURCES}
                                  "${AOM_ROOT}/examples/lightfield_decoder.c")

  set(AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS}
                                       "Lightfield decoder example.")
endif()

if(CONFIG_AV1_DECODER AND CONFIG_AV1_ENCODER)
  set(AOM_DOXYGEN_EXAMPLE_SOURCES
      ${AOM_DOXYGEN_EXAMPLE_SOURCES}
      "${AOM_ROOT}/examples/lightfield_bitstream_parsing.c")

  set(AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS}
                                       "Lightfield bitstream parsing example.")
endif()

# Iterates over list named by $list_name and appends each item to $AOM_DOXYFILE
# as values assigned to $var_name with no line breaks between list items.
# Appends a new line after the entire config variable is expanded.
function(write_cmake_list_to_doxygen_config_var var_name list_name)
  unset(output_string)
  foreach(list_item ${${list_name}})
    set(output_string "${output_string} ${list_item} ")
  endforeach()
  string(STRIP "${output_string}" output_string)
  file(APPEND "${AOM_DOXYFILE}" "${var_name} += ${output_string}\n")
endfunction()

function(get_name file_path name_var)
  get_filename_component(file_basename ${file_path} NAME)
  get_filename_component(${name_var} ${file_basename} NAME_WE)
  set(${name_var} ${${name_var}} PARENT_SCOPE)
endfunction()

function(setup_documentation_targets)

  # Sanity check: the lengths of these lists must match.
  list(LENGTH AOM_DOXYGEN_EXAMPLE_SOURCES num_sources)
  list(LENGTH AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS num_descs)
  if(NOT ${num_sources} EQUAL ${num_descs})
    message(FATAL_ERROR "Unequal example and description totals.")
  endif()

  # Take the list of examples and produce example_basename.dox for each file in
  # the list.
  file(MAKE_DIRECTORY "${AOM_DOXYGEN_OUTPUT_DIR}")
  foreach(example_file ${AOM_DOXYGEN_EXAMPLE_SOURCES})
    unset(example_basename)
    get_name("${example_file}" "example_name")
    set(example_dox "${AOM_DOXYGEN_OUTPUT_DIR}/${example_name}.dox")
    set(dox_string "/*!\\page example_${example_name} ${example_name}\n")
    set(dox_string "${dox_string} \\includelineno ${example_file}\n*/\n")
    file(WRITE "${example_dox}" ${dox_string})
    set(AOM_DOXYGEN_SOURCES ${AOM_DOXYGEN_SOURCES} "${example_dox}")
  endforeach()

  # Generate samples.dox, an index page that refers to the example_basename.dox
  # files that were just created.
  set(samples_header "
/*!\\page samples Sample Code
This SDK includes a number of sample applications. Each sample documents a
feature of the SDK in both prose and the associated C code. The following
samples are included:
")

  set(utils_desc "
In addition, the SDK contains a number of utilities. Since these utilities are
built upon the concepts described in the sample code listed above, they are not
documented in pieces like the samples are. Their source is included here for
reference. The following utilities are included:
")

  # Write the description for the samples section.
  set(samples_dox "${AOM_CONFIG_DIR}/samples.dox")
  file(WRITE "${samples_dox}" "${samples_header}\n")

  # Iterate over $AOM_DOXYGEN_EXAMPLE_SOURCES and
  # $AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS and massage example names as required by
  # AV1's doxygen setup.
  math(EXPR max_example_index "${num_sources} - 1")
  foreach(NUM RANGE ${max_example_index})
    list(GET AOM_DOXYGEN_EXAMPLE_SOURCES ${NUM} ex_name)
    get_name("${ex_name}" "ex_name")

    # AV1's doxygen lists aomdec and aomenc as utils apart from the examples.
    # Save the indexes for another pass.
    if("${ex_name}" MATCHES "aomdec\|aomenc")
      set(util_indexes "${util_indexes}" "${NUM}")
      continue()
    endif()
    list(GET AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${NUM} ex_desc)
    file(APPEND "${samples_dox}" " - \\subpage example_${ex_name} ${ex_desc}\n")
  endforeach()

  # Write the description and index for the utils.
  file(APPEND "${samples_dox}" "${utils_desc}\n")
  foreach(util_index ${util_indexes})
    list(GET AOM_DOXYGEN_EXAMPLE_SOURCES ${util_index} ex_name)
    get_name("${ex_name}" "ex_name")
    list(GET AOM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${util_index} ex_desc)
    file(APPEND "${samples_dox}" " - \\subpage example_${ex_name} ${ex_desc}\n")
  endforeach()
  file(APPEND "${samples_dox}" "*/")

  # Add $samples_dox to the doxygen inputs.
  get_filename_component(samples_dox ${samples_dox} NAME)
  set(AOM_DOXYGEN_SOURCES ${AOM_DOXYGEN_SOURCES} ${samples_dox})

  # There are issues to show Markdown file for old Doxygen version. Here, only
  # enable Markdown support for 1.8.16 or newer.
  if(${DOXYGEN_VERSION_VALUE} GREATER_EQUAL 1008016)
    set(AOM_DOXYGEN_SECTIONS ${AOM_DOXYGEN_SECTIONS} "av1_md_support")
    set(AOM_DOXYGEN_SOURCES ${AOM_DOXYGEN_SOURCES} "${AOM_ROOT}/README.md")
    # Uncomment and add AlgorithmDescription.md in result page when it is done.
    # set(AOM_DOXYGEN_SOURCES ${AOM_DOXYGEN_SOURCES}
    # "${AOM_ROOT}/doc/AlgorithmDescription.md")
  endif()

  # Generate libaom's doxyfile.
  file(WRITE "${AOM_DOXYFILE}" "##\n## GENERATED FILE. DO NOT EDIT\n##\n")
  file(READ "${AOM_ROOT}/${AOM_DOXYGEN_CONFIG_TEMPLATE}" doxygen_template_data)
  file(APPEND "${AOM_DOXYFILE}" ${doxygen_template_data})
  file(APPEND "${AOM_DOXYFILE}"
       "EXAMPLE_PATH += ${AOM_ROOT} ${AOM_ROOT}/examples\n")
  file(APPEND "${AOM_DOXYFILE}"
       "INCLUDE_PATH += ${AOM_CONFIG_DIR} ${AOM_ROOT}\n")
  file(APPEND "${AOM_DOXYFILE}"
       "STRIP_FROM_PATH += ${AOM_ROOT} ${AOM_CONFIG_DIR}\n")
  write_cmake_list_to_doxygen_config_var("INPUT" "AOM_DOXYGEN_SOURCES")
  write_cmake_list_to_doxygen_config_var("ENABLED_SECTIONS"
                                         "AOM_DOXYGEN_SECTIONS")

  # Add AOMedia logo.
  set(aom_logo "aomedia_logo_200.png")
  configure_file(${AOM_ROOT}/${aom_logo} ${AOM_CONFIG_DIR}/${aom_logo} COPYONLY)
  file(APPEND "${AOM_DOXYFILE}"
       "PROJECT_LOGO = ${AOM_CONFIG_DIR}/${aom_logo}\n")

  # Only set HAVE_DOT to YES if dot tool is found.
  if(DOXYGEN_DOT_FOUND)
    file(APPEND "${AOM_DOXYFILE}" "HAVE_DOT = YES\n")
    file(APPEND "${AOM_DOXYFILE}" "DOT_GRAPH_MAX_NODES = 10000\n")
  endif()

  # Add image path.
  file(APPEND "${AOM_DOXYFILE}" "IMAGE_PATH += ${AOM_ROOT}/doc/dev_guide\n")

  # Allow banner style comments
  file(APPEND "${AOM_DOXYFILE}" "JAVADOC_BANNER = YES")

  # Add the doxygen generation rule.
  add_custom_target(docs ALL
                    COMMAND "${DOXYGEN_EXECUTABLE}" "${AOM_DOXYFILE}"
                    DEPENDS "${AOM_DOXYFILE}" ${AOM_DOXYGEN_SOURCES}
                            ${AOM_DOXYGEN_EXAMPLE_SOURCES}
                            "${AOM_DOXYGEN_CONFIG_TEMPLATE}"
                    SOURCES "${AOM_DOXYFILE}" ${AOM_DOXYGEN_SOURCES}
                            ${AOM_DOXYGEN_EXAMPLE_SOURCES}
                            "${AOM_DOXYGEN_CONFIG_TEMPLATE}")
endfunction()
