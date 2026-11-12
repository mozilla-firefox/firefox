/*
 *  Copyright (c) 2025 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "api/datagram_connection.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "api/candidate.h"
#include "api/environment/environment.h"
#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "api/test/mock_datagram_connection_observer.h"
#include "api/transport/ecn_marking.h"
#include "api/transport/enums.h"
#include "api/units/timestamp.h"
#include "modules/rtp_rtcp/source/rtp_packet.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_util.h"
#include "p2p/base/ice_transport_internal.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/test/fake_ice_transport.h"
#include "pc/datagram_connection_internal.h"
#include "pc/dtls_packet_processor.h"
#include "pc/test/fake_rtc_certificate_generator.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/ssl_fingerprint.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"
#include "test/wait_until.h"

namespace webrtc {
namespace {

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::NiceMock;
using ::testing::Return;

using PacketSendParameters = DatagramConnection::PacketSendParameters;
using SendOutcome = DatagramConnection::Observer::SendOutcome;
using WireProtocol = DatagramConnection::WireProtocol;

bool IsRtpOrRtcpPacket(uint8_t first_byte) {
  return (first_byte & 0xc0) == 0x80;
}

class DatagramConnectionTest : public ::testing::Test {
 public:
  DatagramConnectionTest() : env_(CreateTestEnvironment()) {}

  ~DatagramConnectionTest() override {
    conn1_->Terminate([] {});
    conn2_->Terminate([] {});
  }

  void CreateConnections(WireProtocol wire_protocol = WireProtocol::kDtlsSrtp) {
    auto observer1 =
        std::make_unique<NiceMock<MockDatagramConnectionObserver>>();
    observer1_ptr_ = observer1.get();
    auto observer2 =
        std::make_unique<NiceMock<MockDatagramConnectionObserver>>();
    observer2_ptr_ = observer2.get();

    cert1_ = FakeRTCCertificateGenerator::GenerateCertificate();
    cert2_ = FakeRTCCertificateGenerator::GenerateCertificate();
    std::string transport_name1 = "FakeTransport1";
    std::string transport_name2 = "FakeTransport2";

    auto ice1 = std::make_unique<FakeIceTransportInternal>(
        env_, transport_name1, ICE_CANDIDATE_COMPONENT_RTP);
    ice1->SetAsync(true);
    auto ice2 = std::make_unique<FakeIceTransportInternal>(
        env_, transport_name2, ICE_CANDIDATE_COMPONENT_RTP);
    ice2->SetAsync(true);
    ice1_ = ice1.get();
    ice2_ = ice2.get();

    conn1_ = make_ref_counted<DatagramConnectionInternal>(
        env_, /*port_allocator=*/nullptr, transport_name1, true, cert1_,
        std::move(observer1), wire_protocol, std::move(ice1));

    conn2_ = make_ref_counted<DatagramConnectionInternal>(
        env_, /*port_allocator=*/nullptr, transport_name2, false, cert2_,
        std::move(observer2), wire_protocol, std::move(ice2));
  }

  void Connect() {
    auto fingerprint1 = SSLFingerprint::CreateFromCertificate(*cert1_);
    auto fingerprint2 = SSLFingerprint::CreateFromCertificate(*cert2_);

    conn1_->SetRemoteDtlsParameters(
        fingerprint2->algorithm, fingerprint2->digest.data(),
        fingerprint2->digest.size(), DatagramConnection::SSLRole::kClient);
    conn2_->SetRemoteDtlsParameters(
        fingerprint1->algorithm, fingerprint1->digest.data(),
        fingerprint1->digest.size(), DatagramConnection::SSLRole::kServer);

    ice1_->SetDestination(ice2_);
  }

 protected:
  test::RunLoop loop_;
  const Environment env_;
  NiceMock<MockDatagramConnectionObserver>* observer1_ptr_ = nullptr;
  NiceMock<MockDatagramConnectionObserver>* observer2_ptr_ = nullptr;
  scoped_refptr<RTCCertificate> cert1_;
  scoped_refptr<RTCCertificate> cert2_;
  scoped_refptr<DatagramConnectionInternal> conn1_;
  scoped_refptr<DatagramConnectionInternal> conn2_;
  FakeIceTransportInternal* ice1_;
  FakeIceTransportInternal* ice2_;
};

