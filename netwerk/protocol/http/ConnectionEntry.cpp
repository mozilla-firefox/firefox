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

#include "ConnectionEntry.h"
#include "HttpConnectionUDP.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/net/neqo_glue_ffi_generated.h"
#include "nsHttpConnectionMgr.h"
#include "nsHttpHandler.h"
#include "nsQueryObject.h"

namespace mozilla {
namespace net {

// ConnectionEntry
ConnectionEntry::~ConnectionEntry() {
  LOG(("ConnectionEntry::~ConnectionEntry this=%p", this));

  MOZ_ASSERT(!mIdleConns.Length());
  MOZ_ASSERT(!mActiveConns.Length());
  MOZ_ASSERT(!PendingQueueLength());
  MOZ_ASSERT(!UrgentStartQueueLength());
}

ConnectionEntry::ConnectionEntry(nsHttpConnectionInfo* ci,
                                 nsTHashSet<ConnectionEntry*>& aPendingQSet)
    : mConnInfo(ci),
      mUsingSpdy(false),
      mCanUseSpdy(true),
      mPreferIPv4(false),
      mPreferIPv6(false),
      mUsedForConnection(false),
      mPendingQProcessingScheduled(false),
      mPendingQSet(aPendingQSet) {
  LOG(("ConnectionEntry::ConnectionEntry this=%p key=%s", this,
       ci->HashKey().get()));
  mConnectionAttemptPool = new ConnectionAttemptPool(this);
}

bool ConnectionEntry::HasUsableH3Connection() const {
  for (const auto& conn : mActiveConns) {
    // An unusable h3 connection lingering in mActiveConns must not hold the
    // single-H3-per-entry slot: GetH2orH3ActiveConn won't dispatch onto it, so
    // counting it here would make AtActiveConnectionLimit block a replacement
    // forever and wedge any pending transaction (bug 2050384). A
    // still-handshaking connection is not yet connected, so it is still
    // counted.
    RefPtr<HttpConnectionUDP> connUDP = do_QueryObject(conn);
    if (connUDP && !connUDP->IsConnectedAndUnusable()) {
      return true;
    }
  }

  return false;
}

bool ConnectionEntry::HasActiveH3Connection() const {
  return HasUsableH3Connection() ||
         mConnectionAttemptPool->UnconnectedUDPConnsLength() > 0;
}

bool ConnectionEntry::AvailableForDispatchNow() {
  if (mIdleConns.Length() && mIdleConns[0]->CanReuseLikely()) {
    return true;
  }

  return gHttpHandler->ConnMgr()->GetH2orH3ActiveConn(this, false, false) !=
         nullptr;
}

void ConnectionEntry::RemoveConnectionAttempt(ConnectionAttempt* sock,
                                              bool abandon) {
  mConnectionAttemptPool->RemoveConnectionAttempt(sock, abandon);
}

void ConnectionEntry::CloseAllConnectionAttempts() {
  mConnectionAttemptPool->CloseAllConnectionAttempts();
}

void ConnectionEntry::DisallowHttp2() {
  mCanUseSpdy = false;

  // If we have any spdy connections, we want to go ahead and close them when
  // they're done so we can free up some connections.
  for (uint32_t i = 0; i < mActiveConns.Length(); ++i) {
    if (mActiveConns[i]->UsingSpdy()) {
      mActiveConns[i]->DontReuse();
    }
  }
  for (uint32_t i = 0; i < mIdleConns.Length(); ++i) {
    if (mIdleConns[i]->UsingSpdy()) {
      mIdleConns[i]->DontReuse();
    }
  }

  // Can't coalesce if we're not using spdy
  mCoalescingKeys.Clear();
  mAddresses.Clear();
}

void ConnectionEntry::DontReuseHttp3Conn() {
  // If we have any HTTP/3 connections, we want to go ahead and close them when
  // they're done so we can free up some connections.
  for (uint32_t i = 0; i < mActiveConns.Length(); ++i) {
    if (mActiveConns[i]->UsingHttp3()) {
      mActiveConns[i]->DontReuse();
    }
  }

  // Can't coalesce if we're not using http3
  mCoalescingKeys.Clear();
  mAddresses.Clear();
}

void ConnectionEntry::RecordIPFamilyPreference(uint16_t family) {
  LOG(("ConnectionEntry::RecordIPFamilyPreference %p, af=%u", this, family));

  if (family == PR_AF_INET && !mPreferIPv6) {
    mPreferIPv4 = true;
  }

  if (family == PR_AF_INET6 && !mPreferIPv4) {
    mPreferIPv6 = true;
  }

  LOG(("  %p prefer ipv4=%d, ipv6=%d", this, (bool)mPreferIPv4,
       (bool)mPreferIPv6));
}

void ConnectionEntry::ResetIPFamilyPreference() {
  LOG(("ConnectionEntry::ResetIPFamilyPreference %p", this));

  mPreferIPv4 = false;
  mPreferIPv6 = false;
}

bool net::ConnectionEntry::PreferenceKnown() const {
  return (bool)mPreferIPv4 || (bool)mPreferIPv6;
}

size_t ConnectionEntry::PendingQueueLength() const {
  return mPendingQ.PendingQueueLength();
}

bool ConnectionEntry::PendingQueueIsEmpty() const {
  return mPendingQ.PendingQueueIsEmpty();
}

size_t ConnectionEntry::PendingQueueLengthForWindow(uint64_t windowId) const {
  return mPendingQ.PendingQueueLengthForWindow(windowId);
}

void ConnectionEntry::AppendPendingUrgentStartQ(
    nsTArray<RefPtr<PendingTransactionInfo>>& result) {
  mPendingQ.AppendPendingUrgentStartQ(result);
}

void ConnectionEntry::AppendPendingQForFocusedWindow(
    uint64_t windowId, nsTArray<RefPtr<PendingTransactionInfo>>& result,
    uint32_t maxCount) {
  mPendingQ.AppendPendingQForFocusedWindow(windowId, result, maxCount);
  LOG(
      ("ConnectionEntry::AppendPendingQForFocusedWindow [ci=%s], "
       "pendingQ count=%zu for focused window (id=%" PRIu64 ")\n",
       mConnInfo->HashKey().get(), result.Length(), windowId));
}

void ConnectionEntry::AppendPendingQForNonFocusedWindows(
    uint64_t windowId, nsTArray<RefPtr<PendingTransactionInfo>>& result,
    uint32_t maxCount) {
  mPendingQ.AppendPendingQForNonFocusedWindows(windowId, result, maxCount);
  LOG(
      ("ConnectionEntry::AppendPendingQForNonFocusedWindows [ci=%s], "
       "pendingQ count=%zu for non focused window\n",
       mConnInfo->HashKey().get(), result.Length()));
}

void ConnectionEntry::RemoveEmptyPendingQ() { mPendingQ.RemoveEmptyPendingQ(); }

void ConnectionEntry::InsertTransactionSorted(
    nsTArray<RefPtr<PendingTransactionInfo>>& pendingQ,
    PendingTransactionInfo* pendingTransInfo,
    bool aInsertAsFirstForTheSamePriority /*= false*/) {
  mPendingQ.InsertTransactionSorted(pendingQ, pendingTransInfo,
                                    aInsertAsFirstForTheSamePriority);
}

void ConnectionEntry::ReschedTransaction(nsHttpTransaction* aTrans) {
  mPendingQ.ReschedTransaction(aTrans);
}

void ConnectionEntry::InsertTransaction(
    PendingTransactionInfo* pendingTransInfo,
    bool aInsertAsFirstForTheSamePriority /* = false */) {
  mPendingQ.InsertTransaction(pendingTransInfo,
                              aInsertAsFirstForTheSamePriority);
  pendingTransInfo->Transaction()->OnPendingQueueInserted(mConnInfo->HashKey());
  mPendingQSet.EnsureInserted(this);
}

nsTArray<RefPtr<PendingTransactionInfo>>*
ConnectionEntry::GetTransactionPendingQHelper(nsAHttpTransaction* trans) {
  return mPendingQ.GetTransactionPendingQHelper(trans);
}

bool ConnectionEntry::RestrictConnections() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (AvailableForDispatchNow()) {
    // this might be a h2/spdy connection in this connection entry that
    // is able to be immediately muxxed, or it might be one that
    // was found in the same state through a coalescing hash
    LOG(
        ("ConnectionEntry::RestrictConnections %p %s restricted due to "
         "AvailableForDispatchNow()==true\n",
         this, mConnInfo->HashKey().get()));
    return true;
  }

