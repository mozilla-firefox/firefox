/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <iterator>

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

TEST(HtmlSimd, TestCollectLessThanAndAmpersand)
{
  using mozilla::htmlaccel::CollectLessThanAndAmpersand;
  char16_t input[48];
  std::fill(std::begin(input), std::end(input), u'a');
  input[2] = '<';
  input[17] = '&';
  // Only share the low byte with "<" and "&".
  input[20] = 0x203C;
  input[21] = 0x2026;
  input[35] = '\r';
  uint32_t offsets[64];

  uint32_t* out = offsets;
  const char16_t* stop = CollectLessThanAndAmpersand(input, input, input + 48,
                                                     out, std::end(offsets));
  ASSERT_EQ(stop, input + 35);
  ASSERT_EQ(out - offsets, 2);
  ASSERT_EQ(offsets[0], 2u);
  ASSERT_EQ(offsets[1], 17u);

  // Only full strides are scanned.
  out = offsets;
  stop = CollectLessThanAndAmpersand(input, input, input + 40, out,
                                     std::end(offsets));
  ASSERT_EQ(stop, input + 32);
  ASSERT_EQ(out - offsets, 2);

  // And only while their offsets are sure to fit.
  out = offsets;
  stop =
      CollectLessThanAndAmpersand(input, input, input + 48, out, offsets + 15);
  ASSERT_EQ(stop, input);
  ASSERT_EQ(out, offsets);

  input[35] = 0;
  out = offsets;
  stop = CollectLessThanAndAmpersand(input, input + 16, input + 48, out,
                                     std::end(offsets));
  ASSERT_EQ(stop, input + 35);
  ASSERT_EQ(out - offsets, 1);
  ASSERT_EQ(offsets[0], 17u);
}

TEST(HtmlSimd, TestSkipAttributeValue)
{
  using namespace mozilla::htmlaccel;
  char16_t input[40];
  std::fill(std::begin(input), std::end(input), u'a');
  // Only share the low byte with the double quote, apostrophe and ampersand.
  input[3] = 0x2022;
  input[4] = 0x2027;
  input[6] = 0x2026;
  ASSERT_EQ(SkipAttributeValueDoubleQuoted(input, input + 40), 32u);
  ASSERT_EQ(SkipAttributeValueSingleQuoted(input, input + 40), 32u);
  ASSERT_EQ(SkipAttributeValueDoubleQuoted(input, input + 15), 0u);

  input[20] = '"';
  ASSERT_EQ(SkipAttributeValueDoubleQuoted(input, input + 40), 20u);
  ASSERT_EQ(SkipAttributeValueSingleQuoted(input, input + 40), 32u);
  input[20] = '\'';
  ASSERT_EQ(SkipAttributeValueDoubleQuoted(input, input + 40), 32u);
  ASSERT_EQ(SkipAttributeValueSingleQuoted(input, input + 40), 20u);
  for (const char16_t stop : {u'&', u'\r', u'\0'}) {
    input[20] = stop;
    ASSERT_EQ(SkipAttributeValueDoubleQuoted(input, input + 40), 20u);
    ASSERT_EQ(SkipAttributeValueSingleQuoted(input, input + 40), 20u);
  }
}
