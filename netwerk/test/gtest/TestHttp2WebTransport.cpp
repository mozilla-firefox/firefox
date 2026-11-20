/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <utility>

#include "Capsule.h"
#include "CapsuleEncoder.h"
#include "CapsuleParser.h"
#include "Http2WebTransportSession.h"
#include "Http2WebTransportStream.h"
#include "TestCommon.h"
#include "gtest/gtest.h"
#include "mozilla/Queue.h"
#include "mozilla/gtest/MozAssertions.h"
#include "mozilla/net/NeqoHttp3Conn.h"
#include "nsIWebTransport.h"
#include "nsStreamUtils.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"

using namespace mozilla;
using namespace mozilla::net;

class MockWebTransportClient : public CapsuleIOHandler {
 public:
  NS_INLINE_DECL_REFCOUNTING(MockWebTransportClient, override)

  explicit MockWebTransportClient(Http2WebTransportInitialSettings aSettings)
      : mSession(new Http2WebTransportSessionImpl(this, aSettings)),
        mParser(std::make_unique<CapsuleParser>(mSession)) {}

  Http2WebTransportSessionImpl* Session() { return mSession; }

  void HasCapsuleToSend() override {
    mSession->PrepareCapsulesToSend(mOutCapsules);
  }

  void SetSentFin() override { mSetSentFinCalled = true; }

  void StartReading() override { mStartReadingCalled = true; }

  void OnCapsuleParseFailure(nsresult aError) override {
    mOnParseFailureCalled = true;
  }

  void ProcessInputCapsules(
      mozilla::Queue<UniquePtr<CapsuleEncoder>>&& aCapsules) {
    while (!aCapsules.IsEmpty()) {
      UniquePtr<CapsuleEncoder> capsule = aCapsules.Pop();
      auto buffer = capsule->GetBuffer();
      mParser->ProcessCapsuleData(buffer.Elements(), buffer.Length());
    }
  }

  void ProcessOutput() {
    mSession->PrepareCapsulesToSend(mOutCapsules);
    mozilla::Queue<UniquePtr<CapsuleEncoder>> queue(std::move(mOutCapsules));
    while (!queue.IsEmpty()) {
      UniquePtr<CapsuleEncoder> encoder = queue.Pop();
      auto metadata = encoder->GetStreamMetadata();
      if (metadata) {
        mSession->OnStreamDataSent(StreamId(metadata->mID),
                                   metadata->mDataSize);
      }
      mOutCapsules.Push(std::move(encoder));
    }
  }

  mozilla::Queue<UniquePtr<CapsuleEncoder>> GetOutCapsules() {
    return std::move(mOutCapsules);
  }

  void Done() {
    mParser = nullptr;
    mSession->Close(NS_OK);
    mSession = nullptr;
  }

  // State tracking
  bool mSetSentFinCalled = false;
  bool mStartReadingCalled = false;
  bool mOnParseFailureCalled = false;

 private:
  ~MockWebTransportClient() = default;

  RefPtr<Http2WebTransportSessionImpl> mSession;
  UniquePtr<CapsuleParser> mParser;
  mozilla::Queue<UniquePtr<CapsuleEncoder>> mOutCapsules;
};

class MockWebTransportServer : public CapsuleParser::Listener {
 public:
  NS_INLINE_DECL_REFCOUNTING(MockWebTransportServer, override)

  explicit MockWebTransportServer()
      : mParser(std::make_unique<CapsuleParser>(this)) {}

  bool OnCapsule(Capsule&& aCapsule) override {
    mReceivedCapsules.AppendElement(std::move(aCapsule));
    return true;
  }
  void OnCapsuleParseFailure(nsresult aError) override {
    MOZ_RELEASE_ASSERT(false);
  }

  nsTArray<Capsule> GetReceivedCapsules() {
    return std::move(mReceivedCapsules);
  }

  void ProcessInputCapsules(
      mozilla::Queue<UniquePtr<CapsuleEncoder>>&& aCapsules) {
    while (!aCapsules.IsEmpty()) {
      UniquePtr<CapsuleEncoder> capsule = aCapsules.Pop();
      auto buffer = capsule->GetBuffer();
      mParser->ProcessCapsuleData(buffer.Elements(), buffer.Length());
    }
  }

  void SendWebTransportMaxStreamsCapsule(uint64_t aLimit, bool aBidi) {
    Capsule capsule = Capsule::WebTransportMaxStreams(aLimit, aBidi);
    auto encoder = std::make_unique<CapsuleEncoder>();
    encoder->EncodeCapsule(capsule);
    mOutCapsules.Push(std::move(encoder));
  }

  void SendWebTransportStreamDataCapsule(uint64_t aID, bool aFin,
                                         nsTArray<uint8_t>&& aData) {
    Capsule capsule =
        Capsule::WebTransportStreamData(aID, aFin, std::move(aData));
    auto encoder = std::make_unique<CapsuleEncoder>();
    encoder->EncodeCapsule(capsule);
    mOutCapsules.Push(std::move(encoder));
  }

  void SendWebTransportMaxStreamDataCapsule(uint64_t aLimit, uint64_t aID) {
    Capsule capsule = Capsule::WebTransportMaxStreamData(aLimit, aID);
    auto encoder = std::make_unique<CapsuleEncoder>();
    encoder->EncodeCapsule(capsule);
    mOutCapsules.Push(std::move(encoder));
  }

  void SendWebTransportMaxDataCapsule(uint64_t aLimit) {
    Capsule capsule = Capsule::WebTransportMaxData(aLimit);
    auto encoder = std::make_unique<CapsuleEncoder>();
    encoder->EncodeCapsule(capsule);
    mOutCapsules.Push(std::move(encoder));
  }

