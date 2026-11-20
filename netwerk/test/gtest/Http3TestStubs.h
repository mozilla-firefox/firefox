/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NETWERK_TEST_GTEST_HTTP3TESTSTUBS_H_
#define NETWERK_TEST_GTEST_HTTP3TESTSTUBS_H_

#include "Http3Session.h"
#include "Http3StreamBase.h"
#include "mozilla/Components.h"
#include "nsAHttpTransaction.h"
#include "nsHttpConnectionInfo.h"
#include "nsHttpRequestHead.h"
#include "nsIProtocolProxyService.h"
#include "nsProxyInfo.h"
#include "nsStringStream.h"

namespace mozilla::net {

static constexpr const char* kProxyHost = "proxy.org";
static constexpr const char* kHost = "example.com";
static constexpr int32_t kPort = 4433;
static constexpr const char* kMasqueTemplate =
    "/.well-known/masque/udp/{target_host}/{target_port}/";

class Http3SessionStub final : public Http3SessionBase {
 public:
  NS_INLINE_DECL_REFCOUNTING(Http3SessionStub, override)

  nsresult TryActivating(const nsACString& aMethod, const nsACString& aScheme,
                         const nsACString& aAuthorityHeader,
                         const nsACString& aPath, const nsACString& aHeaders,
                         uint64_t* aStreamId,
                         Http3StreamBase* aStream) override {
    mPathHeader = aPath;
    mAuthHeader = aAuthorityHeader;
    return NS_OK;
  }

  void CloseSendingSide(uint64_t aStreamId) override {}

  void SendHTTPDatagram(uint64_t aStreamId, nsTArray<uint8_t>& aData,
                        uint64_t aTrackingId) override {
    mOutputData.AppendElements(aData);
  }

  nsresult SendRequestBody(uint64_t aStreamId, const char* buf, uint32_t count,
                           uint32_t* countRead) override {
    return NS_OK;
  }

  nsresult ReadResponseData(uint64_t aStreamId, char* aBuf, uint32_t aCount,
                            uint32_t* aCountWritten, bool* aFin) override {
    *aCountWritten = 0;
    *aFin = false;
    return NS_OK;
  }

  nsresult SendPriorityUpdateFrame(uint64_t aStreamId, uint8_t aPriorityUrgency,
                                   bool aPriorityIncremental) override {
    return NS_OK;
  }

  void ConnectSlowConsumer(Http3StreamBase* stream) override {}

  void CloseWebTransportConn() override {}

  void StreamHasDataToWrite(Http3StreamBase* aStream) override {
    mReadyForWrite.AppendElement(aStream);
  }

  bool CloseWebTransport(uint64_t aSessionId, uint32_t aError,
                         const nsACString& aMessage,
                         mozilla::dom::WebTransportStatsData& aStats) override {
    return false;
  }

  void SetDatagramMaxBufferedDatagrams(
      uint64_t aSessionId, uint32_t aMaxBufferedDatagrams) override {}

  void SetDatagramMaxAge(uint64_t aSessionId, double aMaxAge) override {}
  void RequestOutgoingDatagramProgress() override {}
  bool GetWebTransportSessionStats(
      uint64_t aSessionId,
      mozilla::dom::WebTransportStatsData& aStats) override {
    return false;
  }

  void SetWebTransportAnticipatedConcurrentIncomingUnidirectionalStreams(
      uint64_t aSessionId, uint16_t aValue) override {}

  void SetWebTransportAnticipatedConcurrentIncomingBidirectionalStreams(
      uint64_t aSessionId, uint16_t aValue) override {}

  nsresult SendDatagram(Http3WebTransportSession* aSession,
                        nsTArray<uint8_t>& aData, uint64_t aSendGroupId,
                        int64_t aSendOrder, TimeStamp aTimestamp) override {
    if (NS_SUCCEEDED(mSendDatagramResult)) {
      mDatagramsEnqueued++;
    }
    return mSendDatagramResult;
  }

  bool GetDatagramQueueCapacity(uint64_t aSessionId, size_t* aRemainingBytes,
                                size_t* aQueuedDatagrams,
                                size_t* aMaxQueuedBytes) override {
    return false;
  }

  bool GetDatagramSessionStats(uint64_t aSessionId, uint64_t* aSent,
                               uint64_t* aExpired,
                               uint64_t* aDropped) override {
    *aSent = mDatagramsSent;
    *aExpired = mDatagramsExpired;
    *aDropped = mDatagramsDropped;
    return true;
  }

  uint64_t MaxDatagramSize(uint64_t aSessionId) override { return 0; }

  nsresult ExportWebTransportKeyingMaterial(
      uint64_t aSessionId, const nsTArray<uint8_t>& aLabel,
      const nsTArray<uint8_t>& aContext, uint32_t aOutputLength,
      nsTArray<uint8_t>& aKeyingMaterial) override {
    return NS_OK;
  }

  nsresult RegisterWebTransportSendGroup(uint64_t aSessionId,
                                         uint64_t aGroupId) override {
    return NS_OK;
  }

  nsresult GetWebTransportSessionProtocol(uint64_t aSessionId,
                                          nsACString& aProtocol) override {
    return NS_OK;
  }

  nsresult TryActivatingWebTransportStream(uint64_t* aStreamId,
                                           Http3StreamBase* aStream) override {
    *aStreamId = 0;
    return NS_OK;
  }

  void ResetWebTransportStream(Http3WebTransportStream* aStream,
                               uint64_t aErrorCode) override {}

