/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <memory>

#include "GetAddrInfo.h"
#include "gtest/gtest.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gtest/MozAssertions.h"
#include "mozilla/net/DNSByTypeRecord.h"
#include "nsTArray.h"

#undef UNICODE
#include <ws2tcpip.h>
#undef GetAddrInfo
#include <windns.h>

using namespace mozilla;
using namespace mozilla::net;

namespace {

// Builds a linked list of records in the format returned by DnsQuery_A.
class RecordList {
 public:
  void AddServiceMode(const char* aName) {
    // SvcPriority 1, TargetName "."
    const uint8_t rdata[] = {0, 1, 0};
    size_t size = std::max(sizeof(DNS_RECORDA),
                           offsetof(DNS_RECORDA, Data) + sizeof(rdata));
    DNS_RECORDA* record = Append(aName, nsIDNSService::RESOLVE_TYPE_HTTPSSVC,
                                 size, sizeof(rdata));
    memcpy(&record->Data, rdata, sizeof(rdata));
  }

  void AddCname(const char* aName, const char* aTarget) {
    DNS_RECORDA* record = Append(aName, DNS_TYPE_CNAME, sizeof(DNS_RECORDA),
                                 sizeof(DNS_PTR_DATAA));
    record->Data.Cname.pNameHost = const_cast<char*>(aTarget);
  }

  const DNS_RECORDA* Head() const {
    return mBuffers.IsEmpty()
               ? nullptr
               : reinterpret_cast<const DNS_RECORDA*>(mBuffers[0].get());
  }

 private:
  DNS_RECORDA* Append(const char* aName, WORD aType, size_t aSize,
                      WORD aDataLength) {
    auto buffer = std::make_unique<uint8_t[]>(aSize);
    memset(buffer.get(), 0, aSize);
    auto* record = reinterpret_cast<DNS_RECORDA*>(buffer.get());
    record->pName = const_cast<char*>(aName);
    record->wType = aType;
    record->wDataLength = aDataLength;
    record->dwTtl = 60;
    if (mTail) {
      mTail->pNext = record;
    }
    mTail = record;
    mBuffers.AppendElement(std::move(buffer));
    return record;
  }

  nsTArray<UniquePtr<uint8_t[]>> mBuffers;
  DNS_RECORDA* mTail = nullptr;
};

void ExpectOneServiceRecord(const RecordList& aRecords,
                            const nsACString& aHost) {
  TypeRecordResultType result = AsVariant(Nothing());
  uint32_t ttl = 0;
  HTTPSAliasTarget alias;
  EXPECT_NS_SUCCEEDED(ParseHTTPSRecordList(aRecords.Head(), aHost, Nothing(),
                                           result, ttl, alias));
  ASSERT_TRUE(result.is<TypeRecordHTTPSSVC>());
  EXPECT_EQ(result.as<TypeRecordHTTPSSVC>().Length(), 1u);
  EXPECT_EQ(result.as<TypeRecordHTTPSSVC>()[0].mSvcFieldPriority, 1);
}

}  // namespace

TEST(TestPlatformDNSWin, OwnerNameIsCaseInsensitive)
{
  RecordList records;
  records.AddServiceMode("Example.COM");
  ExpectOneServiceRecord(records, "example.com"_ns);
}

TEST(TestPlatformDNSWin, HostWithTrailingDot)
{
  RecordList records;
  records.AddServiceMode("example.com");
  ExpectOneServiceRecord(records, "example.com."_ns);
}

// The CNAME target is lowercased while following it, so the next owner name
// has to be compared case-insensitively.
TEST(TestPlatformDNSWin, MixedCaseCnameTarget)
{
  RecordList records;
  records.AddCname("example.com", "Target.CDN.example");
  records.AddServiceMode("Target.CDN.example");
  ExpectOneServiceRecord(records, "example.com"_ns);
}