  // If this host is trying to negotiate a SPDY session right now,
  // don't create any new ssl connections until the result of the
  // negotiation is known.

  bool doRestrict = mConnInfo->FirstHopSSL() &&
                    StaticPrefs::network_http_http2_enabled() && mUsingSpdy &&
                    (mConnectionAttemptPool->Length() || mActiveConns.Length());

  // If there are no restrictions, we are done
  if (!doRestrict) {
    return false;
  }

  // If the restriction is based on a tcp handshake in progress
  // let that connect and then see if it was SPDY or not
  if (mConnectionAttemptPool->UnconnectedConnectionAttempts()) {
    LOG(
        ("ConnectionEntry::RestrictConnections %p %s restricted: "
         "%u unconnected HCA(s) still negotiating (pool length=%zu)\n",
         this, mConnInfo->HashKey().get(),
         mConnectionAttemptPool->UnconnectedConnectionAttempts(),
         mConnectionAttemptPool->Length()));
    return true;
  }

  // There is a concern that a host is using a mix of HTTP/1 and SPDY.
  // In that case we don't want to restrict connections just because
  // there is a single active HTTP/1 session in use.
  // When a tunnel is used, we should avoid bypassing connection restrictions.
  // Otherwise, we might create too many unused tunnels.
  if (mUsingSpdy && mActiveConns.Length() &&
      !(mConnInfo->UsingHttpsProxy() && mConnInfo->UsingConnect())) {
    bool confirmedRestrict = false;
    for (uint32_t index = 0; index < mActiveConns.Length(); ++index) {
      HttpConnectionBase* conn = mActiveConns[index];
      RefPtr<nsHttpConnection> connTCP = do_QueryObject(conn);
      bool npnPending = connTCP && !connTCP->ReportedNPN();
      bool canActivate = conn->CanDirectlyActivate();
      LOG(
          ("ConnectionEntry::RestrictConnections %p %s active conn[%u]=%p "
           "npnPending=%d canActivate=%d dontReuse=%d\n",
           this, mConnInfo->HashKey().get(), index, conn, npnPending,
           canActivate, !conn->CanReuse()));
      if (npnPending || canActivate) {
        confirmedRestrict = true;
        break;
      }
    }
    doRestrict = confirmedRestrict;
    if (!confirmedRestrict) {
      LOG(
          ("nsHttpConnectionMgr spdy connection restriction to "
           "%s bypassed.\n",
           mConnInfo->Origin()));
    }
  }
  return doRestrict;
}

uint32_t ConnectionEntry::TotalActiveConnections() const {
  // Add in the in-progress tcp connections, we will assume they are
  // keepalive enabled.
  // Exclude DnsAndConnectSocket's that has already created a usable connection.
  // This prevents the limit being stuck on ipv6 connections that
  // eventually time out after typical 21 seconds of no ACK+SYN reply.
  return mActiveConns.Length() +
         mConnectionAttemptPool->UnconnectedConnectionAttempts();
}

size_t ConnectionEntry::UrgentStartQueueLength() {
  return mPendingQ.UrgentStartQueueLength();
}

bool ConnectionEntry::UrgentStartQueueIsEmpty() const {
  return mPendingQ.UrgentStartQueueIsEmpty();
}

void ConnectionEntry::PrintPendingQ() { mPendingQ.PrintPendingQ(); }

void ConnectionEntry::Compact() {
  mIdleConns.Compact();
  mActiveConns.Compact();
  mPendingQ.Compact();
}

void ConnectionEntry::RemoveFromIdleConnectionsIndex(size_t inx) {
  mIdleConns.RemoveElementAt(inx);
  gHttpHandler->ConnMgr()->DecrementNumIdleConns();
}

bool ConnectionEntry::RemoveFromIdleConnections(nsHttpConnection* conn) {
  if (!mIdleConns.RemoveElement(conn)) {
    return false;
  }

  gHttpHandler->ConnMgr()->DecrementNumIdleConns();
  return true;
}

void ConnectionEntry::CancelAllTransactions(nsresult reason) {
  mPendingQ.CancelAllTransactions(reason);
  MaybeRemoveFromPendingSet();
}

void ConnectionEntry::MaybeRemoveFromPendingSet() {
  if (PendingQueueIsEmpty() && UrgentStartQueueIsEmpty()) {
    mPendingQSet.Remove(this);
  }
}

nsresult ConnectionEntry::CloseIdleConnection(nsHttpConnection* conn) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  RefPtr<nsHttpConnection> deleteProtector(conn);
  if (!RemoveFromIdleConnections(conn)) {
    return NS_ERROR_UNEXPECTED;
  }

