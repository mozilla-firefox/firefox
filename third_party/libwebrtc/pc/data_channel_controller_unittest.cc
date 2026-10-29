/*
 *  Copyright 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/data_channel_controller.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "api/data_channel_event_observer_interface.h"
#include "api/data_channel_interface.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/priority.h"
#include "api/rtc_error.h"
#include "api/rtp_receiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/transport/data_channel_transport_interface.h"
#include "api/units/timestamp.h"
#include "pc/peer_connection_internal.h"
#include "pc/sctp_data_channel.h"
#include "pc/sctp_utils.h"
#include "pc/test/mock_peer_connection_internal.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/fake_clock.h"
#include "rtc_base/null_socket_server.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "rtc_base/thread.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"

namespace webrtc {

namespace {

using Message = DataChannelEventObserverInterface::Message;
using ::testing::_;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::IsEmpty;
using ::testing::Lt;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnPointee;
using ::testing::SizeIs;

constexpr uint8_t kSomeData[] = {5, 4, 3, 2, 1};

class MockDataChannelTransport : public DataChannelTransportInterface {
 public:
  ~MockDataChannelTransport() override {}

  MOCK_METHOD(RTCError,
              OpenChannel,
              (int channel_id, PriorityValue priority),
              (override));
  MOCK_METHOD(RTCError,
              SendData,
              (int channel_id,
               const SendDataParams& params,
               const CopyOnWriteBuffer& buffer),
              (override));
  MOCK_METHOD(RTCError, CloseChannel, (int channel_id), (override));
  MOCK_METHOD(void, SetDataSink, (DataChannelSink * sink), (override));
  MOCK_METHOD(bool, IsReadyToSend, (), (const, override));
  MOCK_METHOD(size_t, buffered_amount, (int channel_id), (const, override));
  MOCK_METHOD(size_t,
              buffered_amount_low_threshold,
              (int channel_id),
              (const, override));
  MOCK_METHOD(void,
              SetBufferedAmountLowThreshold,
              (int channel_id, size_t bytes),
              (override));
  MOCK_METHOD(std::optional<int>, MaxChannels, (), (override));
  MOCK_METHOD(std::optional<SSLRole>, DtlsRole, (), (override));
};

class MockDataChannelEventObserver : public DataChannelEventObserverInterface {
 public:
  MOCK_METHOD(void,
              OnMessage,
              (const DataChannelEventObserverInterface::Message& message),
              (override));
};

// Convenience class for tests to ensure that shutdown methods for DCC
// are consistently called. In practice SdpOfferAnswerHandler will call
// TeardownDataChannelTransport_n on the network thread when destroying the
// data channel transport and PeerConnection calls PrepareForShutdown() from
// within PeerConnection::Close(). The DataChannelControllerForTest class mimics
// behavior by calling those methods from within its destructor.
class DataChannelControllerForTest : public DataChannelController {
 public:
  explicit DataChannelControllerForTest(
      PeerConnectionInternal* pc,
      DataChannelTransportInterface* transport = nullptr)
      : DataChannelController(pc) {
    if (transport) {
      network_thread()->BlockingCall(
          [&] { SetupDataChannelTransport_n(transport); });
    }
  }

  ~DataChannelControllerForTest() override {
    network_thread()->BlockingCall(
        [&] { TeardownDataChannelTransport_n(RTCError::OK()); });
    PrepareForShutdown();
  }
};

class DataChannelControllerTest : public ::testing::Test {
 protected:
  DataChannelControllerTest()
      : network_thread_(std::make_unique<NullSocketServer>()) {
    network_thread_.Start();
    pc_ = make_ref_counted<NiceMock<MockPeerConnectionInternal>>();
    ON_CALL(*pc_, signaling_thread).WillByDefault(Return(Thread::Current()));
    ON_CALL(*pc_, network_thread).WillByDefault(Return(&network_thread_));
  }

  ~DataChannelControllerTest() override {
    run_loop_.Flush();
    network_thread_.Stop();
  }

  ScopedFakeClock clock_;
  test::RunLoop run_loop_;
  Thread network_thread_;
  scoped_refptr<NiceMock<MockPeerConnectionInternal>> pc_;
};

TEST_F(DataChannelControllerTest, CreateAndDestroy) {
  DataChannelControllerForTest dcc(pc_.get());
}

TEST_F(DataChannelControllerTest, CreateDataChannelEarlyRelease) {
  DataChannelControllerForTest dcc(pc_.get());
  auto ret = dcc.InternalCreateDataChannelWithProxy(
      "label", InternalDataChannelInit(DataChannelInit()));
  ASSERT_TRUE(ret.ok());
  auto channel = ret.MoveValue();
  // DCC still holds a reference to the channel. Release this reference early.
  channel = nullptr;
}

TEST_F(DataChannelControllerTest, CreateDataChannelEarlyClose) {
  DataChannelControllerForTest dcc(pc_.get());
  EXPECT_FALSE(dcc.HasDataChannels());
  EXPECT_FALSE(dcc.HasUsedDataChannels());
  auto ret = dcc.InternalCreateDataChannelWithProxy(
      "label", InternalDataChannelInit(DataChannelInit()));
  ASSERT_TRUE(ret.ok());
  auto channel = ret.MoveValue();
  EXPECT_TRUE(dcc.HasDataChannels());
  EXPECT_TRUE(dcc.HasUsedDataChannels());
  channel->Close();
  run_loop_.Flush();
  EXPECT_FALSE(dcc.HasDataChannels());
  EXPECT_TRUE(dcc.HasUsedDataChannels());
}

TEST_F(DataChannelControllerTest, CreateDataChannelLateRelease) {
  auto dcc = std::make_unique<DataChannelControllerForTest>(pc_.get());
  auto ret = dcc->InternalCreateDataChannelWithProxy(
      "label", InternalDataChannelInit(DataChannelInit()));
  ASSERT_TRUE(ret.ok());
  auto channel = ret.MoveValue();
  dcc.reset();
  channel = nullptr;
}

TEST_F(DataChannelControllerTest, CloseAfterControllerDestroyed) {
  auto dcc = std::make_unique<DataChannelControllerForTest>(pc_.get());
  auto ret = dcc->InternalCreateDataChannelWithProxy(
      "label", InternalDataChannelInit(DataChannelInit()));
  ASSERT_TRUE(ret.ok());
  auto channel = ret.MoveValue();
  dcc.reset();
  channel->Close();
}

// Allocate the maximum number of data channels and then one more.
// The last allocation should fail.
// This verifies that the transport can signal that channels are used up.
TEST_F(DataChannelControllerTest, MaxChannels) {
  const int kReducedMaxSctpStreams = 4;
  NiceMock<MockDataChannelTransport> transport;
  int channel_id = 0;

  EXPECT_CALL(transport, DtlsRole()).WillRepeatedly([&]() {
    return std::optional<SSLRole>((channel_id & 1) ? SSL_SERVER : SSL_CLIENT);
  });
  EXPECT_CALL(transport, OpenChannel(Lt(kReducedMaxSctpStreams), _))
      .WillRepeatedly(Return(RTCError::OK()));
  EXPECT_CALL(transport, OpenChannel(Ge(kReducedMaxSctpStreams), _))
      .WillRepeatedly(Return(RTCError(RTCErrorType::RESOURCE_EXHAUSTED,
                                      "Fake said ID too large")));
  DataChannelControllerForTest dcc(pc_.get(), &transport);

  // Allocate the maximum number of channels + 1. Inside the loop, the creation
  // process will allocate a stream id for each channel.
  for (channel_id = 0; channel_id <= kReducedMaxSctpStreams; ++channel_id) {
    auto ret = dcc.InternalCreateDataChannelWithProxy(
        "label", InternalDataChannelInit(DataChannelInit()));
    if (channel_id == kReducedMaxSctpStreams) {
      // We've reached the maximum and the previous call should have failed.
      EXPECT_FALSE(ret.ok());
    } else {
      // We're still working on saturating the pool. Things should be working.
      EXPECT_TRUE(ret.ok());
    }
  }
}

TEST_F(DataChannelControllerTest, RespectTransportFailureOnOpenChannel) {
  NiceMock<MockDataChannelTransport> transport;
  int channel_id = 0;

  ON_CALL(transport, DtlsRole).WillByDefault([&]() {
    return std::optional<SSLRole>((channel_id & 1) ? SSL_SERVER : SSL_CLIENT);
  });
  EXPECT_CALL(transport, OpenChannel(_, _))
      .WillOnce(Return(RTCError::InvalidParameter()));
  DataChannelControllerForTest dcc(pc_.get(), &transport);

  auto ret = dcc.InternalCreateDataChannelWithProxy(
      "label", InternalDataChannelInit(DataChannelInit()));
  EXPECT_FALSE(ret.ok());
}

// This scenario tests the case where the max message size is too small
// for a DCEP open message. The expectation is that the channels
// get closed when the DCEP message gets sent, and that the program
// does not crash.
TEST_F(DataChannelControllerTest, DcepFailureOnTooSmallMaxMessageSize) {
  NiceMock<MockDataChannelTransport> transport;
  // Reject all SendData with "message too large"
  EXPECT_CALL(transport, SendData(_, _, _))
      .Times(2)
      .WillRepeatedly(
          Return(RTCError(RTCErrorType::INVALID_RANGE, "Message too large")));
  bool ready_to_send = false;
  EXPECT_CALL(transport, IsReadyToSend())
      .WillRepeatedly(ReturnPointee(&ready_to_send));
  DataChannelControllerForTest dcc(pc_.get(), &transport);
  auto ch1 = dcc.InternalCreateDataChannelWithProxy(
      "ch1", InternalDataChannelInit(DataChannelInit()));
  auto ch2 = dcc.InternalCreateDataChannelWithProxy(
      "ch1", InternalDataChannelInit(DataChannelInit()));
  ready_to_send = true;
  pc_->network_thread()->BlockingCall(
      [&] { dcc.AllocateSctpSids(SSL_CLIENT); });
  // Let callbacks to the signaling thread run to completion.
  run_loop_.Flush();
  EXPECT_THAT(ch1.value()->state(), Eq(DataChannelInterface::kClosed));
  EXPECT_THAT(ch2.value()->state(), Eq(DataChannelInterface::kClosed));
}

// This test reproduces the UAF reported in b/503422316.
// It creates a data channel, drops the external reference, and then triggers
// AllocateSctpSids. AllocateSctpSids calls OnTransportReady, which fails
// synchronously, causing the channel to close and be deleted while
// AllocateSctpSids (and SctpDataChannel::UpdateState) are still on the stack.
TEST_F(DataChannelControllerTest, AllocateSctpSidsUafRepro) {
  NiceMock<MockDataChannelTransport> transport;
  // Reject all SendData with "message too large"
  EXPECT_CALL(transport, SendData(_, _, _))
      .WillRepeatedly(
          Return(RTCError(RTCErrorType::INVALID_RANGE, "Message too large")));
  bool ready_to_send = false;
  EXPECT_CALL(transport, IsReadyToSend())
      .WillRepeatedly(ReturnPointee(&ready_to_send));
  EXPECT_CALL(transport, DtlsRole())
      .WillOnce(Return(std::nullopt))
      .WillRepeatedly(Return(SSL_CLIENT));
  ON_CALL(transport, MaxChannels).WillByDefault(Return(100));

  DataChannelControllerForTest dcc(pc_.get(), &transport);
  auto ret = dcc.InternalCreateDataChannelWithProxy(
      "ch1", InternalDataChannelInit(DataChannelInit()));
  ASSERT_TRUE(ret.ok());

  // Drop the reference.
  ret.MoveValue();

  ready_to_send = true;
  pc_->network_thread()->BlockingCall([&] { dcc.OnTransportConnected(); });

  run_loop_.Flush();
}

// Closes `channel_to_close` from within the first `OnStateChange()` callback.
// Callbacks are delivered directly on the network thread.
class CloseOnStateChangeObserver : public DataChannelObserver {
 public:
  explicit CloseOnStateChangeObserver(DataChannelInterface* channel_to_close)
      : channel_to_close_(channel_to_close) {}

  void OnStateChange() override {
    if (channel_to_close_ != nullptr) {
      std::exchange(channel_to_close_, nullptr)->Close();
    }
  }
  void OnMessage(const DataBuffer& /* buffer */) override {}
  bool IsOkToCallOnTheNetworkThread() override { return true; }

 private:
  DataChannelInterface* channel_to_close_;
};

