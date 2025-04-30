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

set(REQUIRED_ARGS "AOM_ROOT" "AOM_CONFIG_DIR" "CMAKE_INSTALL_PREFIX"
                  "CMAKE_INSTALL_BINDIR" "CMAKE_INSTALL_INCLUDEDIR"
                  "CMAKE_INSTALL_LIBDIR" "CMAKE_PROJECT_NAME"
                  "CONFIG_MULTITHREAD")

foreach(arg ${REQUIRED_ARGS})
  if("${${arg}}" STREQUAL "")
    message(FATAL_ERROR "${arg} must not be empty.")
  endif()
endforeach()

include("${AOM_ROOT}/build/cmake/util.cmake")

extract_version_string("${AOM_CONFIG_DIR}/config/aom_version.h" aom_version)

# Create a version string suitable for comparison using the RPM version compare
# algorithm: strip out everything after the number.
string(FIND "${aom_version}" "-" dash_pos)
if(${dash_pos} EQUAL -1)
  set(package_version "${aom_version}")
else()
  string(SUBSTRING "${aom_version}" 0 ${dash_pos} package_version)
endif()

# Write pkg-config info.
set(prefix "${CMAKE_INSTALL_PREFIX}")
set(bindir "${CMAKE_INSTALL_BINDIR}")
set(includedir "${CMAKE_INSTALL_INCLUDEDIR}")
set(libdir "${CMAKE_INSTALL_LIBDIR}")
set(pkgconfig_file "${AOM_CONFIG_DIR}/aom.pc")
string(TOLOWER ${CMAKE_PROJECT_NAME} pkg_name)
file(WRITE "${pkgconfig_file}" "# libaom pkg-config.\n")
file(APPEND "${pkgconfig_file}" "prefix=${prefix}\n")
file(APPEND "${pkgconfig_file}" "exec_prefix=\${prefix}\n")
file(APPEND "${pkgconfig_file}" "includedir=\${prefix}/${includedir}\n")
file(APPEND "${pkgconfig_file}" "libdir=\${exec_prefix}/${libdir}\n\n")
file(APPEND "${pkgconfig_file}" "Name: ${pkg_name}\n")
file(
  APPEND "${pkgconfig_file}"
  "Description: Alliance for Open Media AV1 codec library v${aom_version}.\n")
file(APPEND "${pkgconfig_file}" "Version: ${package_version}\n")
file(APPEND "${pkgconfig_file}" "Requires:")
if(CONFIG_TUNE_VMAF)
  file(APPEND "${pkgconfig_file}" " libvmaf")
endif()
if(CONFIG_TUNE_BUTTERAUGLI)
  file(APPEND "${pkgconfig_file}" " libjxl")
endif()
file(APPEND "${pkgconfig_file}" "\nConflicts:\n")
file(APPEND "${pkgconfig_file}" "Libs: -L\${libdir} -l${pkg_name}\n")
file(APPEND "${pkgconfig_file}" "Libs.private:")
if(NOT WIN32 AND NOT APPLE)
  file(APPEND "${pkgconfig_file}" " -lm")
endif()
if(CONFIG_MULTITHREAD AND CMAKE_THREAD_LIBS_INIT)
  file(APPEND "${pkgconfig_file}" " ${CMAKE_THREAD_LIBS_INIT}")
endif()
file(APPEND "${pkgconfig_file}" "\nCflags: -I\${includedir}\n")