  // The connection is closed immediately no need to call EndIdleMonitoring.
  conn->Close(NS_ERROR_ABORT);
  return NS_OK;
}

void ConnectionEntry::CloseIdleConnections() {
  while (mIdleConns.Length()) {
    RefPtr<nsHttpConnection> conn(mIdleConns[0]);
    RemoveFromIdleConnectionsIndex(0);
    // The connection is closed immediately no need to call EndIdleMonitoring.
    conn->Close(NS_ERROR_ABORT);
  }
}

void ConnectionEntry::CloseIdleConnections(uint32_t maxToClose) {
  uint32_t closed = 0;
  while (mIdleConns.Length() && (closed < maxToClose)) {
    RefPtr<nsHttpConnection> conn(mIdleConns[0]);
    RemoveFromIdleConnectionsIndex(0);
    // The connection is closed immediately no need to call EndIdleMonitoring.
    conn->Close(NS_ERROR_ABORT);
    closed++;
  }
}

void ConnectionEntry::CloseExtendedCONNECTConnections() {
  while (mExtendedCONNECTConns.Length()) {
    RefPtr<HttpConnectionBase> conn(mExtendedCONNECTConns[0]);
    mExtendedCONNECTConns.RemoveElementAt(0);

    // safe to close connection since we are on the socket thread
    // closing via transaction to break connection/transaction bond
    conn->CloseTransaction(conn->Transaction(), NS_ERROR_ABORT, true);
  }
}

nsresult ConnectionEntry::RemoveIdleConnection(nsHttpConnection* conn) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (!RemoveFromIdleConnections(conn)) {
    return NS_ERROR_UNEXPECTED;
  }

  conn->EndIdleMonitoring();
  return NS_OK;
}

bool ConnectionEntry::IsInIdleConnections(HttpConnectionBase* conn) {
  RefPtr<nsHttpConnection> connTCP = do_QueryObject(conn);
  return connTCP && mIdleConns.Contains(connTCP);
}

already_AddRefed<nsHttpConnection> ConnectionEntry::GetIdleConnection(
    bool respectUrgency, bool urgentTrans, bool* onlyUrgent) {
  RefPtr<nsHttpConnection> conn;
  size_t index = 0;
  while (!conn && (mIdleConns.Length() > index)) {
    conn = mIdleConns[index];

    if (!conn->CanReuse()) {
      RemoveFromIdleConnectionsIndex(index);
      LOG(("   dropping stale connection: [conn=%p]\n", conn.get()));
      conn->Close(NS_ERROR_ABORT);
      conn = nullptr;
      continue;
    }

    // non-urgent transactions can only be dispatched on non-urgent
    // started or used connections.
    if (respectUrgency && conn->IsUrgentStartPreferred() && !urgentTrans) {
      LOG(("  skipping urgent: [conn=%p]", conn.get()));
      conn = nullptr;
      ++index;
      continue;
    }

    *onlyUrgent = false;

    RemoveFromIdleConnectionsIndex(index);
    conn->EndIdleMonitoring();
    LOG(("   reusing connection: [conn=%p]\n", conn.get()));
  }

  return conn.forget();
}

nsresult ConnectionEntry::RemoveActiveConnection(HttpConnectionBase* conn) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (!mActiveConns.RemoveElement(conn)) {
    return NS_ERROR_UNEXPECTED;
  }
  conn->SetOwner(nullptr);
  gHttpHandler->ConnMgr()->DecrementActiveConnCount(conn);

  return NS_OK;
}

