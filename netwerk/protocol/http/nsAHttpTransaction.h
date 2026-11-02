/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsAHttpTransaction_h_
#define nsAHttpTransaction_h_

#include "nsILoadInfo.h"
#include "nsIRequest.h"
#include "nsITRRSkipReason.h"
#include "nsTArray.h"
#include "nsWeakReference.h"

#ifdef Status
/* Xlib headers insist on this for some reason... Nuke it because
   it'll override our member name */
typedef Status __StatusTmp;
#  undef Status
typedef __StatusTmp Status;
#endif

class nsIDNSHTTPSSVCRecord;
class nsIInterfaceRequestor;
class nsIRequestContext;
class nsISVCBRecord;
class nsITLSSocketControl;
class nsITransport;

namespace mozilla {
namespace net {

class nsAHttpConnection;
class nsAHttpSegmentReader;
class nsAHttpSegmentWriter;
class nsHttpTransaction;
class nsHttpRequestHead;
class nsHttpResponseHead;
class ProxyConnectResponseHead;
class nsHttpConnectionInfo;
class NullHttpTransaction;

enum class ExtendedCONNECTSupport { UNSURE, NO_SUPPORT, SUPPORTED };

//----------------------------------------------------------------------------
// Abstract base class for a HTTP transaction:
//
// A transaction is a "sink" for the response data.  The connection pushes
// data to the transaction by writing to it.  The transaction supports
// WriteSegments and may refuse to accept data if its buffers are full (its
// write function returns NS_BASE_STREAM_WOULD_BLOCK in this case).
//----------------------------------------------------------------------------

// 2af6d634-13e3-494c-8903-c9dce5c22fc0
#define NS_AHTTPTRANSACTION_IID \
  {0x2af6d634, 0x13e3, 0x494c, {0x89, 0x03, 0xc9, 0xdc, 0xe5, 0xc2, 0x2f, 0xc0}}

class nsAHttpTransaction : public nsSupportsWeakReference {
 public:
  NS_INLINE_DECL_STATIC_IID(NS_AHTTPTRANSACTION_IID)

  // called by the connection when it takes ownership of the transaction.
  virtual void SetConnection(nsAHttpConnection*) = 0;

  // called by the connection after a successfull activation of this transaction
  // in other words, tells the transaction it transitioned to the "active"
  // state.
  virtual void OnActivated() {}

  // used to obtain the connection associated with this transaction
  virtual nsAHttpConnection* Connection() = 0;

  // called by the connection to get security callbacks to set on the
  // socket transport.
  virtual void GetSecurityCallbacks(nsIInterfaceRequestor**) = 0;

  // called to report socket status (see nsITransportEventSink)
  virtual void OnTransportStatus(nsITransport* transport, nsresult status,
                                 int64_t progress) = 0;

  // called to check the transaction status.
  virtual bool IsDone() = 0;
  virtual nsresult Status() = 0;
  virtual uint32_t Caps() = 0;

  // called to read request data from the transaction.
  [[nodiscard]] virtual nsresult ReadSegments(nsAHttpSegmentReader* reader,
                                              uint32_t count,
                                              uint32_t* countRead) = 0;

  // called to write response data to the transaction.
  [[nodiscard]] virtual nsresult WriteSegments(nsAHttpSegmentWriter* writer,
                                               uint32_t count,
                                               uint32_t* countWritten) = 0;

  // These versions of the functions allow the overloader to specify whether or
  // not it is safe to call *Segments() in a loop while they return OK.
  // The callee should turn again to false if it is not, otherwise leave
  // untouched
  [[nodiscard]] virtual nsresult ReadSegmentsAgain(nsAHttpSegmentReader* reader,
                                                   uint32_t count,
                                                   uint32_t* countRead,
                                                   bool* again) {
    return ReadSegments(reader, count, countRead);
  }
  [[nodiscard]] virtual nsresult WriteSegmentsAgain(
      nsAHttpSegmentWriter* writer, uint32_t count, uint32_t* countWritten,
      bool* again) {
    return WriteSegments(writer, count, countWritten);
  }

  // called to close the transaction
  virtual void Close(nsresult reason) = 0;

  // called to cancel the transaction; default implementation is a no-op.
  virtual void Cancel(nsresult aReason) {}

  // called to indicate a failure with proxy CONNECT
  virtual void SetProxyConnectFailed() = 0;

  // called to retrieve the request headers of the transaction. Returns a
  // const pointer so that callers cannot mutate it. nsHttpTransaction does not
  // snapshot the request head: this points at the request head of the
  // channel, which nsHttpRequestHead protects with its own lock.
  virtual const nsHttpRequestHead* RequestHead() = 0;

