/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/dtls_srtp_transport.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/environment/environment.h"
#include "api/make_ref_counted.h"
#include "api/rtp_header_extension_id.h"
#include "api/transport/ecn_marking.h"
#include "api/units/timestamp.h"
#include "call/rtp_demuxer.h"
#include "media/base/fake_rtp.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/dtls/dtls_transport_internal.h"
#include "p2p/dtls/fake_dtls_transport.h"
#include "p2p/test/fake_ice_transport.h"
#include "pc/rtp_transport.h"
#include "pc/test/rtp_transport_test_util.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/buffer.h"
#include "rtc_base/byte_order.h"
#include "rtc_base/containers/flat_set.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/ssl_identity.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"

namespace webrtc {
namespace {

using ::testing::ElementsAre;

constexpr int kRtpAuthTagLen = 10;

class DtlsSrtpTransportTest : public ::testing::Test {
 protected:
  DtlsSrtpTransportTest() {}

  ~DtlsSrtpTransportTest() override {
    if (dtls_srtp_transport1_) {
      dtls_srtp_transport1_->UnregisterRtpDemuxerSink(&transport_observer1_);
    }
    if (dtls_srtp_transport2_) {
      dtls_srtp_transport2_->UnregisterRtpDemuxerSink(&transport_observer2_);
    }
  }

  std::unique_ptr<DtlsSrtpTransport> MakeDtlsSrtpTransport(
      FakeDtlsTransport* rtp_dtls,
      FakeDtlsTransport* rtcp_dtls,
      bool rtcp_mux_enabled) {
    auto dtls_srtp_transport = std::make_unique<DtlsSrtpTransport>(
        rtcp_mux_enabled, env_.field_trials());

    dtls_srtp_transport->SetDtlsTransports(rtp_dtls, rtcp_dtls);

    return dtls_srtp_transport;
  }

  void MakeDtlsSrtpTransports(FakeDtlsTransport* rtp_dtls1,
                              FakeDtlsTransport* rtcp_dtls1,
                              FakeDtlsTransport* rtp_dtls2,
                              FakeDtlsTransport* rtcp_dtls2,
                              bool rtcp_mux_enabled) {
    dtls_srtp_transport1_ =
        MakeDtlsSrtpTransport(rtp_dtls1, rtcp_dtls1, rtcp_mux_enabled);
    dtls_srtp_transport2_ =
        MakeDtlsSrtpTransport(rtp_dtls2, rtcp_dtls2, rtcp_mux_enabled);

    dtls_srtp_transport1_->SubscribeRtcpPacketReceived(
        &transport_observer1_,
        [this](CopyOnWriteBuffer packet, std::optional<Timestamp> arrival_time,
               EcnMarking ecn) {
          transport_observer1_.OnRtcpPacketReceived(std::move(packet),
                                                    arrival_time, ecn);
        });
    dtls_srtp_transport1_->SubscribeReadyToSend(
        &transport_observer1_,
        [this](bool ready) { transport_observer1_.OnReadyToSend(ready); });

    dtls_srtp_transport2_->SubscribeRtcpPacketReceived(
        &transport_observer2_,
        [this](CopyOnWriteBuffer packet, std::optional<Timestamp> arrival_time,
               EcnMarking ecn) {
          transport_observer2_.OnRtcpPacketReceived(std::move(packet),
                                                    arrival_time, ecn);
        });
    dtls_srtp_transport2_->SubscribeReadyToSend(
        &transport_observer2_,
        [this](bool ready) { transport_observer2_.OnReadyToSend(ready); });
    RtpDemuxerCriteria demuxer_criteria;
    // 0x00 is the payload type used in kPcmuFrame.
    demuxer_criteria.payload_types() = {0x00};
    dtls_srtp_transport1_->RegisterRtpDemuxerSink(demuxer_criteria,
                                                  &transport_observer1_);
    dtls_srtp_transport2_->RegisterRtpDemuxerSink(demuxer_criteria,
                                                  &transport_observer2_);
  }

  void CompleteDtlsHandshake(FakeDtlsTransport* fake_dtls1,
                             FakeDtlsTransport* fake_dtls2) {
    auto cert1 =
        RTCCertificate::Create(SSLIdentity::Create("session1", KT_DEFAULT));
    fake_dtls1->SetLocalCertificate(cert1);
    auto cert2 =
        RTCCertificate::Create(SSLIdentity::Create("session1", KT_DEFAULT));
    fake_dtls2->SetLocalCertificate(cert2);
    fake_dtls1->SetDestination(fake_dtls2);
  }

