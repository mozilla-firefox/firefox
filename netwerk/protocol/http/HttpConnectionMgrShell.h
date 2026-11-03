/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HttpConnectionMgrShell_h_
#define HttpConnectionMgrShell_h_

#include "nsISupports.h"

class nsIEventTarget;
class nsIHttpUpgradeListener;
class nsIInterfaceRequestor;

namespace mozilla::net {

class ARefBase;
class EventTokenBucket;
class HttpTransactionShell;
class nsHttpConnectionInfo;
class HttpConnectionBase;
class nsHttpConnectionMgr;
class SpeculativeTransaction;
class ClassOfService;

//----------------------------------------------------------------------------
// Abstract base class for HTTP connection manager in chrome process
//----------------------------------------------------------------------------

// f5379ff9-2758-4bec-9992-2351c258aed6
#define HTTPCONNECTIONMGRSHELL_IID \
  {0xf5379ff9, 0x2758, 0x4bec, {0x99, 0x92, 0x23, 0x51, 0xc2, 0x58, 0xae, 0xd6}}

class HttpConnectionMgrShell : public nsISupports {
 public:
  NS_INLINE_DECL_STATIC_IID(HTTPCONNECTIONMGRSHELL_IID)

  enum nsParamName : uint32_t {
    MAX_URGENT_START_Q,
    MAX_CONNECTIONS,
    MAX_PERSISTENT_CONNECTIONS_PER_HOST,
    MAX_PERSISTENT_CONNECTIONS_PER_PROXY,
    MAX_REQUEST_DELAY,
    THROTTLING_ENABLED,
    THROTTLING_SUSPEND_FOR,
    THROTTLING_RESUME_FOR,
    THROTTLING_READ_LIMIT,
    THROTTLING_READ_INTERVAL,
    THROTTLING_HOLD_TIME,
    THROTTLING_MAX_TIME,
    PROXY_BE_CONSERVATIVE
  };

  [[nodiscard]] virtual nsresult Init(
      uint16_t maxUrgentExcessiveConns, uint16_t maxConnections,
      uint16_t maxPersistentConnectionsPerHost,
      uint16_t maxPersistentConnectionsPerProxy, uint16_t maxRequestDelay,
      bool throttleEnabled, uint32_t throttleSuspendFor,
      uint32_t throttleResumeFor, uint32_t throttleHoldTime,
      uint32_t throttleMaxTime, bool beConservativeForProxy) = 0;

  [[nodiscard]] virtual nsresult Shutdown() = 0;

  // called from main thread to post a new request token bucket
  // to the socket thread
  [[nodiscard]] virtual nsresult UpdateRequestTokenBucket(
      EventTokenBucket* aBucket) = 0;

  // Close all idle persistent connections and prevent any active connections
  // from being reused.
  [[nodiscard]] virtual nsresult DoShiftReloadConnectionCleanup() = 0;

  // Like DoShiftReloadConnectionCleanup() above, but only for connections
  // whose origin attributes match the CI. Also resets CI specific information
  // such as Happy Eyeballs history.
  [[nodiscard]] virtual nsresult DoShiftReloadConnectionCleanupWithConnInfo(
      nsHttpConnectionInfo*) = 0;

  // Close all idle persistent connections and keep active ones.
  [[nodiscard]] virtual nsresult CloseAllIdleConnections() = 0;

  // called to force the connection manager to prune its list of idle
  // connections.
  [[nodiscard]] virtual nsresult PruneDeadConnections() = 0;

  // this cancels all outstanding transactions but does not shut down the mgr
  virtual void AbortAndCloseAllConnections(int32_t, ARefBase*) = 0;

  // called to update a parameter after the connection manager has already
  // been initialized.
  [[nodiscard]] virtual nsresult UpdateParam(nsParamName name,
                                             uint16_t value) = 0;

  // Causes a large amount of connection diagnostic information to be
  // printed to the javascript console
  virtual void PrintDiagnostics() = 0;

  virtual nsresult UpdateCurrentBrowserId(uint64_t aId) = 0;

  // adds a transaction to the list of managed transactions.
  [[nodiscard]] virtual nsresult AddTransaction(HttpTransactionShell*,
                                                int32_t priority) = 0;

  // Add a new transaction with a sticky connection from |transWithStickyConn|.
  [[nodiscard]] virtual nsresult AddTransactionWithStickyConn(
      HttpTransactionShell* trans, int32_t priority,
      HttpTransactionShell* transWithStickyConn) = 0;

  // called to reschedule the given transaction.  it must already have been
  // added to the connection manager via AddTransaction.
  [[nodiscard]] virtual nsresult RescheduleTransaction(HttpTransactionShell*,
                                                       int32_t priority) = 0;

  void virtual UpdateClassOfServiceOnTransaction(
      HttpTransactionShell*, const ClassOfService& classOfService) = 0;

  // cancels a transaction w/ the given reason.
  [[nodiscard]] virtual nsresult CancelTransaction(HttpTransactionShell*,
                                                   nsresult reason) = 0;

  // called when a connection is done processing a transaction.  if the
  // connection can be reused then it will be added to the idle list, else
  // it will be closed.
  [[nodiscard]] virtual nsresult ReclaimConnection(
      HttpConnectionBase* conn) = 0;

  // called to force the transaction queue to be processed once more, giving
  // preference to the specified connection.
  [[nodiscard]] virtual nsresult ProcessPendingQ(nsHttpConnectionInfo*) = 0;

