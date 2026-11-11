/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Portions of this code taken from WebKit, whose copyright is as follows:
 *
 *   Copyright (C) 2017 Caio Lima <ticaiolima@gmail.com>
 *   Copyright (C) 2017-2018 Apple Inc. All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *   THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 *   EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 *   PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 *   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 *   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 *   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 *   OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Portions of this code taken from V8, whose copyright notice is as follows:
 *
 *   Copyright 2017,2021 the V8 project authors. All rights reserved.
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are
 *   met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials provided
 *         with the distribution.
 *       * Neither the name of Google Inc. nor the names of its
 *         contributors may be used to endorse or promote products derived
 *         from this software without specific prior written permission.
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Portions of this code taken from Dart, whose copyright notice is as follows:
 *
 *   Copyright (c) 2014 the Dart project authors.  Please see the AUTHORS file
 * [1] for details. All rights reserved. Use of this source code is governed by
 * a BSD-style license that can be found in the LICENSE file [2].
 *
 *   [1] https://github.com/dart-lang/sdk/blob/master/AUTHORS
 *   [2] https://github.com/dart-lang/sdk/blob/master/LICENSE
 *
 * Portions of this code taken from Go, whose copyright notice is as follows:
 *
 *   Copyright 2009 The Go Authors. All rights reserved.
 *   Use of this source code is governed by a BSD-style
 *   license that can be found in the LICENSE file [3].
 *
 *   [3] https://golang.org/LICENSE
 */

#include "vm/BigIntType.h"

#include "mozilla/Casting.h"
#include "mozilla/CheckedArithmetic.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/FloatingPoint.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/Maybe.h"
#include "mozilla/MemoryChecking.h"
#include "mozilla/Range.h"
#include "mozilla/RangedPtr.h"
#include "mozilla/Span.h"  // mozilla::Span
#include "mozilla/TextUtils.h"
#include "mozilla/Try.h"
#include "mozilla/WrappingOperations.h"

#include <bit>
#include <charconv>
#include <functional>
#include <limits>
#include <memory>
#include <type_traits>  // std::is_same_v

#include "builtin/Number.h"
#include "gc/GCEnum.h"
#include "js/AllocPolicy.h"
#include "js/BigInt.h"
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "js/Printer.h"               // js::GenericPrinter
#include "js/StableStringChars.h"
#include "js/Utility.h"
#include "js/Vector.h"
#include "util/DifferentialTesting.h"
#include "vm/JSONPrinter.h"  // js::JSONPrinter
#include "vm/StaticStrings.h"

#include "gc/BufferAllocator-inl.h"
#include "gc/GCContext-inl.h"
#include "gc/Nursery-inl.h"
#include "vm/JSContext-inl.h"
#include "vm/StringType-inl.h"

using namespace js;

using JS::AutoStableStringChars;
using mozilla::Abs;
using mozilla::AssertedCast;
using mozilla::Maybe;
using mozilla::NegativeInfinity;
using mozilla::Nothing;
using mozilla::PositiveInfinity;
using mozilla::Range;
using mozilla::RangedPtr;
using mozilla::Some;
using mozilla::WrapToSigned;

#ifdef DEBUG
static bool HasLeadingZeroes(const BigInt* bi) {
  return bi->digitLength() > 0 &&
         bi->individualDigit(bi->digitLength() - 1) == 0;
}
#endif

BigInt* BigInt::createUninitialized(JSContext* cx, size_t digitLength,
                                    bool isNegative, gc::Heap heap) {
  if (digitLength > MaxDigitLength) {
    ReportOversizedAllocation(cx, JSMSG_BIGINT_TOO_LARGE);
    return nullptr;
  }

  BigInt* x = cx->newCell<BigInt>(heap);
  if (!x) {
    return nullptr;
  }

  x->setLengthAndFlags(digitLength, isNegative ? SignBit : 0);

  MOZ_ASSERT(x->digitLength() == digitLength);
  MOZ_ASSERT(x->isNegative() == isNegative);

  if (digitLength > InlineDigitsLength) {
    x->heapDigits_ = js::AllocateCellBuffer<Digit>(cx, x, digitLength);
    if (!x->heapDigits_) {
      // |x| is partially initialized, expose it as a BigInt using inline digits
      // to the GC.
      x->setLengthAndFlags(0, 0);
      return nullptr;
    }
  }

  return x;
}

void BigInt::initializeDigitsToZero() {
  auto digs = digits();
  std::uninitialized_fill_n(digs.begin(), digs.Length(), 0);
}

js::HashNumber BigInt::hash() const {
  js::HashNumber h =
      mozilla::HashBytes(digits().data(), digitLength() * sizeof(Digit));
  return mozilla::AddToHash(h, isNegative());
}

size_t BigInt::sizeOfExcludingThis() const {
  return hasInlineDigits() ? 0 : gc::GetAllocSize(zone(), heapDigits_);
}

size_t BigInt::sizeOfExcludingThisInNursery() const {
  MOZ_ASSERT(!isTenured());

  if (hasInlineDigits()) {
    return 0;
  }

  const Nursery& nursery = runtimeFromMainThread()->gc.nursery();
  if (nursery.isInside(heapDigits_)) {
    // Buffer allocations are aligned to the size of JS::Value.
    return RoundUp(digitLength() * sizeof(Digit), sizeof(Value));
  }

  return gc::GetAllocSize(zone(), heapDigits_);
}

BigInt* BigInt::zero(JSContext* cx, gc::Heap heap) {
  return createUninitialized(cx, 0, false, heap);
}

BigInt* BigInt::createFromDigit(JSContext* cx, Digit d, bool isNegative,
                                gc::Heap heap) {
  MOZ_ASSERT(d != 0);
  BigInt* res = createUninitialized(cx, 1, isNegative, heap);
  if (!res) {
    return nullptr;
  }
  res->setIndividualDigit(0, d);
  return res;
}

BigInt* BigInt::one(JSContext* cx) { return createFromDigit(cx, 1, false); }

BigInt* BigInt::negativeOne(JSContext* cx) {
  return createFromDigit(cx, 1, true);
}

BigInt* BigInt::createFromNonZeroRawUint64(JSContext* cx, uint64_t n,
                                           bool isNegative) {
  MOZ_ASSERT(n != 0);

  size_t resultLength = 1;
  if (DigitBits == 32 && (n >> 32) != 0) {
    resultLength = 2;
  }

  BigInt* result = createUninitialized(cx, resultLength, isNegative);
  if (!result) {
    return nullptr;
  }
  result->setIndividualDigit(0, n);
  if (DigitBits == 32 && resultLength > 1) {
    result->setIndividualDigit(1, n >> 32);
  }

  MOZ_ASSERT(!HasLeadingZeroes(result));
  return result;
}

BigInt* BigInt::neg(JSContext* cx, HandleBigInt x) {
  if (x->isZero()) {
    return x;
  }

  BigInt* result = copy(cx, x);
  if (!result) {
    return nullptr;
  }
  result->toggleHeaderFlagBit(SignBit);
  return result;
}

#if !defined(JS_64BIT)
#  define HAVE_TWO_DIGIT 1
using TwoDigit = uint64_t;
#elif defined(__SIZEOF_INT128__)
#  define HAVE_TWO_DIGIT 1
using TwoDigit = __uint128_t;
#endif

inline BigInt::Digit BigInt::digitAdd3(Digit a, Digit b, Digit c,
                                       Digit* carry) {
#if defined(HAVE_TWO_DIGIT)
  TwoDigit result = static_cast<TwoDigit>(a) + b + c;
  *carry = result >> DigitBits;
  return static_cast<Digit>(result);
#else
  Digit result = a + b;
  *carry = result < a;
  result += c;
  *carry += result < c;
  return result;
#endif
}

inline BigInt::Digit BigInt::digitSub3(Digit a, Digit b, Digit c,
                                       Digit* borrow) {
#if defined(HAVE_TWO_DIGIT)
  TwoDigit result = static_cast<TwoDigit>(a) - b - c;
  *borrow = -static_cast<Digit>(result >> DigitBits);
  return static_cast<Digit>(result);
#else
  Digit result = a - b;
  *borrow = result > a;
  Digit difference = result - c;
  *borrow += difference > result;
  return difference;
#endif
}

inline BigInt::Digit BigInt::digitMul(Digit a, Digit b, Digit* high) {
#if defined(HAVE_TWO_DIGIT)
  TwoDigit result = static_cast<TwoDigit>(a) * static_cast<TwoDigit>(b);
  *high = result >> DigitBits;

  return static_cast<Digit>(result);
#else
  // Multiply in half-pointer-sized chunks.
  // For inputs [AH AL]*[BH BL], the result is:
  //
  //            [AL*BL]  // rLow
  //    +    [AL*BH]     // rMid1
  //    +    [AH*BL]     // rMid2
  //    + [AH*BH]        // rHigh
  //    = [R4 R3 R2 R1]  // high = [R4 R3], low = [R2 R1]
  //
  // Where of course we must be careful with carries between the columns.
  Digit aLow = a & HalfDigitMask;
  Digit aHigh = a >> HalfDigitBits;
  Digit bLow = b & HalfDigitMask;
  Digit bHigh = b >> HalfDigitBits;

  Digit rLow = aLow * bLow;
  Digit rMid1 = aLow * bHigh;
  Digit rMid2 = aHigh * bLow;
  Digit rHigh = aHigh * bHigh;

  Digit carry;
  Digit low =
      digitAdd3(rLow, rMid1 << HalfDigitBits, rMid2 << HalfDigitBits, &carry);

  *high = (rMid1 >> HalfDigitBits) + (rMid2 >> HalfDigitBits) + rHigh + carry;

  return low;
#endif
}

inline BigInt::Digit BigInt::digitMulAdd(Digit a, Digit b, Digit c, Digit d,
                                         Digit* high) {
  // The result can't overflow: (B - 1)^2 + 2 * (B - 1) = B^2 - 1.
#if defined(HAVE_TWO_DIGIT)
  TwoDigit result = static_cast<TwoDigit>(a) * b + c + d;
  *high = result >> DigitBits;
  return static_cast<Digit>(result);
#else
  Digit carry;
  Digit low = digitMul(a, b, high);
  low = digitAdd3(low, c, d, &carry);
  *high += carry;
  return low;
#endif
}

BigInt::Digit BigInt::digitDiv(Digit high, Digit low, Digit divisor,
                               Digit* remainder) {
  MOZ_ASSERT(high < divisor, "division must not overflow");
#if defined(__x86_64__)
  Digit quotient;
  Digit rem;
  __asm__("divq  %[divisor]"
          // Outputs: `quotient` will be in rax, `rem` in rdx.
          : "=a"(quotient), "=d"(rem)
          // Inputs: put `high` into rdx, `low` into rax, and `divisor` into
          // any register or stack slot.
          : "d"(high), "a"(low), [divisor] "rm"(divisor));
  *remainder = rem;
  return quotient;
#elif defined(__i386__)
  Digit quotient;
  Digit rem;
  __asm__("divl  %[divisor]"
          // Outputs: `quotient` will be in eax, `rem` in edx.
          : "=a"(quotient), "=d"(rem)
          // Inputs: put `high` into edx, `low` into eax, and `divisor` into
          // any register or stack slot.
          : "d"(high), "a"(low), [divisor] "rm"(divisor));
  *remainder = rem;
  return quotient;
#else
  static constexpr Digit HalfDigitBase = 1ull << HalfDigitBits;
  // Adapted from Warren, Hacker's Delight, p. 152.
  unsigned s = std::countl_zero(divisor);
  // If `s` is DigitBits here, it causes an undefined behavior.
  // But `s` is never DigitBits since `divisor` is never zero here.
  MOZ_ASSERT(s != DigitBits);
  divisor <<= s;

  Digit vn1 = divisor >> HalfDigitBits;
  Digit vn0 = divisor & HalfDigitMask;

  // `sZeroMask` which is 0 if s == 0 and all 1-bits otherwise.
  //
  // `s` can be 0. If `s` is 0, performing "low >> (DigitBits - s)" must not
  // be done since it causes an undefined behavior since `>> DigitBits` is
  // undefined in C++. Quoted from C++ spec, "The type of the result is that of
  // the promoted left operand.
  //
  // The behavior is undefined if the right operand is negative, or greater
  // than or equal to the length in bits of the promoted left operand". We
  // mask the right operand of the shift by `shiftMask` (`DigitBits - 1`),
  // which makes `DigitBits - 0` zero.
  //
  // This shifting produces a value which covers 0 < `s` <= (DigitBits - 1)
  // cases. `s` == DigitBits never happen as we asserted.  Since `sZeroMask`
  // clears the value in the case of `s` == 0, `s` == 0 case is also covered.
  static_assert(sizeof(intptr_t) == sizeof(Digit),
                "unexpected size of BigInt::Digit");
  Digit sZeroMask =
      static_cast<Digit>((-static_cast<intptr_t>(s)) >> (DigitBits - 1));
  static constexpr unsigned shiftMask = DigitBits - 1;
  Digit un32 =
      (high << s) | ((low >> ((DigitBits - s) & shiftMask)) & sZeroMask);

  Digit un10 = low << s;
  Digit un1 = un10 >> HalfDigitBits;
  Digit un0 = un10 & HalfDigitMask;
  Digit q1 = un32 / vn1;
  Digit rhat = un32 - q1 * vn1;

  while (q1 >= HalfDigitBase || q1 * vn0 > rhat * HalfDigitBase + un1) {
    q1--;
    rhat += vn1;
    if (rhat >= HalfDigitBase) {
      break;
    }
  }

  Digit un21 = un32 * HalfDigitBase + un1 - q1 * divisor;
  Digit q0 = un21 / vn1;
  rhat = un21 - q0 * vn1;

  while (q0 >= HalfDigitBase || q0 * vn0 > rhat * HalfDigitBase + un0) {
    q0--;
    rhat += vn1;
    if (rhat >= HalfDigitBase) {
      break;
    }
  }

  *remainder = (un21 * HalfDigitBase + un0 - q0 * divisor) >> s;
  return q1 * HalfDigitBase + q0;
#endif
}

// Multiplies `source` with `factor` and adds `summand` to the result.
// `result` and `source` may be the same BigInt for inplace modification.
void BigInt::internalMultiplyAdd(const BigInt* source, Digit factor,
                                 Digit summand, unsigned n, BigInt* result) {
  MOZ_ASSERT(source->digitLength() >= n);
  MOZ_ASSERT(result->digitLength() >= n);

  auto sourceDigits = source->digits();
  auto resultDigits = result->digits();

  Digit carry = summand;
  Digit high = 0;
  for (unsigned i = 0; i < n; i++) {
    Digit current = sourceDigits[i];

    // Compute this round's multiplication.
    Digit newHigh = 0;
    current = digitMul(current, factor, &newHigh);

    // Add last round's carryovers.
    current = digitAdd3(current, high, carry, &carry);

    // Store result and prepare for next round.
    resultDigits[i] = current;
    high = newHigh;
  }

  if (resultDigits.size() > n) {
    resultDigits[n++] = carry + high;

    // Current callers don't pass in such large results, but let's be robust.
    while (n < resultDigits.size()) {
      resultDigits[n++] = 0;
    }
  } else {
    MOZ_ASSERT(!(carry + high));
  }
}

// Multiplies `this` with `factor` and adds `summand` to the result.
void BigInt::inplaceMultiplyAdd(Digit factor, Digit summand) {
  internalMultiplyAdd(this, factor, summand, digitLength(), this);
}

inline int8_t BigInt::absoluteCompare(ConstDigits x, ConstDigits y) {
  MOZ_RELEASE_ASSERT(x.size() == y.size());
  int i = x.size() - 1;
  while (i >= 0 && x[i] == y[i]) {
    i--;
  }

  if (i < 0) {
    return 0;
  }

  return x[i] > y[i] ? 1 : -1;
}

inline int8_t BigInt::absoluteCompare(const BigInt* x, const BigInt* y) {
  MOZ_ASSERT(!HasLeadingZeroes(x));
  MOZ_ASSERT(!HasLeadingZeroes(y));

  // Sanity checks to catch negative zeroes escaping to the wild.
  MOZ_ASSERT(!x->isNegative() || !x->isZero());
  MOZ_ASSERT(!y->isNegative() || !y->isZero());

  int diff = x->digitLength() - y->digitLength();
  if (diff) {
    return diff < 0 ? -1 : 1;
  }

  return absoluteCompare(x->unguardedDigits(), y->unguardedDigits());
}

BigInt* BigInt::absoluteAdd(JSContext* cx, HandleBigInt x, HandleBigInt y,
                            bool resultNegative) {
  bool swap = x->digitLength() < y->digitLength();
  // Ensure `left` has at least as many digits as `right`.
  HandleBigInt& left = swap ? y : x;
  HandleBigInt& right = swap ? x : y;

  if (left->isZero()) {
    MOZ_ASSERT(right->isZero());
    return left;
  }

  if (right->isZero()) {
    return resultNegative == left->isNegative() ? left : neg(cx, left);
  }

  // Fast path for the likely-common case of up to a uint64_t of magnitude.
  if (left->absFitsInUint64()) {
    MOZ_ASSERT(right->absFitsInUint64());

    uint64_t lhs = left->uint64FromAbsNonZero();
    uint64_t rhs = right->uint64FromAbsNonZero();

    uint64_t res = lhs + rhs;
    bool overflow = res < lhs;
    MOZ_ASSERT(res != 0 || overflow);

    size_t resultLength = 1;
    if (DigitBits == 32) {
      if (overflow) {
        resultLength = 3;
      } else if (res >> 32) {
        resultLength = 2;
      }
    } else {
      if (overflow) {
        resultLength = 2;
      }
    }
    BigInt* result = createUninitialized(cx, resultLength, resultNegative);
    if (!result) {
      return nullptr;
    }
    result->setIndividualDigit(0, res);
    if (DigitBits == 32 && resultLength > 1) {
      result->setIndividualDigit(1, res >> 32);
    }
    if (overflow) {
      constexpr size_t overflowIndex = DigitBits == 32 ? 2 : 1;
      result->setIndividualDigit(overflowIndex, 1);
    }

    MOZ_ASSERT(!HasLeadingZeroes(result));
    return result;
  }

  BigInt* result =
      createUninitialized(cx, left->digitLength() + 1, resultNegative);
  if (!result) {
    return nullptr;
  }
  {
    auto leftDigits = left->digits();
    auto rightDigits = right->digits();
    auto resultDigits = result->digits();
    Digit carry = 0;
    unsigned i = 0;
    for (; i < rightDigits.size(); i++) {
      resultDigits[i] = digitAdd3(leftDigits[i], rightDigits[i], carry, &carry);
    }

    for (; i < leftDigits.size(); i++) {
      resultDigits[i] = digitAdd(leftDigits[i], carry, &carry);
    }

    resultDigits[i] = carry;
  }

  return destructivelyTrimHighZeroDigits(cx, result);
}

