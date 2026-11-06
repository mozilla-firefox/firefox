/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_htmlaccel_htmlaccel_h
#define mozilla_htmlaccel_htmlaccel_h

#include <string.h>
#include <stdint.h>

// Avoid adding more Gecko-specific headers to keep it easy enough to
// copy and paste the contents of this file to Compiler Explorer.
#include "mozilla/Attributes.h"

// This file provides SIMD code for skipping over characters that
// the caller doesn't need to act upon. For example, this code can
// skip over characters that the HTML tokenizer doesn't need to handle
// specially in a given state or this code could be used to skip over
// characters that don't need to be escaped in an HTML serializer.

// ISA SUPPORT: Do not include this file unless the compilation unit is
// being compiled either for little-endian aarch64, for x86/x86_64 with
// at least SSSE3 enabled, or for loongarch64 with LSX enabled. (We're
// actually not using this on 32-bit x86 and are compiling with AVX+BMI
// on x86_64; see below. In the build system, `HTML_ACCEL_FLAGS`
// contains the actually-used flags.)
//
// It's probably feasible to extend this to support little-endian POWER
// by defining
// MOZ_ALWAYS_INLINE_EVEN_DEBUG uint8x16_t TableLookup(uint8x16_t table,
// uint8x16_t nibbles) {
//  return vec_perm(table, table, nibbles);
// }
// but since I don't have a little-endian POWER system to test with,
// this is left as an exercise to the reader. (The x86_64 reduction
// code should be portable to POWER10 using vec_extractm and the aarch64
// reduction code should be portable to older POWER using vec_max.)
//
// ARMv7 is deliberately not supported due to vqtbl1q_u8 being a newer
// addition to NEON.
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#  error "A little-endian target is required."
#endif
#if !(defined(__aarch64__) || defined(__SSSE3__) || defined(__loongarch_sx))
#  error "Must be targeting SSSE3 or above (notably AVX+BMI), aarch64, or LSX."
#endif

// NOTE: This file uses GCC/clang built-ins that provide SIMD portability.
// Compared to pretending unawareness of what arm_neon.h and tmmintrin.h
// map to in GCC and clang, this has the benefit that the code is not stuck
// at an SSSE3 local maximum but adapts maximally to upgrades to SSE 4.2,
// AVX, and AVX+BMI. (Yes, enabling BMI seems to affect more than just
// __builtin_ctz!)
// (We need to check for __clang__, because clang-cl does not define __GNUC__.)
#if !(defined(__GNUC__) || defined(__clang__))
#  error "A compiler that supports GCC-style portable SIMD is required."
#endif

