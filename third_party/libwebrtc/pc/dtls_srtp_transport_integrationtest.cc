/*
 *  Copyright 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>

#include "absl/strings/str_cat.h"
#include "api/crypto/crypto_options.h"
#include "api/dtls_transport_interface.h"
#include "api/environment/environment.h"
#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "api/test/rtc_error_matchers.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "call/rtp_demuxer.h"
#include "media/base/fake_rtp.h"
#include "p2p/base/transport_description.h"
#include "p2p/dtls/dtls_transport.h"
#include "p2p/dtls/dtls_transport_internal.h"
#include "p2p/test/fake_ice_transport.h"
#include "pc/dtls_srtp_transport.h"
#include "pc/ice_transport.h"
#include "pc/srtp_transport.h"
#include "pc/test/rtp_transport_test_util.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/buffer.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/ssl_fingerprint.h"
#include "rtc_base/ssl_identity.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/time_controller/simulated_time_controller.h"
#include "test/wait_until.h"

namespace webrtc {
namespace {
using testing::Eq;
using testing::IsTrue;

constexpr int kRtpAuthTagLen = 10;
constexpr int kTimeout = 10000;

/* A test using a DTLS-SRTP transport on one side and
 * SrtpTransport+DtlsTransport on the other side, connected by a
 * FakeIceTransportInternal.
 */
class DtlsSrtpTransportIntegrationTest : public ::testing::Test {
 protected:
  DtlsSrtpTransportIntegrationTest()
      : time_controller_(Timestamp::Millis(0)),
        env_(CreateTestEnvironment({.time = &time_controller_})),
        client_ice_transport_(MakeIceTransport(ICEROLE_CONTROLLING)),
        server_ice_transport_(MakeIceTransport(ICEROLE_CONTROLLED)),
        client_dtls_transport_(MakeDtlsTransport(client_ice_transport_.get())),
        server_dtls_transport_(MakeDtlsTransport(server_ice_transport_.get())),
        client_certificate_(MakeCertificate()),
        server_certificate_(MakeCertificate()),
        dtls_srtp_transport_(false, env_.field_trials()),
        srtp_transport_(false, env_.field_trials()) {
    dtls_srtp_transport_.SetDtlsTransports(server_dtls_transport_.get(),
                                           nullptr);
    srtp_transport_.SetRtpPacketTransport(client_ice_transport_.get());

    RtpDemuxerCriteria demuxer_criteria;
    demuxer_criteria.payload_types() = {0x00};
    dtls_srtp_transport_.RegisterRtpDemuxerSink(demuxer_criteria,
                                                &dtls_srtp_transport_observer_);
    srtp_transport_.RegisterRtpDemuxerSink(demuxer_criteria,
                                           &srtp_transport_observer_);
  }
  ~DtlsSrtpTransportIntegrationTest() override {
    dtls_srtp_transport_.UnregisterRtpDemuxerSink(
        &dtls_srtp_transport_observer_);
    srtp_transport_.UnregisterRtpDemuxerSink(&srtp_transport_observer_);
  }

  scoped_refptr<RTCCertificate> MakeCertificate() {
    return RTCCertificate::Create(SSLIdentity::Create("test", KT_DEFAULT));
  }
  std::unique_ptr<FakeIceTransportInternal> MakeIceTransport(IceRole role) {
    auto ice_transport = std::make_unique<FakeIceTransportInternal>(
        env_, "fake_" + absl::StrCat(static_cast<int>(role)), 0);
    ice_transport->SetAsync(true);
    ice_transport->SetAsyncDelay(0);
    ice_transport->SetIceRole(role);
    return ice_transport;
  }

  std::unique_ptr<DtlsTransportInternalImpl> MakeDtlsTransport(
      FakeIceTransportInternal* ice_transport) {
    return std::make_unique<DtlsTransportInternalImpl>(
        env_, make_ref_counted<IceTransportWithPointer>(ice_transport),
        CryptoOptions(), SSL_PROTOCOL_DTLS_12);
  }
  void SetRemoteFingerprintFromCert(DtlsTransportInternalImpl* transport,
                                    const scoped_refptr<RTCCertificate>& cert) {
    std::unique_ptr<SSLFingerprint> fingerprint =
        SSLFingerprint::CreateFromCertificate(*cert);

    transport->SetRemoteParameters(
        fingerprint->algorithm,
        reinterpret_cast<const uint8_t*>(fingerprint->digest.data()),
        fingerprint->digest.size(), std::nullopt);
  }

