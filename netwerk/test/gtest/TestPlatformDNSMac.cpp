/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PlatformDNSMac.h"
#include "gtest/gtest.h"
#include "mozilla/net/DNSPacket.h"

using namespace mozilla;
using namespace mozilla::net;

namespace {

// SvcPriority 1, TargetName "."
const uint8_t kServiceModeRData[] = {0, 1, 0};
// CNAME RDATA for "target.example."
const uint8_t kCnameRData[] = {6,   't', 'a', 'r', 'g', 'e', 't', 7,
                               'e', 'x', 'a', 'm', 'p', 'l', 'e', 0};

class MacQueryCallbackTest : public ::testing::Test {
 protected:
  void Deliver(DNSServiceFlags aFlags, const char* aName, uint16_t aType,
               const uint8_t* aRData, uint16_t aRDLen,
               DNSServiceErrorType aError = kDNSServiceErr_NoError) {
    QueryCallback(nullptr, kDNSServiceFlagsAdd | aFlags, 0, aError, aName,
                  aType, kDNSServiceClass_IN, aRDLen, aRData, 60, &mContext);
  }

  void DeliverServiceMode(DNSServiceFlags aFlags, const char* aName) {
    Deliver(aFlags, aName, TRRTYPE_HTTPSSVC, kServiceModeRData,
            sizeof(kServiceModeRData));
  }

  size_t RecordCount() const {
    return mResult.is<TypeRecordHTTPSSVC>()
               ? mResult.as<TypeRecordHTTPSSVC>().Length()
               : 0;
  }

  TypeRecordResultType mResult = AsVariant(Nothing());
  uint32_t mTTL = UINT32_MAX;
  DNSContext mContext{
      .mResult = &mResult,
      .mHost = "example.com"_ns,
      .mTTL = &mTTL,
  };
};

}  // namespace

// With kDNSServiceFlagsReturnIntermediates the CNAME is delivered in its own
// callback; the query must keep waiting for the HTTPS record of the target.
TEST_F(MacQueryCallbackTest, CnameDoesNotCompleteQuery) {
  Deliver(0, "example.com.", TRRTYPE_CNAME, kCnameRData, sizeof(kCnameRData));
  EXPECT_FALSE(mContext.IsComplete());
  EXPECT_EQ(RecordCount(), 0u);

  DeliverServiceMode(0, "target.example.");
  EXPECT_TRUE(mContext.IsComplete());
  EXPECT_EQ(RecordCount(), 1u);
}

TEST_F(MacQueryCallbackTest, WaitsWhileMoreComing) {
  DeliverServiceMode(kDNSServiceFlagsMoreComing, "example.com.");
  EXPECT_FALSE(mContext.IsComplete());

  DeliverServiceMode(0, "example.com.");
  EXPECT_TRUE(mContext.IsComplete());
  EXPECT_EQ(RecordCount(), 2u);
}

TEST_F(MacQueryCallbackTest, ErrorCompletesQuery) {
  Deliver(0, "example.com.", TRRTYPE_HTTPSSVC, nullptr, 0,
          kDNSServiceErr_NoSuchRecord);
  EXPECT_TRUE(mContext.IsComplete());
  EXPECT_EQ(RecordCount(), 0u);
}