  void StreamStopSending(Http3WebTransportStream* aStream,
                         uint64_t aErrorCode) override {}

  void SetSendOrder(Http3StreamBase* aStream, int64_t aSendOrder) override {}

  void SetSendGroup(Http3StreamBase* aStream, uint64_t aSendGroupId) override {}

  void ProcessOutput() {
    for (const auto& stream : mReadyForWrite) {
      (void)stream->ReadSegments();
    }
    mReadyForWrite.Clear();
  }

  void FinishTunnelSetup(nsAHttpTransaction* aTransaction) override {
    mFinishTunnelSetupCalled = true;
  }

  bool FinishTunnelSetupCalled() const { return mFinishTunnelSetupCalled; }

  nsTArray<uint8_t> TakeOutputData() { return std::move(mOutputData); }

  const nsCString& PathHeader() { return mPathHeader; }
  const nsCString& AuthHeader() { return mAuthHeader; }

  // What SendDatagram() returns, standing in for neqo's enqueue result.
  nsresult mSendDatagramResult = NS_OK;
  uint32_t mDatagramsEnqueued = 0;
  // Reported by GetDatagramSessionStats(), standing in for neqo's own counts.
  uint64_t mDatagramsSent = 0;
  uint64_t mDatagramsExpired = 0;
  uint64_t mDatagramsDropped = 0;

 private:
  ~Http3SessionStub() = default;

  nsTArray<RefPtr<Http3StreamBase>> mReadyForWrite;
  nsTArray<uint8_t> mOutputData;
  nsCString mPathHeader;
  nsCString mAuthHeader;
  bool mFinishTunnelSetupCalled = false;
};

class DummyHttpTransaction : public nsAHttpTransaction {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  DummyHttpTransaction() {
    nsCString buffer;
    buffer.AssignLiteral("capsule-protocol = ?1\r\n\r\n");
    NS_NewCStringInputStream(getter_AddRefs(mRequestStream), buffer);

    nsCOMPtr<nsIProtocolProxyService> pps;
    pps = mozilla::components::ProtocolProxy::Service();
    if (pps) {
      nsCOMPtr<nsIProxyInfo> info;
      nsresult rv = pps->NewMASQUEProxyInfo(
          nsCString(kProxyHost), -1, nsCString(kMasqueTemplate), ""_ns, ""_ns,
          0, 0, nullptr, getter_AddRefs(info));
      if (NS_FAILED(rv)) {
        return;
      }
      mConnInfo = new nsHttpConnectionInfo(
          nsCString(kHost), kPort, ""_ns, ""_ns,
          static_cast<nsProxyInfo*>(info.get()), OriginAttributes());
    }
  }

  static nsresult ReadRequestSegment(nsIInputStream* stream, void* closure,
                                     const char* buf, uint32_t offset,
                                     uint32_t count, uint32_t* countRead) {
    DummyHttpTransaction* trans = (DummyHttpTransaction*)closure;
    return trans->mReader->OnReadSegment(buf, count, countRead);
  }

  void SetConnection(nsAHttpConnection*) override {}
  nsAHttpConnection* Connection() override { return nullptr; }
  void GetSecurityCallbacks(nsIInterfaceRequestor**) override {}
  void OnTransportStatus(nsITransport* transport, nsresult status,
                         int64_t progress) override {}
  bool IsDone() override { return mIsDone; }
  nsresult Status() override { return NS_OK; }
  uint32_t Caps() override { return 0; }
  [[nodiscard]] nsresult ReadSegments(nsAHttpSegmentReader* reader,
                                      uint32_t count,
                                      uint32_t* countRead) override {
    mReader = reader;
    (void)mRequestStream->ReadSegments(ReadRequestSegment, this, count,
                                       countRead);
    mReader = nullptr;
    return NS_OK;
  }
  [[nodiscard]] nsresult WriteSegments(nsAHttpSegmentWriter* writer,
                                       uint32_t count,
                                       uint32_t* countWritten) override {
    char buf[1024];
    (void)writer->OnWriteSegment(buf, 1024, countWritten);
    mIsDone = true;
    return NS_OK;
  }
  void Close(nsresult reason) override {}
  nsHttpConnectionInfo* ConnectionInfo() override { return mConnInfo.get(); }
  void SetProxyConnectFailed() override {}
  nsHttpRequestHead* RequestHead() override {
    if (mRequestHead) {
      return mRequestHead.get();
    }

    mRequestHead = std::make_unique<nsHttpRequestHead>();

    // Not nsHttp::Host: odr-using it here makes Http3TestStubs.o, which links
    // before TestHttpAtom.o, supply its definition, and under ASAN that
    // breaks TestHttpAtom's string-literal pointer comparison.
    (void)mRequestHead->SetHeader("Host"_ns, "example.com"_ns);
    return mRequestHead.get();
  }
  uint32_t Http1xTransactionCount() override { return 0; }
  [[nodiscard]] nsresult TakeSubTransactions(
      nsTArray<RefPtr<nsAHttpTransaction>>& outTransactions) override {
    return NS_OK;
  }

 private:
  virtual ~DummyHttpTransaction() = default;

  nsAHttpSegmentReader* mReader{nullptr};
  nsCOMPtr<nsIInputStream> mRequestStream;
  UniquePtr<nsHttpRequestHead> mRequestHead;
  bool mIsDone = false;
  RefPtr<nsHttpConnectionInfo> mConnInfo;
};

void InitHttpHandler();

}  // namespace mozilla::net

#endif  // NETWERK_TEST_GTEST_HTTP3TESTSTUBS_H_