  void Connect() {
    client_dtls_transport_->SetLocalCertificate(client_certificate_);
    client_dtls_transport_->SetDtlsRole(SSL_SERVER);
    server_dtls_transport_->SetLocalCertificate(server_certificate_);
    server_dtls_transport_->SetDtlsRole(SSL_CLIENT);

    SetRemoteFingerprintFromCert(server_dtls_transport_.get(),
                                 client_certificate_);
    SetRemoteFingerprintFromCert(client_dtls_transport_.get(),
                                 server_certificate_);

    // Wire up the ICE and transport.
    client_ice_transport_->SetDestination(server_ice_transport_.get());

    // Wait for the DTLS connection to be up.
    EXPECT_TRUE(WaitUntil(
        [&] {
          return client_dtls_transport_->writable() &&
                 server_dtls_transport_->writable();
        },
        {.timeout = TimeDelta::Millis(kTimeout), .clock = &time_controller_}));
    EXPECT_EQ(client_dtls_transport_->dtls_state(),
              DtlsTransportState::kConnected);
    EXPECT_EQ(server_dtls_transport_->dtls_state(),
              DtlsTransportState::kConnected);
  }
  void SetupClientKeysManually() {
    // Setup the client-side SRTP transport with the keys from the server DTLS
    // transport.
    int selected_crypto_suite;
    ASSERT_TRUE(
        server_dtls_transport_->GetSrtpCryptoSuite(&selected_crypto_suite));
    int key_len;
    int salt_len;
    ASSERT_TRUE(
        GetSrtpKeyAndSaltLengths((selected_crypto_suite), &key_len, &salt_len));

    // Extract the keys. The order depends on the role!
    ZeroOnFreeBuffer<uint8_t> dtls_buffer;
    ASSERT_TRUE(server_dtls_transport_->AppendSrtpKeyingMaterial(dtls_buffer));

    ZeroOnFreeBuffer<unsigned char> client_write_key(&dtls_buffer[0], key_len,
                                                     key_len + salt_len);
    ZeroOnFreeBuffer<unsigned char> server_write_key(
        &dtls_buffer[key_len], key_len, key_len + salt_len);
    client_write_key.AppendData(&dtls_buffer[key_len + key_len], salt_len);
    server_write_key.AppendData(&dtls_buffer[key_len + key_len + salt_len],
                                salt_len);

    EXPECT_TRUE(srtp_transport_.SetRtpParams(
        selected_crypto_suite, server_write_key, {}, selected_crypto_suite,
        client_write_key, {}));
  }

  CopyOnWriteBuffer CreateRtpPacket() {
    return CopyOnWriteBuffer(kPcmuFrame, /*capacity=*/
                             sizeof(kPcmuFrame) + kRtpAuthTagLen);
  }

  void SendRtpPacketFromSrtpToDtlsSrtp() {
    AsyncSocketPacketOptions options;
    CopyOnWriteBuffer packet = CreateRtpPacket();

    EXPECT_TRUE(
        srtp_transport_.SendRtpPacket(&packet, options, PF_SRTP_BYPASS));
    EXPECT_THAT(
        WaitUntil([&] { return dtls_srtp_transport_observer_.rtp_count(); },
                  Eq(1),
                  {.timeout = TimeDelta::Millis(kTimeout),
                   .clock = &time_controller_}),
        IsRtcOk());
    EXPECT_EQ(1, dtls_srtp_transport_observer_.rtp_count());
    ASSERT_TRUE(dtls_srtp_transport_observer_.last_recv_rtp_packet().data());
    EXPECT_EQ(
        0,
        std::memcmp(dtls_srtp_transport_observer_.last_recv_rtp_packet().data(),
                    kPcmuFrame, sizeof(kPcmuFrame)));
  }

  void SendRtpPacketFromDtlsSrtpToSrtp() {
    AsyncSocketPacketOptions options;
    CopyOnWriteBuffer packet = CreateRtpPacket();

    EXPECT_TRUE(
        dtls_srtp_transport_.SendRtpPacket(&packet, options, PF_SRTP_BYPASS));
    EXPECT_THAT(
        WaitUntil([&] { return srtp_transport_observer_.rtp_count(); }, Eq(1),
                  {.timeout = TimeDelta::Millis(kTimeout),
                   .clock = &time_controller_}),
        IsRtcOk());
    EXPECT_EQ(1, srtp_transport_observer_.rtp_count());
    ASSERT_TRUE(srtp_transport_observer_.last_recv_rtp_packet().data());
    EXPECT_EQ(
        0, std::memcmp(srtp_transport_observer_.last_recv_rtp_packet().data(),
                       kPcmuFrame, sizeof(kPcmuFrame)));
  }

 private:
  GlobalSimulatedTimeController time_controller_;
  const Environment env_;

  std::unique_ptr<FakeIceTransportInternal> client_ice_transport_;
  std::unique_ptr<FakeIceTransportInternal> server_ice_transport_;

  std::unique_ptr<DtlsTransportInternalImpl> client_dtls_transport_;
  std::unique_ptr<DtlsTransportInternalImpl> server_dtls_transport_;

  scoped_refptr<RTCCertificate> client_certificate_;
  scoped_refptr<RTCCertificate> server_certificate_;

  DtlsSrtpTransport dtls_srtp_transport_;
  SrtpTransport srtp_transport_;

  TransportObserver dtls_srtp_transport_observer_;
  TransportObserver srtp_transport_observer_;
};

TEST_F(DtlsSrtpTransportIntegrationTest, SendRtpFromSrtpToDtlsSrtp) {
  Connect();
  SetupClientKeysManually();
  SendRtpPacketFromSrtpToDtlsSrtp();
}

TEST_F(DtlsSrtpTransportIntegrationTest, SendRtpFromDtlsSrtpToSrtp) {
  Connect();
  SetupClientKeysManually();
  SendRtpPacketFromDtlsSrtpToSrtp();
}

}  // namespace
}  // namespace webrtc
