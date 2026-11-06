/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>

#include "gtest/gtest.h"
#include "mozilla/htmlaccel/htmlaccelNotInline.h"

// Match in the first half
const char16_t HTML_SIMD_TEST_INPUT_LOW[16] = {
    'a',
    0xD834,  // Surrogate pair
    0xDD65, '\n', '<', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
};

// Match in the second half
const char16_t HTML_SIMD_TEST_INPUT_HIGH[16] = {
    'i',    'j',  'k', 'l', 'm', 'n', 'o', 'p', 'a',
    0xD834,  // Surrogate pair
    0xDD65, '\n', '<', 'f', 'g', 'h',
};

TEST(HtmlSimd, TestTextNodeAllowSurrogatesAndLf)
{
  int32_t index = mozilla::htmlaccel::AccelerateDataFastest(
      HTML_SIMD_TEST_INPUT_LOW, HTML_SIMD_TEST_INPUT_LOW + 16);
  ASSERT_EQ(index, 4);
}

TEST(HtmlSimd, TestTextNodeAllowSurrogatesDisallowLf)
{
  int32_t index = mozilla::htmlaccel::AccelerateDataViewSource(
      HTML_SIMD_TEST_INPUT_LOW, HTML_SIMD_TEST_INPUT_LOW + 16);
  ASSERT_EQ(index, 3);
}

TEST(HtmlSimd, TestTextNodeDisallowSurrogatesAndLf)
{
  int32_t index = mozilla::htmlaccel::AccelerateDataLineCol(
      HTML_SIMD_TEST_INPUT_LOW, HTML_SIMD_TEST_INPUT_LOW + 16);
  ASSERT_EQ(index, 1);
}

TEST(HtmlSimd, TestTextNodeAllowSurrogatesAndLfHigh)
{
  int32_t index = mozilla::htmlaccel::AccelerateDataFastest(
      HTML_SIMD_TEST_INPUT_HIGH, HTML_SIMD_TEST_INPUT_HIGH + 16);
  ASSERT_EQ(index, 4 + 8);
}

TEST(HtmlSimd, TestTextNodeAllowSurrogatesDisallowLfHigh)
{
  int32_t index = mozilla::htmlaccel::AccelerateDataViewSource(
      HTML_SIMD_TEST_INPUT_HIGH, HTML_SIMD_TEST_INPUT_HIGH + 16);
  ASSERT_EQ(index, 3 + 8);
}

TEST(HtmlSimd, TestTextNodeDisallowSurrogatesAndLfHigh)
{
  int32_t index = mozilla::htmlaccel::AccelerateDataLineCol(
      HTML_SIMD_TEST_INPUT_HIGH, HTML_SIMD_TEST_INPUT_HIGH + 16);
  ASSERT_EQ(index, 1 + 8);
}

TEST(HtmlSimd, TestWriteThrough)
{
  using namespace mozilla::htmlaccel;
  int32_t (*const functions[])(const char16_t*, const char16_t*, char16_t*) = {
      AccelerateCommentFastest,
      AccelerateCommentLineCol,
      AccelerateCommentViewSource,
      AccelerateAttributeValueSingleQuotedFastest,
      AccelerateAttributeValueSingleQuotedLineCol,
      AccelerateAttributeValueSingleQuotedViewSource,
      AccelerateAttributeValueDoubleQuotedFastest,
      AccelerateAttributeValueDoubleQuotedLineCol,
      AccelerateAttributeValueDoubleQuotedViewSource,
  };
  // Two full strides followed by a partial one.
  constexpr int32_t length = 40;
  constexpr int32_t fullStrides = length / 16 * 16;
  constexpr char16_t sentinel = 0xCACA;
  for (auto accelerate : functions) {
    for (int32_t stop = 0; stop <= length; stop++) {
      SCOPED_TRACE(stop);
      char16_t input[length + 1];
      for (int32_t i = 0; i <= length; i++) {
        input[i] = char16_t('a' + i % 26);
      }
      input[stop] = 0;
      char16_t output[length + 16];
      std::fill_n(output, length + 16, sentinel);
      int32_t advance = accelerate(input, input + length, output);
      ASSERT_EQ(advance, std::min(stop, fullStrides));
      ASSERT_TRUE(std::equal(input, input + advance, output));
      int32_t written = stop < fullStrides ? (stop / 16 + 1) * 16 : fullStrides;
      ASSERT_TRUE(std::all_of(output + written, output + length + 16,
                              [](char16_t c) { return c == sentinel; }));
    }
  }
}
