/*
 *  Copyright 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "media/sctp/dcsctp_transport.h"

#include <memory>
#include <utility>

#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "net/dcsctp/public/mock_dcsctp_socket.h"
#include "net/dcsctp/public/mock_dcsctp_socket_factory.h"
#include "p2p/base/fake_packet_transport.h"
#include "test/gtest.h"

using ::testing::_;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnPointee;

namespace webrtc {

namespace {
class MockDataChannelSink : public DataChannelSink {
 public:
  MOCK_METHOD(void, OnConnected, ());

  // DataChannelSink
  MOCK_METHOD(void,
              OnDataReceived,
              (int, DataMessageType, const rtc::CopyOnWriteBuffer&));
  MOCK_METHOD(void, OnChannelClosing, (int));
  MOCK_METHOD(void, OnChannelClosed, (int));
  MOCK_METHOD(void, OnReadyToSend, ());
  MOCK_METHOD(void, OnTransportClosed, (RTCError));
  MOCK_METHOD(void, OnBufferedAmountLow, (int channel_id), (override));
};

static_assert(!std::is_abstract_v<MockDataChannelSink>);

class Peer {
 public:
  Peer()
      : fake_packet_transport_("transport"),
        simulated_clock_(1000),
        env_(CreateEnvironment(&simulated_clock_)) {
    auto socket_ptr = std::make_unique<dcsctp::MockDcSctpSocket>();
    socket_ = socket_ptr.get();

    auto mock_dcsctp_socket_factory =
        std::make_unique<dcsctp::MockDcSctpSocketFactory>();
    EXPECT_CALL(*mock_dcsctp_socket_factory, Create)
        .Times(1)
        .WillOnce(Return(ByMove(std::move(socket_ptr))));

    sctp_transport_ = std::make_unique<webrtc::DcSctpTransport>(
        env_, rtc::Thread::Current(), &fake_packet_transport_,
        std::move(mock_dcsctp_socket_factory));
    sctp_transport_->SetDataChannelSink(&sink_);
    sctp_transport_->SetOnConnectedCallback([this]() { sink_.OnConnected(); });
  }

  rtc::FakePacketTransport fake_packet_transport_;
  webrtc::SimulatedClock simulated_clock_;
  Environment env_;
  dcsctp::MockDcSctpSocket* socket_;
  std::unique_ptr<webrtc::DcSctpTransport> sctp_transport_;
  NiceMock<MockDataChannelSink> sink_;
};
}  // namespace

TEST(DcSctpTransportTest, OpenSequence) {
  rtc::AutoThread main_thread;
  Peer peer_a;
  peer_a.fake_packet_transport_.SetWritable(true);

  EXPECT_CALL(*peer_a.socket_, Connect)
      .Times(1)
      .WillOnce(Invoke(peer_a.sctp_transport_.get(),
                       &dcsctp::DcSctpSocketCallbacks::OnConnected));
  EXPECT_CALL(peer_a.sink_, OnReadyToSend);
  EXPECT_CALL(peer_a.sink_, OnConnected);

  peer_a.sctp_transport_->Start(5000, 5000, 256 * 1024);
}

// Tests that the close sequence invoked from one end results in the stream to
// be reset from both ends and all the proper signals are sent.
TEST(DcSctpTransportTest, CloseSequence) {
  rtc::AutoThread main_thread;
  Peer peer_a;
  Peer peer_b;
  peer_a.fake_packet_transport_.SetDestination(&peer_b.fake_packet_transport_,
                                               false);
  {
    InSequence sequence;

    EXPECT_CALL(*peer_a.socket_, ResetStreams(ElementsAre(dcsctp::StreamID(1))))
        .WillOnce(Return(dcsctp::ResetStreamsStatus::kPerformed));

    EXPECT_CALL(*peer_b.socket_, ResetStreams(ElementsAre(dcsctp::StreamID(1))))
        .WillOnce(Return(dcsctp::ResetStreamsStatus::kPerformed));

    EXPECT_CALL(peer_a.sink_, OnChannelClosing(1)).Times(0);
    EXPECT_CALL(peer_b.sink_, OnChannelClosing(1));
    EXPECT_CALL(peer_a.sink_, OnChannelClosed(1));
    EXPECT_CALL(peer_b.sink_, OnChannelClosed(1));
  }

  peer_a.sctp_transport_->Start(5000, 5000, 256 * 1024);
  peer_b.sctp_transport_->Start(5000, 5000, 256 * 1024);
  peer_a.sctp_transport_->OpenStream(1);
  peer_b.sctp_transport_->OpenStream(1);
  peer_a.sctp_transport_->ResetStream(1);

  // Simulate the callbacks from the stream resets
  dcsctp::StreamID streams[1] = {dcsctp::StreamID(1)};
  static_cast<dcsctp::DcSctpSocketCallbacks*>(peer_a.sctp_transport_.get())
      ->OnStreamsResetPerformed(streams);
  static_cast<dcsctp::DcSctpSocketCallbacks*>(peer_b.sctp_transport_.get())
      ->OnIncomingStreamsReset(streams);
  static_cast<dcsctp::DcSctpSocketCallbacks*>(peer_a.sctp_transport_.get())
      ->OnIncomingStreamsReset(streams);
  static_cast<dcsctp::DcSctpSocketCallbacks*>(peer_b.sctp_transport_.get())
      ->OnStreamsResetPerformed(streams);
}

// Tests that the close sequence initiated from both peers at the same time
// terminates properly. Both peers will think they initiated it, so no
// OnClosingProcedureStartedRemotely should be called.
TEST(DcSctpTransportTest, CloseSequenceSimultaneous) {
  rtc::AutoThread main_thread;
  Peer peer_a;
  Peer peer_b;
  peer_a.fake_packet_transport_.SetDestination(&peer_b.fake_packet_transport_,
                                               false);
  {
    InSequence sequence;

    EXPECT_CALL(*peer_a.socket_, ResetStreams(ElementsAre(dcsctp::StreamID(1))))
        .WillOnce(Return(dcsctp::ResetStreamsStatus::kPerformed));

    EXPECT_CALL(*peer_b.socket_, ResetStreams(ElementsAre(dcsctp::StreamID(1))))
        .WillOnce(Return(dcsctp::ResetStreamsStatus::kPerformed));

    EXPECT_CALL(peer_a.sink_, OnChannelClosing(1)).Times(0);
    EXPECT_CALL(peer_b.sink_, OnChannelClosing(1)).Times(0);
    EXPECT_CALL(peer_a.sink_, OnChannelClosed(1));
    EXPECT_CALL(peer_b.sink_, OnChannelClosed(1));
  }

  peer_a.sctp_transport_->Start(5000, 5000, 256 * 1024);
  peer_b.sctp_transport_->Start(5000, 5000, 256 * 1024);
  peer_a.sctp_transport_->OpenStream(1);
  peer_b.sctp_transport_->OpenStream(1);
  peer_a.sctp_transport_->ResetStream(1);
  peer_b.sctp_transport_->ResetStream(1);

  // Simulate the callbacks from the stream resets
  dcsctp::StreamID streams[1] = {dcsctp::StreamID(1)};
  static_cast<dcsctp::DcSctpSocketCallbacks*>(peer_a.sctp_transport_.get())
      ->OnStreamsResetPerformed(streams);
  static_cast<dcsctp::DcSctpSocketCallbacks*>(peer_b.sctp_transport_.get())
      ->OnStreamsResetPerformed(streams);
  static_cast<dcsctp::DcSctpSocketCallbacks*>(peer_a.sctp_transport_.get())
      ->OnIncomingStreamsReset(streams);
  static_cast<dcsctp::DcSctpSocketCallbacks*>(peer_b.sctp_transport_.get())
      ->OnIncomingStreamsReset(streams);
}

TEST(DcSctpTransportTest, DiscardMessageClosedChannel) {
  rtc::AutoThread main_thread;
  Peer peer_a;

  EXPECT_CALL(*peer_a.socket_, Send(_, _)).Times(0);

  peer_a.sctp_transport_->Start(5000, 5000, 256 * 1024);

  SendDataParams params;
  rtc::CopyOnWriteBuffer payload;
  EXPECT_EQ(peer_a.sctp_transport_->SendData(1, params, payload).type(),
            RTCErrorType::INVALID_STATE);
}

TEST(DcSctpTransportTest, DiscardMessageClosingChannel) {
  rtc::AutoThread main_thread;
  Peer peer_a;

  EXPECT_CALL(*peer_a.socket_, Send(_, _)).Times(0);

  peer_a.sctp_transport_->OpenStream(1);
  peer_a.sctp_transport_->Start(5000, 5000, 256 * 1024);
  peer_a.sctp_transport_->ResetStream(1);

  SendDataParams params;
  rtc::CopyOnWriteBuffer payload;
  EXPECT_EQ(peer_a.sctp_transport_->SendData(1, params, payload).type(),
            RTCErrorType::INVALID_STATE);
}

TEST(DcSctpTransportTest, SendDataOpenChannel) {
  rtc::AutoThread main_thread;
  Peer peer_a;
  dcsctp::DcSctpOptions options;

  EXPECT_CALL(*peer_a.socket_, Send(_, _)).Times(1);
  EXPECT_CALL(*peer_a.socket_, options()).WillOnce(ReturnPointee(&options));

  peer_a.sctp_transport_->OpenStream(1);
  peer_a.sctp_transport_->Start(5000, 5000, 256 * 1024);

  SendDataParams params;
  rtc::CopyOnWriteBuffer payload;
  EXPECT_TRUE(peer_a.sctp_transport_->SendData(1, params, payload).ok());
}

TEST(DcSctpTransportTest, DeliversMessage) {
  rtc::AutoThread main_thread;
  Peer peer_a;

  EXPECT_CALL(peer_a.sink_,
              OnDataReceived(1, webrtc::DataMessageType::kBinary, _))
      .Times(1);

  peer_a.sctp_transport_->OpenStream(1);
  peer_a.sctp_transport_->Start(5000, 5000, 256 * 1024);

  static_cast<dcsctp::DcSctpSocketCallbacks*>(peer_a.sctp_transport_.get())
      ->OnMessageReceived(
          dcsctp::DcSctpMessage(dcsctp::StreamID(1), dcsctp::PPID(53), {0}));
}

TEST(DcSctpTransportTest, DropMessageWithUnknownPpid) {
  rtc::AutoThread main_thread;
  Peer peer_a;

  EXPECT_CALL(peer_a.sink_, OnDataReceived(_, _, _)).Times(0);

  peer_a.sctp_transport_->OpenStream(1);
  peer_a.sctp_transport_->Start(5000, 5000, 256 * 1024);

  static_cast<dcsctp::DcSctpSocketCallbacks*>(peer_a.sctp_transport_.get())
      ->OnMessageReceived(
          dcsctp::DcSctpMessage(dcsctp::StreamID(1), dcsctp::PPID(1337), {0}));
}
}  // namespace webrtc
