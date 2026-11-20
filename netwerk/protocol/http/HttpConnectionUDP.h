/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HttpConnectionUDP_h_
#define HttpConnectionUDP_h_

#include "ARefBase.h"
#include "Http3Session.h"
#include "HttpConnectionBase.h"
#include "HttpTrafficAnalyzer.h"
#include "TimingStruct.h"
#include "mozilla/Mutex.h"
#include "nsAHttpTransaction.h"
#include "nsCOMPtr.h"
#include "nsHttpConnectionInfo.h"
#include "nsHttpResponseHead.h"
#include "nsIAsyncInputStream.h"
#include "nsIAsyncOutputStream.h"
#include "nsIInterfaceRequestor.h"
#include "nsISupportsPriority.h"
#include "nsITimer.h"
#include "nsProxyRelease.h"
#include "prinrval.h"

class nsIDNSRecord;
class nsISocketTransport;
class nsITLSSocketControl;

namespace mozilla {
namespace net {

class nsHttpHandler;
class ASpdySession;

// 1dcc863e-db90-4652-a1fe-13fea0b54e46
#define HTTPCONNECTIONUDP_IID \
  {0xb97d2036, 0xb441, 0x48be, {0xb3, 0x1e, 0x25, 0x3e, 0xe8, 0x32, 0xdd, 0x67}}

//-----------------------------------------------------------------------------
// HttpConnectionUDP - represents a connection to a HTTP3 server
//
// NOTE: this objects lives on the socket thread only.  it should not be
// accessed from any other thread.
//-----------------------------------------------------------------------------

class HttpConnectionUDP final : public HttpConnectionBase,
                                public nsIUDPSocketSyncListener,
                                public nsIInterfaceRequestor {
 private:
  virtual ~HttpConnectionUDP();

 public:
  NS_INLINE_DECL_STATIC_IID(HTTPCONNECTIONUDP_IID)
  NS_DECL_HTTPCONNECTIONBASE
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIUDPSOCKETSYNCLISTENER
  NS_DECL_NSIINTERFACEREQUESTOR

  HttpConnectionUDP();

  [[nodiscard]] nsresult Init(nsHttpConnectionInfo* info,
                              nsIDNSRecord* dnsRecord, nsresult status,
                              nsIInterfaceRequestor* callbacks, uint32_t caps);
  [[nodiscard]] nsresult InitWithSocket(nsHttpConnectionInfo* info,
                                        nsIUDPSocket* aSocket,
                                        NetAddr aPeerAddr,
                                        nsIInterfaceRequestor* callbacks,
                                        uint32_t caps);

  friend class HttpConnectionUDPForceIO;

  [[nodiscard]] static nsresult ReadFromStream(nsIInputStream*, void*,
                                               const char*, uint32_t, uint32_t,
                                               uint32_t*);

  bool UsingHttp3() override { return true; }

  void OnQuicTimeoutExpired();

  // Returns the seconds until the next tick is needed.
  uint32_t ReadTimeoutTick();

  int64_t BytesWritten() override;

  nsresult GetSelfAddr(NetAddr* addr) override;
  nsresult GetPeerAddr(NetAddr* addr) override;
  bool ResolvedByTRR() override;
  nsIRequest::TRRMode EffectiveTRRMode() override;
  TRRSkippedReason TRRSkipReason() override;
  bool GetEchConfigUsed() override { return false; }

  void NotifyDataRead();
  void NotifyDataWrite();

  // ForceSend() without its kForceDelay backup timer, for data that should
  // go out on the next event loop turn, e.g. datagrams.
  [[nodiscard]] nsresult ForceSendNow();

  Http3Stats GetStats();

  void ResetTransaction(nsHttpTransaction* aHttpTransaction);

  void HandleTunnelResponse(nsHttpTransaction* aHttpTransaction,
                            const nsHttpResponseHead& responseHead,
                            bool* reset);

  nsresult CreateTunnelStream(nsAHttpTransaction* httpTransaction,
                              HttpConnectionBase** aHttpConnection,
                              bool aIsExtendedCONNECT = false) override;

  void OnConnected();

  void RekeyAfterHttp3OnlyHandOff(nsHttpConnectionInfo* aConnInfo);

  void SetDontExclude() override;

  // True for a connection that finished connecting (mExperienced) and can no
  // longer serve new transactions (!CanReuse()). Unlike a bare !CanReuse(),
  // this stays false while the connection is still handshaking, so it is a safe
  // signal for "was usable, now unusable".
  bool IsConnectedAndUnusable();

 private:
  nsresult InitCommon(nsIUDPSocket* aSocket, const NetAddr& aPeerAddr,
                      nsIInterfaceRequestor* callbacks, uint32_t caps,
                      bool isInTunnel);
  [[nodiscard]] nsresult OnTransactionDone(nsresult reason);
  nsresult RecvData();
  nsresult SendData();
  already_AddRefed<nsIInputStream> CreateProxyConnectStream(
      nsAHttpTransaction* trans);

 private:
  RefPtr<nsHttpHandler> mHttpHandler;  // keep gHttpHandler alive

  RefPtr<nsIAsyncInputStream> mInputOverflow;

  bool mConnectedTransport = false;
  bool mDontReuse = false;
  bool mIsReused = false;
  bool mLastTransactionExpectedNoContent = false;
  bool mConnected = false;

  int32_t mPriority = nsISupportsPriority::PRIORITY_NORMAL;

 private:
  // For ForceSend()
  static void ForceSendIO(nsITimer* aTimer, void* aClosure);
  [[nodiscard]] nsresult MaybeForceSendIO();
  bool mForceSendPending = false;
  nsCOMPtr<nsITimer> mForceSendTimer;

  PRIntervalTime mLastRequestBytesSentTime = 0;
  nsCOMPtr<nsIUDPSocket> mSocket;

  nsCOMPtr<nsINetAddr> mSelfAddr;
  nsCOMPtr<nsINetAddr> mPeerAddr;
  bool mResolvedByTRR = false;
  nsIRequest::TRRMode mEffectiveTRRMode = nsIRequest::TRR_DEFAULT_MODE;
  TRRSkippedReason mTRRSkipReason = nsITRRSkipReason::TRR_UNSET;

 private:
  // Http3
  RefPtr<Http3Session> mHttp3Session;
  nsCString mAlpnToken;
  bool mIsInTunnel = false;
  bool mProxyConnectSucceeded = false;
  nsTArray<RefPtr<nsHttpTransaction>> mQueuedHttpConnectTransaction;
  nsTArray<RefPtr<nsHttpTransaction>> mQueuedConnectUdpTransaction;
  bool mAlreadyWildcard = false;

  // Transactions whose LNA check has been deferred until after the QUIC
  // handshake completes; drained in OnConnected().
  nsTArray<RefPtr<nsHttpTransaction>> mDeferredLnaTransactions;
};

}  // namespace net
}  // namespace mozilla

#endif  // HttpConnectionUDP_h_