  void SendRecvRtpPackets() {
    ASSERT_TRUE(dtls_srtp_transport1_);
    ASSERT_TRUE(dtls_srtp_transport2_);
    ASSERT_TRUE(dtls_srtp_transport1_->IsSrtpActive());
    ASSERT_TRUE(dtls_srtp_transport2_->IsSrtpActive());

    size_t rtp_len = sizeof(kPcmuFrame);
    size_t packet_size = rtp_len + kRtpAuthTagLen;
    Buffer rtp_packet_buffer = Buffer::CreateUninitializedWithSize(packet_size);
    char* rtp_packet_data = rtp_packet_buffer.data<char>();
    memcpy(rtp_packet_data, kPcmuFrame, rtp_len);
    // In order to be able to run this test function multiple times we can not
    // use the same sequence number twice. Increase the sequence number by one.
    SetBE16(std::span<uint8_t>(rtp_packet_buffer).subspan(2),
            ++sequence_number_);
    CopyOnWriteBuffer rtp_packet1to2(rtp_packet_data, rtp_len, packet_size);
    CopyOnWriteBuffer rtp_packet2to1(rtp_packet_data, rtp_len, packet_size);

    AsyncSocketPacketOptions options;
    // Send a packet from `srtp_transport1_` to `srtp_transport2_` and verify
    // that the packet can be successfully received and decrypted.
    int prev_received_packets = transport_observer2_.rtp_count();
    ASSERT_TRUE(dtls_srtp_transport1_->SendRtpPacket(&rtp_packet1to2, options,
                                                     PF_SRTP_BYPASS));
    ASSERT_TRUE(transport_observer2_.last_recv_rtp_packet().data());
    EXPECT_EQ(0, memcmp(transport_observer2_.last_recv_rtp_packet().data(),
                        kPcmuFrame, rtp_len));
    EXPECT_EQ(prev_received_packets + 1, transport_observer2_.rtp_count());

    prev_received_packets = transport_observer1_.rtp_count();
    ASSERT_TRUE(dtls_srtp_transport2_->SendRtpPacket(&rtp_packet2to1, options,
                                                     PF_SRTP_BYPASS));
    ASSERT_TRUE(transport_observer1_.last_recv_rtp_packet().data());
    EXPECT_EQ(0, memcmp(transport_observer1_.last_recv_rtp_packet().data(),
                        kPcmuFrame, rtp_len));
    EXPECT_EQ(prev_received_packets + 1, transport_observer1_.rtp_count());
  }

  void SendRecvRtcpPackets() {
    size_t rtcp_len = sizeof(kFakeRtcpReport);
    size_t packet_size = rtcp_len + 4 + kRtpAuthTagLen;
    Buffer rtcp_packet_buffer =
        Buffer::CreateUninitializedWithSize(packet_size);

    // TODO(zhihuang): Remove the extra copy when the SendRtpPacket method
    // doesn't take the CopyOnWriteBuffer by pointer.
    CopyOnWriteBuffer rtcp_packet1to2(kFakeRtcpReport, rtcp_len, packet_size);
    CopyOnWriteBuffer rtcp_packet2to1(kFakeRtcpReport, rtcp_len, packet_size);

    AsyncSocketPacketOptions options;
    // Send a packet from `srtp_transport1_` to `srtp_transport2_` and verify
    // that the packet can be successfully received and decrypted.
    int prev_received_packets = transport_observer2_.rtcp_count();
    ASSERT_TRUE(dtls_srtp_transport1_->SendRtcpPacket(&rtcp_packet1to2, options,
                                                      PF_SRTP_BYPASS));
    ASSERT_TRUE(transport_observer2_.last_recv_rtcp_packet().data());
    EXPECT_EQ(0, memcmp(transport_observer2_.last_recv_rtcp_packet().data(),
                        kFakeRtcpReport, rtcp_len));
    EXPECT_EQ(prev_received_packets + 1, transport_observer2_.rtcp_count());

    // Do the same thing in the opposite direction;
    prev_received_packets = transport_observer1_.rtcp_count();
    ASSERT_TRUE(dtls_srtp_transport2_->SendRtcpPacket(&rtcp_packet2to1, options,
                                                      PF_SRTP_BYPASS));
    ASSERT_TRUE(transport_observer1_.last_recv_rtcp_packet().data());
    EXPECT_EQ(0, memcmp(transport_observer1_.last_recv_rtcp_packet().data(),
                        kFakeRtcpReport, rtcp_len));
    EXPECT_EQ(prev_received_packets + 1, transport_observer1_.rtcp_count());
  }