nsresult ConnectionEntry::RemovePendingConnection(HttpConnectionBase* conn) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (!mPendingConns.RemoveElement(conn)) {
    return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

void ConnectionEntry::ClosePersistentConnections() {
  LOG(("ConnectionEntry::ClosePersistentConnections [ci=%s]\n",
       mConnInfo->HashKey().get()));
  CloseIdleConnections();

  int32_t activeCount = mActiveConns.Length();
  for (int32_t i = 0; i < activeCount; i++) {
    mActiveConns[i]->DontReuse();
  }

  mCoalescingKeys.Clear();
  mAddresses.Clear();
}

uint32_t ConnectionEntry::PruneDeadConnections() {
  uint32_t timeToNextExpire = UINT32_MAX;

  for (int32_t len = mIdleConns.Length(); len > 0; --len) {
    int32_t idx = len - 1;
    RefPtr<nsHttpConnection> conn(mIdleConns[idx]);
    if (!conn->CanReuse()) {
      RemoveFromIdleConnectionsIndex(idx);
      // The connection is closed immediately no need to call
      // EndIdleMonitoring.
      conn->Close(NS_ERROR_ABORT);
    } else {
      timeToNextExpire = std::min(timeToNextExpire, conn->TimeToLive());
    }
  }

  if (mUsingSpdy) {
    for (uint32_t i = 0; i < mActiveConns.Length(); ++i) {
      RefPtr<nsHttpConnection> connTCP = do_QueryObject(mActiveConns[i]);
      // Http3 has its own timers, it is not using this one.
      if (connTCP && connTCP->UsingSpdy()) {
        if (!connTCP->CanReuse()) {
          // Marking it don't-reuse will create an active
          // tear down if the spdy session is idle.
          connTCP->DontReuse();
        } else {
          timeToNextExpire = std::min(timeToNextExpire, connTCP->TimeToLive());
        }
      }
    }
  }

  return timeToNextExpire;
}

void ConnectionEntry::MakeConnectionPendingAndDontReuse(
    HttpConnectionBase* conn) {
  gHttpHandler->ConnMgr()->DecrementActiveConnCount(conn);
  mPendingConns.AppendElement(conn);
  // After DontReuse(), the connection will be closed after the last
  // transition is done.
  conn->DontReuse();
  LOG(("Move active connection to pending list [conn=%p]\n", conn));
}

void ConnectionEntry::MoveUnusableH3ConnsToPending() {
  // A WebTransport connection reports CanReuse()==false for its whole lifetime
  // (pooling is disabled while it carries a WebTransport session), so it must
  // not be treated as unusable and torn down here.
  if (mConnInfo->GetWebTransport()) {
    return;
  }

  // Walk backwards so removals don't shift the indices we haven't visited.
  for (int32_t i = mActiveConns.Length() - 1; i >= 0; --i) {
    // An HttpConnectionUDP is always an HTTP/3 connection. Only move it once it
    // has connected and become unusable; a still-handshaking connection reports
    // CanReuse()==false too but must not be torn down here.
    RefPtr<HttpConnectionUDP> connUDP = do_QueryObject(mActiveConns[i]);
    if (!connUDP || !connUDP->IsConnectedAndUnusable()) {
      continue;
    }
    RefPtr<HttpConnectionBase> conn = mActiveConns[i];
    LOG(
        ("ConnectionEntry::MoveUnusableH3ConnsToPending [ci=%s conn=%p] "
         "moving unusable HTTP/3 connection to pending list\n",
         mConnInfo->HashKey().get(), conn.get()));
    mActiveConns.RemoveElementAt(i);
    conn->SetOwner(nullptr);
    MakeConnectionPendingAndDontReuse(conn);
  }
}

template <typename ConnType>
static void CheckForTrafficForConns(nsTArray<RefPtr<ConnType>>& aConns,
                                    bool aCheck) {
  for (uint32_t index = 0; index < aConns.Length(); ++index) {
    RefPtr<nsHttpConnection> conn = do_QueryObject(aConns[index]);
    if (conn) {
      conn->CheckForTraffic(aCheck);
    }
  }
}

void ConnectionEntry::VerifyTraffic() {
  CheckForTrafficForConns(mPendingConns, true);
  // Iterate the idle connections and unmark them for traffic checks.
  CheckForTrafficForConns(mIdleConns, false);

  uint32_t numConns = mActiveConns.Length();
  if (numConns) {
    // Walk the list backwards to allow us to remove entries easily.
    for (int index = numConns - 1; index >= 0; index--) {
      RefPtr<nsHttpConnection> conn = do_QueryObject(mActiveConns[index]);
      RefPtr<HttpConnectionUDP> connUDP = do_QueryObject(mActiveConns[index]);
      if (conn) {
        conn->CheckForTraffic(true);
        if (conn->EverUsedSpdy() &&
            StaticPrefs::
                network_http_move_to_pending_list_after_network_change()) {
          mActiveConns.RemoveElementAt(index);
          conn->SetOwner(nullptr);
          MakeConnectionPendingAndDontReuse(conn);
        }
      } else if (connUDP &&
                 StaticPrefs::
                     network_http_move_to_pending_list_after_network_change()) {
        mActiveConns.RemoveElementAt(index);
        connUDP->SetOwner(nullptr);
        MakeConnectionPendingAndDontReuse(connUDP);
      }
    }
  }
}

void ConnectionEntry::InsertIntoIdleConnections_internal(
    nsHttpConnection* conn) {
  uint32_t idx;
  for (idx = 0; idx < mIdleConns.Length(); idx++) {
    nsHttpConnection* idleConn = mIdleConns[idx];
    if (idleConn->MaxBytesRead() < conn->MaxBytesRead()) {
      break;
    }
  }

  mIdleConns.InsertElementAt(idx, conn);
}

void ConnectionEntry::InsertIntoIdleConnections(nsHttpConnection* conn) {
  InsertIntoIdleConnections_internal(conn);
  gHttpHandler->ConnMgr()->NewIdleConnectionAdded(conn->TimeToLive());
  conn->BeginIdleMonitoring();
}

bool ConnectionEntry::IsInActiveConns(HttpConnectionBase* conn) {
  return mActiveConns.Contains(conn);
}

void ConnectionEntry::InsertIntoActiveConns(HttpConnectionBase* conn) {
  mActiveConns.AppendElement(conn);
  conn->SetOwner(this);
  gHttpHandler->ConnMgr()->IncrementActiveConnCount();
}

bool ConnectionEntry::IsInExtendedCONNECTConns(HttpConnectionBase* conn) {
  return mExtendedCONNECTConns.Contains(conn);
}

void ConnectionEntry::InsertIntoExtendedCONNECTConns(HttpConnectionBase* conn) {
  // no incrementing of connection count since it is a tunneled connection
  mExtendedCONNECTConns.AppendElement(conn);
}

void ConnectionEntry::RemoveExtendedCONNECTConns(HttpConnectionBase* conn) {
  mExtendedCONNECTConns.RemoveElement(conn);
}

void ConnectionEntry::MakeAllDontReuseExcept(HttpConnectionBase* conn) {
  RefPtr<HttpConnectionUDP> connUDP = do_QueryObject(conn);
  for (uint32_t index = 0; index < mActiveConns.Length(); ++index) {
    HttpConnectionBase* otherConn = mActiveConns[index];
    if (otherConn != conn) {
      // Through an HTTP/3 proxy, the UDP connections of this entry are proxy
      // connections and the TCP ones are tunnels over them, so one kind
      // doesn't replace the other.
      if (IsHttp3ProxyConnection()) {
        RefPtr<HttpConnectionUDP> otherUDP = do_QueryObject(otherConn);
        if (!connUDP != !otherUDP) {
          continue;
        }
      }
      LOG(
          ("ConnectionEntry::MakeAllDontReuseExcept shutting down old "
           "connection (%p) "
           "because new "
           "spdy connection (%p) takes precedence\n",
           otherConn, conn));
      otherConn->SetCloseReason(
          ConnectionCloseReason::CLOSE_EXISTING_CONN_FOR_COALESCING);
      otherConn->DontReuse();
    }
  }

  // Cancel any other pending connections - their associated transactions
  // are in the pending queue and will be dispatched onto this new connection.
  // Skip for fallback entries: their DnsAndConnectSockets are for
  // FallbackTransactions whose real transactions are in the H3 entry.
  if (!mConnInfo->GetFallbackConnection()) {
    CloseAllConnectionAttempts();
  }
}

bool ConnectionEntry::FindConnToClaim(
    PendingTransactionInfo* pendingTransInfo) {
  nsHttpTransaction* trans = pendingTransInfo->Transaction();

  if (mConnectionAttemptPool->FindConnToClaim(pendingTransInfo)) {
    return true;
  }

  // consider null transactions that are being used to drive the ssl handshake
  // if the transaction creating this connection can re-use persistent
  // connections
  if (trans->Caps() & NS_HTTP_ALLOW_KEEPALIVE) {
    uint32_t activeLength = mActiveConns.Length();
    for (uint32_t i = 0; i < activeLength; i++) {
      if (pendingTransInfo->TryClaimingActiveConn(mActiveConns[i])) {
        LOG(
            ("ConnectionEntry::FindConnectingSocket [ci = %s] "
             "Claiming a null transaction for later use\n",
             mConnInfo->HashKey().get()));
        return true;
      }
    }
  }
  return false;
}

bool ConnectionEntry::MakeFirstActiveSpdyConnDontReuse() {
  if (!mUsingSpdy) {
    return false;
  }

  for (uint32_t index = 0; index < mActiveConns.Length(); ++index) {
    HttpConnectionBase* conn = mActiveConns[index];
    if (conn->UsingSpdy() && conn->CanReuse()) {
      conn->DontReuse();
      return true;
    }
  }
  return false;
}

// Return an active h2 or h3 connection
// that can be directly activated or null.
HttpConnectionBase* ConnectionEntry::GetH2orH3ActiveConn(bool aNoHttp2,
                                                         bool aNoHttp3) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  HttpConnectionBase* experienced = nullptr;
  HttpConnectionBase* noExperience = nullptr;
  uint32_t activeLen = mActiveConns.Length();

  // activeLen should generally be 1.. this is a setup race being resolved
  // take a conn who can activate and is experienced
  for (uint32_t index = 0; index < activeLen; ++index) {
    HttpConnectionBase* tmp = mActiveConns[index];
    if (tmp->CanDirectlyActivate()) {
      if (tmp->IsExperienced()) {
        experienced = tmp;
        break;
      }
      noExperience = tmp;  // keep looking for a better option
    }
  }

  auto allowedToReturn = [](HttpConnectionBase* aConn, bool aNoHttp2,
                            bool aNoHttp3) {
    if (aConn->UsingHttp3() && aNoHttp3) {
      return false;
    }

    if (aConn->UsingSpdy() && aNoHttp2) {
      return false;
    }

    return true;
  };

  // if that worked, cleanup anything else and exit
  if (experienced) {
    for (uint32_t index = 0; index < activeLen; ++index) {
      HttpConnectionBase* tmp = mActiveConns[index];
      // in the case where there is a functional h2 session, drop the others
      if (tmp != experienced) {
        tmp->DontReuse();
      }
    }

    LOG(
        ("GetH2orH3ActiveConn() request for ent %p %s "
         "found an active experienced connection %p in native connection "
         "entry\n",
         this, mConnInfo->HashKey().get(), experienced));
    if (!allowedToReturn(experienced, aNoHttp2, aNoHttp3)) {
      return nullptr;
    }
    return experienced;
  }

  if (noExperience) {
    LOG(
        ("GetH2orH3ActiveConn() request for ent %p %s "
         "found an active but inexperienced connection %p in native connection "
         "entry\n",
         this, mConnInfo->HashKey().get(), noExperience));
    if (!allowedToReturn(noExperience, aNoHttp2, aNoHttp3)) {
      return nullptr;
    }
    return noExperience;
  }

  return nullptr;
}