  void SendWebTransportStopSendingCapsule(uint64_t aError, uint64_t aID) {
    Capsule capsule = Capsule::WebTransportStopSending(aError, aID);
    auto encoder = std::make_unique<CapsuleEncoder>();
    encoder->EncodeCapsule(capsule);
    mOutCapsules.Push(std::move(encoder));
  }

  void SendWebTransportResetStreamCapsule(uint64_t aError, uint64_t aSize,
                                          uint64_t aID) {
    Capsule capsule = Capsule::WebTransportResetStream(aError, aSize, aID);
    auto encoder = std::make_unique<CapsuleEncoder>();
    encoder->EncodeCapsule(capsule);
    mOutCapsules.Push(std::move(encoder));
  }

  void SendDatagramCapsule(nsTArray<uint8_t>&& aPayload) {
    Capsule capsule = Capsule::WebTransportDatagram(std::move(aPayload));
    auto encoder = std::make_unique<CapsuleEncoder>();
    encoder->EncodeCapsule(capsule);
    mOutCapsules.Push(std::move(encoder));
  }

  mozilla::Queue<UniquePtr<CapsuleEncoder>> GetOutCapsules() {
    return std::move(mOutCapsules);
  }

  void Done() { mParser = nullptr; }

 private:
  ~MockWebTransportServer() = default;

  UniquePtr<CapsuleParser> mParser;
  nsTArray<Capsule> mReceivedCapsules;
  mozilla::Queue<UniquePtr<CapsuleEncoder>> mOutCapsules;
};

// TODO: will be used when testing incoming streams.
class MockWebTransportSessionEventListener
    : public WebTransportSessionEventListener,
      public WebTransportSessionEventListenerInternal {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_WEBTRANSPORTSESSIONEVENTLISTENER
  NS_DECL_WEBTRANSPORTSESSIONEVENTLISTENERINTERNAL

  MockWebTransportSessionEventListener() {}
  nsTArray<RefPtr<WebTransportStreamBase>> TakeIncomingStreams() {
    return std::move(mIncomingStreams);
  }
  nsTArray<uint8_t> mReceivedDatagrams;

  Maybe<std::pair<uint64_t, uint64_t>> TakeStopSending() {
    return std::move(mStopSending);
  }

  Maybe<std::pair<uint64_t, uint64_t>> TakeReset() { return std::move(mReset); }

 private:
  virtual ~MockWebTransportSessionEventListener() = default;

  nsTArray<RefPtr<WebTransportStreamBase>> mIncomingStreams;
  Maybe<std::pair<uint64_t, uint64_t>> mStopSending;
  Maybe<std::pair<uint64_t, uint64_t>> mReset;
};

NS_IMPL_ISUPPORTS(MockWebTransportSessionEventListener,
                  WebTransportSessionEventListener,
                  WebTransportSessionEventListenerInternal)

NS_IMETHODIMP
MockWebTransportSessionEventListener::OnSessionReadyInternal(
    WebTransportSessionBase* aSession) {
  return NS_OK;
}

NS_IMETHODIMP
MockWebTransportSessionEventListener::OnIncomingStreamAvailableInternal(
    WebTransportStreamBase* aStream) {
  mIncomingStreams.AppendElement(RefPtr{aStream});
  return NS_OK;
}

NS_IMETHODIMP
MockWebTransportSessionEventListener::OnIncomingBidirectionalStreamAvailable(
    nsIWebTransportBidirectionalStream* aStream) {
  return NS_OK;
}

NS_IMETHODIMP
MockWebTransportSessionEventListener::OnIncomingUnidirectionalStreamAvailable(
    nsIWebTransportReceiveStream* aStream) {
  return NS_OK;
}

NS_IMETHODIMP
MockWebTransportSessionEventListener::OnSessionReady(uint64_t ready) {
  return NS_OK;
}

NS_IMETHODIMP
MockWebTransportSessionEventListener::OnSessionClosed(
    bool aCleanly, uint32_t aStatus, const nsACString& aReason,
    nsIWebTransportSessionStats* aStats) {
  return NS_OK;
}

NS_IMETHODIMP
MockWebTransportSessionEventListener::OnDraining() { return NS_OK; }

NS_IMETHODIMP MockWebTransportSessionEventListener::OnDatagramReceivedInternal(
    nsTArray<uint8_t>&& aData) {
  mReceivedDatagrams = std::move(aData);
  return NS_OK;
}

NS_IMETHODIMP MockWebTransportSessionEventListener::OnDatagramReceived(
    const nsTArray<uint8_t>& aData) {
  return NS_OK;
}

NS_IMETHODIMP MockWebTransportSessionEventListener::OnMaxDatagramSize(
    uint64_t aSize) {
  return NS_OK;
}

NS_IMETHODIMP
MockWebTransportSessionEventListener::OnOutgoingDatagramOutCome(
    uint64_t aId, WebTransportSessionEventListener::DatagramOutcome aOutCome) {
  return NS_OK;
}

NS_IMETHODIMP
MockWebTransportSessionEventListener::GetAnticipatedConcurrentIncomingStreams(
    uint16_t* aUni, uint16_t* aBidi) {
  *aUni = 0;
  *aBidi = 0;
  return NS_OK;
}

NS_IMETHODIMP MockWebTransportSessionEventListener::OnStopSending(
    uint64_t aStreamId, uint64_t aErrorCode) {
  mStopSending = Some(std::pair<uint64_t, uint64_t>(aStreamId, aErrorCode));
  return NS_OK;
}

NS_IMETHODIMP MockWebTransportSessionEventListener::OnResetReceived(
    uint64_t aStreamId, uint64_t aErrorCode) {
  mReset = Some(std::pair<uint64_t, uint64_t>(aStreamId, aErrorCode));
  return NS_OK;
}

