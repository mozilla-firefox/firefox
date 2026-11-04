#include "chrome/common/ipc_message.h"
#include "gtest/gtest.h"
#include "mozilla/net/PHttpChannelParams.h"
#include "nsHttp.h"
#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "nsURLHelper.h"

namespace mozilla {
namespace net {

void AssertRoundTrips(const nsHttpResponseHead& aHead) {
  {
    // Assert it round-trips via IPC.
    UniquePtr<IPC::Message> msg(new IPC::Message(MSG_ROUTING_NONE, 0));
    IPC::MessageWriter writer(*msg);
    IPC::ParamTraits<nsHttpResponseHead>::Write(&writer, aHead);

    nsHttpResponseHead deserializedHead;
    IPC::MessageReader reader(*msg);
    bool res = IPC::ParamTraits<mozilla::net::nsHttpResponseHead>::Read(
        &reader, &deserializedHead);
    ASSERT_TRUE(res);
    ASSERT_EQ(aHead, deserializedHead);
  }

  {
    // Assert it round-trips through copy-ctor.
    nsHttpResponseHead copied(aHead);
    ASSERT_EQ(aHead, copied);
  }

  {
    // Assert it round-trips through operator=
    nsHttpResponseHead copied;
    copied = aHead;
    // It is important that the below statement cannot be
    // ASSERT_EQ(aHead, copied) to avoid potential lock-order inversion problem.
    // See Bug 1829445 for more details
    ASSERT_EQ(copied, aHead);
  }
}

TEST(TestHttpResponseHead, Bug1636930)
{
  nsHttpResponseHead head;

  (void)head.ParseStatusLine("HTTP/1.1 200 OK"_ns);
  (void)head.ParseHeaderLine("content-type: text/plain"_ns);
  (void)head.ParseHeaderLine("etag: Just testing"_ns);
  (void)head.ParseHeaderLine("cache-control: max-age=99999"_ns);
  (void)head.ParseHeaderLine("accept-ranges: bytes"_ns);
  (void)head.ParseHeaderLine("content-length: 1408"_ns);
  (void)head.ParseHeaderLine("connection: close"_ns);
  (void)head.ParseHeaderLine("server: httpd.js"_ns);
  (void)head.ParseHeaderLine("date: Tue, 12 May 2020 09:24:23 GMT"_ns);

  AssertRoundTrips(head);
}

TEST(TestHttpResponseHead, bug1649807)
{
  nsHttpResponseHead head;

  (void)head.ParseStatusLine("HTTP/1.1 200 OK"_ns);
  (void)head.ParseHeaderLine("content-type: text/plain"_ns);
  (void)head.ParseHeaderLine("etag: Just testing"_ns);
  (void)head.ParseHeaderLine(
      "cache-control: public, max-age=31536000, immutable"_ns);
  (void)head.ParseHeaderLine("accept-ranges: bytes"_ns);
  (void)head.ParseHeaderLine("content-length: 1408"_ns);
  (void)head.ParseHeaderLine("connection: close"_ns);
  (void)head.ParseHeaderLine("server: httpd.js"_ns);
  (void)head.ParseHeaderLine("pragma: no-cache"_ns);
  (void)head.ParseHeaderLine("date: Tue, 12 May 2020 09:24:23 GMT"_ns);

  ASSERT_FALSE(head.NoCache())
  << "Cache-Control: immutable wins over Pragma: no-cache";
  AssertRoundTrips(head);
}

TEST(TestHttpResponseHead, bug1937766)
{
  nsHttpResponseHead head;

  (void)head.ParseStatusLine("HTTP/1.1 200 OK"_ns);
  (void)head.ParseHeaderLine("content-type: text/plain"_ns);
  (void)head.ParseHeaderLine("etag: Just testing"_ns);
  (void)head.ParseHeaderLine("cache-control: age=99999"_ns);
  (void)head.ParseHeaderLine("accept-ranges: bytes"_ns);
  (void)head.ParseHeaderLine("content-length: 1408"_ns);
  (void)head.ParseHeaderLine("connection: close"_ns);
  (void)head.ParseHeaderLine("server: httpd.js"_ns);
  (void)head.ParseHeaderLine("pragma: no-cache"_ns);
  (void)head.ParseHeaderLine("date: Tue, 12 May 2020 09:24:23 GMT"_ns);

  ASSERT_TRUE(head.NoCache())
  << "Pragma: no-cache wins over Cache-Control";
  AssertRoundTrips(head);
}

TEST(TestHttpResponseHead, bug1660200)
{
  nsHttpResponseHead head;

  (void)head.ParseStatusLine("HTTP/1.1 200 OK"_ns);
  (void)head.ParseHeaderLine("content-type: text/plain"_ns);
  (void)head.ParseHeaderLine("etag: Just testing"_ns);
  (void)head.ParseHeaderLine("cache-control: no-cache"_ns);
  (void)head.ParseHeaderLine("accept-ranges: bytes"_ns);
  (void)head.ParseHeaderLine("content-length: 1408"_ns);
  (void)head.ParseHeaderLine("connection: close"_ns);
  (void)head.ParseHeaderLine("server: httpd.js"_ns);
  (void)head.ParseHeaderLine("date: Tue, 12 May 2020 09:24:23 GMT"_ns);

  AssertRoundTrips(head);
}

TEST(TestHttpResponseHead, bug1687903)
{
  nsHttpResponseHead head;

  nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);

