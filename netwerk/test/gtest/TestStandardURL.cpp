#include "IPv4Parser.h"
#include "gtest/MozGTestBench.h"  // For MOZ_GTEST_BENCH
#include "gtest/gtest.h"
#include "mozilla/Base64.h"
#include "mozilla/Encoding.h"
#include "mozilla/ipc/URIUtils.h"
#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsEscape.h"
#include "nsIStandardURL.h"
#include "nsIURIMutator.h"
#include "nsIURL.h"
#include "nsNetCID.h"
#include "nsPrintfCString.h"
#include "nsSerializationHelper.h"
#include "nsString.h"
#include "nsURLHelper.h"

using namespace mozilla;

nsresult Test_NormalizeIPv4(const nsACString& host, nsCString& result) {
  return net::IPv4Parser::NormalizeIPv4(host, result);
}

TEST(TestStandardURL, Simple)
{
  nsCOMPtr<nsIURI> url;
  ASSERT_EQ(NS_MutateURI(NS_STANDARDURLMUTATOR_CONTRACTID)
                .SetSpec("http://example.com"_ns)
                .Finalize(url),
            NS_OK);
  ASSERT_TRUE(url);

  ASSERT_EQ(NS_MutateURI(url).SetSpec("http://example.com"_ns).Finalize(url),
            NS_OK);

  nsAutoCString out;

  ASSERT_EQ(url->GetSpec(out), NS_OK);
  ASSERT_TRUE(out == "http://example.com/"_ns);

  ASSERT_EQ(url->Resolve("foo.html?q=45"_ns, out), NS_OK);
  ASSERT_TRUE(out == "http://example.com/foo.html?q=45"_ns);

  ASSERT_EQ(NS_MutateURI(url).SetScheme("foo"_ns).Finalize(url), NS_OK);

  ASSERT_EQ(url->GetScheme(out), NS_OK);
  ASSERT_TRUE(out == "foo"_ns);

  ASSERT_EQ(url->GetHost(out), NS_OK);
  ASSERT_TRUE(out == "example.com"_ns);
  ASSERT_EQ(NS_MutateURI(url).SetHost("www.yahoo.com"_ns).Finalize(url), NS_OK);
  ASSERT_EQ(url->GetHost(out), NS_OK);
  ASSERT_TRUE(out == "www.yahoo.com"_ns);

  ASSERT_EQ(NS_MutateURI(url)
                .SetPathQueryRef(nsLiteralCString(
                    "/some-path/one-the-net/about.html?with-a-query#for-you"))
                .Finalize(url),
            NS_OK);
  ASSERT_EQ(url->GetPathQueryRef(out), NS_OK);
  ASSERT_TRUE(out ==
              nsLiteralCString(
                  "/some-path/one-the-net/about.html?with-a-query#for-you"));

  ASSERT_EQ(NS_MutateURI(url)
                .SetQuery(nsLiteralCString(
                    "a=b&d=c&what-ever-you-want-to-be-called=45"))
                .Finalize(url),
            NS_OK);
  ASSERT_EQ(url->GetQuery(out), NS_OK);
  ASSERT_TRUE(out == "a=b&d=c&what-ever-you-want-to-be-called=45"_ns);

  ASSERT_EQ(NS_MutateURI(url).SetRef("#some-book-mark"_ns).Finalize(url),
            NS_OK);
  ASSERT_EQ(url->GetRef(out), NS_OK);
  ASSERT_TRUE(out == "some-book-mark"_ns);
}

