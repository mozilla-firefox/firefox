/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HappyEyeballsConnMgrDelegate_h_
#define HappyEyeballsConnMgrDelegate_h_

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/net/DNS.h"
#include "nsISupportsImpl.h"
#include "nsTArray.h"

namespace mozilla {
namespace net {

class ConnectionAttempt;
class ConnectionEntry;
class HttpConnectionBase;
class PendingTransactionInfo;
class nsAHttpTransaction;
class nsHttpTransaction;

// Seam over gHttpHandler->ConnMgr() and the ConnectionEntry so HE's dispatch
// decisions can be intercepted in gtests. Default forwards verbatim; the test
// fake records calls. Connections are HttpConnectionBase* (so a fake works);
// the default impl narrows where the real API needs it (ReportSpdyConnection).
class HappyEyeballsConnMgrDelegate {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(HappyEyeballsConnMgrDelegate)

  // ConnMgr
  virtual already_AddRefed<PendingTransactionInfo> FindTransaction(
      bool aRemoveWhenFound, ConnectionEntry* aEntry,
      nsAHttpTransaction* aTrans) = 0;
  virtual nsresult DispatchTransaction(ConnectionEntry* aEntry,
                                       nsHttpTransaction* aTrans,
                                       HttpConnectionBase* aConn) = 0;
  virtual void AddTransaction(nsHttpTransaction* aTrans, int32_t aPriority) = 0;
  virtual void ReportSpdyConnection(HttpConnectionBase* aConn, bool aUsingSpdy,
                                    bool aDisallowHttp3) = 0;
  virtual void ReportHttp3Connection(HttpConnectionBase* aConn,
                                     ConnectionEntry* aEntry) = 0;
  virtual void ReclaimConnection(HttpConnectionBase* aConn) = 0;
  virtual void ProcessSpdyPendingQ(ConnectionEntry* aEntry) = 0;

  // Move a finished h3-only attempt's connection into the origin's entry and
  // re-key it; see HappyEyeballsConnectionAttempt::ProcessUDPConn. Returns the
  // entry the connection now lives in, or nullptr if it did not move.
  virtual already_AddRefed<ConnectionEntry> HandOffHttp3OnlyConnection(
      HttpConnectionBase* aConn, ConnectionEntry* aFromEnt) = 0;

  // ConnectionEntry
  virtual void InsertIntoActiveConns(ConnectionEntry* aEntry,
                                     HttpConnectionBase* aConn) = 0;
  virtual void RemoveConnectionAttempt(ConnectionEntry* aEntry,
                                       ConnectionAttempt* aAttempt,
                                       bool aAbandon) = 0;
  virtual void RecordIPFamilyPreference(ConnectionEntry* aEntry,
                                        uint16_t aFamily) = 0;
  virtual void ResetIPFamilyPreference(ConnectionEntry* aEntry) = 0;
  virtual bool MaybeProcessCoalescingKeys(ConnectionEntry* aEntry,
                                          const nsTArray<NetAddr>& aAddresses,
                                          bool aIsHttp3) = 0;
  virtual bool RemoveTransFromPendingQ(ConnectionEntry* aEntry,
                                       nsHttpTransaction* aTrans) = 0;
  // Start a fresh Happy Eyeballs attempt for aTrans. aRetryWithoutTRR resolves
  // with TRR disabled (the TRR-resolved -> native-DNS fallback); otherwise it's
  // a plain retry (e.g. a claimed attempt that had refused a local peer).
  virtual nsresult StartRetry(ConnectionEntry* aEntry,
                              nsHttpTransaction* aTrans, uint32_t aCaps,
                              bool aSpeculative, bool aUrgentStart,
                              bool aAllow1918, bool aRetryWithoutTRR) = 0;

 protected:
  virtual ~HappyEyeballsConnMgrDelegate() = default;
};

}  // namespace net
}  // namespace mozilla

#endif  // HappyEyeballsConnMgrDelegate_h_
