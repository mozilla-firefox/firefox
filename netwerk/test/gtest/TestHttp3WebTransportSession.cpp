/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Http3WebTransportSession.h"
#include "gtest/gtest.h"
#include "nsTArray.h"

using namespace mozilla;
using namespace mozilla::net;

static nsTArray<uint8_t> ToBytes(const char* aStr) {
  nsTArray<uint8_t> bytes;
  bytes.AppendElements(reinterpret_cast<const uint8_t*>(aStr), strlen(aStr));
  return bytes;
}

TEST(TestHttp3WebTransportSession, ParsesNormalHeaders)
{
  nsTArray<uint8_t> flat =
      ToBytes("HTTP/3 200\r\nfoo: bar\r\nwt-available-protocols: a\r\n\r\n");
  nsTArray<nsCString> names, values;
  ParseFlatWebTransportResponseHeaders(flat, names, values);

  ASSERT_EQ(names.Length(), 2u);
  EXPECT_TRUE(names[0].EqualsLiteral("foo"));
  EXPECT_TRUE(values[0].EqualsLiteral("bar"));
  EXPECT_TRUE(names[1].EqualsLiteral("wt-available-protocols"));
  EXPECT_TRUE(values[1].EqualsLiteral("a"));
}

TEST(TestHttp3WebTransportSession, StripsWtProtocolHeader)
{
  // https://w3c.github.io/webtransport/#process-a-webtransport-fetch-response
  // step 6 requires deleting wt-protocol before exposing responseHeaders.
  nsTArray<uint8_t> flat =
      ToBytes("HTTP/3 200\r\nwt-protocol: chat\r\nfoo: bar\r\n\r\n");
  nsTArray<nsCString> names, values;
  ParseFlatWebTransportResponseHeaders(flat, names, values);

  ASSERT_EQ(names.Length(), 1u);
  EXPECT_TRUE(names[0].EqualsLiteral("foo"));

  // Matching should be case-insensitive.
  flat = ToBytes("HTTP/3 200\r\nWT-Protocol: chat\r\nfoo: bar\r\n\r\n");
  names.Clear();
  values.Clear();
  ParseFlatWebTransportResponseHeaders(flat, names, values);
  ASSERT_EQ(names.Length(), 1u);
  EXPECT_TRUE(names[0].EqualsLiteral("foo"));
}

TEST(TestHttp3WebTransportSession, EmptyBuffer)
{
  nsTArray<uint8_t> flat;
  nsTArray<nsCString> names, values;
  ParseFlatWebTransportResponseHeaders(flat, names, values);
  EXPECT_EQ(names.Length(), 0u);
  EXPECT_EQ(values.Length(), 0u);
}

TEST(TestHttp3WebTransportSession, StatusLineOnlyNoHeaders)
{
  nsTArray<uint8_t> flat = ToBytes("HTTP/3 200\r\n\r\n");
  nsTArray<nsCString> names, values;
  ParseFlatWebTransportResponseHeaders(flat, names, values);
  EXPECT_EQ(names.Length(), 0u);
  EXPECT_EQ(values.Length(), 0u);
}

TEST(TestHttp3WebTransportSession, MissingSeparatorLineIsSkipped)
{
  // A header line with no ": " separator is silently dropped rather than
  // misparsed.
  nsTArray<uint8_t> flat =
      ToBytes("HTTP/3 200\r\nmalformedheader\r\nfoo: bar\r\n\r\n");
  nsTArray<nsCString> names, values;
  ParseFlatWebTransportResponseHeaders(flat, names, values);
  ASSERT_EQ(names.Length(), 1u);
  EXPECT_TRUE(names[0].EqualsLiteral("foo"));
}

TEST(TestHttp3WebTransportSession, EmptyHeaderValue)
{
  nsTArray<uint8_t> flat = ToBytes("HTTP/3 200\r\nfoo: \r\n\r\n");
  nsTArray<nsCString> names, values;
  ParseFlatWebTransportResponseHeaders(flat, names, values);
  ASSERT_EQ(names.Length(), 1u);
  EXPECT_TRUE(names[0].EqualsLiteral("foo"));
  EXPECT_TRUE(values[0].EqualsLiteral(""));
}
