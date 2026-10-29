/*
 *  Copyright 2018 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/strings/string_builder.h"

#include <cstring>
#include <string>

#include "absl/strings/string_view.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

class StructWithAbslStringify {
 public:
  explicit StructWithAbslStringify(absl::string_view sv) : value_(sv) {}

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const StructWithAbslStringify& self) {
    sink.Append(self.value_);
  }

 private:
  std::string value_;
};



////////////////////////////////////////////////////////////////////////////////
// StringBuilder.

TEST(StringBuilder, Limit) {
  StringBuilder sb;
  EXPECT_EQ(0u, sb.str().size());

  sb << "012345678";
  EXPECT_EQ(sb.str(), "012345678");
}

TEST(StringBuilder, NumbersAndChars) {
  StringBuilder sb;
  sb << 1 << ":" << 2.1 << ":" << 2.2f << ":" << 78187493520ll << ":"
     << 78187493520ul;
  EXPECT_THAT(sb.str(),
              ::testing::MatchesRegex("1:2.10*:2.20*:78187493520:78187493520"));
}

TEST(StringBuilder, Format) {
  StringBuilder sb;
  sb << "Here we go - ";
  sb.AppendFormat("This is a hex formatted value: 0x%08llx", 3735928559ULL);
  EXPECT_EQ(sb.str(), "Here we go - This is a hex formatted value: 0xdeadbeef");
}

TEST(StringBuilder, StdString) {
  StringBuilder sb;
  std::string str = "does this work?";
  sb << str;
  EXPECT_EQ(str, sb.str());
}

TEST(StringBuilder, CanUseAbslStringForCustomTypes) {
  StringBuilder sb;
  StructWithAbslStringify value("absl-stringify");
  sb << value;
  EXPECT_EQ(sb.str(), "absl-stringify");
}

TEST(StringBuilder, Release) {
  StringBuilder sb;
  std::string str =
      "This string has to be of a moderate length, or we might "
      "run into problems with small object optimizations.";
  EXPECT_LT(sizeof(str), str.size());
  sb << str;
  EXPECT_EQ(str, sb.str());
  const char* original_buffer = sb.str().c_str();
  std::string moved = sb.Release();
  EXPECT_TRUE(sb.str().empty());
  EXPECT_EQ(str, moved);
  EXPECT_EQ(original_buffer, moved.c_str());
}

TEST(StringBuilder, Reset) {
  StringBuilder sb("abc");
  sb << "def";
  EXPECT_EQ("abcdef", sb.str());
  sb.Clear();
  EXPECT_TRUE(sb.str().empty());
  sb << 123 << "!";
  EXPECT_EQ("123!", sb.str());
}

TEST(StringBuilder, Reserve) {
  constexpr size_t kCapacity = 100;
  StringBuilder sb;
  sb.Reserve(kCapacity);
  EXPECT_EQ(sb.size(), 0u);

  // Appending up to the reserved capacity does not reallocate the buffer.
  const char* buffer = sb.str().c_str();
  const std::string piece(10, 'x');
  for (size_t i = 0; i < kCapacity / piece.size(); ++i) {
    sb << piece;
    EXPECT_EQ(sb.str().c_str(), buffer);
  }
  EXPECT_EQ(sb.str(), std::string(kCapacity, 'x'));
}

TEST(StringBuilder, ReservePreservesContent) {
  StringBuilder sb("abc");
  sb.Reserve(100);
  EXPECT_EQ(sb.str(), "abc");
  sb << "def";
  EXPECT_EQ(sb.str(), "abcdef");
}

}  // namespace
}  // namespace webrtc