// Tests that AllocateSctpSids() doesn't start a channel that got closed (and
// removed) while an earlier channel was being started. Here, the observer of
// the first channel closes the second channel when the first channel opens.
TEST_F(DataChannelControllerTest, AllocateSctpSidsSkipsChannelClosedByOther) {
  NiceMock<MockDataChannelTransport> transport;
  bool ready_to_send = false;
  ON_CALL(transport, IsReadyToSend)
      .WillByDefault(ReturnPointee(&ready_to_send));
  // Don't assign stream ids when the channels are created.
  ON_CALL(transport, DtlsRole).WillByDefault(Return(std::nullopt));
  DataChannelControllerForTest dcc(pc_.get(), &transport);
  // Complete the closing procedure of a channel synchronously.
  ON_CALL(transport, CloseChannel).WillByDefault([&](int channel_id) {
    dcc.OnChannelClosed(channel_id);
    return RTCError::OK();
  });

  scoped_refptr<DataChannelInterface> ch1 =
      dcc.InternalCreateDataChannelWithProxy(
             "ch1", InternalDataChannelInit(DataChannelInit()))
          .MoveValue();
  scoped_refptr<DataChannelInterface> ch2 =
      dcc.InternalCreateDataChannelWithProxy(
             "ch2", InternalDataChannelInit(DataChannelInit()))
          .MoveValue();
  CloseOnStateChangeObserver observer(ch2.get());
  ch1->RegisterObserver(&observer);

  ready_to_send = true;
  network_thread_.BlockingCall([&] { dcc.AllocateSctpSids(SSL_CLIENT); });
  run_loop_.Flush();

  EXPECT_EQ(ch1->state(), DataChannelInterface::kOpen);
  EXPECT_EQ(ch2->state(), DataChannelInterface::kClosed);
  ch1->UnregisterObserver();
}

