/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/srtp_transport.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "api/environment/environment.h"
#include "api/rtp_header_extension_id.h"
#include "api/transport/ecn_marking.h"
#include "api/units/timestamp.h"
#include "call/rtp_demuxer.h"
#include "media/base/fake_rtp.h"
#include "p2p/dtls/dtls_transport_internal.h"
#include "p2p/test/fake_packet_transport.h"
#include "pc/test/rtp_transport_test_util.h"
#include "pc/test/srtp_test_util.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/buffer.h"
#include "rtc_base/byte_order.h"
#include "rtc_base/containers/flat_set.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/network_route.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "test/create_test_environment.h"
#include "test/gtest.h"
#include "test/run_loop.h"

using ::webrtc::kSrtpAeadAes128Gcm;
using ::webrtc::kTestKey1;
using ::webrtc::kTestKey2;

namespace webrtc {
// 128 bits key + 96 bits salt.
static const ZeroOnFreeBuffer<uint8_t> kTestKeyGcm128_1{
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ12", 28};
static const ZeroOnFreeBuffer<uint8_t> kTestKeyGcm128_2{
    "21ZYXWVUTSRQPONMLKJIHGFEDCBA", 28};
// 256 bits key + 96 bits salt.
static const ZeroOnFreeBuffer<uint8_t> kTestKeyGcm256_1{
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqr", 44};
static const ZeroOnFreeBuffer<uint8_t> kTestKeyGcm256_2{
    "rqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA", 44};

class SrtpTransportTest : public ::testing::Test {
 protected:
  SrtpTransportTest() {
    bool rtcp_mux_enabled = true;

    rtp_packet_transport1_ =
        std::make_unique<FakePacketTransport>(env_, "fake_packet_transport1");
    rtp_packet_transport2_ =
        std::make_unique<FakePacketTransport>(env_, "fake_packet_transport2");

    bool asymmetric = false;
    rtp_packet_transport1_->SetDestination(rtp_packet_transport2_.get(),
                                           asymmetric);

    srtp_transport1_ =
        std::make_unique<SrtpTransport>(rtcp_mux_enabled, env_.field_trials());
    srtp_transport2_ =
        std::make_unique<SrtpTransport>(rtcp_mux_enabled, env_.field_trials());

    srtp_transport1_->SetRtpPacketTransport(rtp_packet_transport1_.get());
    srtp_transport2_->SetRtpPacketTransport(rtp_packet_transport2_.get());

    srtp_transport1_->SubscribeRtcpPacketReceived(
        &rtp_sink1_,
        [this](CopyOnWriteBuffer packet, std::optional<Timestamp> arrival_time,
               EcnMarking ecn) {
          rtp_sink1_.OnRtcpPacketReceived(std::move(packet), arrival_time, ecn);
        });
    srtp_transport2_->SubscribeRtcpPacketReceived(
        &rtp_sink2_,
        [this](CopyOnWriteBuffer packet, std::optional<Timestamp> arrival_time,
               EcnMarking ecn) {
          rtp_sink2_.OnRtcpPacketReceived(std::move(packet), arrival_time, ecn);
        });

    RtpDemuxerCriteria demuxer_criteria;
    // 0x00 is the payload type used in kPcmuFrame.
    demuxer_criteria.payload_types().insert(0x00);

    srtp_transport1_->RegisterRtpDemuxerSink(demuxer_criteria, &rtp_sink1_);
    srtp_transport2_->RegisterRtpDemuxerSink(demuxer_criteria, &rtp_sink2_);
  }

  ~SrtpTransportTest() override {
    if (srtp_transport1_) {
      srtp_transport1_->UnregisterRtpDemuxerSink(&rtp_sink1_);
    }
    if (srtp_transport2_) {
      srtp_transport2_->UnregisterRtpDemuxerSink(&rtp_sink2_);
    }
  }

  void TestSendRecvRtpPacket(int crypto_suite) {
    CopyOnWriteBuffer original_rtp_data(
        kPcmuFrame,
        /*capacity=*/sizeof(kPcmuFrame) + rtp_auth_tag_len(crypto_suite));
    // In order to be able to run this test function multiple times we can not
    // use the same sequence number twice. Increase the sequence number by one.
    SetBE16(original_rtp_data.AsMutableSpan().subspan(2), ++sequence_number_);
    CopyOnWriteBuffer rtp_packet1to2 = original_rtp_data;
    CopyOnWriteBuffer rtp_packet2to1 = original_rtp_data;

    AsyncSocketPacketOptions options;
    // Send a packet from `srtp_transport1_` to `srtp_transport2_` and verify
    // that the packet can be successfully received and decrypted.
    ASSERT_TRUE(srtp_transport1_->SendRtpPacket(&rtp_packet1to2, options,
                                                PF_SRTP_BYPASS));
    EXPECT_EQ(rtp_sink2_.last_recv_rtp_packet().Buffer(), original_rtp_data);
    // Get the encrypted packet from underneath packet transport and verify
    // the data is actually encrypted.
    auto fake_rtp_packet_transport = static_cast<FakePacketTransport*>(
        srtp_transport1_->rtp_packet_transport());
    EXPECT_NE(*fake_rtp_packet_transport->last_sent_packet(),
              original_rtp_data);

    // Do the same thing in the opposite direction;
    ASSERT_TRUE(srtp_transport2_->SendRtpPacket(&rtp_packet2to1, options,
                                                PF_SRTP_BYPASS));
    EXPECT_EQ(rtp_sink1_.last_recv_rtp_packet().Buffer(), original_rtp_data);
    fake_rtp_packet_transport = static_cast<FakePacketTransport*>(
        srtp_transport2_->rtp_packet_transport());
    EXPECT_NE(*fake_rtp_packet_transport->last_sent_packet(),
              original_rtp_data);
  }

  void TestSendRecvRtcpPacket(int crypto_suite) {
    CopyOnWriteBuffer rtcp_packet_data(
        kFakeRtcpReport, /*capacity=*/sizeof(kFakeRtcpReport) + 4 +
                             rtcp_auth_tag_len(crypto_suite));

    CopyOnWriteBuffer rtcp_packet1to2 = rtcp_packet_data;
    CopyOnWriteBuffer rtcp_packet2to1 = rtcp_packet_data;

    AsyncSocketPacketOptions options;
    // Send a packet from `srtp_transport1_` to `srtp_transport2_` and verify
    // that the packet can be successfully received and decrypted.
    ASSERT_TRUE(srtp_transport1_->SendRtcpPacket(&rtcp_packet1to2, options,
                                                 PF_SRTP_BYPASS));
    EXPECT_EQ(rtp_sink2_.last_recv_rtcp_packet(), rtcp_packet_data);
    // Get the encrypted packet from underneath packet transport and verify the
    // data is actually encrypted.
    auto fake_rtp_packet_transport = static_cast<FakePacketTransport*>(
        srtp_transport1_->rtp_packet_transport());
    EXPECT_NE(*fake_rtp_packet_transport->last_sent_packet(), rtcp_packet_data);

    // Do the same thing in the opposite direction;
    ASSERT_TRUE(srtp_transport2_->SendRtcpPacket(&rtcp_packet2to1, options,
                                                 PF_SRTP_BYPASS));
    EXPECT_EQ(rtp_sink1_.last_recv_rtcp_packet(), rtcp_packet_data);
    fake_rtp_packet_transport = static_cast<FakePacketTransport*>(
        srtp_transport2_->rtp_packet_transport());
    EXPECT_NE(*fake_rtp_packet_transport->last_sent_packet(), rtcp_packet_data);
  }

  void TestSendRecvPacket(int crypto_suite,
                          const ZeroOnFreeBuffer<uint8_t>& key1,
                          const ZeroOnFreeBuffer<uint8_t>& key2) {
    EXPECT_EQ(key1.size(), key2.size());
    std::vector<RtpHeaderExtensionId> extension_ids;
    EXPECT_TRUE(srtp_transport1_->SetRtpParams(
        crypto_suite, key1, extension_ids, crypto_suite, key2, extension_ids));
    EXPECT_TRUE(srtp_transport2_->SetRtpParams(
        crypto_suite, key2, extension_ids, crypto_suite, key1, extension_ids));
    EXPECT_TRUE(srtp_transport1_->SetRtcpParams(
        crypto_suite, key1, extension_ids, crypto_suite, key2, extension_ids));
    EXPECT_TRUE(srtp_transport2_->SetRtcpParams(
        crypto_suite, key2, extension_ids, crypto_suite, key1, extension_ids));
    EXPECT_TRUE(srtp_transport1_->IsSrtpActive());
    EXPECT_TRUE(srtp_transport2_->IsSrtpActive());
    TestSendRecvRtpPacket(crypto_suite);
    TestSendRecvRtcpPacket(crypto_suite);
  }

  void TestSendRecvPacketWithEncryptedHeaderExtension(
      int crypto_suite,
      const std::vector<RtpHeaderExtensionId>& encrypted_header_ids) {
    CopyOnWriteBuffer original_rtp_data(
        kPcmuFrameWithExtensions,
        /*capacity=*/sizeof(kPcmuFrameWithExtensions) +
            rtp_auth_tag_len(crypto_suite));
    // In order to be able to run this test function multiple times we can not
    // use the same sequence number twice. Increase the sequence number by one.
    SetBE16(original_rtp_data.AsMutableSpan().subspan(2), ++sequence_number_);
    CopyOnWriteBuffer rtp_packet1to2 = original_rtp_data;
    CopyOnWriteBuffer rtp_packet2to1 = original_rtp_data;

    AsyncSocketPacketOptions options;
    // Send a packet from `srtp_transport1_` to `srtp_transport2_` and verify
    // that the packet can be successfully received and decrypted.
    ASSERT_TRUE(srtp_transport1_->SendRtpPacket(&rtp_packet1to2, options,
                                                PF_SRTP_BYPASS));
    EXPECT_EQ(rtp_sink2_.last_recv_rtp_packet().Buffer(), original_rtp_data);
    // Get the encrypted packet from underneath packet transport and verify the
    // data and header extension are actually encrypted.
    auto fake_rtp_packet_transport = static_cast<FakePacketTransport*>(
        srtp_transport1_->rtp_packet_transport());
    EXPECT_NE(*fake_rtp_packet_transport->last_sent_packet(),
              original_rtp_data);
    CompareHeaderExtensions(*fake_rtp_packet_transport->last_sent_packet(),
                            original_rtp_data, encrypted_header_ids, false);

    // Do the same thing in the opposite direction;
    ASSERT_TRUE(srtp_transport2_->SendRtpPacket(&rtp_packet2to1, options,
                                                PF_SRTP_BYPASS));
    EXPECT_EQ(rtp_sink1_.last_recv_rtp_packet().Buffer(), original_rtp_data);
    fake_rtp_packet_transport = static_cast<FakePacketTransport*>(
        srtp_transport2_->rtp_packet_transport());
    EXPECT_NE(*fake_rtp_packet_transport->last_sent_packet(),
              original_rtp_data);
    CompareHeaderExtensions(*fake_rtp_packet_transport->last_sent_packet(),
                            original_rtp_data, encrypted_header_ids, false);
  }

  void TestSendRecvEncryptedHeaderExtension(
      int crypto_suite,
      const ZeroOnFreeBuffer<uint8_t>& key1,
      const ZeroOnFreeBuffer<uint8_t>& key2) {
    std::vector<RtpHeaderExtensionId> encrypted_headers;
    encrypted_headers.push_back(kHeaderExtensionIDs[0]);
    // Don't encrypt header ids 2 and 3.
    encrypted_headers.push_back(kHeaderExtensionIDs[1]);
    EXPECT_EQ(key1.size(), key2.size());
    EXPECT_TRUE(srtp_transport1_->SetRtpParams(crypto_suite, key1,
                                               encrypted_headers, crypto_suite,
                                               key2, encrypted_headers));
    EXPECT_TRUE(srtp_transport2_->SetRtpParams(crypto_suite, key2,
                                               encrypted_headers, crypto_suite,
                                               key1, encrypted_headers));
    EXPECT_TRUE(srtp_transport1_->IsSrtpActive());
    EXPECT_TRUE(srtp_transport2_->IsSrtpActive());
    TestSendRecvPacketWithEncryptedHeaderExtension(crypto_suite,
                                                   encrypted_headers);
  }
  test::RunLoop main_thread;
  const Environment env_ = CreateTestEnvironment();

  std::unique_ptr<SrtpTransport> srtp_transport1_;
  std::unique_ptr<SrtpTransport> srtp_transport2_;

  std::unique_ptr<FakePacketTransport> rtp_packet_transport1_;
  std::unique_ptr<FakePacketTransport> rtp_packet_transport2_;

  TransportObserver rtp_sink1_;
  TransportObserver rtp_sink2_;

  int sequence_number_ = 0;
};

TEST_F(SrtpTransportTest, SendAndRecvPacket_AES_CM_128_HMAC_SHA1_80) {
  TestSendRecvPacket(kSrtpAes128CmSha1_80, kTestKey1, kTestKey2);
}

TEST_F(SrtpTransportTest,
       SendAndRecvPacketWithHeaderExtension_AES_CM_128_HMAC_SHA1_80) {
  TestSendRecvEncryptedHeaderExtension(kSrtpAes128CmSha1_80, kTestKey1,
                                       kTestKey2);
}

TEST_F(SrtpTransportTest, SendAndRecvPacket_AES_CM_128_HMAC_SHA1_32) {
  TestSendRecvPacket(kSrtpAes128CmSha1_32, kTestKey1, kTestKey2);
}

TEST_F(SrtpTransportTest,
       SendAndRecvPacketWithHeaderExtension_AES_CM_128_HMAC_SHA1_32) {
  TestSendRecvEncryptedHeaderExtension(kSrtpAes128CmSha1_32, kTestKey1,
                                       kTestKey2);
}

TEST_F(SrtpTransportTest, SendAndRecvPacket_kSrtpAeadAes128Gcm) {
  TestSendRecvPacket(kSrtpAeadAes128Gcm, kTestKeyGcm128_1, kTestKeyGcm128_2);
}

TEST_F(SrtpTransportTest,
       SendAndRecvPacketWithHeaderExtension_kSrtpAeadAes128Gcm) {
  TestSendRecvEncryptedHeaderExtension(kSrtpAeadAes128Gcm, kTestKeyGcm128_1,
                                       kTestKeyGcm128_2);
}

TEST_F(SrtpTransportTest, SendAndRecvPacket_kSrtpAeadAes256Gcm) {
  TestSendRecvPacket(kSrtpAeadAes256Gcm, kTestKeyGcm256_1, kTestKeyGcm256_2);
}

TEST_F(SrtpTransportTest,
       SendAndRecvPacketWithHeaderExtension_kSrtpAeadAes256Gcm) {
  TestSendRecvEncryptedHeaderExtension(kSrtpAeadAes256Gcm, kTestKeyGcm256_1,
                                       kTestKeyGcm256_2);
}

// Test directly setting the params with bogus keys.
TEST_F(SrtpTransportTest, TestSetParamsKeyTooShort) {
  std::vector<RtpHeaderExtensionId> extension_ids;
  EXPECT_FALSE(srtp_transport1_->SetRtpParams(
      kSrtpAes128CmSha1_80,
      ZeroOnFreeBuffer<uint8_t>(kTestKey1.data(), kTestKey1.size() - 1),
      extension_ids, kSrtpAes128CmSha1_80,
      ZeroOnFreeBuffer<uint8_t>(kTestKey1.data(), kTestKey1.size() - 1),
      extension_ids));
  EXPECT_FALSE(srtp_transport1_->SetRtcpParams(
      kSrtpAes128CmSha1_80,
      ZeroOnFreeBuffer<uint8_t>(kTestKey1.data(), kTestKey1.size() - 1),
      extension_ids, kSrtpAes128CmSha1_80,
      ZeroOnFreeBuffer<uint8_t>(kTestKey1.data(), kTestKey1.size() - 1),
      extension_ids));
}

TEST_F(SrtpTransportTest, RemoveSrtpReceiveStream) {
  Environment env = CreateTestEnvironment(
      {.field_trials = "WebRTC-SrtpRemoveReceiveStream/Enabled/"});
  auto srtp_transport = std::make_unique<SrtpTransport>(
      /*rtcp_mux_enabled=*/true, env.field_trials());
  auto rtp_packet_transport = std::make_unique<FakePacketTransport>(
      env, "fake_packet_transport_loopback");

  bool asymmetric = false;
  rtp_packet_transport->SetDestination(rtp_packet_transport.get(), asymmetric);
  srtp_transport->SetRtpPacketTransport(rtp_packet_transport.get());

  TransportObserver rtp_sink;

  std::vector<RtpHeaderExtensionId> extension_ids;
  EXPECT_TRUE(srtp_transport->SetRtpParams(kSrtpAeadAes128Gcm, kTestKeyGcm128_1,
                                           extension_ids, kSrtpAeadAes128Gcm,
                                           kTestKeyGcm128_1, extension_ids));

  RtpDemuxerCriteria demuxer_criteria;
  uint32_t ssrc = 0x1;  // SSRC of kPcmuFrame
  demuxer_criteria.ssrcs().insert(ssrc);
  EXPECT_TRUE(
      srtp_transport->RegisterRtpDemuxerSink(demuxer_criteria, &rtp_sink));

  // Create a packet and try to send it three times.
  const CopyOnWriteBuffer rtp_packet_data(
      kPcmuFrame,
      /*capacity=*/sizeof(kPcmuFrame) + rtp_auth_tag_len(kSrtpAeadAes128Gcm));

  // First attempt will succeed.
  CopyOnWriteBuffer first_try = rtp_packet_data;
  EXPECT_TRUE(srtp_transport->SendRtpPacket(
      &first_try, AsyncSocketPacketOptions(), PF_SRTP_BYPASS));
  EXPECT_EQ(rtp_sink.rtp_count(), 1);

  // Second attempt will be rejected by libSRTP as a replay attack
  // (srtp_err_status_replay_fail) since the sequence number was already seen.
  // Hence the packet never reaches the sink.
  CopyOnWriteBuffer second_try = rtp_packet_data;
  EXPECT_TRUE(srtp_transport->SendRtpPacket(
      &second_try, AsyncSocketPacketOptions(), PF_SRTP_BYPASS));
  EXPECT_EQ(rtp_sink.rtp_count(), 1);

  // Reset the sink.
  EXPECT_TRUE(srtp_transport->UnregisterRtpDemuxerSink(&rtp_sink));
  EXPECT_TRUE(
      srtp_transport->RegisterRtpDemuxerSink(demuxer_criteria, &rtp_sink));

  // Third attempt will succeed again since libSRTP does not remember seeing
  // the sequence number after the reset.
  CopyOnWriteBuffer third_try = rtp_packet_data;
  EXPECT_TRUE(srtp_transport->SendRtpPacket(
      &third_try, AsyncSocketPacketOptions(), PF_SRTP_BYPASS));
  EXPECT_EQ(rtp_sink.rtp_count(), 2);
  // Clear the sink to clean up.
  srtp_transport->UnregisterRtpDemuxerSink(&rtp_sink);
}

TEST(SrtpTransportTestWithFieldTrialEnabled,
     NetworkRouteOverheadUpdatedWhenSrtpActivated) {
  const Environment env = CreateTestEnvironment(
      {.field_trials = "WebRTC-UpdateNetworkRouteOnSrtpActivation/Enabled/"});
  auto rtp_transport = std::make_unique<FakePacketTransport>(env, "fake");
  auto srtp_transport =
      std::make_unique<SrtpTransport>(true, env.field_trials());
  srtp_transport->SetRtpPacketTransport(rtp_transport.get());

  std::optional<NetworkRoute> current_route;
  srtp_transport->SubscribeNetworkRouteChanged(
      rtp_transport.get(),
      [&](std::optional<NetworkRoute> r) { current_route = r; });

  NetworkRoute route;
  route.connected = true;
  route.packet_overhead = 28;
  rtp_transport->SetNetworkRoute(std::optional<NetworkRoute>(route));

  ASSERT_TRUE(current_route.has_value());
  EXPECT_EQ(current_route->packet_overhead, 28);

  // Activate SRTP.
  EXPECT_TRUE(srtp_transport->SetRtpParams(
      kSrtpAeadAes128Gcm, kTestKeyGcm128_1, std::vector<RtpHeaderExtensionId>(),
      kSrtpAeadAes128Gcm, kTestKeyGcm128_1,
      std::vector<RtpHeaderExtensionId>()));

  ASSERT_TRUE(current_route.has_value());
  EXPECT_EQ(current_route->packet_overhead, 28 + 16);
}

TEST(SrtpTransportTestDefault, NetworkRouteOverheadUnchangedWhenSrtpActivated) {
  const Environment env = CreateTestEnvironment();
  auto rtp_transport = std::make_unique<FakePacketTransport>(env, "fake");
  auto srtp_transport =
      std::make_unique<SrtpTransport>(true, env.field_trials());
  srtp_transport->SetRtpPacketTransport(rtp_transport.get());

  std::optional<NetworkRoute> current_route;
  srtp_transport->SubscribeNetworkRouteChanged(
      rtp_transport.get(),
      [&](std::optional<NetworkRoute> r) { current_route = r; });

  NetworkRoute route;
  route.connected = true;
  route.packet_overhead = 28;
  rtp_transport->SetNetworkRoute(std::optional<NetworkRoute>(route));

  ASSERT_TRUE(current_route.has_value());
  EXPECT_EQ(current_route->packet_overhead, 28);

  EXPECT_TRUE(srtp_transport->SetRtpParams(
      kSrtpAeadAes128Gcm, kTestKeyGcm128_1, std::vector<RtpHeaderExtensionId>(),
      kSrtpAeadAes128Gcm, kTestKeyGcm128_1,
      std::vector<RtpHeaderExtensionId>()));

  ASSERT_TRUE(current_route.has_value());
  EXPECT_EQ(current_route->packet_overhead, 28);
}

}  // namespace webrtc
