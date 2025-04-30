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

if("${AOM_TARGET_CPU}" STREQUAL "arm64")
  set(AOM_ARCH_ARM 1)
  set(AOM_ARCH_AARCH64 1)

  set(ARM64_FLAVORS "NEON;ARM_CRC32;NEON_DOTPROD;NEON_I8MM;SVE;SVE2")
  set(AOM_ARM_CRC32_DEFAULT_FLAG "-march=armv8-a+crc")
  set(AOM_NEON_DOTPROD_DEFAULT_FLAG "-march=armv8.2-a+dotprod")
  set(AOM_NEON_I8MM_DEFAULT_FLAG "-march=armv8.2-a+dotprod+i8mm")
  set(AOM_SVE_DEFAULT_FLAG "-march=armv8.2-a+dotprod+i8mm+sve")
  set(AOM_SVE2_DEFAULT_FLAG "-march=armv9-a+i8mm+sve2") # SVE2 is a v9-only
                                                        # feature

  # Check that the compiler flag to enable each flavor is supported by the
  # compiler. This may not be the case for new architecture features on old
  # compiler versions.
  foreach(flavor ${ARM64_FLAVORS})
    if(ENABLE_${flavor} AND NOT DEFINED AOM_${flavor}_FLAG)
      set(AOM_${flavor}_FLAG "${AOM_${flavor}_DEFAULT_FLAG}")
      string(TOLOWER "${flavor}" flavor_lower)

      # Do not use check_c_compiler_flag here since the regex used to match
      # against stderr does not recognise the "invalid feature modifier" error
      # produced by certain versions of GCC, leading to the feature being
      # incorrectly marked as available.
      set(OLD_CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS})
      set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${AOM_${flavor}_FLAG}")
      unset(FLAG_SUPPORTED)
      aom_check_source_compiles("arm_feature_flag_${flavor_lower}_available"
                                "static void function(void) {}" FLAG_SUPPORTED)
      set(CMAKE_REQUIRED_FLAGS ${OLD_CMAKE_REQUIRED_FLAGS})

      if(NOT ${FLAG_SUPPORTED})
        set(ENABLE_${flavor} 0)
      endif()
    endif()
  endforeach()

  # SVE and SVE2 require that the Neon-SVE bridge header is also available.
  if(ENABLE_SVE OR ENABLE_SVE2)
    set(OLD_CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS})
    set(OLD_CMAKE_TRY_COMPILE_TARGET_TYPE ${CMAKE_TRY_COMPILE_TARGET_TYPE})
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${AOM_SVE_FLAG}")
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    aom_check_source_compiles("arm_neon_sve_bridge_available" "
#ifndef __ARM_NEON_SVE_BRIDGE
#error 1
#endif
#include <arm_sve.h>
#include <arm_neon_sve_bridge.h>" HAVE_SVE_HEADERS)
    # Check whether the compiler can compile SVE functions that require
    # backup/restore of SVE registers according to AAPCS. Clang for Windows used
    # to fail this, see https://github.com/llvm/llvm-project/issues/80009.
    aom_check_source_compiles("arm_sve_preserve" "
#include <arm_sve.h>
void other(void)\;
svfloat32_t func(svfloat32_t a) {
  other()\;
  return a\;
}" CAN_COMPILE_SVE)
    set(CMAKE_REQUIRED_FLAGS ${OLD_CMAKE_REQUIRED_FLAGS})
    set(CMAKE_TRY_COMPILE_TARGET_TYPE ${OLD_CMAKE_TRY_COMPILE_TARGET_TYPE})
    if(HAVE_SVE_HEADERS EQUAL 0 OR CAN_COMPILE_SVE EQUAL 0)
      set(ENABLE_SVE 0)
      set(ENABLE_SVE2 0)
    endif()
  endif()

  foreach(flavor ${ARM64_FLAVORS})
    if(ENABLE_${flavor})
      set(HAVE_${flavor} 1)
    else()
      set(HAVE_${flavor} 0)
      string(TOLOWER ${flavor} flavor)
      set(AOM_RTCD_FLAGS ${AOM_RTCD_FLAGS} --disable-${flavor})
    endif()
  endforeach()

elseif("${AOM_TARGET_CPU}" MATCHES "^arm")
  set(AOM_ARCH_ARM 1)

  if(ENABLE_NEON)
    set(HAVE_NEON 1)
  else()
    set(HAVE_NEON 0)
    set(AOM_RTCD_FLAGS ${AOM_RTCD_FLAGS} --disable-neon)
  endif()

elseif("${AOM_TARGET_CPU}" MATCHES "ppc")
  set(AOM_ARCH_PPC 1)

  if(ENABLE_VSX)
    set(HAVE_VSX 1)
  else()
    set(HAVE_VSX 0)
    set(AOM_RTCD_FLAGS ${AOM_RTCD_FLAGS} --disable-vsx)
  endif()
elseif("${AOM_TARGET_CPU}" MATCHES "^x86")
  if("${AOM_TARGET_CPU}" STREQUAL "x86")
    set(AOM_ARCH_X86 1)
  elseif("${AOM_TARGET_CPU}" STREQUAL "x86_64")
    set(AOM_ARCH_X86_64 1)
  endif()

  set(X86_FLAVORS "MMX;SSE;SSE2;SSE3;SSSE3;SSE4_1;SSE4_2;AVX;AVX2")
  foreach(flavor ${X86_FLAVORS})
    if(ENABLE_${flavor} AND NOT disable_remaining_flavors)
      set(HAVE_${flavor} 1)
    else()
      set(disable_remaining_flavors 1)
      set(HAVE_${flavor} 0)
      string(TOLOWER ${flavor} flavor)
      set(AOM_RTCD_FLAGS ${AOM_RTCD_FLAGS} --disable-${flavor})
    endif()
  endforeach()
elseif("${AOM_TARGET_CPU}" MATCHES "riscv")
  set(AOM_ARCH_RISCV64 1)

  if(ENABLE_RVV)
    set(HAVE_RVV 1)
  else()
    set(HAVE_RVV 0)
    set(AOM_RTCD_FLAGS ${AOM_RTCD_FLAGS} --disable-rvv)
  endif()
endif()