BigInt* BigInt::absoluteSub(JSContext* cx, HandleBigInt x, HandleBigInt y,
                            bool resultNegative) {
  MOZ_ASSERT(x->digitLength() >= y->digitLength());
  MOZ_ASSERT(absoluteCompare(x, y) > 0);
  MOZ_ASSERT(!x->isZero());

  if (y->isZero()) {
    return resultNegative == x->isNegative() ? x : neg(cx, x);
  }

  // Fast path for the likely-common case of up to a uint64_t of magnitude.
  if (x->absFitsInUint64()) {
    MOZ_ASSERT(y->absFitsInUint64());

    uint64_t lhs = x->uint64FromAbsNonZero();
    uint64_t rhs = y->uint64FromAbsNonZero();
    MOZ_ASSERT(lhs > rhs);

    uint64_t res = lhs - rhs;
    MOZ_ASSERT(res != 0);

    return createFromNonZeroRawUint64(cx, res, resultNegative);
  }

  BigInt* result = createUninitialized(cx, x->digitLength(), resultNegative);
  if (!result) {
    return nullptr;
  }
  {
    auto xDigits = x->digits();
    auto yDigits = y->digits();
    auto resultDigits = result->digits();
    Digit borrow = 0;
    unsigned i = 0;
    for (; i < yDigits.size(); i++) {
      resultDigits[i] = digitSub3(xDigits[i], yDigits[i], borrow, &borrow);
    }

    for (; i < xDigits.size(); i++) {
      resultDigits[i] = digitSub(xDigits[i], borrow, &borrow);
    }

    MOZ_ASSERT(!borrow);
  }
  return destructivelyTrimHighZeroDigits(cx, result);
}

// Divides `x` by `divisor`, returning the result in `quotient` and `remainder`.
// Mathematically, the contract is:
//
//   quotient = (x - remainder) / divisor, with 0 <= remainder < divisor.
//
// If `quotient` is an empty handle, an appropriately sized BigInt will be
// allocated for it; otherwise the caller must ensure that it is big enough.
// `quotient` can be the same as `x` for an in-place division. `quotient` can
// also be `Nothing()` if the caller is only interested in the remainder.
//
// This function returns false if `quotient` is an empty handle, but allocating
// the quotient failed.  Otherwise it returns true, indicating success.
bool BigInt::absoluteDivWithDigitDivisor(
    JSContext* cx, HandleBigInt x, Digit divisor,
    const Maybe<MutableHandleBigInt>& quotient, Digit* remainder,
    bool quotientNegative) {
  MOZ_ASSERT(divisor);

  MOZ_ASSERT(!x->isZero());
  *remainder = 0;
  if (divisor == 1) {
    if (quotient) {
      BigInt* q;
      if (x->isNegative() == quotientNegative) {
        q = x;
      } else {
        q = neg(cx, x);
        if (!q) {
          return false;
        }
      }
      quotient.value().set(q);
    }
    return true;
  }

  unsigned length = x->digitLength();
  if (quotient) {
    if (!quotient.value()) {
      BigInt* q = createUninitialized(cx, length, quotientNegative);
      if (!q) {
        return false;
      }
      quotient.value().set(q);
    }

    auto xDigits = x->digits();
    auto quotientDigits = quotient.value()->digits();
    for (int i = length - 1; i >= 0; i--) {
      Digit q = digitDiv(*remainder, xDigits[i], divisor, remainder);
      quotientDigits[i] = q;
    }
  } else {
    auto xDigits = x->digits();
    for (int i = length - 1; i >= 0; i--) {
      digitDiv(*remainder, xDigits[i], divisor, remainder);
    }
  }

  return true;
}

// Returns whether (factor1 * factor2) > (high << kDigitBits) + low.
inline bool BigInt::productGreaterThan(Digit factor1, Digit factor2, Digit high,
                                       Digit low) {
  Digit resultHigh;
  Digit resultLow = digitMul(factor1, factor2, &resultHigh);
  return resultHigh > high || (resultHigh == high && resultLow > low);
}

void BigInt::inplaceRightShiftLowZeroBits(unsigned shift) {
  MOZ_ASSERT(shift < DigitBits);
  auto thisDigits = digits();
  MOZ_ASSERT(!(thisDigits[0] & ((static_cast<Digit>(1) << shift) - 1)),
             "should only be shifting away zeroes");

  if (!shift) {
    return;
  }

  Digit carry = thisDigits[0] >> shift;
  unsigned last = thisDigits.size() - 1;
  for (unsigned i = 0; i < last; i++) {
    Digit d = thisDigits[i + 1];
    thisDigits[i] = (d << (DigitBits - shift)) | carry;
    carry = d >> shift;
  }
  thisDigits[last] = carry;
}

// Always copies the input, even when `shift` == 0.
BigInt* BigInt::absoluteLeftShiftAlwaysCopy(JSContext* cx, HandleBigInt x,
                                            unsigned shift,
                                            LeftShiftMode mode) {
  MOZ_ASSERT(shift < DigitBits);
  MOZ_ASSERT(!x->isZero());

  unsigned n = x->digitLength();
  unsigned resultLength = mode == LeftShiftMode::AlwaysAddOneDigit ? n + 1 : n;
  BigInt* result = createUninitialized(cx, resultLength, x->isNegative());
  if (!result) {
    return nullptr;
  }

  auto xDigits = x->digits();
  auto resultDigits = result->digits();

  if (!shift) {
    for (unsigned i = 0; i < n; i++) {
      resultDigits[i] = xDigits[i];
    }
    if (mode == LeftShiftMode::AlwaysAddOneDigit) {
      resultDigits[n] = 0;
    }

    return result;
  }

  Digit carry = 0;
  for (unsigned i = 0; i < n; i++) {
    Digit d = xDigits[i];
    resultDigits[i] = (d << shift) | carry;
    carry = d >> (DigitBits - shift);
  }

  if (mode == LeftShiftMode::AlwaysAddOneDigit) {
    resultDigits[n] = carry;
  } else {
    MOZ_ASSERT(mode == LeftShiftMode::SameSizeResult);
    MOZ_ASSERT(!carry);
  }

  return result;
}

// The schoolbook division algorithm computes an estimate of the quotient
// (q-hat) by dividing each pair of digits in the dividend by the top digit of
// the divisor. This two-by-one division is fine on architectures with a wide
// hardware division instruction, but AArch64 is not one of those; there it is
// much faster to divide by multiplying against an inverse, and since each
// operation has the same divisor, we can compute the inverse once. As it turns
// out, it's just as fast to do things this way on x86/x86-64, so we just take
// the same path everywhere.
//
// Note that this relies on the divisor being "normalized" (left-shifted such
// that its most significant bit is set); this normalization is stripped off in
// the end as part of the schoolbook algorithm.
//
// This routine was derived from V8's MultiplicativeDigitDiv.
BigInt::Digit BigInt::normalizedDigitInverse(Digit divisor) {
  MOZ_ASSERT(divisor & (Digit(1) << (DigitBits - 1)));
  Digit high = ~divisor;
  Digit low = Digit(-1);
  Digit remainder;
  return digitDiv(high, low, divisor, &remainder);
}

// This routine was derived from V8's MultiplicativeDigitDiv.
BigInt::Digit BigInt::digitDivByMultiplication(Digit high, Digit low,
                                               Digit divisor, Digit inverse,
                                               Digit* remainder) {
  MOZ_ASSERT(divisor & (Digit(1) << (DigitBits - 1)));
  MOZ_ASSERT(high < divisor);
  MOZ_ASSERT(inverse == normalizedDigitInverse(divisor));
  // https://gmplib.org/~tege/division-paper.pdf, "Algorithm 4".
  // Paper: u1 = high, u0 = low, d = divisor, v = inverse,
  // "mod β" means "truncate to Digit width".
  // 1. <q1, q0> ← v*u1
  Digit q1;
  Digit q0 = digitMul(high, inverse, &q1);
  // 2. <q1, q0> ← <q1, q0> + <u1, u0>
  Digit carry;
  q0 = digitAdd(q0, low, &carry);
  q1 = digitAdd3(q1, high, carry, &carry);
  MOZ_ASSERT(carry == 0);
  // 3. q1 ← (q1 + 1) mod β
  q1++;
  // 4. r ← (u0 − q1*d) mod β
  Digit r = low - q1 * divisor;
  // 5. if r > q0:  // Unpredictable condition
  if (r > q0) {
    // 6. q1 ← (q1 − 1) mod β
    q1--;
    // 7. r ← (r + d) mod β
    r += divisor;
  }
  // 8 if r >= d:  // Unlikely condition
  if (MOZ_UNLIKELY(r >= divisor)) {
    // 9. q1 ← q1 + 1
    q1++;
    // 10. r ← r − d
    r -= divisor;
  }
  // 11. return q1, r
  *remainder = r;

#ifdef DEBUG
  {
    Digit q, r;
    q = digitDiv(high, low, divisor, &r);
    MOZ_ASSERT(q == q1);
    MOZ_ASSERT(r == *remainder);
  }
#endif

  return q1;
}

BigInt::Digit BigInt::multiplyDigits(ConstDigits src, Digit multiplier,
                                     Digits dest) {
  dest = dest.First(src.size());
  Digit carry = 0;
  for (size_t i = 0; i < src.size(); i++) {
    dest[i] = digitMulAdd(src[i], multiplier, 0, carry, &carry);
  }
  return carry;
}

BigInt::Digit BigInt::multiplyAccumulateDigits(ConstDigits src,
                                               Digit multiplier, Digits acc) {
  acc = acc.First(src.size());
  Digit carry = 0;
  for (size_t i = 0; i < src.size(); i++) {
    acc[i] = digitMulAdd(src[i], multiplier, acc[i], carry, &carry);
  }
  return carry;
}

void BigInt::schoolbookMultiplyDigits(ConstDigits a, ConstDigits b,
                                      Digits result) {
  // Given a choice, it's best to do fewer outer loops over longer digit
  // spans. Commute the arguments to ensure that `|a| >= |b|`, if needed.
  if (a.size() < b.size()) {
    std::swap(b, a);
  }
  MOZ_RELEASE_ASSERT(!b.empty());
  MOZ_RELEASE_ASSERT(result.size() == a.size() + b.size());
  result[a.size()] = multiplyDigits(a, b[0], result);
  for (size_t i = 1; i < b.size(); i++) {
    result[i + a.size()] =
        b[i] ? multiplyAccumulateDigits(a, b[i], result.From(i)) : 0;
  }
}

// Both inplaceSubDigits and inplaceAddDigits check the bounds once, by taking
// a subspan of `x` of the same length as `y`.
BigInt::Digit BigInt::inplaceSubDigits(Digits x, ConstDigits y) {
  Digits xs = x.First(y.size());
  Digit borrow = 0;
  for (size_t i = 0; i < y.size(); i++) {
    xs[i] = digitSub3(xs[i], y[i], borrow, &borrow);
  }
  return borrow;
}

BigInt::Digit BigInt::inplaceAddDigits(Digits x, ConstDigits y) {
  Digits xs = x.First(y.size());
  Digit carry = 0;
  for (size_t i = 0; i < y.size(); i++) {
    xs[i] = digitAdd3(xs[i], y[i], carry, &carry);
  }
  return carry;
}

// See Knuth, Volume 2, section 4.3.1, Algorithm D.  Also see the overview of
// the algorithm by Jan Marthedal Rasmussen over at
// https://janmr.com/blog/2014/04/basic-multiple-precision-long-division/.
//
// On return the low `v.size()` digits of `u` hold the remainder.
void BigInt::divideDigitsInPlace(Digits u, ConstDigits v, Digits q,
                                 Digits qhatv) {
  // The unusual variable names inside this function are consistent with
  // Knuth's book, as well as with Go's implementation of this algorithm.
  // Maintaining this consistency is probably more useful than trying to
  // come up with more descriptive names for them.

  // First, check preconditions. Use release asserts on sizes, so that the
  // compiler can remove bounds checks on Digits::operator[] access.
  const size_t n = v.size();
  // `v` must have at least two digits with the high bit of its top digit set.
  MOZ_RELEASE_ASSERT(n >= 2);
  MOZ_ASSERT(v[n - 1] >> (DigitBits - 1));

  // `u` must have more digits than `v`, with the top `v.size()` digits of `u`
  // less than `v`.
  MOZ_RELEASE_ASSERT(u.size() > n);
  MOZ_ASSERT(absoluteCompare(u.From(u.size() - n), v) < 0);
  // If `q` is non-empty it must have `u.size() - v.size()` digits and receives
  // the quotient.
  MOZ_RELEASE_ASSERT(q.empty() || q.size() == u.size() - n);
  // `qhatv` must have `v.size() + 1` digits.
  MOZ_RELEASE_ASSERT(qhatv.size() == n + 1);
  const size_t m = u.size() - n - 1;

  // D2.
  // Iterate over the dividend's digit (like the "grade school" algorithm).
  // `vn1` is the divisor's most significant digit.
  Digit vn1 = v[n - 1];
  Digit vn2 = v[n - 2];
  Digit vn1Inverse = normalizedDigitInverse(vn1);
  for (size_t j = m + 1; j-- > 0;) {
    // The digits of `u` that this iteration works on.
    Digits uj = u.From(j).First(n + 1);

    // D3.
    // Estimate the current iteration's quotient digit (see Knuth for
    // details). `qhat` is the current quotient digit.
    Digit qhat = std::numeric_limits<Digit>::max();

    // `ujn` is the dividend's most significant remaining digit.
    Digit ujn = uj[n];
    if (ujn != vn1) {
      // `rhat` is the current iteration's remainder.
      Digit rhat = 0;
      // Estimate the current quotient digit by dividing the most significant
      // digits of dividend and divisor. The result will not be too small,
      // but could be a bit too large.
      qhat = digitDivByMultiplication(ujn, uj[n - 1], vn1, vn1Inverse, &rhat);

      // Decrement the quotient estimate as needed by looking at the next
      // digit, i.e. by testing whether
      // qhat * v_{n-2} > (rhat << DigitBits) + u_{j+n-2}.
      Digit ujn2 = uj[n - 2];
      while (productGreaterThan(qhat, vn2, rhat, ujn2)) {
        qhat--;
        Digit prevRhat = rhat;
        rhat += vn1;
        // v[n-1] >= 0, so this tests for overflow.
        if (rhat < prevRhat) {
          break;
        }
      }
    }

    // D4.
    // Multiply the divisor with the current quotient digit, and subtract
    // it from the dividend. If there was "borrow", then the quotient digit
    // was one too high, so we must correct it and undo one subtraction of
    // the (shifted) divisor.
    qhatv[n] = multiplyDigits(v, qhat, qhatv);
    Digit c = inplaceSubDigits(uj, qhatv);
    if (c) {
      c = inplaceAddDigits(uj, v);
      uj[n] = uj[n] + c;
      qhat--;
    }

    if (!q.empty()) {
      q[j] = qhat;
    }
  }
}

// Divides `dividend` by `divisor`, returning the result in `quotient` and
// `remainder`. Mathematically, the contract is:
//
//   quotient = (dividend - remainder) / divisor, with 0 <= remainder < divisor.
//
// Both `quotient` and `remainder` are optional, for callers that are only
// interested in one of them.
bool BigInt::absoluteDivWithBigIntDivisor(
    JSContext* cx, HandleBigInt dividend, HandleBigInt divisor,
    const Maybe<MutableHandleBigInt>& quotient,
    const Maybe<MutableHandleBigInt>& remainder, bool isNegative) {
  MOZ_ASSERT(divisor->digitLength() >= 2);
  MOZ_ASSERT(dividend->digitLength() >= divisor->digitLength());

  // Any early error return is detectable by checking the quotient and/or
  // remainder output values.
  MOZ_ASSERT(!quotient || !quotient.value());
  MOZ_ASSERT(!remainder || !remainder.value());

  const unsigned n = divisor->digitLength();
  const unsigned m = dividend->digitLength() - n;

  RootedTuple<BigInt*, BigInt*, BigInt*> roots(cx);

  // The quotient to be computed.
  RootedField<BigInt*, 0> q(roots);
  if (quotient) {
    q = createUninitialized(cx, m + 1, isNegative);
    if (!q) {
      return false;
    }
  }

  // In each iteration, `qhatv` holds `divisor` * `current quotient digit`.
  // "v" is the book's name for `divisor`, `qhat` the current quotient digit.
  Vector<Digit, 16, SystemAllocPolicy> qhatv;
  if (!qhatv.resize(n + 1)) {
    ReportOutOfMemory(cx);
    return false;
  }

  // D1.
  // Left-shift inputs so that the divisor's MSB is set. This is necessary to
  // prevent the digit-wise divisions (see digitDiv call below) from
  // overflowing (they take a two digits wide input, and return a one digit
  // result).
  Digit lastDigit = divisor->individualDigit(n - 1);
  unsigned shift = std::countl_zero(lastDigit);

  RootedField<BigInt*, 1> shiftedDivisor(roots);
  if (shift > 0) {
    shiftedDivisor = absoluteLeftShiftAlwaysCopy(cx, divisor, shift,
                                                 LeftShiftMode::SameSizeResult);
    if (!shiftedDivisor) {
      return false;
    }
  } else {
    shiftedDivisor = divisor;
  }

  // Holds the (continuously updated) remaining part of the dividend, which
  // eventually becomes the remainder.
  RootedField<BigInt*, 2> u(
      roots, absoluteLeftShiftAlwaysCopy(cx, dividend, shift,
                                         LeftShiftMode::AlwaysAddOneDigit));
  if (!u) {
    return false;
  }

  divideDigitsInPlace(u->unguardedDigits(),
                      shiftedDivisor->unguardedDigits().First(n),
                      quotient ? q->unguardedDigits() : Digits(), qhatv);

  if (quotient) {
    BigInt* bi = destructivelyTrimHighZeroDigits(cx, q);
    if (!bi) {
      return false;
    }
    quotient.value().set(q);
  }

  if (remainder) {
    u->inplaceRightShiftLowZeroBits(shift);
    remainder.value().set(u);
  }

  return true;
}