TEST(TestStandardURL, NormalizeGood)
{
  nsCString result;
  const char* manual[] = {"0.0.0.0",
                          "0.0.0.0",
                          "0",
                          "0.0.0.0",
                          "000",
                          "0.0.0.0",
                          "0x00",
                          "0.0.0.0",
                          "10.20.100.200",
                          "10.20.100.200",
                          "255.255.255.255",
                          "255.255.255.255",
                          "0XFF.0xFF.0xff.0xFf",
                          "255.255.255.255",
                          "0x000ff.0X00FF.0x0ff.0xff",
                          "255.255.255.255",
                          "0x000fA.0X00FB.0x0fC.0xfD",
                          "250.251.252.253",
                          "0x000fE.0X00FF.0x0fC.0xfD",
                          "254.255.252.253",
                          "0x000fa.0x00fb.0x0fc.0xfd",
                          "250.251.252.253",
                          "0x000fe.0x00ff.0x0fc.0xfd",
                          "254.255.252.253",
                          "0377.0377.0377.0377",
                          "255.255.255.255",
                          "0000377.000377.00377.0377",
                          "255.255.255.255",
                          "65535",
                          "0.0.255.255",
                          "0xfFFf",
                          "0.0.255.255",
                          "0x00000ffff",
                          "0.0.255.255",
                          "0177777",
                          "0.0.255.255",
                          "000177777",
                          "0.0.255.255",
                          "0.13.65535",
                          "0.13.255.255",
                          "0.22.0xffff",
                          "0.22.255.255",
                          "0.123.0177777",
                          "0.123.255.255",
                          "65536",
                          "0.1.0.0",
                          "0200000",
                          "0.1.0.0",
                          "0x10000",
                          "0.1.0.0"};
  for (uint32_t i = 0; i < sizeof(manual) / sizeof(manual[0]); i += 2) {
    nsCString encHost(manual[i + 0]);
    ASSERT_EQ(NS_OK, Test_NormalizeIPv4(encHost, result));
    ASSERT_TRUE(result.Equals(manual[i + 1]));
  }

  // Make sure we're getting the numbers correctly interpreted:
  for (int i = 0; i < 256; i++) {
    nsCString encHost = nsPrintfCString("0x%x", i);
    ASSERT_EQ(NS_OK, Test_NormalizeIPv4(encHost, result));
    ASSERT_TRUE(result.Equals(nsPrintfCString("0.0.0.%d", i)));

    encHost = nsPrintfCString("0%o", i);
    ASSERT_EQ(NS_OK, Test_NormalizeIPv4(encHost, result));
    ASSERT_TRUE(result.Equals(nsPrintfCString("0.0.0.%d", i)));
  }

  // Some random numbers in the range, mixing hex, decimal, octal
  for (int i = 0; i < 8; i++) {
    int val[4] = {i * 11 + 13, i * 18 + 22, i * 4 + 28, i * 15 + 2};

    nsCString encHost =
        nsPrintfCString("%d.%d.%d.%d", val[0], val[1], val[2], val[3]);
    ASSERT_EQ(NS_OK, Test_NormalizeIPv4(encHost, result));
    ASSERT_TRUE(result.Equals(encHost));

    nsCString encHostM =
        nsPrintfCString("0x%x.0x%x.0x%x.0x%x", val[0], val[1], val[2], val[3]);
    ASSERT_EQ(NS_OK, Test_NormalizeIPv4(encHostM, result));
    ASSERT_TRUE(result.Equals(encHost));

    encHostM =
        nsPrintfCString("0%o.0%o.0%o.0%o", val[0], val[1], val[2], val[3]);
    ASSERT_EQ(NS_OK, Test_NormalizeIPv4(encHostM, result));
    ASSERT_TRUE(result.Equals(encHost));

    encHostM =
        nsPrintfCString("0x%x.%d.0%o.%d", val[0], val[1], val[2], val[3]);
    ASSERT_EQ(NS_OK, Test_NormalizeIPv4(encHostM, result));
    ASSERT_TRUE(result.Equals(encHost));

    encHostM =
        nsPrintfCString("%d.0%o.0%o.0x%x", val[0], val[1], val[2], val[3]);
    ASSERT_EQ(NS_OK, Test_NormalizeIPv4(encHostM, result));
    ASSERT_TRUE(result.Equals(encHost));

    encHostM =
        nsPrintfCString("0%o.0%o.0x%x.0x%x", val[0], val[1], val[2], val[3]);
    ASSERT_EQ(NS_OK, Test_NormalizeIPv4(encHostM, result));
    ASSERT_TRUE(result.Equals(encHost));
  }
}

