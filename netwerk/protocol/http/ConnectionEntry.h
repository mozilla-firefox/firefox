/* vim:set ts=4 sw=2 sts=2 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ConnectionEntry_h__
#define ConnectionEntry_h__

#include "PendingTransactionInfo.h"
#include "PendingTransactionQueue.h"
#include "DnsAndConnectSocket.h"
#include "DashboardTypes.h"

namespace mozilla {
namespace net {

// ConnectionEntry
//
// nsHttpConnectionMgr::mCT maps connection info hash key to ConnectionEntry
// object, which contains list of active and idle connections as well as the
// list of pending transactions.
class ConnectionEntry {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ConnectionEntry)
  explicit ConnectionEntry(nsHttpConnectionInfo* ci);

  void ReschedTransaction(nsHttpTransaction* aTrans);

  nsTArray<RefPtr<PendingTransactionInfo>>* GetTransactionPendingQHelper(
      nsAHttpTransaction* trans);

  void InsertTransactionSorted(
      nsTArray<RefPtr<PendingTransactionInfo>>& pendingQ,
      PendingTransactionInfo* pendingTransInfo,
      bool aInsertAsFirstForTheSamePriority = false);

  void InsertTransaction(PendingTransactionInfo* aPendingTransInfo,
                         bool aInsertAsFirstForTheSamePriority = false);

  size_t UrgentStartQueueLength();

  void PrintPendingQ();

  void Compact();

  void CancelAllTransactions(nsresult reason);

  nsresult CloseIdleConnection(nsHttpConnection* conn);
  void CloseIdleConnections();
  void CloseIdleConnections(uint32_t maxToClose);
  void CloseExtendedCONNECTConnections();
  void ClosePendingConnections();
  nsresult RemoveIdleConnection(nsHttpConnection* conn);
  bool IsInIdleConnections(HttpConnectionBase* conn);
  size_t IdleConnectionsLength() const { return mIdleConns.Length(); }
  void InsertIntoIdleConnections(nsHttpConnection* conn);
  already_AddRefed<nsHttpConnection> GetIdleConnection(bool respectUrgency,
                                                       bool urgentTrans,
                                                       bool* onlyUrgent);

  size_t ActiveConnsLength() const { return mActiveConns.Length(); }
  void InsertIntoActiveConns(HttpConnectionBase* conn);
  bool IsInActiveConns(HttpConnectionBase* conn);
  nsresult RemoveActiveConnection(HttpConnectionBase* conn);
  nsresult RemovePendingConnection(HttpConnectionBase* conn);
  void MakeAllDontReuseExcept(HttpConnectionBase* conn);
  bool FindConnToClaim(PendingTransactionInfo* pendingTransInfo);
  void CloseActiveConnections();
  void CloseAllActiveConnsWithNullTransactcion(nsresult aCloseCode);

  bool IsInExtendedCONNECTConns(HttpConnectionBase* conn);
  void InsertIntoExtendedCONNECTConns(HttpConnectionBase* conn);
  void RemoveExtendedCONNECTConns(HttpConnectionBase* conn);

  HttpConnectionBase* GetH2orH3ActiveConn();
  // Make an active spdy connection DontReuse.
  // TODO: this is a helper function and should nbe improved.
  bool MakeFirstActiveSpdyConnDontReuse();

  void ClosePersistentConnections();

  uint32_t PruneDeadConnections();
  void MakeConnectionPendingAndDontReuse(HttpConnectionBase* conn);
  void VerifyTraffic();
  void PruneNoTraffic();
  uint32_t TimeoutTick();

  void MoveConnection(HttpConnectionBase* proxyConn, ConnectionEntry* otherEnt);

  size_t DnsAndConnectSocketsLength() const {
    return mDnsAndConnectSockets.Length();
  }

  void InsertIntoDnsAndConnectSockets(DnsAndConnectSocket* sock);
  void RemoveDnsAndConnectSocket(DnsAndConnectSocket* dnsAndSock, bool abandon);
  void CloseAllDnsAndConnectSockets();

  HttpRetParams GetConnectionData();
  Http3ConnectionStatsParams GetHttp3ConnectionStatsData();
  void LogConnections();

  const RefPtr<nsHttpConnectionInfo> mConnInfo;

  bool AvailableForDispatchNow();

  // calculate the number of half open sockets that have not had at least 1
  // connection complete
  uint32_t UnconnectedDnsAndConnectSockets() const;

  // Remove a particular DnsAndConnectSocket from the mDnsAndConnectSocket array
  bool RemoveDnsAndConnectSocket(DnsAndConnectSocket*);

  bool MaybeProcessCoalescingKeys(nsIDNSAddrRecord* dnsRecord,
                                  bool aIsHttp3 = false);

  nsresult CreateDnsAndConnectSocket(nsAHttpTransaction* trans, uint32_t caps,
                                     bool speculative, bool isFromPredictor,
                                     bool urgentStart, bool allow1918,
                                     PendingTransactionInfo* pendingTransInfo);

  // Spdy sometimes resolves the address in the socket manager in order
  // to re-coalesce sharded HTTP hosts. The dotted decimal address is
  // combined with the Anonymous flag and OA from the connection information
  // to build the hash key for hosts in the same ip pool.
  //

  nsTArray<nsCString> mCoalescingKeys;

  // This is a list of addresses matching the coalescing keys.
  // This is necessary to check if the origin's DNS entries
  // contain the IP address of the active connection.
  nsTArray<NetAddr> mAddresses;

  // To have the UsingSpdy flag means some host with the same connection
  // entry has done NPN=spdy/* at some point. It does not mean every
  // connection is currently using spdy.
  bool mUsingSpdy : 1;

  // Determines whether or not we can continue to use spdy-enabled
  // connections in the future. This is generally set to false either when
  // some connection here encounters connection-based auth (such as NTLM)
  // or when some connection here encounters a server that is totally
  // busted (such as it fails to properly perform the h2 handshake).
  bool mCanUseSpdy : 1;

  // Flags to remember our happy-eyeballs decision.
  // Reset only by Ctrl-F5 reload.
  // True when we've first connected an IPv4 server for this host,
  // initially false.
  bool mPreferIPv4 : 1;
  // True when we've first connected an IPv6 server for this host,
  // initially false.
  bool mPreferIPv6 : 1;

  // True if this connection entry has initiated a socket
  bool mUsedForConnection : 1;

  bool mDoNotDestroy : 1;

  bool IsHttp3() const { return mConnInfo->IsHttp3(); }
  bool AllowHttp2() const { return mCanUseSpdy; }
  void DisallowHttp2();
  void DontReuseHttp3Conn();

  // Set the IP family preference flags according the connected family
  void RecordIPFamilyPreference(uint16_t family);
  // Resets all flags to their default values
  void ResetIPFamilyPreference();
  // True iff there is currently an established IP family preference
  bool PreferenceKnown() const;

  // Return the count of pending transactions for all window ids.
  size_t PendingQueueLength() const;
  size_t PendingQueueLengthForWindow(uint64_t windowId) const;

  void AppendPendingUrgentStartQ(
      nsTArray<RefPtr<PendingTransactionInfo>>& result);

  // Append transactions to the |result| whose window id
  // is equal to |windowId|.
  // NOTE: maxCount == 0 will get all transactions in the queue.
  void AppendPendingQForFocusedWindow(
      uint64_t windowId, nsTArray<RefPtr<PendingTransactionInfo>>& result,
      uint32_t maxCount = 0);

  // Append transactions whose window id isn't equal to |windowId|.
  // NOTE: windowId == 0 will get all transactions for both
  // focused and non-focused windows.
  void AppendPendingQForNonFocusedWindows(
      uint64_t windowId, nsTArray<RefPtr<PendingTransactionInfo>>& result,
      uint32_t maxCount = 0);

  // Remove the empty pendingQ in |mPendingTransactionTable|.
  void RemoveEmptyPendingQ();

  void PrintDiagnostics(nsCString& log, uint32_t aMaxPersistConns);

  bool RestrictConnections();

  // Return total active connection count, which is the sum of
  // active connections and unconnected half open connections.
  uint32_t TotalActiveConnections() const;

  bool RemoveTransFromPendingQ(nsHttpTransaction* aTrans);

  void MaybeUpdateEchConfig(nsHttpConnectionInfo* aConnInfo);

  bool AllowToRetryDifferentIPFamilyForHttp3(nsresult aError);
  void SetRetryDifferentIPFamilyForHttp3(uint16_t aIPFamily);

  void SetServerCertHashes(nsTArray<RefPtr<nsIWebTransportHash>>&& aHashes);

  const nsTArray<RefPtr<nsIWebTransportHash>>& GetServerCertHashes();

 private:
  void InsertIntoIdleConnections_internal(nsHttpConnection* conn);
  void RemoveFromIdleConnectionsIndex(size_t inx);
  bool RemoveFromIdleConnections(nsHttpConnection* conn);

  nsTArray<RefPtr<nsHttpConnection>> mIdleConns;  // idle persistent connections
  nsTArray<RefPtr<HttpConnectionBase>> mActiveConns;  // active connections
  // When a connection is added to this mPendingConns list, it is primarily
  // to keep the connection alive and to continue serving its ongoing
  // transaction. While in this list, the connection will not be available to
  // serve any new transactions and will remain here until its current
  // transaction is complete.
  nsTArray<RefPtr<HttpConnectionBase>> mPendingConns;
  // Tunneled connections used for extended CONNECT that needs to be cleaned up
  // on shutdown
  nsTArray<RefPtr<HttpConnectionBase>> mExtendedCONNECTConns;

  nsTArray<RefPtr<DnsAndConnectSocket>>
      mDnsAndConnectSockets;  // dns resolution and half open connections

  // If serverCertificateHashes are used, these are stored here
  nsTArray<RefPtr<nsIWebTransportHash>> mServerCertHashes;

  PendingTransactionQueue mPendingQ;
  ~ConnectionEntry();

  bool mRetriedDifferentIPFamilyForHttp3 = false;
};

}  // namespace net
}  // namespace mozilla

#endif  // !ConnectionEntry_h__