// Helper for Absolute{And,AndNot,Or,Xor}.
// Performs the given binary `op` on digit pairs of `x` and `y`; when the
// end of the shorter of the two is reached, `kind` configures how
// remaining digits are handled.
// Example:
//       y:             [ y2 ][ y1 ][ y0 ]
//       x:       [ x3 ][ x2 ][ x1 ][ x0 ]
//                   |     |     |     |
//                (Fill)  (op)  (op)  (op)
//                   |     |     |     |
//                   v     v     v     v
//  result: [  0 ][ x3 ][ r2 ][ r1 ][ r0 ]
template <BigInt::BitwiseOpKind kind, typename BitwiseOp>
inline BigInt* BigInt::absoluteBitwiseOp(JSContext* cx, HandleBigInt x,
                                         HandleBigInt y, BitwiseOp&& op) {
  unsigned xLength = x->digitLength();
  unsigned yLength = y->digitLength();
  unsigned numPairs = std::min(xLength, yLength);
  unsigned resultLength;
  if (kind == BitwiseOpKind::SymmetricTrim) {
    resultLength = numPairs;
  } else if (kind == BitwiseOpKind::SymmetricFill) {
    resultLength = std::max(xLength, yLength);
  } else {
    MOZ_ASSERT(kind == BitwiseOpKind::AsymmetricFill);
    resultLength = xLength;
  }
  bool resultNegative = false;

  BigInt* result = createUninitialized(cx, resultLength, resultNegative);
  if (!result) {
    return nullptr;
  }
  {
    auto xDigits = x->digits();
    auto yDigits = y->digits();
    auto resultDigits = result->digits();

    unsigned i = 0;
    for (; i < numPairs; i++) {
      resultDigits[i] = op(xDigits[i], yDigits[i]);
    }

    if (kind != BitwiseOpKind::SymmetricTrim) {
      DigitsGuard& sourceDigits = kind == BitwiseOpKind::AsymmetricFill
                                      ? xDigits
                                  : xLength == i ? yDigits
                                                 : xDigits;
      for (; i < resultLength; i++) {
        resultDigits[i] = sourceDigits[i];
      }
    }

    MOZ_ASSERT(i == resultLength);
  }
  return destructivelyTrimHighZeroDigits(cx, result);
}

BigInt* BigInt::absoluteAnd(JSContext* cx, HandleBigInt x, HandleBigInt y) {
  return absoluteBitwiseOp<BitwiseOpKind::SymmetricTrim>(cx, x, y,
                                                         std::bit_and<>());
}

BigInt* BigInt::absoluteOr(JSContext* cx, HandleBigInt x, HandleBigInt y) {
  return absoluteBitwiseOp<BitwiseOpKind::SymmetricFill>(cx, x, y,
                                                         std::bit_or<>());
}

BigInt* BigInt::absoluteAndNot(JSContext* cx, HandleBigInt x, HandleBigInt y) {
  auto digitOperation = [](Digit a, Digit b) { return a & ~b; };
  return absoluteBitwiseOp<BitwiseOpKind::AsymmetricFill>(cx, x, y,
                                                          digitOperation);
}

BigInt* BigInt::absoluteXor(JSContext* cx, HandleBigInt x, HandleBigInt y) {
  return absoluteBitwiseOp<BitwiseOpKind::SymmetricFill>(cx, x, y,
                                                         std::bit_xor<>());
}

BigInt* BigInt::absoluteAddOne(JSContext* cx, HandleBigInt x,
                               bool resultNegative) {
  unsigned inputLength = x->digitLength();
  // The addition will overflow into a new digit if all existing digits are
  // at maximum.
  bool willOverflow = true;
  {
    auto xDigits = x->digits();
    for (unsigned i = 0; i < inputLength; i++) {
      if (std::numeric_limits<Digit>::max() != xDigits[i]) {
        willOverflow = false;
        break;
      }
    }
  }

  unsigned resultLength = inputLength + willOverflow;
  BigInt* result = createUninitialized(cx, resultLength, resultNegative);
  if (!result) {
    return nullptr;
  }
  {
    auto xDigits = x->digits();
    auto resultDigits = result->digits();
    Digit carry = 1;
    for (unsigned i = 0; i < inputLength; i++) {
      resultDigits[i] = digitAdd(xDigits[i], carry, &carry);
    }
    if (resultLength > inputLength) {
      MOZ_ASSERT(carry == 1);
      resultDigits[inputLength] = 1;
    } else {
      MOZ_ASSERT(!carry);
    }
  }
  return destructivelyTrimHighZeroDigits(cx, result);
}

BigInt* BigInt::absoluteSubOne(JSContext* cx, HandleBigInt x,
                               bool resultNegative) {
  MOZ_ASSERT(!x->isZero());

  unsigned length = x->digitLength();

  if (length == 1) {
    Digit d = x->individualDigit(0);
    if (d == 1) {
      // Ignore resultNegative.
      return zero(cx);
    }
    return createFromDigit(cx, d - 1, resultNegative);
  }

  BigInt* result = createUninitialized(cx, length, resultNegative);
  if (!result) {
    return nullptr;
  }
  {
    auto xDigits = x->digits();
    auto resultDigits = result->digits();
    Digit borrow = 1;
    for (unsigned i = 0; i < length; i++) {
      resultDigits[i] = digitSub(xDigits[i], borrow, &borrow);
    }
    MOZ_ASSERT(!borrow);
  }
  return destructivelyTrimHighZeroDigits(cx, result);
}

BigInt* BigInt::inc(JSContext* cx, HandleBigInt x) {
  if (x->isZero()) {
    return one(cx);
  }

  bool isNegative = x->isNegative();
  if (isNegative) {
    return absoluteSubOne(cx, x, isNegative);
  }

  return absoluteAddOne(cx, x, isNegative);
}

BigInt* BigInt::dec(JSContext* cx, HandleBigInt x) {
  if (x->isZero()) {
    return negativeOne(cx);
  }

  bool isNegative = x->isNegative();
  if (isNegative) {
    return absoluteAddOne(cx, x, isNegative);
  }

  return absoluteSubOne(cx, x, isNegative);
}

// Lookup table for the maximum number of bits required per character of a
// base-N string representation of a number. To increase accuracy, the array
// value is the actual value multiplied by 32. To generate this table:
// for (var i = 0; i <= 36; i++) { print(Math.ceil(Math.log2(i) * 32) + ","); }
static constexpr uint8_t maxBitsPerCharTable[] = {
    0,   0,   32,  51,  64,  75,  83,  90,  96,  // 0..8
    102, 107, 111, 115, 119, 122, 126, 128,      // 9..16
    131, 134, 136, 139, 141, 143, 145, 147,      // 17..24
    149, 151, 153, 154, 156, 158, 159, 160,      // 25..32
    162, 163, 165, 166,                          // 33..36
};

static constexpr unsigned bitsPerCharTableShift = 5;
static constexpr size_t bitsPerCharTableMultiplier = 1u
                                                     << bitsPerCharTableShift;
static constexpr char radixDigits[] = "0123456789abcdefghijklmnopqrstuvwxyz";

static inline uint64_t CeilDiv(uint64_t numerator, uint64_t denominator) {
  MOZ_ASSERT(numerator != 0);
  return 1 + (numerator - 1) / denominator;
};

// Compute (an overapproximation of) the length of the string representation of
// a BigInt.  In base B an X-digit number has maximum value:
//
//   B**X - 1
//
// We're trying to find N for an N-digit number in base |radix| full
// representing a |bitLength|-digit number in base 2, so we have:
//
//   radix**N - 1 ≥ 2**bitLength - 1
//   radix**N ≥ 2**bitLength
//   N ≥ log2(2**bitLength) / log2(radix)
//   N ≥ bitLength / log2(radix)
//
// so the smallest N is:
//
//   N = ⌈bitLength / log2(radix)⌉
//
// We want to avoid floating-point computations and precompute the logarithm, so
// we multiply both sides of the division by |bitsPerCharTableMultiplier|:
//
//   N = ⌈(bPCTM * bitLength) / (bPCTM * log2(radix))⌉
//
// and then because |maxBitsPerChar| representing the denominator may have been
// rounded *up* -- which could produce an overall under-computation -- we reduce
// by one to undo any rounding and conservatively compute:
//
//   N ≥ ⌈(bPCTM * bitLength) / (maxBitsPerChar - 1)⌉
//
size_t BigInt::calculateMaximumCharactersRequired(HandleBigInt x,
                                                  unsigned radix) {
  MOZ_ASSERT(!x->isZero());
  MOZ_ASSERT(radix >= 2 && radix <= 36);

  auto xDigits = x->digits();
  size_t length = xDigits.size();
  Digit lastDigit = xDigits[length - 1];
  size_t bitLength = length * DigitBits - std::countl_zero(lastDigit);

  uint8_t maxBitsPerChar = maxBitsPerCharTable[radix];
  uint64_t maximumCharactersRequired =
      CeilDiv(static_cast<uint64_t>(bitsPerCharTableMultiplier) * bitLength,
              maxBitsPerChar - 1);
  maximumCharactersRequired += x->isNegative();

  return AssertedCast<size_t>(maximumCharactersRequired);
}

template <AllowGC allowGC>
JSLinearString* BigInt::toStringBasePowerOfTwo(JSContext* cx, HandleBigInt x,
                                               unsigned radix) {
  MOZ_ASSERT(std::has_single_bit(radix));
  MOZ_ASSERT(radix >= 2 && radix <= 32);
  MOZ_ASSERT(!x->isZero());
  MOZ_ASSERT(x->digitLength() > 1);

  const unsigned length = x->digitLength();
  const bool sign = x->isNegative();
  const unsigned bitsPerChar = std::countr_zero(radix);
  const unsigned charMask = radix - 1;
  // Compute the length of the resulting string: divide the bit length of the
  // BigInt by the number of bits representable per character (rounding up).
  const Digit msd = x->individualDigit(length - 1);

  const size_t bitLength = length * DigitBits - std::countl_zero(msd);
  const size_t charsRequired = CeilDiv(bitLength, bitsPerChar) + sign;

  static_assert(MaxBitLength + 1 <= JSString::MAX_LENGTH,
                "Base 2 representation (including leading sign) of the largest "
                "possible BigInt fits into a string");

  MOZ_RELEASE_ASSERT(charsRequired <= JSString::MAX_LENGTH);

  StringChars<JS::Latin1Char> stringChars(cx);
  if (!stringChars.maybeAlloc(cx, charsRequired)) {
    if constexpr (!allowGC) {
      cx->recoverFromOutOfMemory();
    }
    return nullptr;
  }

  {
    JS::AutoCheckCannotGC nogc;
    auto* resultChars = stringChars.data(nogc);
    const auto xDigits = x->digits();

    Digit digit = 0;
    // Keeps track of how many unprocessed bits there are in |digit|.
    unsigned availableBits = 0;
    size_t pos = charsRequired;
    for (unsigned i = 0; i < length - 1; i++) {
      Digit newDigit = xDigits[i];
      // Take any leftover bits from the last iteration into account.
      unsigned current = (digit | (newDigit << availableBits)) & charMask;
      MOZ_ASSERT(pos);
      resultChars[--pos] = radixDigits[current];
      unsigned consumedBits = bitsPerChar - availableBits;
      digit = newDigit >> consumedBits;
      availableBits = DigitBits - consumedBits;
      while (availableBits >= bitsPerChar) {
        MOZ_ASSERT(pos);
        resultChars[--pos] = radixDigits[digit & charMask];
        digit >>= bitsPerChar;
        availableBits -= bitsPerChar;
      }
    }

    // Write out the character containing the lowest-order bit of |msd|.
    //
    // This character may include leftover bits from the Digit below |msd|.  For
    // example, if |x === 2n**64n| and |radix == 32|: the preceding loop writes
    // twelve zeroes for low-order bits 0-59 in |x->digit(0)| (and |x->digit(1)|
    // on 32-bit); then the highest 4 bits of of |x->digit(0)| (or |x->digit(1)|
    // on 32-bit) and bit 0 of |x->digit(1)| (|x->digit(2)| on 32-bit) will
    // comprise the |current == 0b1'0000| computed below for the high-order 'g'
    // character.
    unsigned current = (digit | (msd << availableBits)) & charMask;
    MOZ_ASSERT(pos);
    resultChars[--pos] = radixDigits[current];

    // Write out remaining characters represented by |msd|.  (There may be none,
    // as in the example above.)
    digit = msd >> (bitsPerChar - availableBits);
    while (digit != 0) {
      MOZ_ASSERT(pos);
      resultChars[--pos] = radixDigits[digit & charMask];
      digit >>= bitsPerChar;
    }

    if (sign) {
      MOZ_ASSERT(pos);
      resultChars[--pos] = '-';
    }
    MOZ_ASSERT(pos == 0);
  }

  return stringChars.toStringDontDeflateNonStatic<allowGC>(cx, charsRequired);
}

template <AllowGC allowGC>
JSLinearString* BigInt::toStringSingleDigit(JSContext* cx, Digit digit,
                                            bool isNegative, unsigned radix) {
  MOZ_ASSERT(digit != 0, "zero case should have been handled in toString");

  if (digit <= Digit(INT32_MAX)) {
    int32_t val = AssertedCast<int32_t>(digit);
    return Int32ToStringWithBase<allowGC>(cx, isNegative ? -val : val, radix,
                                          /* lowerCase = */ true);
  }

  // Plus one to include the sign.
  constexpr size_t maxLength = std::numeric_limits<Digit>::digits + 1;
  static_assert(maxLength == 33 || maxLength == 65,
                "unexpected single digit string length");

  char resultChars[maxLength];

  char* chars = resultChars;
  if (isNegative) {
    *chars++ = '-';
  }

  auto result = std::to_chars(chars, std::end(resultChars), digit, radix);
  MOZ_ASSERT(result.ec == std::errc());

  size_t length = result.ptr - resultChars;
  MOZ_ASSERT(length <= maxLength);

  return NewStringCopyN<allowGC>(cx, resultChars, length);
}

static constexpr BigInt::Digit MaxPowerInDigit(uint8_t radix) {
  BigInt::Digit result = 1;
  while (result < BigInt::Digit(-1) / radix) {
    result *= radix;
  }
  return result;
}

static constexpr uint8_t MaxExponentInDigit(uint8_t radix) {
  uint8_t exp = 0;
  BigInt::Digit result = 1;
  while (result < BigInt::Digit(-1) / radix) {
    result *= radix;
    exp += 1;
  }
  return exp;
}

struct RadixInfo {
  BigInt::Digit maxPowerInDigit;
  uint8_t maxExponentInDigit;

  constexpr RadixInfo(BigInt::Digit maxPower, uint8_t maxExponent)
      : maxPowerInDigit(maxPower), maxExponentInDigit(maxExponent) {}

  explicit constexpr RadixInfo(uint8_t radix)
      : RadixInfo(MaxPowerInDigit(radix), MaxExponentInDigit(radix)) {}
};

static constexpr const RadixInfo toStringInfo[37] = {
    {0, 0},        {0, 0},        RadixInfo(2),  RadixInfo(3),  RadixInfo(4),
    RadixInfo(5),  RadixInfo(6),  RadixInfo(7),  RadixInfo(8),  RadixInfo(9),
    RadixInfo(10), RadixInfo(11), RadixInfo(12), RadixInfo(13), RadixInfo(14),
    RadixInfo(15), RadixInfo(16), RadixInfo(17), RadixInfo(18), RadixInfo(19),
    RadixInfo(20), RadixInfo(21), RadixInfo(22), RadixInfo(23), RadixInfo(24),
    RadixInfo(25), RadixInfo(26), RadixInfo(27), RadixInfo(28), RadixInfo(29),
    RadixInfo(30), RadixInfo(31), RadixInfo(32), RadixInfo(33), RadixInfo(34),
    RadixInfo(35), RadixInfo(36),
};

JSLinearString* BigInt::toStringGeneric(JSContext* cx, HandleBigInt x,
                                        unsigned radix) {
  MOZ_ASSERT(radix >= 2 && radix <= 36);
  MOZ_ASSERT(!x->isZero());
  MOZ_ASSERT(x->digitLength() > 1);
  MOZ_ASSERT(!std::has_single_bit(radix));

  size_t maximumCharactersRequired =
      calculateMaximumCharactersRequired(x, radix);
  if (maximumCharactersRequired > JSString::MAX_LENGTH) {
    ReportAllocationOverflow(cx);
    return nullptr;
  }

  auto resultString = cx->make_pod_array<char>(maximumCharactersRequired);
  if (!resultString) {
    return nullptr;
  }

  size_t writePos = maximumCharactersRequired;

  unsigned chunkChars = toStringInfo[radix].maxExponentInDigit;
  Digit chunkDivisor = toStringInfo[radix].maxPowerInDigit;

  unsigned length = x->digitLength();
  unsigned nonZeroDigit = length - 1;
  MOZ_ASSERT(x->digits()[nonZeroDigit] != 0);

  // `rest` holds the part of the BigInt that we haven't looked at yet.
  // Not to be confused with "remainder"!
  RootedBigInt rest(cx);

  // In the first round, divide the input, allocating a new BigInt for
  // the result == rest; from then on divide the rest in-place.
  //
  // FIXME: absoluteDivWithDigitDivisor doesn't
  // destructivelyTrimHighZeroDigits for in-place divisions, leading to
  // worse constant factors.  See
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1510213.
  RootedBigInt dividend(cx, x);
  do {
    Digit chunk;
    if (!absoluteDivWithDigitDivisor(cx, dividend, chunkDivisor, Some(&rest),
                                     &chunk, dividend->isNegative())) {
      return nullptr;
    }

    dividend = rest;
    for (unsigned i = 0; i < chunkChars; i++) {
      MOZ_ASSERT(writePos > 0);
      resultString[--writePos] = radixDigits[chunk % radix];
      chunk /= radix;
    }
    MOZ_ASSERT(!chunk);

    if (!rest->individualDigit(nonZeroDigit)) {
      nonZeroDigit--;
    }

    MOZ_ASSERT(rest->individualDigit(nonZeroDigit) != 0,
               "division by a single digit can't remove more than one "
               "digit from a number");
  } while (nonZeroDigit > 0);

  Digit lastDigit = rest->individualDigit(0);
  do {
    MOZ_ASSERT(writePos > 0);
    resultString[--writePos] = radixDigits[lastDigit % radix];
    lastDigit /= radix;
  } while (lastDigit > 0);
  MOZ_ASSERT(writePos < maximumCharactersRequired);
  MOZ_ASSERT(maximumCharactersRequired - writePos <=
             static_cast<size_t>(maximumCharactersRequired));

  // Remove leading zeroes.
  while (writePos + 1 < maximumCharactersRequired &&
         resultString[writePos] == '0') {
    writePos++;
  }

  if (x->isNegative()) {
    MOZ_ASSERT(writePos > 0);
    resultString[--writePos] = '-';
  }

  MOZ_ASSERT(writePos < maximumCharactersRequired);
  // Would be better to somehow adopt resultString directly.
  return NewStringCopyN<CanGC>(cx, resultString.get() + writePos,
                               maximumCharactersRequired - writePos);
}