already_AddRefed<nsHttpConnection> ConnectionEntry::GetH2TunnelActiveConn() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  for (const auto& conn : mActiveConns) {
    RefPtr<nsHttpConnection> connTCP = do_QueryObject(conn);
    if (connTCP && connTCP->UsingSpdy() && connTCP->CanDirectlyActivate()) {
      LOG(
          ("GetH2TunnelActiveConn() request for ent %p %s "
           "found an H2 tunnel connection %p\n",
           this, mConnInfo->HashKey().get(), connTCP.get()));
      return connTCP.forget();
    }
  }

  return nullptr;
}

void ConnectionEntry::CloseActiveConnections() {
  while (mActiveConns.Length()) {
    RefPtr<HttpConnectionBase> conn(mActiveConns[0]);
    mActiveConns.RemoveElementAt(0);
    conn->SetOwner(nullptr);
    gHttpHandler->ConnMgr()->DecrementActiveConnCount(conn);

    // Since HttpConnectionBase::Close doesn't break the bond with
    // the connection's transaction, we must explicitely tell it
    // to close its transaction and not just self.
    conn->CloseTransaction(conn->Transaction(), NS_ERROR_ABORT, true);
  }
}

void ConnectionEntry::CloseAllActiveConnsWithNullTransactcion(
    nsresult aCloseCode) {
  for (uint32_t index = 0; index < mActiveConns.Length(); ++index) {
    RefPtr<HttpConnectionBase> activeConn = mActiveConns[index];
    nsAHttpTransaction* liveTransaction = activeConn->Transaction();
    if (liveTransaction && liveTransaction->IsNullTransaction()) {
      LOG(
          ("ConnectionEntry::CloseAllActiveConnsWithNullTransactcion "
           "also canceling Null Transaction %p on conn %p\n",
           liveTransaction, activeConn.get()));
      activeConn->CloseTransaction(liveTransaction, aCloseCode);
    }
  }
}

