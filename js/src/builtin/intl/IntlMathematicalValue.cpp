/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "builtin/intl/IntlMathematicalValue.h"

#include "mozilla/Assertions.h"
#include "mozilla/FloatingPoint.h"
#include "mozilla/Range.h"
#include "mozilla/Span.h"
#include "mozilla/TextUtils.h"

#include <cmath>
#include <stdint.h>

#include "jspubtd.h"

#include "builtin/Number.h"
#include "js/CharacterEncoding.h"
#include "js/GCAPI.h"
#include "js/TracingAPI.h"
#include "util/Text.h"
#include "vm/BigIntType.h"
#include "vm/JSContext.h"
#include "vm/JSObject.h"
#include "vm/StringType.h"

using namespace js;
using namespace js::intl;

void js::intl::IntlMathematicalValue::trace(JSTracer* trc) {
  JS::TraceRoot(trc, &value_, "IntlMathematicalValue::value");
}

void js::intl::IntlMathematicalValueString::trace(JSTracer* trc) {
  TraceRoot(trc, &string_, "IntlMathematicalValueString::string");
}

bool js::intl::IntlMathematicalValue::isRepresentableAsDouble(
    double* result) const {
  if (value_.isNumber()) {
    *result = value_.toNumber();
    return true;
  }
  if (value_.isBigInt()) {
    int64_t i64;
    if (JS::BigInt::isInt64(value_.toBigInt(), &i64) &&
        i64 < int64_t(DOUBLE_INTEGRAL_PRECISION_LIMIT) &&
        i64 > -int64_t(DOUBLE_INTEGRAL_PRECISION_LIMIT)) {
      *result = double(i64);
      return true;
    }
  }
  return false;
}

JSLinearString* js::intl::IntlMathematicalValue::toLinearString(
    JSContext* cx) const {
  if (value_.isInt32()) {
    return Int32ToString<CanGC>(cx, value_.toInt32());
  }

  if (value_.isDouble()) {
    // Special case to preserve negative zero.
    if (mozilla::IsNegativeZero(value_.toDouble())) {
      constexpr std::string_view negativeZero = "-0";
      return NewStringCopy<CanGC>(cx, negativeZero);
    }

    auto* str = NumberToString<CanGC>(cx, value_.toNumber());
    if (!str) {
      return nullptr;
    }
    return str->ensureLinear(cx);
  }

  if (value_.isBigInt()) {
    Rooted<JS::BigInt*> bigInt(cx, value_.toBigInt());
    return BigInt::toString<CanGC>(cx, bigInt, 10);
  }

  return value_.toString()->ensureLinear(cx);
};

// Return the number part of the input by removing leading and trailing
// whitespace.
template <typename CharT>
static mozilla::Span<const CharT> NumberPart(mozilla::Span<const CharT> chars) {
  const CharT* start = chars.data();
  const CharT* end = chars.data() + chars.size();

  start = SkipSpace(start, end);

  // |SkipSpace| only supports forward iteration, so inline the backwards
  // iteration here.
  MOZ_ASSERT(start <= end);
  while (end > start && unicode::IsSpace(end[-1])) {
    end--;
  }

  // The number part is a non-empty, ASCII-only substring.
  MOZ_ASSERT(start < end);
  MOZ_ASSERT(mozilla::IsAscii(mozilla::Span(start, end)));

  return {start, end};
}

IntlMathematicalValueStringView js::intl::IntlMathematicalValueString::asView(
    JSContext* cx, const JS::AutoCheckCannotGC& nogc) const {
  MOZ_ASSERT(string_ != nullptr);

  if (string_->hasLatin1Chars()) {
    auto span = NumberPart(mozilla::AsChars(string_->latin1Range(nogc)));

    auto view = std::string_view{span.data(), span.size()};
    return IntlMathematicalValueStringView{view};
  }

  auto span = NumberPart(mozilla::Span{string_->twoByteRange(nogc)});

  JS::UniqueChars latin1{
      JS::LossyTwoByteCharsToNewLatin1CharsZ(cx, span).c_str()};
  if (!latin1) {
    return IntlMathematicalValueStringView{};
  }

  auto view = std::string_view{latin1.get(), span.size()};
  return IntlMathematicalValueStringView{view, std::move(latin1)};
}

// Return true if the string starts with "0[bBoOxX]", possibly skipping over
// leading whitespace.
template <typename CharT>
static bool IsNonDecimalNumber(mozilla::Range<const CharT> chars) {
  const CharT* end = chars.end().get();
  const CharT* start = SkipSpace(chars.begin().get(), end);

  if (end - start >= 2 && start[0] == '0') {
    CharT ch = start[1];
    return ch == 'b' || ch == 'B' || ch == 'o' || ch == 'O' || ch == 'x' ||
           ch == 'X';
  }
  return false;
}

static bool IsNonDecimalNumber(const JSLinearString* str) {
  JS::AutoCheckCannotGC nogc;
  return str->hasLatin1Chars() ? IsNonDecimalNumber(str->latin1Range(nogc))
                               : IsNonDecimalNumber(str->twoByteRange(nogc));
}

/**
 * 15.5.16 ToIntlMathematicalValue ( value )
 *
 * ES2024 Intl draft rev 74ca7099f103d143431b2ea422ae640c6f43e3e6
 */
static bool ToIntlMathematicalValue(JSContext* cx,
                                    JS::MutableHandle<JS::Value> value) {
  // Step 1.
  if (!ToPrimitive(cx, JSTYPE_NUMBER, value)) {
    return false;
  }

  // Step 2.
  if (value.isBigInt()) {
    return true;
  }

  // Step 4.
  if (!value.isString()) {
    // Step 4.a. (Steps 4.b-10 not applicable in our implementation.)
    return ToNumber(cx, value);
  }

  // Step 3.
  auto* str = value.toString()->ensureLinear(cx);
  if (!str) {
    return false;
  }

  // Steps 5-6, 8, and 9.a.
  double number = LinearStringToNumber(str);

  // Step 7.
  if (std::isnan(number)) {
    // Set to NaN if the input can't be parsed as a number.
    value.setNaN();
    return true;
  }

  // Step 9.
  if (number == 0.0 || std::isinf(number)) {
    // Step 9.a. (Reordered)

    // Steps 9.b-e.
    value.setDouble(number);
    return true;
  }

  // Step 10.
  if (IsNonDecimalNumber(str)) {
    // ICU doesn't accept non-decimal numbers, so we have to convert the input
    // into a base-10 string.

    MOZ_ASSERT(!mozilla::IsNegative(number),
               "non-decimal numbers can't be negative");

    if (number < DOUBLE_INTEGRAL_PRECISION_LIMIT) {
      // Fast-path if we can guarantee there was no loss of precision.
      value.setDouble(number);
    } else {
      // For the slow-path convert the string into a BigInt.

      // StringToBigInt can't fail (other than OOM) when StringToNumber already
      // succeeded.
      JS::Rooted<JSString*> rooted(cx, str);
      JS::BigInt* bi;
      JS_TRY_VAR_OR_RETURN_FALSE(cx, bi, StringToBigInt(cx, rooted));
      MOZ_ASSERT(bi);

      value.setBigInt(bi);
    }
  }
  return true;
}

bool js::intl::ToIntlMathematicalValue(
    JSContext* cx, JS::Handle<JS::Value> value,
    JS::MutableHandle<IntlMathematicalValue> result) {
  JS::Rooted<JS::Value> val(cx, value);
  if (!::ToIntlMathematicalValue(cx, &val)) {
    return false;
  }
  result.set(IntlMathematicalValue{val});
  return true;
}