NS_IMETHODIMP MockWebTransportSessionEventListener::OnStatsAvailable(
    nsIWebTransportSessionStats* aStats) {
  return NS_OK;
}

static void ServerProcessCapsules(MockWebTransportServer* aServer,
                                  MockWebTransportClient* aClient) {
  aClient->ProcessOutput();
  mozilla::Queue<UniquePtr<CapsuleEncoder>> outCapsules =
      aClient->GetOutCapsules();
  aServer->ProcessInputCapsules(std::move(outCapsules));
}

static void ClientProcessCapsules(MockWebTransportServer* aServer,
                                  MockWebTransportClient* aClient) {
  mozilla::Queue<UniquePtr<CapsuleEncoder>> outCapsules =
      aServer->GetOutCapsules();
  aClient->ProcessInputCapsules(std::move(outCapsules));
}

TEST(TestHttp2WebTransport, CloseSessionCapsule)
{
  RefPtr<MockWebTransportClient> client =
      new MockWebTransportClient(Http2WebTransportInitialSettings());
  RefPtr<MockWebTransportServer> server = new MockWebTransportServer();

  nsCString reason("test");
  client->Session()->CloseSession(42, "test"_ns);

  ServerProcessCapsules(server, client);

  nsTArray<Capsule> received = server->GetReceivedCapsules();
  ASSERT_EQ(received.Length(), 1u);

  CloseWebTransportSessionCapsule& parsedCapsule =
      received[0].GetCloseWebTransportSessionCapsule();
  ASSERT_EQ(parsedCapsule.mStatus, 42u);
  ASSERT_EQ(parsedCapsule.mReason, reason);

  client->Done();
  server->Done();
}

TEST(TestHttp2WebTransport, CreateOutgoingStream)
{
  RefPtr<MockWebTransportClient> client =
      new MockWebTransportClient(Http2WebTransportInitialSettings());
  RefPtr<MockWebTransportServer> server = new MockWebTransportServer();

  RefPtr<WebTransportStreamBase> bidiStream;
  auto callback =
      [&](Result<RefPtr<WebTransportStreamBase>, nsresult>&& aResult) {
        if (aResult.isErr()) {
          return;
        }
        bidiStream = aResult.unwrap();
      };
  client->Session()->CreateOutgoingBidirectionalStream(std::move(callback));
  ASSERT_TRUE(bidiStream == nullptr);

  ServerProcessCapsules(server, client);

  // Creating a stream is blocked, we should see a
  // WebTransportStreamsBlockedCapsule from the client.
  nsTArray<Capsule> received = server->GetReceivedCapsules();
  ASSERT_EQ(received.Length(), 1u);

  WebTransportStreamsBlockedCapsule& streamsBlocked =
      received[0].GetWebTransportStreamsBlockedCapsule();
  ASSERT_EQ(streamsBlocked.mLimit, 0u);
  ASSERT_EQ(streamsBlocked.mBidi, true);

  server->SendWebTransportMaxStreamsCapsule(1, true);
  ClientProcessCapsules(server, client);
  ASSERT_TRUE(bidiStream != nullptr);

  RefPtr<WebTransportStreamBase> unidiStream;
  auto callback1 =
      [&](Result<RefPtr<WebTransportStreamBase>, nsresult>&& aResult) {
        if (aResult.isErr()) {
          return;
        }
        unidiStream = aResult.unwrap();
      };
  client->Session()->CreateOutgoingUnidirectionalStream(std::move(callback1));
  ASSERT_TRUE(unidiStream == nullptr);

  ServerProcessCapsules(server, client);

  received = server->GetReceivedCapsules();
  ASSERT_EQ(received.Length(), 1u);

  WebTransportStreamsBlockedCapsule& streamsBlocked1 =
      received[0].GetWebTransportStreamsBlockedCapsule();
  ASSERT_EQ(streamsBlocked1.mLimit, 0u);
  ASSERT_EQ(streamsBlocked1.mBidi, false);

  server->SendWebTransportMaxStreamsCapsule(1, false);
  ClientProcessCapsules(server, client);
  ASSERT_TRUE(unidiStream != nullptr);

  client->Done();
  server->Done();
}

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

static void ValidateData(nsIInputStream* aStream,
                         nsTArray<uint8_t>& aExpectedData) {
  nsTArray<uint8_t> outputData;
  nsresult rv = NS_ConsumeStream(aStream, UINT32_MAX, outputData);
  ASSERT_NS_SUCCEEDED(rv);
  ValidateData(outputData, aExpectedData);
}

static std::pair<nsCOMPtr<nsIAsyncOutputStream>, nsCOMPtr<nsIAsyncInputStream>>
CreateStreamAndSendData(WebTransportStreamBase* aStream,
                        const nsTArray<uint8_t>& aData) {
  nsCOMPtr<nsIAsyncOutputStream> writer;
  nsCOMPtr<nsIAsyncInputStream> reader;
  aStream->GetWriterAndReader(getter_AddRefs(writer), getter_AddRefs(reader));

  uint32_t numWritten = 0;
  (void)writer->Write((const char*)aData.Elements(), aData.Length(),
                      &numWritten);
  NS_ProcessPendingEvents(nullptr);
  return std::make_pair(writer, reader);
}

static void ValidateStreamCapsule(MockWebTransportServer* aServer,
                                  nsTArray<uint8_t>& aExpectedData,
                                  bool aExpectBidi) {
  nsTArray<Capsule> received = aServer->GetReceivedCapsules();
  ASSERT_EQ(received.Length(), 1u);

  WebTransportStreamDataCapsule& streamData =
      received[0].GetWebTransportStreamDataCapsule();
  StreamId id(streamData.mID);
  ASSERT_TRUE(id.IsClientInitiated());
  ASSERT_EQ(id.IsBiDi(), aExpectBidi);
  ValidateData(streamData.mData, aExpectedData);
}

