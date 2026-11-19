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

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;
using ::testing::SizeIs;

class HexEncodeTest : public ::testing::Test {
 public:
  HexEncodeTest() {
    for (size_t i = 0; i < sizeof(data_); ++i) {
      data_[i] = (i + 128) & 0xff;
    }
    std::ranges::fill(decoded_, 0x7f);
  }

  std::array<uint8_t, 10> data_;
  std::array<uint8_t, 11> decoded_;
};

// Test that we can convert to/from hex with no delimiter.
TEST_F(HexEncodeTest, TestWithNoDelimiter) {
  std::string encoded = HexEncode(data_);
  EXPECT_EQ(encoded, "80818283848586878889");
  EXPECT_THAT(HexDecode(decoded_, encoded), ElementsAreArray(data_));
}

// Test that we can convert to/from hex with a colon delimiter.
TEST_F(HexEncodeTest, TestWithDelimiter) {
  std::string encoded = HexEncodeWithDelimiter(data_, ':');
  EXPECT_EQ(encoded, "80:81:82:83:84:85:86:87:88:89");
  EXPECT_THAT(HexDecodeWithDelimiter(decoded_, encoded, ':'),
              ElementsAreArray(data_));
}

// Test that encoding with one delimiter and decoding with another fails.
TEST_F(HexEncodeTest, TestWithWrongDelimiter) {
  std::string encoded = HexEncodeWithDelimiter(data_, ':');
  EXPECT_THAT(HexDecodeWithDelimiter(decoded_, encoded, '/'), IsEmpty());
}

// Test that encoding without a delimiter and decoding with one fails.
TEST_F(HexEncodeTest, TestExpectedDelimiter) {
  std::string encoded = HexEncode(data_);
  EXPECT_THAT(encoded, SizeIs(2 * std::size(data_)));
  EXPECT_THAT(HexDecodeWithDelimiter(decoded_, encoded, ':'), IsEmpty());
}

// Test that encoding with a delimiter and decoding without one fails.
TEST_F(HexEncodeTest, TestExpectedNoDelimiter) {
  std::string encoded = HexEncodeWithDelimiter(data_, ':');
  EXPECT_THAT(encoded, SizeIs(3 * std::size(data_) - 1));
  EXPECT_THAT(HexDecode(decoded_, encoded), IsEmpty());
}

// Test that we handle a zero-length buffer with no delimiter.
TEST_F(HexEncodeTest, TestZeroLengthNoDelimiter) {
  std::string encoded = HexEncode({});
  EXPECT_THAT(encoded, IsEmpty());
  EXPECT_THAT(HexDecode(decoded_, encoded), IsEmpty());
}

// Test that we handle a zero-length buffer with a delimiter.
TEST_F(HexEncodeTest, TestZeroLengthWithDelimiter) {
  std::string encoded = HexEncodeWithDelimiter({}, ':');
  EXPECT_THAT(encoded, IsEmpty());
  EXPECT_THAT(HexDecodeWithDelimiter(decoded_, encoded, ':'), IsEmpty());
}

// Test that decoding into a too-small output buffer fails.
TEST_F(HexEncodeTest, TestDecodeTooShort) {
  EXPECT_THAT(HexDecode(std::span(decoded_).first(4), "0123456789"), IsEmpty());
  ASSERT_EQ(decoded_[4], 0x7f);
}

// Test that decoding non-hex data fails.
TEST_F(HexEncodeTest, TestDecodeBogusData) {
  EXPECT_THAT(HexDecode(decoded_, "axyz"), IsEmpty());
}

// Test that decoding an odd number of hex characters fails.
TEST_F(HexEncodeTest, TestDecodeOddHexDigits) {
  EXPECT_THAT(HexDecode(decoded_, "012"), IsEmpty());
}

// Test that decoding a string with too many delimiters fails.
TEST_F(HexEncodeTest, TestDecodeWithDelimiterTooManyDelimiters) {
  EXPECT_THAT(HexDecodeWithDelimiter(std::span(decoded_).first(4),
                                     "01::23::45::67", ':'),
              IsEmpty());
}

// Test that decoding a string with a leading delimiter fails.
TEST_F(HexEncodeTest, TestDecodeWithDelimiterLeadingDelimiter) {
  EXPECT_THAT(
      HexDecodeWithDelimiter(std::span(decoded_).first(4), ":01:23:45:67", ':'),
      IsEmpty());
}