CopyOnWriteBuffer MakeRtpPacketBuffer(int sequence_number = 1) {
  RtpPacket rtp_packet;
  rtp_packet.SetSequenceNumber(sequence_number);
  rtp_packet.SetTimestamp(2);
  rtp_packet.SetSsrc(12345);
  std::vector<uint8_t> data = {1, 2, 3, 4, 5};
  rtp_packet.SetPayload(data);
  return rtp_packet.Buffer();
}

TEST_F(DatagramConnectionTest, CreateAndDestroy) {
  CreateConnections();
  EXPECT_TRUE(conn1_);
  EXPECT_TRUE(conn2_);
}

TEST_F(DatagramConnectionTest, IceCredsGettersReturnCorrectValues) {
  CreateConnections();

  auto* ice_parameters = ice1_->local_ice_parameters();
  EXPECT_EQ(ice_parameters->ufrag, conn1_->IceUsernameFragment());
  EXPECT_EQ(ice_parameters->pwd, conn1_->IcePassword());
}

TEST_F(DatagramConnectionTest, TransportsBecomeWritable) {
  CreateConnections();
  Connect();

  ASSERT_TRUE(
      WaitUntil([&]() { return conn1_->Writable() && conn2_->Writable(); }));
  EXPECT_TRUE(conn1_->Writable());
  EXPECT_TRUE(conn2_->Writable());
}

TEST_F(DatagramConnectionTest, ObserverNotifiedOnWritableChange) {
  CreateConnections();
  EXPECT_FALSE(conn1_->Writable());

  bool callback_called = false;
  EXPECT_CALL(*observer1_ptr_, OnWritableChange()).WillOnce([&]() {
    callback_called = true;
    loop_.Quit();
  });

  Connect();

  loop_.Run();
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(conn1_->Writable());
}

TEST_F(DatagramConnectionTest, ObserverCalledOnReceivedRtpPacket) {
  CreateConnections();

  auto packet_data = MakeRtpPacketBuffer();
  RtpPacketReceived packet;
  packet.Parse(packet_data);
  packet.set_arrival_time(Timestamp::Seconds(1234));
  packet.set_ecn(EcnMarking::kCe);

  bool callback_called = false;
  EXPECT_CALL(*observer1_ptr_, OnPacketReceived(_, _))
      .WillOnce(
          [&](std::span<const uint8_t> data,
              const DatagramConnection::Observer::PacketMetadata& metadata) {
            EXPECT_EQ(data.size(), packet_data.size());
            EXPECT_EQ(
                memcmp(data.data(), packet_data.data(), packet_data.size()), 0);
            EXPECT_EQ(metadata.receive_time, packet.arrival_time());
            EXPECT_EQ(metadata.ecn, packet.ecn());
            callback_called = true;
            loop_.Quit();
          });

  conn1_->OnRtpPacket(packet);

  loop_.Run();
  EXPECT_TRUE(callback_called);
}

TEST_F(DatagramConnectionTest, DtlsWithFeedbackPacketReportsEcnMarking) {
  CreateConnections(WireProtocol::kDtlsWithFeedback);

  const std::vector<uint8_t> payload = {1, 2, 3, 4, 5};
  DtlsPacketProcessor sender;
  CopyOnWriteBuffer wire =
      sender.ProcessOutgoingPacket(payload, Timestamp::Millis(1));

  bool callback_called = false;
  EXPECT_CALL(*observer1_ptr_, OnPacketReceived)
      .WillOnce(
          [&](std::span<const uint8_t> data,
              const DatagramConnection::Observer::PacketMetadata& metadata) {
            EXPECT_THAT(data, ElementsAreArray(payload));
            EXPECT_EQ(metadata.receive_time, Timestamp::Millis(2));
            EXPECT_EQ(metadata.ecn, EcnMarking::kCe);
            callback_called = true;
            loop_.Quit();
          });

  conn1_->OnDtlsPacket(wire, Timestamp::Millis(2), EcnMarking::kCe);

  loop_.Run();
  EXPECT_TRUE(callback_called);
}

