/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZ_DECIMAL_UTILS_H
#define MOZ_DECIMAL_UTILS_H

// This file contains extra includes, defines and typedefs to allow compilation
// of Decimal.cpp under the Mozilla source without blink core dependencies. Do
// not include it into any file other than Decimal.cpp.

#include "double-conversion/double-conversion.h"
#include "mozilla/Casting.h"
#include "mozilla/FloatingPoint.h"
#include "mozilla/Span.h"

#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

#ifndef UINT64_C
// For Android toolchain
#define UINT64_C(c) (c ## ULL)
#endif

#ifdef ASSERT
#undef ASSERT
#endif
#define ASSERT MOZ_ASSERT

#define ASSERT_NOT_REACHED() MOZ_ASSERT_UNREACHABLE("moz-decimal-utils.h")

#define STACK_ALLOCATED() DISALLOW_NEW()

#define WTF_MAKE_NONCOPYABLE(ClassName) \
  private: \
    ClassName(const ClassName&) = delete; \
    void operator=(const ClassName&) = delete;

typedef std::string String;

double mozToDouble(mozilla::Span<const char> aStr, bool *valid) {
  double_conversion::StringToDoubleConverter converter(
    double_conversion::StringToDoubleConverter::NO_FLAGS,
    mozilla::UnspecifiedNaN<double>(), mozilla::UnspecifiedNaN<double>(), nullptr, nullptr);
  const char* str = aStr.Elements();
  int length = mozilla::AssertedCast<int>(aStr.Length());
  int processed_char_count; // unused - NO_FLAGS requires the whole string to parse
  double result = converter.StringToDouble(str, length, &processed_char_count);
  *valid = std::isfinite(result);
  return result;
}

double mozToDouble(const String &aStr, bool *valid) {
  return mozToDouble(mozilla::MakeStringSpan(aStr.c_str()), valid);
}

String mozToString(double aNum) {
  char buffer[64];
  int buffer_length = std::size(buffer);
  const double_conversion::DoubleToStringConverter& converter =
    double_conversion::DoubleToStringConverter::EcmaScriptConverter();
  double_conversion::StringBuilder builder(buffer, buffer_length);
  converter.ToShortest(aNum, &builder);
  return String(builder.Finalize());
}

String mozToString(int64_t aNum) {
#ifdef _LIBCPP_VERSION
  std::ostringstream o;
  o << std::setprecision(std::numeric_limits<int64_t>::digits10) << aNum;
  return o.str();
#else
  return std::to_string(aNum);
#endif
}

String mozToString(uint64_t aNum) {
#ifdef _LIBCPP_VERSION
  std::ostringstream o;
  o << std::setprecision(std::numeric_limits<uint64_t>::digits10) << aNum;
  return o.str();
#else
  return std::to_string(aNum);
#endif
}

namespace moz_decimal_utils {

class StringBuilder
{
public:
  void append(char c) {
    mStr += c;
  }
  void appendLiteral(const char *aStr) {
    mStr += aStr;
  }
  void appendNumber(int aNum) {
    mStr += mozToString(int64_t(aNum));
  }
  void append(const String& aStr) {
    mStr += aStr;
  }
  std::string toString() const {
    return mStr;
  }
private:
  std::string mStr;
};

} // namespace moz_decimal_utils

#endif
