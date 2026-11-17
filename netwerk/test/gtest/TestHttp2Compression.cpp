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
  }
  void TearDown() override {
    Preferences::ClearUser(kRejectDuplicatePseudoHeadersPref);
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