void ConnectionEntry::ClosePendingConnections() {
  while (mPendingConns.Length()) {
    RefPtr<HttpConnectionBase> conn(mPendingConns[0]);
    mPendingConns.RemoveElementAt(0);

    // Since HttpConnectionBase::Close doesn't break the bond with
    // the connection's transaction, we must explicitely tell it
    // to close its transaction and not just self.
    conn->CloseTransaction(conn->Transaction(), NS_ERROR_ABORT, true);
  }
}

void ConnectionEntry::PruneNoTraffic() {
  LOG(("  pruning no traffic [ci=%s]\n", mConnInfo->HashKey().get()));
  uint32_t numConns = mActiveConns.Length();
  if (numConns) {
    // Walk the list backwards to allow us to remove entries easily.
    for (int index = numConns - 1; index >= 0; index--) {
      RefPtr<nsHttpConnection> conn = do_QueryObject(mActiveConns[index]);
      if (conn && conn->NoTraffic()) {
        mActiveConns.RemoveElementAt(index);
        conn->SetOwner(nullptr);
        gHttpHandler->ConnMgr()->DecrementActiveConnCount(conn);
        conn->Close(NS_ERROR_ABORT);
        LOG(
            ("  closed active connection due to no traffic "
             "[conn=%p]\n",
             conn.get()));
      }
    }
  }
}

uint32_t ConnectionEntry::TimeoutTick() {
  uint32_t timeoutTickNext = 3600;  // 1hr

  LOG(
      ("ConnectionEntry::TimeoutTick() this=%p host=%s "
       "idle=%zu active=%zu"
       " dnsAndSock-len=%zu pending=%zu"
       " urgentStart pending=%zu\n",
       this, mConnInfo->Origin(), IdleConnectionsLength(), ActiveConnsLength(),
       mConnectionAttemptPool->Length(), PendingQueueLength(),
       UrgentStartQueueLength()));

  // First call the tick handler for each active connection.
  PRIntervalTime tickTime = PR_IntervalNow();
  for (uint32_t index = 0; index < mActiveConns.Length(); ++index) {
    RefPtr<nsHttpConnection> conn = do_QueryObject(mActiveConns[index]);
    if (conn) {
      uint32_t connNextTimeout = conn->ReadTimeoutTick(tickTime);
      timeoutTickNext = std::min(timeoutTickNext, connNextTimeout);
      continue;
    }
    RefPtr<HttpConnectionUDP> udpConn = do_QueryObject(mActiveConns[index]);
    if (udpConn) {
      uint32_t nextTickInSeconds = udpConn->ReadTimeoutTick();
      timeoutTickNext = std::min(timeoutTickNext, nextTickInSeconds);
    }
  }

  // Now check for any stalled DnsAndConnectSockets.
  mConnectionAttemptPool->TimeoutTick();
  if (mConnectionAttemptPool->Length()) {
    timeoutTickNext = 1;
  }

  return timeoutTickNext;
}

void ConnectionEntry::MoveConnection(HttpConnectionBase* proxyConn,
                                     ConnectionEntry* otherEnt) {
  // To avoid changing mNumActiveConns/mNumIdleConns counter use internal
  // functions.
  RefPtr<HttpConnectionBase> deleteProtector(proxyConn);
  if (mActiveConns.RemoveElement(proxyConn)) {
    otherEnt->mActiveConns.AppendElement(proxyConn);
    proxyConn->SetOwner(otherEnt);
    return;
  }

  RefPtr<nsHttpConnection> proxyConnTCP = do_QueryObject(proxyConn);
  if (proxyConnTCP) {
    if (mIdleConns.RemoveElement(proxyConnTCP)) {
      otherEnt->InsertIntoIdleConnections_internal(proxyConnTCP);
      return;
    }
  }
}