TEST_F(DataChannelControllerTest, BufferedAmountIncludesFromTransport) {
  NiceMock<MockDataChannelTransport> transport;
  EXPECT_CALL(transport, buffered_amount(0)).WillOnce(Return(4711));
  ON_CALL(transport, DtlsRole).WillByDefault([&]() { return SSL_CLIENT; });

  DataChannelControllerForTest dcc(pc_.get(), &transport);
  auto dc = dcc.InternalCreateDataChannelWithProxy(
                   "label", InternalDataChannelInit(DataChannelInit()))
                .MoveValue();
  EXPECT_EQ(dc->buffered_amount(), 4711u);
}

// Test that while a data channel is in the `kClosing` state, its StreamId does
// not get re-used for new channels. Only once the state reaches `kClosed`
// should a StreamId be available again for allocation.
TEST_F(DataChannelControllerTest, NoStreamIdReuseWhileClosing) {
  NiceMock<MockDataChannelTransport> transport;  // Wider scope than `dcc`.
  ON_CALL(transport, DtlsRole).WillByDefault([&]() { return SSL_CLIENT; });
  DataChannelControllerForTest dcc(pc_.get(), &transport);

  // Create the first channel and check that we got the expected, first sid.
  auto channel1 = dcc.InternalCreateDataChannelWithProxy(
                         "label", InternalDataChannelInit(DataChannelInit()))
                      .MoveValue();
  ASSERT_EQ(channel1->id(), 0);

  // Start closing the channel and make sure its state is `kClosing`
  channel1->Close();
  ASSERT_EQ(channel1->state(), DataChannelInterface::DataState::kClosing);

  // Create a second channel and make sure we get a new StreamId, not the same
  // as that of channel1.
  auto channel2 = dcc.InternalCreateDataChannelWithProxy(
                         "label2", InternalDataChannelInit(DataChannelInit()))
                      .MoveValue();
  ASSERT_NE(channel2->id(), channel1->id());  // In practice the id will be 2.

  // Simulate the acknowledgement of the channel closing from the transport.
  // This completes the closing operation of channel1.
  pc_->network_thread()->BlockingCall([&] { dcc.OnChannelClosed(0); });
  run_loop_.Flush();
  ASSERT_EQ(channel1->state(), DataChannelInterface::DataState::kClosed);

  // Now create a third channel. This time, the id of the first channel should
  // be available again and therefore the ids of the first and third channels
  // should be the same.
  auto channel3 = dcc.InternalCreateDataChannelWithProxy(
                         "label3", InternalDataChannelInit(DataChannelInit()))
                      .MoveValue();
  EXPECT_EQ(channel3->id(), channel1->id());
}

