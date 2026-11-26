/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DNSByTypeRecord_h_
#define DNSByTypeRecord_h_

#include "ipc/IPCMessageUtils.h"
#include "mozilla/net/HTTPSSVC.h"
#include "mozilla/net/NeckoMessageUtils.h"

namespace mozilla {
namespace net {

// The types of nsIDNSByTypeRecord: Nothing, TXT, HTTPSSVC
using TypeRecordEmpty = Nothing;
using TypeRecordTxt = CopyableTArray<nsCString>;
using TypeRecordHTTPSSVC = CopyableTArray<SVCB>;

// This variant reflects the multiple types of data a nsIDNSByTypeRecord
// can hold.
using TypeRecordResultType =
    Variant<TypeRecordEmpty, TypeRecordTxt, TypeRecordHTTPSSVC>;

// TypeRecordResultType is a variant, but since it doesn't have a default
// constructor it's not a type we can pass directly over IPC.
struct IPCTypeRecord {
  bool operator==(const IPCTypeRecord& aOther) const {
    return mData == aOther.mData;
  }
  explicit IPCTypeRecord() : mData(Nothing{}) {}
  TypeRecordResultType mData;
  uint32_t mTTL = 0;
  bool mIsTRR = false;
  bool mFromStaleCache = false;
};

}  // namespace net
}  // namespace mozilla

namespace IPC {

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::net::IPCTypeRecord, mData, mTTL,
                                  mIsTRR, mFromStaleCache);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::net::SVCB, mSvcFieldPriority,
                                  mSvcDomainName, mEchConfig, mODoHConfig,
                                  mHasIPHints, mHasEchConfig, mSvcFieldValue);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::net::SvcParamAlpn, mValue);

DEFINE_IPC_SERIALIZER_WITHOUT_FIELDS(mozilla::net::SvcParamNoDefaultAlpn);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::net::SvcParamPort, mValue);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::net::SvcParamIpv4Hint, mValue);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::net::SvcParamEchConfig, mValue);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::net::SvcParamIpv6Hint, mValue);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::net::SvcParamODoHConfig, mValue);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::net::SvcFieldValue, mValue);

}  // namespace IPC

#endif  // DNSByTypeRecord_h_
