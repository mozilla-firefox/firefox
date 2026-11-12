/*
 *  Copyright 2018 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_STRINGS_STRING_BUILDER_H_
#define RTC_BASE_STRINGS_STRING_BUILDER_H_

#include <cstdio>
#include <string>
#include <utility>

#include "absl/strings/has_absl_stringify.h"
#include "absl/strings/string_view.h"
#include "rtc_base/system/rtc_export.h"

namespace webrtc {

namespace string_builder_internal {

struct StringBuilderSink {
  std::string& s;
  void Append(absl::string_view part) { s.append(part); }
};

// AbslFormatFlush is a customization point called by Abseil's formatting
// library (via ADL). Providing this direct implementation avoids intermediate
// std::string allocations or buffering overhead (e.g. from absl::StrCat),
// allowing Abseil to append formatted pieces directly to the sink's string.
inline void AbslFormatFlush(StringBuilderSink* sink, absl::string_view part) {
  sink->Append(part);
}

}  // namespace string_builder_internal

// A string builder that supports dynamic resizing while building a string.
// The class is based around an instance of std::string and allows moving
// ownership out of the class once the string has been built.
class RTC_EXPORT StringBuilder {
 public:
  StringBuilder() = default;
  explicit StringBuilder(absl::string_view s) : str_(s) {}
  StringBuilder(const StringBuilder&) = default;
  StringBuilder& operator=(const StringBuilder&) = default;

  StringBuilder& operator<<(absl::string_view str);
  StringBuilder& operator<<(char c);
  StringBuilder& operator<<(int i);
  StringBuilder& operator<<(unsigned i);
  StringBuilder& operator<<(long i);                // NOLINT
  StringBuilder& operator<<(long long i);           // NOLINT
  StringBuilder& operator<<(unsigned long i);       // NOLINT
  StringBuilder& operator<<(unsigned long long i);  // NOLINT
  StringBuilder& operator<<(float f);
  StringBuilder& operator<<(double f);

  template <typename T>
    requires absl::HasAbslStringify<T>::value
  StringBuilder& operator<<(const T& value) {
    string_builder_internal::StringBuilderSink sink{str_};
    AbslStringify(sink, value);
    return *this;
  }

  const std::string& str() const { return str_; }

  void Clear() { str_.clear(); }

  size_t size() const { return str_.size(); }

  // Pre-allocates capacity for a string of at least `size` characters, to
  // avoid repeated reallocations when the final size can be estimated.
  void Reserve(size_t size) { str_.reserve(size); }

  // Moves out the internal std::string.
  std::string Release() { return std::move(str_); }

  // Allows appending a printf style formatted string.
  StringBuilder& AppendFormat(const char* fmt, ...)
#if defined(__GNUC__)
      __attribute__((__format__(__printf__, 2, 3)))
#endif
      ;

 private:
  std::string str_;
};

}  //  namespace webrtc


#endif  // RTC_BASE_STRINGS_STRING_BUILDER_H_