  void SendRecvRtpPacketsWithHeaderExtension(
      const std::vector<RtpHeaderExtensionId>& encrypted_header_ids) {
    ASSERT_TRUE(dtls_srtp_transport1_);
    ASSERT_TRUE(dtls_srtp_transport2_);
    ASSERT_TRUE(dtls_srtp_transport1_->IsSrtpActive());
    ASSERT_TRUE(dtls_srtp_transport2_->IsSrtpActive());

    size_t rtp_len = sizeof(kPcmuFrameWithExtensions);
    size_t packet_size = rtp_len + kRtpAuthTagLen;
    Buffer rtp_packet_buffer = Buffer::CreateUninitializedWithSize(packet_size);
    char* rtp_packet_data = rtp_packet_buffer.data<char>();
    memcpy(rtp_packet_data, kPcmuFrameWithExtensions, rtp_len);
    // In order to be able to run this test function multiple times we can not
    // use the same sequence number twice. Increase the sequence number by one.
    SetBE16(std::span<uint8_t>(rtp_packet_buffer).subspan(2),
            ++sequence_number_);
    CopyOnWriteBuffer rtp_packet1to2(rtp_packet_data, rtp_len, packet_size);
    CopyOnWriteBuffer rtp_packet2to1(rtp_packet_data, rtp_len, packet_size);

    uint8_t original_rtp_data[sizeof(kPcmuFrameWithExtensions)];
    memcpy(original_rtp_data, rtp_packet_data, rtp_len);

    AsyncSocketPacketOptions options;
    // Send a packet from `srtp_transport1_` to `srtp_transport2_` and verify
    // that the packet can be successfully received and decrypted.
    ASSERT_TRUE(dtls_srtp_transport1_->SendRtpPacket(&rtp_packet1to2, options,
                                                     PF_SRTP_BYPASS));
    ASSERT_TRUE(transport_observer2_.last_recv_rtp_packet().data());
    EXPECT_EQ(0, memcmp(transport_observer2_.last_recv_rtp_packet().data(),
                        original_rtp_data, rtp_len));
    // Get the encrypted packet from underneath packet transport and verify the
    // data and header extension are actually encrypted.
    auto fake_dtls_transport = static_cast<FakeDtlsTransport*>(
        dtls_srtp_transport1_->rtp_packet_transport());
    auto fake_ice_transport = static_cast<FakeIceTransportInternal*>(
        fake_dtls_transport->ice_transport());
    EXPECT_NE(0, memcmp(fake_ice_transport->last_sent_packet().data(),
                        original_rtp_data, rtp_len));
    CompareHeaderExtensions(fake_ice_transport->last_sent_packet(),
                            original_rtp_data, encrypted_header_ids, false);

    // Do the same thing in the opposite direction.
    ASSERT_TRUE(dtls_srtp_transport2_->SendRtpPacket(&rtp_packet2to1, options,
                                                     PF_SRTP_BYPASS));
    ASSERT_TRUE(transport_observer1_.last_recv_rtp_packet().data());
    EXPECT_EQ(0, memcmp(transport_observer1_.last_recv_rtp_packet().data(),
                        original_rtp_data, rtp_len));
    // Get the encrypted packet from underneath packet transport and verify the
    // data and header extension are actually encrypted.
    fake_dtls_transport = static_cast<FakeDtlsTransport*>(
        dtls_srtp_transport2_->rtp_packet_transport());
    fake_ice_transport = static_cast<FakeIceTransportInternal*>(
        fake_dtls_transport->ice_transport());
    EXPECT_NE(0, memcmp(fake_ice_transport->last_sent_packet().data(),
                        original_rtp_data, rtp_len));
    CompareHeaderExtensions(fake_ice_transport->last_sent_packet(),
                            original_rtp_data, encrypted_header_ids, false);
  }

  void SendRecvPackets() {
    SendRecvRtpPackets();
    SendRecvRtcpPackets();
  }