  // Try and process all pending transactions
  [[nodiscard]] virtual nsresult ProcessPendingQ() = 0;

  // called to get a reference to the socket transport service.  the socket
  // transport service is not available when the connection manager is down.
  [[nodiscard]] virtual nsresult GetSocketThreadTarget(nsIEventTarget**) = 0;

  // called to indicate a transaction for the connectionInfo is likely coming
  // soon. The connection manager may use this information to start a TCP
  // and/or SSL level handshake for that resource immediately so that it is
  // ready when the transaction is submitted. No obligation is taken on by the
  // connection manager, nor is the submitter obligated to actually submit a
  // real transaction for this connectionInfo.
  [[nodiscard]] virtual nsresult SpeculativeConnect(
      nsHttpConnectionInfo*, nsIInterfaceRequestor*, uint32_t caps = 0,
      SpeculativeTransaction* = nullptr, bool aFetchHTTPSRR = false) = 0;

  // "VerifyTraffic" means marking connections now, and then check again in
  // N seconds to see if there's been any traffic and if not, kill
  // that connection.
  [[nodiscard]] virtual nsresult VerifyTraffic() = 0;

  virtual void ExcludeHttp2(const nsHttpConnectionInfo* ci) = 0;
  virtual void ExcludeHttp3(const nsHttpConnectionInfo* ci) = 0;

  // clears the connection history mCT
  [[nodiscard]] virtual nsresult ClearConnectionHistory() = 0;

  // called by the main thread to execute the taketransport() logic on the
  // socket thread after a 101 response has been received and the socket
  // needs to be transferred to an expectant upgrade listener such as
  // websockets.
  // @param aTrans: a transaction that contains a sticky connection. We'll
  //                take the transport of this connection.
  [[nodiscard]] virtual nsresult CompleteUpgrade(
      HttpTransactionShell* aTrans,
      nsIHttpUpgradeListener* aUpgradeListener) = 0;

  virtual nsHttpConnectionMgr* AsHttpConnectionMgr() = 0;
};

#define NS_DECL_HTTPCONNECTIONMGRSHELL                                         \
  virtual nsresult Init(                                                       \
      uint16_t maxUrgentExcessiveConns, uint16_t maxConnections,               \
      uint16_t maxPersistentConnectionsPerHost,                                \
      uint16_t maxPersistentConnectionsPerProxy, uint16_t maxRequestDelay,     \
      bool throttleEnabled, uint32_t throttleSuspendFor,                       \
      uint32_t throttleResumeFor, uint32_t throttleHoldTime,                   \
      uint32_t throttleMaxTime, bool beConservativeForProxy) override;         \
  virtual nsresult Shutdown() override;                                        \
  virtual nsresult UpdateRequestTokenBucket(EventTokenBucket* aBucket)         \
      override;                                                                \
  virtual nsresult DoShiftReloadConnectionCleanup() override;                  \
  virtual nsresult DoShiftReloadConnectionCleanupWithConnInfo(                 \
      nsHttpConnectionInfo*) override;                                         \
  virtual nsresult CloseAllIdleConnections() override;                         \
  virtual nsresult PruneDeadConnections() override;                            \
  virtual void AbortAndCloseAllConnections(int32_t, ARefBase*) override;       \
  virtual nsresult UpdateParam(nsParamName name, uint16_t value) override;     \
  virtual void PrintDiagnostics() override;                                    \
  virtual nsresult UpdateCurrentBrowserId(uint64_t aId) override;              \
  virtual nsresult AddTransaction(HttpTransactionShell*, int32_t priority)     \
      override;                                                                \
  virtual nsresult AddTransactionWithStickyConn(                               \
      HttpTransactionShell* trans, int32_t priority,                           \
      HttpTransactionShell* transWithStickyConn) override;                     \
  virtual nsresult RescheduleTransaction(HttpTransactionShell*,                \
                                         int32_t priority) override;           \
  void virtual UpdateClassOfServiceOnTransaction(                              \
      HttpTransactionShell*, const ClassOfService& classOfService) override;   \
  virtual nsresult CancelTransaction(HttpTransactionShell*, nsresult reason)   \
      override;                                                                \
  virtual nsresult ReclaimConnection(HttpConnectionBase* conn) override;       \
  virtual nsresult ProcessPendingQ(nsHttpConnectionInfo*) override;            \
  virtual nsresult ProcessPendingQ() override;                                 \
  virtual nsresult GetSocketThreadTarget(nsIEventTarget**) override;           \
  virtual nsresult SpeculativeConnect(                                         \
      nsHttpConnectionInfo*, nsIInterfaceRequestor*, uint32_t caps = 0,        \
      SpeculativeTransaction* = nullptr, bool aFetchHTTPSRR = false) override; \
  virtual nsresult VerifyTraffic() override;                                   \
  virtual void ExcludeHttp2(const nsHttpConnectionInfo* ci) override;          \
  virtual void ExcludeHttp3(const nsHttpConnectionInfo* ci) override;          \
  virtual nsresult ClearConnectionHistory() override;                          \
  virtual nsresult CompleteUpgrade(HttpTransactionShell* aTrans,               \
                                   nsIHttpUpgradeListener* aUpgradeListener)   \
      override;                                                                \
  nsHttpConnectionMgr* AsHttpConnectionMgr() override;

}  // namespace mozilla::net

#endif  // HttpConnectionMgrShell_h_
