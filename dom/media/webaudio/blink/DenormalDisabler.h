/*
 * Copyright (C) 2011, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef DenormalDisabler_h
#define DenormalDisabler_h

#include <float.h>

#if defined(__GNUC__) && defined(__SSE__) && !defined(__x86_64__)
#  include <cstdint>
#endif
#include <cmath>
#include <cstring>

namespace WebCore {

// Deal with denormals. They can very seriously impact performance on x86.

// Define HAVE_DENORMAL if we support flushing denormals to zero.

#if defined(XP_WIN) && defined(_MSC_VER)
// Windows compiled using MSVC with SSE2
#  define HAVE_DENORMAL 1
#endif

#if defined(__GNUC__) && defined(__SSE__)
#  define HAVE_DENORMAL 1
#endif

#if defined(__arm__) || defined(__aarch64__)
#  define HAVE_DENORMAL 1
#endif

#ifdef HAVE_DENORMAL
namespace detail {

#  if defined(__GNUC__) && defined(__SSE__)
inline bool isDAZSupported() {
#    if defined(__x86_64__)
  return true;
#    else
  static bool s_isInited = false;
  static bool s_isSupported = false;
  if (s_isInited) {
    return s_isSupported;
  }

  struct fxsaveResult {
    uint8_t before[28];
    uint32_t CSRMask;
    uint8_t after[480];
  } __attribute__((aligned(16)));

  fxsaveResult registerData;
  memset(&registerData, 0, sizeof(fxsaveResult));
  asm volatile("fxsave %0" : "=m"(registerData));
  s_isSupported = registerData.CSRMask & 0x0040;
  s_isInited = true;
  return s_isSupported;
#    endif
}

inline int getCSR() {
  int result;
  asm volatile("stmxcsr %0" : "=m"(result));
  return result;
}

inline void setCSR(int a) {
  int temp = a;
  asm volatile("ldmxcsr %0" : : "m"(temp));
}

inline unsigned setDenormalMode(bool aDisabled) {
  unsigned saved = getCSR();
  unsigned mask = isDAZSupported() ? 0x8040 : 0x8000;
  setCSR(aDisabled ? (saved | mask) : (saved & ~mask));
  return saved;
}

inline void restoreDenormalMode(unsigned aSaved) { setCSR(aSaved); }

#  elif defined(XP_WIN) && defined(_MSC_VER)
inline unsigned setDenormalMode(bool aDisabled) {
  // Save the current state before changing the mode.
  //
  // http://stackoverflow.com/questions/637175/possible-bug-in-controlfp-s-may-not-restore-control-word-correctly
  unsigned saved;
  _controlfp_s(&saved, 0, 0);
  unsigned unused;
  _controlfp_s(&unused, aDisabled ? _DN_FLUSH : _DN_SAVE, _MCW_DN);
  return saved;
}

inline void restoreDenormalMode(unsigned aSaved) {
  unsigned unused;
  _controlfp_s(&unused, aSaved, _MCW_DN);
}

#  elif defined(__arm__) || defined(__aarch64__)
inline int getStatusWord() {
  int result;
#    if defined(__aarch64__)
  asm volatile("mrs %x[result], FPCR" : [result] "=r"(result));
#    else
  asm volatile("vmrs %[result], FPSCR" : [result] "=r"(result));
#    endif
  return result;
}

inline void setStatusWord(int a) {
#    if defined(__aarch64__)
  asm volatile("msr FPCR, %x[src]" : : [src] "r"(a));
#    else
  asm volatile("vmsr FPSCR, %[src]" : : [src] "r"(a));
#    endif
}

inline unsigned setDenormalMode(bool aDisabled) {
  unsigned saved = getStatusWord();
  // Bit 24 is the flush-to-zero mode control bit. Setting it to 1 flushes
  // denormals to 0.
  setStatusWord(aDisabled ? (saved | (1u << 24)) : (saved & ~(1u << 24)));
  return saved;
}

inline void restoreDenormalMode(unsigned aSaved) { setStatusWord(aSaved); }

#  endif
}  // namespace detail

// Disable denormals for the duration of this scope.
class DenormalDisabler {
 public:
  DenormalDisabler() : m_savedCSR(detail::setDenormalMode(true)) {}

  ~DenormalDisabler() { detail::restoreDenormalMode(m_savedCSR); }

  // This is a nop if we can flush denormals to zero in hardware.
  static inline float flushDenormalFloatToZero(float f) { return f; }

 private:
  unsigned m_savedCSR;
};

// Restore denormals for the duration of this scope if there is a
// DenormalDisabler somewhere higher on the stack.
class DenormalEnabler {
 public:
  DenormalEnabler() : m_savedCSR(detail::setDenormalMode(false)) {}

  ~DenormalEnabler() { detail::restoreDenormalMode(m_savedCSR); }

 private:
  unsigned m_savedCSR;
};

#else
// FIXME: add implementations for other architectures and compilers
class DenormalDisabler {
 public:
  DenormalDisabler() {}

  // Assume the worst case that other architectures and compilers
  // need to flush denormals to zero manually.
  static inline float flushDenormalFloatToZero(float f) {
    return (fabs(f) < FLT_MIN) ? 0.0f : f;
  }
};

class DenormalEnabler {
 public:
  DenormalEnabler() {}
};

#endif

}  // namespace WebCore
#endif  // DenormalDisabler_h
