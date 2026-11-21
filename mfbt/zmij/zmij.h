// A double-to-string conversion library: https://github.com/vitaut/zmij/
//
// Copyright (c) 2025 - present, Victor Zverovich
// Distributed under the MIT license (see LICENSE) or alternatively
// the Boost Software License, Version 1.0.

#ifndef ZMIJ_H_
#define ZMIJ_H_

#include <assert.h>  // assert
#include <float.h>   // DBL_MANT_DIG, LDBL_MANT_DIG
#include <stddef.h>  // size_t
#include <stdint.h>  // uint64_t
#include <stdlib.h>  // malloc, free
#include <string.h>  // memcpy

#include <limits>       // std::numeric_limits
#include <type_traits>  // std::conditional

#ifdef _MSVC_LANG
#  define ZMIJ_CPLUSPLUS _MSVC_LANG
#else
#  define ZMIJ_CPLUSPLUS __cplusplus
#endif

#if ZMIJ_CPLUSPLUS >= 202002L
#  include <bit>  // std::bit_cast
#endif

#ifdef _MSC_VER
#  include <intrin.h>  // __lzcnt64/_umul128/__umulh
#  define ZMIJ_MSC_VER _MSC_VER
#else
#  define ZMIJ_MSC_VER 0
#endif

#include "mozilla/Types.h"

#if defined(__has_builtin) && !defined(ZMIJ_NO_BUILTINS)
#  define ZMIJ_HAS_BUILTIN(x) __has_builtin(x)
#else
#  define ZMIJ_HAS_BUILTIN(x) 0
#endif
// GCC only has __has_builtin since version 10, but has had __builtin_clzll,
// __builtin_ctzll and __builtin_bswap64 much longer.
#if defined(__GNUC__) && !defined(ZMIJ_NO_BUILTINS)
#  define ZMIJ_GNUC_BUILTINS 1
#else
#  define ZMIJ_GNUC_BUILTINS 0
#endif
// IBM double-double, the ppc64le default long double, has a 106-bit significand
// and is not IEEE 754, so the long double API is not provided for it.
#define ZMIJ_USE_LONG_DOUBLE (LDBL_MANT_DIG != 106)
#ifdef __has_attribute
#  define ZMIJ_HAS_ATTRIBUTE(x) __has_attribute(x)
#else
#  define ZMIJ_HAS_ATTRIBUTE(x) 0
#endif
#ifdef __has_cpp_attribute
#  define ZMIJ_HAS_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#else
#  define ZMIJ_HAS_CPP_ATTRIBUTE(x) 0
#endif

#ifdef ZMIJ_USE_IS_CONSTANT_EVALUATED
// Use the provided definition.
#elif defined(__cpp_lib_is_constant_evaluated) ||                    \
    ZMIJ_HAS_BUILTIN(__builtin_is_constant_evaluated) ||             \
    (defined(_MSC_VER) && _MSC_VER >= 1925 && !defined(__clang__) && \
     !defined(__CUDACC__) && !defined(ZMIJ_NO_BUILTINS))
#  define ZMIJ_USE_IS_CONSTANT_EVALUATED 1
#else
#  define ZMIJ_USE_IS_CONSTANT_EVALUATED 0
#endif

// Disabling C++14 relaxed constexpr costs ~2x, so it is opt-in, not detected.
#ifndef ZMIJ_USE_CONSTEXPR
#  define ZMIJ_USE_CONSTEXPR 1
#endif
#if ZMIJ_USE_CONSTEXPR
#  define ZMIJ_CONSTEXPR constexpr
#  define ZMIJ_DEFAULT(...) = __VA_ARGS__
#else
#  define ZMIJ_CONSTEXPR
// A default member initializer makes its class a non-aggregate in C++11.
#  define ZMIJ_DEFAULT(...)
#endif

#if defined(__cpp_lib_is_constant_evaluated) && ZMIJ_USE_CONSTEXPR
#  define ZMIJ_CONSTEXPR20 constexpr
#else
#  define ZMIJ_CONSTEXPR20
#endif

#ifdef __x86_64__
#  define ZMIJ_X86_64 1
#else
#  define ZMIJ_X86_64 0
#endif

#ifndef ZMIJ_USE_SIMD
#  define ZMIJ_USE_SIMD 1
#endif

#ifdef ZMIJ_USE_NEON
// Use the provided definition.
#elif (defined(__ARM_NEON) && defined(__aarch64__)) || defined(_M_ARM64)
// The NEON code uses AArch64-only intrinsics such as vqtbl1q_u8.
#  define ZMIJ_USE_NEON ZMIJ_USE_SIMD
#else
#  define ZMIJ_USE_NEON 0
#endif

#ifdef ZMIJ_USE_SSE
// Use the provided definition.
#elif defined(_M_IX86) && ZMIJ_MSC_VER && !defined(__clang__)
// MSVC up to at least 19.37 miscompiles the SSE2 code on 32-bit x86: the
// upper half of _mm_set_epi64x of a computed 64-bit value comes out wrong.
#  define ZMIJ_USE_SSE 0
#elif defined(__SSE2__)
#  define ZMIJ_USE_SSE ZMIJ_USE_SIMD
#elif defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP == 2)
#  define ZMIJ_USE_SSE ZMIJ_USE_SIMD
#else
#  define ZMIJ_USE_SSE 0
#endif

#ifdef ZMIJ_USE_SSE4_1
// Use the provided definition.
static_assert(!ZMIJ_USE_SSE4_1 || ZMIJ_USE_SSE, "SSE4.1 requires SSE");
#elif (defined(__SSE4_1__) || defined(__AVX__)) && \
    (defined(__x86_64__) || defined(_M_X64))
// On MSVC there's no way to check for SSE4.1 specifically so check __AVX__.
// The SSE4.1 code uses x86-64-only intrinsics such as _mm_insert_epi64.
#  define ZMIJ_USE_SSE4_1 ZMIJ_USE_SSE
#else
#  define ZMIJ_USE_SSE4_1 0
#endif

#ifdef ZMIJ_OPTIMIZE_SIZE
// Use the provided definition.
#elif defined(__OPTIMIZE_SIZE__)
#  define ZMIJ_OPTIMIZE_SIZE 1
#else
#  define ZMIJ_OPTIMIZE_SIZE 0
#endif

#if ZMIJ_HAS_ATTRIBUTE(always_inline) && !ZMIJ_OPTIMIZE_SIZE
#  define ZMIJ_INLINE __attribute__((always_inline)) inline
#elif ZMIJ_MSC_VER
#  define ZMIJ_INLINE __forceinline
#else
#  define ZMIJ_INLINE inline
#endif

#if ZMIJ_HAS_ATTRIBUTE(noinline)
#  define ZMIJ_NOINLINE __attribute__((noinline))
#elif ZMIJ_MSC_VER
#  define ZMIJ_NOINLINE __declspec(noinline)
#else
#  define ZMIJ_NOINLINE
#endif

#if ZMIJ_CPLUSPLUS >= 202002L && ZMIJ_HAS_CPP_ATTRIBUTE(likely) && \
    ZMIJ_HAS_CPP_ATTRIBUTE(unlikely)
#  define ZMIJ_LIKELY(x) (x) [[likely]]
#  define ZMIJ_UNLIKELY(x) (x) [[unlikely]]
#elif ZMIJ_HAS_BUILTIN(__builtin_expect) || ZMIJ_GNUC_BUILTINS
#  define ZMIJ_LIKELY(x) (__builtin_expect(!!(x), 1))
#  define ZMIJ_UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
#  define ZMIJ_LIKELY(x) (x)
#  define ZMIJ_UNLIKELY(x) (x)
#endif

#if ZMIJ_CPLUSPLUS >= 201703L && ZMIJ_HAS_CPP_ATTRIBUTE(maybe_unused)
#  define ZMIJ_MAYBE_UNUSED maybe_unused
#elif defined(__GNUC__)
#  define ZMIJ_MAYBE_UNUSED gnu::unused
#else
#  define ZMIJ_MAYBE_UNUSED
#endif

#ifdef __GNUC__
#  define ZMIJ_ASM(x) asm x
#else
#  define ZMIJ_ASM(x)
#endif

