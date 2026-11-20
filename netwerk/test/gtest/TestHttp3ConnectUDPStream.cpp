/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Http3ConnectUDPStream.h"
#include "Http3Session.h"
#include "TestCommon.h"
#include "gtest/gtest.h"
#include "mozilla/Components.h"
#include "nsHttpConnectionInfo.h"
#include "nsHttpHandler.h"
#include "nsHttpRequestHead.h"
#include "nsIIOService.h"
#include "nsIProtocolHandler.h"
#include "nsIProtocolProxyService.h"
#include "nsIUDPSocket.h"
#include "nsProxyInfo.h"
#include "nsStringStream.h"
#include "nsThreadUtils.h"

using namespace mozilla;
using namespace mozilla::net;

static const char* kProxyHost = "proxy.org";
static const char* kHost = "example.com";
static const int32_t kPort = 4433;
static const char* kMasqueTemplate =
    "/.well-known/masque/udp/{target_host}/{target_port}/";
static const char* kPathHeader = "/.well-known/masque/udp/example.com/4433/";

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
  bool GetWebTransportSessionStats(
      uint64_t aSessionId,
      mozilla::dom::WebTransportStatsData& aStats) override {
    return false;
  }

  void SetWebTransportAnticipatedConcurrentIncomingUnidirectionalStreams(
      uint64_t aSessionId, uint16_t aValue) override {}

  void SetWebTransportAnticipatedConcurrentIncomingBidirectionalStreams(
      uint64_t aSessionId, uint16_t aValue) override {}

  void SendDatagram(Http3WebTransportSession* aSession,
                    nsTArray<uint8_t>& aData, uint64_t aTrackingId,
                    uint64_t aSendGroupId, int64_t aSendOrder,
                    TimeStamp aTimestamp) override {}

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

    (void)mRequestHead->SetHeader(nsHttp::Host, "example.com"_ns);
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

NS_IMPL_ISUPPORTS(DummyHttpTransaction, nsISupportsWeakReference)

class UDPListener final : public nsIUDPSocketSyncListener {
 public:
  NS_DECL_ISUPPORTS

  UDPListener() = default;

  NS_IMETHOD OnPacketReceived(nsIUDPSocket* aSocket) override {
    nsTArray<uint8_t> data;
    NetAddr addr{};
    (void)aSocket->RecvWithAddr(&addr, data);
    mReceivedData.AppendElements(data);
    return NS_OK;
  }

  NS_IMETHOD OnStopListening(nsIUDPSocket* aSocket, nsresult aStatus) override {
    mOnStopListeningCalled = true;
    return NS_OK;
  }

  nsTArray<uint8_t> TakeInputData() { return std::move(mReceivedData); }

  bool OnStopListeningCalled() const { return mOnStopListeningCalled; }

 private:
  ~UDPListener() = default;

  bool mOnStopListeningCalled = false;
  nsTArray<uint8_t> mReceivedData;
};

NS_IMPL_ISUPPORTS(UDPListener, nsIUDPSocketSyncListener)

static void InitHttpHandler() {
  if (gHttpHandler) {
    return;
  }

  nsresult rv;
  nsCOMPtr<nsIIOService> ios = do_GetIOService(&rv);
  if (NS_FAILED(rv)) {
    return;
  }

  nsCOMPtr<nsIProtocolHandler> handler;
  rv = ios->GetProtocolHandler("http", getter_AddRefs(handler));
  if (NS_FAILED(rv)) {
    return;
  }
}

static already_AddRefed<Http3ConnectUDPStream> CreateUDPStream(
    Http3SessionStub* aSession) {
  RefPtr<DummyHttpTransaction> trans = new DummyHttpTransaction();
  RefPtr<Http3ConnectUDPStream> stream =
      new Http3ConnectUDPStream(trans, aSession, NS_GetCurrentThread());

  NetAddr peerAddr;
  peerAddr.InitFromString("127.0.0.1"_ns);
  stream->SetPeerAddr(peerAddr);

  aSession->StreamHasDataToWrite(stream);
  aSession->ProcessOutput();

  // HTTP/3 200
  static constexpr uint8_t kResponse[] = {0x48, 0x54, 0x54, 0x50, 0x2F, 0x33,
                                          0x20, 0x32, 0x30, 0x30, 0x0A, 0x0A};
  static constexpr uint32_t kResponseLen = sizeof(kResponse) - 1;
  nsTArray<uint8_t> response;
  response.AppendElements(kResponse, kResponseLen);

  stream->SetResponseHeaders(response, false, false);
  (void)stream->WriteSegments();

  return stream.forget();
}

