/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_Http3WebTransportSession_h
#define mozilla_net_Http3WebTransportSession_h

#include "Http3StreamBase.h"
#include "WebTransportSessionBase.h"
#include "mozilla/dom/PWebTransport.h"
#include "mozilla/net/NeqoHttp3Conn.h"
#include "nsIWebTransport.h"

namespace mozilla::net {

class Http3Session;

// Parses the flat "HTTP/3 200\r\nname: value\r\n...\r\n\r\n"-style buffer
// saved from the CONNECT response into aNames/aValues, skipping the status
// line and filtering out "wt-protocol" (see
// https://w3c.github.io/webtransport/#process-a-webtransport-fetch-response
// step 6). Exposed as a free function so it can be unit-tested (see
// TestHttp3WebTransportSession.cpp) without constructing a full
// Http3WebTransportSession.
void ParseFlatWebTransportResponseHeaders(const nsTArray<uint8_t>& aFlatHeaders,
                                          nsTArray<nsCString>& aNames,
                                          nsTArray<nsCString>& aValues);

class Http3TunnelStreamBase : public Http3StreamBase,
                              public nsAHttpSegmentWriter,
                              public nsAHttpSegmentReader {
 public:
  NS_DECL_NSAHTTPSEGMENTWRITER
  NS_DECL_NSAHTTPSEGMENTREADER

  explicit Http3TunnelStreamBase(nsAHttpTransaction* trans,
                                 Http3SessionBase* aHttp3Session);

  [[nodiscard]] nsresult ReadSegments() override;
  [[nodiscard]] nsresult WriteSegments() override;

  void SetResponseHeaders(nsTArray<uint8_t>& aResponseHeaders, bool fin,
                          bool interim) override;

  virtual nsresult TryActivating();

  bool Done() const override { return mRecvState == RECV_DONE; }

  virtual void OnDatagramReceived(nsTArray<uint8_t>&& aData) = 0;

  void TransactionIsDone(nsresult aResult);

  virtual bool OnActivated() { return true; }
  virtual nsresult OnProcessDatagram() { return NS_OK; }

 protected:
  bool ConsumeHeaders(const char* buf, uint32_t avail, uint32_t* countUsed);
  virtual void OnClosePending() = 0;

  enum RecvStreamState {
    BEFORE_HEADERS,
    READING_HEADERS,
    READING_INTERIM_HEADERS,
    ACTIVE,
    CLOSE_PENDING,
    RECV_DONE
  } mRecvState{BEFORE_HEADERS};

  enum SendStreamState {
    PREPARING_HEADERS,
    WAITING_TO_ACTIVATE,
    PROCESSING_DATAGRAM,
    SEND_DONE,
  } mSendState{PREPARING_HEADERS};

  nsCString mFlatHttpRequestHeaders;
  nsTArray<uint8_t> mFlatResponseHeaders;
  nsTArray<uint8_t> mSavedFlatResponseHeaders;

  nsresult mSocketInCondition = NS_ERROR_NOT_INITIALIZED;
  nsresult mSocketOutCondition = NS_ERROR_NOT_INITIALIZED;
};

// TODO Http3WebTransportSession is very similar to Http3Stream. It should
// be built on top of it with a couple of small changes. The docs will be added
// when this is implemented.

class Http3WebTransportSession final : public WebTransportSessionBase,
                                       public Http3TunnelStreamBase {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(Http3WebTransportSession, override)

  Http3WebTransportSession(nsAHttpTransaction*, Http3Session*);

  Http3WebTransportSession* GetHttp3WebTransportSession() override {
    return this;
  }
  Http3WebTransportStream* GetHttp3WebTransportStream() override {
    return nullptr;
  }
  Http3Stream* GetHttp3Stream() override { return nullptr; }
  Http3ConnectUDPStream* GetHttp3ConnectUDPStream() override { return nullptr; }
  Http3StreamTunnel* GetHttp3StreamTunnel() override { return nullptr; }

  void Close(nsresult aResult) override;

  void CloseSession(uint32_t aStatus, const nsACString& aReason) override;
  bool CloseSessionAndGetStats(uint32_t aStatus, const nsACString& aReason,
                               mozilla::dom::WebTransportStatsData& aStats);
  void OnSessionClosed(bool aCleanly, uint32_t aStatus,
                       const nsACString& aReason);
  void OnSessionClosedWithStats(
      bool aCleanly, uint32_t aStatus, const nsACString& aReason,
      const mozilla::dom::WebTransportStatsData& aStats);
  void OnSessionDraining();

  uint64_t GetStreamId() const override;

  void CreateOutgoingBidirectionalStream(
      std::function<void(Result<RefPtr<WebTransportStreamBase>, nsresult>&&)>&&
          aCallback) override;
  void CreateOutgoingUnidirectionalStream(
      std::function<void(Result<RefPtr<WebTransportStreamBase>, nsresult>&&)>&&
          aCallback) override;
  void RemoveWebTransportStream(Http3WebTransportStream* aStream);

  already_AddRefed<Http3WebTransportStream> OnIncomingWebTransportStream(
      WebTransportStreamType aType, uint64_t aId);

  void SendDatagram(nsTArray<uint8_t>&& aData, uint64_t aTrackingId,
                    uint64_t aSendGroupId, int64_t aSendOrder,
                    TimeStamp aTimestamp) override;

  void OnDatagramReceived(nsTArray<uint8_t>&& aData) override;

  void GetMaxDatagramSize() override;

  void SetDatagramMaxBufferedDatagrams(uint32_t aMaxBufferedDatagrams) override;
  void SetDatagramMaxAge(double aMaxAge) override;

  nsresult ExportKeyingMaterial(const nsTArray<uint8_t>& aLabel,
                                const nsTArray<uint8_t>& aContext,
                                uint32_t aOutputLength,
                                nsTArray<uint8_t>& aKeyingMaterial) override;

  void GetStats() override;

  nsresult RegisterSendGroup(uint64_t aGroupId) override;

  void GetNegotiatedProtocol(nsACString& aProtocol) override;
  void GetResponseHeaders(nsTArray<nsCString>& aNames,
                          nsTArray<nsCString>& aValues) override;

  void OnOutgoingDatagramOutCome(
      uint64_t aId, WebTransportSessionEventListener::DatagramOutcome aOutCome);

  void SetAnticipatedConcurrentIncomingUnidirectionalStreams(
      uint16_t aValue) override;
  void SetAnticipatedConcurrentIncomingBidirectionalStreams(
      uint16_t aValue) override;

  void OnStreamStopSending(uint64_t aId, uint64_t aErrorCode);
  void OnStreamReset(uint64_t aId, uint64_t aErrorCode);

 private:
  virtual ~Http3WebTransportSession();

  void CreateStreamInternal(
      bool aBidi,
      std::function<void(Result<RefPtr<WebTransportStreamBase>, nsresult>&&)>&&
          aCallback);
  void OnClosePending() override;

  nsTArray<RefPtr<Http3WebTransportStream>> mStreams;
  uint32_t mStatus{0};
  nsCString mReason;

  // Cached stats for passing to OnSessionClosed
  mozilla::dom::WebTransportStatsData mCachedStats;
};

}  // namespace mozilla::net
#endif  // mozilla_net_Http3WebTransportSession_h
