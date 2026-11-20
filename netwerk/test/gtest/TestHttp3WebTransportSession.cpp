/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Http3TestStubs.h"
#include "Http3WebTransportSession.h"
#include "gtest/gtest.h"
#include "mozilla/ScopeExit.h"
#include "nsSocketTransportService2.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"

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

// Http3WebTransportSession asserts it runs on the socket thread.
static void RunOnSocketThreadAndWait(std::function<void()>&& aFn) {
  nsCOMPtr<nsIEventTarget> sts = gSocketTransportService;
  ASSERT_TRUE(sts);
  NS_DispatchAndSpinEventLoopUntilComplete(
      "TestHttp3WebTransportSession"_ns, sts,
      NS_NewRunnableFunction("TestHttp3WebTransportSession", std::move(aFn)));
}

// Drives a session through activation and the response headers, to the state
// in which it accepts outgoing datagrams.
static already_AddRefed<Http3WebTransportSession> CreateActiveSession(
    Http3SessionStub* aStub, DummyHttpTransaction* aTrans) {
  RefPtr<Http3WebTransportSession> session =
      new Http3WebTransportSession(aTrans, aStub);
  aStub->StreamHasDataToWrite(session);
  aStub->ProcessOutput();

  nsTArray<uint8_t> response = ToBytes("HTTP/3 200\n\n");
  session->SetResponseHeaders(response, false, false);
  (void)session->WriteSegments();
  return session.forget();
}

TEST(TestHttp3WebTransportSession, FailedDatagramEnqueueCountsAsDropped)
{
  InitHttpHandler();
  RefPtr<DummyHttpTransaction> trans = new DummyHttpTransaction();

  RunOnSocketThreadAndWait([trans]() {
    RefPtr<Http3SessionStub> stub = new Http3SessionStub();
    RefPtr<Http3WebTransportSession> session = CreateActiveSession(stub, trans);
    // Releases the stub's reference to the session, even if an ASSERT fails.
    auto releaseSession = MakeScopeExit([&] { stub->ProcessOutput(); });
    stub->mDatagramsSent = 3;
    stub->mDatagramsExpired = 1;
    stub->mDatagramsDropped = 2;

    session->SendDatagram(nsTArray<uint8_t>{1, 2, 3}, 0, 0, TimeStamp::Now());
    stub->mSendDatagramResult = NS_ERROR_NOT_AVAILABLE;
    session->SendDatagram(nsTArray<uint8_t>{4, 5, 6}, 0, 0, TimeStamp::Now());
    session->SendDatagram(nsTArray<uint8_t>{7, 8, 9}, 0, 0, TimeStamp::Now());
    EXPECT_EQ(stub->mDatagramsEnqueued, 1u);

    uint64_t sent = 0;
    uint64_t expired = 0;
    uint64_t dropped = 0;
    ASSERT_TRUE(session->GetDatagramSessionStats(&sent, &expired, &dropped));
    EXPECT_EQ(sent, 3u);
    EXPECT_EQ(expired, 1u);
    // neqo's own two, plus the two it refused to enqueue.
    EXPECT_EQ(dropped, 4u);
  });
}

TEST(TestHttp3WebTransportSession, EnqueuedDatagramIsNotCountedAsDropped)
{
  InitHttpHandler();
  RefPtr<DummyHttpTransaction> trans = new DummyHttpTransaction();

  RunOnSocketThreadAndWait([trans]() {
    RefPtr<Http3SessionStub> stub = new Http3SessionStub();
    RefPtr<Http3WebTransportSession> session = CreateActiveSession(stub, trans);
    auto releaseSession = MakeScopeExit([&] { stub->ProcessOutput(); });

    session->SendDatagram(nsTArray<uint8_t>{1, 2, 3}, 0, 0, TimeStamp::Now());
    session->SendDatagram(nsTArray<uint8_t>{4, 5, 6}, 0, 0, TimeStamp::Now());
    EXPECT_EQ(stub->mDatagramsEnqueued, 2u);

    uint64_t sent = 0;
    uint64_t expired = 0;
    uint64_t dropped = 0;
    ASSERT_TRUE(session->GetDatagramSessionStats(&sent, &expired, &dropped));
    EXPECT_EQ(dropped, 0u);
  });
}

TEST(TestHttp3WebTransportSession, ErrorCodeMapping)
{
  for (uint32_t code : {0u, 1u, 29u, 30u, 31u, 255u, 256u, 0xffffffffu}) {
    Maybe<uint32_t> decoded =
        Http3ErrorToWebTransportError(WebTransportErrorToHttp3Error(code));
    ASSERT_TRUE(decoded.isSome());
    EXPECT_EQ(*decoded, code);
  }

  uint64_t first = WebTransportErrorToHttp3Error(0);
  uint64_t last = WebTransportErrorToHttp3Error(0xffffffffu);
  EXPECT_TRUE(Http3ErrorToWebTransportError(first - 1).isNothing());
  EXPECT_TRUE(Http3ErrorToWebTransportError(last + 1).isNothing());
  // H3_REQUEST_CANCELLED is no application error code.
  EXPECT_TRUE(Http3ErrorToWebTransportError(0x10c).isNothing());

  // The reserved codepoints inside the range are no application codes either.
  uint64_t reserved = first;
  while ((reserved - 0x21) % 0x1f != 0) {
    reserved++;
  }
  EXPECT_TRUE(Http3ErrorToWebTransportError(reserved).isNothing());
  EXPECT_TRUE(Http3ErrorToWebTransportError(reserved + 1).isSome());
}