// # General
//
// There is an entry point per combination of what characters terminate
// the acceleration loop (i.e. characters that the HTML tokenizer would not
// simply skip over). The shared implementation code is inlined into these
// FFI entry point functions, so the parametrization made inside the FFI
// functions constant-propagates through the implementation internals.
//
// The code examines 16 UTF-16 code units at a time as two 128-bit SIMD
// vectors. First, the bytes are regrouped to so that one SIMD vector
// contains the high halves of the UTF-16 code units (zeros for ASCII/Basic
// Latin) and another one contains the low halves.
//
// In the case of the low half, we mask the vector to take the low 4 bits of
// each 8-bit value and do a lookup from a lookup table contained in a SIMD
// vector. The 4 bits index into 16 lanes of the other SIMD vector such that
// we get a vector where the positions corresponding to positions of the
// original code units contain the 8-bit value looked up from by the 4-bit
// index.
//
// The lookup operation is available unconditionally on aarch64. On
// x86/x86_64, it is part of the SSSE3 instruction set extension, which is
// why on x86/x86_64 we must not call into this code unless SSSE3 is
// available. (Each additional level of compiling this code with SSE4.2,
// AVX, or AVX+BMI makes this code shorter, which presumably means more
// efficient, so instead of compiling this just with SSSE3, we compile this
// with AVX+BMI on x86_64, considering that CPUs with such capabilities
// have been available for 12 years at the time of landing this code.)
//
// The lookup table contains the loop-terminating ASCII characters in the
// positions given by their low 4 bits. For example, the less-than sign is
// U+003C, so the value 0x3C is at index 0xC (decimal 12). Positions that
// don’t correspond to a character of interest have the value 1, except lane
// 1 has the placeholder value 2. This way, characters that we don’t want to
// match anything in the lookup table get a non-matching placeholder: U+0001
// gets compared with 2 (semantically U+0002) and everything else not of
// interest gets compared with 1 (semantically U+0001) to produce a
// non-matching lane.
//
// This means that instead of comparing the vector of the low halves of the
// UTF-16 code units against multiple constant vectors each filled in all
// lanes with a given ASCII character of interest, the table lookup gives us
// one vector to compare against where each lane can have a different ASCII
// character of interest to compare with.
//
// This requires the ASCII characters of interest to have mutually distinct
// low 4 bits. This is true for U+0000, &, <, LF, CR, ", and ', but,
// unfortunately, CR, ] and - share the low 4 bits, so cases where we need
// to include a check for ] or - needs to do a separate check, since CR is
// always in the lookup table. Note that it's not worthwhile to pursue
// the low 5 bits instead when possible, because CR and - share the low
// 5 bits, too.
//
// From these operations, we get a vector of 16 8-bit mask lanes where a
// lane is 0xFF if the low 8 bits of the UTF-16 code unit matched an ASCII
// character that terminates the loop and 0x00 otherwise. We lane-wise
// compare the high halves with zero and AND the resulting mask vector
// together with the mask vector that resulted from processing the low 8
// bits to confirm which low 8 bits had 0 as the high 8 bits, i.e. the
// UTF-16 code unit really was Basic Latin.
//
// If we have a configuration that requires terminating the loop on
// surrogates, we check the vector containing the high halves of the UTF-16
// code units for surrogates (by masking certain high bits to compare them
// with a constant) and OR the resulting mask vector together with the
// vector computed above.
//
// Now we have a vector of 16 8-bit mask lanes that corresponds to the input
// of 16 UTF-16 code units to indicate which code units in the run of 16
// UTF-16 code units require terminating the loop (i.e. must not be skipped
// over). At this point, the handling diverges for x86_64 and aarch64.
//
// ## x86_64
//
// We convert the SIMD mask into bits in an ALU register. The operation
// returns a 32-bit type, but only the low 16 bits can be non-zero. If the
// integer is non-zero, the loop terminates, since some lane in the mask was
// non-zero. In this case, we return the number of trailing zeros in the
// integer. (We already know must have a non-zero bit somewhere in the low
// 16 bits, so we can’t end up counting to the high half of the 32-bit type.)
// Due to the little-endian semantics, the first UTF-16 code unit in the
// input corresponds to the least-significant bit in the integer, so when the
// first UTF-16 code unit in the input is unskippable, the least-significant
// bit in the integer is 1, so there are 0 trailing zeros, i.e. 0 skippable
// UTF-16 code units.
//
// ## aarch64
//
// We want to know if any lane is the mask is non-zero to decide whether to
// terminate the loop. If there is a non-zero lane, we want to know the
// position of the first (in the content order of the input UTF-16 text)
// non-zero lane. To accomplish these goals, we bitwise AND the mask vector
// with a vector of 16 constants. Since ANDing with a mask lane set to zero
// results in zero, we need all 16 constants to be non-zero. Yet, we need to
// be able to accommodate the possibility of first lane in content order
// being set, which means we need to compute 0 as the result. To be able to
// compute 0 but have the constants be non-zero, the constants are numbers
// that need be subtracted from 16. That is, the constant vector has lanes
// set to numbers from 16 to 1 (inclusive). We do the reduction of the
// resulting SIMD vector to an ALU integer by taking the value of the lane
// with the largest value.
//
// If no mask lane was set, the max operation results in 0, so if the
// integer is zero, the loop continues. Otherwise, we get the number of
// skippable UTF-16 code units by subtracting the integer from 16. That is,
// if the first UTF-16 unit is unstoppable, we get 16 as the max lane value
// and 16-16=0.
//
// # Alignment
//
// These functions use unaligned SIMD loads, because alignment
// doesn't matter on aarch64 CPUs or on x86_64 CPUs from the most
// recent decade or so. It's not worthwhile to add complexity for
// old CPUs.
//
// # Inlining
//
// This code was designed for inlining the public functions all the
// way to the caller for maximum LICM. However, due to
// https://github.com/llvm/llvm-project/issues/160886 the public
// functions are currently annotated _not_ to be inlined, because
// currently inlining them into the eventual caller results in
// no LICM but leaving them not-inlined results in one level of
// LICM in the leaf function.
//
// # Acknowledments
//
// https://lemire.me/blog/2024/06/08/scan-html-faster-with-simd-instructions-chrome-edition/

