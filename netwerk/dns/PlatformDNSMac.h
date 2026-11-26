/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef netwerk_dns_PlatformDNSMac_h
#define netwerk_dns_PlatformDNSMac_h

#include <dns_sd.h>

#include "mozilla/Maybe.h"
#include "mozilla/net/DNSByTypeRecord.h"
#include "nsString.h"

namespace mozilla::net {

// State shared with QueryCallback for a single DNSServiceQueryRecord call.
struct DNSContext {
  nsresult mRv = NS_OK;
  TypeRecordResultType* mResult;
  nsCString mHost;
  Maybe<nsCString> mOriginHost;
  uint32_t* mTTL;
  // Set to the TargetName when the response is an HTTPS AliasMode record.
  nsCString mAliasName;
  // Set once the HTTPS RRSet has been delivered. Intermediate records such as
  // CNAMEs arrive in earlier callbacks and don't complete the query.
  bool mDone = false;

  bool IsComplete() const { return mDone || NS_FAILED(mRv); }
};

void QueryCallback(DNSServiceRef aSDRef, DNSServiceFlags aFlags,
                   uint32_t aInterfaceIndex, DNSServiceErrorType aErrorCode,
                   const char* aFullname, uint16_t aRRType, uint16_t aRRClass,
                   uint16_t aRDLen, const void* aRdata, uint32_t aTtl,
                   void* aContext);

}  // namespace mozilla::net

#endif  // netwerk_dns_PlatformDNSMac_h