TEST_F(DatagramConnectionTest, RtpPacketsAreSent) {
  // Calling SendPacket causes the packet to be sent on ice1_
  CreateConnections();
  Connect();

  ASSERT_TRUE(
      WaitUntil([&]() { return conn1_->Writable() && conn2_->Writable(); }));

  auto data = MakeRtpPacketBuffer();
  bool callback_called = false;
  EXPECT_CALL(*observer1_ptr_, OnSendOutcome(_))
      .WillOnce([&](const SendOutcome& outcome) {
        EXPECT_EQ(outcome.id, 1u);
        EXPECT_EQ(outcome.status, SendOutcome::Status::kSuccess);
        EXPECT_NE(outcome.send_time, Timestamp::MinusInfinity());
        callback_called = true;
        loop_.Quit();
      });
  std::vector<PacketSendParameters> packets = {
      PacketSendParameters{.id = 1, .payload = data}};
  conn1_->SendPackets(packets);

  // Pull the RTP sequence number from ice1's last_sent_packet
  uint16_t seq_num = ParseRtpSequenceNumber(ice1_->last_sent_packet());
  EXPECT_EQ(seq_num, 1);
  loop_.Run();
  EXPECT_TRUE(callback_called);
}

TEST_F(DatagramConnectionTest, RtpPacketsAreReceived) {
  CreateConnections();
  Connect();

  ASSERT_TRUE(
      WaitUntil([&]() { return conn1_->Writable() && conn2_->Writable(); }));

  auto data = MakeRtpPacketBuffer();

  struct Callbacks {
    bool packet_received = false;
    bool send_outcome = false;
  } callbacks;

  auto check_done = [&] {
    if (callbacks.packet_received && callbacks.send_outcome)
      loop_.Quit();
  };

  EXPECT_CALL(*observer2_ptr_, OnPacketReceived(_, _))
      .WillOnce(
          [&](std::span<const uint8_t> received_data,
              const DatagramConnection::Observer::PacketMetadata& metadata) {
            EXPECT_EQ(received_data.size(), data.size());
            EXPECT_EQ(memcmp(received_data.data(), data.data(), data.size()),
                      0);
            EXPECT_NE(metadata.receive_time, Timestamp::Zero());
            callbacks.packet_received = true;
            check_done();
          });

  EXPECT_CALL(*observer1_ptr_, OnSendOutcome(_))
      .WillOnce([&](const SendOutcome& outcome) {
        EXPECT_EQ(outcome.id, 1u);
        EXPECT_EQ(outcome.status, SendOutcome::Status::kSuccess);
        EXPECT_NE(outcome.send_time, Timestamp::MinusInfinity());
        callbacks.send_outcome = true;
        check_done();
      });

  std::vector<PacketSendParameters> packets = {
      PacketSendParameters{.id = 1, .payload = data}};
  conn1_->SendPackets(packets);
  loop_.Run();
  EXPECT_TRUE(callbacks.packet_received);
  EXPECT_TRUE(callbacks.send_outcome);
}

TEST_F(DatagramConnectionTest, SendMultipleRtpPackets) {
  CreateConnections();
  Connect();

  ASSERT_TRUE(
      WaitUntil([&]() { return conn1_->Writable() && conn2_->Writable(); }));

  std::vector<CopyOnWriteBuffer> data = {
      MakeRtpPacketBuffer(1), MakeRtpPacketBuffer(2), MakeRtpPacketBuffer(3)};
  std::vector<PacketSendParameters> packets;
  for (size_t i = 0; i < data.size(); i++) {
    packets.push_back(
        PacketSendParameters{.id = static_cast<DatagramConnection::PacketId>(i),
                             .payload = data[i]});
  }

  std::set<DatagramConnection::PacketId> expected_send_ids = {0, 1, 2};
  std::vector<std::vector<uint8_t>> received_packets;
  auto check_done = [&] {
    if (expected_send_ids.empty() && received_packets.size() == data.size()) {
      loop_.Quit();
    }
  };

  EXPECT_CALL(*observer1_ptr_, OnSendOutcome(_))
      .WillRepeatedly([&](const SendOutcome& outcome) {
        EXPECT_EQ(outcome.status, SendOutcome::Status::kSuccess);
        EXPECT_NE(outcome.send_time, Timestamp::MinusInfinity());
        EXPECT_NE(expected_send_ids.find(outcome.id), expected_send_ids.end());
        expected_send_ids.erase(outcome.id);
        check_done();
      });

  EXPECT_CALL(*observer2_ptr_, OnPacketReceived(_, _))
      .Times(data.size())
      .WillRepeatedly(
          [&](std::span<const uint8_t> received_data,
              const DatagramConnection::Observer::PacketMetadata& metadata) {
            received_packets.emplace_back(received_data.begin(),
                                          received_data.end());
            check_done();
          });

  conn1_->SendPackets(packets);

  loop_.Run();

  EXPECT_EQ(received_packets.size(), data.size());
  for (size_t i = 0; i < data.size(); i++) {
    EXPECT_THAT(received_packets[i], ElementsAreArray(data[i]));
  }
}