TEST(TestStandardURL, NormalizeBad)
{
  nsAutoCString result;
  const char* manual[] = {
      "x22.232.12.32", "122..12.32",    "122.12.32.12.32",   "122.12.32..",
      "122.12.xx.22",  "122.12.0xx.22", "0xx.12.01.22",      "12.12.02x.22",
      "1q.12.2.22",    "122.01f.02.22", "12a.01.02.22",      "12.01.02.20x1",
      "10x2.01.02.20", "0xx.01.02.20",  "10.x.02.20",        "10.00x2.02.20",
      "10.13.02x2.20", "10.x13.02.20",  "10.0x134def.02.20", "\0.2.2.2",
      "256.2.2.2",     "2.256.2.2",     "2.2.256.2",         "2.2.2.256",
      "2.2.-2.3",      "+2.2.2.3",      "13.0x2x2.2.3",      "0x2x2.13.2.3"};

  for (auto& i : manual) {
    nsCString encHost(i);
    ASSERT_EQ(NS_ERROR_FAILURE, Test_NormalizeIPv4(encHost, result));
  }
}

TEST(TestStandardURL, From_test_standardurldotjs)
{
  // These are test (success and failure) cases from test_standardurl.js
  nsAutoCString result;

  const char* localIPv4s[] = {
      "127.0.0.1",
      "127.0.1",
      "127.1",
      "2130706433",
      "0177.00.00.01",
      "0177.00.01",
      "0177.01",
      "00000000000000000000000000177.0000000.0000000.0001",
      "000000177.0000001",
      "017700000001",
      "0x7f.0x00.0x00.0x01",
      "0x7f.0x01",
      "0x7f000001",
      "0x007f.0x0000.0x0000.0x0001",
      "000177.0.00000.0x0001",
      "127.0.0.1.",

      "0X7F.0X00.0X00.0X01",
      "0X7F.0X01",
      "0X7F000001",
      "0X007F.0X0000.0X0000.0X0001",
      "000177.0.00000.0X0001"};
  for (auto& localIPv4 : localIPv4s) {
    nsCString encHost(localIPv4);
    ASSERT_EQ(NS_OK, Test_NormalizeIPv4(encHost, result));
    ASSERT_TRUE(result.EqualsLiteral("127.0.0.1"));
  }

  const char* nonIPv4s[] = {"0xfffffffff", "0x100000000",
                            "4294967296",  "1.2.0x10000",
                            "1.0x1000000", "256.0.0.1",
                            "1.256.1",     "-1.0.0.0",
                            "1.2.3.4.5",   "010000000000000000",
                            "2+3",         "0.0.0.-1",
                            "1.2.3.4..",   "1..2",
                            ".1.2.3.4",    ".127"};
  for (auto& nonIPv4 : nonIPv4s) {
    nsCString encHost(nonIPv4);
    ASSERT_EQ(NS_ERROR_FAILURE, Test_NormalizeIPv4(encHost, result));
  }

  const char* oneOrNoDotsIPv4s[] = {"127", "127."};
  for (auto& localIPv4 : oneOrNoDotsIPv4s) {
    nsCString encHost(localIPv4);
    ASSERT_EQ(NS_OK, Test_NormalizeIPv4(encHost, result));
    ASSERT_TRUE(result.EqualsLiteral("0.0.0.127"));
  }
}

#define TEST_COUNT 10000

MOZ_GTEST_BENCH(TestStandardURL, DISABLED_Perf, [] {
  nsCOMPtr<nsIURI> url;
  ASSERT_EQ(NS_OK, NS_MutateURI(NS_STANDARDURLMUTATOR_CONTRACTID)
                       .SetSpec("http://example.com"_ns)
                       .Finalize(url));

  nsAutoCString out;
  for (int i = TEST_COUNT; i; --i) {
    ASSERT_EQ(NS_MutateURI(url).SetSpec("http://example.com"_ns).Finalize(url),
              NS_OK);
    ASSERT_EQ(url->GetSpec(out), NS_OK);
    url->Resolve("foo.html?q=45"_ns, out);
    (void)NS_MutateURI(url).SetScheme("foo"_ns).Finalize(url);
    url->GetScheme(out);
    (void)NS_MutateURI(url).SetHost("www.yahoo.com"_ns).Finalize(url);
    url->GetHost(out);
    (void)NS_MutateURI(url)
        .SetPathQueryRef(nsLiteralCString(
            "/some-path/one-the-net/about.html?with-a-query#for-you"))
        .Finalize(url);
    url->GetPathQueryRef(out);
    (void)NS_MutateURI(url)
        .SetQuery(
            nsLiteralCString("a=b&d=c&what-ever-you-want-to-be-called=45"))
        .Finalize(url);
    url->GetQuery(out);
    (void)NS_MutateURI(url).SetRef("#some-book-mark"_ns).Finalize(url);
    url->GetRef(out);
  }
});

