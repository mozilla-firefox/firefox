#include "gtest/gtest.h"
#include "mozilla/Preferences.h"
#include "mozilla/net/DNSPacket.h"

using namespace mozilla;
using namespace mozilla::net;

void AssertDnsPadding(uint32_t PaddingLength, unsigned int WithPadding,
                      unsigned int WithoutPadding, bool DisableEcn,
                      const nsCString& host) {
  DNSPacket encoder;
  nsCString buf;

  ASSERT_EQ(Preferences::SetUint("network.trr.padding.length", PaddingLength),
            NS_OK);

  ASSERT_EQ(Preferences::SetBool("network.trr.padding", true), NS_OK);
  ASSERT_EQ(encoder.EncodeRequest(buf, host, 1, DisableEcn), NS_OK);
  ASSERT_EQ(buf.Length(), WithPadding);

  ASSERT_EQ(Preferences::SetBool("network.trr.padding", false), NS_OK);
  ASSERT_EQ(encoder.EncodeRequest(buf, host, 1, DisableEcn), NS_OK);
  ASSERT_EQ(buf.Length(), WithoutPadding);
}

TEST(TestDNSPacket, PaddingLenEcn)
{
  AssertDnsPadding(16, 48, 41, true, "a.de"_ns);
  AssertDnsPadding(16, 48, 42, true, "ab.de"_ns);
  AssertDnsPadding(16, 48, 43, true, "abc.de"_ns);
  AssertDnsPadding(16, 48, 44, true, "abcd.de"_ns);
  AssertDnsPadding(16, 64, 45, true, "abcde.de"_ns);
  AssertDnsPadding(16, 64, 46, true, "abcdef.de"_ns);
  AssertDnsPadding(16, 64, 47, true, "abcdefg.de"_ns);
  AssertDnsPadding(16, 64, 48, true, "abcdefgh.de"_ns);
}

TEST(TestDNSPacket, PaddingLenDisableEcn)
{
  AssertDnsPadding(16, 48, 22, false, "a.de"_ns);
  AssertDnsPadding(16, 48, 23, false, "ab.de"_ns);
  AssertDnsPadding(16, 48, 24, false, "abc.de"_ns);
  AssertDnsPadding(16, 48, 25, false, "abcd.de"_ns);
  AssertDnsPadding(16, 48, 26, false, "abcde.de"_ns);
  AssertDnsPadding(16, 48, 27, false, "abcdef.de"_ns);
  AssertDnsPadding(16, 48, 32, false, "abcdefghijk.de"_ns);
  AssertDnsPadding(16, 48, 33, false, "abcdefghijkl.de"_ns);
  AssertDnsPadding(16, 64, 34, false, "abcdefghijklm.de"_ns);
  AssertDnsPadding(16, 64, 35, false, "abcdefghijklmn.de"_ns);
}

TEST(TestDNSPacket, PaddingLengths)
{
  AssertDnsPadding(0, 45, 41, true, "a.de"_ns);
  AssertDnsPadding(1, 45, 41, true, "a.de"_ns);
  AssertDnsPadding(2, 46, 41, true, "a.de"_ns);
  AssertDnsPadding(3, 45, 41, true, "a.de"_ns);
  AssertDnsPadding(4, 48, 41, true, "a.de"_ns);
  AssertDnsPadding(16, 48, 41, true, "a.de"_ns);
  AssertDnsPadding(32, 64, 41, true, "a.de"_ns);
  AssertDnsPadding(42, 84, 41, true, "a.de"_ns);
  AssertDnsPadding(52, 52, 41, true, "a.de"_ns);
  AssertDnsPadding(80, 80, 41, true, "a.de"_ns);
  AssertDnsPadding(128, 128, 41, true, "a.de"_ns);
  AssertDnsPadding(256, 256, 41, true, "a.de"_ns);
  AssertDnsPadding(1024, 1024, 41, true, "a.de"_ns);
  AssertDnsPadding(1025, 1024, 41, true, "a.de"_ns);
}