TEST_F(DatagramConnectionTest, SendRtpPacketFailsWhenNotWritable) {
  CreateConnections();
  // Don't call Connect(), so the transports are not writable.
  CopyOnWriteBuffer data = MakeRtpPacketBuffer();
  EXPECT_FALSE(conn1_->Writable());
  EXPECT_CALL(*observer1_ptr_, OnSendOutcome(_))
      .WillOnce([&](const SendOutcome& outcome) {
        EXPECT_EQ(outcome.id, 1u);
        EXPECT_EQ(outcome.status, SendOutcome::Status::kNotSent);
        EXPECT_EQ(outcome.send_time, Timestamp::MinusInfinity());
      });
  std::vector<PacketSendParameters> packets = {
      PacketSendParameters{.id = 1, .payload = data}};
  conn1_->SendPackets(packets);
}

TEST_F(DatagramConnectionTest, SendRtpPacketFailsWhenDtlsNotActive) {
  CreateConnections();
  // Set destination to make the transport channel writable, but don't set DTLS
  // parameters, so DTLS is not active.
  ice1_->SetDestination(ice2_);
  ASSERT_TRUE(WaitUntil([&]() { return ice1_->writable(); }));
  EXPECT_TRUE(ice1_->writable());
  EXPECT_FALSE(
      conn1_->Writable());  // Should be false because DTLS is not active.

  std::vector<uint8_t> data = {1, 2, 3, 4, 5};
  EXPECT_CALL(*observer1_ptr_, OnSendOutcome(_))
      .WillOnce([&](const SendOutcome& outcome) {
        EXPECT_EQ(outcome.id, 1u);
        EXPECT_EQ(outcome.status, SendOutcome::Status::kNotSent);
        EXPECT_EQ(outcome.send_time, Timestamp::MinusInfinity());
      });
  std::vector<PacketSendParameters> packets = {
      PacketSendParameters{.id = 1, .payload = data}};
  conn1_->SendPackets(packets);
}

TEST_F(DatagramConnectionTest, NonRtpPacketsInSRTPModeAreDTLSProtected) {
  CreateConnections();
  Connect();

  ASSERT_TRUE(
      WaitUntil([&]() { return conn1_->Writable() && conn2_->Writable(); }));

  std::vector<uint8_t> non_rtp_data = {1, 2, 3, 4, 5};

  EXPECT_CALL(*observer1_ptr_, OnSendOutcome(_));
  std::vector<PacketSendParameters> packets = {
      PacketSendParameters{.id = 1, .payload = non_rtp_data}};
  conn1_->SendPackets(packets);

  // Payload isn't an RTP packet, so it should be sent as a DTLS packet.
  CopyOnWriteBuffer sent_buffer = ice1_->last_sent_packet();
  EXPECT_FALSE(IsRtpOrRtcpPacket(sent_buffer[0]));

  bool callback_called = false;
  EXPECT_CALL(*observer2_ptr_, OnPacketReceived(_, _))
      .WillOnce(
          [&](std::span<const uint8_t> received_data,
              const DatagramConnection::Observer::PacketMetadata& metadata) {
            // Check the data is decrypted correctly.
            EXPECT_EQ(received_data.size(), non_rtp_data.size());
            EXPECT_THAT(received_data, ElementsAreArray(non_rtp_data));
            callback_called = true;
            loop_.Quit();
          });

  loop_.Run();
  EXPECT_TRUE(callback_called);
}

