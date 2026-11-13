/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_Http3StreamBase_h
#define mozilla_net_Http3StreamBase_h

#include "ARefBase.h"
#include "mozilla/WeakPtr.h"
#include "nsAHttpTransaction.h"
#include "nsIClassOfService.h"

namespace mozilla::net {

class Http3SessionBase;
class Http3Stream;
class Http3WebTransportSession;
class Http3WebTransportStream;
class Http3ConnectUDPStream;
class Http3StreamTunnel;

class Http3StreamBase : public SupportsWeakPtr, public ARefBase {
 public:
  Http3StreamBase(nsAHttpTransaction* trans, Http3SessionBase* session);

  virtual Http3WebTransportSession* GetHttp3WebTransportSession() = 0;
  virtual Http3WebTransportStream* GetHttp3WebTransportStream() = 0;
  virtual Http3Stream* GetHttp3Stream() = 0;
  virtual Http3ConnectUDPStream* GetHttp3ConnectUDPStream() = 0;
  virtual Http3StreamTunnel* GetHttp3StreamTunnel() = 0;

  bool HasStreamId() const { return mStreamId != UINT64_MAX; }
  uint64_t StreamId() const { return mStreamId; }

  [[nodiscard]] virtual nsresult ReadSegments() = 0;
  [[nodiscard]] virtual nsresult WriteSegments() = 0;

  virtual bool Done() const = 0;

  virtual void SetResponseHeaders(nsTArray<uint8_t>& aResponseHeaders, bool fin,
                                  bool interim) = 0;

  void SetQueued(bool aStatus) { mQueued = aStatus; }
  bool Queued() const { return mQueued; }

  virtual void Close(nsresult aResult) = 0;

  nsAHttpTransaction* Transaction() { return mTransaction; }
  // Replace the stream's backing transaction. Used by the HE / 0-RTT
  // flow when a HappyEyeballsTransaction shim is adopted by the real
  // nsHttpTransaction so the session's stream and its hash both point
  // at the real txn (see Http3Session::SwapTransaction).
  void SetTransaction(nsAHttpTransaction* aTrans) { mTransaction = aTrans; }

  // Mirrors nsAHttpTransaction
  virtual bool Do0RTT() { return false; }
  virtual nsresult Finish0RTT(bool aRestart) { return NS_OK; }

  virtual bool RecvdFin() const { return mFin; }
  virtual bool RecvdReset() const { return mResetRecv; }
  virtual void SetRecvdReset() { mResetRecv = true; }

  void SetInTxQueue(bool aValue) { mInTxQueue = aValue; }
  bool IsInTxQueue() const { return mInTxQueue; }

  void SetInRxQueue(bool aValue) { mInRxQueue = aValue; }
  bool IsInRxQueue() const { return mInRxQueue; }

  void SetBlockedByFlowControl(bool aValue) { mBlockedByFlowControl = aValue; }
  bool BlockedByFlowControl() const { return mBlockedByFlowControl; }

  bool Closed() const { return mClosed; }

 protected:
  ~Http3StreamBase();

  uint64_t mStreamId{UINT64_MAX};
  int64_t mSendOrder{0};
  bool mSendOrderIsSet{false};
  RefPtr<nsAHttpTransaction> mTransaction;
  RefPtr<Http3SessionBase> mSession;
  bool mQueued{false};
  bool mFin{false};
  bool mResetRecv{false};
  bool mInTxQueue{false};
  bool mInRxQueue{false};
  bool mBlockedByFlowControl{false};
  bool mClosed{false};
};

}  // namespace mozilla::net

#endif  // mozilla_net_Http3StreamBase_h
