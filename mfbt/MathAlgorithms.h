/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* mfbt maths algorithms. */

#ifndef mozilla_MathAlgorithms_h
#define mozilla_MathAlgorithms_h

#include <algorithm>
#include <bit>
#include <climits>
#include <cmath>
#include <cstdint>
#include <type_traits>

#include "mozilla/Assertions.h"

namespace mozilla {

namespace detail {

template <typename T, typename = void>
struct AbsReturnType;

template <typename T>
struct AbsReturnType<
    T, std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T>>> {
  using Type = std::make_unsigned_t<T>;
};

template <typename T>
struct AbsReturnType<T, std::enable_if_t<std::is_floating_point_v<T>>> {
  using Type = T;
};

}  // namespace detail

template <typename T>
inline constexpr typename detail::AbsReturnType<T>::Type Abs(const T aValue) {
  using ReturnType = typename detail::AbsReturnType<T>::Type;
  return aValue >= 0 ? ReturnType(aValue) : ~ReturnType(aValue) + 1;
}

template <>
inline float Abs<float>(const float aFloat) {
  return std::fabs(aFloat);
}

template <>
inline double Abs<double>(const double aDouble) {
  return std::fabs(aDouble);
}

template <>
inline long double Abs<long double>(const long double aLongDouble) {
  return std::fabs(aLongDouble);
}

/**
 * Compute the log of the least power of 2 greater than or equal to |aValue|.
 *
 * CeilingLog2(0..1) is 0;
 * CeilingLog2(2) is 1;
 * CeilingLog2(3..4) is 2;
 * CeilingLog2(5..8) is 3;
 * CeilingLog2(9..16) is 4; and so on.
 */
template <typename T>
constexpr uint_fast8_t CeilingLog2(const T aValue) {
  static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);

  return aValue <= 1 ? 0u
                     : static_cast<uint_fast8_t>(
                           std::bit_width(static_cast<T>(aValue - 1)));
}

/** A CeilingLog2 variant that accepts only size_t. */
constexpr uint_fast8_t CeilingLog2Size(size_t aValue) {
  return CeilingLog2(aValue);
}

/**
 * Compute the bit position of the most significant bit set in
 * |aValue|. Requires that |aValue| is non-zero.
 */
template <typename T>
constexpr uint_fast8_t FindMostSignificantBit(T aValue) {
  static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);

  MOZ_ASSERT(aValue != 0);
  return static_cast<uint_fast8_t>(std::bit_width(aValue) - 1);
}

/**
 * Compute the log of the greatest power of 2 less than or equal to |aValue|.
 *
 * FloorLog2(0..1) is 0;
 * FloorLog2(2..3) is 1;
 * FloorLog2(4..7) is 2;
 * FloorLog2(8..15) is 3; and so on.
 */
template <typename T>
constexpr uint_fast8_t FloorLog2(const T aValue) {
  return FindMostSignificantBit(static_cast<T>(aValue | 1));
}

/** A FloorLog2 variant that accepts only size_t. */
constexpr uint_fast8_t FloorLog2Size(size_t aValue) {
  return FloorLog2(aValue);
}

/*
 * Compute the smallest power of 2 greater than or equal to |x|.  |x| must not
 * be so great that the computed value would overflow |size_t|.
 */
constexpr size_t RoundUpPow2(size_t aValue) {
  MOZ_ASSERT(aValue <= (size_t(1) << (sizeof(size_t) * CHAR_BIT - 1)),
             "can't round up -- will overflow!");
  return std::bit_ceil(aValue);
}

/**
 * Rotates the bits of the given value left by the amount of the shift width.
 */
template <typename T>
MOZ_NO_SANITIZE_UNSIGNED_OVERFLOW constexpr T RotateLeft(const T aValue,
                                                         uint_fast8_t aShift) {
  MOZ_ASSERT(aShift < sizeof(T) * CHAR_BIT, "Shift value is too large!");

  return std::rotl(aValue, aShift);
}

/**
 * Rotates the bits of the given value right by the amount of the shift width.
 */
template <typename T>
MOZ_NO_SANITIZE_UNSIGNED_OVERFLOW constexpr T RotateRight(const T aValue,
                                                          uint_fast8_t aShift) {
  MOZ_ASSERT(aShift < sizeof(T) * CHAR_BIT, "Shift value is too large!");

  return std::rotr(aValue, aShift);
}

// Greatest Common Divisor, from
// https://en.wikipedia.org/wiki/Binary_GCD_algorithm#Implementation
template <typename T>
constexpr MOZ_ALWAYS_INLINE T GCD(T aA, T aB) {
  static_assert(std::is_integral_v<T>);

  MOZ_ASSERT(aA >= 0);
  MOZ_ASSERT(aB >= 0);

  if (aA == 0) {
    return aB;
  }
  if (aB == 0) {
    return aA;
  }

  using UnsignedT = std::make_unsigned_t<T>;

  auto az = std::countr_zero(static_cast<UnsignedT>(aA));
  auto bz = std::countr_zero(static_cast<UnsignedT>(aB));
  auto shift = std::min<T>(az, bz);
  aA >>= az;
  aB >>= bz;

  while (aA != 0) {
    if constexpr (!std::is_signed_v<T>) {
      if (aA < aB) {
        std::swap(aA, aB);
      }
    }
    T diff = aA - aB;
    if constexpr (std::is_signed_v<T>) {
      aB = std::min<T>(aA, aB);
    }
    if constexpr (std::is_signed_v<T>) {
      aA = std::abs(diff);
    } else {
      aA = diff;
    }
    if (aA) {
      aA >>= std::countr_zero(static_cast<UnsignedT>(aA));
    }
  }

  return aB << shift;
}

} /* namespace mozilla */

#endif /* mozilla_MathAlgorithms_h */