TEST_F(DatagramConnectionTest, OnCandidateGathered) {
  CreateConnections();

  Candidate candidate(ICE_CANDIDATE_COMPONENT_RTP, "udp",
                      SocketAddress("1.1.1.1", 1234), 100, "", "",
                      IceCandidateType::kHost, 0, "1");
  bool callback_called = false;
  EXPECT_CALL(*observer1_ptr_, OnCandidateGathered(_))
      .WillOnce([&](const Candidate& c) {
        EXPECT_EQ(c.address(), candidate.address());
        callback_called = true;
        loop_.Quit();
      });

  conn1_->OnCandidateGathered(ice1_, candidate);

  loop_.Run();
  EXPECT_TRUE(callback_called);
}

TEST_F(DatagramConnectionTest, ObserverNotifiedOnConnectionError) {
  CreateConnections();

  bool callback_called = false;
  EXPECT_CALL(*observer1_ptr_, OnConnectionError()).WillOnce([&]() {
    callback_called = true;
    loop_.Quit();
  });

  ice1_->SetTransportState(webrtc::IceTransportState::kFailed,
                           webrtc::IceTransportStateInternal::STATE_FAILED);

  loop_.Run();
  EXPECT_TRUE(callback_called);
}

TEST_F(DatagramConnectionTest, DirectDtlsPacketsAreSent) {
  CreateConnections(WireProtocol::kDtls);
  Connect();
  ASSERT_TRUE(
      WaitUntil([&]() { return conn1_->Writable() && conn2_->Writable(); }));

  std::vector<uint8_t> data = {1, 2, 3, 4, 5};
  bool callback_called = false;
  EXPECT_CALL(*observer1_ptr_, OnSendOutcome(_))
      .WillOnce([&](const SendOutcome& outcome) {
        EXPECT_EQ(outcome.id, 1u);
        EXPECT_EQ(outcome.status, SendOutcome::Status::kSuccess);
        EXPECT_NE(outcome.send_time, Timestamp::MinusInfinity());
        callback_called = true;
        loop_.Quit();
      });
  std::vector<PacketSendParameters> packets = {
      PacketSendParameters{.id = 1, .payload = data}};
  conn1_->SendPackets(packets);
  // For direct DTLS, the sent packet should be larger than the data due to
  // DTLS overhead.
  EXPECT_GT(ice1_->last_sent_packet().size(), data.size());
  loop_.Run();
  EXPECT_TRUE(callback_called);
}

TEST_F(DatagramConnectionTest, DirectDtlsPacketsAreReceived) {
  CreateConnections(WireProtocol::kDtls);
  Connect();

  ASSERT_TRUE(
      WaitUntil([&]() { return conn1_->Writable() && conn2_->Writable(); }));

  std::vector<uint8_t> data = {1, 2, 3, 4, 5};
  struct Callbacks {
    bool packet_received = false;
    bool send_outcome = false;
  } callbacks;

  auto check_done = [&] {
    if (callbacks.packet_received && callbacks.send_outcome)
      loop_.Quit();
  };

  EXPECT_CALL(*observer2_ptr_, OnPacketReceived(_, _))
      .WillOnce(
          [&](std::span<const uint8_t> received_data,
              const DatagramConnection::Observer::PacketMetadata& metadata) {
            EXPECT_EQ(received_data.size(), data.size());
            EXPECT_EQ(memcmp(received_data.data(), data.data(), data.size()),
                      0);
            EXPECT_NE(metadata.receive_time, Timestamp::Zero());
            callbacks.packet_received = true;
            check_done();
          });

  EXPECT_CALL(*observer1_ptr_, OnSendOutcome(_))
      .WillOnce([&](const SendOutcome& outcome) {
        EXPECT_EQ(outcome.id, 1u);
        EXPECT_EQ(outcome.status, SendOutcome::Status::kSuccess);
        EXPECT_NE(outcome.send_time, Timestamp::MinusInfinity());
        callbacks.send_outcome = true;
        check_done();
      });

  std::vector<PacketSendParameters> packets = {
      PacketSendParameters{.id = 1, .payload = data}};
  conn1_->SendPackets(packets);
  loop_.Run();
  EXPECT_TRUE(callbacks.packet_received);
  EXPECT_TRUE(callbacks.send_outcome);
}

