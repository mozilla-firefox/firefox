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
cmake_minimum_required(VERSION 3.16)

set(REQUIRED_ARGS "AOM_ROOT" "AOM_CONFIG_DIR" "GIT_EXECUTABLE"
                  "PERL_EXECUTABLE")

foreach(arg ${REQUIRED_ARGS})
  if("${${arg}}" STREQUAL "")
    message(FATAL_ERROR "${arg} must not be empty.")
  endif()
endforeach()

include("${AOM_ROOT}/build/cmake/util.cmake")

# Generate the version string for this run.
unset(aom_version)
if(EXISTS "${GIT_EXECUTABLE}")
  execute_process(COMMAND ${GIT_EXECUTABLE}
                          --git-dir=${AOM_ROOT}/.git describe
                          --match=v[0-9]*
                  OUTPUT_VARIABLE aom_version
                  ERROR_QUIET
                  RESULT_VARIABLE version_check_result)

  if(${version_check_result} EQUAL 0)
    string(STRIP "${aom_version}" aom_version)

    # Remove the leading 'v' from the version string.
    string(FIND "${aom_version}" "v" v_pos)
    if(${v_pos} EQUAL 0)
      string(SUBSTRING "${aom_version}" 1 -1 aom_version)
    endif()
  else()
    set(aom_version "")
  endif()
endif()

if("${aom_version}" STREQUAL "")
  set(aom_version "${AOM_ROOT}/CHANGELOG")
endif()

unset(last_aom_version)
set(version_file "${AOM_CONFIG_DIR}/config/aom_version.h")
if(EXISTS "${version_file}")
  extract_version_string("${version_file}" last_aom_version)
  if("${aom_version}" MATCHES "CHANGELOG$")
    set(aom_version "${last_aom_version}")
  endif()
endif()

if(NOT "${aom_version}" STREQUAL "${last_aom_version}")
  # TODO(tomfinegan): Perl dependency is unnecessary. CMake can do everything
  # that is done by version.pl on its own (if a bit more verbosely...).
  execute_process(COMMAND ${PERL_EXECUTABLE}
                          "${AOM_ROOT}/build/cmake/version.pl"
                          --version_data=${aom_version}
                          --version_filename=${version_file} VERBATIM)
endif()