// Note the five calls in the loop, so divide by 100k
MOZ_GTEST_BENCH(TestStandardURL, DISABLED_NormalizePerf, [] {
  nsAutoCString result;
  for (int i = 0; i < 20000; i++) {
    nsAutoCString encHost("123.232.12.32");
    ASSERT_EQ(NS_OK, Test_NormalizeIPv4(encHost, result));
    nsAutoCString encHost2("83.62.12.92");
    ASSERT_EQ(NS_OK, Test_NormalizeIPv4(encHost2, result));
    nsAutoCString encHost3("8.7.6.5");
    ASSERT_EQ(NS_OK, Test_NormalizeIPv4(encHost3, result));
    nsAutoCString encHost4("111.159.123.220");
    ASSERT_EQ(NS_OK, Test_NormalizeIPv4(encHost4, result));
    nsAutoCString encHost5("1.160.204.200");
    ASSERT_EQ(NS_OK, Test_NormalizeIPv4(encHost5, result));
  }
});

// Bug 1394785 - ignore unstable test on OSX
#ifndef XP_MACOSX
// Note the five calls in the loop, so divide by 100k
MOZ_GTEST_BENCH(TestStandardURL, DISABLED_NormalizePerfFails, [] {
  nsAutoCString result;
  for (int i = 0; i < 20000; i++) {
    nsAutoCString encHost("123.292.12.32");
    ASSERT_EQ(NS_ERROR_FAILURE, Test_NormalizeIPv4(encHost, result));
    nsAutoCString encHost2("83.62.12.0x13292");
    ASSERT_EQ(NS_ERROR_FAILURE, Test_NormalizeIPv4(encHost2, result));
    nsAutoCString encHost3("8.7.6.0xhello");
    ASSERT_EQ(NS_ERROR_FAILURE, Test_NormalizeIPv4(encHost3, result));
    nsAutoCString encHost4("111.159.notonmywatch.220");
    ASSERT_EQ(NS_ERROR_FAILURE, Test_NormalizeIPv4(encHost4, result));
    nsAutoCString encHost5("1.160.204.20f");
    ASSERT_EQ(NS_ERROR_FAILURE, Test_NormalizeIPv4(encHost5, result));
  }
});
#endif

TEST(TestStandardURL, Mutator)
{
  nsAutoCString out;
  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_MutateURI(NS_STANDARDURLMUTATOR_CONTRACTID)
                    .SetSpec("http://example.com"_ns)
                    .Finalize(uri);
  ASSERT_EQ(rv, NS_OK);

  ASSERT_EQ(uri->GetSpec(out), NS_OK);
  ASSERT_TRUE(out == "http://example.com/"_ns);

  rv = NS_MutateURI(uri)
           .SetScheme("ftp"_ns)
           .SetHost("mozilla.org"_ns)
           .SetPathQueryRef("/path?query#ref"_ns)
           .Finalize(uri);
  ASSERT_EQ(rv, NS_OK);
  ASSERT_EQ(uri->GetSpec(out), NS_OK);
  ASSERT_TRUE(out == "ftp://mozilla.org/path?query#ref"_ns);

  nsCOMPtr<nsIURL> url;
  rv = NS_MutateURI(uri).SetScheme("https"_ns).Finalize(url);
  ASSERT_EQ(rv, NS_OK);
  ASSERT_EQ(url->GetSpec(out), NS_OK);
  ASSERT_TRUE(out == "https://mozilla.org/path?query#ref"_ns);
}