TEST_F(DataChannelControllerTest, ObserverNotifiedOnStringMessageSent) {
  NiceMock<MockDataChannelTransport> transport;
  DataChannelControllerForTest dcc(pc_.get(), &transport);

  std::vector<Message> messages;
  auto observer = std::make_unique<NiceMock<MockDataChannelEventObserver>>();
  ON_CALL(*observer, OnMessage).WillByDefault([&](const Message& m) {
    messages.push_back(m);
  });
  network_thread_.BlockingCall(
      [&]() { dcc.SetEventObserver(std::move(observer)); });

  RTCErrorOr<scoped_refptr<DataChannelInterface>> ret =
      dcc.InternalCreateDataChannelWithProxy(
          "TestingSomeSendStuff",
          InternalDataChannelInit({.negotiated = true, .id = 5}));
  ASSERT_TRUE(ret.ok());
  auto channel = ret.MoveValue();

  clock_.SetTime(Timestamp::Millis(123));
  network_thread_.BlockingCall([&]() {
    dcc.SendData(StreamId(5), {.type = DataMessageType::kText},
                 CopyOnWriteBuffer(kSomeData));
  });

  channel->Close();
  run_loop_.Flush();

  ASSERT_THAT(messages, SizeIs(1));
  EXPECT_EQ(messages[0].unix_timestamp_ms(), 123);
  EXPECT_EQ(messages[0].datachannel_id(), 5);
  EXPECT_EQ(messages[0].label(), "TestingSomeSendStuff");
  EXPECT_EQ(messages[0].direction(), Message::Direction::kSend);
  EXPECT_EQ(messages[0].data_type(), Message::DataType::kString);
  EXPECT_THAT(messages[0].data(), ElementsAreArray(kSomeData));
}