BigInt* BigInt::destructivelyTrimHighZeroDigits(JSContext* cx, BigInt* x) {
  if (x->isZero()) {
    MOZ_ASSERT(!x->isNegative());
    return x;
  }
  MOZ_ASSERT(x->digitLength());

  auto xDigits = x->unguardedDigits();
  int nonZeroIndex = xDigits.size() - 1;
  while (nonZeroIndex >= 0 && xDigits[nonZeroIndex] == 0) {
    nonZeroIndex--;
  }

  if (nonZeroIndex < 0) {
    return zero(cx);
  }

  if (nonZeroIndex == static_cast<int>(x->digitLength() - 1)) {
    return x;
  }

  unsigned newLength = nonZeroIndex + 1;

  if (newLength > InlineDigitsLength) {
    MOZ_ASSERT(x->hasHeapDigits());

    size_t oldLength = x->digitLength();
    Digit* newdigits = ReallocateCellBuffer<Digit>(cx, x, x->heapDigits_,
                                                   oldLength, newLength);
    if (!newdigits) {
      return nullptr;
    }
    x->heapDigits_ = newdigits;
  } else {
    if (x->hasHeapDigits()) {
      Digit digits[InlineDigitsLength];
      std::copy_n(x->heapDigits_, InlineDigitsLength, digits);
      if (!cx->nursery().isInside(x->heapDigits_)) {
        FreeBuffer(x->zone(), x->heapDigits_);
      }
      std::copy_n(digits, InlineDigitsLength, x->inlineDigits_);
    }
  }

  x->setLengthAndFlags(newLength, x->isNegative() ? SignBit : 0);

  return x;
}

// The maximum value `radix**charCount - 1` must be represented as a max number
// `2**(N * DigitBits) - 1` for `N` digits, so
//
//   2**(N * DigitBits) - 1 ≥ radix**charcount - 1
//   2**(N * DigitBits) ≥ radix**charcount
//   N * DigitBits ≥ log2(radix**charcount)
//   N * DigitBits ≥ charcount * log2(radix)
//   N ≥ ⌈charcount * log2(radix) / DigitBits⌉ (conservatively)
//
// or in the code's terms (all numbers promoted to exact mathematical values),
//
//   N ≥ ⌈charcount * bitsPerChar / (DigitBits * bitsPerCharTableMultiplier)⌉
//
// Note that `N` is computed even more conservatively here because `bitsPerChar`
// is rounded up.
bool BigInt::calculateMaximumDigitsRequired(JSContext* cx, uint8_t radix,
                                            size_t charcount, size_t* result) {
  MOZ_ASSERT(2 <= radix && radix <= 36);

  uint8_t bitsPerChar = maxBitsPerCharTable[radix];

  MOZ_ASSERT(charcount > 0);
  MOZ_ASSERT(charcount <= std::numeric_limits<uint64_t>::max() / bitsPerChar);
  static_assert(
      MaxDigitLength < std::numeric_limits<size_t>::max(),
      "can't safely cast calculateMaximumDigitsRequired result to size_t");

  uint64_t n = CeilDiv(static_cast<uint64_t>(charcount) * bitsPerChar,
                       DigitBits * bitsPerCharTableMultiplier);
  if (n > MaxDigitLength) {
    ReportOversizedAllocation(cx, JSMSG_BIGINT_TOO_LARGE);
    return false;
  }

  *result = n;
  return true;
}

static BigInt::Digit PowerOf(BigInt::Digit base, size_t n) {
  MOZ_ASSERT(n > 0);
  mozilla::CheckedInt<BigInt::Digit> result = base;
  for (size_t i = 1; i < n; i++) {
    result *= base;
  }
  MOZ_ASSERT(result.isValid(), "unexpected overflow");
  return result.value();
}

template <typename CharT>
static bool ParseLiteralDigit(Range<const CharT> chars, unsigned radix,
                              BigInt::Digit* result) {
  MOZ_ASSERT(chars.length() > 0);
  RangedPtr<const CharT> start = chars.begin();
  RangedPtr<const CharT> end = chars.end();

  mozilla::CheckedInt<BigInt::Digit> d = 0;
  for (; start < end; start++) {
    uint32_t digit;
    CharT c = *start;
    if (c >= '0' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'z') {
      digit = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'Z') {
      digit = c - 'A' + 10;
    } else {
      return false;
    }
    if (MOZ_UNLIKELY(digit >= radix)) {
      return false;
    }

    d = d * radix + digit;
  }
  MOZ_ASSERT(d.isValid(), "unexpected overflow");

  *result = d.value();
  return true;
}

template <typename CharT>
BigInt* BigInt::parseLiteralDigits(JSContext* cx, Range<const CharT> chars,
                                   unsigned radix, bool isNegative,
                                   bool* haveParseError, gc::Heap heap) {
  static_assert(
      std::is_same_v<CharT, JS::Latin1Char> || std::is_same_v<CharT, char16_t>,
      "only the bare minimum character types are supported, to avoid "
      "excessively instantiating this template");

  MOZ_ASSERT(chars.length());
  MOZ_ASSERT(2 <= radix && radix <= 36);

  RangedPtr<const CharT> start = chars.begin();
  RangedPtr<const CharT> end = chars.end();

  // Skipping leading zeroes.
  while (start[0] == '0') {
    start++;
    if (start == end) {
      return zero(cx, heap);
    }
  }

  size_t length;
  if (!calculateMaximumDigitsRequired(cx, radix, end - start, &length)) {
    return nullptr;
  }
  MOZ_ASSERT(length > 0);

  // Fast path for the single digit case.
  if (length == 1) {
    BigInt::Digit digit = 0;
    if (!ParseLiteralDigit(Range{start, end}, radix, &digit)) {
      *haveParseError = true;
      return nullptr;
    }
    MOZ_ASSERT(digit > 0);
    return BigInt::createFromDigit(cx, digit, isNegative, heap);
  }

  BigInt* result = createUninitialized(cx, length, isNegative, heap);
  if (!result) {
    return nullptr;
  }

  result->initializeDigitsToZero();

  // Numbers in radix 2, 4, and 16 can be directly stored into the result when
  // parsing from right to left.
  if (radix == 2 || radix == 4 || radix == 16) {
    uint8_t log2 = mozilla::FloorLog2(radix);
    size_t chunkChars = BigInt::DigitBits >> mozilla::FloorLog2(log2);

    size_t i = 0;
    RangedPtr<const CharT> to = end;
    while (to > start) {
      RangedPtr<const CharT> from = to - std::min(to - start, chunkChars);

      Digit chunk = 0;
      if (!ParseLiteralDigit(Range{from, to}, radix, &chunk)) {
        *haveParseError = true;
        return nullptr;
      }

      result->digits()[i++] = chunk;
      to = from;
    }
    MOZ_ASSERT(i == length, "unexpected over allocation");
    MOZ_ASSERT(result->digits()[length - 1] > 0, "unexpected leading zero");

    return result;
  }

  size_t chunkChars = toStringInfo[radix].maxExponentInDigit;
  Digit chunkMultiplier = toStringInfo[radix].maxPowerInDigit;

  while (start < end) {
    RangedPtr<const CharT> limit = start + std::min(end - start, chunkChars);

    Digit chunk = 0;
    if (!ParseLiteralDigit(Range{start, limit}, radix, &chunk)) {
      *haveParseError = true;
      return nullptr;
    }

    BigInt::Digit multiplier = chunkMultiplier;
    if ((limit - start) < chunkChars) {
      multiplier = PowerOf(radix, limit - start);
      MOZ_ASSERT(multiplier < chunkMultiplier);
    }
    result->inplaceMultiplyAdd(multiplier, chunk);
    start = limit;
  }

  return destructivelyTrimHighZeroDigits(cx, result);
}

// BigInt proposal section 7.2
template <typename CharT>
BigInt* BigInt::parseLiteral(JSContext* cx, Range<const CharT> chars,
                             bool* haveParseError, js::gc::Heap heap) {
  RangedPtr<const CharT> start = chars.begin();
  const RangedPtr<const CharT> end = chars.end();
  bool isNegative = false;

  MOZ_ASSERT(chars.length());

  if (end - start > 2 && start[0] == '0') {
    if (start[1] == 'b' || start[1] == 'B') {
      // StringNumericLiteral ::: BinaryIntegerLiteral
      return parseLiteralDigits(cx, Range<const CharT>(start + 2, end), 2,
                                isNegative, haveParseError, heap);
    }
    if (start[1] == 'x' || start[1] == 'X') {
      // StringNumericLiteral ::: HexIntegerLiteral
      return parseLiteralDigits(cx, Range<const CharT>(start + 2, end), 16,
                                isNegative, haveParseError, heap);
    }
    if (start[1] == 'o' || start[1] == 'O') {
      // StringNumericLiteral ::: OctalIntegerLiteral
      return parseLiteralDigits(cx, Range<const CharT>(start + 2, end), 8,
                                isNegative, haveParseError, heap);
    }
  }

  return parseLiteralDigits(cx, Range<const CharT>(start, end), 10, isNegative,
                            haveParseError, heap);
}

BigInt* BigInt::createFromDouble(JSContext* cx, double d) {
  MOZ_ASSERT(IsInteger(d), "Only integer-valued doubles can convert to BigInt");

  if (d == 0) {
    return zero(cx);
  }

  int exponent = mozilla::ExponentComponent(d);
  MOZ_ASSERT(exponent >= 0);
  int length = exponent / DigitBits + 1;
  BigInt* result = createUninitialized(cx, length, d < 0);
  if (!result) {
    return nullptr;
  }

  // We construct a BigInt from the double `d` by shifting its mantissa
  // according to its exponent and mapping the bit pattern onto digits.
  //
  //               <----------- bitlength = exponent + 1 ----------->
  //                <----- 52 ------> <------ trailing zeroes ------>
  // mantissa:     1yyyyyyyyyyyyyyyyy 0000000000000000000000000000000
  // digits:    0001xxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
  //                <-->          <------>
  //          msdTopBits          DigitBits
  //
  using Double = mozilla::FloatingPoint<double>;
  uint64_t mantissa =
      mozilla::BitwiseCast<uint64_t>(d) & Double::kSignificandBits;
  // Add implicit high bit.
  mantissa |= 1ull << Double::kSignificandWidth;

  const int mantissaTopBit = Double::kSignificandWidth;  // 0-indexed.

  // 0-indexed position of `d`'s most significant bit within the `msd`.
  int msdTopBit = exponent % DigitBits;

  // Next digit under construction.
  Digit digit;

  // First, build the MSD by shifting the mantissa appropriately.
  if (msdTopBit < mantissaTopBit) {
    int remainingMantissaBits = mantissaTopBit - msdTopBit;
    digit = mantissa >> remainingMantissaBits;
    mantissa = mantissa << (64 - remainingMantissaBits);
  } else {
    MOZ_ASSERT(msdTopBit >= mantissaTopBit);
    digit = mantissa << (msdTopBit - mantissaTopBit);
    mantissa = 0;
  }
  MOZ_ASSERT(digit != 0, "most significant digit should not be zero");
  auto resultDigits = result->digits();
  resultDigits[--length] = digit;

  // Fill in digits containing mantissa contributions.
  while (mantissa) {
    MOZ_ASSERT(length > 0,
               "double bits were all non-fractional, so there must be "
               "digits present to hold them");

    if (DigitBits == 64) {
      resultDigits[--length] = mantissa;
      break;
    }

    MOZ_ASSERT(DigitBits == 32);
    Digit current = mantissa >> 32;
    mantissa = mantissa << 32;
    resultDigits[--length] = current;
  }

  // Fill in low-order zeroes.
  for (int i = length - 1; i >= 0; i--) {
    resultDigits[i] = 0;
  }

  return result;
}

BigInt* BigInt::createFromUint64(JSContext* cx, uint64_t n, gc::Heap heap) {
  if (n == 0) {
    return zero(cx, heap);
  }

  const bool isNegative = false;

  if (DigitBits == 32) {
    Digit low = n;
    Digit high = n >> 32;
    size_t length = high ? 2 : 1;

    BigInt* res = createUninitialized(cx, length, isNegative, heap);
    if (!res) {
      return nullptr;
    }
    res->setIndividualDigit(0, low);
    if (high) {
      res->setIndividualDigit(1, high);
    }
    return res;
  }

  return createFromDigit(cx, n, isNegative, heap);
}

BigInt* BigInt::createFromInt64(JSContext* cx, int64_t n, gc::Heap heap) {
  BigInt* res = createFromUint64(cx, Abs(n), heap);
  if (!res) {
    return nullptr;
  }

  if (n < 0) {
    res->setHeaderFlagBit(SignBit);
  }
  MOZ_ASSERT(res->isNegative() == (n < 0));

  return res;
}

BigInt* BigInt::createFromIntPtr(JSContext* cx, intptr_t n) {
  static_assert(sizeof(intptr_t) == sizeof(BigInt::Digit));

  if (n == 0) {
    return BigInt::zero(cx);
  }
  return BigInt::createFromDigit(cx, BigInt::Digit(Abs(n)), n < 0);
}

// BigInt proposal section 5.1.2
BigInt* js::NumberToBigInt(JSContext* cx, double d) {
  // Step 1 is an assertion checked by the caller.
  // Step 2.
  if (!IsInteger(d)) {
    ToCStringBuf cbuf;
    const char* str = NumberToCString(&cbuf, d);
    MOZ_ASSERT(str);

    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_NONINTEGER_NUMBER_TO_BIGINT, str);
    return nullptr;
  }

  // Step 3.
  return BigInt::createFromDouble(cx, d);
}

BigInt* BigInt::copy(JSContext* cx, HandleBigInt x, gc::Heap heap) {
  if (x->isZero()) {
    return zero(cx, heap);
  }

  BigInt* result =
      createUninitialized(cx, x->digitLength(), x->isNegative(), heap);
  if (!result) {
    return nullptr;
  }
  auto xDigits = x->digits();
  auto resultDigits = result->digits();
  for (size_t i = 0; i < xDigits.size(); i++) {
    resultDigits[i] = xDigits[i];
  }
  return result;
}

// BigInt proposal section 1.1.7
BigInt* BigInt::add(JSContext* cx, HandleBigInt x, HandleBigInt y) {
  bool xNegative = x->isNegative();

  // x + y == x + y
  // -x + -y == -(x + y)
  if (xNegative == y->isNegative()) {
    return absoluteAdd(cx, x, y, xNegative);
  }

  // x + -y == x - y == -(y - x)
  // -x + y == y - x == -(x - y)
  int8_t compare = absoluteCompare(x, y);
  if (compare == 0) {
    return zero(cx);
  }

  if (compare > 0) {
    return absoluteSub(cx, x, y, xNegative);
  }

  return absoluteSub(cx, y, x, !xNegative);
}

// BigInt proposal section 1.1.8
BigInt* BigInt::sub(JSContext* cx, HandleBigInt x, HandleBigInt y) {
  bool xNegative = x->isNegative();
  if (xNegative != y->isNegative()) {
    // x - (-y) == x + y
    // (-x) - y == -(x + y)
    return absoluteAdd(cx, x, y, xNegative);
  }

  // x - y == -(y - x)
  // (-x) - (-y) == y - x == -(x - y)
  int8_t compare = absoluteCompare(x, y);
  if (compare == 0) {
    return zero(cx);
  }

  if (compare > 0) {
    return absoluteSub(cx, x, y, xNegative);
  }

  return absoluteSub(cx, y, x, !xNegative);
}

// BigInt proposal section 1.1.4
BigInt* BigInt::mul(JSContext* cx, HandleBigInt x, HandleBigInt y) {
  if (x->isZero()) {
    return x;
  }
  if (y->isZero()) {
    return y;
  }

  bool resultNegative = x->isNegative() != y->isNegative();

  // Fast path for the likely-common case of up to a uint64_t of magnitude.
  if (x->absFitsInUint64() && y->absFitsInUint64()) {
    uint64_t lhs = x->uint64FromAbsNonZero();
    uint64_t rhs = y->uint64FromAbsNonZero();

    uint64_t res;
    if (mozilla::SafeMul(lhs, rhs, &res)) {
      MOZ_ASSERT(res != 0);
      return createFromNonZeroRawUint64(cx, res, resultNegative);
    }
  }

  size_t resultLength = x->digitLength() + y->digitLength();
  BigInt* result = createUninitialized(cx, resultLength, resultNegative);
  if (!result) {
    return nullptr;
  }
  schoolbookMultiplyDigits(x->unguardedDigits(), y->unguardedDigits(),
                           result->unguardedDigits());
  return destructivelyTrimHighZeroDigits(cx, result);
}

// BigInt proposal section 1.1.5
BigInt* BigInt::div(JSContext* cx, HandleBigInt x, HandleBigInt y) {
  // 1. If y is 0n, throw a RangeError exception.
  if (y->isZero()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_BIGINT_DIVISION_BY_ZERO);
    return nullptr;
  }

  // 2. Let quotient be the mathematical value of x divided by y.
  // 3. Return a BigInt representing quotient rounded towards 0 to the next
  //    integral value.
  if (x->isZero()) {
    return x;
  }

  if (absoluteCompare(x, y) < 0) {
    return zero(cx);
  }

  RootedBigInt quotient(cx);
  bool resultNegative = x->isNegative() != y->isNegative();
  if (y->digitLength() == 1) {
    Digit divisor = y->individualDigit(0);
    if (divisor == 1) {
      return resultNegative == x->isNegative() ? x : neg(cx, x);
    }

    Digit remainder;
    if (!absoluteDivWithDigitDivisor(cx, x, divisor, Some(&quotient),
                                     &remainder, resultNegative)) {
      return nullptr;
    }
  } else {
    if (!absoluteDivWithBigIntDivisor(cx, x, y, Some(&quotient), Nothing(),
                                      resultNegative)) {
      return nullptr;
    }
  }

  return destructivelyTrimHighZeroDigits(cx, quotient);
}

// BigInt proposal section 1.1.6
BigInt* BigInt::mod(JSContext* cx, HandleBigInt x, HandleBigInt y) {
  // 1. If y is 0n, throw a RangeError exception.
  if (y->isZero()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_BIGINT_DIVISION_BY_ZERO);
    return nullptr;
  }

  // 2. If x is 0n, return x.
  if (x->isZero()) {
    return x;
  }
  // 3. Let r be the BigInt defined by the mathematical relation r = x - (y ×
  // q) where q is a BigInt that is negative only if x/y is negative and
  // positive only if x/y is positive, and whose magnitude is as large as
  // possible without exceeding the magnitude of the true mathematical
  // quotient of x and y.
  if (absoluteCompare(x, y) < 0) {
    return x;
  }

  if (y->digitLength() == 1) {
    Digit divisor = y->individualDigit(0);
    if (divisor == 1) {
      return zero(cx);
    }

    Digit remainderDigit;
    bool unusedQuotientNegative = false;
    if (!absoluteDivWithDigitDivisor(cx, x, divisor, Nothing(), &remainderDigit,
                                     unusedQuotientNegative)) {
      MOZ_CRASH("BigInt div by digit failed unexpectedly");
    }

    if (!remainderDigit) {
      return zero(cx);
    }

    return createFromDigit(cx, remainderDigit, x->isNegative());
  } else {
    uint32_t k;
    Digit c;
    if (shouldModuloByCrandallReduction(y, &k, &c)) {
      RootedBigInt result(cx, modCrandall(cx, x, k, c));
#ifdef DEBUG
      if (result) {
        RootedBigInt expected(cx);
        if (absoluteDivWithBigIntDivisor(cx, x, y, Nothing(), Some(&expected),
                                         x->isNegative()) &&
            (expected = destructivelyTrimHighZeroDigits(cx, expected))) {
          MOZ_ASSERT(equal(result, expected));
        } else {
          // Could be OOM, or it could be that schoolbook needed more scratch
          // space than crandall, which pushed it over the max bigint size.
          // Clear the exn generically instead of recoverFromOutOfMemory.
          cx->clearPendingException();
        }
      }
#endif
      return result;
    }

    RootedBigInt remainder(cx);
    if (!absoluteDivWithBigIntDivisor(cx, x, y, Nothing(), Some(&remainder),
                                      x->isNegative())) {
      return nullptr;
    }
    MOZ_ASSERT(remainder);
    return destructivelyTrimHighZeroDigits(cx, remainder);
  }
}

