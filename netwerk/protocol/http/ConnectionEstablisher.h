/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ConnectionEstablisher_h_
#define ConnectionEstablisher_h_

#include <functional>

#include "ConnectionHandle.h"
#include "HappyEyeballsTransaction.h"
#include "mozilla/Mutex.h"
#include "mozilla/Result.h"
#include "mozilla/net/DNS.h"
#include "nsAHttpConnection.h"
#include "nsHttpConnection.h"
#include "nsIAsyncOutputStream.h"

class nsIDNSAddrRecord;
class nsISocketTransport;

namespace mozilla {
namespace net {

struct DnsMetadata {
  bool mIsTRR = false;
  double mTrrFetchDuration = 0.0;
  double mTrrFetchDurationNetworkOnly = 0.0;
  nsIRequest::TRRMode mEffectiveTRRMode = nsIRequest::TRR_DEFAULT_MODE;
  nsITRRSkipReason::value mTrrSkipReason = nsITRRSkipReason::TRR_UNSET;

  void Fill(nsIDNSAddrRecord* aRecord);
};

class ConnectionEstablisher : public nsITransportEventSink,
                              public nsIInterfaceRequestor {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  NS_DECL_NSITRANSPORTEVENTSINK
  NS_DECL_NSIINTERFACEREQUESTOR

  using DoneCallback =
      std::function<void(Result<RefPtr<HttpConnectionBase>, nsresult>)>;
  using TransportStatusCallback =
      std::function<void(nsITransport*, nsresult, int64_t)>;
  using LnaCheckCallback = std::function<nsresult(nsISocketTransport*)>;

  ConnectionEstablisher(nsHttpConnectionInfo* aConnInfo, const NetAddr& aAddr,
                        uint32_t aCaps, bool aAllow1918);

  virtual bool Start(DoneCallback&& aCallback) = 0;
  void SetSecurityCallbacks(nsIInterfaceRequestor* aCallbacks) {
    MutexAutoLock lock(mSecurityCallbacksLock);
    mSecurityCallbacks = aCallbacks;
  }
  void SetTransportStatusCallback(TransportStatusCallback&& aCallback) {
    mTransportStatusCallback = std::move(aCallback);
  }
  void SetLnaCheckCallback(LnaCheckCallback&& aCallback) {
    mLnaCheckCallback = std::move(aCallback);
  }
  void SetDnsMetadata(const DnsMetadata& aMetadata) {
    mDnsMetadata = aMetadata;
  }

  virtual void Close(nsresult aReason) = 0;
  virtual void ResetSpeculativeFlags() = 0;
  void SetTransaction(HappyEyeballsTransaction* aTrans) {
    mTransaction = aTrans;
  }
  HappyEyeballsTransaction* Transaction() const { return mTransaction; }
  const NetAddr& Addr() const { return mAddr; }
  already_AddRefed<nsIDNSAddrRecord> AddrRecord() const;
  void ClearResultConnection();
  HttpConnectionBase* ResultConn() const { return mResultConn; }
  virtual bool IsUDP() const { return false; }
  bool HasConnected() const { return mHasConnected; }
  bool RefusedForLocalAddress() const { return mRefusedForLocalAddress; }

 protected:
  virtual ~ConnectionEstablisher();
  // Refuse a local (RFC1918) peer when !mAllow1918: sets
  // mRefusedForLocalAddress and returns true so Start() bails without a socket.
  bool RefuseIfLocalAddress();

  // Common implementation for activating a connection with a transaction
  nsresult ActivateConnectionWithTransaction(
      RefPtr<HttpConnectionBase> aConn,
      std::function<void(nsresult)> aOnActivated);

  // Common implementation for finishing the establisher
  void FinishInternal(nsresult aResult);

  virtual void Finish(nsresult aResult) = 0;
  void SetConnecting();
  void MaybeSetConnectingDone();

  RefPtr<nsHttpConnectionInfo> mConnInfo;
  NetAddr mAddr;
  nsCOMPtr<nsIDNSAddrRecord> mAddrRecord;
  uint32_t mCaps = 0;
  // When false, a local (RFC1918) peer is refused; Claim() flips it true once a
  // real transaction takes over.
  bool mAllow1918 = false;
  bool mFinished = false;
  bool mWaitingForConnect = false;
  bool mHasConnected = false;
  bool mConnectedOK = false;
  bool mRefusedForLocalAddress = false;

  TimeStamp mConnectStart;
  TimeStamp mTcpConnectEnd;

  DoneCallback mCallback;
  TransportStatusCallback mTransportStatusCallback;
  LnaCheckCallback mLnaCheckCallback;
  Mutex mSecurityCallbacksLock{"ConnectionEstablisher::mSecurityCallbacksLock"};
  nsCOMPtr<nsIInterfaceRequestor> mSecurityCallbacks
      MOZ_GUARDED_BY(mSecurityCallbacksLock);
  RefPtr<ConnectionHandle> mHandle;
  RefPtr<HttpConnectionBase> mResultConn;
  RefPtr<HappyEyeballsTransaction> mTransaction;
  DnsMetadata mDnsMetadata;
};

class TCPConnectionEstablisher : public ConnectionEstablisher,
                                 public nsIOutputStreamCallback {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIOUTPUTSTREAMCALLBACK

  TCPConnectionEstablisher(nsHttpConnectionInfo* aConnInfo, NetAddr aAddr,
                           uint32_t aCaps, bool aSpeculative, bool aAllow1918);

  // Starts creating the socket transport + streams, and arms AsyncWait.
  // If it fails synchronously, the callback is invoked before returning.
  bool Start(DoneCallback&& aCallback) override;
  void ResetSpeculativeFlags() override;
  void Close(nsresult aReason) override;

 private:
  ~TCPConnectionEstablisher();

  nsresult CreateAndConfigureSocketTransport();
  void Finish(nsresult aResult) override;

  TimeStamp mSynStarted;
  bool mSpeculative = false;

  nsCOMPtr<nsISocketTransport> mSocketTransport;
  nsCOMPtr<nsIAsyncOutputStream> mStreamOut;
  nsCOMPtr<nsIAsyncInputStream> mStreamIn;
};

class UDPConnectionEstablisher : public ConnectionEstablisher {
 public:
  NS_INLINE_DECL_REFCOUNTING_INHERITED(UDPConnectionEstablisher,
                                       ConnectionEstablisher)