  std::unique_ptr<FakeDtlsTransport> CreateFakeDtlsTransport(
      absl::string_view name,
      int component) {
    return std::make_unique<FakeDtlsTransport>(
        make_ref_counted<FakeIceTransport>(
            std::make_unique<FakeIceTransportInternal>(env_, name, component)));
  }

  test::RunLoop main_thread_;
  const Environment env_ = CreateTestEnvironment();
  std::unique_ptr<DtlsSrtpTransport> dtls_srtp_transport1_;
  std::unique_ptr<DtlsSrtpTransport> dtls_srtp_transport2_;
  TransportObserver transport_observer1_;
  TransportObserver transport_observer2_;

  int sequence_number_ = 0;
};

// Tests that if RTCP muxing is enabled and transports are set after RTP
// transport finished the handshake, SRTP is set up.
TEST_F(DtlsSrtpTransportTest, SetTransportsAfterHandshakeCompleteWithRtcpMux) {
  std::unique_ptr<FakeDtlsTransport> rtp_dtls1 =
      CreateFakeDtlsTransport("video", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtp_dtls2 =
      CreateFakeDtlsTransport("video", ICE_CANDIDATE_COMPONENT_RTP);

  MakeDtlsSrtpTransports(rtp_dtls1.get(), nullptr, rtp_dtls2.get(), nullptr,
                         /*rtcp_mux_enabled=*/true);

  std::unique_ptr<FakeDtlsTransport> rtp_dtls3 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtp_dtls4 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);

  CompleteDtlsHandshake(rtp_dtls3.get(), rtp_dtls4.get());

  dtls_srtp_transport1_->SetDtlsTransports(rtp_dtls3.get(), nullptr);
  dtls_srtp_transport2_->SetDtlsTransports(rtp_dtls4.get(), nullptr);

  SendRecvPackets();
}

// Tests that if RTCP muxing is not enabled and transports are set after both
// RTP and RTCP transports finished the handshake, SRTP is set up.
TEST_F(DtlsSrtpTransportTest,
       SetTransportsAfterHandshakeCompleteWithoutRtcpMux) {
  std::unique_ptr<FakeDtlsTransport> rtp_dtls1 =
      CreateFakeDtlsTransport("video", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtcp_dtls1 =
      CreateFakeDtlsTransport("video", ICE_CANDIDATE_COMPONENT_RTCP);
  std::unique_ptr<FakeDtlsTransport> rtp_dtls2 =
      CreateFakeDtlsTransport("video", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtcp_dtls2 =
      CreateFakeDtlsTransport("video", ICE_CANDIDATE_COMPONENT_RTCP);

  MakeDtlsSrtpTransports(rtp_dtls1.get(), rtcp_dtls1.get(), rtp_dtls2.get(),
                         rtcp_dtls2.get(), /*rtcp_mux_enabled=*/false);

  std::unique_ptr<FakeDtlsTransport> rtp_dtls3 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtcp_dtls3 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTCP);
  std::unique_ptr<FakeDtlsTransport> rtp_dtls4 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtcp_dtls4 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTCP);

  CompleteDtlsHandshake(rtp_dtls3.get(), rtp_dtls4.get());
  CompleteDtlsHandshake(rtcp_dtls3.get(), rtcp_dtls4.get());

  dtls_srtp_transport1_->SetDtlsTransports(rtp_dtls3.get(), rtcp_dtls3.get());
  dtls_srtp_transport2_->SetDtlsTransports(rtp_dtls4.get(), rtcp_dtls4.get());

  SendRecvPackets();
}

// Tests if RTCP muxing is enabled, SRTP is set up as soon as the RTP DTLS
// handshake is finished.
TEST_F(DtlsSrtpTransportTest, SetTransportsBeforeHandshakeCompleteWithRtcpMux) {
  std::unique_ptr<FakeDtlsTransport> rtp_dtls1 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtcp_dtls1 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTCP);
  std::unique_ptr<FakeDtlsTransport> rtp_dtls2 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtcp_dtls2 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTCP);

  MakeDtlsSrtpTransports(rtp_dtls1.get(), rtcp_dtls1.get(), rtp_dtls2.get(),
                         rtcp_dtls2.get(),
                         /*rtcp_mux_enabled=*/false);

  dtls_srtp_transport1_->SetRtcpMuxEnabled(true);
  dtls_srtp_transport2_->SetRtcpMuxEnabled(true);
  CompleteDtlsHandshake(rtp_dtls1.get(), rtp_dtls2.get());
  SendRecvPackets();
}