bool BigInt::shouldModuloByCrandallReduction(const BigInt* m, uint32_t* k,
                                             Digit* c) {
  // `m` is `2^k - c` for a single-digit `c` if all of its bits above the low
  // digit are set and the low digit is non-zero.
  auto mDigits = m->digits();
  size_t n = mDigits.size();
  if (n < 2) {
    return false;
  }

  // High digit is all ones followed by zeroes in the high bits.
  Digit top = mDigits[n - 1];
  if (top & (top + 1)) {
    return false;
  }

  // Middle digits are all ones.
  for (size_t i = 1; i < n - 1; i++) {
    if (mDigits[i] != Digit(-1)) {
      return false;
    }
  }

  // Low digit is non-zero.
  if (!mDigits[0]) {
    return false;
  }

  unsigned topBits = DigitBits - std::countl_zero(top);
  Digit cValue = -mDigits[0];
  uint32_t kValue = (n - 1) * DigitBits + topBits;

  // Crandall reduction is always possible if the preceding conditions hold.
  // But for some one- and two-digit divisors, it's not profitable.
  unsigned cBits = DigitBits - std::countl_zero(cValue);
  if (2 * cBits > kValue) {
    return false;
  }

  *k = kValue;
  *c = cValue;
  return true;
}

// See "Handbook of Applied Cryptography" by A. Menezes, P. van Oorschot and
// S. Vanstone, §14.3.4. `r` is an in-out parameter: it starts as the dividend
// and is incrementally mutated into the modulo.
void BigInt::modDigitsInPlaceByCrandallReduction(Digits r, uint32_t k,
                                                 Digit c) {
  // For single-digit `c`, the divisor has three parts: a bottom digit whose
  // value is `-c`, a string of all-ones middle digits, and a high digit with a
  // string of `kHighDigitBits` ones. The bottom and middle digits are together
  // the low digits.
  const size_t kLowDigitCount = k / DigitBits;
  const unsigned kHighDigitBits = k % DigitBits;
  const Digit kHighDigitMask = (Digit(1) << kHighDigitBits) - 1;
  MOZ_RELEASE_ASSERT(kLowDigitCount > 0);
  // If r is smaller, it is already reduced, and the caller should just return
  // it without copying.
  MOZ_RELEASE_ASSERT(r.size() >= kLowDigitCount);

  // Consider that `(x + y) mod z = ((x mod z) + y) mod z`. This congruence
  // holds also for multiplication. We can replace any part of the dividend with
  // a smaller value that is congruent to it modulo `2^k - c`, without changing
  // the result.
  //
  // Consider also that `2^k mod (2^k - c) = c`. Therefore, if the bits of a
  // digit at or above bit `k` have value `d` and start at bit `k + s`, they
  // contribute `d * 2^(k + s)` to the dividend, which is congruent to `d * c *
  // 2^s`. We clear those bits and add `d * c` at bit `s` instead.
  //
  // We do this from the top digit down. Each fold decreases the dividend, so it
  // never carries above the digit being cleared, but it may carry into that
  // digit, which is then folded again. Once all bits at or above bit `k` are
  // clear, the dividend is less than `2^k`, so at most one subtraction of `2^k
  // - c` remains.
  for (size_t j = r.size(); j-- > kLowDigitCount;) {
    const unsigned shift = j == kLowDigitCount ? kHighDigitBits : 0;
    while (Digit d = r[j] >> shift) {
      r[j] &= (Digit(1) << shift) - 1;
      const size_t s = j * DigitBits + shift - k;
      const unsigned sb = s % DigitBits;
      // `d * c`...
      Digit high;
      Digit low = digitMul(d, c, &high);
      // ...at bit `s`.
      Digit t0 = low << sb;
      Digit t1 = sb ? (high << sb) | (low >> (DigitBits - sb)) : high;
      Digit t2 = sb ? high >> (DigitBits - sb) : 0;
      size_t i = s / DigitBits;
      // Finally, add `d * c * 2^s` back into dividend.
      Digit carry;
      r[i] = digitAdd(r[i], t0, &carry);
      i++;
      r[i] = digitAdd3(r[i], t1, carry, &carry);
      i++;
      if (i < r.size()) {
        r[i] = digitAdd3(r[i], t2, carry, &carry);
        for (i++; carry; i++) {
          r[i] = digitAdd(r[i], carry, &carry);
        }
      } else {
        MOZ_ASSERT(!t2 && !carry);
      }
    }
  }

  // Now `r < 2^k`, so at most one subtraction of `2^k - c` is needed, which is
  // the case if `r + c >= 2^k`.
  Digit carry = c;
  for (size_t i = 0; carry && i < kLowDigitCount; i++) {
    r[i] = digitAdd(r[i], carry, &carry);
  }
  if (kLowDigitCount < r.size()) {
    Digit top = r[kLowDigitCount] + carry;
    r[kLowDigitCount] = top & kHighDigitMask;
    carry = top >> kHighDigitBits;
  }
  if (carry) {
    return;
  }
  Digit borrow = c;
  for (size_t i = 0; borrow; i++) {
    r[i] = digitSub(r[i], borrow, &borrow);
  }
}

BigInt* BigInt::modCrandall(JSContext* cx, HandleBigInt x, uint32_t k,
                            Digit c) {
  if (x->digitLength() * DigitBits < k) {
    return x;
  }
  RootedBigInt r(cx, copy(cx, x));
  if (!r) {
    return nullptr;
  }
  modDigitsInPlaceByCrandallReduction(r->unguardedDigits(), k, c);
  return destructivelyTrimHighZeroDigits(cx, r);
}

bool BigInt::divmod(JSContext* cx, Handle<BigInt*> x, Handle<BigInt*> y,
                    MutableHandle<BigInt*> quotient,
                    MutableHandle<BigInt*> remainder) {
  // 1. If y is 0n, throw a RangeError exception.
  if (y->isZero()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_BIGINT_DIVISION_BY_ZERO);
    return false;
  }

  // 2. If x is 0n, quotient and remainder are zero, too.
  if (x->isZero()) {
    quotient.set(x);
    remainder.set(x);
    return true;
  }

  // 3. Let r be the BigInt defined by the mathematical relation r = x - (y ×
  // q) where q is a BigInt that is negative only if x/y is negative and
  // positive only if x/y is positive, and whose magnitude is as large as
  // possible without exceeding the magnitude of the true mathematical
  // quotient of x and y.
  if (absoluteCompare(x, y) < 0) {
    auto* zero = BigInt::zero(cx);
    if (!zero) {
      return false;
    }

    quotient.set(zero);
    remainder.set(x);
    return true;
  }

  bool resultNegative = x->isNegative() != y->isNegative();

  if (y->digitLength() == 1) {
    Digit divisor = y->individualDigit(0);
    if (divisor == 1) {
      quotient.set(resultNegative == x->isNegative() ? x : neg(cx, x));
      if (!quotient) {
        return false;
      }

      remainder.set(BigInt::zero(cx));
      if (!remainder) {
        return false;
      }
    } else {
      Rooted<BigInt*> quot(cx);
      Digit remainderDigit;
      if (!absoluteDivWithDigitDivisor(cx, x, divisor, Some(&quot),
                                       &remainderDigit, resultNegative)) {
        return false;
      }

      quotient.set(destructivelyTrimHighZeroDigits(cx, quot));
      if (!quotient) {
        return false;
      }

      if (!remainderDigit) {
        remainder.set(zero(cx));
      } else {
        remainder.set(createFromDigit(cx, remainderDigit, x->isNegative()));
      }
      if (!remainder) {
        return false;
      }
    }
  } else {
    RootedBigInt quot(cx);
    RootedBigInt rem(cx);
    if (!absoluteDivWithBigIntDivisor(cx, x, y, Some(&quot), Some(&rem),
                                      resultNegative)) {
      return false;
    }

    quotient.set(destructivelyTrimHighZeroDigits(cx, quot));
    if (!quotient) {
      return false;
    }

    remainder.set(destructivelyTrimHighZeroDigits(cx, rem));
    if (!remainder) {
      return false;
    }
  }

  MOZ_ASSERT(quotient && remainder,
             "quotient and remainder are computed on return");
  MOZ_ASSERT(!quotient->isZero(), "zero quotient is handled earlier");
  MOZ_ASSERT(quotient->isNegative() == resultNegative,
             "quotient has the correct sign");
  MOZ_ASSERT(
      remainder->isZero() || (x->isNegative() == remainder->isNegative()),
      "remainder has the correct sign");

  return true;
}

// BigInt proposal section 1.1.3
BigInt* BigInt::pow(JSContext* cx, HandleBigInt x, HandleBigInt y) {
  // 1. If exponent is < 0, throw a RangeError exception.
  if (y->isNegative()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_BIGINT_NEGATIVE_EXPONENT);
    return nullptr;
  }

  // 2. If base is 0n and exponent is 0n, return 1n.
  if (y->isZero()) {
    return one(cx);
  }

  if (x->isZero()) {
    return x;
  }

  // 3. Return a BigInt representing the mathematical value of base raised
  //    to the power exponent.
  if (x->digitLength() == 1 && x->individualDigit(0) == 1) {
    // (-1) ** even_number == 1.
    if (x->isNegative() && (y->individualDigit(0) & 1) == 0) {
      return neg(cx, x);
    }
    // (-1) ** odd_number == -1; 1 ** anything == 1.
    return x;
  }

  // For all bases >= 2, very large exponents would lead to unrepresentable
  // results.
  static_assert(MaxBitLength < std::numeric_limits<Digit>::max(),
                "unexpectedly large MaxBitLength");
  if (y->digitLength() > 1) {
    ReportOversizedAllocation(cx, JSMSG_BIGINT_TOO_LARGE);
    return nullptr;
  }
  Digit exponent = y->individualDigit(0);
  if (exponent == 1) {
    return x;
  }
  if (exponent >= MaxBitLength) {
    ReportOversizedAllocation(cx, JSMSG_BIGINT_TOO_LARGE);
    return nullptr;
  }

  static_assert(
      MaxBitLength <= static_cast<unsigned>(std::numeric_limits<int>::max()),
      "unexpectedly large MaxBitLength");
  int n = static_cast<int>(exponent);
  bool isOddPower = n & 1;

  if (x->digitLength() == 1 && std::has_single_bit(x->individualDigit(0))) {
    // Fast path for (2^m)^n.

    // Result is negative for odd powers.
    bool resultNegative = x->isNegative() && isOddPower;

    unsigned m = mozilla::FloorLog2(x->individualDigit(0));
    MOZ_ASSERT(m < DigitBits);

    static_assert(MaxBitLength * DigitBits > MaxBitLength,
                  "n * m can't overflow");
    n *= int(m);

    int length = 1 + (n / DigitBits);
    BigInt* result = createUninitialized(cx, length, resultNegative);
    if (!result) {
      return nullptr;
    }
    result->initializeDigitsToZero();
    result->setIndividualDigit(length - 1, static_cast<Digit>(1)
                                               << (n % DigitBits));
    return result;
  }

  RootedBigInt runningSquare(cx, x);
  RootedBigInt result(cx, isOddPower ? x : nullptr);
  n /= 2;

  // Fast path for the likely-common case of up to a uint64_t of magnitude.
  if (x->absFitsInUint64()) {
    bool resultNegative = x->isNegative() && isOddPower;

    uint64_t runningSquareInt = x->uint64FromAbsNonZero();
    uint64_t resultInt = isOddPower ? runningSquareInt : 1;
    while (true) {
      uint64_t runningSquareStart = runningSquareInt;
      uint64_t r;
      if (!mozilla::SafeMul(runningSquareInt, runningSquareInt, &r)) {
        break;
      }
      runningSquareInt = r;

      if (n & 1) {
        if (!mozilla::SafeMul(resultInt, runningSquareInt, &r)) {
          // Recover |runningSquare| before we restart the loop.
          runningSquareInt = runningSquareStart;
          break;
        }
        resultInt = r;
      }

      n /= 2;
      if (n == 0) {
        return createFromNonZeroRawUint64(cx, resultInt, resultNegative);
      }
    }

    runningSquare = createFromNonZeroRawUint64(cx, runningSquareInt, false);
    if (!runningSquare) {
      return nullptr;
    }

    result = createFromNonZeroRawUint64(cx, resultInt, resultNegative);
    if (!result) {
      return nullptr;
    }
  }

  // This implicitly sets the result's sign correctly.
  while (true) {
    runningSquare = mul(cx, runningSquare, runningSquare);
    if (!runningSquare) {
      return nullptr;
    }

    if (n & 1) {
      if (!result) {
        result = runningSquare;
      } else {
        result = mul(cx, result, runningSquare);
        if (!result) {
          return nullptr;
        }
      }
    }

    n /= 2;
    if (n == 0) {
      return result;
    }
  }
}

bool BigInt::powIntPtr(intptr_t x, intptr_t y, intptr_t* result) {
  if (y < 0) {
    return false;
  }
  uintptr_t n = uintptr_t(y);

  // x^y where x == 1 returns 1 for any y.
  if (x == 1) {
    *result = 1;
    return true;
  }

  // x^y where x == -1 returns 1 for even y, and -1 for odd y.
  if (x == -1) {
    *result = (y & 1) ? -1 : 1;
    return true;
  }

  using CheckedIntPtr = mozilla::CheckedInt<intptr_t>;

  CheckedIntPtr runningSquare = x;
  CheckedIntPtr res = 1;
  while (true) {
    if ((n & 1) != 0) {
      res *= runningSquare;
      if (!res.isValid()) {
        return false;
      }
    }

    n >>= 1;
    if (n == 0) {
      *result = res.value();
      return true;
    }

    runningSquare *= runningSquare;
    if (!runningSquare.isValid()) {
      return false;
    }
  }
}

BigInt* BigInt::lshByAbsolute(JSContext* cx, HandleBigInt x, HandleBigInt y) {
  if (x->isZero() || y->isZero()) {
    return x;
  }

  if (y->digitLength() > 1 || y->individualDigit(0) > MaxBitLength) {
    ReportOversizedAllocation(cx, JSMSG_BIGINT_TOO_LARGE);
    if (js::SupportDifferentialTesting()) {
      fprintf(stderr, "ReportOutOfMemory called\n");
    }
    return nullptr;
  }
  Digit shift = y->individualDigit(0);
  int digitShift = static_cast<int>(shift / DigitBits);
  int bitsShift = static_cast<int>(shift % DigitBits);
  int length = x->digitLength();
  bool grow =
      bitsShift && (x->individualDigit(length - 1) >> (DigitBits - bitsShift));
  int resultLength = length + digitShift + grow;
  BigInt* result = createUninitialized(cx, resultLength, x->isNegative());
  if (!result) {
    return nullptr;
  }

  auto xDigits = x->digits();
  auto resultDigits = result->digits();
  int i = 0;
  for (; i < digitShift; i++) {
    resultDigits[i] = 0;
  }

  if (bitsShift == 0) {
    for (int j = 0; i < resultLength; i++, j++) {
      resultDigits[i] = xDigits[j];
    }
  } else {
    Digit carry = 0;
    for (int j = 0; j < length; i++, j++) {
      Digit d = xDigits[j];
      resultDigits[i] = (d << bitsShift) | carry;
      carry = d >> (DigitBits - bitsShift);
    }
    if (grow) {
      resultDigits[i] = carry;
    } else {
      MOZ_ASSERT(!carry);
    }
  }
  return result;
}

BigInt* BigInt::rshByMaximum(JSContext* cx, bool isNegative) {
  return isNegative ? negativeOne(cx) : zero(cx);
}

BigInt* BigInt::rshByAbsolute(JSContext* cx, HandleBigInt x, HandleBigInt y) {
  if (x->isZero() || y->isZero()) {
    return x;
  }

  if (y->digitLength() > 1 || y->individualDigit(0) >= MaxBitLength) {
    return rshByMaximum(cx, x->isNegative());
  }
  Digit shift = y->individualDigit(0);
  int length = x->digitLength();
  int digitShift = static_cast<int>(shift / DigitBits);
  int bitsShift = static_cast<int>(shift % DigitBits);
  int resultLength = length - digitShift;
  if (resultLength <= 0) {
    return rshByMaximum(cx, x->isNegative());
  }
  // For negative numbers, round down if any bit was shifted out (so that e.g.
  // -5n >> 1n == -3n and not -2n). Check now whether this will happen and
  // whether it can cause overflow into a new digit. If we allocate the result
  // large enough up front, it avoids having to do a second allocation later.
  bool mustRoundDown = false;
  {
    auto xDigits = x->digits();
    if (x->isNegative()) {
      const Digit mask = (static_cast<Digit>(1) << bitsShift) - 1;
      if ((xDigits[digitShift] & mask)) {
        mustRoundDown = true;
      } else {
        for (int i = 0; i < digitShift; i++) {
          if (xDigits[i]) {
            mustRoundDown = true;
            break;
          }
        }
      }
    }
    // If bits_shift is non-zero, it frees up bits, preventing overflow.
    if (mustRoundDown && bitsShift == 0) {
      // Overflow cannot happen if the most significant digit has unset bits.
      Digit msd = xDigits[length - 1];
      bool roundingCanOverflow = msd == std::numeric_limits<Digit>::max();
      if (roundingCanOverflow) {
        resultLength++;
      }
    }
  }

  MOZ_ASSERT(resultLength <= length);
  RootedBigInt result(cx,
                      createUninitialized(cx, resultLength, x->isNegative()));
  if (!result) {
    return nullptr;
  }
  {
    auto xDigits = x->digits();
    auto resultDigits = result->digits();
    if (!bitsShift) {
      // If roundingCanOverflow, manually initialize the overflow digit.
      resultDigits[resultLength - 1] = 0;
      for (int i = digitShift; i < length; i++) {
        resultDigits[i - digitShift] = xDigits[i];
      }
    } else {
      Digit carry = xDigits[digitShift] >> bitsShift;
      int last = length - digitShift - 1;
      for (int i = 0; i < last; i++) {
        Digit d = xDigits[i + digitShift + 1];
        resultDigits[i] = (d << (DigitBits - bitsShift)) | carry;
        carry = d >> bitsShift;
      }
      resultDigits[last] = carry;
    }
  }

  if (mustRoundDown) {
    MOZ_ASSERT(x->isNegative());
    // Since the result is negative, rounding down means adding one to
    // its absolute value. This cannot overflow.  TODO: modify the result in
    // place.
    return absoluteAddOne(cx, result, x->isNegative());
  }
  return destructivelyTrimHighZeroDigits(cx, result);
}