namespace {

void AppendBE16(nsCString& aBuf, uint16_t aVal) {
  aBuf += static_cast<char>((aVal >> 8) & 0xff);
  aBuf += static_cast<char>(aVal & 0xff);
}

void AppendHeader(nsCString& aBuf, uint16_t aAnswers) {
  AppendBE16(aBuf, 0);       // id
  AppendBE16(aBuf, 0x8180);  // flags (QR, RD, RA)
  AppendBE16(aBuf, 0);       // qdcount
  AppendBE16(aBuf, aAnswers);
  AppendBE16(aBuf, 0);  // nscount
  AppendBE16(aBuf, 0);  // arcount
}

void AppendRecord(nsCString& aBuf, const nsACString& aOwner, uint16_t aType,
                  const nsCString& aRData) {
  ASSERT_EQ(DNSPacket::EncodeHost(aBuf, aOwner), NS_OK);
  AppendBE16(aBuf, aType);
  AppendBE16(aBuf, 1);  // CLASS IN
  AppendBE16(aBuf, 0);
  AppendBE16(aBuf, 60);  // TTL
  AppendBE16(aBuf, aRData.Length());
  aBuf.Append(aRData);
}

nsCString AliasModeRootRData() {
  nsCString rdata;
  AppendBE16(rdata, 0);  // SvcPriority 0 => AliasMode
  rdata += '\0';         // TargetName "."
  return rdata;
}

nsresult DecodeHTTPS(const nsCString& aPacket, nsCString& aHost,
                     nsCString& aCname, TypeRecordResultType& aResult) {
  DNSPacket packet;
  nsresult rv = packet.FillBuffer([&](unsigned char* aResponse) -> int {
    memcpy(aResponse, aPacket.BeginReading(), aPacket.Length());
    return aPacket.Length();
  });
  if (NS_FAILED(rv)) {
    return rv;
  }
  packet.SetNativePacket(true);
  DOHresp resp;
  nsClassHashtable<nsCStringHashKey, DOHresp> additionalRecords;
  uint32_t ttl = UINT32_MAX;
  return packet.Decode(aHost, TRRTYPE_HTTPSSVC, aCname, true, resp, aResult,
                       additionalRecords, ttl);
}

}  // namespace

// An AliasMode record with TargetName "." means the service doesn't exist.
// Skipping it must still advance past its RDATA, otherwise the decoder reads
// the rest of the response at the wrong offset.
TEST(TestDNSPacket, HTTPSAliasModeRootIsSkipped)
{
  nsCString packet;
  AppendHeader(packet, 1);
  AppendRecord(packet, "example.com"_ns, TRRTYPE_HTTPSSVC,
               AliasModeRootRData());

  nsCString host("example.com"_ns);
  nsCString cname;
  TypeRecordResultType result = AsVariant(Nothing());
  EXPECT_EQ(DecodeHTTPS(packet, host, cname, result), NS_ERROR_UNKNOWN_HOST);
  EXPECT_TRUE(result.is<TypeRecordEmpty>());
}

TEST(TestDNSPacket, HTTPSAliasModeRootFollowedByRecord)
{
  nsCString packet;
  AppendHeader(packet, 2);
  AppendRecord(packet, "example.com"_ns, TRRTYPE_HTTPSSVC,
               AliasModeRootRData());
  nsCString target;
  ASSERT_EQ(DNSPacket::EncodeHost(target, "target.example.net"_ns), NS_OK);
  AppendRecord(packet, "example.com"_ns, TRRTYPE_CNAME, target);

  nsCString host("example.com"_ns);
  nsCString cname;
  TypeRecordResultType result = AsVariant(Nothing());
  EXPECT_EQ(DecodeHTTPS(packet, host, cname, result), NS_OK);
  EXPECT_TRUE(cname.EqualsLiteral("target.example.net"));
}