namespace zmij {

enum {
  nonfinite_exp = int(~0u >> 1),
};

// A decimal floating-point number (negative ? -1 : 1) * sig * pow(10, exp).
// If exp is nonfinite_exp then the number is a NaN or an infinity.
template <typename Sig = uint64_t> struct dec_fp {
  Sig sig;  // significand
  int exp;  // exponent
  bool negative;
};

// Floating-point formatting style. Values match std::chars_format, so
// general == fixed | scientific.
enum class format {
  scientific = 1,
  fixed = 2,
  general = 3,
  hex = 4,
};

namespace detail {
struct no_long_double {
  operator double() const noexcept;  // Intentionally undefined.
};
}  // namespace detail

// The long double API's parameter type: long double, or where that API isn't
// provided (IBM double-double), a type no argument converts to, so those
// overloads drop out of overload resolution.
using long_double = std::conditional<ZMIJ_USE_LONG_DOUBLE, long double,
                                     detail::no_long_double>::type;

// Minimum buffer sizes for the shortest `write`, one per floating-point type.
enum {
  float_buffer_size = 17,
  double_buffer_size = 34,
  // Worst case is IEEE binary128: 1 sign + 36 digits + '.' + "e-dddd".
  long_double_buffer_size = 44,
};

// Minimum buffer capacities for integer write functions. They include space
// for whole-block stores that may extend past the returned end pointer.
enum {
  uint32_buffer_size = 16,
  int32_buffer_size = 17,
  uint64_buffer_size = 20,
  int64_buffer_size = 21,
  uint128_buffer_size = 48,
  int128_buffer_size = 49,
};

namespace detail {

constexpr auto is_constant_evaluated() noexcept -> bool {
#ifdef __cpp_lib_is_constant_evaluated
  return std::is_constant_evaluated();
#elif ZMIJ_USE_IS_CONSTANT_EVALUATED
  return __builtin_is_constant_evaluated();
#else
  return false;
#endif
}

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
constexpr bool is_big_endian = true;
#else
constexpr bool is_big_endian = false;
#endif

inline auto bswap64(uint64_t x) noexcept -> uint64_t {
#if ZMIJ_HAS_BUILTIN(__builtin_bswap64) || ZMIJ_GNUC_BUILTINS
  return __builtin_bswap64(x);
#elif ZMIJ_MSC_VER
  return _byteswap_uint64(x);
#else
  return ((x & 0xff00000000000000) >> 56) | ((x & 0x00ff000000000000) >> 40) |
         ((x & 0x0000ff0000000000) >> 24) | ((x & 0x000000ff00000000) >> +8) |
         ((x & 0x00000000ff000000) << +8) | ((x & 0x0000000000ff0000) << 24) |
         ((x & 0x000000000000ff00) << 40) | ((x & 0x00000000000000ff) << 56);
#endif
}

inline ZMIJ_CONSTEXPR20 auto clz(uint64_t x) noexcept -> int {
  assert(x != 0);
  if (!is_constant_evaluated()) {
#if ZMIJ_HAS_BUILTIN(__builtin_clzll) || ZMIJ_GNUC_BUILTINS
    return __builtin_clzll(x);
#elif defined(_M_AMD64) && defined(__AVX2__)
    // Use lzcnt only on AVX2-capable CPUs that have this BMI instruction.
    return __lzcnt64(x);
#elif defined(_M_AMD64) || defined(_M_ARM64)
    unsigned long idx;
    _BitScanReverse64(&idx, x);
    return 63 - idx;
#elif ZMIJ_MSC_VER
    unsigned long idx;
    if (_BitScanReverse(&idx, uint32_t(x >> 32))) return 31 - idx;
    _BitScanReverse(&idx, uint32_t(x));
    return 63 - idx;
#endif
  }
  int n = 64;
  for (; x > 0; x >>= 1) --n;
  return n;
}

struct uint128 {
  uint64_t lo;
  uint64_t hi;

  uint128() = default;
  constexpr uint128(uint64_t h, uint64_t l) noexcept : lo(l), hi(h) {}
  constexpr uint128(uint64_t l) noexcept : lo(l), hi(0) {}

  explicit constexpr operator uint64_t() const noexcept { return lo; }

  constexpr auto operator+(uint128 rhs) const noexcept -> uint128 {
    return {hi + rhs.hi + (lo + rhs.lo < lo), lo + rhs.lo};
  }
  constexpr auto operator-(uint128 rhs) const noexcept -> uint128 {
    return {hi - rhs.hi - (lo - rhs.lo > lo), lo - rhs.lo};
  }
  constexpr auto operator%(uint32_t d) const noexcept -> uint64_t {
    // (UINT64_MAX % d + 1) % d is 2**64 mod d, which fits in 32 bits.
    return ((hi % d) * ((UINT64_MAX % d + 1) % d) + lo % d) % d;
  }
  constexpr auto operator&(uint128 rhs) const noexcept -> uint128 {
    return {hi & rhs.hi, lo & rhs.lo};
  }
  constexpr auto operator|(uint128 rhs) const noexcept -> uint128 {
    return {hi | rhs.hi, lo | rhs.lo};
  }
  constexpr auto operator^(uint128 rhs) const noexcept -> uint128 {
    return {hi ^ rhs.hi, lo ^ rhs.lo};
  }
  constexpr auto operator<<(int s) const noexcept -> uint128 {
    return s == 0    ? *this
           : s < 64  ? uint128(hi << s | lo >> (64 - s), lo << s)
           : s < 128 ? uint128(lo << (s - 64), 0)
                     : uint128(0, 0);
  }
  constexpr auto operator>>(int s) const noexcept -> uint128 {
    return s == 0    ? *this
           : s < 64  ? uint128(hi >> s, lo >> s | hi << (64 - s))
           : s < 128 ? uint128(0, hi >> (s - 64))
                     : uint128(0, 0);
  }

  constexpr auto operator==(uint128 rhs) const noexcept -> bool {
    return hi == rhs.hi && lo == rhs.lo;
  }
  constexpr auto operator!=(uint128 rhs) const noexcept -> bool {
    return hi != rhs.hi || lo != rhs.lo;
  }
  constexpr auto operator<(uint128 rhs) const noexcept -> bool {
    return hi != rhs.hi ? hi < rhs.hi : lo < rhs.lo;
  }
  constexpr auto operator>(uint128 rhs) const noexcept -> bool {
    return rhs < *this;
  }
  constexpr auto operator<=(uint128 rhs) const noexcept -> bool {
    return !(rhs < *this);
  }
  constexpr auto operator>=(uint128 rhs) const noexcept -> bool {
    return !(*this < rhs);
  }