static already_AddRefed<WebTransportStreamBase> CreateOutgoingStream(
    MockWebTransportClient* aClient, bool aBidi = true) {
  RefPtr<WebTransportStreamBase> stream;
  auto callback =
      [&](Result<RefPtr<WebTransportStreamBase>, nsresult>&& aResult) {
        if (aResult.isErr()) {
          return;
        }
        stream = aResult.unwrap();
        MOZ_RELEASE_ASSERT(stream);
      };
  if (aBidi) {
    aClient->Session()->CreateOutgoingBidirectionalStream(std::move(callback));
  } else {
    aClient->Session()->CreateOutgoingUnidirectionalStream(std::move(callback));
  }
  return stream.forget();
}

TEST(TestHttp2WebTransport, OutgoingUniStream)
{
  Http2WebTransportInitialSettings settings;
  settings.mInitialMaxData = 1024;
  settings.mInitialMaxStreamsUni = 1;
  settings.mInitialMaxStreamDataUni = 512;
  RefPtr<MockWebTransportClient> client = new MockWebTransportClient(settings);
  RefPtr<MockWebTransportServer> server = new MockWebTransportServer();

  // Try to create bidi stream (should fail and trigger streams-blocked capsule)
  RefPtr<WebTransportStreamBase> bidiStream;
  client->Session()->CreateOutgoingBidirectionalStream([&](auto&& aResult) {
    if (aResult.isOk()) {
      bidiStream = aResult.unwrap();
    }
  });
  ASSERT_TRUE(bidiStream == nullptr);

  ServerProcessCapsules(server, client);

  {
    nsTArray<Capsule> received = server->GetReceivedCapsules();
    ASSERT_EQ(received.Length(), 1u);
    auto& capsule = received[0].GetWebTransportStreamsBlockedCapsule();
    ASSERT_EQ(capsule.mLimit, 0u);
    ASSERT_TRUE(capsule.mBidi);
  }

  // Create unidirectional stream and send data
  RefPtr<WebTransportStreamBase> unidiStream =
      CreateOutgoingStream(client, false);
  ASSERT_TRUE(unidiStream != nullptr);

  nsTArray<uint8_t> inputData;
  CreateTestData(512, inputData);
  CreateStreamAndSendData(unidiStream, inputData);

  ServerProcessCapsules(server, client);
  ValidateStreamCapsule(server, inputData, /* aExpectBidi = */ false);

  client->Done();
  server->Done();
}

TEST(TestHttp2WebTransport, OutgoingBidiStream)
{
  Http2WebTransportInitialSettings settings;
  settings.mInitialMaxData = 1024;
  settings.mInitialMaxStreamsUni = 1;
  settings.mInitialMaxStreamsBidi = 1;
  settings.mInitialMaxStreamDataBidi = 512;
  RefPtr<MockWebTransportClient> client = new MockWebTransportClient(settings);
  RefPtr<MockWebTransportServer> server = new MockWebTransportServer();

  RefPtr<WebTransportStreamBase> bidiStream = CreateOutgoingStream(client);
  ASSERT_TRUE(bidiStream != nullptr);

  nsTArray<uint8_t> inputData;
  CreateTestData(512, inputData);
  CreateStreamAndSendData(bidiStream, inputData);

  ServerProcessCapsules(server, client);
  ValidateStreamCapsule(server, inputData, /* aExpectBidi = */ true);

  // Echo back from server
  nsTArray<uint8_t> echo;
  echo.AppendElements(inputData.Elements(), inputData.Length());
  StreamId id = StreamId::From(0);
  server->SendWebTransportStreamDataCapsule(id, false, std::move(echo));
  ClientProcessCapsules(server, client);

  nsCOMPtr<nsIAsyncInputStream> reader;
  nsCOMPtr<nsIAsyncOutputStream> writer;
  bidiStream->GetWriterAndReader(getter_AddRefs(writer),
                                 getter_AddRefs(reader));
  uint64_t available = 0;
  (void)reader->Available(&available);
  EXPECT_EQ(available, inputData.Length());

  ValidateData(reader, inputData);

  client->Done();
  server->Done();
}

TEST(TestHttp2WebTransport, IncomingBidiStream)
{
  Http2WebTransportInitialSettings settings;
  settings.mInitialLocalMaxStreamsBidi = 1;
  settings.mInitialLocalMaxStreamDataBidi = 512;
  RefPtr<MockWebTransportClient> client = new MockWebTransportClient(settings);
  RefPtr<MockWebTransportSessionEventListener> listener =
      new MockWebTransportSessionEventListener();
  client->Session()->SetWebTransportSessionEventListener(listener);

  RefPtr<MockWebTransportServer> server = new MockWebTransportServer();

  nsTArray<uint8_t> inputData;
  CreateTestData(512, inputData);
  nsTArray<uint8_t> cloned(inputData.Clone());

  server->SendWebTransportStreamDataCapsule(1, false, std::move(cloned));

  ClientProcessCapsules(server, client);

  nsTArray<RefPtr<WebTransportStreamBase>> streams =
      listener->TakeIncomingStreams();
  ASSERT_EQ(streams.Length(), 1u);

  nsCOMPtr<nsIAsyncOutputStream> writer;
  nsCOMPtr<nsIAsyncInputStream> reader;
  RefPtr<WebTransportStreamBase> stream = streams[0];
  stream->GetWriterAndReader(getter_AddRefs(writer), getter_AddRefs(reader));

  ValidateData(reader, inputData);

  cloned = inputData.Clone();
  server->SendWebTransportStreamDataCapsule(5, false, std::move(cloned));

  ClientProcessCapsules(server, client);
  streams = listener->TakeIncomingStreams();
  ASSERT_EQ(streams.Length(), 0u);

  client->Session()->OnStreamClosed(
      static_cast<Http2WebTransportStream*>(stream.get()));

  ServerProcessCapsules(server, client);

  cloned = inputData.Clone();
  server->SendWebTransportStreamDataCapsule(5, false, std::move(cloned));

  ClientProcessCapsules(server, client);
  streams = listener->TakeIncomingStreams();
  ASSERT_EQ(streams.Length(), 1u);

  client->Done();
  server->Done();
}

