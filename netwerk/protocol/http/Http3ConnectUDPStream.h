/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_Http3ConnectUDPStream_h
#define mozilla_net_Http3ConnectUDPStream_h

#include "Http3StreamBase.h"
#include "Http3WebTransportSession.h"
#include "mozilla/Queue.h"
#include "mozilla/net/DNS.h"
#include "nsASocketHandler.h"
#include "nsIUDPSocket.h"

namespace mozilla::net {

class Http3WebTransportSession;
class HttpConnectionUDP;

class UDPPayload final {
 public:
  explicit UDPPayload(nsTArray<uint8_t>&& aData) : mData(std::move(aData)) {
    MOZ_COUNT_CTOR(UDPPayload);
  }

  MOZ_COUNTED_DTOR(UDPPayload)

  nsTArray<uint8_t> TakeData() { return std::move(mData); }

 private:
  nsTArray<uint8_t> mData;
};

class Http3ConnectUDPStream final : public Http3TunnelStreamBase,
                                    public nsASocketHandler,
                                    public nsIUDPSocket {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIUDPSOCKET

  explicit Http3ConnectUDPStream(nsAHttpTransaction* aTrans,
                                 Http3SessionBase* aSession,
                                 nsIEventTarget* aTarget);

  Http3WebTransportSession* GetHttp3WebTransportSession() override {
    return nullptr;
  }
  Http3WebTransportStream* GetHttp3WebTransportStream() override {
    return nullptr;
  }
  Http3Stream* GetHttp3Stream() override { return nullptr; }
  Http3ConnectUDPStream* GetHttp3ConnectUDPStream() override { return this; }
  Http3StreamTunnel* GetHttp3StreamTunnel() override { return nullptr; }

  nsresult TryActivating() override;

  void SetPeerAddr(const NetAddr& addr) { mAddr = addr; }

  void Close(nsresult aResult) override;

  void OnDatagramReceived(nsTArray<uint8_t>&& aData) override;

  bool OnActivated() override;

  nsresult OnProcessDatagram() override;

  // nsASocketHandler methods:
  void OnSocketReady(PRFileDesc* fd, int16_t outFlags) override;
  void OnSocketDetached(PRFileDesc* fd) override;
  void IsLocal(bool* aIsLocal) override;
  nsresult GetRemoteAddr(NetAddr* addr) override;

  uint64_t ByteCountSent() override { return mByteWriteCount; }
  uint64_t ByteCountReceived() override { return mByteReadCount; }

  bool IsTRRConnection() override { return mIsTRRServiceChannel; }

  already_AddRefed<HttpConnectionUDP> CreateUDPConnection(
      nsIInterfaceRequestor* aCallbacks);

  void OnSessionClosed(bool aCleanly, uint32_t aStatus,
                       const nsACString& aReason);

 private:
  virtual ~Http3ConnectUDPStream();
  void OnClosePending() override {}

  NetAddr mAddr;
  nsCOMPtr<nsIUDPSocketSyncListener> mSyncListener;

  uint64_t mByteReadCount{0};
  uint64_t mByteWriteCount{0};

  nsCOMPtr<nsIEventTarget> mTarget;
  mozilla::Queue<UniquePtr<UDPPayload>> mReceivedData;
  mozilla::Queue<UniquePtr<UDPPayload>> mOutputData;
  uint64_t mTrackingId{1};

  bool mIsTRRServiceChannel{false};
};

}  // namespace mozilla::net

#endif  // mozilla_net_Http3ConnectUDPStream_h