// Tests if RTCP muxing is not enabled, SRTP is set up when both the RTP and
// RTCP DTLS handshake are finished.
TEST_F(DtlsSrtpTransportTest,
       SetTransportsBeforeHandshakeCompleteWithoutRtcpMux) {
  std::unique_ptr<FakeDtlsTransport> rtp_dtls1 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtcp_dtls1 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTCP);
  std::unique_ptr<FakeDtlsTransport> rtp_dtls2 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtcp_dtls2 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTCP);

  MakeDtlsSrtpTransports(rtp_dtls1.get(), rtcp_dtls1.get(), rtp_dtls2.get(),
                         rtcp_dtls2.get(), /*rtcp_mux_enabled=*/false);

  CompleteDtlsHandshake(rtp_dtls1.get(), rtp_dtls2.get());
  EXPECT_FALSE(dtls_srtp_transport1_->IsSrtpActive());
  EXPECT_FALSE(dtls_srtp_transport2_->IsSrtpActive());
  CompleteDtlsHandshake(rtcp_dtls1.get(), rtcp_dtls2.get());
  SendRecvPackets();
}

// Tests that if the DtlsTransport underneath is changed, the previous DTLS-SRTP
// context will be reset and will be re-setup once the new transports' handshake
// complete.
TEST_F(DtlsSrtpTransportTest, DtlsSrtpResetAfterDtlsTransportChange) {
  std::unique_ptr<FakeDtlsTransport> rtp_dtls1 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtp_dtls2 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);

  MakeDtlsSrtpTransports(rtp_dtls1.get(), nullptr, rtp_dtls2.get(), nullptr,
                         /*rtcp_mux_enabled=*/true);

  CompleteDtlsHandshake(rtp_dtls1.get(), rtp_dtls2.get());
  EXPECT_TRUE(dtls_srtp_transport1_->IsSrtpActive());
  EXPECT_TRUE(dtls_srtp_transport2_->IsSrtpActive());

  auto rtp_ice3 = std::make_unique<FakeIceTransportInternal>(
      env_, "audio", ICE_CANDIDATE_COMPONENT_RTP);
  auto rtp_dtls3 = std::make_unique<FakeDtlsTransport>(
      make_ref_counted<FakeIceTransport>(std::move(rtp_ice3)));
  auto rtp_ice4 = std::make_unique<FakeIceTransportInternal>(
      env_, "audio", ICE_CANDIDATE_COMPONENT_RTP);
  auto rtp_dtls4 = std::make_unique<FakeDtlsTransport>(
      make_ref_counted<FakeIceTransport>(std::move(rtp_ice4)));

  // The previous context is reset.
  dtls_srtp_transport1_->SetDtlsTransports(rtp_dtls3.get(), nullptr);
  dtls_srtp_transport2_->SetDtlsTransports(rtp_dtls4.get(), nullptr);
  EXPECT_FALSE(dtls_srtp_transport1_->IsSrtpActive());
  EXPECT_FALSE(dtls_srtp_transport2_->IsSrtpActive());

  // Re-setup.
  CompleteDtlsHandshake(rtp_dtls3.get(), rtp_dtls4.get());
  SendRecvPackets();
}

// Tests if only the RTP DTLS handshake complete, and then RTCP muxing is
// enabled, SRTP is set up.
TEST_F(DtlsSrtpTransportTest,
       RtcpMuxEnabledAfterRtpTransportHandshakeComplete) {
  std::unique_ptr<FakeDtlsTransport> rtp_dtls1 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtcp_dtls1 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTCP);
  std::unique_ptr<FakeDtlsTransport> rtp_dtls2 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtcp_dtls2 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTCP);

  MakeDtlsSrtpTransports(rtp_dtls1.get(), rtcp_dtls1.get(), rtp_dtls2.get(),
                         rtcp_dtls2.get(), /*rtcp_mux_enabled=*/false);

  CompleteDtlsHandshake(rtp_dtls1.get(), rtp_dtls2.get());
  // Inactive because the RTCP transport handshake didn't complete.
  EXPECT_FALSE(dtls_srtp_transport1_->IsSrtpActive());
  EXPECT_FALSE(dtls_srtp_transport2_->IsSrtpActive());

  dtls_srtp_transport1_->SetRtcpMuxEnabled(true);
  dtls_srtp_transport2_->SetRtcpMuxEnabled(true);
  // The transports should be active and be able to send packets when the
  // RTCP muxing is enabled.
  SendRecvPackets();
}

