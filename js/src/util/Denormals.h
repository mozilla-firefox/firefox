/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef util_Denormals_h
#define util_Denormals_h

#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/SSE.h"

#include <stdint.h>
#include <string.h>

#if defined(JS_CODEGEN_X86) || defined(JS_CODEGEN_X64)
#  include <immintrin.h>
#endif

namespace js {

// MXCSR bits that suppress denormals. FTZ flushes denormal results to zero,
// DAZ treats denormal inputs as zero.
static constexpr uint32_t MxcsrFlushToZero = 0x8000;
static constexpr uint32_t MxcsrDenormalsAreZero = 0x0040;
static constexpr uint32_t MxcsrDenormalsDisabled =
    MxcsrFlushToZero | MxcsrDenormalsAreZero;

#if defined(JS_CODEGEN_X86) || defined(JS_CODEGEN_X64)
inline uint32_t ReadMxcsr() { return _mm_getcsr(); }
#endif

#if defined(JS_CODEGEN_X86)
// Whether the processor supports the MXCSR DAZ flag, using the MXCSR_MASK
// check from Intel SDM Vol. 1, 11.6.6 "Guidelines for Writing to the MXCSR
// Register". Requires FXSR, which SSE implies.
#  if defined(__GNUC__) || defined(__clang__)
__attribute__((target("fxsr")))
#  endif
inline bool MxcsrSupportsDenormalsAreZero() {
  // FXSAVE may leave reserved areas untouched, so start from zero. A zero
  // MXCSR_MASK means the default mask, which lacks DAZ.
  alignas(16) uint8_t fxsaveArea[512] = {};
  _fxsave(fxsaveArea);
  uint32_t mxcsrMask;
  memcpy(&mxcsrMask, fxsaveArea + 28, sizeof(mxcsrMask));
  return (mxcsrMask & MxcsrDenormalsAreZero) != 0;
}
#endif

// Whether both our JIT backend and architecture support disabling denormals.
inline bool CanDisableDenormals() {
#if defined(JS_CODEGEN_X64)
  // All x86-64 processors support SSE2 and the MXCSR DAZ flag. libgcc's
  // crtfastmath.c relies on this as well.
  return true;
#elif defined(JS_CODEGEN_X86)
  // Some SSE2 processors (early Pentium 4 models) reserve the DAZ flag, and
  // setting it raises #GP. See Intel SDM Vol. 1, 10.2.3.4
  // "Denormals-Are-Zeros".
  static const bool supported =
      mozilla::supports_sse2() && MxcsrSupportsDenormalsAreZero();
  return supported;
#else
  return false;
#endif
}

// Whether the calling thread's floating point environment flushes denormal
// inputs or results to zero.
//
// Returns false on platforms where we cannot read the FP control register.
inline bool DenormalsDisabled() {
#if defined(JS_CODEGEN_X86) || defined(JS_CODEGEN_X64)
  return (ReadMxcsr() & MxcsrDenormalsDisabled) != 0;
#elif defined(__GNUC__) && defined(__aarch64__)
  uint64_t fpcr;
  asm volatile("mrs %0, FPCR" : "=r"(fpcr));
  return (fpcr & (1 << 24)) != 0;
#elif defined(__GNUC__) && defined(__arm__)
  uint32_t fpscr;
  asm volatile("vmrs %0, FPSCR" : "=r"(fpscr));
  return (fpscr & (1 << 24)) != 0;
#else
  return false;
#endif
}

// Assert that denormals are enabled both on entry to and on exit from a scope.
class MOZ_RAII AutoAssertDenormalsEnabled {
 public:
  AutoAssertDenormalsEnabled() { MOZ_ASSERT(!DenormalsDisabled()); }
  ~AutoAssertDenormalsEnabled() { MOZ_ASSERT(!DenormalsDisabled()); }
};

}  // namespace js

#endif /* util_Denormals_h */