// BigInt proposal section 1.1.9. BigInt::leftShift ( x, y )
BigInt* BigInt::lsh(JSContext* cx, HandleBigInt x, HandleBigInt y) {
  if (y->isNegative()) {
    return rshByAbsolute(cx, x, y);
  }
  return lshByAbsolute(cx, x, y);
}

// BigInt proposal section 1.1.10. BigInt::signedRightShift ( x, y )
BigInt* BigInt::rsh(JSContext* cx, HandleBigInt x, HandleBigInt y) {
  if (y->isNegative()) {
    return lshByAbsolute(cx, x, y);
  }
  return rshByAbsolute(cx, x, y);
}

// BigInt proposal section 1.1.17. BigInt::bitwiseAND ( x, y )
BigInt* BigInt::bitAnd(JSContext* cx, HandleBigInt x, HandleBigInt y) {
  if (x->isZero()) {
    return x;
  }

  if (y->isZero()) {
    return y;
  }

  if (!x->isNegative() && !y->isNegative()) {
    return absoluteAnd(cx, x, y);
  }

  if (x->isNegative() && y->isNegative()) {
    // (-x) & (-y) == ~(x-1) & ~(y-1) == ~((x-1) | (y-1))
    // == -(((x-1) | (y-1)) + 1)
    RootedBigInt x1(cx, absoluteSubOne(cx, x));
    if (!x1) {
      return nullptr;
    }
    RootedBigInt y1(cx, absoluteSubOne(cx, y));
    if (!y1) {
      return nullptr;
    }
    RootedBigInt result(cx, absoluteOr(cx, x1, y1));
    if (!result) {
      return nullptr;
    }
    bool resultNegative = true;
    return absoluteAddOne(cx, result, resultNegative);
  }

  MOZ_ASSERT(x->isNegative() != y->isNegative());
  HandleBigInt& pos = x->isNegative() ? y : x;
  HandleBigInt& neg = x->isNegative() ? x : y;

  RootedBigInt neg1(cx, absoluteSubOne(cx, neg));
  if (!neg1) {
    return nullptr;
  }

  // x & (-y) == x & ~(y-1) == x & ~(y-1)
  return absoluteAndNot(cx, pos, neg1);
}

// BigInt proposal section 1.1.18. BigInt::bitwiseXOR ( x, y )
BigInt* BigInt::bitXor(JSContext* cx, HandleBigInt x, HandleBigInt y) {
  if (x->isZero()) {
    return y;
  }

  if (y->isZero()) {
    return x;
  }

  if (!x->isNegative() && !y->isNegative()) {
    return absoluteXor(cx, x, y);
  }

  if (x->isNegative() && y->isNegative()) {
    // (-x) ^ (-y) == ~(x-1) ^ ~(y-1) == (x-1) ^ (y-1)
    RootedBigInt x1(cx, absoluteSubOne(cx, x));
    if (!x1) {
      return nullptr;
    }
    RootedBigInt y1(cx, absoluteSubOne(cx, y));
    if (!y1) {
      return nullptr;
    }
    return absoluteXor(cx, x1, y1);
  }
  MOZ_ASSERT(x->isNegative() != y->isNegative());

  HandleBigInt& pos = x->isNegative() ? y : x;
  HandleBigInt& neg = x->isNegative() ? x : y;

  // x ^ (-y) == x ^ ~(y-1) == ~(x ^ (y-1)) == -((x ^ (y-1)) + 1)
  RootedBigInt result(cx, absoluteSubOne(cx, neg));
  if (!result) {
    return nullptr;
  }
  result = absoluteXor(cx, result, pos);
  if (!result) {
    return nullptr;
  }
  bool resultNegative = true;
  return absoluteAddOne(cx, result, resultNegative);
}

// BigInt proposal section 1.1.19. BigInt::bitwiseOR ( x, y )
BigInt* BigInt::bitOr(JSContext* cx, HandleBigInt x, HandleBigInt y) {
  if (x->isZero()) {
    return y;
  }

  if (y->isZero()) {
    return x;
  }

  bool resultNegative = x->isNegative() || y->isNegative();

  if (!resultNegative) {
    return absoluteOr(cx, x, y);
  }

  if (x->isNegative() && y->isNegative()) {
    // (-x) | (-y) == ~(x-1) | ~(y-1) == ~((x-1) & (y-1))
    // == -(((x-1) & (y-1)) + 1)
    RootedBigInt result(cx, absoluteSubOne(cx, x));
    if (!result) {
      return nullptr;
    }
    RootedBigInt y1(cx, absoluteSubOne(cx, y));
    if (!y1) {
      return nullptr;
    }
    result = absoluteAnd(cx, result, y1);
    if (!result) {
      return nullptr;
    }
    return absoluteAddOne(cx, result, resultNegative);
  }

  MOZ_ASSERT(x->isNegative() != y->isNegative());
  HandleBigInt& pos = x->isNegative() ? y : x;
  HandleBigInt& neg = x->isNegative() ? x : y;

  // x | (-y) == x | ~(y-1) == ~((y-1) &~ x) == -(((y-1) &~ x) + 1)
  RootedBigInt result(cx, absoluteSubOne(cx, neg));
  if (!result) {
    return nullptr;
  }
  result = absoluteAndNot(cx, result, pos);
  if (!result) {
    return nullptr;
  }
  return absoluteAddOne(cx, result, resultNegative);
}

// BigInt proposal section 1.1.2. BigInt::bitwiseNOT ( x )
BigInt* BigInt::bitNot(JSContext* cx, HandleBigInt x) {
  if (x->isNegative()) {
    // ~(-x) == ~(~(x-1)) == x-1
    return absoluteSubOne(cx, x);
  } else {
    // ~x == -x-1 == -(x+1)
    bool resultNegative = true;
    return absoluteAddOne(cx, x, resultNegative);
  }
}

int64_t BigInt::toInt64(const BigInt* x) { return WrapToSigned(toUint64(x)); }

uint64_t BigInt::toUint64(const BigInt* x) {
  if (x->isZero()) {
    return 0;
  }

  uint64_t digit = x->uint64FromAbsNonZero();

  // Return the two's complement if x is negative.
  if (x->isNegative()) {
    return ~(digit - 1);
  }

  return digit;
}

bool BigInt::isInt32(const BigInt* x, int32_t* result) {
  MOZ_MAKE_MEM_UNDEFINED(result, sizeof(*result));

  int64_t r;
  if (!BigInt::isInt64(x, &r)) {
    return false;
  }

  if (std::numeric_limits<int32_t>::min() <= r &&
      r <= std::numeric_limits<int32_t>::max()) {
    *result = AssertedCast<int32_t>(r);
    return true;
  }
  return false;
}

bool BigInt::isInt64(const BigInt* x, int64_t* result) {
  MOZ_MAKE_MEM_UNDEFINED(result, sizeof(*result));

  if (!x->absFitsInUint64()) {
    return false;
  }

  if (x->isZero()) {
    *result = 0;
    return true;
  }

  uint64_t magnitude = x->uint64FromAbsNonZero();

  if (x->isNegative()) {
    constexpr uint64_t Int64MinMagnitude = uint64_t(1) << 63;
    if (magnitude <= Int64MinMagnitude) {
      *result = magnitude == Int64MinMagnitude
                    ? std::numeric_limits<int64_t>::min()
                    : -AssertedCast<int64_t>(magnitude);
      return true;
    }
  } else {
    if (magnitude <=
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      *result = AssertedCast<int64_t>(magnitude);
      return true;
    }
  }

  return false;
}

bool BigInt::isUint64(const BigInt* x, uint64_t* result) {
  MOZ_MAKE_MEM_UNDEFINED(result, sizeof(*result));

  if (!x->absFitsInUint64() || x->isNegative()) {
    return false;
  }

  if (x->isZero()) {
    *result = 0;
    return true;
  }

  *result = x->uint64FromAbsNonZero();
  return true;
}

bool BigInt::isIntPtr(const BigInt* x, intptr_t* result) {
  MOZ_MAKE_MEM_UNDEFINED(result, sizeof(*result));

  static_assert(sizeof(intptr_t) == sizeof(BigInt::Digit));

  if (x->digitLength() > 1) {
    return false;
  }

  if (x->isZero()) {
    *result = 0;
    return true;
  }

  uintptr_t magnitude = x->individualDigit(0);

  if (x->isNegative()) {
    constexpr uintptr_t IntPtrMinMagnitude = uintptr_t(1) << (DigitBits - 1);
    if (magnitude <= IntPtrMinMagnitude) {
      *result = magnitude == IntPtrMinMagnitude
                    ? std::numeric_limits<intptr_t>::min()
                    : -AssertedCast<intptr_t>(magnitude);
      return true;
    }
  } else {
    if (magnitude <=
        static_cast<uintptr_t>(std::numeric_limits<intptr_t>::max())) {
      *result = AssertedCast<intptr_t>(magnitude);
      return true;
    }
  }

  return false;
}

bool BigInt::isNumber(const BigInt* x, double* result) {
  MOZ_MAKE_MEM_UNDEFINED(result, sizeof(*result));

  if (!x->absFitsInUint64()) {
    return false;
  }

  if (x->isZero()) {
    *result = 0;
    return true;
  }

  uint64_t magnitude = x->uint64FromAbsNonZero();
  if (magnitude < uint64_t(DOUBLE_INTEGRAL_PRECISION_LIMIT)) {
    *result = x->isNegative() ? -double(magnitude) : double(magnitude);
    return true;
  }

  return false;
}

// Compute `2**bits - (x & (2**bits - 1))`.  Used when treating BigInt values as
// arbitrary-precision two's complement signed integers.
BigInt* BigInt::truncateAndSubFromPowerOfTwo(JSContext* cx, HandleBigInt x,
                                             uint64_t bits,
                                             bool resultNegative) {
  MOZ_ASSERT(bits != 0);
  MOZ_ASSERT(!x->isZero());

  if (bits > MaxBitLength) {
    ReportOversizedAllocation(cx, JSMSG_BIGINT_TOO_LARGE);
    return nullptr;
  }

  size_t resultLength = CeilDiv(bits, DigitBits);
  BigInt* result = createUninitialized(cx, resultLength, resultNegative);
  if (!result) {
    return nullptr;
  }
  {
    // Process all digits except the MSD.
    auto xDigits = x->digits();
    auto resultDigits = result->digits();
    size_t xLength = xDigits.size();
    Digit borrow = 0;
    // Take digits from `x` until its length is exhausted.
    for (size_t i = 0; i < std::min(resultLength - 1, xLength); i++) {
      resultDigits[i] = digitSub3(0, xDigits[i], borrow, &borrow);
    }
    // Then simulate leading zeroes in `x` as needed.
    for (size_t i = xLength; i < resultLength - 1; i++) {
      resultDigits[i] = digitSub(0, borrow, &borrow);
    }

    // The MSD might contain extra bits that we don't want.
    Digit xMSD = resultLength <= xLength ? xDigits[resultLength - 1] : 0;
    Digit resultMSD;
    if (bits % DigitBits == 0) {
      resultMSD = digitSub3(0, xMSD, borrow, &borrow);
    } else {
      size_t drop = DigitBits - (bits % DigitBits);
      xMSD = (xMSD << drop) >> drop;
      Digit minuendMSD = Digit(1) << (DigitBits - drop);
      resultMSD = digitSub3(minuendMSD, xMSD, borrow, &borrow);
      MOZ_ASSERT(borrow == 0, "result < 2^bits");
      // If all subtracted bits were zero, we have to get rid of the
      // materialized minuendMSD again.
      resultMSD &= (minuendMSD - 1);
    }
    resultDigits[resultLength - 1] = resultMSD;
  }

  return destructivelyTrimHighZeroDigits(cx, result);
}

BigInt* BigInt::asUintN(JSContext* cx, HandleBigInt x, uint64_t bits) {
  if (x->isZero()) {
    return x;
  }

  if (bits == 0) {
    return zero(cx);
  }

  // When truncating a negative number, simulate two's complement.
  if (x->isNegative()) {
    bool resultNegative = false;
    return truncateAndSubFromPowerOfTwo(cx, x, bits, resultNegative);
  }

  if (bits <= 64) {
    uint64_t u64 = toUint64(x);
    uint64_t mask = uint64_t(-1) >> (64 - bits);
    uint64_t n = u64 & mask;
    if (u64 == n && x->absFitsInUint64()) {
      return x;
    }
    return createFromUint64(cx, n);
  }

  if (bits >= MaxBitLength) {
    return x;
  }

  size_t length;
  Digit highDigitMask, mask;
  {
    auto xDigits = x->digits();
    Digit msd = xDigits[xDigits.size() - 1];
    size_t msdBits = DigitBits - std::countl_zero(msd);
    size_t bitLength = msdBits + (xDigits.size() - 1) * DigitBits;

    if (bits >= bitLength) {
      return x;
    }

    length = CeilDiv(bits, DigitBits);
    MOZ_ASSERT(length >= 2, "single-digit cases should be handled above");
    MOZ_ASSERT(length <= xDigits.size());

    // Eagerly trim high zero digits.
    const size_t highDigitBits = ((bits - 1) % DigitBits) + 1;
    highDigitMask = Digit(-1) >> (DigitBits - highDigitBits);
    mask = highDigitMask;
    while (length > 0) {
      if (xDigits[length - 1] & mask) {
        break;
      }

      mask = Digit(-1);
      length--;
    }
  }

  const bool isNegative = false;
  BigInt* res = createUninitialized(cx, length, isNegative);
  if (!res) {
    return nullptr;
  }

  MOZ_ASSERT_IF(length == 0, res->isZero());

  auto xDigits = x->digits();
  auto resDigits = res->digits();
  while (length-- > 0) {
    resDigits[length] = xDigits[length] & mask;
    mask = Digit(-1);
  }

  return res;
}

BigInt* BigInt::asIntN(JSContext* cx, HandleBigInt x, uint64_t bits) {
  if (x->isZero()) {
    return x;
  }

  if (bits == 0) {
    return zero(cx);
  }

  if (bits == 64) {
    int64_t n = toInt64(x);
    if (((n < 0) == x->isNegative()) && x->absFitsInUint64()) {
      return x;
    }
    return createFromInt64(cx, n);
  }

  if (bits > MaxBitLength) {
    return x;
  }

  Digit signBit;
  size_t bitLength;
  {
    auto xDigits = x->digits();
    Digit msd = xDigits[xDigits.size() - 1];
    size_t msdBits = DigitBits - std::countl_zero(msd);
    bitLength = msdBits + (xDigits.size() - 1) * DigitBits;

    if (bits > bitLength) {
      return x;
    }

    signBit = Digit(1) << ((bits - 1) % DigitBits);
    if (bits == bitLength && msd < signBit) {
      return x;
    }
  }

  // All the cases above were the trivial cases: truncating zero, or to zero
  // bits, or to more bits than are in `x` (so we return `x` directly), or we
  // already have the 64-bit fast path.  If we get here, follow the textbook
  // algorithm from the specification.

  // BigInt.asIntN step 3:  Let `mod` be `x` modulo `2**bits`.
  RootedBigInt mod(cx, asUintN(cx, x, bits));
  if (!mod) {
    return nullptr;
  }

  // Step 4: If `mod >= 2**(bits - 1)`, return `mod - 2**bits`; otherwise,
  // return `mod`.
  if (mod->digitLength() == CeilDiv(bits, DigitBits)) {
    MOZ_ASSERT(!mod->isZero(),
               "nonzero bits implies nonzero digit length which implies "
               "nonzero overall");

    if ((mod->individualDigit(mod->digitLength() - 1) & signBit) != 0) {
      bool resultNegative = true;
      return truncateAndSubFromPowerOfTwo(cx, mod, bits, resultNegative);
    }
  }

  return mod;
}

static bool ValidBigIntOperands(JSContext* cx, HandleValue lhs,
                                HandleValue rhs) {
  MOZ_ASSERT(lhs.isBigInt() || rhs.isBigInt());

  if (!lhs.isBigInt() || !rhs.isBigInt()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_BIGINT_TO_NUMBER);
    return false;
  }

  return true;
}

bool BigInt::addValue(JSContext* cx, HandleValue lhs, HandleValue rhs,
                      MutableHandleValue res) {
  if (!ValidBigIntOperands(cx, lhs, rhs)) {
    return false;
  }

  RootedBigInt lhsBigInt(cx, lhs.toBigInt());
  RootedBigInt rhsBigInt(cx, rhs.toBigInt());
  BigInt* resBigInt = BigInt::add(cx, lhsBigInt, rhsBigInt);
  if (!resBigInt) {
    return false;
  }
  res.setBigInt(resBigInt);
  return true;
}

bool BigInt::subValue(JSContext* cx, HandleValue lhs, HandleValue rhs,
                      MutableHandleValue res) {
  if (!ValidBigIntOperands(cx, lhs, rhs)) {
    return false;
  }

  RootedBigInt lhsBigInt(cx, lhs.toBigInt());
  RootedBigInt rhsBigInt(cx, rhs.toBigInt());
  BigInt* resBigInt = BigInt::sub(cx, lhsBigInt, rhsBigInt);
  if (!resBigInt) {
    return false;
  }
  res.setBigInt(resBigInt);
  return true;
}

bool BigInt::mulValue(JSContext* cx, HandleValue lhs, HandleValue rhs,
                      MutableHandleValue res) {
  if (!ValidBigIntOperands(cx, lhs, rhs)) {
    return false;
  }

  RootedBigInt lhsBigInt(cx, lhs.toBigInt());
  RootedBigInt rhsBigInt(cx, rhs.toBigInt());
  BigInt* resBigInt = BigInt::mul(cx, lhsBigInt, rhsBigInt);
  if (!resBigInt) {
    return false;
  }
  res.setBigInt(resBigInt);
  return true;
}

bool BigInt::divValue(JSContext* cx, HandleValue lhs, HandleValue rhs,
                      MutableHandleValue res) {
  if (!ValidBigIntOperands(cx, lhs, rhs)) {
    return false;
  }

  RootedBigInt lhsBigInt(cx, lhs.toBigInt());
  RootedBigInt rhsBigInt(cx, rhs.toBigInt());
  BigInt* resBigInt = BigInt::div(cx, lhsBigInt, rhsBigInt);
  if (!resBigInt) {
    return false;
  }
  res.setBigInt(resBigInt);
  return true;
}