// Test that decoding a string with a trailing delimiter fails.
TEST_F(HexEncodeTest, TestDecodeWithDelimiterTrailingDelimiter) {
  EXPECT_THAT(
      HexDecodeWithDelimiter(std::span(decoded_).first(4), "01:23:45:67:", ':'),
      IsEmpty());
}

// Tests counting substrings.
TEST(TokenizeTest, CountSubstrings) {
  std::vector<std::string> fields;

  EXPECT_EQ(5ul, tokenize("one two three four five", ' ', &fields));
  fields.clear();
  EXPECT_EQ(1ul, tokenize("one", ' ', &fields));

  // Extra spaces should be ignored.
  fields.clear();
  EXPECT_EQ(5ul, tokenize("  one    two  three    four five  ", ' ', &fields));
  fields.clear();
  EXPECT_EQ(1ul, tokenize("  one  ", ' ', &fields));
  fields.clear();
  EXPECT_EQ(0ul, tokenize(" ", ' ', &fields));
}

// Tests comparing substrings.
TEST(TokenizeTest, CompareSubstrings) {
  std::vector<std::string> fields;

  tokenize("find middle one", ' ', &fields);
  ASSERT_EQ(3ul, fields.size());
  ASSERT_STREQ("middle", fields.at(1).c_str());
  fields.clear();

  // Extra spaces should be ignored.
  tokenize("  find   middle  one    ", ' ', &fields);
  ASSERT_EQ(3ul, fields.size());
  ASSERT_STREQ("middle", fields.at(1).c_str());
  fields.clear();
  tokenize(" ", ' ', &fields);
  ASSERT_EQ(0ul, fields.size());
}

TEST(TokenizeFirstTest, NoLeadingSpaces) {
  std::string token;
  std::string rest;

  ASSERT_TRUE(tokenize_first("A &*${}", ' ', &token, &rest));
  ASSERT_STREQ("A", token.c_str());
  ASSERT_STREQ("&*${}", rest.c_str());

  ASSERT_TRUE(tokenize_first("A B& *${}", ' ', &token, &rest));
  ASSERT_STREQ("A", token.c_str());
  ASSERT_STREQ("B& *${}", rest.c_str());

  ASSERT_TRUE(tokenize_first("A    B& *${}    ", ' ', &token, &rest));
  ASSERT_STREQ("A", token.c_str());
  ASSERT_STREQ("B& *${}    ", rest.c_str());
}

TEST(TokenizeFirstTest, LeadingSpaces) {
  std::string token;
  std::string rest;

  ASSERT_TRUE(tokenize_first("     A B C", ' ', &token, &rest));
  ASSERT_STREQ("", token.c_str());
  ASSERT_STREQ("A B C", rest.c_str());

  ASSERT_TRUE(tokenize_first("     A    B   C    ", ' ', &token, &rest));
  ASSERT_STREQ("", token.c_str());
  ASSERT_STREQ("A    B   C    ", rest.c_str());
}

TEST(TokenizeFirstTest, SingleToken) {
  std::string token;
  std::string rest;

  // In the case where we cannot find delimiter the whole string is a token.
  ASSERT_FALSE(tokenize_first("ABC", ' ', &token, &rest));

  ASSERT_TRUE(tokenize_first("ABC    ", ' ', &token, &rest));
  ASSERT_STREQ("ABC", token.c_str());
  ASSERT_STREQ("", rest.c_str());

  ASSERT_TRUE(tokenize_first("    ABC    ", ' ', &token, &rest));
  ASSERT_STREQ("", token.c_str());
  ASSERT_STREQ("ABC    ", rest.c_str());
}

TEST(TokenizeTest, StringViewFieldsPointIntoSource) {
  constexpr absl::string_view kSource = "  find   middle  one    ";
  std::vector<absl::string_view> fields;
  EXPECT_EQ(3u, tokenize(kSource, ' ', &fields));
  EXPECT_THAT(fields, ElementsAre("find", "middle", "one"));
  EXPECT_EQ(fields[1].data(), kSource.substr(9).data());

  EXPECT_EQ(0u, tokenize(" ", ' ', &fields));
  EXPECT_THAT(fields, IsEmpty());
  EXPECT_EQ(0u, tokenize("", ' ', &fields));
  EXPECT_THAT(fields, IsEmpty());
}

