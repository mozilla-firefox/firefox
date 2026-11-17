/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <functional>

#include "Http2Compression.h"
#include "gtest/gtest.h"
#include "mozilla/Preferences.h"
#include "nsIEventTarget.h"
#include "nsNetCID.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"

using namespace mozilla;
using namespace mozilla::net;

static const char kRejectDuplicatePseudoHeadersPref[] =
    "network.http.http2.reject_duplicate_pseudo_headers";
static const char kMaxHeaderListSizePref[] =
    "network.http.http2.max_header_list_size";
// The Nightly default; set explicitly since other channels default to 0.
static const uint32_t kMaxHeaderListSize = 256 * 1024;

// HPACK static table entries (RFC 7541 Appendix A).
static const uint8_t kStatus200 = 0x88;
static const uint8_t kStatus204 = 0x89;
static const uint8_t kMethodGet = 0x82;
static const uint8_t kPathRoot = 0x84;
static const uint8_t kSchemeHttps = 0x87;

// Literal without indexing, name from static index 8 (:status), value "404".
static const uint8_t kLiteralStatus404[] = {0x08, 0x03, '4', '0', '4'};
// Literal without indexing, literal name "foo", value "bar".
static const uint8_t kLiteralFooBar[] = {0x00, 0x03, 'f', 'o', 'o',
                                         0x03, 'b',  'a', 'r'};

// First entry of the dynamic table.
static const uint8_t kDynamicIndex0 = 0xbe;

// Appends a literal with a literal name "x" and a value of aValueLength bytes.
// aPrefix is 0x40 for incremental indexing or 0x00 for no indexing.
static void AppendLiteral(nsTArray<uint8_t>& aBlock, uint8_t aPrefix,
                          uint32_t aValueLength) {
  const uint8_t name[] = {aPrefix, 0x01, 'x'};
  aBlock.AppendElements(name, sizeof(name));
  // 7-bit prefix integer encoding (RFC 7541 Section 5.1).
  if (aValueLength < 0x7f) {
    aBlock.AppendElement(aValueLength);
  } else {
    aBlock.AppendElement(0x7f);
    uint32_t rest = aValueLength - 0x7f;
    while (rest >= 0x80) {
      aBlock.AppendElement((rest & 0x7f) | 0x80);
      rest >>= 7;
    }
    aBlock.AppendElement(rest);
  }
  for (uint32_t i = 0; i < aValueLength; ++i) {
    aBlock.AppendElement('v');
  }
}

// The decompressor asserts that it runs on the socket thread.
static void RunOnSocketThread(std::function<void()>&& aFn) {
  nsCOMPtr<nsIEventTarget> sts =
      do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID);
  ASSERT_TRUE(sts);
  NS_DispatchAndSpinEventLoopUntilComplete(
      "TestHttp2Compression"_ns, sts,
      NS_NewRunnableFunction("TestHttp2Compression", std::move(aFn)));
}

static nsresult Decode(Http2Decompressor& aDecompressor,
                       const nsTArray<uint8_t>& aBlock, nsACString& aOutput,
                       bool aIsPush = false) {
  return aDecompressor.DecodeHeaderBlock(aBlock.Elements(), aBlock.Length(),
                                         aOutput, aIsPush);
}

class Http2DecompressorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Preferences::SetBool(kRejectDuplicatePseudoHeadersPref, true);
    Preferences::SetUint(kMaxHeaderListSizePref, kMaxHeaderListSize);
  }
  void TearDown() override {
    Preferences::ClearUser(kRejectDuplicatePseudoHeadersPref);
    Preferences::ClearUser(kMaxHeaderListSizePref);
  }
};

TEST_F(Http2DecompressorTest, SingleStatus) {
  RunOnSocketThread([]() {
    Http2Decompressor decompressor;
    nsAutoCString output;
    nsTArray<uint8_t> block{kStatus200};
    block.AppendElements(kLiteralFooBar, sizeof(kLiteralFooBar));
    ASSERT_EQ(Decode(decompressor, block, output), NS_OK);
    EXPECT_TRUE(output.EqualsLiteral("HTTP/2 200\r\nfoo: bar\r\n"));
    nsAutoCString status;
    decompressor.GetStatus(status);
    EXPECT_TRUE(status.EqualsLiteral("200"));
  });
}

TEST_F(Http2DecompressorTest, DuplicateIndexedStatusRejected) {
  RunOnSocketThread([]() {
    Http2Decompressor decompressor;
    nsAutoCString output;
    nsTArray<uint8_t> block{kStatus200, kStatus204};
    EXPECT_TRUE(NS_FAILED(Decode(decompressor, block, output)));
  });
}

TEST_F(Http2DecompressorTest, DuplicateLiteralStatusRejected) {
  RunOnSocketThread([]() {
    Http2Decompressor decompressor;
    nsAutoCString output;
    nsTArray<uint8_t> block{kStatus200};
    block.AppendElements(kLiteralStatus404, sizeof(kLiteralStatus404));
    EXPECT_TRUE(NS_FAILED(Decode(decompressor, block, output)));
  });
}