  auto operator++() noexcept -> uint128& {
    if (++lo == 0) ++hi;
    return *this;
  }
};

#ifdef ZMIJ_USE_INT128
// Use the provided definition.
#elif defined(__SIZEOF_INT128__)
#  define ZMIJ_USE_INT128 1
#else
#  define ZMIJ_USE_INT128 0
#endif

#ifdef ZMIJ_USE_FLOAT128
// Use the provided definition.
#elif defined(__SIZEOF_FLOAT128__)
#  define ZMIJ_USE_FLOAT128 1
#else
#  define ZMIJ_USE_FLOAT128 0
#endif

#if ZMIJ_USE_INT128
using uint128_t = __uint128_t;
#else
using uint128_t = uint128;
#endif  // ZMIJ_USE_INT128

// Computes 128-bit result of multiplication of two 64-bit unsigned integers.
inline ZMIJ_CONSTEXPR auto umul128(uint64_t x, uint64_t y) noexcept
    -> uint128_t {
#if ZMIJ_USE_INT128
  return uint128_t(x) * y;
#else
#  if ZMIJ_USE_IS_CONSTANT_EVALUATED && defined(_M_AMD64)
  if (!is_constant_evaluated()) {
    uint64_t hi = 0;
    uint64_t lo = _umul128(x, y, &hi);
    return {hi, lo};
  }
#  elif ZMIJ_USE_IS_CONSTANT_EVALUATED && defined(_M_ARM64)
  if (!is_constant_evaluated()) return {__umulh(x, y), x * y};
#  endif
  uint64_t a = x >> 32;
  uint64_t b = uint32_t(x);
  uint64_t c = y >> 32;
  uint64_t d = uint32_t(y);

  uint64_t ac = a * c;
  uint64_t bc = b * c;
  uint64_t ad = a * d;
  uint64_t bd = b * d;

  uint64_t cs = (bd >> 32) + uint32_t(ad) + uint32_t(bc);  // cross sum
  return {ac + (ad >> 32) + (bc >> 32) + (cs >> 32), (cs << 32) + uint32_t(bd)};
#endif  // ZMIJ_USE_INT128
}

inline ZMIJ_CONSTEXPR auto umul192_hi128(uint64_t x_hi, uint64_t x_lo,
                                         uint64_t y) noexcept -> uint128 {
  uint128_t p = umul128(x_hi, y);
  uint64_t lo = uint64_t(p) + uint64_t(umul128(x_lo, y) >> 64);
  return {uint64_t(p >> 64) + (lo < uint64_t(p)), lo};
}

// Computes the decimal exponent as floor(log10(2**bin_exp)) if regular or
// floor(log10(3/4 * 2**bin_exp)) otherwise, without branching.
constexpr auto compute_dec_exp(int bin_exp, bool regular = true) noexcept
    -> int {
#if ZMIJ_USE_CONSTEXPR  // In C++11, GCC rejects assert in noexcept constexpr.
  assert(bin_exp >= -1334 && bin_exp <= 2620);
#endif
  // 315653 is round(log10(2) * 2**20) and 131072 is -log10(3/4) * 2**20
  // rounded to a power of two.
  return (bin_exp * 315653 - !regular * 131072) >> 20;
}

constexpr auto ilog2(int n) noexcept -> int {
  return n > 1 ? 1 + ilog2(n >> 1) : 0;
}

#if ZMIJ_USE_FLOAT128
template <typename T> struct is_float128 : std::is_same<T, __float128> {};
#else
template <typename T> struct is_float128 : std::false_type {};
#endif

template <typename Float> struct float_traits : std::numeric_limits<Float> {
  // std::numeric_limits doesn't describe __float128, which is IEEE binary128.
  static constexpr bool binary128 =
      is_float128<Float>::value && !std::numeric_limits<Float>::is_specialized;
  static_assert(std::numeric_limits<Float>::is_iec559 || binary128,
                "IEEE 754 required");
  static constexpr int digits =
      binary128 ? 113 : std::numeric_limits<Float>::digits;
  static constexpr int max_exponent =
      binary128 ? 16384 : std::numeric_limits<Float>::max_exponent;

  // x87 80-bit stores the integer bit explicitly (digits == 64), so its
  // exponent sits one bit higher than the implicit-bit binary32/64/128 layouts.
  static constexpr int num_sig_bits = float_traits::digits - 1;
  static constexpr int num_exp_bits = ilog2(float_traits::max_exponent) + 1;
  static constexpr int exp_shift =
      num_sig_bits + (float_traits::digits == 64 ? 1 : 0);
  static constexpr int num_bits = exp_shift + num_exp_bits + 1;
  static constexpr int exp_mask = (1 << num_exp_bits) - 1;
  static constexpr int exp_bias = (1 << (num_exp_bits - 1)) - 1;
  static constexpr int exp_offset = exp_bias + num_sig_bits;
  static constexpr int min_fixed_dec_exp = -4;
  static constexpr int max_fixed_dec_exp =
      compute_dec_exp(float_traits::digits + 1) - 1;

  using sig_type = typename std::conditional<
      num_bits <= 32, uint32_t,
      typename std::conditional<num_bits <= 64, uint64_t,
                                uint128_t>::type>::type;
  static constexpr sig_type implicit_bit = sig_type(1) << num_sig_bits;

  // Bounds for the exact big-integer path (write_big): the significand times
  // 5**k (k up to exp_offset + num_sig_bits - 1) fits in big_bits bits, hence
  // big_limbs base-2**32 limbs and big_digits decimal digits. 2322/1000 and
  // 30103/100000 over-approximate log2(5) and log10(2); the +2 rounds the digit
  // count up and reserves one carry digit.
  static constexpr int big_bits =
      num_sig_bits + 2 + (exp_offset + num_sig_bits - 1) * 2322 / 1000;
  static constexpr int big_limbs = (big_bits + 31) / 32;
  static constexpr int big_digits = big_bits * 30103 / 100000 + 2;

  static ZMIJ_CONSTEXPR20 auto to_bits(Float value) noexcept -> sig_type {
#if defined(__cpp_lib_bit_cast) && __cpp_lib_bit_cast >= 201806L
    if (std::is_constant_evaluated()) {
      if constexpr (sizeof(sig_type) == sizeof(Float))
        return std::bit_cast<sig_type>(value);
    }
#endif
    sig_type bits = sig_type();
    memcpy(&bits, &value, sizeof(value));
    return bits;
  }

  static constexpr auto is_negative(sig_type bits) noexcept -> bool {
    return ((bits >> (num_bits - 1)) & 1) != 0;
  }
  static constexpr auto get_sig(sig_type bits) noexcept -> sig_type {
    return bits & (implicit_bit - 1);
  }
  static constexpr auto get_exp(sig_type bits) noexcept -> int64_t {
    return int64_t(uint64_t(bits >> exp_shift) & unsigned(exp_mask));
  }
  static constexpr auto is_normal(int64_t bin_exp) noexcept -> bool {
    // Fold subnormal/zero (exp 0) and inf/nan (exp exp_mask) into one check.
    return unsigned(bin_exp - 1) < unsigned(exp_mask - 1);
  }
};

#ifndef __cpp_inline_variables
template <typename Float>
constexpr
    typename float_traits<Float>::sig_type float_traits<Float>::implicit_bit;
#endif

// Compressed 128-bit significands of powers of 10, and powers of 5 below 5**13.
template <typename = void> struct pow10_data {
  static constexpr uint64_t minor[28] = {
      0x8000000000000000, 0xa000000000000000, 0xc800000000000000,
      0xfa00000000000000, 0x9c40000000000000, 0xc350000000000000,
      0xf424000000000000, 0x9896800000000000, 0xbebc200000000000,
      0xee6b280000000000, 0x9502f90000000000, 0xba43b74000000000,
      0xe8d4a51000000000, 0x9184e72a00000000, 0xb5e620f480000000,
      0xe35fa931a0000000, 0x8e1bc9bf04000000, 0xb1a2bc2ec5000000,
      0xde0b6b3a76400000, 0x8ac7230489e80000, 0xad78ebc5ac620000,
      0xd8d726b7177a8000, 0x878678326eac9000, 0xa968163f0a57b400,
      0xd3c21bcecceda100, 0x84595161401484a0, 0xa56fa5b99019a5c8,
      0xcecb8f27f4200f3a,
  };
  // {hi, lo} pairs: GCC 4.9 rejects constexpr uint128 arrays in templates.
  static constexpr uint64_t major[25][2] = {
      {0xaddcb9e83c6b1793, 0xdf4abe242a1bbf3e},  // -331 (+1 ULP: see fixups)
      {0xaf8e5410288e1b6f, 0x07ecf0ae5ee44dda},  // -303
      {0xb1442798f49ffb4a, 0x99cd11cfdf41779d},  // -275
      {0xb2fe3f0b8599ef07, 0x861fa7e6dcb4aa15},  // -247
      {0xb4bca50b065abe63, 0x0fed077a756b53aa},  // -219
      {0xb67f6455292cbf08, 0x1a3bc84c17b1d543},  // -191
      {0xb84687c269ef3bfb, 0x3d5d514f40eea742},  // -163
      {0xba121a4650e4ddeb, 0x92f34d62616ce413},  // -135
      {0xbbe226efb628afea, 0x890489f70a55368c},  // -107
      {0xbdb6b8e905cb600f, 0x5400e987bbc1c921},  //  -79
      {0xbf8fdb78849a5f96, 0xde98520472bdd034},  //  -51
      {0xc16d9a0095928a27, 0x75b7053c0f178294},  //  -23
      {0xc350000000000000, 0x0000000000000000},  //    5
      {0xc5371912364ce305, 0x6c28000000000000},  //   33
      {0xc722f0ef9d80aad6, 0x424d3ad2b7b97ef6},  //   61
      {0xc913936dd571c84c, 0x03bc3a19cd1e38ea},  //   89
      {0xcb090c8001ab551c, 0x5cadf5bfd3072cc6},  //  117
      {0xcd036837130890a1, 0x36dba887c37a8c10},  //  145
      {0xcf02b2c21207ef2e, 0x94f967e45e03f4bc},  //  173
      {0xd106f86e69d785c7, 0xe13336d701beba52},  //  201
      {0xd31045a8341ca07c, 0x1ede48111209a051},  //  229
      {0xd51ea6fa85785631, 0x552a74227f3ea566},  //  257
      {0xd732290fbacaf133, 0xa97c177947ad4096},  //  285
      {0xd94ad8b1c7380874, 0x18375281ae7822bd},  //  313 (+1 ULP: see fixups)
      {0xdb68c2ca82ed2a05, 0xa67398db9f6820e1},  //  341
  };
  static constexpr uint32_t fixups[21] = {
      0x8d8fc810, 0x06100293, 0x19000000, 0x00100000, 0x00000908, 0x00000000,
      0x04e00300, 0x3807e0b2, 0x3d83d793, 0x0006f5cc, 0x00000000, 0xffff0000,
      0x8076337d, 0x4ff45ba0, 0x09405033, 0x034376d9, 0x09000000, 0x4e100501,
      0x076d14dc, 0xf964f45e, 0x0000003d};
  static constexpr uint32_t pow5[13] = {
      1,     5,      25,      125,     625,      3125,     15625,
      78125, 390625, 1953125, 9765625, 48828125, 244140625};
};

#ifndef __cpp_inline_variables
template <typename T> constexpr uint64_t pow10_data<T>::minor[];
template <typename T> constexpr uint64_t pow10_data<T>::major[][2];
template <typename T> constexpr uint32_t pow10_data<T>::fixups[];
template <typename T> constexpr uint32_t pow10_data<T>::pow5[];
#endif

// Computes the 128-bit significand of 10**exp rounded down using method by
// Dougall Johnson.
inline ZMIJ_CONSTEXPR auto compute_pow10(int exp) noexcept -> uint128 {
  using data = pow10_data<>;
  constexpr int min_exp = -307;
  constexpr int stride = sizeof(data::minor) / sizeof(*data::minor);
  assert(exp >= min_exp && exp <= 341);
  unsigned i = unsigned(exp - min_exp);
  uint64_t m = data::minor[(i + 24) % stride];
  unsigned j = (i + 24) / stride;
  uint128 h = {data::major[j][0], data::major[j][1]};

  uint64_t h1 = uint64_t(umul128(h.lo, m) >> 64);
  uint64_t c0 = h.lo * m;
  uint64_t c1 = h1 + h.hi * m;
  uint64_t c2 = (c1 < h1) + uint64_t(umul128(h.hi, m) >> 64);

  uint128 result = (c2 >> 63) != 0
                       ? uint128{c2, c1}
                       : uint128{c2 << 1 | c1 >> 63, c1 << 1 | c0 >> 63};
  result.lo -= (data::fixups[i >> 5] >> (i & 31)) & 1;
  return result;
}

// Computes a shift so that, after scaling by a power of 10, the intermediate
// result always has a fixed 128-bit fractional part (for float and double).
//
// Different binary exponents can map to the same decimal exponent, but place
// the decimal point at different bit positions. The shift compensates for this.
//
// For example, both 3 * 2**59 and 3 * 2**60 have dec_exp = 2, but dividing by
// 10^dec_exp puts the decimal point in different bit positions:
//   3 * 2**59 / 100 = 1.72...e+16  (needs shift = 1 + 1)
//   3 * 2**60 / 100 = 3.45...e+16  (needs shift = 2 + 1)
inline ZMIJ_CONSTEXPR auto compute_exp_shift(int bin_exp, int dec_exp) noexcept
    -> signed char {
  assert(dec_exp >= -350 && dec_exp <= 350);
  // log2_pow10_sig = round(log2(10) * 2**log2_pow10_exp) + 1
  constexpr int log2_pow10_sig = 217707, log2_pow10_exp = 16;
  // pow10_bin_exp = floor(log2(10**-dec_exp))
  int pow10_bin_exp = -dec_exp * log2_pow10_sig >> log2_pow10_exp;
  // pow10 = ((pow10_hi << 64) | pow10_lo) * 2**(pow10_bin_exp - 127)
  return static_cast<signed char>(bin_exp + pow10_bin_exp + 1);
}

// Converts the nonzero finite binary value bin_sig * 2**bin_exp using yy.
template <typename Float>
inline ZMIJ_CONSTEXPR20 auto to_decimal(uint64_t bin_sig, int bin_exp) noexcept
    -> dec_fp<> {
  assert(bin_sig != 0);
  constexpr uint64_t implicit_bit = float_traits<Float>::implicit_bit;
  bool irregular = bin_sig == implicit_bit;
  int dec_exp = compute_dec_exp(bin_exp, !irregular);
  int shift = compute_exp_shift(bin_exp, dec_exp) - 1;
  uint128 pow10 = compute_pow10(-dec_exp);
  uint128 p = umul192_hi128(pow10.hi, pow10.lo, bin_sig << (shift + 1));

  uint64_t one = p.hi % 10;
  uint64_t ten = p.hi - one;
  uint64_t c = one << 60 | p.lo >> 4;
  uint64_t half_ulp = pow10.hi >> (4 - shift);

  bool round_u1 = false;
  bool round_d0 = false;
  if (irregular) {
    bool round_d1 = (half_ulp >> 1) >= (p.lo >> 4);
    round_u1 = p.lo > (uint64_t(1) << 63) || !round_d1;
    round_d0 = (half_ulp >> 1) >= c;
  } else {
    uint64_t half = uint64_t(1) << 63;
    round_u1 = p.lo == half ? (p.hi & 1) != 0 : p.lo > half;
    round_d0 = half_ulp == c ? (bin_sig & 1) == 0 : c < half_ulp;
  }

  uint64_t t0 = uint64_t(10) << 60;
  uint64_t t1 = c + half_ulp;
  bool even = (bin_sig & 1) == 0;
  bool round_u0 = t0 <= t1;
  // Exact trim-up boundaries round toward the even binary significand.
  if (t1 + 1 == t0 || (dec_exp == 0 && t1 == t0)) round_u0 = even;

  uint64_t dec_one = p.hi + round_u1;
  uint64_t dec_ten = ten + (round_u0 ? 10 : 0);
  return {round_d0 || round_u0 ? dec_ten : dec_one, dec_exp, false};
}

inline ZMIJ_CONSTEXPR20 auto copy_n(char* out, const char* in,
                                    size_t count) noexcept -> char* {
  for (size_t i = 0; i < count; ++i) *out++ = *in++;
  return out;
}

// Divides x by 10 in place and returns the remainder.
inline auto divmod10(uint128_t& x) noexcept -> uint64_t {
#if ZMIJ_USE_INT128
  uint64_t r = uint64_t(x % 10);
  x /= 10;
  return r;
#else
  auto div = [](uint64_t& w, uint64_t rem) -> uint64_t {
    uint64_t hi = rem << 32 | w >> 32;
    uint64_t lo = (hi % 10) << 32 | uint32_t(w);
    w = (hi / 10) << 32 | (lo / 10);
    return lo % 10;
  };
  return div(x.lo, div(x.hi, 0));
#endif
}

// A minimal binary big integer (little-endian base-2**32 limbs) over caller-
// provided storage.
struct bigint {
  uint32_t* limbs;
  int num_limbs = 0;  // Significant limbs; 0 represents zero.
  int max_limbs;      // Available limbs in `limbs`.