TEST(TokenizeFirstTest, StringViewOutputs) {
  constexpr absl::string_view kSource = "A    B& *${}    ";
  absl::string_view token;
  absl::string_view rest;
  ASSERT_TRUE(tokenize_first(kSource, ' ', &token, &rest));
  EXPECT_EQ(token, "A");
  EXPECT_EQ(rest, "B& *${}    ");
  EXPECT_EQ(rest.data(), kSource.substr(5).data());

  ASSERT_TRUE(tokenize_first("    ABC", ' ', &token, &rest));
  EXPECT_EQ(token, "");
  EXPECT_EQ(rest, "ABC");

  ASSERT_TRUE(tokenize_first("ABC    ", ' ', &token, &rest));
  EXPECT_EQ(token, "ABC");
  EXPECT_EQ(rest, "");

  // The outputs are left untouched when the delimiter is not found.
  EXPECT_FALSE(tokenize_first("XYZ", ' ', &token, &rest));
  EXPECT_EQ(token, "ABC");
  EXPECT_EQ(rest, "");

  // The same view can be used as input and output.
  rest = "IN IP4 1.2.3.4";
  ASSERT_TRUE(tokenize_first(rest, ' ', &token, &rest));
  EXPECT_EQ(token, "IN");
  EXPECT_EQ(rest, "IP4 1.2.3.4");
}

// Tests counting substrings.
TEST(SplitTest, CountSubstrings) {
  EXPECT_EQ(5ul, split("one,two,three,four,five", ',').size());
  EXPECT_EQ(1ul, split("one", ',').size());

  // Empty fields between commas count.
  EXPECT_EQ(5ul, split("one,,three,four,five", ',').size());
  EXPECT_EQ(3ul, split(",three,", ',').size());
  EXPECT_EQ(1ul, split("", ',').size());
}

// Tests comparing substrings.
TEST(SplitTest, CompareSubstrings) {
  std::vector<absl::string_view> fields = split("find,middle,one", ',');
  ASSERT_EQ(3ul, fields.size());
  ASSERT_EQ("middle", fields.at(1));

  // Empty fields between commas count.
  fields = split("find,,middle,one", ',');
  ASSERT_EQ(4ul, fields.size());
  ASSERT_EQ("middle", fields.at(2));
  fields = split("", ',');
  ASSERT_EQ(1ul, fields.size());
  ASSERT_EQ("", fields.at(0));
}

TEST(SplitTest, EmptyTokens) {
  std::vector<absl::string_view> fields = split("a.b.c", '.');
  ASSERT_EQ(3ul, fields.size());
  EXPECT_EQ("a", fields[0]);
  EXPECT_EQ("b", fields[1]);
  EXPECT_EQ("c", fields[2]);

  fields = split("..c", '.');
  ASSERT_EQ(3ul, fields.size());
  EXPECT_TRUE(fields[0].empty());
  EXPECT_TRUE(fields[1].empty());
  EXPECT_EQ("c", fields[2]);

  fields = split("", '.');
  ASSERT_EQ(1ul, fields.size());
  EXPECT_TRUE(fields[0].empty());
}

TEST(SplitTest, SplitsIntoGivenContainer) {
  using Fields = absl::InlinedVector<absl::string_view, 2>;
  EXPECT_THAT(split<Fields>("a b", ' '), ElementsAre("a", "b"));
  // More fields than the inline capacity, and empty fields.
  EXPECT_THAT(split<Fields>(" a  b", ' '), ElementsAre("", "a", "", "b"));
  EXPECT_THAT(split<Fields>("", ' '), ElementsAre(""));
}

template <typename T>
void ParsesTo(std::string s, T t) {
  T value;
  EXPECT_TRUE(FromString(s, &value));
  EXPECT_EQ(value, t);
}

TEST(FromString, DecodeValid) {
  ParsesTo("true", true);
  ParsesTo("false", false);

  ParsesTo("105", 105);
  ParsesTo("0.25", 0.25);
}

template <typename T>
void FailsToParse(std::string s) {
  T value;
  EXPECT_FALSE(FromString(s, &value)) << "[" << s << "]";
}

TEST(FromString, DecodeInvalid) {
  FailsToParse<bool>("True");
  FailsToParse<bool>("0");
  FailsToParse<bool>("yes");

  FailsToParse<int>("0.5");
  FailsToParse<int>("XIV");
  FailsToParse<double>("");
  FailsToParse<double>("  ");
  FailsToParse<int>("1 2");
}

}  // namespace webrtc