HttpRetParams ConnectionEntry::GetConnectionData() {
  HttpRetParams data;
  data.host = mConnInfo->Origin();
  data.port = mConnInfo->OriginPort();
  mConnInfo->GetOriginAttributes().CreateSuffix(data.originAttributesSuffix);
  for (uint32_t i = 0; i < mActiveConns.Length(); i++) {
    HttpConnInfo info;
    RefPtr<nsHttpConnection> connTCP = do_QueryObject(mActiveConns[i]);
    if (connTCP) {
      info.ttl = connTCP->TimeToLive();
    } else {
      info.ttl = 0;
    }
    info.rtt = mActiveConns[i]->Rtt();
    info.SetHTTPProtocolVersion(mActiveConns[i]->Version());
    data.active.AppendElement(info);
  }
  for (uint32_t i = 0; i < mIdleConns.Length(); i++) {
    HttpConnInfo info;
    info.ttl = mIdleConns[i]->TimeToLive();
    info.rtt = mIdleConns[i]->Rtt();
    info.SetHTTPProtocolVersion(mIdleConns[i]->Version());
    data.idle.AppendElement(info);
  }
  mConnectionAttemptPool->GetConnectionData(data);
  if (mConnInfo->IsHttp3()) {
    data.httpVersion = "HTTP/3"_ns;
  } else if (mUsingSpdy) {
    data.httpVersion = "HTTP/2"_ns;
  } else {
    data.httpVersion = "HTTP <= 1.1"_ns;
  }
  data.ssl = mConnInfo->EndToEndSSL();
  return data;
}

Http3ConnectionStatsParams ConnectionEntry::GetHttp3ConnectionStatsData() {
  Http3ConnectionStatsParams data;

  data.host = mConnInfo->Origin();
  data.port = mConnInfo->OriginPort();

  for (uint32_t i = 0; i < mActiveConns.Length(); i++) {
    RefPtr<HttpConnectionUDP> connUDP = do_QueryObject(mActiveConns[i]);
    if (!connUDP) {
      continue;
    }

    Http3Stats stats = connUDP->GetStats();
    Http3ConnStats res;
    res.packetsRx = stats.packets_rx;
    res.dupsRx = stats.dups_rx;
    res.droppedRx = stats.dropped_rx;
    res.savedDatagrams = stats.saved_datagrams;
    res.packetsTx = stats.packets_tx;
    res.lost = stats.lost;
    res.lateAck = stats.late_ack;
    res.ptoAck = stats.pto_ack;
    res.wouldBlockRx = stats.would_block_rx;
    res.wouldBlockTx = stats.would_block_tx;
    res.ptoCounts.AppendElements(&stats.pto_counts[0], 16);

    data.stats.AppendElement(std::move(res));
  }
  return data;
}

void ConnectionEntry::LogConnections() {
  LOG(("active conns ["));
  for (HttpConnectionBase* conn : mActiveConns) {
    if (conn->CanDirectlyActivate()) {
      LOG(("  %p (ready=1)", conn));
    } else {
      RefPtr<nsHttpConnection> connTCP = do_QueryObject(conn);
      LOG(("  %p (ready=0 reason=%s)", conn,
           connTCP ? connTCP->CanDirectlyActivateReason() : "not-tcp-conn"));
    }
  }

  LOG(("] idle conns ["));
  for (nsHttpConnection* conn : mIdleConns) {
    LOG(("  %p", conn));
  }
  LOG(("]"));
}

bool ConnectionEntry::RemoveTransFromPendingQ(nsHttpTransaction* aTrans) {
  // We will abandon all DnsAndConnectSockets belonging to the given
  // transaction.
  nsTArray<RefPtr<PendingTransactionInfo>>* infoArray =
      GetTransactionPendingQHelper(aTrans);

  RefPtr<PendingTransactionInfo> pendingTransInfo;
  int32_t transIndex =
      infoArray ? infoArray->IndexOf(aTrans, 0, PendingComparator()) : -1;
  if (transIndex >= 0) {
    pendingTransInfo = (*infoArray)[transIndex];
    infoArray->RemoveElementAt(transIndex);
    if (!(aTrans->Caps() & NS_HTTP_URGENT_START)) {
      mPendingQ.OnPendingTransactionRemovedFromTable();
    }
  }

  if (!pendingTransInfo) {
    return false;
  }

  // Abandon all DnsAndConnectSockets belonging to the given transaction.
  nsWeakPtr tmp = pendingTransInfo->ForgetConnectionAttemptAndActiveConn();
  RefPtr<ConnectionAttempt> sock = do_QueryReferent(tmp);
  if (sock) {
    RemoveConnectionAttempt(sock, true);
  }
  MaybeRemoveFromPendingSet();
  return true;
}

void ConnectionEntry::MaybeUpdateEchConfig(nsHttpConnectionInfo* aConnInfo) {
  if (!mConnInfo->HashKey().Equals(aConnInfo->HashKey())) {
    return;
  }

  const nsCString& echConfig = aConnInfo->GetEchConfig();
  if (mConnInfo->GetEchConfig().Equals(echConfig)) {
    return;
  }

  LOG(("ConnectionEntry::MaybeUpdateEchConfig [ci=%s]\n",
       mConnInfo->HashKey().get()));

  mConnInfo = mConnInfo->Mutate().SetEchConfig(echConfig).Finalize();

  // If echConfig is changed, we should close all DnsAndConnectSockets and idle
  // connections. This is to make sure the new echConfig will be used for the
  // next connection.
  CloseAllConnectionAttempts();
  CloseIdleConnections();
}

bool ConnectionEntry::MaybeProcessCoalescingKeys(nsIDNSAddrRecord* dnsRecord,
                                                 bool aIsHttp3) {
  if (!dnsRecord) {
    return false;
  }

  nsTArray<NetAddr> addresses;
  if (NS_FAILED(dnsRecord->GetAddresses(addresses))) {
    return false;
  }

  return MaybeProcessCoalescingKeys(addresses, aIsHttp3);
}