#if defined(__aarch64__)

#  include <arm_neon.h>

#elif defined(__loongarch_sx)

#  include <lsxintrin.h>
typedef uint8_t uint8x16_t __attribute__((vector_size(16)));

#else  // x86/x86_64

#  include <tmmintrin.h>
// Using syntax that clang-tidy doesn't like to match GCC guidance.
typedef uint8_t uint8x16_t __attribute__((vector_size(16)));

#endif

namespace mozilla::htmlaccel {

namespace detail {

#if defined(__aarch64__)
// The idea is that when this is ANDed with the mask, we get 0 in the
// non-match positions and the leftmost match ends up with higest number.
// This way, taking the max value of the result is zero if all positions
// are non-match, and otherwise we get a value that when subtracted from
// 16 indicates the index of the leftmost match.
const uint8x16_t INVERTED_ADVANCES = {16, 15, 14, 13, 12, 11, 10, 9,
                                      8,  7,  6,  5,  4,  3,  2,  1};
const uint8x16_t ALL_ONES = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

MOZ_ALWAYS_INLINE_EVEN_DEBUG uint8x16_t TableLookup(uint8x16_t aTable,
                                                    uint8x16_t aNibbles) {
  return vqtbl1q_u8(aTable, aNibbles);
}

#elif defined(__loongarch_sx)

// vshuf.b's selector picks from the second vector when 0..15. Since the nibbles
// are pre-masked, the first vector is a don't-care.
MOZ_ALWAYS_INLINE_EVEN_DEBUG uint8x16_t TableLookup(uint8x16_t aTable,
                                                    uint8x16_t aNibbles) {
  return reinterpret_cast<uint8x16_t>(
      __lsx_vshuf_b(__lsx_vldi(0), reinterpret_cast<__m128i>(aTable),
                    reinterpret_cast<__m128i>(aNibbles)));
}

#else  // x86/x86_64

MOZ_ALWAYS_INLINE_EVEN_DEBUG uint8x16_t TableLookup(uint8x16_t aTable,
                                                    uint8x16_t aNibbles) {
  // GCC wants reinterpret_cast
  return reinterpret_cast<uint8x16_t>(_mm_shuffle_epi8(aTable, aNibbles));
}

#endif

// These formulations optimize nicely, so no point in trying something fancier
// to fill all lanes with the same byte.
const uint8x16_t ALL_ZEROS = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
const uint8x16_t NIBBLE_MASK = {0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF,
                                0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF};
const uint8x16_t SURROGATE_MASK = {0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8,
                                   0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8,
                                   0xF8, 0xF8, 0xF8, 0xF8};
const uint8x16_t SURROGATE_MATCH = {0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8,
                                    0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8,
                                    0xD8, 0xD8, 0xD8, 0xD8};
const uint8x16_t HYPHENS = {'-', '-', '-', '-', '-', '-', '-', '-',
                            '-', '-', '-', '-', '-', '-', '-', '-'};
const uint8x16_t RSQBS = {']', ']', ']', ']', ']', ']', ']', ']',
                          ']', ']', ']', ']', ']', ']', ']', ']'};

// The approach here supports disallowing up to 16 different
// characters that 1) are in the Latin1 range, i.e. U+00FF or
// below, and 2) do not have the lowest 4 bits in common with
// each other.
//
// The code point value of each disallowed character needs
// to be placed in the vector at the position indexed by the
// low 4 bits of the character (low four bits 0 is the leftmost
// position and low four bits 15 is the rightmost position).
//
// U+0001 neither occurs in typical HTML nor is one of the
// code points we care about, so use 1 as the non-matching
// value. We do care about U+0000, unfortunately.
// We use U+0002 at position 1 to make sure it doesn't
// match, either. That is, we put 1 in the positions we
// don't care about except we put 2 at position 1.

/// Disallow U+0000, less-than, ampersand, and carriage return.
const uint8x16_t ZERO_LT_AMP_CR = {0, 2, 1, 1, 1,   1,    '&', 1,
                                   1, 1, 1, 1, '<', '\r', 1,   1};
/// Disallow U+0000, less-than, ampersand, carriage return, and line feed.
const uint8x16_t ZERO_LT_AMP_CR_LF = {0, 2, 1,    1, 1,   1,    '&', 1,
                                      1, 1, '\n', 1, '<', '\r', 1,   1};
/// Disallow less-than, greater-than, ampersand, and no-break space.
const uint8x16_t LT_GT_AMP_NBSP = {0xA0, 2, 1, 1, 1,   1, '&', 1,
                                   1,    1, 1, 1, '<', 1, '>', 1};
/// Disallow less-than, greater-than, ampersand, no-break space, and double
/// quote.
const uint8x16_t LT_GT_AMP_NBSP_QUOT = {0xA0, 2, '"', 1, 1,   1, '&', 1,
                                        1,    1, 1,   1, '<', 1, '>', 1};
/// Disallow U+0000, less-than, and carriage return.
const uint8x16_t ZERO_LT_CR = {0, 2, 1, 1, 1,   1,    1, 1,
                               1, 1, 1, 1, '<', '\r', 1, 1};
/// Disallow U+0000, less-than, carriage return, and line feed.
const uint8x16_t ZERO_LT_CR_LF = {0, 2, 1,    1, 1,   1,    1, 1,
                                  1, 1, '\n', 1, '<', '\r', 1, 1};
/// Disallow U+0000, single quote, ampersand, and carriage return.
const uint8x16_t ZERO_APOS_AMP_CR = {0, 2, 1, 1, 1, 1,    '&', '\'',
                                     1, 1, 1, 1, 1, '\r', 1,   1};
/// Disallow U+0000, single quote, ampersand, carriage return, and line feed.
const uint8x16_t ZERO_APOS_AMP_CR_LF = {0, 2, 1,    1, 1, 1,    '&', '\'',
                                        1, 1, '\n', 1, 1, '\r', 1,   1};
/// Disallow U+0000, double quote, ampersand, and carriage return.
const uint8x16_t ZERO_QUOT_AMP_CR = {0, 2, '"', 1, 1, 1,    '&', 1,
                                     1, 1, 1,   1, 1, '\r', 1,   1};
/// Disallow U+0000, single quote, ampersand, carriage return, and line feed.
const uint8x16_t ZERO_QUOT_AMP_CR_LF = {0, 2, '"',  1, 1, 1,    '&', 1,
                                        1, 1, '\n', 1, 1, '\r', 1,   1};
/// Disallow U+0000 and carriage return.
const uint8x16_t ZERO_CR = {0, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, '\r', 1, 1};
/// Disallow U+0000, carriage return, and line feed.
const uint8x16_t ZERO_CR_LF = {0, 2, 1,    1, 1, 1,    1, 1,
                               1, 1, '\n', 1, 1, '\r', 1, 1};

/// 16 UTF-16 code units as loaded into SIMD registers.
struct Utf16Stride {
  uint8x16_t first;
  uint8x16_t second;
};

/// 16 Latin1 code units as loaded into a SIMD register.
struct Latin1Stride {
  uint8x16_t units;
};

MOZ_ALWAYS_INLINE_EVEN_DEBUG Utf16Stride
LoadStride(const char16_t* aArr /* len = 16 */) {
  Utf16Stride stride;
  // memcpy generates a single unaligned load instruction with both ISAs.
  memcpy(&stride.first, aArr, 16);
  memcpy(&stride.second, aArr + 8, 16);
  return stride;
}

MOZ_ALWAYS_INLINE_EVEN_DEBUG Latin1Stride
LoadStride(const char* aArr /* len = 16 */) {
  Latin1Stride stride;
  // memcpy generates a single unaligned load instruction with both ISAs.
  memcpy(&stride.units, aArr, 16);
  return stride;
}

MOZ_ALWAYS_INLINE_EVEN_DEBUG void StoreStride(char16_t* aOut /* len = 16 */,
                                              const Utf16Stride& aStride) {
  // memcpy generates a single unaligned store instruction with both ISAs.
  memcpy(aOut, &aStride.first, 16);
  memcpy(aOut + 8, &aStride.second, 16);
}

/// Compute a 16-lane mask for for 16 UTF-16 code units, where a lane
/// is 0x00 if OK to skip and 0xFF in not OK to skip.
MOZ_ALWAYS_INLINE_EVEN_DEBUG uint8x16_t StrideToMask(
    const Utf16Stride& aStride, uint8x16_t aTable, bool aAllowSurrogates = true,
    bool aAllowHyphen = true, bool aAllowRightSquareBracket = true) {
  // Each shuffle maps to a single instruction on aarch64.
  // On x86/x86_64, how efficiently these shuffles maps to instructions
  // depends on the level of instruction set extensions chosen, which
  // is the main reason that we compile this file at a higher extension
  // level than the minimum SSSE3 (and the main reason why this file
  // uses GNU C portable SIMD instead of sticking to what's in the
  // Intel-defined headers).
  uint8x16_t low_halves =
      __builtin_shufflevector(aStride.first, aStride.second, 0, 2, 4, 6, 8, 10,
                              12, 14, 16, 18, 20, 22, 24, 26, 28, 30);
  uint8x16_t high_halves =
      __builtin_shufflevector(aStride.first, aStride.second, 1, 3, 5, 7, 9, 11,
                              13, 15, 17, 19, 21, 23, 25, 27, 29, 31);
  uint8x16_t high_half_matches = high_halves == ALL_ZEROS;
  uint8x16_t low_half_matches =
      low_halves == TableLookup(aTable, low_halves & NIBBLE_MASK);
  if (!aAllowHyphen) {  // Assumed to be constant-propagated
    low_half_matches |= low_halves == HYPHENS;
  }
  if (!aAllowRightSquareBracket) {  // Assumed to be constant-propagated
    low_half_matches |= low_halves == RSQBS;
  }
  uint8x16_t ret = low_half_matches & high_half_matches;
  if (!aAllowSurrogates) {  // Assumed to be constant-propagated
    ret |= (high_halves & SURROGATE_MASK) == SURROGATE_MATCH;
  }
  return ret;
}

/// Compute a 16-lane mask for for 16 Latin1 code units, where a lane
/// is 0x00 if OK to skip and 0xFF in not OK to skip.
/// The boolean arguments exist for signature compatibility with the UTF-16
/// case and are unused in the Latin1 case.
MOZ_ALWAYS_INLINE_EVEN_DEBUG uint8x16_t
StrideToMask(const Latin1Stride& aStride, uint8x16_t aTable,
             bool aAllowSurrogates = true, bool aAllowHyphen = true,
             bool aAllowRightSquareBracket = true) {
  // == compares lane-wise and returns a mask vector.
  return aStride.units == TableLookup(aTable, aStride.units & NIBBLE_MASK);
}

template <typename CharT>
MOZ_ALWAYS_INLINE_EVEN_DEBUG uint8x16_t
StrideToMask(const CharT* aArr /* len = 16 */, uint8x16_t aTable,
             bool aAllowSurrogates = true, bool aAllowHyphen = true,
             bool aAllowRightSquareBracket = true) {
  return StrideToMask(LoadStride(aArr), aTable, aAllowSurrogates, aAllowHyphen,
                      aAllowRightSquareBracket);
}

template <bool kWriteThrough, typename CharT>
MOZ_ALWAYS_INLINE_EVEN_DEBUG size_t AccelerateTextNodeImpl(
    const CharT* aInput, const CharT* aEnd, CharT* aOut, uint8x16_t aTable,
    bool aAllowSurrogates, bool aAllowHyphen, bool aAllowRightSquareBracket) {
  const CharT* current = aInput;
  CharT* out = aOut;
  while (aEnd - current >= 16) {
    auto stride = LoadStride(current);
    if constexpr (kWriteThrough) {
      // The entire stride fits, even if the scan stops within it.
      StoreStride(out, stride);
      out += 16;
    }
    uint8x16_t mask = StrideToMask(stride, aTable, aAllowSurrogates,
                                   aAllowHyphen, aAllowRightSquareBracket);
#if defined(__aarch64__)
    uint8_t max = vmaxvq_u8(mask & INVERTED_ADVANCES);
    if (max != 0) {
      return size_t((current - aInput) + 16 - max);
    }
#elif defined(__loongarch_sx)
    int int_mask = __lsx_vpickve2gr_hu(
        __lsx_vmskltz_b(reinterpret_cast<__m128i>(mask)), 0);
    if (int_mask != 0) {
      return size_t((current - aInput) + __builtin_ctz(int_mask));
    }
#else  // x86/x86_64
    int int_mask = _mm_movemask_epi8(mask);
    if (int_mask != 0) {
      // The least-significant bit in the integer corresponds to
      // the first SIMD lane in text order. Hence, we need to count
      // trailing zeros. We already checked that the bits are not
      // all zeros, so __builtin_ctz isn't UB.
      return size_t((current - aInput) + __builtin_ctz(int_mask));
    }
#endif
    current += 16;
  }
  return size_t(current - aInput);
}

template <typename CharT>
MOZ_ALWAYS_INLINE_EVEN_DEBUG size_t
AccelerateTextNode(const CharT* aInput, const CharT* aEnd, uint8x16_t aTable,
                   bool aAllowSurrogates = true, bool aAllowHyphen = true,
                   bool aAllowRightSquareBracket = true) {
  return AccelerateTextNodeImpl<false>(
      aInput, aEnd, static_cast<CharT*>(nullptr), aTable, aAllowSurrogates,
      aAllowHyphen, aAllowRightSquareBracket);
}

MOZ_ALWAYS_INLINE_EVEN_DEBUG size_t AccelerateTextNode(
    const char16_t* aInput, const char16_t* aEnd, char16_t* aOut,
    uint8x16_t aTable, bool aAllowSurrogates = true, bool aAllowHyphen = true,
    bool aAllowRightSquareBracket = true) {
  return AccelerateTextNodeImpl<true>(aInput, aEnd, aOut, aTable,
                                      aAllowSurrogates, aAllowHyphen,
                                      aAllowRightSquareBracket);
}

template <typename CharT>
MOZ_ALWAYS_INLINE_EVEN_DEBUG uint32_t CountEscaped(const CharT* aInput,
                                                   const CharT* aEnd,
                                                   bool aCountDoubleQuote) {
  uint32_t numEncodedChars = 0;
  const CharT* current = aInput;
  while (aEnd - current >= 16) {
    uint8x16_t mask = StrideToMask(
        current, aCountDoubleQuote ? LT_GT_AMP_NBSP_QUOT : LT_GT_AMP_NBSP);
#if defined(__aarch64__)
    // Reduce on each iteration to avoid branching for overflow avoidance
    // on each iteration.
    numEncodedChars += vaddvq_u8(mask & ALL_ONES);
#elif defined(__loongarch_sx)
    // vmskltz.b leaves lane 1 all zeros, so picking the popcount of lane 0 is
    // enough.
    numEncodedChars += static_cast<uint32_t>(__lsx_vpickve2gr_d(
        __lsx_vpcnt_d(__lsx_vmskltz_b(reinterpret_cast<__m128i>(mask))), 0));
#else  // x86_64
    numEncodedChars += __builtin_popcount(_mm_movemask_epi8(mask));
#endif
    current += 16;
  }
  while (current != aEnd) {
    CharT c = *current;
    if ((aCountDoubleQuote && c == CharT('"')) || c == CharT('&') ||
        c == CharT('<') || c == CharT('>') || c == CharT(0xA0)) {
      ++numEncodedChars;
    }
    ++current;
  }
  return numEncodedChars;
}

MOZ_ALWAYS_INLINE_EVEN_DEBUG bool ContainsMarkup(const char16_t* aInput,
                                                 const char16_t* aEnd) {
  const char16_t* current = aInput;
  while (aEnd - current >= 16) {
    uint8x16_t mask = StrideToMask(current, ZERO_LT_AMP_CR);
#if defined(__aarch64__)
    uint8_t max = vmaxvq_u8(mask);
    if (max != 0) {
      return true;
    }
#elif defined(__loongarch_sx)
    int int_mask = __lsx_vpickve2gr_hu(
        __lsx_vmskltz_b(reinterpret_cast<__m128i>(mask)), 0);
    if (int_mask != 0) {
      return true;
    }
#else  // x86/x86_64
    int int_mask = _mm_movemask_epi8(mask);
    if (int_mask != 0) {
      return true;
    }
#endif
    current += 16;
  }
  while (current != aEnd) {
    char16_t c = *current;
    if (c == char16_t('<') || c == char16_t('&') || c == char16_t('\r') ||
        c == char16_t('\0')) {
      return true;
    }
    ++current;
  }
  return false;
}

}  // namespace detail

// Public entry points are in htmlaccelNotInline.h for now.

}  // namespace mozilla::htmlaccel

#endif  // mozilla_htmlaccel_htmlaccel_h