TEST_F(DataChannelControllerTest, ObserverNotifiedOnBinaryMessageSent) {
  NiceMock<MockDataChannelTransport> transport;
  DataChannelControllerForTest dcc(pc_.get(), &transport);

  std::vector<Message> messages;
  auto observer = std::make_unique<NiceMock<MockDataChannelEventObserver>>();
  ON_CALL(*observer, OnMessage).WillByDefault([&](const Message& m) {
    messages.push_back(m);
  });
  network_thread_.BlockingCall(
      [&]() { dcc.SetEventObserver(std::move(observer)); });

  RTCErrorOr<scoped_refptr<DataChannelInterface>> ret =
      dcc.InternalCreateDataChannelWithProxy(
          "TestingSomeSendStuff",
          InternalDataChannelInit({.negotiated = true, .id = 5}));
  ASSERT_TRUE(ret.ok());
  auto channel = ret.MoveValue();

  clock_.SetTime(Timestamp::Millis(123));
  network_thread_.BlockingCall([&]() {
    dcc.SendData(StreamId(5), {.type = DataMessageType::kBinary},
                 CopyOnWriteBuffer(kSomeData));
  });

  channel->Close();
  run_loop_.Flush();

  ASSERT_THAT(messages, SizeIs(1));
  EXPECT_EQ(messages[0].unix_timestamp_ms(), 123);
  EXPECT_EQ(messages[0].datachannel_id(), 5);
  EXPECT_EQ(messages[0].label(), "TestingSomeSendStuff");
  EXPECT_EQ(messages[0].direction(), Message::Direction::kSend);
  EXPECT_EQ(messages[0].data_type(), Message::DataType::kBinary);
  EXPECT_THAT(messages[0].data(), ElementsAreArray(kSomeData));
}