  // Callers must size `max_limbs` for the largest value the number will reach.
  ZMIJ_CONSTEXPR bigint(uint128_t value, uint32_t* buffer, int size) noexcept
      : limbs(buffer), max_limbs(size) {
    uint64_t lo = uint64_t(value), hi = uint64_t(value >> 64);
    limbs[0] = uint32_t(lo);
    limbs[1] = uint32_t(lo >> 32);
    limbs[2] = uint32_t(hi);
    limbs[3] = uint32_t(hi >> 32);
    // Keep after the limb stores: setting it first worsens write_fixed_big.
    num_limbs = 4;
    trim();
  }

  ZMIJ_CONSTEXPR void trim() noexcept {
    while (num_limbs > 0 && limbs[num_limbs - 1] == 0) --num_limbs;
  }

  // Shifts left by `n`.
  ZMIJ_CONSTEXPR void shl(int n) noexcept {
    assert(n >= 0);
    if (num_limbs == 0) return;
    int limb_shift = n >> 5, bit_shift = n & 31;
    assert(num_limbs + limb_shift + (bit_shift != 0) <= max_limbs);
    if (bit_shift == 0) {
      for (int i = num_limbs - 1; i >= 0; --i) limbs[i + limb_shift] = limbs[i];
      num_limbs += limb_shift;
    } else {
      limbs[num_limbs + limb_shift] = limbs[num_limbs - 1] >> (32 - bit_shift);
      for (int i = num_limbs - 1; i > 0; --i) {
        limbs[i + limb_shift] =
            limbs[i] << bit_shift | limbs[i - 1] >> (32 - bit_shift);
      }
      limbs[limb_shift] = limbs[0] << bit_shift;
      num_limbs += limb_shift + 1;
    }
    for (int i = 0; i < limb_shift; ++i) limbs[i] = 0;
    trim();
  }

  // Divides by 10**9 in place and returns the remainder.
  ZMIJ_CONSTEXPR auto divmod_1e9() noexcept -> uint32_t {
    uint64_t rem = 0;
    for (int i = num_limbs - 1; i >= 0; --i) {
      uint64_t div = rem << 32 | limbs[i];
      limbs[i] = uint32_t(div / 1000000000u);
      rem = div % 1000000000u;
    }
    trim();
    return uint32_t(rem);
  }

  // Multiplies in place by a nonzero `factor`.
  ZMIJ_CONSTEXPR void mul(uint32_t factor) noexcept {
    assert(factor != 0);
    uint64_t carry = 0;
    for (int i = 0; i < num_limbs; ++i) {
      uint64_t product = uint64_t(limbs[i]) * factor + carry;
      limbs[i] = uint32_t(product);
      carry = product >> 32;
    }
    if (carry == 0) return;
    assert(num_limbs < max_limbs);
    limbs[num_limbs++] = uint32_t(carry);
  }