TEST(TestHttp2WebTransport, StreamDataSenderFlowControl)
{
  Http2WebTransportInitialSettings settings;
  settings.mInitialMaxData = 1024;
  settings.mInitialMaxStreamsBidi = 1;
  settings.mInitialMaxStreamDataBidi = 100;
  RefPtr<MockWebTransportClient> client = new MockWebTransportClient(settings);
  RefPtr<MockWebTransportServer> server = new MockWebTransportServer();

  RefPtr<WebTransportStreamBase> bidiStream = CreateOutgoingStream(client);
  ASSERT_TRUE(bidiStream != nullptr);

  nsCOMPtr<nsIAsyncOutputStream> writer;
  nsCOMPtr<nsIAsyncInputStream> reader;
  bidiStream->GetWriterAndReader(getter_AddRefs(writer),
                                 getter_AddRefs(reader));

  nsTArray<uint8_t> inputData;
  CreateTestData(100, inputData);
  uint32_t numWritten = 0;
  (void)writer->Write((const char*)inputData.Elements(), inputData.Length(),
                      &numWritten);

  NS_ProcessPendingEvents(nullptr);

  ServerProcessCapsules(server, client);

  nsTArray<Capsule> received = server->GetReceivedCapsules();
  ASSERT_EQ(received.Length(), 1u);

  WebTransportStreamDataCapsule& streamData =
      received[0].GetWebTransportStreamDataCapsule();

  StreamId id(streamData.mID);
  ASSERT_TRUE(id.IsClientInitiated());
  ASSERT_TRUE(id.IsBiDi());
  ValidateData(streamData.mData, inputData);

  numWritten = 0;
  (void)writer->Write((const char*)inputData.Elements(), inputData.Length(),
                      &numWritten);

  NS_ProcessPendingEvents(nullptr);
  ServerProcessCapsules(server, client);

  received = server->GetReceivedCapsules();
  ASSERT_EQ(received.Length(), 1u);

  WebTransportStreamDataBlockedCapsule& blocked =
      received[0].GetWebTransportStreamDataBlockedCapsule();
  StreamId receivedId(blocked.mID);
  ASSERT_EQ(id, receivedId);
  ASSERT_EQ(blocked.mLimit, 100u);

  server->SendWebTransportMaxStreamDataCapsule(300, id);
  ClientProcessCapsules(server, client);

  (void)writer->Write((const char*)inputData.Elements(), inputData.Length(),
                      &numWritten);

  NS_ProcessPendingEvents(nullptr);
  ServerProcessCapsules(server, client);

  received = server->GetReceivedCapsules();
  ASSERT_EQ(received.Length(), 1u);

  WebTransportStreamDataCapsule& streamData1 =
      received[0].GetWebTransportStreamDataCapsule();
  ASSERT_EQ(streamData1.mData.Length(), 200u);

  client->Done();
  server->Done();
}

TEST(TestHttp2WebTransport, StreamDataSenderFlowControlMaxData)
{
  Http2WebTransportInitialSettings settings;
  settings.mInitialMaxData = 100;
  settings.mInitialMaxStreamsBidi = 1;
  settings.mInitialMaxStreamDataBidi = 100;
  RefPtr<MockWebTransportClient> client = new MockWebTransportClient(settings);
  RefPtr<MockWebTransportServer> server = new MockWebTransportServer();

  RefPtr<WebTransportStreamBase> bidiStream = CreateOutgoingStream(client);
  ASSERT_TRUE(bidiStream != nullptr);

  nsCOMPtr<nsIAsyncOutputStream> writer;
  nsCOMPtr<nsIAsyncInputStream> reader;
  bidiStream->GetWriterAndReader(getter_AddRefs(writer),
                                 getter_AddRefs(reader));

  nsTArray<uint8_t> inputData;
  CreateTestData(100, inputData);
  uint32_t numWritten = 0;
  (void)writer->Write((const char*)inputData.Elements(), inputData.Length(),
                      &numWritten);

  NS_ProcessPendingEvents(nullptr);

  ServerProcessCapsules(server, client);

  nsTArray<Capsule> received = server->GetReceivedCapsules();
  ASSERT_EQ(received.Length(), 1u);

  WebTransportStreamDataCapsule& streamData =
      received[0].GetWebTransportStreamDataCapsule();

  StreamId id(streamData.mID);
  ASSERT_TRUE(id.IsClientInitiated());
  ASSERT_TRUE(id.IsBiDi());
  ValidateData(streamData.mData, inputData);

  numWritten = 0;
  (void)writer->Write((const char*)inputData.Elements(), inputData.Length(),
                      &numWritten);

  NS_ProcessPendingEvents(nullptr);
  ServerProcessCapsules(server, client);

  received = server->GetReceivedCapsules();
  ASSERT_EQ(received.Length(), 2u);

  WebTransportDataBlockedCapsule& sessionDataBlocked =
      received[0].GetWebTransportDataBlockedCapsule();
  ASSERT_EQ(sessionDataBlocked.mLimit, 100u);

  WebTransportStreamDataBlockedCapsule& blocked =
      received[1].GetWebTransportStreamDataBlockedCapsule();
  StreamId receivedId(blocked.mID);
  ASSERT_EQ(id, receivedId);
  ASSERT_EQ(blocked.mLimit, 100u);

  server->SendWebTransportMaxStreamDataCapsule(500, id);
  ClientProcessCapsules(server, client);

  (void)writer->Write((const char*)inputData.Elements(), inputData.Length(),
                      &numWritten);

  NS_ProcessPendingEvents(nullptr);
  ServerProcessCapsules(server, client);

  received = server->GetReceivedCapsules();
  ASSERT_EQ(received.Length(), 0u);

  server->SendWebTransportMaxDataCapsule(1024);
  ClientProcessCapsules(server, client);

  (void)writer->Write((const char*)inputData.Elements(), inputData.Length(),
                      &numWritten);

  NS_ProcessPendingEvents(nullptr);
  ServerProcessCapsules(server, client);

  received = server->GetReceivedCapsules();
  ASSERT_EQ(received.Length(), 1u);

  WebTransportStreamDataCapsule& streamData1 =
      received[0].GetWebTransportStreamDataCapsule();
  ASSERT_EQ(streamData1.mData.Length(), 300u);

  client->Done();
  server->Done();
}