TEST_F(DataChannelControllerTest, ObserverNotNotifiedOnControlMessageSent) {
  NiceMock<MockDataChannelTransport> transport;
  DataChannelControllerForTest dcc(pc_.get(), &transport);

  std::vector<Message> messages;
  auto observer = std::make_unique<NiceMock<MockDataChannelEventObserver>>();
  ON_CALL(*observer, OnMessage).WillByDefault([&](const Message& m) {
    messages.push_back(m);
  });
  network_thread_.BlockingCall(
      [&]() { dcc.SetEventObserver(std::move(observer)); });

  RTCErrorOr<scoped_refptr<DataChannelInterface>> ret =
      dcc.InternalCreateDataChannelWithProxy(
          "TestingSomeSendStuff",
          InternalDataChannelInit({.negotiated = true, .id = 5}));
  ASSERT_TRUE(ret.ok());
  auto channel = ret.MoveValue();

  network_thread_.BlockingCall([&]() {
    dcc.SendData(StreamId(5), {.type = DataMessageType::kControl},
                 CopyOnWriteBuffer(kSomeData));
  });

  channel->Close();
  run_loop_.Flush();

  ASSERT_TRUE(messages.empty());
}

TEST_F(DataChannelControllerTest, ObserverNotNotifiedOnTransportFailed) {
  NiceMock<MockDataChannelTransport> transport;
  ON_CALL(transport, SendData)
      .WillByDefault(Return(RTCError(RTCErrorType::INVALID_STATE)));
  DataChannelControllerForTest dcc(pc_.get(), &transport);

  std::vector<Message> messages;
  auto observer = std::make_unique<NiceMock<MockDataChannelEventObserver>>();
  ON_CALL(*observer, OnMessage).WillByDefault([&](const Message& m) {
    messages.push_back(m);
  });
  network_thread_.BlockingCall(
      [&]() { dcc.SetEventObserver(std::move(observer)); });

  RTCErrorOr<scoped_refptr<DataChannelInterface>> ret =
      dcc.InternalCreateDataChannelWithProxy(
          "TestingSomeSendStuff",
          InternalDataChannelInit({.negotiated = true, .id = 5}));
  ASSERT_TRUE(ret.ok());
  auto channel = ret.MoveValue();

  network_thread_.BlockingCall([&]() {
    dcc.SendData(StreamId(5), {.type = DataMessageType::kText},
                 CopyOnWriteBuffer(kSomeData));
  });

  channel->Close();
  run_loop_.Flush();

  ASSERT_TRUE(messages.empty());
}

TEST_F(DataChannelControllerTest, ObserverNotifiedOnStringMessageReceived) {
  NiceMock<MockDataChannelTransport> transport;
  DataChannelControllerForTest dcc(pc_.get(), &transport);

  std::vector<Message> messages;
  auto observer = std::make_unique<NiceMock<MockDataChannelEventObserver>>();
  ON_CALL(*observer, OnMessage).WillByDefault([&](const Message& m) {
    messages.push_back(m);
  });
  network_thread_.BlockingCall(
      [&]() { dcc.SetEventObserver(std::move(observer)); });

  RTCErrorOr<scoped_refptr<DataChannelInterface>> ret =
      dcc.InternalCreateDataChannelWithProxy(
          "TestingSomeReceiveStuff",
          InternalDataChannelInit({.negotiated = true, .id = 5}));
  ASSERT_TRUE(ret.ok());
  auto channel = ret.MoveValue();

  clock_.SetTime(Timestamp::Millis(123));
  network_thread_.BlockingCall([&]() {
    dcc.OnDataReceived(5, DataMessageType::kText, CopyOnWriteBuffer(kSomeData));
  });

  channel->Close();
  run_loop_.Flush();

  ASSERT_THAT(messages, SizeIs(1));
  EXPECT_EQ(messages[0].unix_timestamp_ms(), 123);
  EXPECT_EQ(messages[0].datachannel_id(), 5);
  EXPECT_EQ(messages[0].label(), "TestingSomeReceiveStuff");
  EXPECT_EQ(messages[0].direction(), Message::Direction::kReceive);
  EXPECT_EQ(messages[0].data_type(), Message::DataType::kString);
  EXPECT_THAT(messages[0].data(), ElementsAreArray(kSomeData));
}

