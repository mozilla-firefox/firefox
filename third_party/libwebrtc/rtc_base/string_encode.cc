/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/string_encode.h"

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "rtc_base/checks.h"

namespace webrtc {

/////////////////////////////////////////////////////////////////////////////
// String Encoding Utilities
/////////////////////////////////////////////////////////////////////////////

namespace {
constexpr absl::string_view kHEX = "0123456789abcdef";

// Convert an unsigned value from 0 to 15 to the hex character equivalent...
char HexEncode(uint8_t val) {
  RTC_DCHECK_LT(val, 16);
  return (val < 16) ? kHEX[val] : '!';
}

// ...and vice-versa.
bool HexDecode(char ch, uint8_t* val) {
  if ((ch >= '0') && (ch <= '9')) {
    *val = ch - '0';
  } else if ((ch >= 'A') && (ch <= 'F')) {
    *val = (ch - 'A') + 10;
  } else if ((ch >= 'a') && (ch <= 'f')) {
    *val = (ch - 'a') + 10;
  } else {
    return false;
  }
  return true;
}

size_t HexEncodeOutputLength(size_t srclen, char delimiter) {
  return delimiter && srclen > 0 ? (srclen * 3 - 1) : (srclen * 2);
}

// HexEncode shows the hex representation of binary data in ascii, with
// `delimiter` between bytes, or none if `delimiter` == 0.
void HexEncodeWithDelimiter(std::string& buffer,
                            std::span<const uint8_t> source,
                            char delimiter) {
  // Init and check bounds.
  size_t srcpos = 0, bufpos = 0;

  size_t srclen = source.size();
  while (srcpos < srclen) {
    uint8_t ch = source[srcpos++];
    buffer[bufpos] = HexEncode((ch >> 4) & 0xF);
    buffer[bufpos + 1] = HexEncode((ch) & 0xF);
    bufpos += 2;

    // Don't write a delimiter after the last byte.
    if (delimiter && (srcpos < srclen)) {
      buffer[bufpos] = delimiter;
      ++bufpos;
    }
  }
}

// Shared implementation of the tokenize() overloads. `T` is either std::string
// or absl::string_view.
template <typename T>
size_t Tokenize(absl::string_view source,
                char delimiter,
                std::vector<T>& fields) {
  fields.clear();
  size_t last = 0;
  for (size_t i = 0; i < source.length(); ++i) {
    if (source[i] == delimiter) {
      if (i != last) {
        fields.emplace_back(source.substr(last, i - last));
      }
      last = i + 1;
    }
  }
  if (last != source.length()) {
    fields.emplace_back(source.substr(last, source.length() - last));
  }
  return fields.size();
}

}  // namespace

std::string HexEncode(std::span<const uint8_t> source) {
  return HexEncodeWithDelimiter(source, 0);
}

std::string HexEncodeWithDelimiter(std::span<const uint8_t> source,
                                   char delimiter) {
  std::string s(HexEncodeOutputLength(source.size(), delimiter), 0);
  HexEncodeWithDelimiter(s, source, delimiter);
  return s;
}

std::span<uint8_t> HexDecodeWithDelimiter(std::span<uint8_t> buffer,
                                          absl::string_view source,
                                          char delimiter) {
  if (buffer.empty()) {
    return {};
  }

  // Init and bounds check.
  size_t srcpos = 0, bufpos = 0;
  size_t srclen = source.length();

  size_t needed = (delimiter) ? (srclen + 1) / 3 : srclen / 2;
  if (buffer.size() < needed)
    return {};

  while (srcpos < srclen) {
    if ((srclen - srcpos) < 2) {
      // This means we have an odd number of bytes.
      return {};
    }

    uint8_t h1, h2;
    if (!HexDecode(source[srcpos], &h1) ||
        !HexDecode(source[srcpos + 1], &h2)) {
      return {};
    }

    buffer[bufpos++] = (h1 << 4) | h2;
    srcpos += 2;

    // Remove the delimiter if needed.
    if (delimiter && (srclen - srcpos) > 1) {
      if (source[srcpos] != delimiter) {
        return {};
      }
      ++srcpos;
    }
  }

  return buffer.first(bufpos);
}

std::span<uint8_t> HexDecode(std::span<uint8_t> buffer,
                             absl::string_view source) {
  return HexDecodeWithDelimiter(buffer, source, 0);
}

size_t tokenize(absl::string_view source,
                char delimiter,
                std::vector<std::string>* fields) {
  return Tokenize(source, delimiter, *fields);
}

size_t tokenize(absl::string_view source,
                char delimiter,
                std::vector<absl::string_view>* fields) {
  return Tokenize(source, delimiter, *fields);
}

bool tokenize_first(absl::string_view source,
                    const char delimiter,
                    absl::string_view* token,
                    absl::string_view* rest) {
  // Find the first delimiter
  size_t left_pos = source.find(delimiter);
  if (left_pos == absl::string_view::npos) {
    return false;
  }

  // Look for additional occurrences of delimiter.
  size_t right_pos = left_pos + 1;
  while (right_pos < source.size() && source[right_pos] == delimiter) {
    right_pos++;
  }

  *token = source.substr(0, left_pos);
  *rest = source.substr(right_pos);
  return true;
}

bool tokenize_first(absl::string_view source,
                    const char delimiter,
                    std::string* token,
                    std::string* rest) {
  absl::string_view token_view;
  absl::string_view rest_view;
  if (!tokenize_first(source, delimiter, &token_view, &rest_view)) {
    return false;
  }
  *token = token_view;
  *rest = rest_view;
  return true;
}

bool FromString(absl::string_view s, bool* b) {
  if (s == "false") {
    *b = false;
    return true;
  }
  if (s == "true") {
    *b = true;
    return true;
  }
  return false;
}

}  // namespace webrtc
