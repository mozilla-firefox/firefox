/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Http3ConnectUDPStream.h"
#include "Http3TestStubs.h"
#include "TestCommon.h"
#include "gtest/gtest.h"
#include "nsIUDPSocket.h"
#include "nsThreadUtils.h"

using namespace mozilla;
using namespace mozilla::net;

static const char* kPathHeader = "/.well-known/masque/udp/example.com/4433/";

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