static void CheckFc(ReceiverFlowControlBase& aFc, uint64_t aConsumed,
                    uint64_t aRetired) {
  MOZ_RELEASE_ASSERT(aFc.Consumed() == aConsumed);
  MOZ_RELEASE_ASSERT(aFc.Retired() == aRetired);
}

TEST(TestHttp2WebTransport, ReceiverFlowControl)
{
  const uint32_t FC_SIZE = 1024;
  Http2WebTransportInitialSettings settings;
  settings.mInitialMaxStreamsBidi = 2;
  settings.mInitialLocalMaxStreamDataBidi = FC_SIZE * 3 / 4;
  settings.mInitialLocalMaxData = FC_SIZE;
  RefPtr<MockWebTransportClient> client = new MockWebTransportClient(settings);
  RefPtr<MockWebTransportServer> server = new MockWebTransportServer();

  RefPtr<WebTransportStreamBase> s1 = CreateOutgoingStream(client);
  ASSERT_TRUE(s1 != nullptr);

  RefPtr<WebTransportStreamBase> s2 = CreateOutgoingStream(client);
  ASSERT_TRUE(s2 != nullptr);

  CheckFc(client->Session()->ReceiverFc(), 0, 0);
  CheckFc(*s1->ReceiverFc(), 0, 0);
  CheckFc(*s2->ReceiverFc(), 0, 0);

  nsTArray<uint8_t> inputData;
  CreateTestData(FC_SIZE / 4, inputData);

  StreamId id = StreamId::From(0);
  server->SendWebTransportStreamDataCapsule(id, false, std::move(inputData));

  CreateTestData(FC_SIZE / 4, inputData);
  StreamId id1 = StreamId::From(4);
  server->SendWebTransportStreamDataCapsule(id1, false, std::move(inputData));

  ClientProcessCapsules(server, client);

  CheckFc(client->Session()->ReceiverFc(), FC_SIZE / 2, FC_SIZE / 2);
  CheckFc(*s1->ReceiverFc(), FC_SIZE / 4, FC_SIZE / 4);
  CheckFc(*s2->ReceiverFc(), FC_SIZE / 4, FC_SIZE / 4);

  CreateTestData(FC_SIZE / 4, inputData);
  server->SendWebTransportStreamDataCapsule(id, false, std::move(inputData));

  ClientProcessCapsules(server, client);

  CheckFc(client->Session()->ReceiverFc(), FC_SIZE * 3 / 4, FC_SIZE * 3 / 4);
  CheckFc(*s1->ReceiverFc(), FC_SIZE / 2, FC_SIZE / 2);
  CheckFc(*s2->ReceiverFc(), FC_SIZE / 4, FC_SIZE / 4);

  client->Done();
  server->Done();
}

TEST(TestHttp2WebTransport, ReceiverFlowControl1)
{
  const uint32_t FC_SIZE = 1024;
  Http2WebTransportInitialSettings settings;
  settings.mInitialMaxStreamsBidi = 1;
  settings.mInitialLocalMaxStreamDataBidi = FC_SIZE / 2;
  settings.mInitialLocalMaxData = FC_SIZE;
  RefPtr<MockWebTransportClient> client = new MockWebTransportClient(settings);
  RefPtr<MockWebTransportServer> server = new MockWebTransportServer();

  RefPtr<WebTransportStreamBase> bidiStream = CreateOutgoingStream(client);
  ASSERT_TRUE(bidiStream != nullptr);

  nsCOMPtr<nsIAsyncOutputStream> writer;
  nsCOMPtr<nsIAsyncInputStream> reader;
  bidiStream->GetWriterAndReader(getter_AddRefs(writer),
                                 getter_AddRefs(reader));

  nsTArray<uint8_t> inputData;
  CreateTestData(FC_SIZE / 4, inputData);

  StreamId id = StreamId::From(0);
  server->SendWebTransportStreamDataCapsule(id, false, std::move(inputData));
  ClientProcessCapsules(server, client);

  uint64_t available = 0;
  (void)reader->Available(&available);
  EXPECT_EQ(available, FC_SIZE / 4);

  nsTArray<uint8_t> outputData;
  nsresult rv = NS_ConsumeStream(reader, UINT32_MAX, outputData);
  ASSERT_NS_SUCCEEDED(rv);

  CheckFc(client->Session()->ReceiverFc(), FC_SIZE / 4, FC_SIZE / 4);
  CheckFc(*bidiStream->ReceiverFc(), FC_SIZE / 4, FC_SIZE / 4);

  CreateTestData(1, inputData);
  server->SendWebTransportStreamDataCapsule(id, false, std::move(inputData));
  ClientProcessCapsules(server, client);

  CheckFc(client->Session()->ReceiverFc(), FC_SIZE / 4 + 1, FC_SIZE / 4 + 1);
  CheckFc(*bidiStream->ReceiverFc(), FC_SIZE / 4 + 1, FC_SIZE / 4 + 1);

  available = 0;
  (void)reader->Available(&available);
  EXPECT_EQ(available, 1u);

  ServerProcessCapsules(server, client);

  nsTArray<Capsule> received = server->GetReceivedCapsules();
  ASSERT_EQ(received.Length(), 1u);

  WebTransportMaxStreamDataCapsule& capsule =
      received[0].GetWebTransportMaxStreamDataCapsule();
  ASSERT_EQ(capsule.mID, 0u);
  ASSERT_EQ(capsule.mLimit, FC_SIZE * 3 / 4 + 1);

  CreateTestData(FC_SIZE / 4 - 1, inputData);
  server->SendWebTransportStreamDataCapsule(id, false, std::move(inputData));
  ClientProcessCapsules(server, client);

  CheckFc(client->Session()->ReceiverFc(), FC_SIZE / 2, FC_SIZE / 2);
  CheckFc(*bidiStream->ReceiverFc(), FC_SIZE / 2, FC_SIZE / 2);

  CreateTestData(1, inputData);
  server->SendWebTransportStreamDataCapsule(id, false, std::move(inputData));
  ClientProcessCapsules(server, client);

  CheckFc(client->Session()->ReceiverFc(), FC_SIZE / 2 + 1, FC_SIZE / 2 + 1);
  CheckFc(*bidiStream->ReceiverFc(), FC_SIZE / 2 + 1, FC_SIZE / 2 + 1);

  ServerProcessCapsules(server, client);
  received = server->GetReceivedCapsules();
  ASSERT_EQ(received.Length(), 1u);

  WebTransportMaxDataCapsule& maxData =
      received[0].GetWebTransportMaxDataCapsule();
  ASSERT_EQ(maxData.mMaxDataSize, FC_SIZE * 3 / 2 + 1);

  client->Done();
  server->Done();
}