  // Multiplies in place by 5**n.
  ZMIJ_CONSTEXPR void mul_pow5(int n) noexcept {
    assert(n >= 0);
    while (n >= 13) {  // 5**13 is the largest power of five below 2**32.
      mul(1220703125u);
      n -= 13;
    }
    if (n != 0) mul(pow10_data<>::pow5[n]);
  }
};

// Emits n's decimal digits (most significant first) ending at `end`, consuming
// n, and returns a pointer to the first (most significant) digit.
inline ZMIJ_CONSTEXPR auto write_digits(bigint n, char* end) noexcept -> char* {
  char* p = end;
  uint32_t group = n.divmod_1e9();
  while (n.num_limbs != 0) {  // Lower groups keep all 9 digits.
    for (int k = 0; k < 9; ++k, group /= 10) *--p = char('0' + group % 10);
    group = n.divmod_1e9();
  }
  do {  // The most significant group drops its leading zeros.
    *--p = char('0' + group % 10);
  } while ((group /= 10) != 0);
  return p;
}

// Returns true if any character in [first, last) is not '0'.
inline ZMIJ_CONSTEXPR auto any_nonzero(const char* first,
                                       const char* last) noexcept -> bool {
  for (; first < last; ++first)
    if (*first != '0') return true;
  return false;
}

// `buffer` params require at least buffer_sizes<Float> capacity;
// `out`/`n` params write at most `n` characters.

// Converts `value` to the shortest correctly rounded decimal (see to_decimal).
template <typename Float> auto to_decimal(Float value) noexcept -> dec_fp<>;

// Wide-significand variant for long double (see to_decimal).
template <typename Float>
auto to_decimal_big(Float value) noexcept -> dec_fp<uint128_t>;

template <typename Float, bool ecmascript = false>
auto write(char* buffer, Float value) noexcept -> char*;

extern template MFBT_API auto write<double, true>(char* buffer,
                                                  double value) noexcept
    -> char*;

// Writes the shortest decimal representation of `value`, correctly rounded
// (ties to even), into `out`, truncating after `n` chars. Returns the total
// length the result would need; if it exceeds `n` the output was truncated to
// the first `n` chars.
template <typename Float>
auto write_big(char* out, size_t n, Float value) noexcept -> size_t;

// Writes `value` in `fmt` notation with `precision` digits, correctly rounded
// (ties to even), into `out`, truncating after `n` chars. Returns the total
// length the result would need; if it exceeds `n` the output was truncated to
// the first `n` chars, and 0 on allocation failure (only possible for long
// double).
template <typename Float>
ZMIJ_CONSTEXPR20 auto write_big(char* out, size_t n, Float value, int precision,
                                format fmt) noexcept -> size_t;
template <>
inline auto write_big(char* out, size_t n, float value, int precision,
                      format fmt) noexcept -> size_t {
  return write_big(out, n, double(value), precision, fmt);
}

template <typename Float>
auto write_scientific(char* buffer, Float value, int precision) noexcept
    -> char*;

template <typename Float>
auto write_general(char* buffer, Float value, int precision) noexcept -> char*;

template <typename Float>
auto write_fixed(char* buffer, Float value, int precision) noexcept -> char*;

// Writes the decimal exponent as 'e', a sign and at least two digits, up to
// four (e.g. e+05 or e+4932, enough for extended long double).
inline ZMIJ_CONSTEXPR auto write_big_exp(char* buffer, int dec_exp) noexcept
    -> char* {
  *buffer++ = 'e';
  *buffer++ = dec_exp >= 0 ? '+' : '-';
  uint32_t abs_exp = dec_exp >= 0 ? uint32_t(dec_exp) : uint32_t(-dec_exp);
  uint32_t hi = abs_exp / 100;
  *buffer = char('0' + hi / 10);
  buffer += hi >= 10;
  *buffer = char('0' + hi % 10);
  buffer += abs_exp >= 100;
  uint32_t lo = abs_exp - hi * 100;
  buffer[0] = char('0' + lo / 10);
  buffer[1] = char('0' + lo % 10);
  return buffer + 2;
}

// An output sink that appends to `out`, discarding anything past `end`.
struct writer {
  char* out;
  char* end;
  size_t count;  // total chars, including any dropped past `end`