bool BigInt::modValue(JSContext* cx, HandleValue lhs, HandleValue rhs,
                      MutableHandleValue res) {
  if (!ValidBigIntOperands(cx, lhs, rhs)) {
    return false;
  }

  RootedBigInt lhsBigInt(cx, lhs.toBigInt());
  RootedBigInt rhsBigInt(cx, rhs.toBigInt());
  BigInt* resBigInt = BigInt::mod(cx, lhsBigInt, rhsBigInt);
  if (!resBigInt) {
    return false;
  }
  res.setBigInt(resBigInt);
  return true;
}

bool BigInt::powValue(JSContext* cx, HandleValue lhs, HandleValue rhs,
                      MutableHandleValue res) {
  if (!ValidBigIntOperands(cx, lhs, rhs)) {
    return false;
  }

  RootedBigInt lhsBigInt(cx, lhs.toBigInt());
  RootedBigInt rhsBigInt(cx, rhs.toBigInt());
  BigInt* resBigInt = BigInt::pow(cx, lhsBigInt, rhsBigInt);
  if (!resBigInt) {
    return false;
  }
  res.setBigInt(resBigInt);
  return true;
}

bool BigInt::negValue(JSContext* cx, HandleValue operand,
                      MutableHandleValue res) {
  MOZ_ASSERT(operand.isBigInt());

  RootedBigInt operandBigInt(cx, operand.toBigInt());
  BigInt* resBigInt = BigInt::neg(cx, operandBigInt);
  if (!resBigInt) {
    return false;
  }
  res.setBigInt(resBigInt);
  return true;
}

bool BigInt::incValue(JSContext* cx, HandleValue operand,
                      MutableHandleValue res) {
  MOZ_ASSERT(operand.isBigInt());

  RootedBigInt operandBigInt(cx, operand.toBigInt());
  BigInt* resBigInt = BigInt::inc(cx, operandBigInt);
  if (!resBigInt) {
    return false;
  }
  res.setBigInt(resBigInt);
  return true;
}

bool BigInt::decValue(JSContext* cx, HandleValue operand,
                      MutableHandleValue res) {
  MOZ_ASSERT(operand.isBigInt());

  RootedBigInt operandBigInt(cx, operand.toBigInt());
  BigInt* resBigInt = BigInt::dec(cx, operandBigInt);
  if (!resBigInt) {
    return false;
  }
  res.setBigInt(resBigInt);
  return true;
}

bool BigInt::lshValue(JSContext* cx, HandleValue lhs, HandleValue rhs,
                      MutableHandleValue res) {
  if (!ValidBigIntOperands(cx, lhs, rhs)) {
    return false;
  }

  RootedBigInt lhsBigInt(cx, lhs.toBigInt());
  RootedBigInt rhsBigInt(cx, rhs.toBigInt());
  BigInt* resBigInt = BigInt::lsh(cx, lhsBigInt, rhsBigInt);
  if (!resBigInt) {
    return false;
  }
  res.setBigInt(resBigInt);
  return true;
}

bool BigInt::rshValue(JSContext* cx, HandleValue lhs, HandleValue rhs,
                      MutableHandleValue res) {
  if (!ValidBigIntOperands(cx, lhs, rhs)) {
    return false;
  }

  RootedBigInt lhsBigInt(cx, lhs.toBigInt());
  RootedBigInt rhsBigInt(cx, rhs.toBigInt());
  BigInt* resBigInt = BigInt::rsh(cx, lhsBigInt, rhsBigInt);
  if (!resBigInt) {
    return false;
  }
  res.setBigInt(resBigInt);
  return true;
}

bool BigInt::bitAndValue(JSContext* cx, HandleValue lhs, HandleValue rhs,
                         MutableHandleValue res) {
  if (!ValidBigIntOperands(cx, lhs, rhs)) {
    return false;
  }

  RootedBigInt lhsBigInt(cx, lhs.toBigInt());
  RootedBigInt rhsBigInt(cx, rhs.toBigInt());
  BigInt* resBigInt = BigInt::bitAnd(cx, lhsBigInt, rhsBigInt);
  if (!resBigInt) {
    return false;
  }
  res.setBigInt(resBigInt);
  return true;
}

bool BigInt::bitXorValue(JSContext* cx, HandleValue lhs, HandleValue rhs,
                         MutableHandleValue res) {
  if (!ValidBigIntOperands(cx, lhs, rhs)) {
    return false;
  }

  RootedBigInt lhsBigInt(cx, lhs.toBigInt());
  RootedBigInt rhsBigInt(cx, rhs.toBigInt());
  BigInt* resBigInt = BigInt::bitXor(cx, lhsBigInt, rhsBigInt);
  if (!resBigInt) {
    return false;
  }
  res.setBigInt(resBigInt);
  return true;
}

bool BigInt::bitOrValue(JSContext* cx, HandleValue lhs, HandleValue rhs,
                        MutableHandleValue res) {
  if (!ValidBigIntOperands(cx, lhs, rhs)) {
    return false;
  }

  RootedBigInt lhsBigInt(cx, lhs.toBigInt());
  RootedBigInt rhsBigInt(cx, rhs.toBigInt());
  BigInt* resBigInt = BigInt::bitOr(cx, lhsBigInt, rhsBigInt);
  if (!resBigInt) {
    return false;
  }
  res.setBigInt(resBigInt);
  return true;
}

bool BigInt::bitNotValue(JSContext* cx, HandleValue operand,
                         MutableHandleValue res) {
  MOZ_ASSERT(operand.isBigInt());

  RootedBigInt operandBigInt(cx, operand.toBigInt());
  BigInt* resBigInt = BigInt::bitNot(cx, operandBigInt);
  if (!resBigInt) {
    return false;
  }
  res.setBigInt(resBigInt);
  return true;
}

// BigInt proposal section 7.3
BigInt* js::ToBigInt(JSContext* cx, HandleValue val) {
  RootedValue v(cx, val);

  // Step 1.
  if (!ToPrimitive(cx, JSTYPE_NUMBER, &v)) {
    return nullptr;
  }

  // Step 2.
  if (v.isBigInt()) {
    return v.toBigInt();
  }

  if (v.isBoolean()) {
    return v.toBoolean() ? BigInt::one(cx) : BigInt::zero(cx);
  }

  if (v.isString()) {
    RootedString str(cx, v.toString());
    BigInt* bi;
    JS_TRY_VAR_OR_RETURN_NULL(cx, bi, StringToBigInt(cx, str));
    if (!bi) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_BIGINT_INVALID_SYNTAX);
      return nullptr;
    }
    return bi;
  }

  ReportValueError(cx, JSMSG_CANT_CONVERT_TO, JSDVG_IGNORE_STACK, v, nullptr,
                   "BigInt");
  return nullptr;
}

JS::Result<int64_t> js::ToBigInt64(JSContext* cx, HandleValue v) {
  BigInt* bi = js::ToBigInt(cx, v);
  if (!bi) {
    return cx->alreadyReportedError();
  }
  return BigInt::toInt64(bi);
}

JS::Result<uint64_t> js::ToBigUint64(JSContext* cx, HandleValue v) {
  BigInt* bi = js::ToBigInt(cx, v);
  if (!bi) {
    return cx->alreadyReportedError();
  }
  return BigInt::toUint64(bi);
}

double BigInt::numberValue(const BigInt* x) {
  if (x->isZero()) {
    return 0.0;
  }

  using Double = mozilla::FloatingPoint<double>;
  constexpr uint8_t ExponentShift = Double::kExponentShift;
  constexpr uint8_t SignificandWidth = Double::kSignificandWidth;
  constexpr unsigned ExponentBias = Double::kExponentBias;
  constexpr uint8_t SignShift = Double::kExponentWidth + SignificandWidth;

  MOZ_ASSERT(x->digitLength() > 0);

  // Fast path for the likely-common case of up to a uint64_t of magnitude not
  // exceeding integral precision in IEEE-754.  (Note that we *depend* on this
  // optimization being performed further down.)
  if (x->absFitsInUint64()) {
    uint64_t magnitude = x->uint64FromAbsNonZero();
    const uint64_t MaxIntegralPrecisionDouble = uint64_t(1)
                                                << (SignificandWidth + 1);
    if (magnitude <= MaxIntegralPrecisionDouble) {
      return x->isNegative() ? -double(magnitude) : +double(magnitude);
    }
  }

  auto xDigits = x->digits();
  size_t length = xDigits.size();
  Digit msd = xDigits[length - 1];
  uint8_t msdLeadingZeroes = uint8_t(std::countl_zero(msd));

  // `2**ExponentBias` is the largest power of two in a finite IEEE-754
  // double.  If this bigint has a greater power of two, it'll round to
  // infinity.
  uint64_t exponent = length * DigitBits - msdLeadingZeroes - 1;
  if (exponent > ExponentBias) {
    return x->isNegative() ? mozilla::NegativeInfinity<double>()
                           : mozilla::PositiveInfinity<double>();
  }

  // Otherwise munge the most significant bits of the number into proper
  // position in an IEEE-754 double and go to town.

  // Omit the most significant bit: the IEEE-754 format includes this bit
  // implicitly for all double-precision integers.
  const uint8_t msdIgnoredBits = msdLeadingZeroes + 1;
  const uint8_t msdIncludedBits = DigitBits - msdIgnoredBits;

  // We compute the final mantissa of the result, shifted upward to the top of
  // the `uint64_t` space -- plus an extra bit to detect potential rounding.
  constexpr uint8_t BitsNeededForShiftedMantissa = SignificandWidth + 1;

  // Shift `msd`'s contributed bits upward to remove high-order zeroes and the
  // highest set bit (which is implicit in IEEE-754 integral values so must be
  // removed) and to add low-order zeroes.  (Lower-order garbage bits are
  // discarded when `shiftedMantissa` is converted to a real mantissa.)
  uint64_t shiftedMantissa =
      msdIncludedBits == 0 ? 0 : uint64_t(msd) << (64 - msdIncludedBits);

  // If the extra bit is set, correctly rounding the result may require
  // examining all lower-order bits.  Also compute 1) the index of the Digit
  // storing the extra bit, and 2) whether bits beneath the extra bit in that
  // Digit are nonzero so we can round if needed.
  size_t digitContainingExtraBit;
  Digit bitsBeneathExtraBitInDigitContainingExtraBit;

  // Add shifted bits to `shiftedMantissa` until we have a complete mantissa and
  // an extra bit.
  if (msdIncludedBits >= BitsNeededForShiftedMantissa) {
    //       DigitBits=64 (necessarily for msdIncludedBits ≥ SignificandWidth+1;
    //            |        C++ compiler range analysis ought eliminate this
    //            |        check on 32-bit)
    //   _________|__________
    //  /                    |
    //        msdIncludedBits
    //      ________|________
    //     /                 |
    // [001···················|
    //  \_/\_____________/\__|
    //   |            |    |
    // msdIgnoredBits |   bits below the extra bit (may be no bits)
    //      BitsNeededForShiftedMantissa=SignificandWidth+1
    digitContainingExtraBit = length - 1;

    const uint8_t countOfBitsInDigitBelowExtraBit =
        DigitBits - BitsNeededForShiftedMantissa - msdIgnoredBits;
    bitsBeneathExtraBitInDigitContainingExtraBit =
        msd & ((Digit(1) << countOfBitsInDigitBelowExtraBit) - 1);
  } else {
    MOZ_ASSERT(length >= 2,
               "single-Digit numbers with this few bits should have been "
               "handled by the fast-path above");

    Digit second = xDigits[length - 2];
    if (DigitBits == 64) {
      shiftedMantissa |= second >> msdIncludedBits;

      digitContainingExtraBit = length - 2;

      //  msdIncludedBits + DigitBits
      //      ________|_________
      //     /                  |
      //             DigitBits=64
      // msdIncludedBits    |
      //      __|___   _____|___
      //     /      \ /         |
      // [001········|···········|
      //  \_/\_____________/\___|
      //   |            |     |
      // msdIgnoredBits | bits below the extra bit (always more than one)
      //                |
      //      BitsNeededForShiftedMantissa=SignificandWidth+1
      const uint8_t countOfBitsInSecondDigitBelowExtraBit =
          (msdIncludedBits + DigitBits) - BitsNeededForShiftedMantissa;

      bitsBeneathExtraBitInDigitContainingExtraBit =
          second << (DigitBits - countOfBitsInSecondDigitBelowExtraBit);
    } else {
      shiftedMantissa |= uint64_t(second) << msdIgnoredBits;

      if (msdIncludedBits + DigitBits >= BitsNeededForShiftedMantissa) {
        digitContainingExtraBit = length - 2;

        //  msdIncludedBits + DigitBits
        //      ______|________
        //     /               |
        //             DigitBits=32
        // msdIncludedBits |
        //      _|_   _____|___
        //     /   \ /         |
        // [001·····|···········|
        //     \___________/\__|
        //          |        |
        //          |      bits below the extra bit (may be no bits)
        //      BitsNeededForShiftedMantissa=SignificandWidth+1
        const uint8_t countOfBitsInSecondDigitBelowExtraBit =
            (msdIncludedBits + DigitBits) - BitsNeededForShiftedMantissa;

        bitsBeneathExtraBitInDigitContainingExtraBit =
            second & ((Digit(1) << countOfBitsInSecondDigitBelowExtraBit) - 1);
      } else {
        MOZ_ASSERT(length >= 3,
                   "we must have at least three digits here, because "
                   "`msdIncludedBits + 32 < BitsNeededForShiftedMantissa` "
                   "guarantees `x < 2**53` -- and therefore the "
                   "MaxIntegralPrecisionDouble optimization above will have "
                   "handled two-digit cases");

        Digit third = xDigits[length - 3];
        shiftedMantissa |= uint64_t(third) >> msdIncludedBits;

        digitContainingExtraBit = length - 3;

        //    msdIncludedBits + DigitBits + DigitBits
        //      ____________|______________
        //     /                           |
        //             DigitBits=32
        // msdIncludedBits |     DigitBits=32
        //      _|_   _____|___   ____|____
        //     /   \ /         \ /         |
        // [001·····|···········|···········|
        //     \____________________/\_____|
        //               |               |
        //               |      bits below the extra bit
        //      BitsNeededForShiftedMantissa=SignificandWidth+1
        static_assert(2 * DigitBits > BitsNeededForShiftedMantissa,
                      "two 32-bit digits should more than fill a mantissa");
        const uint8_t countOfBitsInThirdDigitBelowExtraBit =
            msdIncludedBits + 2 * DigitBits - BitsNeededForShiftedMantissa;

        // Shift out the mantissa bits and the extra bit.
        bitsBeneathExtraBitInDigitContainingExtraBit =
            third << (DigitBits - countOfBitsInThirdDigitBelowExtraBit);
      }
    }
  }

  constexpr uint64_t LeastSignificantBit = uint64_t(1)
                                           << (64 - SignificandWidth);
  constexpr uint64_t ExtraBit = LeastSignificantBit >> 1;

  // The extra bit must be set for rounding to change the mantissa.
  if ((shiftedMantissa & ExtraBit) != 0) {
    bool shouldRoundUp;
    if (shiftedMantissa & LeastSignificantBit) {
      // If the lowest mantissa bit is set, it doesn't matter what lower bits
      // are: nearest-even rounds up regardless.
      shouldRoundUp = true;
    } else {
      // If the lowest mantissa bit is unset, *all* lower bits are relevant.
      // All-zero bits below the extra bit situates `x` halfway between two
      // values, and the nearest *even* value lies downward.  But if any bit
      // below the extra bit is set, `x` is closer to the rounded-up value.
      shouldRoundUp = bitsBeneathExtraBitInDigitContainingExtraBit != 0;
      if (!shouldRoundUp) {
        while (digitContainingExtraBit-- > 0) {
          if (xDigits[digitContainingExtraBit] != 0) {
            shouldRoundUp = true;
            break;
          }
        }
      }
    }

    if (shouldRoundUp) {
      // Add one to the significand bits.  If they overflow, the exponent must
      // also be increased.  If *that* overflows, return the correct infinity.
      uint64_t before = shiftedMantissa;
      shiftedMantissa += ExtraBit;
      if (shiftedMantissa < before) {
        exponent++;
        if (exponent > ExponentBias) {
          return x->isNegative() ? NegativeInfinity<double>()
                                 : PositiveInfinity<double>();
        }
      }
    }
  }

  uint64_t significandBits = shiftedMantissa >> (64 - SignificandWidth);
  uint64_t signBit = uint64_t(x->isNegative() ? 1 : 0) << SignShift;
  uint64_t exponentBits = (exponent + ExponentBias) << ExponentShift;
  return mozilla::BitwiseCast<double>(signBit | exponentBits | significandBits);
}

int8_t BigInt::compare(const BigInt* x, const BigInt* y) {
  // Sanity checks to catch negative zeroes escaping to the wild.
  MOZ_ASSERT(!x->isNegative() || !x->isZero());
  MOZ_ASSERT(!y->isNegative() || !y->isZero());

  bool xSign = x->isNegative();

  if (xSign != y->isNegative()) {
    return xSign ? -1 : 1;
  }

  if (xSign) {
    std::swap(x, y);
  }

  return absoluteCompare(x, y);
}

bool BigInt::equal(const BigInt* lhs, const BigInt* rhs) {
  if (lhs == rhs) {
    return true;
  }
  if (lhs->digitLength() != rhs->digitLength()) {
    return false;
  }
  if (lhs->isNegative() != rhs->isNegative()) {
    return false;
  }
  auto lhsDigits = lhs->digits();
  auto rhsDigits = rhs->digits();
  for (size_t i = 0; i < lhsDigits.size(); i++) {
    if (lhsDigits[i] != rhsDigits[i]) {
      return false;
    }
  }
  return true;
}

