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

# Converts spaces in $in_string to semicolons and writes the output to
# $out_string. In CMake's eyes this converts the input string to a list.
function(listify_string in_string out_string)
  string(REPLACE " " ";" ${out_string} ${in_string})
  set(${out_string} "${${out_string}}" PARENT_SCOPE)
endfunction()

set(REQUIRED_ARGS "AOM_ROOT" "AOM_CONFIG_DIR" "AOM_DIST_DIR" "AOM_DIST_INCLUDES"
                  "AOM_DIST_LIBS" "ENABLE_DOCS")

foreach(arg ${REQUIRED_ARGS})
  if("${${arg}}" STREQUAL "")
    message(FATAL_ERROR "${arg} must not be empty.")
  endif()
endforeach()

if(ENABLE_DOCS)
  file(INSTALL "${AOM_CONFIG_DIR}/docs" DESTINATION "${AOM_DIST_DIR}")
endif()

if(AOM_DIST_EXAMPLES)
  listify_string("${AOM_DIST_EXAMPLES}" "AOM_DIST_EXAMPLES")
  foreach(example ${AOM_DIST_EXAMPLES})
    if(NOT "${example}" MATCHES "aomdec\|aomenc")
      file(INSTALL "${example}" DESTINATION "${AOM_DIST_DIR}/bin/examples")
    endif()
  endforeach()
endif()

if(AOM_DIST_TOOLS)
  listify_string("${AOM_DIST_TOOLS}" "AOM_DIST_TOOLS")
  foreach(tool ${AOM_DIST_TOOLS})
    file(INSTALL "${tool}" DESTINATION "${AOM_DIST_DIR}/bin/tools")
  endforeach()
endif()

if(AOM_DIST_APPS)
  listify_string("${AOM_DIST_APPS}" "AOM_DIST_APPS")
  foreach(app ${AOM_DIST_APPS})
    file(INSTALL "${app}" DESTINATION "${AOM_DIST_DIR}/bin")
  endforeach()
endif()

listify_string("${AOM_DIST_INCLUDES}" "AOM_DIST_INCLUDES")
foreach(inc ${AOM_DIST_INCLUDES})
  file(INSTALL "${inc}" DESTINATION "${AOM_DIST_DIR}/include/aom")
endforeach()

listify_string("${AOM_DIST_LIBS}" "AOM_DIST_LIBS")
foreach(lib ${AOM_DIST_LIBS})
  file(INSTALL "${lib}" DESTINATION "${AOM_DIST_DIR}/lib")
endforeach()