TEST(TestStandardURL, Deserialize_Bug1392739)
{
  mozilla::ipc::StandardURLParams standard_params;
  standard_params.urlType() = nsIStandardURL::URLTYPE_STANDARD;
  standard_params.spec().Truncate();
  standard_params.host() = mozilla::ipc::StandardURLSegment(4294967295, 1);

  mozilla::ipc::URIParams params(standard_params);

  nsCOMPtr<nsIURIMutator> mutator =
      do_CreateInstance(NS_STANDARDURLMUTATOR_CID);
  ASSERT_EQ(mutator->Deserialize(params), NS_ERROR_FAILURE);
}

TEST(TestStandardURL, Deserialize_Bug2027976)
{
  mozilla::ipc::StandardURLParams params;

  nsAutoCString evilSpec;
  evilSpec.AssignLiteral("http://127.0.0.1:8888/");
  for (int i = 0; i < 500; i++) {
    evilSpec.Append('x');
  }
  evilSpec.Append('/');

  uint32_t specLen = evilSpec.Length();
  uint32_t pathPos = specLen - 1;

  params.spec() = evilSpec;
  params.urlType() = nsIStandardURL::URLTYPE_AUTHORITY;
  params.port() = 8888;
  params.defaultPort() = 80;
  params.supportsFileURL() = false;
  params.isSubstituting() = false;

  params.scheme() = mozilla::ipc::StandardURLSegment(0, 4);
  params.authority() = mozilla::ipc::StandardURLSegment(7, 14);
  params.username() = mozilla::ipc::StandardURLSegment(0, -1);
  params.password() = mozilla::ipc::StandardURLSegment(0, -1);
  params.host() = mozilla::ipc::StandardURLSegment(7, 9);
  params.path() = mozilla::ipc::StandardURLSegment(pathPos, 1);
  params.filePath() = mozilla::ipc::StandardURLSegment(pathPos, 1);
  params.directory() = mozilla::ipc::StandardURLSegment(21, 1);
  params.baseName() = mozilla::ipc::StandardURLSegment(0, -1);
  params.extension() = mozilla::ipc::StandardURLSegment(0, -1);
  params.query() = mozilla::ipc::StandardURLSegment(0, -1);
  params.ref() = mozilla::ipc::StandardURLSegment(0, -1);

  mozilla::ipc::URIParams uriParams(params);

  nsCOMPtr<nsIURIMutator> mutator =
      do_CreateInstance(NS_STANDARDURLMUTATOR_CID);
  nsresult rv = mutator->Deserialize(uriParams);
  ASSERT_EQ(rv, NS_ERROR_FAILURE);
  if (NS_FAILED(rv)) {
    return;
  }
  // The following code should not normally be reachable.
  nsCOMPtr<nsIURI> uri;
  rv = mutator->Finalize(getter_AddRefs(uri));
  ASSERT_EQ(rv, NS_OK);
  nsAutoCString spec;
  uri->Resolve("foo"_ns, spec);
}

TEST(TestStandardURL, CorruptSerialization)
{
  auto spec = "http://user:pass@example.com/path/to/file.ext?query#hash"_ns;

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_MutateURI(NS_STANDARDURLMUTATOR_CONTRACTID)
                    .SetSpec(spec)
                    .Finalize(uri);
  ASSERT_EQ(rv, NS_OK);

  nsAutoCString serialization;
  nsCOMPtr<nsISerializable> serializable = do_QueryInterface(uri);
  ASSERT_TRUE(serializable);

  // Check that the URL is normally serializable.
  ASSERT_EQ(NS_OK, NS_SerializeToString(serializable, serialization));
  nsCOMPtr<nsISupports> deserializedObject;
  ASSERT_EQ(NS_OK, NS_DeserializeObject(serialization,
                                        getter_AddRefs(deserializedObject)));

  nsAutoCString canonicalBin;
  (void)Base64Decode(serialization, canonicalBin);

// The spec serialization begins at byte 49
// If the implementation of nsStandardURL::Write changes, this test will need
// to be adjusted.
#define SPEC_OFFSET 49

  ASSERT_EQ(Substring(canonicalBin, SPEC_OFFSET, 7), "http://"_ns);

  nsAutoCString corruptedBin = canonicalBin;
  // change mScheme.mPos
  corruptedBin.BeginWriting()[SPEC_OFFSET + spec.Length()] = 1;
  (void)Base64Encode(corruptedBin, serialization);
  ASSERT_EQ(
      NS_ERROR_MALFORMED_URI,
      NS_DeserializeObject(serialization, getter_AddRefs(deserializedObject)));

  corruptedBin = canonicalBin;
  // change mScheme.mLen
  corruptedBin.BeginWriting()[SPEC_OFFSET + spec.Length() + 4] = 127;
  (void)Base64Encode(corruptedBin, serialization);
  ASSERT_EQ(
      NS_ERROR_MALFORMED_URI,
      NS_DeserializeObject(serialization, getter_AddRefs(deserializedObject)));
}