bool ConnectionEntry::MaybeProcessCoalescingKeys(
    const nsTArray<NetAddr>& aAddresses, bool aIsHttp3) {
  if (!mConnInfo || !mConnInfo->EndToEndSSL() || (!aIsHttp3 && !AllowHttp2()) ||
      mConnInfo->UsingProxy() || !mCoalescingKeys.IsEmpty()) {
    return false;
  }

  if (aAddresses.IsEmpty()) {
    return false;
  }
  mAddresses = aAddresses.Clone();

  nsAutoCString suffix;
  mConnInfo->GetOriginAttributes().CreateSuffix(suffix);

  const char* anonFlag = mConnInfo->GetAnonymous() ? "~A:" : "~.:";
  const char* fallbackFlag = mConnInfo->GetFallbackConnection() ? "~F:" : "~.:";
  int32_t port = mConnInfo->OriginPort();

  nsCString newKey;
  for (uint32_t i = 0; i < mAddresses.Length(); ++i) {
    if ((mAddresses[i].raw.family == AF_INET && mAddresses[i].inet.ip == 0) ||
        (mAddresses[i].raw.family == AF_INET6 &&
         mAddresses[i].inet6.ip.u64[0] == 0 &&
         mAddresses[i].inet6.ip.u64[1] == 0)) {
      // Bug 1680249 - Don't create the coalescing key if the ip address is
      // `0.0.0.0` or `::`.
      LOG(
          ("ConnectionEntry::MaybeProcessCoalescingKeys skip creating "
           "Coalescing Key for host [%s]",
           mConnInfo->Origin()));
      continue;
    }
    newKey.Truncate();
    newKey.SetCapacity(kIPv6CStrBufSize + suffix.Length() + 21);
    mAddresses[i].ToString(newKey);
    newKey.Append(anonFlag);
    newKey.Append(fallbackFlag);
    newKey.AppendInt(port);
    newKey.AppendLiteral("/[");
    newKey.Append(suffix);
    newKey.AppendLiteral("]viaDNS");
    HashNumber hash = HashString(newKey);
    LOG(
        ("ConnectionEntry::MaybeProcessCoalescingKeys "
         "Established New Coalescing Key # %d for host "
         "%s [%s] hash:%" PRIu32,
         i, mConnInfo->Origin(), newKey.get(), hash));
    mCoalescingKeys.AppendElement(CoalescingKey{hash, newKey});
  }
  return true;
}

nsresult ConnectionEntry::CreateDnsAndConnectSocket(
    nsAHttpTransaction* trans, uint32_t caps, bool speculative,
    bool urgentStart, bool allow1918, PendingTransactionInfo* pendingTransInfo,
    bool retryWithoutTRR) {
  return mConnectionAttemptPool->StartConnectionEstablishment(
      this, trans, caps, speculative, urgentStart, allow1918, pendingTransInfo,
      retryWithoutTRR);
}

bool ConnectionEntry::AllowToRetryDifferentIPFamilyForHttp3(nsresult aError) {
  LOG(
      ("ConnectionEntry::AllowToRetryDifferentIPFamilyForHttp3 %p "
       "error=%" PRIx32,
       this, static_cast<uint32_t>(aError)));
  if (mConnInfo->GetHappyEyeballsEnabled()) {
    return false;
  }
  if (!mConnInfo->IsHttp3() && !mConnInfo->IsHttp3ProxyConnection()) {
    MOZ_ASSERT(false, "Should not be called for non Http/3 connection");
    return false;
  }

  if (!StaticPrefs::network_http_http3_retry_different_ip_family()) {
    return false;
  }

  // Only allow to retry with these two errors.
  if (aError != NS_ERROR_CONNECTION_REFUSED &&
      aError != NS_ERROR_PROXY_CONNECTION_REFUSED) {
    return false;
  }

  // Already retried once.
  if (mRetriedDifferentIPFamilyForHttp3) {
    return false;
  }

  return true;
}

void ConnectionEntry::SetRetryDifferentIPFamilyForHttp3(uint16_t aIPFamily) {
  LOG(("ConnectionEntry::SetRetryDifferentIPFamilyForHttp3 %p, af=%u", this,
       aIPFamily));

  mPreferIPv4 = false;
  mPreferIPv6 = false;

  if (aIPFamily == AF_INET) {
    mPreferIPv6 = true;
  }

  if (aIPFamily == AF_INET6) {
    mPreferIPv4 = true;
  }

  mRetriedDifferentIPFamilyForHttp3 = true;

  LOG(("  %p prefer ipv4=%d, ipv6=%d", this, (bool)mPreferIPv4,
       (bool)mPreferIPv6));
  MOZ_DIAGNOSTIC_ASSERT(mPreferIPv4 ^ mPreferIPv6);
}

void ConnectionEntry::SetServerCertHashes(
    nsTArray<RefPtr<nsIWebTransportHash>>&& aHashes) {
  mServerCertHashes = std::move(aHashes);
}

const nsTArray<RefPtr<nsIWebTransportHash>>&
ConnectionEntry::GetServerCertHashes() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  return mServerCertHashes;
}

const CoalescingKey& ConnectionEntry::OriginFrameHashKey() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (mOriginFrameHashKey.isNothing()) {
    mOriginFrameHashKey.emplace(nsHttpConnectionInfo::BuildOriginFrameHashKey(
        mConnInfo, mConnInfo->GetOrigin(), mConnInfo->OriginPort()));
  }
  return mOriginFrameHashKey.ref();
}

}  // namespace net
}  // namespace mozilla