TEST_F(Http2DecompressorTest, ManyDuplicateStatusRejected) {
  RunOnSocketThread([]() {
    Http2Decompressor decompressor;
    nsAutoCString output;
    nsTArray<uint8_t> block;
    block.AppendElements(200000);
    memset(block.Elements(), kStatus200, block.Length());
    EXPECT_TRUE(NS_FAILED(Decode(decompressor, block, output)));
  });
}

TEST_F(Http2DecompressorTest, DuplicatePushPseudoHeaderRejected) {
  RunOnSocketThread([]() {
    Http2Decompressor decompressor;
    nsAutoCString output;
    nsTArray<uint8_t> block{kMethodGet, kSchemeHttps, kPathRoot};
    ASSERT_EQ(Decode(decompressor, block, output, true), NS_OK);

    block.AppendElement(kPathRoot);
    EXPECT_TRUE(NS_FAILED(Decode(decompressor, block, output, true)));
  });
}

TEST_F(Http2DecompressorTest, DuplicateStatusAllowedWithPrefDisabled) {
  Preferences::SetBool(kRejectDuplicatePseudoHeadersPref, false);
  RunOnSocketThread([]() {
    Http2Decompressor decompressor;
    nsAutoCString output;
    nsTArray<uint8_t> block{kStatus200, kStatus204};
    ASSERT_EQ(Decode(decompressor, block, output), NS_OK);
    EXPECT_TRUE(output.EqualsLiteral("HTTP/2 204\r\nHTTP/2 200\r\n"));
  });
}

TEST_F(Http2DecompressorTest, PseudoHeadersResetBetweenBlocks) {
  RunOnSocketThread([]() {
    Http2Decompressor decompressor;
    nsAutoCString output;
    nsTArray<uint8_t> block{kStatus200};
    ASSERT_EQ(Decode(decompressor, block, output), NS_OK);
    ASSERT_EQ(Decode(decompressor, block, output), NS_OK);
  });
}

// Each field counts as name + value + 32 bytes (RFC 9113 Section 6.5.2), so
// ":status: 200" uses 42 bytes and a literal "x" field with an N-byte value
// uses N + 33 bytes.
static const uint32_t kValueLengthAtLimit = kMaxHeaderListSize - 42 - 33;

TEST_F(Http2DecompressorTest, HeaderListAtLimitAccepted) {
  RunOnSocketThread([]() {
    Http2Decompressor decompressor;
    nsAutoCString output;
    nsTArray<uint8_t> block{kStatus200};
    AppendLiteral(block, 0x00, kValueLengthAtLimit);
    EXPECT_EQ(Decode(decompressor, block, output), NS_OK);
  });
}

TEST_F(Http2DecompressorTest, HeaderListOverLimitRejected) {
  RunOnSocketThread([]() {
    Http2Decompressor decompressor;
    nsAutoCString output;
    nsTArray<uint8_t> block{kStatus200};
    AppendLiteral(block, 0x00, kValueLengthAtLimit + 1);
    EXPECT_EQ(Decode(decompressor, block, output), NS_ERROR_ILLEGAL_VALUE);
  });
}

TEST_F(Http2DecompressorTest, HeaderListOverLimitFromDynamicTableRejected) {
  RunOnSocketThread([]() {
    Http2Decompressor decompressor;
    nsAutoCString output;
    nsTArray<uint8_t> block{kStatus200};
    AppendLiteral(block, 0x40, 4000);
    // 1-byte references to the 4033-byte dynamic table entry.
    for (int i = 0; i < 1000; ++i) {
      block.AppendElement(kDynamicIndex0);
    }
    EXPECT_EQ(Decode(decompressor, block, output), NS_ERROR_ILLEGAL_VALUE);
    EXPECT_LE(output.Length(), kMaxHeaderListSize);
  });
}

TEST_F(Http2DecompressorTest, HeaderListLimitDisabled) {
  Preferences::SetUint(kMaxHeaderListSizePref, 0);
  RunOnSocketThread([]() {
    Http2Decompressor decompressor;
    nsAutoCString output;
    nsTArray<uint8_t> block{kStatus200};
    AppendLiteral(block, 0x00, kValueLengthAtLimit + 1);
    EXPECT_EQ(Decode(decompressor, block, output), NS_OK);
  });
}

TEST_F(Http2DecompressorTest, HeaderListOverLimitKeepsTableInSync) {
  Preferences::SetUint(kMaxHeaderListSizePref, 100);
  RunOnSocketThread([]() {
    Http2Decompressor decompressor;
    nsAutoCString output;
    nsTArray<uint8_t> block{kStatus200};
    AppendLiteral(block, 0x00, 100);
    // Incremental entry after the limit was hit still goes in the table.
    AppendLiteral(block, 0x40, 2);
    EXPECT_EQ(Decode(decompressor, block, output), NS_ERROR_ILLEGAL_VALUE);

    nsTArray<uint8_t> next{kStatus200, kDynamicIndex0};
    ASSERT_EQ(Decode(decompressor, next, output), NS_OK);
    EXPECT_TRUE(output.EqualsLiteral("HTTP/2 200\r\nx: vv\r\n"));
  });
}