TEST(TestStandardURL, ParseIPv4Num)
{
  auto host = "0x.0x.0"_ns;

  int32_t bases[4] = {10, 10, 10, 10};
  bool onlyBase10 = true;  // Track this as a special case
  int32_t dotIndex[3];     // The positions of the dots in the string
  int32_t length = static_cast<int32_t>(host.Length());

  ASSERT_EQ(2, mozilla::net::IPv4Parser::ValidateIPv4Number(
                   host, bases, dotIndex, onlyBase10, length, false));

  nsCString result;
  ASSERT_EQ(NS_OK, Test_NormalizeIPv4("0x.0x.0"_ns, result));

  uint32_t number;
  mozilla::net::IPv4Parser::ParseIPv4Number("0x10"_ns, 16, number, 255);
  ASSERT_EQ(number, (uint32_t)16);

  // Inputs whose mathematical value exceeds 0xFFFFFFFF must be rejected,
  // even when they wrap modulo 2^64 back into the valid uint32_t range.
  // 18446744073709551616 == 2^64             => wraps to 0
  // 18446744075840258049 == 2^64 + 0x7F000001 => wraps to 127.0.0.1
  nsCString wrapResult;
  ASSERT_EQ(NS_ERROR_FAILURE,
            Test_NormalizeIPv4("18446744073709551616"_ns, wrapResult));
  ASSERT_EQ(NS_ERROR_FAILURE,
            Test_NormalizeIPv4("18446744075840258049"_ns, wrapResult));

  ASSERT_EQ(NS_ERROR_FAILURE,
            mozilla::net::IPv4Parser::ParseIPv4Number10(
                "18446744075840258049"_ns, number, 0xffffffffu));
  ASSERT_EQ(NS_ERROR_FAILURE,
            mozilla::net::IPv4Parser::ParseIPv4Number("18446744075840258049"_ns,
                                                      10, number, 0xffffffffu));
}