TEST_F(DatagramConnectionTest, SingleBytePacketsAreSent) {
  CreateConnections(WireProtocol::kDtls);
  Connect();
  ASSERT_TRUE(
      WaitUntil([&]() { return conn1_->Writable() && conn2_->Writable(); }));

  std::vector<uint8_t> data = {1};
  bool callback_called = false;
  EXPECT_CALL(*observer1_ptr_, OnSendOutcome(_))
      .WillOnce([&](const SendOutcome& outcome) {
        EXPECT_EQ(outcome.id, 1u);
        EXPECT_EQ(outcome.status, SendOutcome::Status::kSuccess);
        EXPECT_NE(outcome.send_time, Timestamp::MinusInfinity());
        callback_called = true;
        loop_.Quit();
      });
  std::vector<PacketSendParameters> packets = {
      PacketSendParameters{.id = 1, .payload = data}};
  conn1_->SendPackets(packets);
  // For direct DTLS, the sent packet should be larger than the data due to
  // DTLS overhead.
  EXPECT_GT(ice1_->last_sent_packet().size(), data.size());
  loop_.Run();
  EXPECT_TRUE(callback_called);
}

TEST_F(DatagramConnectionTest, DtlsWithFeedbackPacketsAreReceived) {
  CreateConnections(WireProtocol::kDtlsWithFeedback);
  Connect();

  ASSERT_TRUE(
      WaitUntil([&]() { return conn1_->Writable() && conn2_->Writable(); }));

  std::vector<uint8_t> data = {1, 2, 3, 4, 5};
  struct Callbacks {
    bool packet_received = false;
    bool send_outcome = false;
  } callbacks;

  auto check_done = [&] {
    if (callbacks.packet_received && callbacks.send_outcome)
      loop_.Quit();
  };

  // The app payload must be delivered intact, i.e. with the transport header
  // stripped off by the DtlsPacketProcessor.
  EXPECT_CALL(*observer2_ptr_, OnPacketReceived(_, _))
      .WillOnce(
          [&](std::span<const uint8_t> received_data,
              const DatagramConnection::Observer::PacketMetadata& metadata) {
            ASSERT_EQ(received_data.size(), data.size());
            EXPECT_EQ(memcmp(received_data.data(), data.data(), data.size()),
                      0);
            callbacks.packet_received = true;
            check_done();
          });

  EXPECT_CALL(*observer1_ptr_, OnSendOutcome(_))
      .WillOnce([&](const SendOutcome& outcome) {
        EXPECT_EQ(outcome.id, 1u);
        EXPECT_EQ(outcome.status, SendOutcome::Status::kSuccess);
        callbacks.send_outcome = true;
        check_done();
      });

  std::vector<PacketSendParameters> packets = {
      PacketSendParameters{.id = 1, .payload = data}};
  conn1_->SendPackets(packets);
  // The framed packet carries a transport header, so more than the payload is
  // put on the wire (plus DTLS overhead).
  EXPECT_GT(ice1_->last_sent_packet().size(), data.size());
  loop_.Run();
  EXPECT_TRUE(callbacks.packet_received);
  EXPECT_TRUE(callbacks.send_outcome);
}

TEST_F(DatagramConnectionTest, InternalSentPacketsAreIgnored) {
  CreateConnections();
  EXPECT_CALL(*observer1_ptr_, OnSendOutcome(_)).Times(0);

  SentPacketInfo internal_packet;
  internal_packet.packet_id = -1;

  conn1_->OnSentPacket(internal_packet);
}

TEST_F(DatagramConnectionTest, UserSentPacketsTriggerOnSendOutcome) {
  CreateConnections();
  bool callback_called = false;
  EXPECT_CALL(*observer1_ptr_, OnSendOutcome(_))
      .WillOnce([&](const SendOutcome& outcome) {
        EXPECT_EQ(outcome.id, 123u);
        EXPECT_EQ(outcome.status, SendOutcome::Status::kSuccess);
        EXPECT_EQ(outcome.send_time, Timestamp::Millis(456));
        EXPECT_EQ(outcome.bytes_sent, 100u);
        callback_called = true;
      });

  SentPacketInfo user_packet;
  user_packet.packet_id = 123;
  user_packet.send_time_ms = 456;
  user_packet.info.packet_size_bytes = 100;

  conn1_->OnSentPacket(user_packet);
  EXPECT_TRUE(callback_called);
}

}  // namespace
}  // namespace webrtc