// Tests that when SetSend/RecvEncryptedHeaderExtensionIds is called, the SRTP
// sessions are updated with new encryped header extension IDs immediately.
TEST_F(DtlsSrtpTransportTest, EncryptedHeaderExtensionIdUpdated) {
  std::unique_ptr<FakeDtlsTransport> rtp_dtls1 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtp_dtls2 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);

  MakeDtlsSrtpTransports(rtp_dtls1.get(), nullptr, rtp_dtls2.get(), nullptr,
                         /*rtcp_mux_enabled=*/true);
  CompleteDtlsHandshake(rtp_dtls1.get(), rtp_dtls2.get());

  std::vector<RtpHeaderExtensionId> encrypted_headers;
  encrypted_headers.push_back(kHeaderExtensionIDs[0]);
  encrypted_headers.push_back(kHeaderExtensionIDs[1]);

  dtls_srtp_transport1_->UpdateSendEncryptedHeaderExtensionIds(
      encrypted_headers);
  dtls_srtp_transport1_->UpdateRecvEncryptedHeaderExtensionIds(
      encrypted_headers);
  dtls_srtp_transport2_->UpdateSendEncryptedHeaderExtensionIds(
      encrypted_headers);
  dtls_srtp_transport2_->UpdateRecvEncryptedHeaderExtensionIds(
      encrypted_headers);
}

TEST_F(DtlsSrtpTransportTest, WritableStateChangesAfterSrtpActivation) {
  std::unique_ptr<FakeDtlsTransport> rtp_dtls1 =
      CreateFakeDtlsTransport("video", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtp_dtls2 =
      CreateFakeDtlsTransport("video", ICE_CANDIDATE_COMPONENT_RTP);
  MakeDtlsSrtpTransports(rtp_dtls1.get(), nullptr, rtp_dtls2.get(), nullptr,
                         /*rtcp_mux_enabled=*/true);
  std::vector<bool> writable_states;
  dtls_srtp_transport1_->SubscribeWritableState(
      &writable_states,
      [&](bool writable) { writable_states.push_back(writable); });

  CompleteDtlsHandshake(rtp_dtls1.get(), rtp_dtls2.get());
  EXPECT_THAT(writable_states, ElementsAre(true));
  rtp_dtls1->SetWritable(false);
  EXPECT_TRUE(dtls_srtp_transport1_->IsSrtpActive());
  EXPECT_FALSE(dtls_srtp_transport1_->IsWritable(/*rtcp=*/false));
  EXPECT_THAT(writable_states, ElementsAre(true, false));

  // A media channel attached during the outage must see the recovery edge.
  std::vector<bool> late_subscriber_states;
  dtls_srtp_transport1_->SubscribeWritableState(
      &late_subscriber_states,
      [&](bool writable) { late_subscriber_states.push_back(writable); });
  rtp_dtls1->SetWritable(true);
  EXPECT_THAT(writable_states, ElementsAre(true, false, true));
  EXPECT_THAT(late_subscriber_states, ElementsAre(true));
  SendRecvPackets();
  dtls_srtp_transport1_->UnsubscribeWritableState(&late_subscriber_states);
  dtls_srtp_transport1_->UnsubscribeWritableState(&writable_states);
}

TEST_F(DtlsSrtpTransportTest, WritableStateRequiresRtpAndRtcpRecovery) {
  std::unique_ptr<FakeDtlsTransport> rtp_dtls1 =
      CreateFakeDtlsTransport("video", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtp_dtls2 =
      CreateFakeDtlsTransport("video", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtcp_dtls1 =
      CreateFakeDtlsTransport("video", ICE_CANDIDATE_COMPONENT_RTCP);
  std::unique_ptr<FakeDtlsTransport> rtcp_dtls2 =
      CreateFakeDtlsTransport("video", ICE_CANDIDATE_COMPONENT_RTCP);
  MakeDtlsSrtpTransports(rtp_dtls1.get(), rtcp_dtls1.get(), rtp_dtls2.get(),
                         rtcp_dtls2.get(), /*rtcp_mux_enabled=*/false);
  CompleteDtlsHandshake(rtp_dtls1.get(), rtp_dtls2.get());
  CompleteDtlsHandshake(rtcp_dtls1.get(), rtcp_dtls2.get());
  std::vector<bool> writable_states;
  dtls_srtp_transport1_->SubscribeWritableState(
      &writable_states,
      [&](bool writable) { writable_states.push_back(writable); });

  rtp_dtls1->SetWritable(false);
  rtcp_dtls1->SetWritable(false);
  EXPECT_THAT(writable_states, ElementsAre(false));
  rtp_dtls1->SetWritable(true);
  EXPECT_THAT(writable_states, ElementsAre(false));
  rtcp_dtls1->SetWritable(true);
  EXPECT_THAT(writable_states, ElementsAre(false, true));
  SendRecvPackets();
  dtls_srtp_transport1_->UnsubscribeWritableState(&writable_states);
}

// Tests if RTCP muxing is enabled. DtlsSrtpTransport is ready to send once the
// RTP DtlsTransport is ready.
TEST_F(DtlsSrtpTransportTest, SignalReadyToSendFiredWithRtcpMux) {
  std::unique_ptr<FakeDtlsTransport> rtp_dtls1 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtp_dtls2 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);

  MakeDtlsSrtpTransports(rtp_dtls1.get(), nullptr, rtp_dtls2.get(), nullptr,
                         /*rtcp_mux_enabled=*/true);

  rtp_dtls1->SetDestination(rtp_dtls2.get());
  EXPECT_TRUE(transport_observer1_.ready_to_send());
  EXPECT_TRUE(transport_observer2_.ready_to_send());
}

// Tests if RTCP muxing is not enabled. DtlsSrtpTransport is ready to send once
// both the RTP and RTCP DtlsTransport are ready.
TEST_F(DtlsSrtpTransportTest, SignalReadyToSendFiredWithoutRtcpMux) {
  std::unique_ptr<FakeDtlsTransport> rtp_dtls1 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtcp_dtls1 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTCP);
  std::unique_ptr<FakeDtlsTransport> rtp_dtls2 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtcp_dtls2 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTCP);

  MakeDtlsSrtpTransports(rtp_dtls1.get(), rtcp_dtls1.get(), rtp_dtls2.get(),
                         rtcp_dtls2.get(), /*rtcp_mux_enabled=*/false);

  rtp_dtls1->SetDestination(rtp_dtls2.get());
  EXPECT_FALSE(transport_observer1_.ready_to_send());
  EXPECT_FALSE(transport_observer2_.ready_to_send());

  rtcp_dtls1->SetDestination(rtcp_dtls2.get());
  EXPECT_TRUE(transport_observer1_.ready_to_send());
  EXPECT_TRUE(transport_observer2_.ready_to_send());
}