TEST(TestHttp2WebTransport, StreamStopSending)
{
  Http2WebTransportInitialSettings settings;
  settings.mInitialMaxStreamsUni = 1;
  RefPtr<MockWebTransportClient> client = new MockWebTransportClient(settings);
  RefPtr<MockWebTransportServer> server = new MockWebTransportServer();

  RefPtr<WebTransportStreamBase> uniStream =
      CreateOutgoingStream(client, false);
  ASSERT_TRUE(uniStream != nullptr);

  uniStream->SendStopSending(0);
  ServerProcessCapsules(server, client);

  nsTArray<Capsule> received = server->GetReceivedCapsules();
  ASSERT_EQ(received.Length(), 1u);

  WebTransportStopSendingCapsule& stopSending =
      received[0].GetWebTransportStopSendingCapsule();
  ASSERT_EQ(stopSending.mID, uniStream->WebTransportStreamId());
  ASSERT_EQ(stopSending.mErrorCode, 0u);

  client->Done();
  server->Done();
}

TEST(TestHttp2WebTransport, StreamOnStopSending)
{
  Http2WebTransportInitialSettings settings;
  settings.mInitialMaxStreamsUni = 1;
  RefPtr<MockWebTransportClient> client = new MockWebTransportClient(settings);
  RefPtr<MockWebTransportSessionEventListener> listener =
      new MockWebTransportSessionEventListener();
  client->Session()->SetWebTransportSessionEventListener(listener);
  RefPtr<MockWebTransportServer> server = new MockWebTransportServer();

  RefPtr<WebTransportStreamBase> uniStream =
      CreateOutgoingStream(client, false);
  ASSERT_TRUE(uniStream != nullptr);

  nsTArray<uint8_t> inputData;
  CreateTestData(512, inputData);
  CreateStreamAndSendData(uniStream, inputData);

  ServerProcessCapsules(server, client);

  // Above 255, so a narrowing anywhere on the way would show.
  const uint64_t kErrorCode = 0x12345;
  server->SendWebTransportStopSendingCapsule(kErrorCode,
                                             uniStream->WebTransportStreamId());
  ClientProcessCapsules(server, client);

  auto stopSending = listener->TakeStopSending();
  ASSERT_TRUE(stopSending);
  ASSERT_EQ(stopSending->first, uniStream->WebTransportStreamId());
  ASSERT_EQ(stopSending->second, kErrorCode);

  client->Done();
  server->Done();
}

TEST(TestHttp2WebTransport, StreamReset)
{
  const uint32_t TOTAL_SIZE = 1024;
  Http2WebTransportInitialSettings settings;
  settings.mInitialMaxStreamsBidi = 1;
  settings.mInitialMaxStreamDataBidi = TOTAL_SIZE;
  settings.mInitialMaxData = TOTAL_SIZE;
  RefPtr<MockWebTransportClient> client = new MockWebTransportClient(settings);
  RefPtr<MockWebTransportSessionEventListener> listener =
      new MockWebTransportSessionEventListener();
  client->Session()->SetWebTransportSessionEventListener(listener);
  RefPtr<MockWebTransportServer> server = new MockWebTransportServer();

  RefPtr<WebTransportStreamBase> stream = CreateOutgoingStream(client);
  ASSERT_TRUE(stream != nullptr);

  nsTArray<uint8_t> inputData;
  CreateTestData(TOTAL_SIZE / 4, inputData);
  CreateStreamAndSendData(stream, inputData);

  ServerProcessCapsules(server, client);

  inputData.Clear();
  CreateTestData(TOTAL_SIZE / 4, inputData);
  CreateStreamAndSendData(stream, inputData);

  ServerProcessCapsules(server, client);

  nsTArray<Capsule> received = server->GetReceivedCapsules();
  ASSERT_EQ(received.Length(), 2u);

  stream->Reset(0);

  ServerProcessCapsules(server, client);
  received = server->GetReceivedCapsules();
  ASSERT_EQ(received.Length(), 1u);

  WebTransportResetStreamCapsule& reset =
      received[0].GetWebTransportResetStreamCapsule();
  ASSERT_EQ(reset.mID, stream->WebTransportStreamId());
  ASSERT_EQ(reset.mErrorCode, 0u);
  ASSERT_EQ(reset.mReliableSize, TOTAL_SIZE / 2);

  client->Done();
  server->Done();
}