namespace ConnectUdp::testing {

static void CreateTestData(uint32_t aNumBytes, nsTArray<uint8_t>& aDataOut) {
  static constexpr const char kSampleText[] =
      "{\"type\":\"message\",\"id\":42,\"payload\":\"The quick brown fox jumps "
      "over the lazy dog.\"}";
  static constexpr uint32_t kSampleTextLen = sizeof(kSampleText) - 1;

  aDataOut.SetCapacity(aNumBytes);

  while (aNumBytes > 0) {
    uint32_t chunkSize = std::min(kSampleTextLen, aNumBytes);
    aDataOut.AppendElements(reinterpret_cast<const uint8_t*>(kSampleText),
                            chunkSize);
    aNumBytes -= chunkSize;
  }
}

static void ValidateData(nsTArray<uint8_t>& aInput,
                         nsTArray<uint8_t>& aExpectedData) {
  ASSERT_EQ(aExpectedData.Length(), aInput.Length());
  for (size_t i = 0; i < aExpectedData.Length(); i++) {
    ASSERT_EQ(aExpectedData[i], aInput[i]);
  }
}

}  // namespace ConnectUdp::testing

TEST(ConnectUDP, SendDataBeforeActivate)
{
  InitHttpHandler();

  RefPtr<Http3SessionStub> session = new Http3SessionStub();
  RefPtr<Http3ConnectUDPStream> stream =
      new Http3ConnectUDPStream(nullptr, session, NS_GetCurrentThread());
  nsCOMPtr<nsIUDPSocket> udp = static_cast<nsIUDPSocket*>(stream.get());
  ASSERT_TRUE(udp);

  NetAddr addr;
  addr.InitFromString("127.0.0.1"_ns);
  nsTArray<uint8_t> data;
  ConnectUdp::testing::CreateTestData(100, data);
  uint32_t written = 0;
  nsresult rv =
      udp->SendWithAddress(&addr, data.Elements(), data.Length(), &written);
  ASSERT_EQ(rv, NS_ERROR_NOT_AVAILABLE);
}

TEST(ConnectUDP, SendData)
{
  InitHttpHandler();

  RefPtr<Http3SessionStub> session = new Http3SessionStub();
  RefPtr<Http3ConnectUDPStream> stream = CreateUDPStream(session);

  ASSERT_TRUE(session->FinishTunnelSetupCalled());
  ASSERT_TRUE(session->AuthHeader().EqualsASCII(kProxyHost));
  ASSERT_TRUE(session->PathHeader().EqualsASCII(kPathHeader));

  nsCOMPtr<nsIUDPSocket> udp = static_cast<nsIUDPSocket*>(stream.get());
  ASSERT_TRUE(udp);

  NetAddr peerAddr;
  peerAddr.InitFromString("127.0.0.1"_ns);
  nsTArray<uint8_t> data;
  ConnectUdp::testing::CreateTestData(100, data);
  uint32_t written = 0;
  nsresult rv =
      udp->SendWithAddress(&peerAddr, data.Elements(), data.Length(), &written);
  ASSERT_EQ(rv, NS_OK);

  NS_ProcessPendingEvents(nullptr);

  session->ProcessOutput();

  nsTArray<uint8_t> output = session->TakeOutputData();
  ConnectUdp::testing::ValidateData(data, output);

  data.Clear();
  ConnectUdp::testing::CreateTestData(200, data);

  rv =
      udp->SendWithAddress(&peerAddr, data.Elements(), data.Length(), &written);
  ASSERT_EQ(rv, NS_OK);

  NS_ProcessPendingEvents(nullptr);

  session->ProcessOutput();
  output = session->TakeOutputData();
  ConnectUdp::testing::ValidateData(data, output);
  ASSERT_EQ(stream->ByteCountSent(), 300u);

  udp->Close();
}

TEST(ConnectUDP, RecvData)
{
  InitHttpHandler();

  RefPtr<Http3SessionStub> session = new Http3SessionStub();
  RefPtr<Http3ConnectUDPStream> stream = CreateUDPStream(session);

  ASSERT_TRUE(session->FinishTunnelSetupCalled());
  ASSERT_TRUE(session->AuthHeader().EqualsASCII(kProxyHost));
  ASSERT_TRUE(session->PathHeader().EqualsASCII(kPathHeader));

  nsCOMPtr<nsIUDPSocket> udp = static_cast<nsIUDPSocket*>(stream.get());
  ASSERT_TRUE(udp);

  RefPtr<UDPListener> listener = new UDPListener();
  udp->SyncListen(listener);

  nsTArray<uint8_t> data;
  ConnectUdp::testing::CreateTestData(100, data);
  stream->OnDatagramReceived(std::move(data));

  nsTArray<uint8_t> input = listener->TakeInputData();
  ASSERT_EQ(input.Length(), 100u);

  ConnectUdp::testing::CreateTestData(200, data);
  stream->OnDatagramReceived(std::move(data));

  input = listener->TakeInputData();
  ASSERT_EQ(input.Length(), 200u);

  ASSERT_EQ(stream->ByteCountReceived(), 300u);

  udp->Close();

  ASSERT_EQ(listener->OnStopListeningCalled(), true);
}