TEST(TestStandardURL, CoalescePath)
{
  auto testCoalescing = [](const char* input, const char* expected,
                           uint32_t lastSlash, uint32_t endOfBasename) {
    nsAutoCString buf(input);
    auto resultCoalesceDirs = net_CoalesceDirs(buf.BeginWriting());

    ASSERT_EQ(nsCString(buf.get()), nsCString(expected));

    // If the path does not begin with '/', it should return Nothing().
    if (input[0] == '/') {
      ASSERT_EQ(resultCoalesceDirs->first(), lastSlash);
      ASSERT_EQ(resultCoalesceDirs->second(), endOfBasename);
    } else {
      ASSERT_TRUE(resultCoalesceDirs == Nothing());
    }
  };

#ifndef DEBUG
  // invalid input
  testCoalescing("", "", -1, -1);
#endif
  // end of basename is the null character's index
  testCoalescing("/", "/", 0, 1);
  testCoalescing("/.", "/", 0, 1);
  testCoalescing("/..", "/", 0, 1);
  testCoalescing("/foo/foo1/.", "/foo/foo1/", 9, 10);
  testCoalescing("/foo/../foo1", "/foo1", 0, 5);
  testCoalescing("/foo/./foo1", "/foo/foo1", 4, 9);
  testCoalescing("/foo/foo1/..", "/foo/", 4, 5);

  // Bug 1890346
  testCoalescing("/..?/..", "/?/..", 0, 1);

  testCoalescing("/.?/..", "/?/..", 0, 1);
  testCoalescing("/./../?", "/?", 0, 1);
  testCoalescing("/.abc", "/.abc", 0, 5);
  testCoalescing("//", "//", 1, 2);
  testCoalescing("/../", "/", 0, 1);
  testCoalescing("/./", "/", 0, 1);
  testCoalescing("/.../", "/.../", 4, 5);

  // Bug 1873915
  testCoalescing("/hello/%2e%2e/foo", "/foo", 0, 4);
  testCoalescing("/hello/%2e%2E/foo", "/foo", 0, 4);
  testCoalescing("/hello/%2E%2e/foo", "/foo", 0, 4);
  testCoalescing("/hello/%2E%2E/foo", "/foo", 0, 4);
  testCoalescing("/hello/%2e./foo", "/foo", 0, 4);
  testCoalescing("/hello/.%2e/foo", "/foo", 0, 4);
  testCoalescing("/hello/%2E./foo", "/foo", 0, 4);
  testCoalescing("/hello/.%2E/foo", "/foo", 0, 4);
  testCoalescing("/hello/%2e%2E/%2e", "/", 0, 1);
  testCoalescing("/hello/%2e/%2e%2e", "/", 0, 1);
  testCoalescing("/hello/%2e%2E/%#", "/%#", 0, 2);
  testCoalescing("/hello/%2e%2E/%?", "/%?", 0, 2);
  testCoalescing("/hello/%2e", "/hello/", 6, 7);
  testCoalescing("/hello/%2E", "/hello/", 6, 7);
  testCoalescing("/hello/%2e/", "/hello/", 6, 7);
  testCoalescing("/hello/%2E/", "/hello/", 6, 7);
  testCoalescing("/hello/%2e.", "/", 0, 1);
  testCoalescing("/hello/.%2e", "/", 0, 1);
  testCoalescing("/hello/%2E.", "/", 0, 1);
  testCoalescing("/hello/.%2E", "/", 0, 1);
  testCoalescing("/hello/%2E%2e/%2e/./../.", "/", 0, 1);
  testCoalescing("/test/%2e%2e/dir/%2E%2e/file", "/file", 0, 5);

  // should not convert as a character is present
  testCoalescing("/hello/%2E%2ea", "/hello/%2E%2ea", 6, 14);
  testCoalescing("/hello/a%2E%2e", "/hello/a%2E%2e", 6, 14);
  testCoalescing("/hello/a%2E%2ea", "/hello/a%2E%2ea", 6, 15);

  // Encoded sequences followed by special characters
  testCoalescing("/foo/%2e%2e/%2e%23/file.txt", "/%2e%23/file.txt", 7, 16);
  testCoalescing("/foo/%2e%2e/%2e%3f/file.txt", "/%2e%3f/file.txt", 7, 16);

  // Query strings and fragments with encoded sequences
  testCoalescing("/query/%2e%2e/path?p=%2e", "/path?p=%2e", 0, 5);
  testCoalescing("/frag/%2e%2e/path#q=%2e", "/path#q=%2e", 0, 5);

  // 3 or more "%2e" repeating should not convert to "."
  testCoalescing("/hello/%2E%2e%2E", "/hello/%2E%2e%2E", 6, 16);
  testCoalescing("/hello/%2E%2e%2E%2e", "/hello/%2E%2e%2E%2e", 6, 19);
  testCoalescing("/hello/%2E%2e%2E%2e.", "/hello/%2E%2e%2E%2e.", 6, 20);

  // Relatively long inputs
  testCoalescing("/foo/bar/foo/bar/foo/bar/foo/bar/foo?query#frag",
                 "/foo/bar/foo/bar/foo/bar/foo/bar/foo?query#frag", 32, 36);

  testCoalescing("/coder/coder/edit/main/docs/./enterprise.md",
                 "/coder/coder/edit/main/docs/enterprise.md", 27, 41);

  // bug 1942820
  testCoalescing("/foo/bar/.%2e", "/foo/", 4, 5);
  testCoalescing("/foo/bar/..", "/foo/", 4, 5);
  testCoalescing("/foo/bar/%2e%2e", "/foo/", 4, 5);
  testCoalescing("/foo/bar/%2e%2e#frag", "/foo/#frag", 4, 5);
  testCoalescing("/foo/bar/%2e%2e?query", "/foo/?query", 4, 5);
}