  // The write functions return `count`, the running total of requested chars.
  ZMIJ_INLINE ZMIJ_CONSTEXPR auto write(char c) noexcept -> size_t {
    if (out < end) *out++ = c;
    return ++count;
  }
  // Copies s[0..n), or n zeros if `s` is null, during constant evaluation.
  // Compares with `end` rather than subtracting: GCC rejects `end - out` when
  // `out` is a row of a 2D array.
  ZMIJ_CONSTEXPR20 void put(const char* s, int n) noexcept {
    for (int i = 0; i < n && out < end; ++i) *out++ = s ? s[i] : '0';
  }
  // Copies at most `end - out` chars of `s[0..n)`; `n` must be non-negative.
  ZMIJ_INLINE ZMIJ_CONSTEXPR20 auto write(const char* s, int n) noexcept
      -> size_t {
    assert(n >= 0);
    if (zmij::detail::is_constant_evaluated()) {
      put(s, n);
    } else {
      size_t take = n < end - out ? size_t(n) : size_t(end - out);
      memcpy(out, s, take);
      out += take;
    }
    return count += size_t(n);
  }
  // Writes at most `end - out` copies of '0'; `n` must be non-negative.
  ZMIJ_INLINE ZMIJ_CONSTEXPR20 auto write_zeros(int n) noexcept -> size_t {
    assert(n >= 0);
    if (zmij::detail::is_constant_evaluated()) {
      put(nullptr, n);
    } else {
      size_t take = n < end - out ? size_t(n) : size_t(end - out);
      memset(out, '0', take);
      out += take;
    }
    return count += size_t(n);
  }
};

// Emits the digit run digits[0..num_digits) with the decimal point placed per
// lead_exp, in fixed or scientific notation. Fixed pads to decimal_places
// fractional digits; scientific appends num_tail_zeros to the fraction.
inline ZMIJ_CONSTEXPR20 auto write_number(writer& w, const char* digits,
                                          int num_digits, int lead_exp,
                                          bool fixed, int decimal_places,
                                          int num_tail_zeros) noexcept
    -> size_t {
  if (!fixed) {
    // Emit scientific notation d.ddde±XX.
    w.write(digits[0]);
    // Emit a point only when fractional digits follow it.
    if (num_digits - 1 + num_tail_zeros > 0) {
      w.write('.');
      w.write(digits + 1, num_digits - 1);
      w.write_zeros(num_tail_zeros);  // pad the fraction to precision
    }
    char exp[8];
    char* end = zmij::detail::write_big_exp(exp, lead_exp);
    return w.write(exp, int(end - exp));
  }

  // Emit fixed notation.
  int point_pos = lead_exp + 1;
  int num_int_digits = 0;  // significant digits before the point
  if (point_pos <= 0) {    // |value| < 1, e.g. 0.00123
    w.write('0');
  } else {
    num_int_digits = point_pos < num_digits ? point_pos : num_digits;
    w.write(digits, num_int_digits);
    w.write_zeros(point_pos - num_int_digits);  // integer zeros, e.g. 12300
  }
  if (decimal_places <= 0) return w.count;
  w.write('.');
  int num_lead_zeros = point_pos < 0 ? -point_pos : 0;
  w.write_zeros(num_lead_zeros);  // 0.00...
  int num_frac_digits = num_digits - num_int_digits;
  w.write(digits + num_int_digits, num_frac_digits);
  return w.write_zeros(decimal_places - num_lead_zeros - num_frac_digits);
}

// Writes the shortest representation of `value` like `write`, for constant
// evaluation, into `out`, truncating after `n` chars. Returns the total length
// the result would need.
template <typename Float>
inline ZMIJ_CONSTEXPR20 auto write_constexpr(char* out, size_t n,
                                             Float value) noexcept -> size_t {
  using traits = float_traits<Float>;
  auto bits = traits::to_bits(value);
  auto raw_exp = int(traits::get_exp(bits));
  auto bin_sig = uint64_t(traits::get_sig(bits));

  writer w = {out, out + n, 0};
  if (traits::is_negative(bits)) w.write('-');
  if (raw_exp == traits::exp_mask)
    return w.write(bin_sig != 0 ? "nan" : "inf", 3);
  if (raw_exp == 0 && bin_sig == 0) return w.write('0');

  int bin_exp = (raw_exp == 0 ? 1 : raw_exp) - traits::exp_offset;
  if (raw_exp != 0) bin_sig |= traits::implicit_bit;
  dec_fp<> dec = to_decimal<Float>(bin_sig, bin_exp);
  while (dec.sig % 10 == 0) {
    dec.sig /= 10;
    ++dec.exp;
  }

  char digits[traits::max_digits10] = {};
  char* first = digits + sizeof(digits);
  do {
    *--first = char('0' + dec.sig % 10);
    dec.sig /= 10;
  } while (dec.sig != 0);
  int num_digits = int(digits + sizeof(digits) - first);
  int lead_exp = dec.exp + num_digits - 1;
  bool fixed = lead_exp >= traits::min_fixed_dec_exp &&
               lead_exp <= traits::max_fixed_dec_exp;
  return write_number(w, first, num_digits, lead_exp, fixed,
                      num_digits - lead_exp - 1, 0);
}

inline ZMIJ_CONSTEXPR20 auto write_big(writer& w, bigint num, int bin_exp,
                                       int precision, char* digits,
                                       int digits_size,
                                       zmij::format fmt) noexcept -> size_t {
  bool general = fmt == zmij::format::general;
  bool fixed = fmt == zmij::format::fixed;
  assert(precision >= general);

  digits[0] = '0';
  char* p = digits;
  int num_digits = 1;
  int lead_exp = 0;  // exponent of the leading digit

  if (num.num_limbs != 0) {
    // Represent the value exactly as an integer num times a power of ten:
    // value = sig * 2**bin_exp = num * 10**base_exp, so its decimal digits are
    // num's. For bin_exp < 0 the identity 2**bin_exp = 5**-bin_exp *
    // 10**bin_exp keeps num integral via a power-of-five multiply.
    int base_exp = 0;
    if (bin_exp >= 0) {
      num.shl(bin_exp);
    } else {
      num.mul_pow5(-bin_exp);
      base_exp = bin_exp;
    }

    p = write_digits(num, digits + digits_size);
    num_digits = int(digits + digits_size - p);
    lead_exp = num_digits - 1 + base_exp;

    // Significant digits to keep: precision for %g. For %e/%f it counts
    // fractional digits, so add one leading digit; %f adds lead_exp more.
    int max_digits = precision + !general + (fixed ? lead_exp : 0);
    // Round to max_digits significant digits, ties to even.
    if (max_digits < 1) {
      // |value| < 10**-precision: rounds to 0, or up to 10**-precision when the
      // discarded part exceeds half a unit (a tie rounds to even, i.e. 0).
      bool round_up = false;
      if (max_digits == 0) {
        round_up =
            p[0] > '5' || (p[0] == '5' && any_nonzero(p + 1, p + num_digits));
      }
      digits[0] = char('0' + round_up);
      p = digits;
      num_digits = 1;
      lead_exp = round_up ? -precision : 0;
    } else if (num_digits > max_digits) {
      char dropped = p[max_digits];
      bool round_up = dropped > '5';
      // A dropped 5 ties to even unless a lower nonzero digit rounds up.
      if (dropped == '5') {
        round_up = ((p[max_digits - 1] - '0') & 1) ||
                   any_nonzero(p + max_digits + 1, p + num_digits);
      }
      num_digits = max_digits;
      if (round_up) {
        char* q = p + max_digits - 1;
        // Propagate the carry over trailing nines.
        while (q >= p && *q == '9') *q-- = '0';
        // 999.. rolling over to 1000.. adds a significant digit.
        if (q < p) {
          *--p = '1';
          ++lead_exp;
        } else {
          ++*q;
        }
      }
    }
  }

  int decimal_places = precision;  // fractional digit positions to emit
  if (general) {
    // Drop trailing zeros and pick fixed or scientific.
    while (num_digits > 1 && p[num_digits - 1] == '0') --num_digits;
    fixed = lead_exp >= -4 && lead_exp < precision;
    decimal_places = num_digits - lead_exp - 1;
  }

  // %e pads the fraction to `precision` digits; %g and shortest emit none.
  int num_tail_zeros = general ? 0 : precision - num_digits + 1;
  return write_number(w, p, num_digits, lead_exp, fixed, decimal_places,
                      num_tail_zeros);
}

template <typename Float>
ZMIJ_CONSTEXPR20 auto write_big(char* out, size_t n, Float value, int precision,
                                format fmt) noexcept -> size_t {
  using traits = float_traits<Float>;
  auto bits = traits::to_bits(value);
  auto bin_exp = traits::get_exp(bits);
  auto bin_sig = traits::get_sig(bits);

  writer w = {out, out + n, 0};
  if (traits::is_negative(bits)) w.write('-');

  if ZMIJ_UNLIKELY (!traits::is_normal(bin_exp)) {
    if (bin_exp != 0)  // inf or nan
      return w.write(bin_sig != 0 ? "nan" : "inf", 3);
    bin_exp = 1;  // subnormal or zero
    bin_sig = bin_sig ^ traits::implicit_bit;
  }
  bin_sig = bin_sig ^ traits::implicit_bit;

  // The big integer's limbs and the decimal digits stay on the stack except for
  // the extended long double case, which is too large (~11.6 KB) and puts both
  // in one heap block.
  constexpr bool use_heap =
      traits::big_digits > float_traits<double>::big_digits;
  uint32_t stack_limbs[use_heap ? 1 : traits::big_limbs];
  char stack_digits[use_heap ? 1 : traits::big_digits];
  uint32_t* limbs = stack_limbs;
  char* digits = stack_digits;
  if (use_heap) {
    constexpr int digit_words = (traits::big_digits + 3) / 4;
    limbs = static_cast<uint32_t*>(
        malloc(size_t(traits::big_limbs + digit_words) * 4));
    if (!limbs) return 0;
    digits = reinterpret_cast<char*>(limbs + traits::big_limbs);
  }
  bigint num(bin_sig, limbs, traits::big_limbs);
  size_t size = write_big(w, num, int(bin_exp - traits::exp_offset), precision,
                          digits, traits::big_digits, fmt);
  if (use_heap) free(limbs);
  return size;
}

// Not inlined, so that callers use the instantiations in zmij.cc. On the
// template itself, noinline makes GCC warn since constexpr implies inline.
extern template ZMIJ_NOINLINE auto write_big(char* out, size_t n, double value,
                                             int precision, format fmt) noexcept
    -> size_t;
#if LDBL_MANT_DIG != DBL_MANT_DIG && ZMIJ_USE_LONG_DOUBLE
extern template ZMIJ_NOINLINE auto write_big(char* out, size_t n,
                                             long double value, int precision,
                                             format fmt) noexcept -> size_t;
#endif

// Writes chars `a` and `b` to `out` and returns the position after them.
ZMIJ_INLINE ZMIJ_CONSTEXPR20 auto write2(char* out, char a, char b) noexcept
    -> char* {
  if (zmij::detail::is_constant_evaluated()) {
    out[0] = a;
    out[1] = b;
    return out + 2;
  }
  uint16_t v = uint16_t(uint8_t(a) | uint8_t(b) << 8);
  if (is_big_endian) v = uint16_t(v << 8 | v >> 8);
  memcpy(out, &v, 2);
  return out + 2;
}

// Writes "inf"/"nan" with one 4-byte store, so the buffer must hold 4 bytes.
inline ZMIJ_CONSTEXPR20 auto write_inf_nan(char* buffer, bool is_nan) noexcept
    -> char* {
  if (zmij::detail::is_constant_evaluated())
    return zmij::detail::copy_n(buffer, is_nan ? "nan" : "inf", 3);
  memcpy(buffer, is_nan ? "nan" : "inf", 4);
  return buffer + 3;
}

// Buffer must hold 4 bytes in the "NaN" case or 8 in the "Infinity" case.
// Writing 4 bytes is faster than writing 3 bytes so we copy a junk 0 byte.
inline auto write_inf_nan_ecmascript(char* buffer, bool is_nan) noexcept
    -> char* {
  if (is_nan) {
    memcpy(buffer, "NaN", 4);
    return buffer + 3;
  }
  memcpy(buffer, "Infinity", 8);
  return buffer + 8;
}

// Writes the binary exponent as 'p', a sign and at least one digit (e.g. p+3).
inline ZMIJ_CONSTEXPR20 auto write_hex_exp(char* buffer, int bin_exp) noexcept
    -> char* {
  buffer = write2(buffer, 'p', bin_exp < 0 ? '-' : '+');
  unsigned abs_exp = unsigned(bin_exp < 0 ? -bin_exp : bin_exp);
  char digits[8];
  char* p = digits + sizeof(digits);
  do {
    *--p = char('0' + abs_exp % 10);
    abs_exp /= 10;
  } while (abs_exp != 0);
  size_t n = size_t(digits + sizeof(digits) - p);
  if (zmij::detail::is_constant_evaluated())
    return zmij::detail::copy_n(buffer, p, n);
  memcpy(buffer, p, n);
  return buffer + n;
}

template <typename Float, typename UInt>
ZMIJ_INLINE ZMIJ_CONSTEXPR20 void normalize(UInt& bin_sig,
                                            int64_t& bin_exp) noexcept {
  // clz counts from the top of its operand, so measure from that width.
  int lz, width;
  if (sizeof(UInt) > 8) {
    uint128_t v = bin_sig;
    uint64_t hi = uint64_t(v >> 64);
    lz = hi != 0 ? clz(hi) : 64 + clz(uint64_t(v));
    width = 128;
  } else {
    lz = clz(uint64_t(bin_sig));
    width = 64;
  }
  int shift = lz - (width - 1 - float_traits<Float>::num_sig_bits);
  bin_sig = bin_sig << shift;
  bin_exp = 1 - shift;
}

// Writes `value` in hexadecimal floating-point notation (like printf's %a) in
// its shortest form, e.g. -0x1.8p+1. If `prefix` is false the leading "0x" is
// omitted (e.g. -1.8p+1).
template <typename Float>
ZMIJ_CONSTEXPR20 auto write_hex(char* buffer, Float value,
                                bool prefix = true) noexcept -> char* {
  using traits = float_traits<Float>;
  auto bits = traits::to_bits(value);
  auto bin_exp = traits::get_exp(bits);
  auto bin_sig = traits::get_sig(bits);

  *buffer = '-';
  buffer += traits::is_negative(bits);

  bool zero = false;
  if ZMIJ_UNLIKELY (!traits::is_normal(bin_exp)) {
    if (bin_exp != 0) return write_inf_nan(buffer, bin_sig != 0);
    zero = bin_sig == 0;
    if (zero)
      bin_exp = traits::exp_bias;  // This cancels to 0 below.
    else
      normalize<Float>(bin_sig, bin_exp);
  }
  bin_exp -= traits::exp_bias;

  if (prefix) buffer = write2(buffer, '0', 'x');
  *buffer++ = char('0' + !zero);

  constexpr int width = int(sizeof(bin_sig)) * 8;
  bin_sig = bin_sig << (width - traits::num_sig_bits);
  if (bin_sig != 0) {
    *buffer++ = '.';
    do {
      *buffer++ = "0123456789abcdef"[uint64_t(bin_sig >> (width - 4))];
      bin_sig = bin_sig << 4;
    } while (bin_sig != 0);
  }

  return write_hex_exp(buffer, int(bin_exp));
}

// Not inlined, so that callers use the instantiations in zmij.cc.
extern template ZMIJ_NOINLINE auto write_hex(char* buffer, double value,
                                             bool prefix) noexcept -> char*;
#if LDBL_MANT_DIG != DBL_MANT_DIG && ZMIJ_USE_LONG_DOUBLE
extern template ZMIJ_NOINLINE auto write_hex(char* buffer, long double value,
                                             bool prefix) noexcept -> char*;
#endif

// Writes `value` in hexadecimal floating-point notation with `precision` hex
// digits after the point, correctly rounded (ties to even), e.g. -0x1.80p+1,
// into `out`, truncating after `n` chars. Returns the total length the result
// would need. If `prefix` is false the leading "0x" is omitted.
template <typename Float>
auto write_hex(char* out, size_t n, Float value, int precision,
               bool prefix = true) noexcept -> size_t;

// long_double has no explicit instantiations when long double == double or the
// long double API isn't provided, so forward its detail functions to double.
#if LDBL_MANT_DIG == DBL_MANT_DIG || !ZMIJ_USE_LONG_DOUBLE
template <>
inline auto to_decimal_big(long_double value) noexcept -> dec_fp<uint128_t> {
  dec_fp<> dec = to_decimal(double(value));
  return {dec.sig, dec.exp, dec.negative};
}
// There is no write_big for double, so route shortest through write.
template <>
inline auto write_big(char* out, size_t n, long_double value) noexcept
    -> size_t {
  char buffer[double_buffer_size];
  size_t size = size_t(write(buffer, double(value)) - buffer);
  memcpy(out, buffer, size < n ? size : n);
  return size;
}
template <>
inline auto write_big(char* out, size_t n, long_double value, int precision,
                      format fmt) noexcept -> size_t {
  return write_big(out, n, double(value), precision, fmt);
}
template <>
inline auto write_hex(char* buffer, long_double value, bool prefix) noexcept
    -> char* {
  return write_hex(buffer, double(value), prefix);
}
template <>
inline auto write_hex(char* out, size_t n, long_double value, int precision,
                      bool prefix) noexcept -> size_t {
  return write_hex(out, n, double(value), precision, prefix);
}
#endif  // LDBL_MANT_DIG == DBL_MANT_DIG || !ZMIJ_USE_LONG_DOUBLE

// Returns the past-the-end pointer after writing min(size, n) chars to `out`.
constexpr auto clamp_end(char* out, size_t size, size_t n) noexcept -> char* {
  return out + (size < n ? size : n);
}

// Copies the result in [`buffer`, `end`) to `out`, truncating after `n` chars,
// and returns the past-the-end pointer.
inline ZMIJ_CONSTEXPR20 auto copy_clamped(char* out, size_t n,
                                          const char* buffer,
                                          const char* end) noexcept -> char* {
  size_t size = size_t(end - buffer);
  if (is_constant_evaluated()) return copy_n(out, buffer, size < n ? size : n);
  memcpy(out, buffer, size < n ? size : n);
  return clamp_end(out, size, n);
}

// Writes the decimal representation of unsigned value to out and returns one
// past the last digit. Stores may run past that, up to the type's
// *_buffer_size.
template <typename UInt>
auto write_unsigned(char* out, UInt value) noexcept -> char*;

// Like write_unsigned for a signed value, with a '-' before negatives.
template <typename Int>
auto write_signed(char* out, Int value) noexcept -> char*;

}  // namespace detail

/// Converts `value` into the shortest correctly rounded decimal representation.
/// Usage:
///   auto [sig, exp, negative] = to_decimal(6.62607015e-34);
inline auto to_decimal(float value) noexcept -> dec_fp<> {
  return detail::to_decimal(value);
}
inline auto to_decimal(double value) noexcept -> dec_fp<> {
  return detail::to_decimal(value);
}
inline auto to_decimal(long_double value) noexcept
    -> dec_fp<detail::uint128_t> {
  return detail::to_decimal_big(value);
}

/// Buffer sizes for the write* functions, usable in generic code as
/// buffer_sizes<Float>::shortest, ::scientific, ::fixed, and ::hex.
/// `scientific` assumes precision up to 17 and `fixed` up to 18; long double
/// sets its own bounds below. Larger precision must be sized by the caller.
template <typename Float> struct buffer_sizes;

template <> struct buffer_sizes<float> {
  static constexpr size_t shortest = float_buffer_size;  // write
  static constexpr size_t scientific = 24;  // write_scientific (and general)
  static constexpr size_t fixed = 59;       // write_fixed
  static constexpr size_t hex = 16;         // write_hex
};
template <> struct buffer_sizes<double> {
  static constexpr size_t shortest = double_buffer_size;  // write
  static constexpr size_t scientific = 25;  // write_scientific (and general)
  static constexpr size_t fixed = 329;      // write_fixed
  static constexpr size_t hex = 24;         // write_hex
};
// long double: `scientific` is sized to round-trip: precision 35 (scientific)
// or 36 (general). `fixed` is omitted as it would need an impractical buffer,
// so callers must size fixed output themselves.
template <> struct buffer_sizes<long_double> {
  static constexpr size_t shortest = long_double_buffer_size;  // write
  static constexpr size_t scientific = 44;  // write_scientific (and general)
  // Worst case is IEEE binary128: 1 sign + "0x1." + 28 digits + "p+16383".
  static constexpr size_t hex = 40;  // write_hex
};

/// Writes the shortest correctly rounded decimal representation of `value` to
/// `out`, without a null terminator.
///
/// Returns a pointer past the last character written; if the representation
/// exceeds `n` characters, only the first `n` are written.
inline ZMIJ_CONSTEXPR20 auto write(char* out, size_t n, double value) noexcept
    -> char* {
  if (detail::is_constant_evaluated())
    return detail::clamp_end(out, detail::write_constexpr(out, n, value), n);
  char buffer[double_buffer_size];
  if (n >= sizeof(buffer)) return detail::write(out, value);
  return detail::copy_clamped(out, n, buffer, detail::write(buffer, value));
}

inline ZMIJ_CONSTEXPR20 auto write(char* out, size_t n, float value) noexcept
    -> char* {
  if (detail::is_constant_evaluated())
    return detail::clamp_end(out, detail::write_constexpr(out, n, value), n);
  char buffer[float_buffer_size];
  if (n >= sizeof(buffer)) return detail::write(out, value);
  return detail::copy_clamped(out, n, buffer, detail::write(buffer, value));
}

inline auto write(char* out, size_t n, long_double value) noexcept -> char* {
  if (LDBL_MANT_DIG == DBL_MANT_DIG) return write(out, n, double(value));
  return detail::clamp_end(out, detail::write_big(out, n, value), n);
}

/// Writes the decimal representation of `value` to `out` without a null
/// terminator. Returns a pointer past the last character written; if the
/// representation exceeds `n` characters, only the first `n` are written.
inline auto write(char* out, size_t n, int value) noexcept -> char* {
  char buffer[int32_buffer_size];
  if (n >= sizeof(buffer)) return detail::write_signed(out, int32_t(value));
  return detail::copy_clamped(out, n, buffer,
                              detail::write_signed(buffer, int32_t(value)));
}

inline auto write(char* out, size_t n, unsigned value) noexcept -> char* {
  char buffer[uint32_buffer_size];
  if (n >= sizeof(buffer)) return detail::write_unsigned(out, uint32_t(value));
  return detail::copy_clamped(out, n, buffer,
                              detail::write_unsigned(buffer, uint32_t(value)));
}

inline auto write(char* out, size_t n, long long value) noexcept -> char* {
  char buffer[int64_buffer_size];
  if (n >= sizeof(buffer)) return detail::write_signed(out, int64_t(value));
  return detail::copy_clamped(out, n, buffer,
                              detail::write_signed(buffer, int64_t(value)));
}

inline auto write(char* out, size_t n, unsigned long long value) noexcept
    -> char* {
  char buffer[uint64_buffer_size];
  if (n >= sizeof(buffer)) return detail::write_unsigned(out, uint64_t(value));
  return detail::copy_clamped(out, n, buffer,
                              detail::write_unsigned(buffer, uint64_t(value)));
}

inline auto write(char* out, size_t n, long value) noexcept -> char* {
  if (sizeof(long) <= sizeof(int32_t)) return write(out, n, int(value));
  return write(out, n, static_cast<long long>(value));
}

inline auto write(char* out, size_t n, unsigned long value) noexcept -> char* {
  if (sizeof(unsigned long) <= sizeof(uint32_t))
    return write(out, n, static_cast<unsigned>(value));
  return write(out, n, static_cast<unsigned long long>(value));
}

#if ZMIJ_USE_INT128
inline auto write(char* out, size_t n, __int128_t value) noexcept -> char* {
  char buffer[int128_buffer_size];
  if (n >= sizeof(buffer)) return detail::write_signed(out, value);
  return detail::copy_clamped(out, n, buffer,
                              detail::write_signed(buffer, value));
}

inline auto write(char* out, size_t n, __uint128_t value) noexcept -> char* {
  char buffer[uint128_buffer_size];
  if (n >= sizeof(buffer)) return detail::write_unsigned(out, value);
  return detail::copy_clamped(out, n, buffer,
                              detail::write_unsigned(buffer, value));
}
#endif  // ZMIJ_USE_INT128

/// Writes `value` in scientific format with `precision` digits after the
/// decimal point (e.g. 1.234e+05) to `out`, without a null terminator, like
/// printf's %e. A negative `precision` defaults to 6.
///
/// Returns a pointer past the last character written; if the representation
/// exceeds `n` characters, only the first `n` are written. The `long double`
/// overload returns nullptr on allocation failure.
inline ZMIJ_CONSTEXPR20 auto write_scientific(char* out, size_t n, double value,
                                              int precision) noexcept -> char* {
  if (precision < 0) precision = 6;
  if (precision >= 18 || detail::is_constant_evaluated()) {
    auto size = detail::write_big(out, n, value, precision, format::scientific);
    return detail::clamp_end(out, size, n);
  }
  char buffer[buffer_sizes<double>::scientific];
  if (n >= sizeof(buffer))
    return detail::write_scientific(out, value, precision + 1);
  return detail::copy_clamped(
      out, n, buffer, detail::write_scientific(buffer, value, precision + 1));
}

inline ZMIJ_CONSTEXPR20 auto write_scientific(char* out, size_t n, float value,
                                              int precision) noexcept -> char* {
  if (precision < 0) precision = 6;
  if (precision >= 18 || detail::is_constant_evaluated()) {
    auto size =
        detail::write_big(out, n, double(value), precision, format::scientific);
    return detail::clamp_end(out, size, n);
  }
  char buffer[buffer_sizes<float>::scientific];
  if (n >= sizeof(buffer))
    return detail::write_scientific(out, value, precision + 1);
  return detail::copy_clamped(
      out, n, buffer, detail::write_scientific(buffer, value, precision + 1));
}

inline auto write_scientific(char* out, size_t n, long_double value,
                             int precision) noexcept -> char* {
  if (double(value) == value)
    return write_scientific(out, n, double(value), precision);
  if (precision < 0) precision = 6;
  auto size = detail::write_big(out, n, value, precision, format::scientific);
  return size != 0 ? detail::clamp_end(out, size, n) : nullptr;
}

/// Writes `value` in general format with up to `precision` significant digits
/// and no trailing zeros (e.g. 1.5 or 1.5e+20) to `out`, without a null
/// terminator. Fixed notation is used when `value`'s decimal exponent is in
/// [-4, precision), and scientific otherwise. A negative `precision` defaults
/// to 6 and zero is treated as 1, matching printf.
///
/// Returns a pointer past the last character written; if the representation
/// exceeds `n` characters, only the first `n` are written. The `long double`
/// overload returns nullptr on allocation failure.
inline ZMIJ_CONSTEXPR20 auto write_general(char* out, size_t n, double value,
                                           int precision) noexcept -> char* {
  if (precision <= 1) precision = precision < 0 ? 6 : 1;
  if (precision > 18 || detail::is_constant_evaluated()) {
    auto size = detail::write_big(out, n, value, precision, format::general);
    return detail::clamp_end(out, size, n);
  }
  char buffer[buffer_sizes<double>::scientific];
  if (n >= sizeof(buffer)) return detail::write_general(out, value, precision);
  return detail::copy_clamped(out, n, buffer,
                              detail::write_general(buffer, value, precision));
}

inline ZMIJ_CONSTEXPR20 auto write_general(char* out, size_t n, float value,
                                           int precision) noexcept -> char* {
  if (precision <= 1) precision = precision < 0 ? 6 : 1;
  if (precision > 18 || detail::is_constant_evaluated()) {
    auto size =
        detail::write_big(out, n, double(value), precision, format::general);
    return detail::clamp_end(out, size, n);
  }
  char buffer[buffer_sizes<float>::scientific];
  if (n >= sizeof(buffer)) return detail::write_general(out, value, precision);
  return detail::copy_clamped(out, n, buffer,
                              detail::write_general(buffer, value, precision));
}

inline auto write_general(char* out, size_t n, long_double value,
                          int precision) noexcept -> char* {
  if (double(value) == value)
    return write_general(out, n, double(value), precision);
  if (precision <= 1) precision = precision < 0 ? 6 : 1;
  auto size = detail::write_big(out, n, value, precision, format::general);
  return size != 0 ? detail::clamp_end(out, size, n) : nullptr;
}

/// Writes `value` in fixed notation with exactly `precision` digits after the
/// decimal point (e.g. 1.500) to `out`, without a null terminator. The result
/// is the exact value correctly rounded (ties to even), matching printf's %f.
/// A negative `precision` defaults to 6.
///
/// Returns a pointer past the last character written; if the representation
/// exceeds `n` characters, only the first `n` are written. The `long double`
/// overload returns nullptr on allocation failure.
inline ZMIJ_CONSTEXPR20 auto write_fixed(char* out, size_t n, double value,
                                         int precision) noexcept -> char* {
  if (precision < 0) precision = 6;
  if (precision > 18 || detail::is_constant_evaluated()) {
    auto size = detail::write_big(out, n, value, precision, format::fixed);
    return detail::clamp_end(out, size, n);
  }
  char buffer[buffer_sizes<double>::fixed];
  if (n >= sizeof(buffer)) return detail::write_fixed(out, value, precision);
  return detail::copy_clamped(out, n, buffer,
                              detail::write_fixed(buffer, value, precision));
}

inline ZMIJ_CONSTEXPR20 auto write_fixed(char* out, size_t n, float value,
                                         int precision) noexcept -> char* {
  if (precision < 0) precision = 6;
  if (precision > 18 || detail::is_constant_evaluated()) {
    auto size =
        detail::write_big(out, n, double(value), precision, format::fixed);
    return detail::clamp_end(out, size, n);
  }
  char buffer[buffer_sizes<float>::fixed];
  if (n >= sizeof(buffer)) return detail::write_fixed(out, value, precision);
  return detail::copy_clamped(out, n, buffer,
                              detail::write_fixed(buffer, value, precision));
}

inline auto write_fixed(char* out, size_t n, long_double value,
                        int precision) noexcept -> char* {
  if (double(value) == value)
    return write_fixed(out, n, double(value), precision);
  if (precision < 0) precision = 6;
  auto size = detail::write_big(out, n, value, precision, format::fixed);
  return size != 0 ? detail::clamp_end(out, size, n) : nullptr;
}

/// Writes `value` in hexadecimal floating-point notation (like printf's %a) in
/// its shortest form (e.g. -0x1.8p+1) to `out`, without a null terminator.
///
/// Returns a pointer past the last character written; if the representation
/// exceeds `n` characters, only the first `n` are written.
inline ZMIJ_CONSTEXPR20 auto write_hex(char* out, size_t n,
                                       double value) noexcept -> char* {
  char buffer[buffer_sizes<double>::hex];
  if (n >= sizeof(buffer)) return detail::write_hex(out, value);
  return detail::copy_clamped(out, n, buffer, detail::write_hex(buffer, value));
}

inline ZMIJ_CONSTEXPR20 auto write_hex(char* out, size_t n,
                                       float value) noexcept -> char* {
  return write_hex(out, n, double(value));
}

inline auto write_hex(char* out, size_t n, long_double value) noexcept
    -> char* {
  char buffer[buffer_sizes<long_double>::hex];
  if (n >= sizeof(buffer)) return detail::write_hex(out, value);
  return detail::copy_clamped(out, n, buffer, detail::write_hex(buffer, value));
}

// Fixed-notation writes 16 scratch bytes starting at max_fixed_dec_exp + 2
// after the sign char.
enum {
    ecmascript_min_fixed_dec_exp = -6,
    ecmascript_max_fixed_dec_exp = 20,
    ecmascript_buffer_size = 1 + (ecmascript_max_fixed_dec_exp + 2) + 16,
};

/// Writes the ECMAScript decimal representation of `value` to `out`, without a
/// null terminator. Differences with write() are single-digit exponents without
/// a leading 0, fixed notation for decimal exponents -6 through 20, "NaN",
/// "Infinity", and no sign for -0 and -NaN.
///
/// Returns a pointer past the last character written; if the representation
/// exceeds `n` characters, only the first `n` are written. Copies unless the
/// buffer is at least `ecmascript_buffer_size` chars.
inline auto write_ecmascript(char* out, size_t n, double value) noexcept
    -> char* {
  char buffer[ecmascript_buffer_size];
  if (n >= sizeof(buffer)) return detail::write<double, true>(out, value);
  return detail::copy_clamped(
      out, n, buffer, detail::write<double, true>(buffer, value));
}

}  // namespace zmij

#endif  // ZMIJ_H_
