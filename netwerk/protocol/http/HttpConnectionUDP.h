/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HttpConnectionUDP_h__
#define HttpConnectionUDP_h__

#include "HttpConnectionBase.h"
#include "nsHttpConnectionInfo.h"
#include "nsHttpResponseHead.h"
#include "nsAHttpTransaction.h"
#include "nsCOMPtr.h"
#include "nsProxyRelease.h"
#include "prinrval.h"
#include "mozilla/Mutex.h"
#include "ARefBase.h"
#include "TimingStruct.h"
#include "HttpTrafficAnalyzer.h"

#include "nsIAsyncInputStream.h"
#include "nsIAsyncOutputStream.h"
#include "nsISupportsPriority.h"
#include "nsIInterfaceRequestor.h"
#include "nsITimer.h"
#include "Http3Session.h"

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
  NS_DECLARE_STATIC_IID_ACCESSOR(HTTPCONNECTIONUDP_IID)
  NS_DECL_HTTPCONNECTIONBASE
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIUDPSOCKETSYNCLISTENER
  NS_DECL_NSIINTERFACEREQUESTOR

  HttpConnectionUDP();

  [[nodiscard]] nsresult Init(nsHttpConnectionInfo* info,
                              nsIDNSRecord* dnsRecord, nsresult status,
                              nsIInterfaceRequestor* callbacks, uint32_t caps);

  friend class HttpConnectionUDPForceIO;

  [[nodiscard]] static nsresult ReadFromStream(nsIInputStream*, void*,
                                               const char*, uint32_t, uint32_t,
                                               uint32_t*);

  bool UsingHttp3() override { return true; }

  void OnQuicTimeoutExpired();

  int64_t BytesWritten() override;

  nsresult GetSelfAddr(NetAddr* addr) override;
  nsresult GetPeerAddr(NetAddr* addr) override;
  bool ResolvedByTRR() override;
  nsIRequest::TRRMode EffectiveTRRMode() override;
  TRRSkippedReason TRRSkipReason() override;
  bool GetEchConfigUsed() override { return false; }

  void NotifyDataRead();
  void NotifyDataWrite();

  Http3Stats GetStats();

 private:
  [[nodiscard]] nsresult OnTransactionDone(nsresult reason);
  nsresult RecvData();
  nsresult SendData();

 private:
  RefPtr<nsHttpHandler> mHttpHandler;  // keep gHttpHandler alive

  RefPtr<nsIAsyncInputStream> mInputOverflow;

  bool mConnectedTransport = false;
  bool mDontReuse = false;
  bool mIsReused = false;
  bool mLastTransactionExpectedNoContent = false;

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
};

NS_DEFINE_STATIC_IID_ACCESSOR(HttpConnectionUDP, HTTPCONNECTIONUDP_IID)

}  // namespace net
}  // namespace mozilla

#endif  // HttpConnectionUDP_h__
