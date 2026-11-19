/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_STRING_ENCODE_H_
#define RTC_BASE_STRING_ENCODE_H_

#include <stddef.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/strings/string_view.h"
#include "rtc_base/checks.h"
#include "rtc_base/span_helpers.h"
#include "rtc_base/string_to_number.h"
#include "rtc_base/strings/string_format.h"  // IWYU pragma: keep

namespace webrtc {

inline std::string BoolToString(bool b) {
  return b ? "true" : "false";
}

std::string HexEncode(std::span<const uint8_t> source);
std::string HexEncodeWithDelimiter(std::span<const uint8_t> source,
                                   char delimiter);

inline std::string hex_encode(absl::string_view str) {
  return HexEncode(AsUint8Span(str));
}

inline std::string hex_encode_with_delimiter(absl::string_view source,
                                             char delimiter) {
  return HexEncodeWithDelimiter(AsUint8Span(source), delimiter);
}

// hex_decode converts ascii hex to binary.
std::span<uint8_t> HexDecode(std::span<uint8_t> buffer,
                             absl::string_view source);

// hex_decode, assuming that there is a delimiter between every byte
// pair.
// `delimiter` == 0 means no delimiter
// Returns decoded binary which is subspan of `buffer`. Returns empty span if
// the buffer is too short or the data is invalid.
std::span<uint8_t> HexDecodeWithDelimiter(std::span<uint8_t> buffer,
                                          absl::string_view source,
                                          char delimiter);

[[deprecated]] inline size_t hex_decode_with_delimiter(std::span<char> buffer,
                                                       absl::string_view source,
                                                       char delimiter) {
  return HexDecodeWithDelimiter(AsWritableUint8Span(buffer), source, delimiter)
      .size();
}

// Splits the source string into multiple fields separated by delimiter,
// with duplicates of delimiter creating empty fields. Empty input produces a
// single, empty, field. The fields are views into `source`, which must outlive
// them. `Container` can be, for example, an absl::InlinedVector that holds the
// expected number of fields without a heap allocation.
template <typename Container = std::vector<absl::string_view>>
Container split(absl::string_view source, char delimiter) {
  Container fields;
  size_t start = 0;
  for (size_t end = source.find(delimiter); end != absl::string_view::npos;
       end = source.find(delimiter, start)) {
    fields.push_back(source.substr(start, end - start));
    start = end + 1;
  }
  fields.push_back(source.substr(start));
  return fields;
}

// Splits the source string into multiple fields separated by delimiter,
// with duplicates of delimiter ignored.  Trailing delimiter ignored.
// The absl::string_view version returns views into `source`, which must outlive
// `fields`.
size_t tokenize(absl::string_view source,
                char delimiter,
                std::vector<std::string>* fields);
size_t tokenize(absl::string_view source,
                char delimiter,
                std::vector<absl::string_view>* fields);

// Extract the first token from source as separated by delimiter, with
// duplicates of delimiter ignored. Return false if the delimiter could not be
// found, otherwise return true.
// The absl::string_view version returns views into `source`, which must outlive
// `token` and `rest`.
bool tokenize_first(absl::string_view source,
                    char delimiter,
                    std::string* token,
                    std::string* rest);
bool tokenize_first(absl::string_view source,
                    char delimiter,
                    absl::string_view* token,
                    absl::string_view* rest);

// Versions that behave differently from StrCat

// Versions not supported by StrCat:

template <typename T,
          typename std::enable_if<std::is_arithmetic<T>::value &&
                                      !std::is_same<T, bool>::value,
                                  int>::type = 0>
static bool FromString(absl::string_view s, T* t) {
  RTC_DCHECK(t);
  std::optional<T> result = StringToNumber<T>(s);

  if (result)
    *t = *result;

  return result.has_value();
}

bool FromString(absl::string_view s, bool* b);

template <typename T>
static inline T FromString(absl::string_view str) {
  T val;
  FromString(str, &val);
  return val;
}

//////////////////////////////////////////////////////////////////////

}  //  namespace webrtc


#endif  // RTC_BASE_STRING_ENCODE_H__
