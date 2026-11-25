/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

// Log on level :5, instead of default :4.
#undef LOG
#define LOG(args) LOG5(args)
#undef LOG_ENABLED
#define LOG_ENABLED() LOG5_ENABLED()

#define TLS_EARLY_DATA_NOT_AVAILABLE 0
#define TLS_EARLY_DATA_AVAILABLE_BUT_NOT_USED 1
#define TLS_EARLY_DATA_AVAILABLE_AND_USED 2

#include "ConnectionEntry.h"
#include "HttpConnectionBase.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "nsHttpHandler.h"
#include "nsHttpTransaction.h"
#include "nsIClassOfService.h"
#include "nsIOService.h"
#include "nsISocketTransport.h"
#include "xpcpublic.h"

namespace mozilla {
namespace net {

//-----------------------------------------------------------------------------
// nsHttpConnection <public>
//-----------------------------------------------------------------------------

HttpConnectionBase::HttpConnectionBase() {
  LOG(("Creating HttpConnectionBase @%p\n", this));
}

void HttpConnectionBase::BootstrapTimings(TimingStruct times) {
  mBootstrappedTimings = times;
}

void HttpConnectionBase::SetDnsBootstrapTimings(TimeStamp domainLookupStart,
                                                TimeStamp domainLookupEnd) {
  mBootstrappedTimings.domainLookupStart = domainLookupStart;
  mBootstrappedTimings.domainLookupEnd = domainLookupEnd;
}

void HttpConnectionBase::SetConnectBootstrapTimings(
    TimeStamp connectStart, TimeStamp tcpConnectEnd,
    TimeStamp secureConnectionStart, TimeStamp connectEnd) {
  mBootstrappedTimings.connectStart = connectStart;
  if (!tcpConnectEnd.IsNull()) {
    mBootstrappedTimings.tcpConnectEnd = tcpConnectEnd;
  }
  if (!secureConnectionStart.IsNull()) {
    mBootstrappedTimings.secureConnectionStart = secureConnectionStart;
  }
  if (!connectEnd.IsNull()) {
    mBootstrappedTimings.connectEnd = connectEnd;
  }
}

void HttpConnectionBase::HandOffConnectPhase(nsAHttpTransaction* aTrans) {
  // A null transaction (including a Happy Eyeballs racer that has adopted the
  // real transaction) doesn't report timings itself.
  nsHttpTransaction* hTrans =
      aTrans->IsNullTransaction() ? nullptr : aTrans->QueryHttpTransaction();
  if (!hTrans) {
    return;
  }

  // Whatever connect timings aTrans collected so far describe some other
  // connection: our own connect events only reach it once it is activated, so
  // nothing of ours is lost by overwriting them here.
  if (mConnectPhaseHandedOff) {
    // Our connect phase belongs to the transaction we were established for.
    // aTrans reused this connection and reports no connect phase, rather than
    // the one of a connection attempt it ended up not using (bug 2046698).
    hTrans->BootstrapTimings(TimingStruct());
  } else if (!mBootstrappedTimings.connectStart.IsNull()) {
    hTrans->BootstrapTimings(mBootstrappedTimings);
  }
  // Connections without a connect phase of their own (a tunnel, whose connect
  // phase is the one of the connection carrying it) leave aTrans its timings.

  mConnectPhaseHandedOff = true;
  mBootstrappedTimings = TimingStruct();
}

void HttpConnectionBase::SetSecurityCallbacks(
    nsIInterfaceRequestor* aCallbacks) {
  MutexAutoLock lock(mCallbacksLock);
  // This is called both on and off the main thread. For JS-implemented
  // callbacks, we requires that the call happen on the main thread, but
  // for C++-implemented callbacks we don't care. Use a pointer holder with
  // strict checking disabled.
  mCallbacks = new nsMainThreadPtrHolder<nsIInterfaceRequestor>(
      "nsHttpConnection::mCallbacks", aCallbacks, false);
}

void HttpConnectionBase::SetTrafficCategory(HttpTrafficCategory aCategory) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (aCategory == HttpTrafficCategory::eInvalid ||
      mTrafficCategory.Contains(aCategory)) {
    return;
  }
  (void)mTrafficCategory.AppendElement(aCategory);
}

void HttpConnectionBase::ChangeConnectionState(ConnectionState aState) {
  LOG(("HttpConnectionBase::ChangeConnectionState this=%p (%d->%d)", this,
       static_cast<uint32_t>(mConnectionState), static_cast<uint32_t>(aState)));

  // The state can't move backward.
  if (aState <= mConnectionState) {
    return;
  }

  mConnectionState = aState;
}

void HttpConnectionBase::RecordConnectionCloseTelemetry(nsresult aReason) {
  SetCloseReason(ToCloseReason(aReason));
}

void HttpConnectionBase::RecordConnectionAddressType() {
  if (mAddressTypeReported) {
    return;
  }

  NetAddr addr;
  GetPeerAddr(&addr);
  // We allow recording this metric in the test environment.
  if (addr.GetIpAddressSpace() != nsILoadInfo::IPAddressSpace::Public &&
      !xpc::AreNonLocalConnectionsDisabled()) {
    return;
  }

  if (mConnInfo->UsingProxy()) {
    return;
  }

  nsAutoCString key(HttpVersionToTelemetryLabel(Version()));

  if (addr.IsIPAddrV4()) {
    key.Append("_ipv4");
  } else {
    key.Append("_ipv6");
  }

  mozilla::glean::networking::connection_address_type.Get(key).Add(1);
  mAddressTypeReported = true;
}

void HttpConnectionBase::ChangeState(HttpConnectionState newState) {
  LOG(("HttpConnectionBase::ChangeState %d -> %d [this=%p]", mState, newState,
       this));
  mState = newState;
}

nsresult HttpConnectionBase::CheckTunnelIsNeeded(
    nsAHttpTransaction* aTransaction) {
  switch (mState) {
    case HttpConnectionState::UNINITIALIZED: {
      // This is is called first time. Check if we need a tunnel.
      if (!aTransaction->ConnectionInfo()->UsingConnect()) {
        ChangeState(HttpConnectionState::REQUEST);
        return NS_OK;
      }
      ChangeState(HttpConnectionState::SETTING_UP_TUNNEL);
    }
      [[fallthrough]];
    case HttpConnectionState::SETTING_UP_TUNNEL: {
      // When a HttpConnectionBase is in this state that means that an
      // authentication was needed and we are resending a CONNECT
      // request. This request will include authentication headers.
      nsresult rv = SetupProxyConnectStream();
      if (NS_FAILED(rv)) {
        ChangeState(HttpConnectionState::UNINITIALIZED);
      }
      return rv;
    }
    case HttpConnectionState::REQUEST:
      return NS_OK;
  }
  return NS_OK;
}

void HttpConnectionBase::SetOwner(ConnectionEntry* aEntry) {
  mOwnerEntry = aEntry;
}

ConnectionEntry* HttpConnectionBase::OwnerEntry() const {
  return mOwnerEntry.get();
}

}  // namespace net
}  // namespace mozilla