TEST(TestHttp2WebTransport, StreamResetReliableSize)
{
  const uint32_t TOTAL_SIZE = 1024;
  Http2WebTransportInitialSettings settings;
  settings.mInitialMaxStreamsBidi = 1;
  settings.mInitialMaxStreamDataBidi = TOTAL_SIZE;
  settings.mInitialMaxData = TOTAL_SIZE;
  RefPtr<MockWebTransportClient> client = new MockWebTransportClient(settings);
  RefPtr<MockWebTransportSessionEventListener> listener =
      new MockWebTransportSessionEventListener();
  client->Session()->SetWebTransportSessionEventListener(listener);
  RefPtr<MockWebTransportServer> server = new MockWebTransportServer();

  RefPtr<WebTransportStreamBase> stream = CreateOutgoingStream(client);
  ASSERT_TRUE(stream != nullptr);

  nsTArray<uint8_t> inputData;
  CreateTestData(TOTAL_SIZE / 4, inputData);
  auto streams = CreateStreamAndSendData(stream, inputData);

  ServerProcessCapsules(server, client);

  server->SendWebTransportStreamDataCapsule(stream->WebTransportStreamId(),
                                            false, std::move(inputData));
  CreateTestData(TOTAL_SIZE / 4, inputData);
  server->SendWebTransportStreamDataCapsule(stream->WebTransportStreamId(),
                                            false, std::move(inputData));

  const uint64_t kErrorCode = 0x12345;
  server->SendWebTransportResetStreamCapsule(kErrorCode, TOTAL_SIZE / 2,
                                             stream->WebTransportStreamId());
  ClientProcessCapsules(server, client);

  auto reset = listener->TakeReset();
  ASSERT_TRUE(reset);
  ASSERT_EQ(reset->first, stream->WebTransportStreamId());
  ASSERT_EQ(reset->second, kErrorCode);

  client->Done();
  server->Done();
}

TEST(TestHttp2WebTransport, SendHalfStaysWritableAfterPeerReset)
{
  const uint32_t TOTAL_SIZE = 1024;
  Http2WebTransportInitialSettings settings;
  settings.mInitialMaxStreamsBidi = 1;
  settings.mInitialMaxStreamDataBidi = TOTAL_SIZE;
  settings.mInitialMaxData = TOTAL_SIZE;
  RefPtr<MockWebTransportClient> client = new MockWebTransportClient(settings);
  RefPtr<MockWebTransportSessionEventListener> listener =
      new MockWebTransportSessionEventListener();
  client->Session()->SetWebTransportSessionEventListener(listener);
  RefPtr<MockWebTransportServer> server = new MockWebTransportServer();

  RefPtr<WebTransportStreamBase> stream = CreateOutgoingStream(client);
  ASSERT_TRUE(stream != nullptr);

  nsTArray<uint8_t> inputData;
  CreateTestData(64, inputData);
  auto streams = CreateStreamAndSendData(stream, inputData);
  ServerProcessCapsules(server, client);

  server->SendWebTransportResetStreamCapsule(0x12345, 0,
                                             stream->WebTransportStreamId());
  ClientProcessCapsules(server, client);
  ASSERT_TRUE(listener->TakeReset());

  nsTArray<uint8_t> moreData;
  CreateTestData(64, moreData);
  nsTArray<uint8_t> expectedData;
  expectedData.AppendElements(moreData);
  uint32_t numWritten = 0;
  (void)streams.first->Write((const char*)moreData.Elements(),
                             moreData.Length(), &numWritten);
  NS_ProcessPendingEvents(nullptr);
  ServerProcessCapsules(server, client);

  nsTArray<Capsule> received = server->GetReceivedCapsules();
  ASSERT_EQ(received.Length(), 2u);
  ValidateData(received[1].GetWebTransportStreamDataCapsule().mData,
               expectedData);

  client->Done();
  server->Done();
}

TEST(TestHttp2WebTransport, SendAndReceiveDatagram)
{
  RefPtr<MockWebTransportClient> client =
      new MockWebTransportClient(Http2WebTransportInitialSettings());
  RefPtr<MockWebTransportServer> server = new MockWebTransportServer();
  RefPtr<MockWebTransportSessionEventListener> listener =
      new MockWebTransportSessionEventListener();
  client->Session()->SetWebTransportSessionEventListener(listener);

  nsTArray<uint8_t> mockData, expectedData;
  CreateTestData(512, mockData);
  expectedData.AppendElements(mockData);

  // Send datagram from client to server
  client->Session()->SendDatagram(std::move(mockData), 1, 0, 0,
                                  TimeStamp::Now());
  ServerProcessCapsules(server, client);

  // Verify the server received the correct datagram capsule
  nsTArray<Capsule> received = server->GetReceivedCapsules();
  WebTransportDatagramCapsule& parsedCapsule =
      received[0].GetWebTransportDatagramCapsule();
  ValidateData(parsedCapsule.mPayload, expectedData);

  // Echo and verify the received datagram payload back to the client
  server->SendDatagramCapsule(std::move(parsedCapsule.mPayload));
  ClientProcessCapsules(server, client);
  ValidateData(listener->mReceivedDatagrams, expectedData);

  client->Done();
  server->Done();
}