int8_t BigInt::compare(const BigInt* x, double y) {
  MOZ_ASSERT(!std::isnan(y));

  constexpr int LessThan = -1, Equal = 0, GreaterThan = 1;

  // ±Infinity exceeds a finite bigint value.
  if (!std::isfinite(y)) {
    return y > 0 ? LessThan : GreaterThan;
  }

  // Handle `x === 0n` and `y == 0` special cases.
  if (x->isZero()) {
    if (y == 0) {
      // -0 and +0 are treated identically.
      return Equal;
    }

    return y > 0 ? LessThan : GreaterThan;
  }

  const bool xNegative = x->isNegative();
  if (y == 0) {
    return xNegative ? LessThan : GreaterThan;
  }

  // Nonzero `x` and `y` with different signs are trivially compared.
  const bool yNegative = y < 0;
  if (xNegative != yNegative) {
    return xNegative ? LessThan : GreaterThan;
  }

  // `x` and `y` are same-signed.  Determine which has greater magnitude,
  // then combine that with the signedness just computed to reach a result.
  const int exponent = mozilla::ExponentComponent(y);
  if (exponent < 0) {
    // `y` is a nonzero fraction of magnitude less than 1.
    return xNegative ? LessThan : GreaterThan;
  }

  auto xDigits = x->digits();
  size_t xLength = xDigits.size();
  MOZ_ASSERT(xLength > 0);

  Digit xMSD = xDigits[xLength - 1];
  const int shift = std::countl_zero(xMSD);
  int xBitLength = xLength * DigitBits - shift;

  // Differing bit-length makes for a simple comparison.
  int yBitLength = exponent + 1;
  if (xBitLength < yBitLength) {
    return xNegative ? GreaterThan : LessThan;
  }
  if (xBitLength > yBitLength) {
    return xNegative ? LessThan : GreaterThan;
  }

  // Compare the high 64 bits of both numbers.  (Lower-order bits not present
  // in either number are zeroed.)  Either that distinguishes `x` and `y`, or
  // `x` and `y` differ only if a subsequent nonzero bit in `x` means `x` has
  // larger magnitude.

  using Double = mozilla::FloatingPoint<double>;
  constexpr uint8_t SignificandWidth = Double::kSignificandWidth;
  constexpr uint64_t SignificandBits = Double::kSignificandBits;

  const uint64_t doubleBits = mozilla::BitwiseCast<uint64_t>(y);
  const uint64_t significandBits = doubleBits & SignificandBits;

  // Readd the implicit-one bit when constructing `y`'s high 64 bits.
  const uint64_t yHigh64Bits =
      ((uint64_t(1) << SignificandWidth) | significandBits)
      << (64 - SignificandWidth - 1);

  // Cons up `x`'s high 64 bits, backfilling zeroes for binary fractions of 1
  // if `x` doesn't have 64 bits.
  uint8_t xBitsFilled = DigitBits - shift;
  uint64_t xHigh64Bits = uint64_t(xMSD) << (64 - xBitsFilled);

  // At this point we no longer need to look at the most significant digit.
  xLength--;

  // The high 64 bits from `x` will probably not align to a digit boundary.
  // `xHasNonZeroLeftoverBits` will be set to true if any remaining
  // least-significant bit from the digit holding xHigh64Bits's
  // least-significant bit is nonzero.
  bool xHasNonZeroLeftoverBits = false;

  if (xBitsFilled < std::min(xBitLength, 64)) {
    MOZ_ASSERT(xLength >= 1,
               "If there are more bits to fill, there should be "
               "more digits to fill them from");

    Digit second = xDigits[--xLength];
    if (DigitBits == 32) {
      xBitsFilled += 32;
      xHigh64Bits |= uint64_t(second) << (64 - xBitsFilled);
      if (xBitsFilled < 64 && xLength >= 1) {
        Digit third = xDigits[--xLength];
        const uint8_t neededBits = 64 - xBitsFilled;
        xHigh64Bits |= uint64_t(third) >> (DigitBits - neededBits);
        xHasNonZeroLeftoverBits = (third << neededBits) != 0;
      }
    } else {
      const uint8_t neededBits = 64 - xBitsFilled;
      xHigh64Bits |= uint64_t(second) >> (DigitBits - neededBits);
      xHasNonZeroLeftoverBits = (second << neededBits) != 0;
    }
  }

  // If high bits are unequal, the larger one has greater magnitude.
  if (yHigh64Bits > xHigh64Bits) {
    return xNegative ? GreaterThan : LessThan;
  }
  if (xHigh64Bits > yHigh64Bits) {
    return xNegative ? LessThan : GreaterThan;
  }

  // Otherwise the top 64 bits of both are equal.  If the values differ, a
  // lower-order bit in `x` is nonzero and `x` has greater magnitude than
  // `y`; otherwise `x == y`.
  if (xHasNonZeroLeftoverBits) {
    return xNegative ? LessThan : GreaterThan;
  }
  while (xLength != 0) {
    if (xDigits[--xLength] != 0) {
      return xNegative ? LessThan : GreaterThan;
    }
  }

  return Equal;
}

bool BigInt::equal(const BigInt* lhs, double rhs) {
  if (std::isnan(rhs)) {
    return false;
  }
  return compare(lhs, rhs) == 0;
}

JS::Result<bool> BigInt::equal(JSContext* cx, Handle<BigInt*> lhs,
                               HandleString rhs) {
  BigInt* rhsBigInt = MOZ_TRY(StringToBigInt(cx, rhs));
  if (!rhsBigInt) {
    return false;
  }
  return equal(lhs, rhsBigInt);
}

// BigInt proposal section 1.1.12. BigInt::lessThan ( x, y )
bool BigInt::lessThan(const BigInt* x, const BigInt* y) {
  return compare(x, y) < 0;
}

Maybe<bool> BigInt::lessThan(const BigInt* lhs, double rhs) {
  if (std::isnan(rhs)) {
    return Maybe<bool>(Nothing());
  }
  return Some(compare(lhs, rhs) < 0);
}

Maybe<bool> BigInt::lessThan(double lhs, const BigInt* rhs) {
  if (std::isnan(lhs)) {
    return Maybe<bool>(Nothing());
  }
  return Some(-compare(rhs, lhs) < 0);
}

bool BigInt::lessThan(JSContext* cx, HandleBigInt lhs, HandleString rhs,
                      Maybe<bool>& res) {
  BigInt* rhsBigInt;
  JS_TRY_VAR_OR_RETURN_FALSE(cx, rhsBigInt, StringToBigInt(cx, rhs));
  if (!rhsBigInt) {
    res = Nothing();
    return true;
  }
  res = Some(lessThan(lhs, rhsBigInt));
  return true;
}

bool BigInt::lessThan(JSContext* cx, HandleString lhs, HandleBigInt rhs,
                      Maybe<bool>& res) {
  BigInt* lhsBigInt;
  JS_TRY_VAR_OR_RETURN_FALSE(cx, lhsBigInt, StringToBigInt(cx, lhs));
  if (!lhsBigInt) {
    res = Nothing();
    return true;
  }
  res = Some(lessThan(lhsBigInt, rhs));
  return true;
}

bool BigInt::lessThan(JSContext* cx, HandleValue lhs, HandleValue rhs,
                      Maybe<bool>& res) {
  if (lhs.isBigInt()) {
    if (rhs.isString()) {
      RootedBigInt lhsBigInt(cx, lhs.toBigInt());
      RootedString rhsString(cx, rhs.toString());
      return lessThan(cx, lhsBigInt, rhsString, res);
    }

    if (rhs.isNumber()) {
      res = lessThan(lhs.toBigInt(), rhs.toNumber());
      return true;
    }

    MOZ_ASSERT(rhs.isBigInt());
    res = Some(lessThan(lhs.toBigInt(), rhs.toBigInt()));
    return true;
  }

  MOZ_ASSERT(rhs.isBigInt());
  if (lhs.isString()) {
    RootedString lhsString(cx, lhs.toString());
    RootedBigInt rhsBigInt(cx, rhs.toBigInt());
    return lessThan(cx, lhsString, rhsBigInt, res);
  }

  MOZ_ASSERT(lhs.isNumber());
  res = lessThan(lhs.toNumber(), rhs.toBigInt());
  return true;
}

template <js::AllowGC allowGC>
JSLinearString* BigInt::toString(JSContext* cx, HandleBigInt x, uint8_t radix) {
  MOZ_ASSERT(2 <= radix && radix <= 36);

  if (x->isZero()) {
    return cx->staticStrings().getInt(0);
  }

  if (x->digitLength() == 1) {
    return toStringSingleDigit<allowGC>(cx, x->individualDigit(0),
                                        x->isNegative(), radix);
  }

  if (std::has_single_bit(radix)) {
    return toStringBasePowerOfTwo<allowGC>(cx, x, radix);
  }

  // Punt on doing generic toString without GC.
  if (!allowGC) {
    return nullptr;
  }

  return toStringGeneric(cx, x, radix);
}

template JSLinearString* BigInt::toString<js::CanGC>(JSContext* cx,
                                                     HandleBigInt x,
                                                     uint8_t radix);
template JSLinearString* BigInt::toString<js::NoGC>(JSContext* cx,
                                                    HandleBigInt x,
                                                    uint8_t radix);

template <typename CharT>
static inline BigInt* ParseStringBigIntLiteral(JSContext* cx,
                                               Range<const CharT> range,
                                               bool* haveParseError) {
  auto start = range.begin();
  auto end = range.end();

  while (start < end && unicode::IsSpace(start[0])) {
    start++;
  }

  while (start < end && unicode::IsSpace(end[-1])) {
    end--;
  }

  if (start == end) {
    return BigInt::zero(cx);
  }

  // StringNumericLiteral ::: StrDecimalLiteral, but without Infinity, decimal
  // points, or exponents.  Note that the raw '+' or '-' cases fall through
  // because the string is too short, and eventually signal a parse error.
  if (end - start > 1) {
    if (start[0] == '+') {
      bool isNegative = false;
      start++;
      return BigInt::parseLiteralDigits(cx, Range<const CharT>(start, end), 10,
                                        isNegative, haveParseError);
    }
    if (start[0] == '-') {
      bool isNegative = true;
      start++;
      return BigInt::parseLiteralDigits(cx, Range<const CharT>(start, end), 10,
                                        isNegative, haveParseError);
    }
  }

  return BigInt::parseLiteral(cx, Range<const CharT>(start, end),
                              haveParseError);
}

// Called from BigInt constructor.
JS::Result<BigInt*> js::StringToBigInt(JSContext* cx, HandleString str) {
  JSLinearString* linear = str->ensureLinear(cx);
  if (!linear) {
    return cx->alreadyReportedOOM();
  }

  AutoStableStringChars chars(cx);
  if (!chars.init(cx, str)) {
    return cx->alreadyReportedOOM();
  }

  BigInt* res;
  bool parseError = false;
  if (chars.isLatin1()) {
    res = ParseStringBigIntLiteral(cx, chars.latin1Range(), &parseError);
  } else {
    res = ParseStringBigIntLiteral(cx, chars.twoByteRange(), &parseError);
  }

  // A nullptr result can indicate either a parse error or generic error.
  if (!res && !parseError) {
    return cx->alreadyReportedError();
  }

  return res;
}

// Called from parser with already trimmed and validated token.
BigInt* js::ParseBigIntLiteral(JSContext* cx,
                               const Range<const char16_t>& chars) {
  // This function is only called from the frontend when parsing BigInts. Parsed
  // BigInts are stored in the script's data vector and therefore need to be
  // allocated in the tenured heap.
  constexpr gc::Heap heap = gc::Heap::Tenured;

  bool parseError = false;
  BigInt* res = BigInt::parseLiteral(cx, chars, &parseError, heap);
  if (!res) {
    return nullptr;
  }
  MOZ_ASSERT(res->isTenured());
  MOZ_RELEASE_ASSERT(!parseError);
  return res;
}

mozilla::Maybe<int64_t> js::ParseBigInt64Literal(
    mozilla::Range<const char16_t> chars) {
  size_t length = chars.length();
  MOZ_ASSERT(length > 0);

  int32_t radix = 10;
  if (length > 2 && chars[0] == '0') {
    if (chars[1] == 'b' || chars[1] == 'B') {
      // StringNumericLiteral ::: BinaryIntegerLiteral
      radix = 2;
    } else if (chars[1] == 'x' || chars[1] == 'X') {
      // StringNumericLiteral ::: HexIntegerLiteral
      radix = 16;
    } else if (chars[1] == 'o' || chars[1] == 'O') {
      // StringNumericLiteral ::: OctalIntegerLiteral
      radix = 8;
    }
  }

  auto start = chars.begin();
  const auto end = chars.end();

  // Skip over prefix.
  if (radix != 10) {
    start += 2;
  }

  // Skipping leading zeroes.
  while (*start == '0') {
    start++;
    if (start == end) {
      return mozilla::Some(0);
    }
  }

  mozilla::CheckedInt<int64_t> r = 0;
  while (start < end) {
    char16_t c = *start++;
    MOZ_ASSERT(mozilla::IsAsciiAlphanumeric(c));

    int32_t digit = mozilla::AsciiAlphanumericToNumber(c);
    MOZ_ASSERT(digit < radix);

    r *= radix;
    r += digit;
    if (!r.isValid()) {
      return mozilla::Nothing();
    }
  }

  return mozilla::Some(r.value());
}

template <js::AllowGC allowGC>
JSAtom* js::BigIntToAtom(JSContext* cx, HandleBigInt bi) {
  JSString* str = BigInt::toString<allowGC>(cx, bi, 10);
  if (!str) {
    return nullptr;
  }
  JSAtom* atom = AtomizeString(cx, str);
  if (!atom) {
    if constexpr (!allowGC) {
      // NOTE: AtomizeString can call ReportAllocationOverflow other than
      //       ReportOutOfMemory, but ReportAllocationOverflow cannot happen
      //       because the length is guarded by BigInt::toString.
      cx->recoverFromOutOfMemory();
    }
    return nullptr;
  }
  return atom;
}

template JSAtom* js::BigIntToAtom<js::CanGC>(JSContext* cx, HandleBigInt bi);
template JSAtom* js::BigIntToAtom<js::NoGC>(JSContext* cx, HandleBigInt bi);

#if defined(DEBUG) || defined(JS_JITSPEW)
void BigInt::dump() const {
  js::Fprinter out(stderr);
  dump(out);
}

void BigInt::dump(js::GenericPrinter& out) const {
  js::JSONPrinter json(out);
  dump(json);
  out.put("\n");
}

void BigInt::dump(js::JSONPrinter& json) const {
  json.beginObject();
  dumpFields(json);
  json.endObject();
}

void BigInt::dumpFields(js::JSONPrinter& json) const {
  json.formatProperty("address", "(JS::BigInt*)0x%p", this);

  json.property("digitLength", digitLength());

  js::GenericPrinter& out = json.beginStringProperty("value");
  dumpLiteral(out);
  json.endStringProperty();
}

void BigInt::dumpStringContent(js::GenericPrinter& out) const {
  dumpLiteral(out);

  out.printf(" @ (JS::BigInt*)0x%p", this);
}

void BigInt::dumpLiteral(js::GenericPrinter& out) const {
  if (isNegative()) {
    out.putChar('-');
  }

  auto thisDigits = digits();
  if (thisDigits.size() == 0) {
    out.put("0");
  } else if (digitLength() == 1) {
    uint64_t d = individualDigit(0);
    out.printf("%" PRIu64, d);
  } else {
    out.put("0x");
    for (size_t i = 0; i < thisDigits.size(); i++) {
      uint64_t d = thisDigits[thisDigits.size() - i - 1];
      if (sizeof(Digit) == 4) {
        out.printf("%.8" PRIX32, uint32_t(d));
      } else {
        out.printf("%.16" PRIX64, d);
      }
    }
  }

  out.putChar('n');
}
#endif

JS::ubi::Node::Size JS::ubi::Concrete<BigInt>::size(
    mozilla::MallocSizeOf mallocSizeOf) const {
  BigInt& bi = get();
  size_t size = sizeof(JS::BigInt);
  if (IsInsideNursery(&bi)) {
    size += Nursery::nurseryCellHeaderSize();
    size += bi.sizeOfExcludingThisInNursery();
  } else {
    size += bi.sizeOfExcludingThis();
  }
  return size;
}

// Public API

BigInt* JS::NumberToBigInt(JSContext* cx, double num) {
  return js::NumberToBigInt(cx, num);
}

template <typename CharT>
static inline BigInt* StringToBigIntHelper(JSContext* cx,
                                           const Range<const CharT>& chars) {
  bool parseError = false;
  BigInt* bi = ParseStringBigIntLiteral(cx, chars, &parseError);
  if (!bi) {
    if (parseError) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_BIGINT_INVALID_SYNTAX);
    }
    return nullptr;
  }
  MOZ_RELEASE_ASSERT(!parseError);
  return bi;
}

BigInt* JS::StringToBigInt(JSContext* cx,
                           const Range<const Latin1Char>& chars) {
  return StringToBigIntHelper(cx, chars);
}

BigInt* JS::StringToBigInt(JSContext* cx, const Range<const char16_t>& chars) {
  return StringToBigIntHelper(cx, chars);
}

BigInt* JS::SimpleStringToBigInt(JSContext* cx, mozilla::Span<const char> chars,
                                 uint8_t radix) {
  if (chars.empty()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_BIGINT_INVALID_SYNTAX);
    return nullptr;
  }
  if (radix < 2 || radix > 36) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_BAD_RADIX);
    return nullptr;
  }

  mozilla::Span<const Latin1Char> latin1{
      reinterpret_cast<const Latin1Char*>(chars.data()), chars.size()};

  bool isNegative = false;
  if (chars.size() > 1 && (chars[0] == '-' || chars[0] == '+')) {
    isNegative = chars[0] == '-';
    latin1 = latin1.From(1);
  }

  bool haveParseError = false;
  BigInt* bi = BigInt::parseLiteralDigits(cx, mozilla::Range{latin1}, radix,
                                          isNegative, &haveParseError);
  if (!bi) {
    if (haveParseError) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_BIGINT_INVALID_SYNTAX);
    }
    return nullptr;
  }
  MOZ_RELEASE_ASSERT(!haveParseError);
  return bi;
}

BigInt* JS::ToBigInt(JSContext* cx, HandleValue val) {
  return js::ToBigInt(cx, val);
}

int64_t JS::ToBigInt64(const JS::BigInt* bi) { return BigInt::toInt64(bi); }

uint64_t JS::ToBigUint64(const JS::BigInt* bi) { return BigInt::toUint64(bi); }

double JS::BigIntToNumber(const JS::BigInt* bi) {
  return BigInt::numberValue(bi);
}

bool JS::BigIntIsNegative(const BigInt* bi) {
  return !bi->isZero() && bi->isNegative();
}

bool JS::BigIntFitsNumber(const BigInt* bi, double* out) {
  return bi->isNumber(bi, out);
}

JSString* JS::BigIntToString(JSContext* cx, Handle<BigInt*> bi, uint8_t radix) {
  if (radix < 2 || radix > 36) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_BAD_RADIX);
    return nullptr;
  }
  return BigInt::toString<CanGC>(cx, bi, radix);
}

// Semi-public template details

BigInt* JS::detail::BigIntFromInt64(JSContext* cx, int64_t num) {
  return BigInt::createFromInt64(cx, num);
}

BigInt* JS::detail::BigIntFromUint64(JSContext* cx, uint64_t num) {
  return BigInt::createFromUint64(cx, num);
}

BigInt* JS::detail::BigIntFromBool(JSContext* cx, bool b) {
  return b ? BigInt::one(cx) : BigInt::zero(cx);
}

bool JS::detail::BigIntIsInt64(const BigInt* bi, int64_t* result) {
  return BigInt::isInt64(bi, result);
}

bool JS::detail::BigIntIsUint64(const BigInt* bi, uint64_t* result) {
  return BigInt::isUint64(bi, result);
}