TEST_F(DataChannelControllerTest, ObserverNotifiedOnBinaryMessageReceived) {
  NiceMock<MockDataChannelTransport> transport;
  DataChannelControllerForTest dcc(pc_.get(), &transport);

  std::vector<Message> messages;
  auto observer = std::make_unique<NiceMock<MockDataChannelEventObserver>>();
  ON_CALL(*observer, OnMessage).WillByDefault([&](const Message& m) {
    messages.push_back(m);
  });
  network_thread_.BlockingCall(
      [&]() { dcc.SetEventObserver(std::move(observer)); });

  RTCErrorOr<scoped_refptr<DataChannelInterface>> ret =
      dcc.InternalCreateDataChannelWithProxy(
          "TestingSomeReceiveStuff",
          InternalDataChannelInit({.negotiated = true, .id = 5}));
  ASSERT_TRUE(ret.ok());
  auto channel = ret.MoveValue();

  clock_.SetTime(Timestamp::Millis(123));
  network_thread_.BlockingCall([&]() {
    dcc.OnDataReceived(5, DataMessageType::kBinary,
                       CopyOnWriteBuffer(kSomeData));
  });

  channel->Close();
  run_loop_.Flush();

  ASSERT_THAT(messages, SizeIs(1));
  EXPECT_EQ(messages[0].unix_timestamp_ms(), 123);
  EXPECT_EQ(messages[0].datachannel_id(), 5);
  EXPECT_EQ(messages[0].label(), "TestingSomeReceiveStuff");
  EXPECT_EQ(messages[0].direction(), Message::Direction::kReceive);
  EXPECT_EQ(messages[0].data_type(), Message::DataType::kBinary);
  EXPECT_THAT(messages[0].data(), ElementsAreArray(kSomeData));
}

TEST_F(DataChannelControllerTest, ObserverNotNotifiedOnControlMessageReceived) {
  NiceMock<MockDataChannelTransport> transport;
  DataChannelControllerForTest dcc(pc_.get(), &transport);

  std::vector<Message> messages;
  auto observer = std::make_unique<NiceMock<MockDataChannelEventObserver>>();
  ON_CALL(*observer, OnMessage).WillByDefault([&](const Message& m) {
    messages.push_back(m);
  });
  network_thread_.BlockingCall(
      [&]() { dcc.SetEventObserver(std::move(observer)); });

  RTCErrorOr<scoped_refptr<DataChannelInterface>> ret =
      dcc.InternalCreateDataChannelWithProxy(
          "TestingSomeReceiveStuff",
          InternalDataChannelInit({.negotiated = true, .id = 5}));
  ASSERT_TRUE(ret.ok());
  auto channel = ret.MoveValue();

  network_thread_.BlockingCall([&]() {
    dcc.OnDataReceived(5, DataMessageType::kControl,
                       CopyOnWriteBuffer(kSomeData));
  });

  channel->Close();
  run_loop_.Flush();

  EXPECT_THAT(messages, IsEmpty());
}