  // determine the number of real http/1.x transactions on this
  // abstract object. Pipelines had multiple, SPDY has 0,
  // normal http transactions have 1.
  virtual uint32_t Http1xTransactionCount() = 0;

  // called to remove the unused sub transactions from an object that can
  // handle multiple transactions simultaneously (i.e. h2).
  //
  // Returns NS_ERROR_NOT_IMPLEMENTED if the object does not implement
  // sub-transactions.
  //
  // Returns NS_ERROR_ALREADY_OPENED if the subtransactions have been
  // at least partially written and cannot be moved.
  //
  [[nodiscard]] virtual nsresult TakeSubTransactions(
      nsTArray<RefPtr<nsAHttpTransaction> >& outTransactions) = 0;

  // Occasionally the abstract interface has to give way to base implementations
  // to respect differences between spdy, h2, etc..
  // These Query* (and IsNullTransaction()) functions provide a way to do
  // that without using xpcom or rtti. Any calling code that can't deal with
  // a null response from one of them probably shouldn't be using
  // nsAHttpTransaction

  // equivalent to !!dynamic_cast<NullHttpTransaction *>(this)
  // A null transaction is expected to return BASE_STREAM_CLOSED on all of
  // its IO functions all the time.
  virtual bool IsNullTransaction() { return false; }
  virtual NullHttpTransaction* QueryNullTransaction() { return nullptr; }

  // If we used rtti this would be the result of doing
  // dynamic_cast<nsHttpTransaction *>(this).. i.e. it can be nullptr for
  // non nsHttpTransaction implementations of nsAHttpTransaction
  virtual nsHttpTransaction* QueryHttpTransaction() { return nullptr; }

  // return the request context associated with the transaction
  virtual nsIRequestContext* RequestContext() { return nullptr; }

  // return the connection information associated with the transaction
  virtual nsHttpConnectionInfo* ConnectionInfo() = 0;

  // The base definition of these is done in nsHttpTransaction.cpp
  virtual bool ResponseTimeoutEnabled() const;
  virtual PRIntervalTime ResponseTimeout();

  // conceptually the socket control is part of the connection, but sometimes
  // in the case of TLS tunneled within TLS the transaction might present
  // a more specific socket control that cannot be represented as a layer in
  // the connection due to multiplexing. This interface represents such an
  // overload. If it returns NS_FAILURE the connection should be considered
  // authoritative.
  [[nodiscard]] virtual nsresult GetTransactionTLSSocketControl(
      nsITLSSocketControl**) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  virtual void DisableSpdy() {}
  // When called, we disallow to connect through a Http/2 proxy.
  virtual void DisableHttp2ForProxy() {}
  virtual void DisableHttp3(bool aAllowRetryHTTPSRR) {}
  virtual void MakeNonSticky() {}
  virtual void MakeRestartable() {}
  virtual void ReuseConnectionOnRestartOK(bool) {}
  virtual void SetIsHttp2Websocket(bool) {}
  virtual bool IsHttp2Websocket() { return false; }
  virtual void SetTRRInfo(nsIRequest::TRRMode aMode,
                          TRRSkippedReason aSkipReason) {};
  virtual bool AllowedToConnectToIpAddressSpace(
      nsILoadInfo::IPAddressSpace aTargetIpAddressSpace) {
    return true;
  };

  // We call this function if we want to use alt-svc host again on the next
  // restart. If this function is not called on the next restart the
  // transaction will use the original route.
  // For example in case we receive a GOAWAY frame from a server, we can
  // restart and use the same alt-svc. If we get VersionFallback we do not
  // want to use the alt-svc on the restart.
  virtual void DoNotRemoveAltSvc() {}

  // We call this function if we do want to reset IP family preference again on
  // the next restart.
  virtual void DoNotResetIPFamilyPreference() {}

  // Signals that a PSK resumption token was offered in the ClientHello.
  // aCanSendEarlyData is true when the server advertised max_early_data_size
  // and the transaction may also send early data; false when a PSK was offered
  // but no early data was sent (server didn't advertise max_early_data_size).
  // Returns true only when early data was actually started.
  [[nodiscard]] virtual bool Do0RTT(bool aCanSendEarlyData = true) {
    return false;
  }

  // Called when the TLS handshake succeeds and the PSK was accepted by the
  // server. Clears any pending PSK-retry state so that post-handshake errors
  // (e.g. ALPN mismatch) do not trigger a spurious PSK retry.
  virtual void OnPSKResumptionAccepted() {}
  // This function will be called when a tls handshake has been finished and
  // we know whether early-data that was sent has been accepted or not, e.g.
  // do we need to restart a transaction. This will be called only if Do0RTT
  // returns true.
  // If aRestart parameter is true we need to restart the transaction,
  // otherwise the erly-data has been accepted and we can continue the
  // transaction.
  // If aAlpnChanged is true (and we were assuming http/2), we'll need to take
  // the transactions out of the session, rewind them all, and start them back
  // over as http/1 transactions
  // The function will return success or failure of the transaction restart.
  [[nodiscard]] virtual nsresult Finish0RTT(bool aRestart, bool aAlpnChanged) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  virtual uint64_t BrowserId() {
    MOZ_ASSERT(false);
    return 0;
  }