// Test that if an endpoint "fully" enables RTCP mux, setting the RTCP
// transport to null, it *doesn't* reset its SRTP context. That would cause the
// ROC and SRTCP index to be reset, causing replay detection and other errors
// when attempting to unprotect packets.
// Regression test for bugs.webrtc.org/8996
TEST_F(DtlsSrtpTransportTest, SrtpSessionNotResetWhenRtcpTransportRemoved) {
  std::unique_ptr<FakeDtlsTransport> rtp_dtls1 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtcp_dtls1 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTCP);
  std::unique_ptr<FakeDtlsTransport> rtp_dtls2 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTP);
  std::unique_ptr<FakeDtlsTransport> rtcp_dtls2 =
      CreateFakeDtlsTransport("audio", ICE_CANDIDATE_COMPONENT_RTCP);

  MakeDtlsSrtpTransports(rtp_dtls1.get(), rtcp_dtls1.get(), rtp_dtls2.get(),
                         rtcp_dtls2.get(), /*rtcp_mux_enabled=*/true);
  CompleteDtlsHandshake(rtp_dtls1.get(), rtp_dtls2.get());
  CompleteDtlsHandshake(rtcp_dtls1.get(), rtcp_dtls2.get());

  // Send some RTCP packets, causing the SRTCP index to be incremented.
  SendRecvRtcpPackets();

  // Set RTCP transport to null, which previously would trigger this problem.
  dtls_srtp_transport1_->SetDtlsTransports(rtp_dtls1.get(), nullptr);

  // Attempt to send more RTCP packets. If the issue occurred, one side would
  // reset its context while the other would not, causing replay detection
  // errors when a packet with a duplicate SRTCP index is received.
  SendRecvRtcpPackets();
}

}  // namespace
}  // namespace webrtc