TEST_F(DataChannelControllerTest, ObserverNotNotifiedOnUnknownId) {
  NiceMock<MockDataChannelTransport> transport;
  DataChannelControllerForTest dcc(pc_.get(), &transport);

  std::vector<Message> messages;
  auto observer = std::make_unique<NiceMock<MockDataChannelEventObserver>>();
  ON_CALL(*observer, OnMessage).WillByDefault([&](const Message& m) {
    messages.push_back(m);
  });
  network_thread_.BlockingCall(
      [&]() { dcc.SetEventObserver(std::move(observer)); });

  RTCErrorOr<scoped_refptr<DataChannelInterface>> ret =
      dcc.InternalCreateDataChannelWithProxy(
          "TestingSomeReceiveStuff",
          InternalDataChannelInit({.negotiated = true, .id = 5}));
  ASSERT_TRUE(ret.ok());
  auto channel = ret.MoveValue();

  network_thread_.BlockingCall([&]() {
    dcc.OnDataReceived(3, DataMessageType::kText, CopyOnWriteBuffer(kSomeData));
  });

  channel->Close();
  run_loop_.Flush();

  EXPECT_THAT(messages, IsEmpty());
}

TEST_F(DataChannelControllerTest, ReopenChannelWhileClosing) {
  NiceMock<MockDataChannelTransport> transport;
  DataChannelControllerForTest dcc(pc_.get(), &transport);

  // 1. Create old channel with ID 4.
  RTCErrorOr<scoped_refptr<DataChannelInterface>> ret =
      dcc.InternalCreateDataChannelWithProxy(
          "OldChannel", InternalDataChannelInit({.negotiated = true, .id = 4}));
  ASSERT_TRUE(ret.ok());
  auto old_channel = ret.MoveValue();

  // 2. Close the old channel. It should transition to kClosing.
  old_channel->Close();
  run_loop_.Flush();
  EXPECT_EQ(old_channel->state(), DataChannelInterface::DataState::kClosing);

  // 3. Prepare to capture the new channel creation via observer.
  scoped_refptr<DataChannelInterface> new_channel;
  ON_CALL(*pc_, RunWithObserver)
      .WillByDefault(
          [&](absl::AnyInvocable<void(webrtc::PeerConnectionObserver*) &&>
                  callback) {
            class FakeObserver : public PeerConnectionObserver {
             public:
              explicit FakeObserver(
                  scoped_refptr<DataChannelInterface>& out_channel)
                  : out_channel_(out_channel) {}
              void OnSignalingChange(
                  PeerConnectionInterface::SignalingState) override {}
              void OnAddTrack(
                  scoped_refptr<RtpReceiverInterface>,
                  const std::vector<scoped_refptr<MediaStreamInterface>>&)
                  override {}
              void OnDataChannel(
                  scoped_refptr<DataChannelInterface> data_channel) override {
                out_channel_ = data_channel;
              }
              void OnRenegotiationNeeded() override {}
              void OnIceConnectionChange(
                  PeerConnectionInterface::IceConnectionState) override {}
              void OnIceGatheringChange(
                  PeerConnectionInterface::IceGatheringState) override {}
              void OnIceCandidate(const IceCandidate*) override {}

             private:
              scoped_refptr<DataChannelInterface>& out_channel_;
            };
            FakeObserver observer(new_channel);
            std::move(callback)(&observer);
          });

  // 4. Construct a valid OPEN message for SID 4.
  CopyOnWriteBuffer open_message;
  DataChannelInit open_config;
  open_config.id = 4;
  ASSERT_TRUE(
      WriteDataChannelOpenMessage("NewChannel", open_config, &open_message));

  // 5. Deliver the OPEN message. This should trigger creation of the new
  // channel.
  network_thread_.BlockingCall([&]() {
    dcc.OnDataReceived(4, DataMessageType::kControl, open_message);
  });
  run_loop_.Flush();

  // 6. Verify that the new channel was created.
  ASSERT_NE(new_channel, nullptr);
  EXPECT_EQ(new_channel->label(), "NewChannel");
  EXPECT_EQ(new_channel->id(), 4);

  // Verify that the old channel is now in the kClosed state.
  EXPECT_EQ(old_channel->state(), DataChannelInterface::DataState::kClosed);
}

}  // namespace
}  // namespace webrtc