TEST(TestStandardURL, bug1904582)
{
  auto spec = "x:///%2e%2e"_ns;

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_MutateURI(NS_STANDARDURLMUTATOR_CONTRACTID)
                    .SetSpec(spec)
                    .Finalize(uri);
  ASSERT_TRUE(NS_SUCCEEDED(rv));
}

TEST(TestStandardURL, bug1911529)
{
  nsCOMPtr<nsIURI> uri;
  nsresult rv =
      NS_MutateURI(NS_STANDARDURLMUTATOR_CONTRACTID)
          .SetSpec(
              "https://github.com/coder/coder/edit/main/docs/./enterprise.md"_ns)
          .Finalize(uri);
  ASSERT_TRUE(NS_SUCCEEDED(rv));

  nsAutoCString out;
  ASSERT_EQ(uri->GetSpec(out), NS_OK);
  ASSERT_TRUE(out ==
              "https://github.com/coder/coder/edit/main/docs/enterprise.md"_ns);

  nsCOMPtr<nsIURI> uri2;
  rv = NS_MutateURI(NS_STANDARDURLMUTATOR_CONTRACTID)
           .SetSpec(
               "https://github.com/coder/coder/edit/main/docs/enterprise.md"_ns)
           .Finalize(uri2);
  ASSERT_TRUE(NS_SUCCEEDED(rv));

  bool equals = false;
  ASSERT_EQ(uri->Equals(uri2, &equals), NS_OK);
  ASSERT_TRUE(equals);

  rv = NS_MutateURI(NS_STANDARDURLMUTATOR_CONTRACTID)
           .SetSpec("https://domain.com/."_ns)
           .Finalize(uri);
  ASSERT_TRUE(NS_SUCCEEDED(rv));
  rv = NS_MutateURI(NS_STANDARDURLMUTATOR_CONTRACTID)
           .SetSpec("https://domain.com/"_ns)
           .Finalize(uri2);
  ASSERT_TRUE(NS_SUCCEEDED(rv));

  ASSERT_EQ(uri->Equals(uri2, &equals), NS_OK);
  ASSERT_TRUE(equals);
}

// The setters take nsACString, so nothing guarantees the bytes are UTF-8.
// SetQueryWithEncoding reaches nsSegmentEncoder with a non-null encoding,
// where invalid sequences must be replaced rather than treated as impossible.
TEST(TestStandardURL, NonUtf8QueryWithEncoding)
{
  nsCOMPtr<nsIURI> uri;
  ASSERT_EQ(NS_MutateURI(NS_STANDARDURLMUTATOR_CONTRACTID)
                .SetSpec("http://example.com/"_ns)
                .Finalize(uri),
            NS_OK);

  // A lone continuation byte, a truncated sequence and a byte that can never
  // appear in UTF-8 at all.
  nsAutoCString badQuery(
      "a\xA7"
      "b\xF9"
      "c\xFE");

  const auto* encoding = mozilla::Encoding::ForLabelNoReplacement(
      mozilla::MakeStringSpan("Shift_JIS"));
  ASSERT_TRUE(encoding);

  nsCOMPtr<nsIURI> out;
  ASSERT_EQ(
      NS_MutateURI(uri).SetQueryWithEncoding(badQuery, encoding).Finalize(out),
      NS_OK);

  nsAutoCString spec;
  ASSERT_EQ(out->GetSpec(spec), NS_OK);

  // Each invalid byte became U+FFFD, which Shift_JIS cannot represent, so it
  // is emitted as a numeric character reference and then percent-escaped.
  ASSERT_EQ(
      spec,
      "http://example.com/?a%26%2365533%3Bb%26%2365533%3Bc%26%2365533%3B"_ns);
}