  virtual void OnProxyConnectComplete(ProxyConnectResponseHead* aResponseHead) {
  }

  // TLS handshake saw the server's CertificateRequest / the user's selection.
  // No-op by default; HE overrides to pause around the cert dialog.
  virtual void OnClientAuthCertificateRequested() {}
  virtual void OnClientAuthCertificateSelected() {}

  virtual nsresult FetchHTTPSRR() { return NS_ERROR_NOT_IMPLEMENTED; }
  virtual nsresult OnHTTPSRRAvailable(nsIDNSHTTPSSVCRecord* aHTTPSSVCRecord,
                                      nsISVCBRecord* aHighestPriorityRecord,
                                      const nsACString& aCname) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  virtual bool IsForWebTransport() { return false; }
  virtual bool IsResettingForTunnelConn() { return false; }
  virtual void SetResettingForTunnelConn(bool aValue) {}

  virtual void InvokeCallback() {}
  virtual bool IsForFallback() { return false; }
};

#define NS_DECL_NSAHTTPTRANSACTION                                             \
  void SetConnection(nsAHttpConnection*) override;                             \
  nsAHttpConnection* Connection() override;                                    \
  void GetSecurityCallbacks(nsIInterfaceRequestor**) override;                 \
  void OnTransportStatus(nsITransport* transport, nsresult status,             \
                         int64_t progress) override;                           \
  bool IsDone() override;                                                      \
  nsresult Status() override;                                                  \
  uint32_t Caps() override;                                                    \
  [[nodiscard]] virtual nsresult ReadSegments(nsAHttpSegmentReader*, uint32_t, \
                                              uint32_t*) override;             \
  [[nodiscard]] virtual nsresult WriteSegments(nsAHttpSegmentWriter*,          \
                                               uint32_t, uint32_t*) override;  \
  virtual void Close(nsresult reason) override;                                \
  nsHttpConnectionInfo* ConnectionInfo() override;                             \
  void SetProxyConnectFailed() override;                                       \
  virtual const nsHttpRequestHead* RequestHead() override;                     \
  uint32_t Http1xTransactionCount() override;                                  \
  [[nodiscard]] nsresult TakeSubTransactions(                                  \
      nsTArray<RefPtr<nsAHttpTransaction> >& outTransactions) override;

//-----------------------------------------------------------------------------
// nsAHttpSegmentReader
//-----------------------------------------------------------------------------

class nsAHttpSegmentReader {
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

 public:
  // any returned failure code stops segment iteration
  [[nodiscard]] virtual nsresult OnReadSegment(const char* segment,
                                               uint32_t count,
                                               uint32_t* countRead) = 0;

  // Ask the segment reader to commit to accepting size bytes of
  // data from subsequent OnReadSegment() calls or throw hard
  // (i.e. not wouldblock) exceptions. Implementations
  // can return NS_ERROR_FAILURE if they never make commitments of that size
  // (the default), NS_OK if they make the commitment, or
  // NS_BASE_STREAM_WOULD_BLOCK if they cannot make the
  // commitment now but might in the future and forceCommitment is not true .
  // (forceCommitment requires a hard failure or OK at this moment.)
  //
  // SpdySession uses this to make sure frames are atomic.
  [[nodiscard]] virtual nsresult CommitToSegmentSize(uint32_t size,
                                                     bool forceCommitment) {
    return NS_ERROR_FAILURE;
  }
};

#define NS_DECL_NSAHTTPSEGMENTREADER                                     \
  [[nodiscard]] nsresult OnReadSegment(const char*, uint32_t, uint32_t*) \
      override;

//-----------------------------------------------------------------------------
// nsAHttpSegmentWriter
//-----------------------------------------------------------------------------

class nsAHttpSegmentWriter {
 public:
  // any returned failure code stops segment iteration
  [[nodiscard]] virtual nsresult OnWriteSegment(char* segment, uint32_t count,
                                                uint32_t* countWritten) = 0;
};

#define NS_DECL_NSAHTTPSEGMENTWRITER \
  [[nodiscard]] nsresult OnWriteSegment(char*, uint32_t, uint32_t*) override;

}  // namespace net
}  // namespace mozilla

#endif  // nsAHttpTransaction_h_