  UDPConnectionEstablisher(nsHttpConnectionInfo* aConnInfo, NetAddr aAddr,
                           uint32_t aCaps, bool aSpeculative, bool aAllow1918);

  bool Start(DoneCallback&& aCallback) override;
  void ResetSpeculativeFlags() override {}
  void Close(nsresult aReason) override;
  bool IsUDP() const override { return true; }

 private:
  ~UDPConnectionEstablisher();

  nsresult CreateAndConfigureUDPConn();
  void Finish(nsresult aResult) override;
};

enum class ConnectionEstablisherType { TCP, UDP };

// Factory for connection establishers. Default creates the real ones; tests
// inject a fake whose establishers complete on demand instead of using sockets.
class ConnectionEstablisherFactory {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ConnectionEstablisherFactory)

  virtual already_AddRefed<ConnectionEstablisher> Create(
      ConnectionEstablisherType aType, nsHttpConnectionInfo* aConnInfo,
      const NetAddr& aAddr, uint32_t aCaps, bool aSpeculative,
      bool aAllow1918) = 0;

 protected:
  virtual ~ConnectionEstablisherFactory() = default;
};

class DefaultConnectionEstablisherFactory final
    : public ConnectionEstablisherFactory {
 public:
  already_AddRefed<ConnectionEstablisher> Create(
      ConnectionEstablisherType aType, nsHttpConnectionInfo* aConnInfo,
      const NetAddr& aAddr, uint32_t aCaps, bool aSpeculative,
      bool aAllow1918) override {
    switch (aType) {
      case ConnectionEstablisherType::TCP:
        return MakeAndAddRef<TCPConnectionEstablisher>(
            aConnInfo, aAddr, aCaps, aSpeculative, aAllow1918);
      case ConnectionEstablisherType::UDP:
        return MakeAndAddRef<UDPConnectionEstablisher>(
            aConnInfo, aAddr, aCaps, aSpeculative, aAllow1918);
    }
    MOZ_ASSERT_UNREACHABLE("unknown ConnectionEstablisherType");
    return nullptr;
  }
};

}  // namespace net
}  // namespace mozilla

#endif