  nsresult expectation = NS_ERROR_PARSING_HTTP_STATUS_LINE;

  ASSERT_EQ(expectation, head.ParseStatusLine("HTTP/1.1 "_ns));
  ASSERT_EQ(expectation, head.ParseStatusLine("HTTP/1.1 BLAH"_ns));
  ASSERT_EQ(expectation, head.ParseStatusLine("HTTP/1.1 1000 BOO"_ns));
  ASSERT_EQ(expectation, head.ParseStatusLine("HTTP/1.1 0200 BOO"_ns));
  ASSERT_EQ(expectation, head.ParseStatusLine("HTTP/1.1 60200 200"_ns));
  ASSERT_EQ(expectation, head.ParseStatusLine("HTTP/1.1 131072 HIOK"_ns));
  ASSERT_EQ(expectation, head.ParseStatusLine("HTTP/1.1 -200 OK"_ns));
  ASSERT_EQ(expectation, head.ParseStatusLine("HTTP/1.1 0x9 OK"_ns));
  ASSERT_EQ(expectation, head.ParseStatusLine("HTTP/1.1 C8 OK"_ns));
}

TEST(TestHttpResponseHead, atoms)
{
  // Test that the resolving the content-type atom returns the initial static
  ASSERT_EQ(nsHttp::Content_Type, nsHttp::ResolveAtom("content-type"_ns));
  // Check that they're case insensitive
  ASSERT_EQ(nsHttp::ResolveAtom("Content-Type"_ns),
            nsHttp::ResolveAtom("content-type"_ns));
  // This string literal should be the backing of the atom when resolved first
  auto header1 = "CustomHeaderXXX1"_ns;
  auto atom1 = nsHttp::ResolveAtom(header1);
  auto header2 = "customheaderxxx1"_ns;
  auto atom2 = nsHttp::ResolveAtom(header2);
  ASSERT_EQ(atom1, atom2);
  ASSERT_EQ(atom1.get(), atom2.get());
  // Check that we get the expected pointer back.
  ASSERT_EQ(atom2.get(), header1.get());
}

TEST(ContentTypeParsing, CommentHandling1)
{
  bool dummy;
  const nsAutoCString val("text/html;charset=gbk(");
  nsCString contentType;
  nsCString contentCharset;

  net_ParseContentType(val, contentType, contentCharset, &dummy);

  ASSERT_TRUE(contentType.EqualsLiteral("text/html"));
  ASSERT_TRUE(contentCharset.EqualsLiteral("gbk("));
}

TEST(ContentTypeParsing, CommentHandling2)
{
  bool dummy;
  const nsAutoCString val("text/html;x=(;charset=gbk");
  nsCString contentType;
  nsCString contentCharset;

  net_ParseContentType(val, contentType, contentCharset, &dummy);

  ASSERT_TRUE(contentType.EqualsLiteral("text/html"));
  ASSERT_TRUE(contentCharset.EqualsLiteral("gbk"));
}

TEST(ContentTypeParsing, CommentHandling3)
{
  bool dummy;
  const nsAutoCString val("text/html;test=test;(;charset=gbk");
  nsCString contentType;
  nsCString contentCharset;

  net_ParseContentType(val, contentType, contentCharset, &dummy);

  ASSERT_TRUE(contentType.EqualsLiteral("text/html"));
  ASSERT_TRUE(contentCharset.EqualsLiteral("gbk"));
}

TEST(TestHttpResponseHead, MoveConstructor)
{
  // Construct an initial response head
  nsHttpResponseHead originalHead;
  (void)originalHead.ParseStatusLine("HTTP/1.1 200 OK"_ns);
  (void)originalHead.ParseHeaderLine("content-type: text/plain"_ns);
  (void)originalHead.ParseHeaderLine("content-length: 1408"_ns);

  // Move construct a new object from the initial one
  nsHttpResponseHead movedHead(std::move(originalHead));

  // Ensure the moved head retains the original status
  ASSERT_EQ(movedHead.Status(), 200);
  nsCString value;
  ASSERT_EQ(movedHead.GetHeader(nsHttp::Content_Type, value), NS_OK);
  ASSERT_EQ(value, "text/plain"_ns);
  ASSERT_EQ(movedHead.GetHeader(nsHttp::Content_Length, value), NS_OK);
  ASSERT_EQ(value, "1408"_ns);

  // Check original object is in an unspecified state
  ASSERT_EQ(originalHead.GetHeader(nsHttp::Content_Type, value),
            NS_ERROR_NOT_AVAILABLE);
  ASSERT_FALSE(originalHead.HasHeader(nsHttp::Content_Type));
  ASSERT_FALSE(originalHead.HasHeader(nsHttp::Content_Length));
}

TEST(TestHttpResponseHead, MultipleCacheControl)
{
  nsHttpResponseHead originalHead;
  (void)originalHead.ParseStatusLine("HTTP/1.1 200 OK"_ns);
  (void)originalHead.ParseHeaderLine("content-type: text/plain"_ns);
  (void)originalHead.ParseHeaderLine("content-length: 1408"_ns);
  (void)originalHead.ParseHeaderLine("cache-control: no-cache"_ns);
  (void)originalHead.ParseHeaderLine("cache-control: no-store"_ns);

  ASSERT_EQ(originalHead.NoStore(), true);
  ASSERT_EQ(originalHead.NoCache(), true);
}

TEST(TestHttpResponseHead, ContentTypePreservedOn304)
{
  nsHttpResponseHead original200Head;
  (void)original200Head.ParseStatusLine("HTTP/1.0 200 OK"_ns);
  (void)original200Head.ParseHeaderLine(
      "Server: SimpleHTTP/0.6 Python/3.13.7"_ns);
  (void)original200Head.ParseHeaderLine(
      "Date: Mon, 08 Dec 2025 08:22:42 GMT"_ns);
  (void)original200Head.ParseHeaderLine("Content-type: application/xml"_ns);
  (void)original200Head.ParseHeaderLine("Content-Length: 185"_ns);
  (void)original200Head.ParseHeaderLine(
      "Last-Modified: Mon, 08 Dec 2025 08:16:37 GMT"_ns);
  (void)original200Head.ParseHeaderLine("Content-Type: text/plain"_ns);
  (void)original200Head.ParseHeaderLine("X-Content-Type-Options: nosniff"_ns);

  nsCString headerValue;
  ASSERT_EQ(original200Head.GetHeader(nsHttp::Content_Type, headerValue),
            NS_OK);
  ASSERT_EQ(headerValue, "application/xml,text/plain"_ns);

  nsCString contentType200;
  original200Head.ContentType(contentType200);
  ASSERT_EQ(contentType200, "text/plain"_ns);

  nsAutoCString flattenedHead;
  original200Head.Flatten(flattenedHead, true);

  nsAutoCString flattenedOriginal;
  original200Head.FlattenNetworkOriginalHeaders(flattenedOriginal);

  nsHttpResponseHead cachedResponseHead;
  nsresult rv;

  // Simlar to nsHttp::GetHttpResponseHeadFromCacheEntry
  rv = cachedResponseHead.ParseCachedOriginalHeaders(
      (char*)flattenedOriginal.get());
  ASSERT_EQ(rv, NS_OK);

  rv = cachedResponseHead.ParseCachedHead(flattenedHead.get());
  ASSERT_EQ(rv, NS_OK);

  nsCString cachedContentType;
  cachedResponseHead.ContentType(cachedContentType);
  ASSERT_EQ(cachedContentType, "text/plain"_ns);

  nsHttpResponseHead response304Head;
  (void)response304Head.ParseStatusLine("HTTP/1.0 304 Not Modified"_ns);
  (void)response304Head.ParseHeaderLine(
      "Date: Mon, 08 Dec 2025 08:23:00 GMT"_ns);
  cachedResponseHead.UpdateHeaders(&response304Head);

  nsCString contentTypeAfter304;
  cachedResponseHead.ContentType(contentTypeAfter304);
  ASSERT_EQ(contentTypeAfter304, "text/plain"_ns);
  ASSERT_EQ(contentType200, contentTypeAfter304);
}

TEST(TestHttpResponseHead, ParseCachedHead)
{
  nsAutoCString flattenedOriginal(
      "Server: SimpleHTTP/0.6 Python/3.13.7\r\n"
      "Date: Mon, 08 Dec 2025 08:22:42 GMT\r\n"
      "Content-type: application/xml\r\n"
      "Content-Length: 185\r\n"
      "Last-Modified: Mon, 08 Dec 2025 08:16:37 GMT\r\n"
      "Content-Type: text/plain\r\n"
      "X-Content-Type-Options: nosniff\r\n");

  // Incorrectly parsed content type.
  nsAutoCString flattenedHead(
      "HTTP/1.0 200 OK\r\n"
      "Server: SimpleHTTP/0.6 Python/3.13.7\r\n"
      "Date: Mon, 08 Dec 2025 08:22:42 GMT\r\n"
      "Content-type: application/xml\r\n"
      "Content-Length: 185\r\n"
      "Last-Modified: Mon, 08 Dec 2025 08:16:37 GMT\r\n"
      "X-Content-Type-Options: nosniff\r\n");

  nsHttpResponseHead cachedResponseHead;
  nsresult rv;

  rv = cachedResponseHead.ParseCachedOriginalHeaders(
      (char*)flattenedOriginal.get());
  ASSERT_EQ(rv, NS_OK);

  rv = cachedResponseHead.ParseCachedHead(flattenedHead.get());
  ASSERT_EQ(rv, NS_OK);

  nsCString cachedContentType;
  cachedResponseHead.ContentType(cachedContentType);
  ASSERT_EQ(cachedContentType, "text/plain"_ns);
}

}  // namespace net
}  // namespace mozilla
