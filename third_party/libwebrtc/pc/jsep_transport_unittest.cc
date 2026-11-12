/*
 *  Copyright 2018 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/jsep_transport.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "api/candidate.h"
#include "api/crypto/crypto_options.h"
#include "api/environment/environment.h"
#include "api/ice_transport_interface.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/rtp_header_extension_id.h"
#include "api/scoped_refptr.h"
#include "call/rtp_demuxer.h"
#include "media/base/fake_rtp.h"
#include "p2p/base/ice_transport_internal.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/packet_transport_internal.h"
#include "p2p/base/transport_description.h"
#include "p2p/dtls/dtls_transport_internal.h"
#include "p2p/dtls/fake_dtls_transport.h"
#include "p2p/test/fake_ice_transport.h"
#include "pc/dtls_srtp_transport.h"
#include "pc/dtls_transport.h"
#include "pc/rtp_transport.h"
#include "pc/rtp_transport_internal.h"
#include "pc/srtp_transport.h"
#include "pc/test/rtp_transport_test_util.h"
#include "pc/transport_stats.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/byte_order.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/logging.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/ssl_certificate.h"
#include "rtc_base/ssl_fingerprint.h"
#include "rtc_base/ssl_identity.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "test/create_test_environment.h"
#include "test/gtest.h"
#include "test/run_loop.h"

namespace webrtc {
namespace {

constexpr char kIceUfrag1[] = "U001";
constexpr char kIcePwd1[] = "TESTICEPWD00000000000001";
constexpr char kIceUfrag2[] = "U002";
constexpr char kIcePwd2[] = "TESTIEPWD00000000000002";
constexpr char kTransportName[] = "Test Transport";

struct NegotiateRoleParams {
  ConnectionRole local_role;
  ConnectionRole remote_role;
  SdpType local_type;
  SdpType remote_type;
};

std::ostream& operator<<(std::ostream& os, const ConnectionRole& role) {
  std::string str = "invalid";
  ConnectionRoleToString(role, &str);
  os << str;
  return os;
}

std::ostream& operator<<(std::ostream& os, const NegotiateRoleParams& param) {
  os << "[Local role " << param.local_role << " Remote role "
     << param.remote_role << " LocalType " << SdpTypeToString(param.local_type)
     << " RemoteType " << SdpTypeToString(param.remote_type) << "]";
  return os;
}

scoped_refptr<IceTransportInterface> CreateIceTransport(
    std::unique_ptr<FakeIceTransportInternal> internal) {
  if (!internal) {
    return nullptr;
  }

  return make_ref_counted<FakeIceTransport>(std::move(internal));
}

class JsepTransport2Test : public ::testing::Test {
 protected:
  std::unique_ptr<SrtpTransport> CreateSdesTransport(
      PacketTransportInternal* rtp_packet_transport,
      PacketTransportInternal* rtcp_packet_transport) {
    auto srtp_transport = std::make_unique<SrtpTransport>(
        rtcp_packet_transport == nullptr, env_.field_trials());

    srtp_transport->SetRtpPacketTransport(rtp_packet_transport);
    if (rtcp_packet_transport) {
      srtp_transport->SetRtcpPacketTransport(rtp_packet_transport);
    }
    return srtp_transport;
  }

  std::unique_ptr<DtlsSrtpTransport> CreateDtlsSrtpTransport(
      std::unique_ptr<DtlsTransportInternal> rtp_dtls_transport,
      std::unique_ptr<DtlsTransportInternal> rtcp_dtls_transport) {
    auto dtls_srtp_transport = std::make_unique<DtlsSrtpTransport>(
        rtcp_dtls_transport == nullptr, env_.field_trials());
    dtls_srtp_transport->SetDtlsTransportsOwned(std::move(rtp_dtls_transport),
                                                std::move(rtcp_dtls_transport));
    return dtls_srtp_transport;
  }

  // Create a new JsepTransport with a FakeDtlsTransport and a
  // FakeIceTransportInternal.
  std::unique_ptr<JsepTransport> CreateJsepTransport2(bool rtcp_mux_enabled) {
    auto ice_internal = std::make_unique<FakeIceTransportInternal>(
        env_, kTransportName, ICE_CANDIDATE_COMPONENT_RTP);
    auto ice = CreateIceTransport(std::move(ice_internal));
    auto rtp_dtls_transport = std::make_unique<FakeDtlsTransport>(ice);

    std::unique_ptr<FakeIceTransportInternal> rtcp_ice_internal;
    std::unique_ptr<FakeDtlsTransport> rtcp_dtls_transport;
    scoped_refptr<IceTransportInterface> rtcp_ice;
    if (!rtcp_mux_enabled) {
      rtcp_ice_internal = std::make_unique<FakeIceTransportInternal>(
          env_, kTransportName, ICE_CANDIDATE_COMPONENT_RTCP);
      rtcp_ice = CreateIceTransport(std::move(rtcp_ice_internal));
      rtcp_dtls_transport = std::make_unique<FakeDtlsTransport>(rtcp_ice);
    }

    DtlsTransportInternal* rtp_dtls_transport_ptr = rtp_dtls_transport.get();
    std::unique_ptr<RtpTransport> rtp_transport = CreateDtlsSrtpTransport(
        std::move(rtp_dtls_transport), std::move(rtcp_dtls_transport));

    scoped_refptr<DtlsTransport> rtp_dtls_transport_wrapper =
        make_ref_counted<DtlsTransport>(rtp_dtls_transport_ptr);

    auto jsep_transport = std::make_unique<JsepTransport>(
        /*local_certificate=*/nullptr, std::move(rtp_transport),
        std::move(rtp_dtls_transport_wrapper),
        /*sctp_transport=*/nullptr,
        /*rtcp_mux_active_callback=*/[&]() { OnRtcpMuxActive(); },
        /*cryptex_policy=*/CryptoOptions::Srtp::CryptexPolicy::kDisabled);

    signal_rtcp_mux_active_received_ = false;
    return jsep_transport;
  }

  JsepTransportDescription MakeJsepTransportDescription(
      bool rtcp_mux_enabled,
      const char* ufrag,
      const char* pwd,
      const scoped_refptr<RTCCertificate>& cert,
      ConnectionRole role = CONNECTIONROLE_NONE) {
    JsepTransportDescription jsep_description;
    jsep_description.rtcp_mux_enabled = rtcp_mux_enabled;

    std::unique_ptr<SSLFingerprint> fingerprint;
    if (cert) {
      fingerprint = SSLFingerprint::CreateFromCertificate(*cert);
    }
    jsep_description.transport_desc =
        TransportDescription(std::vector<std::string>(), ufrag, pwd,
                             ICEMODE_FULL, role, fingerprint.get());
    return jsep_description;
  }

  Candidate CreateCandidate(int component) {
    Candidate c;
    c.set_address(SocketAddress("192.168.1.1", 8000));
    c.set_component(component);
    c.set_protocol(UDP_PROTOCOL_NAME);
    c.set_priority(1);
    return c;
  }

  void OnRtcpMuxActive() { signal_rtcp_mux_active_received_ = true; }

  test::RunLoop main_thread_;
  const Environment env_ = CreateTestEnvironment();
  std::unique_ptr<JsepTransport> jsep_transport_;
  bool signal_rtcp_mux_active_received_ = false;
};

// The parameterized tests cover both cases when RTCP mux is enable and
// disabled.
class JsepTransport2WithRtcpMux : public JsepTransport2Test,
                                  public ::testing::WithParamInterface<bool> {};

// This test verifies the ICE parameters are properly applied to the transports.
TEST_P(JsepTransport2WithRtcpMux, SetIceParameters) {
  bool rtcp_mux_enabled = GetParam();
  jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);

  JsepTransportDescription jsep_description;
  jsep_description.transport_desc = TransportDescription(kIceUfrag1, kIcePwd1);
  jsep_description.rtcp_mux_enabled = rtcp_mux_enabled;
  ASSERT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(jsep_description, SdpType::kOffer)
          .ok());
  auto fake_ice_transport = static_cast<FakeIceTransportInternal*>(
      jsep_transport_->rtp_dtls_transport()->ice_transport());
  EXPECT_EQ(ICEMODE_FULL, fake_ice_transport->remote_ice_mode());
  EXPECT_EQ(kIceUfrag1, fake_ice_transport->local_ice_parameters()->ufrag);
  EXPECT_EQ(kIcePwd1, fake_ice_transport->local_ice_parameters()->pwd);
  if (!rtcp_mux_enabled) {
    fake_ice_transport = static_cast<FakeIceTransportInternal*>(
        jsep_transport_->rtcp_dtls_transport()->ice_transport());
    ASSERT_TRUE(fake_ice_transport);
    EXPECT_EQ(ICEMODE_FULL, fake_ice_transport->remote_ice_mode());
    EXPECT_EQ(kIceUfrag1, fake_ice_transport->local_ice_parameters()->ufrag);
    EXPECT_EQ(kIcePwd1, fake_ice_transport->local_ice_parameters()->pwd);
  }

  jsep_description.transport_desc = TransportDescription(kIceUfrag2, kIcePwd2);
  ASSERT_TRUE(jsep_transport_
                  ->SetRemoteJsepTransportDescription(jsep_description,
                                                      SdpType::kAnswer)
                  .ok());
  fake_ice_transport = static_cast<FakeIceTransportInternal*>(
      jsep_transport_->rtp_dtls_transport()->ice_transport());
  EXPECT_EQ(ICEMODE_FULL, fake_ice_transport->remote_ice_mode());
  EXPECT_EQ(kIceUfrag2, fake_ice_transport->remote_ice_parameters()->ufrag);
  EXPECT_EQ(kIcePwd2, fake_ice_transport->remote_ice_parameters()->pwd);
  if (!rtcp_mux_enabled) {
    fake_ice_transport = static_cast<FakeIceTransportInternal*>(
        jsep_transport_->rtcp_dtls_transport()->ice_transport());
    ASSERT_TRUE(fake_ice_transport);
    EXPECT_EQ(ICEMODE_FULL, fake_ice_transport->remote_ice_mode());
    EXPECT_EQ(kIceUfrag2, fake_ice_transport->remote_ice_parameters()->ufrag);
    EXPECT_EQ(kIcePwd2, fake_ice_transport->remote_ice_parameters()->pwd);
  }
}

// Similarly, test DTLS parameters are properly applied to the transports.
TEST_P(JsepTransport2WithRtcpMux, SetDtlsParameters) {
  bool rtcp_mux_enabled = GetParam();
  jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);

  // Create certificates.
  scoped_refptr<RTCCertificate> local_cert =
      RTCCertificate::Create(SSLIdentity::Create("local", KT_DEFAULT));
  scoped_refptr<RTCCertificate> remote_cert =
      RTCCertificate::Create(SSLIdentity::Create("remote", KT_DEFAULT));
  jsep_transport_->SetLocalCertificate(local_cert);

  // Apply offer.
  JsepTransportDescription local_description =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag1, kIcePwd1,
                                   local_cert, CONNECTIONROLE_ACTPASS);
  ASSERT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_description, SdpType::kOffer)
          .ok());
  // Apply Answer.
  JsepTransportDescription remote_description =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag2, kIcePwd2,
                                   remote_cert, CONNECTIONROLE_ACTIVE);
  ASSERT_TRUE(jsep_transport_
                  ->SetRemoteJsepTransportDescription(remote_description,
                                                      SdpType::kAnswer)
                  .ok());

  // Verify that SSL role and remote fingerprint were set correctly based on
  // transport descriptions.
  auto role = jsep_transport_->GetDtlsRole();
  ASSERT_TRUE(role);
  EXPECT_EQ(SSL_SERVER,
            role);  // Because remote description was "active".
  auto fake_dtls =
      static_cast<FakeDtlsTransport*>(jsep_transport_->rtp_dtls_transport());
  EXPECT_EQ(remote_description.transport_desc.identity_fingerprint->ToString(),
            fake_dtls->dtls_fingerprint().ToString());

  if (!rtcp_mux_enabled) {
    auto fake_rtcp_dtls =
        static_cast<FakeDtlsTransport*>(jsep_transport_->rtcp_dtls_transport());
    EXPECT_EQ(
        remote_description.transport_desc.identity_fingerprint->ToString(),
        fake_rtcp_dtls->dtls_fingerprint().ToString());
  }
}

// Same as above test, but with remote transport description using
// CONNECTIONROLE_PASSIVE, expecting SSL_CLIENT role.
TEST_P(JsepTransport2WithRtcpMux, SetDtlsParametersWithPassiveAnswer) {
  bool rtcp_mux_enabled = GetParam();
  jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);

  // Create certificates.
  scoped_refptr<RTCCertificate> local_cert =
      RTCCertificate::Create(SSLIdentity::Create("local", KT_DEFAULT));
  scoped_refptr<RTCCertificate> remote_cert =
      RTCCertificate::Create(SSLIdentity::Create("remote", KT_DEFAULT));
  jsep_transport_->SetLocalCertificate(local_cert);

  // Apply offer.
  JsepTransportDescription local_description =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag1, kIcePwd1,
                                   local_cert, CONNECTIONROLE_ACTPASS);
  ASSERT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_description, SdpType::kOffer)
          .ok());
  // Apply Answer.
  JsepTransportDescription remote_description =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag2, kIcePwd2,
                                   remote_cert, CONNECTIONROLE_PASSIVE);
  ASSERT_TRUE(jsep_transport_
                  ->SetRemoteJsepTransportDescription(remote_description,
                                                      SdpType::kAnswer)
                  .ok());

  // Verify that SSL role and remote fingerprint were set correctly based on
  // transport descriptions.
  auto role = jsep_transport_->GetDtlsRole();
  ASSERT_TRUE(role);
  EXPECT_EQ(SSL_CLIENT,
            role);  // Because remote description was "passive".
  auto fake_dtls =
      static_cast<FakeDtlsTransport*>(jsep_transport_->rtp_dtls_transport());
  EXPECT_EQ(remote_description.transport_desc.identity_fingerprint->ToString(),
            fake_dtls->dtls_fingerprint().ToString());

  if (!rtcp_mux_enabled) {
    auto fake_rtcp_dtls =
        static_cast<FakeDtlsTransport*>(jsep_transport_->rtcp_dtls_transport());
    EXPECT_EQ(
        remote_description.transport_desc.identity_fingerprint->ToString(),
        fake_rtcp_dtls->dtls_fingerprint().ToString());
  }
}

TEST_P(JsepTransport2WithRtcpMux, SetGoogSpedV1OptionUpdatesIceConfig) {
  bool rtcp_mux_enabled = GetParam();
  jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);

  auto fake_ice_transport = static_cast<FakeIceTransportInternal*>(
      jsep_transport_->rtp_dtls_transport()->ice_transport());
  // PeerConnection enables this from the field trial before negotiating.
  IceConfig config = fake_ice_transport->config();
  config.dtls_handshake_in_stun = true;
  fake_ice_transport->SetIceConfig(config);

  scoped_refptr<RTCCertificate> local_cert =
      RTCCertificate::Create(SSLIdentity::Create("local", KT_DEFAULT));
  scoped_refptr<RTCCertificate> remote_cert =
      RTCCertificate::Create(SSLIdentity::Create("remote", KT_DEFAULT));
  jsep_transport_->SetLocalCertificate(local_cert);

  JsepTransportDescription local_description =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag1, kIcePwd1,
                                   local_cert, CONNECTIONROLE_ACTPASS);
  local_description.transport_desc.AddOption(ICE_OPTION_GOOG_SPED_V1);

  ASSERT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_description, SdpType::kOffer)
          .ok());

  JsepTransportDescription remote_description =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag2, kIcePwd2,
                                   remote_cert, CONNECTIONROLE_ACTIVE);

  ASSERT_TRUE(jsep_transport_
                  ->SetRemoteJsepTransportDescription(remote_description,
                                                      SdpType::kAnswer)
                  .ok());

  EXPECT_TRUE(fake_ice_transport->config().dtls_handshake_in_stun);
}

TEST_P(JsepTransport2WithRtcpMux, SetSpedOptionUpdatesIceConfig) {
  bool rtcp_mux_enabled = GetParam();
  jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);

  auto fake_ice_transport = static_cast<FakeIceTransportInternal*>(
      jsep_transport_->rtp_dtls_transport()->ice_transport());
  // PeerConnection enables this from the field trial before negotiating.
  IceConfig config = fake_ice_transport->config();
  config.dtls_handshake_in_stun = true;
  fake_ice_transport->SetIceConfig(config);

  scoped_refptr<RTCCertificate> local_cert =
      RTCCertificate::Create(SSLIdentity::Create("local", KT_DEFAULT));
  scoped_refptr<RTCCertificate> remote_cert =
      RTCCertificate::Create(SSLIdentity::Create("remote", KT_DEFAULT));
  jsep_transport_->SetLocalCertificate(local_cert);

  JsepTransportDescription local_description =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag1, kIcePwd1,
                                   local_cert, CONNECTIONROLE_ACTPASS);
  local_description.transport_desc.AddOption(ICE_OPTION_SPED);

  ASSERT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_description, SdpType::kOffer)
          .ok());

  JsepTransportDescription remote_description =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag2, kIcePwd2,
                                   remote_cert, CONNECTIONROLE_ACTIVE);
  remote_description.transport_desc.AddOption(ICE_OPTION_SPED);

  ASSERT_TRUE(jsep_transport_
                  ->SetRemoteJsepTransportDescription(remote_description,
                                                      SdpType::kAnswer)
                  .ok());

  EXPECT_TRUE(fake_ice_transport->config().dtls_handshake_in_stun);
}

TEST_P(JsepTransport2WithRtcpMux, SetSpedOptionUpdatesIceConfigAsAnswerer) {
  bool rtcp_mux_enabled = GetParam();
  jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);

  auto fake_ice_transport = static_cast<FakeIceTransportInternal*>(
      jsep_transport_->rtp_dtls_transport()->ice_transport());
  // PeerConnection enables this from the field trial before negotiating.
  IceConfig config = fake_ice_transport->config();
  config.dtls_handshake_in_stun = true;
  fake_ice_transport->SetIceConfig(config);

  scoped_refptr<RTCCertificate> local_cert =
      RTCCertificate::Create(SSLIdentity::Create("local", KT_DEFAULT));
  scoped_refptr<RTCCertificate> remote_cert =
      RTCCertificate::Create(SSLIdentity::Create("remote", KT_DEFAULT));
  jsep_transport_->SetLocalCertificate(local_cert);

  JsepTransportDescription remote_description =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag2, kIcePwd2,
                                   remote_cert, CONNECTIONROLE_ACTPASS);
  remote_description.transport_desc.AddOption(ICE_OPTION_SPED);

  ASSERT_TRUE(jsep_transport_
                  ->SetRemoteJsepTransportDescription(remote_description,
                                                      SdpType::kOffer)
                  .ok());

  JsepTransportDescription local_description =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag1, kIcePwd1,
                                   local_cert, CONNECTIONROLE_ACTIVE);
  local_description.transport_desc.AddOption(ICE_OPTION_SPED);

  ASSERT_TRUE(jsep_transport_
                  ->SetLocalJsepTransportDescription(local_description,
                                                     SdpType::kAnswer)
                  .ok());

  EXPECT_TRUE(fake_ice_transport->config().dtls_handshake_in_stun);
}

TEST_P(JsepTransport2WithRtcpMux, RemoteWithoutSpedDisablesDtlsInStun) {
  bool rtcp_mux_enabled = GetParam();
  jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);

  auto fake_ice_transport = static_cast<FakeIceTransportInternal*>(
      jsep_transport_->rtp_dtls_transport()->ice_transport());
  // PeerConnection enables this from the field trial before negotiating.
  IceConfig config = fake_ice_transport->config();
  config.dtls_handshake_in_stun = true;
  fake_ice_transport->SetIceConfig(config);

  scoped_refptr<RTCCertificate> local_cert =
      RTCCertificate::Create(SSLIdentity::Create("local", KT_DEFAULT));
  scoped_refptr<RTCCertificate> remote_cert =
      RTCCertificate::Create(SSLIdentity::Create("remote", KT_DEFAULT));
  jsep_transport_->SetLocalCertificate(local_cert);

  JsepTransportDescription local_description =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag1, kIcePwd1,
                                   local_cert, CONNECTIONROLE_ACTPASS);
  local_description.transport_desc.AddOption(ICE_OPTION_SPED);

  ASSERT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_description, SdpType::kOffer)
          .ok());

  JsepTransportDescription remote_description =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag2, kIcePwd2,
                                   remote_cert, CONNECTIONROLE_ACTIVE);

  ASSERT_TRUE(jsep_transport_
                  ->SetRemoteJsepTransportDescription(remote_description,
                                                      SdpType::kAnswer)
                  .ok());

  EXPECT_FALSE(fake_ice_transport->config().dtls_handshake_in_stun);
}

TEST_P(JsepTransport2WithRtcpMux, RemoteWithoutGoogSpedV1KeepsDtlsInStun) {
  bool rtcp_mux_enabled = GetParam();
  jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);

  auto fake_ice_transport = static_cast<FakeIceTransportInternal*>(
      jsep_transport_->rtp_dtls_transport()->ice_transport());
  // PeerConnection enables this from the field trial before negotiating.
  IceConfig config = fake_ice_transport->config();
  config.dtls_handshake_in_stun = true;
  fake_ice_transport->SetIceConfig(config);

  scoped_refptr<RTCCertificate> local_cert =
      RTCCertificate::Create(SSLIdentity::Create("local", KT_DEFAULT));
  scoped_refptr<RTCCertificate> remote_cert =
      RTCCertificate::Create(SSLIdentity::Create("remote", KT_DEFAULT));
  jsep_transport_->SetLocalCertificate(local_cert);

  JsepTransportDescription local_description =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag1, kIcePwd1,
                                   local_cert, CONNECTIONROLE_ACTPASS);
  local_description.transport_desc.AddOption(ICE_OPTION_GOOG_SPED_V1);

  ASSERT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_description, SdpType::kOffer)
          .ok());

  JsepTransportDescription remote_description =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag2, kIcePwd2,
                                   remote_cert, CONNECTIONROLE_ACTIVE);

  ASSERT_TRUE(jsep_transport_
                  ->SetRemoteJsepTransportDescription(remote_description,
                                                      SdpType::kAnswer)
                  .ok());

  EXPECT_TRUE(fake_ice_transport->config().dtls_handshake_in_stun);
}

TEST_P(JsepTransport2WithRtcpMux, MungedSpedOptionDoesNotUpdateIceConfig) {
  bool rtcp_mux_enabled = GetParam();
  jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);

  scoped_refptr<RTCCertificate> local_cert =
      RTCCertificate::Create(SSLIdentity::Create("local", KT_DEFAULT));
  scoped_refptr<RTCCertificate> remote_cert =
      RTCCertificate::Create(SSLIdentity::Create("remote", KT_DEFAULT));
  jsep_transport_->SetLocalCertificate(local_cert);

  // The ice-option is present on both sides but was never enabled locally, so
  // it can only have been munged into the local description.
  JsepTransportDescription local_description =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag1, kIcePwd1,
                                   local_cert, CONNECTIONROLE_ACTPASS);
  local_description.transport_desc.AddOption(ICE_OPTION_SPED);

  ASSERT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_description, SdpType::kOffer)
          .ok());

  JsepTransportDescription remote_description =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag2, kIcePwd2,
                                   remote_cert, CONNECTIONROLE_ACTIVE);
  remote_description.transport_desc.AddOption(ICE_OPTION_SPED);

  ASSERT_TRUE(jsep_transport_
                  ->SetRemoteJsepTransportDescription(remote_description,
                                                      SdpType::kAnswer)
                  .ok());

  auto fake_ice_transport = static_cast<FakeIceTransportInternal*>(
      jsep_transport_->rtp_dtls_transport()->ice_transport());

  EXPECT_FALSE(fake_ice_transport->config().dtls_handshake_in_stun);
}

// Tests SetNeedsIceRestartFlag and need_ice_restart, ensuring needs_ice_restart
// only starts returning "false" once an ICE restart has been initiated.
TEST_P(JsepTransport2WithRtcpMux, NeedsIceRestart) {
  bool rtcp_mux_enabled = GetParam();
  jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);

  // Use the same JsepTransportDescription for both offer and answer.
  JsepTransportDescription description;
  description.transport_desc = TransportDescription(kIceUfrag1, kIcePwd1);
  ASSERT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(description, SdpType::kOffer)
          .ok());
  ASSERT_TRUE(
      jsep_transport_
          ->SetRemoteJsepTransportDescription(description, SdpType::kAnswer)
          .ok());
  // Flag initially should be false.
  EXPECT_FALSE(jsep_transport_->needs_ice_restart());

  // After setting flag, it should be true.
  jsep_transport_->SetNeedsIceRestartFlag();
  EXPECT_TRUE(jsep_transport_->needs_ice_restart());

  ASSERT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(description, SdpType::kOffer)
          .ok());
  ASSERT_TRUE(
      jsep_transport_
          ->SetRemoteJsepTransportDescription(description, SdpType::kAnswer)
          .ok());
  EXPECT_TRUE(jsep_transport_->needs_ice_restart());

  // Doing an offer/answer that restarts ICE should clear the flag.
  description.transport_desc = TransportDescription(kIceUfrag2, kIcePwd2);
  ASSERT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(description, SdpType::kOffer)
          .ok());
  ASSERT_TRUE(
      jsep_transport_
          ->SetRemoteJsepTransportDescription(description, SdpType::kAnswer)
          .ok());
  EXPECT_FALSE(jsep_transport_->needs_ice_restart());
}

TEST_P(JsepTransport2WithRtcpMux, GetStats) {
  bool rtcp_mux_enabled = GetParam();
  jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);

  size_t expected_stats_size = rtcp_mux_enabled ? 1u : 2u;
  TransportStats stats;
  EXPECT_TRUE(jsep_transport_->GetStats(&stats));
  EXPECT_EQ(expected_stats_size, stats.channel_stats.size());
  EXPECT_EQ(ICE_CANDIDATE_COMPONENT_RTP, stats.channel_stats[0].component);
  if (!rtcp_mux_enabled) {
    EXPECT_EQ(ICE_CANDIDATE_COMPONENT_RTCP, stats.channel_stats[1].component);
  }
}

// Tests that VerifyCertificateFingerprint only returns true when the
// certificate matches the fingerprint.
TEST_P(JsepTransport2WithRtcpMux, VerifyCertificateFingerprint) {
  bool rtcp_mux_enabled = GetParam();
  jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);

  EXPECT_FALSE(
      jsep_transport_->VerifyCertificateFingerprint(nullptr, nullptr).ok());
  KeyType key_types[] = {KT_RSA, KT_ECDSA};

  for (auto& key_type : key_types) {
    scoped_refptr<RTCCertificate> certificate =
        RTCCertificate::Create(SSLIdentity::Create("testing", key_type));
    ASSERT_NE(nullptr, certificate);

    std::string digest_algorithm;
    ASSERT_TRUE(certificate->GetSSLCertificate().GetSignatureDigestAlgorithm(
        &digest_algorithm));
    ASSERT_FALSE(digest_algorithm.empty());
    std::unique_ptr<SSLFingerprint> good_fingerprint =
        SSLFingerprint::Create(digest_algorithm, *certificate->identity());
    ASSERT_NE(good_fingerprint, nullptr);

    EXPECT_TRUE(jsep_transport_
                    ->VerifyCertificateFingerprint(certificate.get(),
                                                   good_fingerprint.get())
                    .ok());
    EXPECT_FALSE(jsep_transport_
                     ->VerifyCertificateFingerprint(certificate.get(), nullptr)
                     .ok());
    EXPECT_FALSE(
        jsep_transport_
            ->VerifyCertificateFingerprint(nullptr, good_fingerprint.get())
            .ok());

    SSLFingerprint bad_fingerprint = *good_fingerprint;
    bad_fingerprint.digest.AppendData("0", 1);
    EXPECT_FALSE(
        jsep_transport_
            ->VerifyCertificateFingerprint(certificate.get(), &bad_fingerprint)
            .ok());
  }
}

// Tests the logic of DTLS role negotiation for an initial offer/answer.
TEST_P(JsepTransport2WithRtcpMux, ValidDtlsRoleNegotiation) {
  bool rtcp_mux_enabled = GetParam();
  // Just use the same certificate for both sides; doesn't really matter in a
  // non end-to-end test.
  scoped_refptr<RTCCertificate> certificate =
      RTCCertificate::Create(SSLIdentity::Create("testing", KT_ECDSA));

  JsepTransportDescription local_description = MakeJsepTransportDescription(
      rtcp_mux_enabled, kIceUfrag1, kIcePwd1, certificate);
  JsepTransportDescription remote_description = MakeJsepTransportDescription(
      rtcp_mux_enabled, kIceUfrag2, kIcePwd2, certificate);

  // Parameters which set the SSL role to SSL_CLIENT.
  NegotiateRoleParams valid_client_params[] = {
      {.local_role = CONNECTIONROLE_ACTIVE,
       .remote_role = CONNECTIONROLE_ACTPASS,
       .local_type = SdpType::kAnswer,
       .remote_type = SdpType::kOffer},
      {.local_role = CONNECTIONROLE_ACTIVE,
       .remote_role = CONNECTIONROLE_ACTPASS,
       .local_type = SdpType::kPrAnswer,
       .remote_type = SdpType::kOffer},
      {.local_role = CONNECTIONROLE_ACTPASS,
       .remote_role = CONNECTIONROLE_PASSIVE,
       .local_type = SdpType::kOffer,
       .remote_type = SdpType::kAnswer},
      {.local_role = CONNECTIONROLE_ACTPASS,
       .remote_role = CONNECTIONROLE_PASSIVE,
       .local_type = SdpType::kOffer,
       .remote_type = SdpType::kPrAnswer},
      // Combinations permitted by RFC 8842 section 5.3
      {.local_role = CONNECTIONROLE_ACTIVE,
       .remote_role = CONNECTIONROLE_PASSIVE,
       .local_type = SdpType::kAnswer,
       .remote_type = SdpType::kOffer},
      {.local_role = CONNECTIONROLE_ACTIVE,
       .remote_role = CONNECTIONROLE_PASSIVE,
       .local_type = SdpType::kPrAnswer,
       .remote_type = SdpType::kOffer},
  };

  for (auto& param : valid_client_params) {
    jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);
    jsep_transport_->SetLocalCertificate(certificate);

    local_description.transport_desc.connection_role = param.local_role;
    remote_description.transport_desc.connection_role = param.remote_role;

    // Set the offer first.
    if (param.local_type == SdpType::kOffer) {
      EXPECT_TRUE(jsep_transport_
                      ->SetLocalJsepTransportDescription(local_description,
                                                         param.local_type)
                      .ok());
      EXPECT_TRUE(jsep_transport_
                      ->SetRemoteJsepTransportDescription(remote_description,
                                                          param.remote_type)
                      .ok());
    } else {
      EXPECT_TRUE(jsep_transport_
                      ->SetRemoteJsepTransportDescription(remote_description,
                                                          param.remote_type)
                      .ok());
      EXPECT_TRUE(jsep_transport_
                      ->SetLocalJsepTransportDescription(local_description,
                                                         param.local_type)
                      .ok());
    }
    EXPECT_EQ(SSL_CLIENT, *jsep_transport_->GetDtlsRole());
  }

  // Parameters which set the SSL role to SSL_SERVER.
  NegotiateRoleParams valid_server_params[] = {
      {.local_role = CONNECTIONROLE_PASSIVE,
       .remote_role = CONNECTIONROLE_ACTPASS,
       .local_type = SdpType::kAnswer,
       .remote_type = SdpType::kOffer},
      {.local_role = CONNECTIONROLE_PASSIVE,
       .remote_role = CONNECTIONROLE_ACTPASS,
       .local_type = SdpType::kPrAnswer,
       .remote_type = SdpType::kOffer},
      {.local_role = CONNECTIONROLE_ACTPASS,
       .remote_role = CONNECTIONROLE_ACTIVE,
       .local_type = SdpType::kOffer,
       .remote_type = SdpType::kAnswer},
      {.local_role = CONNECTIONROLE_ACTPASS,
       .remote_role = CONNECTIONROLE_ACTIVE,
       .local_type = SdpType::kOffer,
       .remote_type = SdpType::kPrAnswer},
      // Combinations permitted by RFC 8842 section 5.3
      {.local_role = CONNECTIONROLE_PASSIVE,
       .remote_role = CONNECTIONROLE_ACTIVE,
       .local_type = SdpType::kPrAnswer,
       .remote_type = SdpType::kOffer},
  };

  for (auto& param : valid_server_params) {
    jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);
    jsep_transport_->SetLocalCertificate(certificate);

    local_description.transport_desc.connection_role = param.local_role;
    remote_description.transport_desc.connection_role = param.remote_role;

    // Set the offer first.
    if (param.local_type == SdpType::kOffer) {
      EXPECT_TRUE(jsep_transport_
                      ->SetLocalJsepTransportDescription(local_description,
                                                         param.local_type)
                      .ok());
      EXPECT_TRUE(jsep_transport_
                      ->SetRemoteJsepTransportDescription(remote_description,
                                                          param.remote_type)
                      .ok());
    } else {
      EXPECT_TRUE(jsep_transport_
                      ->SetRemoteJsepTransportDescription(remote_description,
                                                          param.remote_type)
                      .ok());
      EXPECT_TRUE(jsep_transport_
                      ->SetLocalJsepTransportDescription(local_description,
                                                         param.local_type)
                      .ok());
    }
    EXPECT_EQ(SSL_SERVER, *jsep_transport_->GetDtlsRole());
  }
}

// Tests the logic of DTLS role negotiation for an initial offer/answer.
TEST_P(JsepTransport2WithRtcpMux, InvalidDtlsRoleNegotiation) {
  bool rtcp_mux_enabled = GetParam();
  // Just use the same certificate for both sides; doesn't really matter in a
  // non end-to-end test.
  scoped_refptr<RTCCertificate> certificate =
      RTCCertificate::Create(SSLIdentity::Create("testing", KT_ECDSA));

  JsepTransportDescription local_description = MakeJsepTransportDescription(
      rtcp_mux_enabled, kIceUfrag1, kIcePwd1, certificate);
  JsepTransportDescription remote_description = MakeJsepTransportDescription(
      rtcp_mux_enabled, kIceUfrag2, kIcePwd2, certificate);

  NegotiateRoleParams duplicate_params[] = {
      {.local_role = CONNECTIONROLE_ACTIVE,
       .remote_role = CONNECTIONROLE_ACTIVE,
       .local_type = SdpType::kAnswer,
       .remote_type = SdpType::kOffer},
      {.local_role = CONNECTIONROLE_ACTPASS,
       .remote_role = CONNECTIONROLE_ACTPASS,
       .local_type = SdpType::kAnswer,
       .remote_type = SdpType::kOffer},
      {.local_role = CONNECTIONROLE_PASSIVE,
       .remote_role = CONNECTIONROLE_PASSIVE,
       .local_type = SdpType::kAnswer,
       .remote_type = SdpType::kOffer},
      {.local_role = CONNECTIONROLE_ACTIVE,
       .remote_role = CONNECTIONROLE_ACTIVE,
       .local_type = SdpType::kPrAnswer,
       .remote_type = SdpType::kOffer},
      {.local_role = CONNECTIONROLE_ACTPASS,
       .remote_role = CONNECTIONROLE_ACTPASS,
       .local_type = SdpType::kPrAnswer,
       .remote_type = SdpType::kOffer},
      {.local_role = CONNECTIONROLE_PASSIVE,
       .remote_role = CONNECTIONROLE_PASSIVE,
       .local_type = SdpType::kPrAnswer,
       .remote_type = SdpType::kOffer},
      {.local_role = CONNECTIONROLE_ACTIVE,
       .remote_role = CONNECTIONROLE_ACTIVE,
       .local_type = SdpType::kOffer,
       .remote_type = SdpType::kAnswer},
      {.local_role = CONNECTIONROLE_ACTPASS,
       .remote_role = CONNECTIONROLE_ACTPASS,
       .local_type = SdpType::kOffer,
       .remote_type = SdpType::kAnswer},
      {.local_role = CONNECTIONROLE_PASSIVE,
       .remote_role = CONNECTIONROLE_PASSIVE,
       .local_type = SdpType::kOffer,
       .remote_type = SdpType::kAnswer},
      {.local_role = CONNECTIONROLE_ACTIVE,
       .remote_role = CONNECTIONROLE_ACTIVE,
       .local_type = SdpType::kOffer,
       .remote_type = SdpType::kPrAnswer},
      {.local_role = CONNECTIONROLE_ACTPASS,
       .remote_role = CONNECTIONROLE_ACTPASS,
       .local_type = SdpType::kOffer,
       .remote_type = SdpType::kPrAnswer},
      {.local_role = CONNECTIONROLE_PASSIVE,
       .remote_role = CONNECTIONROLE_PASSIVE,
       .local_type = SdpType::kOffer,
       .remote_type = SdpType::kPrAnswer}};

  for (auto& param : duplicate_params) {
    jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);
    jsep_transport_->SetLocalCertificate(certificate);

    local_description.transport_desc.connection_role = param.local_role;
    remote_description.transport_desc.connection_role = param.remote_role;

    if (param.local_type == SdpType::kOffer) {
      EXPECT_TRUE(jsep_transport_
                      ->SetLocalJsepTransportDescription(local_description,
                                                         param.local_type)
                      .ok());
      EXPECT_FALSE(jsep_transport_
                       ->SetRemoteJsepTransportDescription(remote_description,
                                                           param.remote_type)
                       .ok());
    } else {
      EXPECT_TRUE(jsep_transport_
                      ->SetRemoteJsepTransportDescription(remote_description,
                                                          param.remote_type)
                      .ok());
      EXPECT_FALSE(jsep_transport_
                       ->SetLocalJsepTransportDescription(local_description,
                                                          param.local_type)
                       .ok());
    }
  }

  // Invalid parameters due to the offerer not using a role consistent with the
  // state
  NegotiateRoleParams offerer_without_actpass_params[] = {
      // Cannot use ACTPASS in an answer
      {.local_role = CONNECTIONROLE_ACTPASS,
       .remote_role = CONNECTIONROLE_PASSIVE,
       .local_type = SdpType::kAnswer,
       .remote_type = SdpType::kOffer},
      {.local_role = CONNECTIONROLE_ACTPASS,
       .remote_role = CONNECTIONROLE_PASSIVE,
       .local_type = SdpType::kPrAnswer,
       .remote_type = SdpType::kOffer},
      // Cannot send ACTIVE or PASSIVE in an offer (must handle, must not send)
      {.local_role = CONNECTIONROLE_ACTIVE,
       .remote_role = CONNECTIONROLE_PASSIVE,
       .local_type = SdpType::kOffer,
       .remote_type = SdpType::kAnswer},
      {.local_role = CONNECTIONROLE_PASSIVE,
       .remote_role = CONNECTIONROLE_ACTIVE,
       .local_type = SdpType::kOffer,
       .remote_type = SdpType::kAnswer},
      {.local_role = CONNECTIONROLE_PASSIVE,
       .remote_role = CONNECTIONROLE_ACTPASS,
       .local_type = SdpType::kOffer,
       .remote_type = SdpType::kAnswer},
      {.local_role = CONNECTIONROLE_ACTIVE,
       .remote_role = CONNECTIONROLE_PASSIVE,
       .local_type = SdpType::kOffer,
       .remote_type = SdpType::kPrAnswer},
      {.local_role = CONNECTIONROLE_PASSIVE,
       .remote_role = CONNECTIONROLE_ACTIVE,
       .local_type = SdpType::kOffer,
       .remote_type = SdpType::kPrAnswer},
      {.local_role = CONNECTIONROLE_PASSIVE,
       .remote_role = CONNECTIONROLE_ACTPASS,
       .local_type = SdpType::kOffer,
       .remote_type = SdpType::kPrAnswer}};

  for (auto& param : offerer_without_actpass_params) {
    jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);
    jsep_transport_->SetLocalCertificate(certificate);

    local_description.transport_desc.connection_role = param.local_role;
    remote_description.transport_desc.connection_role = param.remote_role;

    if (param.local_type == SdpType::kOffer) {
      EXPECT_TRUE(jsep_transport_
                      ->SetLocalJsepTransportDescription(local_description,
                                                         param.local_type)
                      .ok())
          << param;
      EXPECT_FALSE(jsep_transport_
                       ->SetRemoteJsepTransportDescription(remote_description,
                                                           param.remote_type)
                       .ok())
          << param;
    } else {
      EXPECT_TRUE(jsep_transport_
                      ->SetRemoteJsepTransportDescription(remote_description,
                                                          param.remote_type)
                      .ok())
          << param;
      EXPECT_FALSE(jsep_transport_
                       ->SetLocalJsepTransportDescription(local_description,
                                                          param.local_type)
                       .ok())
          << param;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(JsepTransport2Test,
                         JsepTransport2WithRtcpMux,
                         ::testing::Bool());

// Test that a reoffer in the opposite direction is successful as long as the
// role isn't changing. Doesn't test every possible combination like the test
// above.
TEST_F(JsepTransport2Test, ValidDtlsReofferFromAnswerer) {
  // Just use the same certificate for both sides; doesn't really matter in a
  // non end-to-end test.
  scoped_refptr<RTCCertificate> certificate =
      RTCCertificate::Create(SSLIdentity::Create("testing", KT_ECDSA));
  bool rtcp_mux_enabled = true;
  jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);
  jsep_transport_->SetLocalCertificate(certificate);

  JsepTransportDescription local_offer =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag1, kIcePwd1,
                                   certificate, CONNECTIONROLE_ACTPASS);
  JsepTransportDescription remote_answer =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag2, kIcePwd2,
                                   certificate, CONNECTIONROLE_ACTIVE);

  EXPECT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_offer, SdpType::kOffer)
          .ok());
  EXPECT_TRUE(
      jsep_transport_
          ->SetRemoteJsepTransportDescription(remote_answer, SdpType::kAnswer)
          .ok());

  // We were actpass->active previously, now in the other direction it's
  // actpass->passive.
  JsepTransportDescription remote_offer =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag2, kIcePwd2,
                                   certificate, CONNECTIONROLE_ACTPASS);
  JsepTransportDescription local_answer =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag1, kIcePwd1,
                                   certificate, CONNECTIONROLE_PASSIVE);

  EXPECT_TRUE(
      jsep_transport_
          ->SetRemoteJsepTransportDescription(remote_offer, SdpType::kOffer)
          .ok());
  EXPECT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_answer, SdpType::kAnswer)
          .ok());
}

// Test that a reoffer in the opposite direction fails if the role changes.
// Inverse of test above.
TEST_F(JsepTransport2Test, InvalidDtlsReofferFromAnswerer) {
  // Just use the same certificate for both sides; doesn't really matter in a
  // non end-to-end test.
  scoped_refptr<RTCCertificate> certificate =
      RTCCertificate::Create(SSLIdentity::Create("testing", KT_ECDSA));
  bool rtcp_mux_enabled = true;
  jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);
  jsep_transport_->SetLocalCertificate(certificate);

  JsepTransportDescription local_offer =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag1, kIcePwd1,
                                   certificate, CONNECTIONROLE_ACTPASS);
  JsepTransportDescription remote_answer =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag2, kIcePwd2,
                                   certificate, CONNECTIONROLE_ACTIVE);

  EXPECT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_offer, SdpType::kOffer)
          .ok());
  EXPECT_TRUE(
      jsep_transport_
          ->SetRemoteJsepTransportDescription(remote_answer, SdpType::kAnswer)
          .ok());

  // Changing role to passive here isn't allowed. Though for some reason this
  // only fails in SetLocalTransportDescription.
  JsepTransportDescription remote_offer =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag2, kIcePwd2,
                                   certificate, CONNECTIONROLE_PASSIVE);
  JsepTransportDescription local_answer =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag1, kIcePwd1,
                                   certificate, CONNECTIONROLE_ACTIVE);

  EXPECT_TRUE(
      jsep_transport_
          ->SetRemoteJsepTransportDescription(remote_offer, SdpType::kOffer)
          .ok());
  EXPECT_FALSE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_answer, SdpType::kAnswer)
          .ok());
}

// Test that a remote offer with the current negotiated role can be accepted.
// This is allowed by dtls-sdp, though we'll never generate such an offer,
// since JSEP requires generating "actpass".
TEST_F(JsepTransport2Test, RemoteOfferWithCurrentNegotiatedDtlsRole) {
  scoped_refptr<RTCCertificate> certificate =
      RTCCertificate::Create(SSLIdentity::Create("testing", KT_ECDSA));
  bool rtcp_mux_enabled = true;
  jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);
  jsep_transport_->SetLocalCertificate(certificate);

  JsepTransportDescription remote_desc =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag1, kIcePwd1,
                                   certificate, CONNECTIONROLE_ACTPASS);
  JsepTransportDescription local_desc =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag2, kIcePwd2,
                                   certificate, CONNECTIONROLE_ACTIVE);

  // Normal initial offer/answer with "actpass" in the offer and "active" in
  // the answer.
  ASSERT_TRUE(
      jsep_transport_
          ->SetRemoteJsepTransportDescription(remote_desc, SdpType::kOffer)
          .ok());
  ASSERT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_desc, SdpType::kAnswer)
          .ok());

  // Sanity check that role was actually negotiated.
  std::optional<SSLRole> role = jsep_transport_->GetDtlsRole();
  ASSERT_TRUE(role);
  EXPECT_EQ(SSL_CLIENT, *role);

  // Subsequent offer with current negotiated role of "passive".
  remote_desc.transport_desc.connection_role = CONNECTIONROLE_PASSIVE;
  EXPECT_TRUE(
      jsep_transport_
          ->SetRemoteJsepTransportDescription(remote_desc, SdpType::kOffer)
          .ok());
  EXPECT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_desc, SdpType::kAnswer)
          .ok());
}

// Test that a remote offer with the inverse of the current negotiated DTLS
// role is rejected.
TEST_F(JsepTransport2Test, RemoteOfferThatChangesNegotiatedDtlsRole) {
  scoped_refptr<RTCCertificate> certificate =
      RTCCertificate::Create(SSLIdentity::Create("testing", KT_ECDSA));
  bool rtcp_mux_enabled = true;
  jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);
  jsep_transport_->SetLocalCertificate(certificate);

  JsepTransportDescription remote_desc =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag1, kIcePwd1,
                                   certificate, CONNECTIONROLE_ACTPASS);
  JsepTransportDescription local_desc =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag2, kIcePwd2,
                                   certificate, CONNECTIONROLE_ACTIVE);

  // Normal initial offer/answer with "actpass" in the offer and "active" in
  // the answer.
  ASSERT_TRUE(
      jsep_transport_
          ->SetRemoteJsepTransportDescription(remote_desc, SdpType::kOffer)
          .ok());
  ASSERT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_desc, SdpType::kAnswer)
          .ok());

  // Sanity check that role was actually negotiated.
  std::optional<SSLRole> role = jsep_transport_->GetDtlsRole();
  ASSERT_TRUE(role);
  EXPECT_EQ(SSL_CLIENT, *role);

  // Subsequent offer with current negotiated role of "passive".
  remote_desc.transport_desc.connection_role = CONNECTIONROLE_ACTIVE;
  EXPECT_TRUE(
      jsep_transport_
          ->SetRemoteJsepTransportDescription(remote_desc, SdpType::kOffer)
          .ok());
  EXPECT_FALSE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_desc, SdpType::kAnswer)
          .ok());
}

// Test that a remote offer which changes both fingerprint and role is accepted.
TEST_F(JsepTransport2Test, RemoteOfferThatChangesFingerprintAndDtlsRole) {
  scoped_refptr<RTCCertificate> certificate =
      RTCCertificate::Create(SSLIdentity::Create("testing1", KT_ECDSA));
  scoped_refptr<RTCCertificate> certificate2 =
      RTCCertificate::Create(SSLIdentity::Create("testing2", KT_ECDSA));
  bool rtcp_mux_enabled = true;
  jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);
  jsep_transport_->SetLocalCertificate(certificate);

  JsepTransportDescription remote_desc =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag1, kIcePwd1,
                                   certificate, CONNECTIONROLE_ACTPASS);
  JsepTransportDescription remote_desc2 =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag1, kIcePwd1,
                                   certificate2, CONNECTIONROLE_ACTPASS);

  JsepTransportDescription local_desc =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag2, kIcePwd2,
                                   certificate, CONNECTIONROLE_ACTIVE);

  // Normal initial offer/answer with "actpass" in the offer and "active" in
  // the answer.
  ASSERT_TRUE(
      jsep_transport_
          ->SetRemoteJsepTransportDescription(remote_desc, SdpType::kOffer)
          .ok());
  ASSERT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_desc, SdpType::kAnswer)
          .ok());

  // Sanity check that role was actually negotiated.
  std::optional<SSLRole> role = jsep_transport_->GetDtlsRole();
  ASSERT_TRUE(role);
  EXPECT_EQ(SSL_CLIENT, *role);

  // Subsequent exchange with new remote fingerprint and different role.
  local_desc.transport_desc.connection_role = CONNECTIONROLE_PASSIVE;
  EXPECT_TRUE(
      jsep_transport_
          ->SetRemoteJsepTransportDescription(remote_desc2, SdpType::kOffer)
          .ok());
  EXPECT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_desc, SdpType::kAnswer)
          .ok());

  role = jsep_transport_->GetDtlsRole();
  ASSERT_TRUE(role);
  EXPECT_EQ(SSL_SERVER, *role);
}

// Testing that a legacy client that doesn't use the setup attribute will be
// interpreted as having an active role.
TEST_F(JsepTransport2Test, DtlsSetupWithLegacyAsAnswerer) {
  scoped_refptr<RTCCertificate> certificate =
      RTCCertificate::Create(SSLIdentity::Create("testing", KT_ECDSA));
  bool rtcp_mux_enabled = true;
  jsep_transport_ = CreateJsepTransport2(rtcp_mux_enabled);
  jsep_transport_->SetLocalCertificate(certificate);

  JsepTransportDescription remote_desc =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag1, kIcePwd1,
                                   certificate, CONNECTIONROLE_ACTPASS);
  JsepTransportDescription local_desc =
      MakeJsepTransportDescription(rtcp_mux_enabled, kIceUfrag2, kIcePwd2,
                                   certificate, CONNECTIONROLE_ACTIVE);

  local_desc.transport_desc.connection_role = CONNECTIONROLE_ACTPASS;
  ASSERT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_desc, SdpType::kOffer)
          .ok());
  // Use CONNECTIONROLE_NONE to simulate legacy endpoint.
  remote_desc.transport_desc.connection_role = CONNECTIONROLE_NONE;
  ASSERT_TRUE(
      jsep_transport_
          ->SetRemoteJsepTransportDescription(remote_desc, SdpType::kAnswer)
          .ok());

  std::optional<SSLRole> role = jsep_transport_->GetDtlsRole();
  ASSERT_TRUE(role);
  // Since legacy answer omitted setup atribute, and we offered actpass, we
  // should act as passive (server).
  EXPECT_EQ(SSL_SERVER, *role);
}

// Tests that when the RTCP mux is successfully negotiated, the RTCP transport
// will be destroyed and the SignalRtpMuxActive will be fired.
TEST_F(JsepTransport2Test, RtcpMuxNegotiation) {
  jsep_transport_ = CreateJsepTransport2(/*rtcp_mux_enabled=*/false);
  JsepTransportDescription local_desc;
  local_desc.rtcp_mux_enabled = true;
  ASSERT_NE(nullptr, jsep_transport_->rtcp_dtls_transport());
  EXPECT_FALSE(signal_rtcp_mux_active_received_);

  // The remote side supports RTCP-mux.
  JsepTransportDescription remote_desc;
  remote_desc.rtcp_mux_enabled = true;
  ASSERT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_desc, SdpType::kOffer)
          .ok());
  ASSERT_TRUE(
      jsep_transport_
          ->SetRemoteJsepTransportDescription(remote_desc, SdpType::kAnswer)
          .ok());

  EXPECT_EQ(nullptr, jsep_transport_->rtcp_dtls_transport());
  EXPECT_TRUE(signal_rtcp_mux_active_received_);

  // The remote side doesn't support RTCP-mux.
  jsep_transport_ = CreateJsepTransport2(/*rtcp_mux_enabled=*/false);
  signal_rtcp_mux_active_received_ = false;
  remote_desc.rtcp_mux_enabled = false;
  ASSERT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(local_desc, SdpType::kOffer)
          .ok());
  ASSERT_TRUE(
      jsep_transport_
          ->SetRemoteJsepTransportDescription(remote_desc, SdpType::kAnswer)
          .ok());

  EXPECT_NE(nullptr, jsep_transport_->rtcp_dtls_transport());
  EXPECT_FALSE(signal_rtcp_mux_active_received_);
}

// Simulates a completed DTLS handshake between two fake DTLS transports.
void ConnectFakeDtlsTransports(DtlsTransportInternal* dtls1,
                               DtlsTransportInternal* dtls2) {
  auto* fake_dtls1 = static_cast<FakeDtlsTransport*>(dtls1);
  auto* fake_dtls2 = static_cast<FakeDtlsTransport*>(dtls2);
  fake_dtls1->SetLocalCertificate(
      RTCCertificate::Create(SSLIdentity::Create("session1", KT_DEFAULT)));
  fake_dtls2->SetLocalCertificate(
      RTCCertificate::Create(SSLIdentity::Create("session2", KT_DEFAULT)));
  fake_dtls1->SetDestination(fake_dtls2);
}

// Applies `description` as the offer from `offerer` and as the answer from
// `answerer`.
void NegotiateTransports(JsepTransport& offerer,
                         JsepTransport& answerer,
                         const JsepTransportDescription& description) {
  ASSERT_TRUE(
      offerer.SetLocalJsepTransportDescription(description, SdpType::kOffer)
          .ok());
  ASSERT_TRUE(
      answerer.SetRemoteJsepTransportDescription(description, SdpType::kOffer)
          .ok());
  ASSERT_TRUE(
      answerer.SetLocalJsepTransportDescription(description, SdpType::kAnswer)
          .ok());
  ASSERT_TRUE(
      offerer.SetRemoteJsepTransportDescription(description, SdpType::kAnswer)
          .ok());
}

// Negotiates without RTCP mux and completes the DTLS handshakes of both the RTP
// and the RTCP transports, so that SRTP becomes active.
void EstablishSrtpWithoutRtcpMux(JsepTransport& offerer,
                                 JsepTransport& answerer) {
  JsepTransportDescription description;
  description.rtcp_mux_enabled = false;
  ASSERT_NO_FATAL_FAILURE(NegotiateTransports(offerer, answerer, description));
  ASSERT_NE(nullptr, offerer.rtcp_dtls_transport());
  ASSERT_NE(nullptr, answerer.rtcp_dtls_transport());
  ConnectFakeDtlsTransports(offerer.rtp_dtls_transport(),
                            answerer.rtp_dtls_transport());
  ConnectFakeDtlsTransports(offerer.rtcp_dtls_transport(),
                            answerer.rtcp_dtls_transport());
  ASSERT_TRUE(offerer.rtp_transport()->IsSrtpActive());
  ASSERT_TRUE(answerer.rtp_transport()->IsSrtpActive());
}

// Sends an RTP packet and returns the SRTP packet as it was sent on the wire.
CopyOnWriteBuffer SendRtpPacketAndGetWirePacket(JsepTransport& sender,
                                                uint16_t sequence_number) {
  // Room for the auth tag of the default crypto suite of FakeDtlsTransport.
  constexpr size_t kRtpAuthTagLen = 10;
  CopyOnWriteBuffer packet(kPcmuFrame,
                           /*capacity=*/sizeof(kPcmuFrame) + kRtpAuthTagLen);
  SetBE16(packet.AsMutableSpan().subspan(2), sequence_number);
  AsyncSocketPacketOptions options;
  EXPECT_TRUE(
      sender.rtp_transport()->SendRtpPacket(&packet, options, PF_SRTP_BYPASS));
  return static_cast<FakeDtlsTransport*>(sender.rtp_dtls_transport())
      ->fake_ice_transport()
      ->last_sent_packet();
}

// Counts the RTP packets that `rtp_transport` delivers, i.e. the packets that
// passed SRTP processing. Must be destroyed before `rtp_transport`.
class ReceivedRtpPacketCounter {
 public:
  explicit ReceivedRtpPacketCounter(RtpTransportInternal* rtp_transport)
      : rtp_transport_(rtp_transport) {
    RtpDemuxerCriteria criteria;
    // 0x00 is the payload type used in kPcmuFrame.
    criteria.payload_types() = {0x00};
    EXPECT_TRUE(rtp_transport_->RegisterRtpDemuxerSink(criteria, &observer_));
  }
  ~ReceivedRtpPacketCounter() {
    rtp_transport_->UnregisterRtpDemuxerSink(&observer_);
  }

  int count() const { return observer_.rtp_count(); }

 private:
  RtpTransportInternal* const rtp_transport_;
  TransportObserver observer_;
};

// Tests that activating RTCP mux in a subsequent offer/answer exchange keeps
// the SRTP sessions that were set up while RTCP mux was disabled. Recreating
// the sessions with the same keys would reset their replay protection and
// rollover counters, which the test detects by replaying earlier packets.
TEST_F(JsepTransport2Test, RtcpMuxActivationPreservesSrtpSessions) {
  std::unique_ptr<JsepTransport> transport1 =
      CreateJsepTransport2(/*rtcp_mux_enabled=*/false);
  std::unique_ptr<JsepTransport> transport2 =
      CreateJsepTransport2(/*rtcp_mux_enabled=*/false);
  ReceivedRtpPacketCounter received1(transport1->rtp_transport());
  ReceivedRtpPacketCounter received2(transport2->rtp_transport());
  ASSERT_NO_FATAL_FAILURE(
      EstablishSrtpWithoutRtcpMux(*transport1, *transport2));

  CopyOnWriteBuffer packet_1to2 =
      SendRtpPacketAndGetWirePacket(*transport1, /*sequence_number=*/1);
  CopyOnWriteBuffer packet_2to1 =
      SendRtpPacketAndGetWirePacket(*transport2, /*sequence_number=*/1);
  ASSERT_EQ(1, received2.count());
  ASSERT_EQ(1, received1.count());

  // `transport2` activates RTCP mux when applying the answer as a local
  // description and `transport1` when applying it as a remote description.
  JsepTransportDescription description;
  description.rtcp_mux_enabled = true;
  ASSERT_NO_FATAL_FAILURE(
      NegotiateTransports(*transport1, *transport2, description));
  ASSERT_EQ(nullptr, transport1->rtcp_dtls_transport());
  ASSERT_EQ(nullptr, transport2->rtcp_dtls_transport());

  // Send the earlier SRTP packets again, unmodified. Both receivers must
  // reject them as replays.
  AsyncSocketPacketOptions options;
  transport1->rtp_dtls_transport()->SendPacket(
      packet_1to2.data<char>(), packet_1to2.size(), options, PF_SRTP_BYPASS);
  transport2->rtp_dtls_transport()->SendPacket(
      packet_2to1.data<char>(), packet_2to1.size(), options, PF_SRTP_BYPASS);
  EXPECT_EQ(1, received2.count());
  EXPECT_EQ(1, received1.count());

  // New packets are still received.
  SendRtpPacketAndGetWirePacket(*transport1, /*sequence_number=*/2);
  SendRtpPacketAndGetWirePacket(*transport2, /*sequence_number=*/2);
  EXPECT_EQ(2, received2.count());
  EXPECT_EQ(2, received1.count());
}

// Tests that the remote candidates can be added to the transports after both
// local and remote descriptions are set.
TEST_F(JsepTransport2Test, AddRemoteCandidates) {
  jsep_transport_ = CreateJsepTransport2(/*rtcp_mux_enabled=*/true);
  auto fake_ice_transport = static_cast<FakeIceTransportInternal*>(
      jsep_transport_->rtp_dtls_transport()->ice_transport());

  Candidates candidates;
  candidates.push_back(CreateCandidate(/*COMPONENT_RTP*/ 1));
  candidates.push_back(CreateCandidate(/*COMPONENT_RTP*/ 1));

  JsepTransportDescription desc;
  ASSERT_TRUE(
      jsep_transport_->SetLocalJsepTransportDescription(desc, SdpType::kOffer)
          .ok());
  // Expected to fail because the remote description is unset.
  EXPECT_FALSE(jsep_transport_->AddRemoteCandidates(candidates).ok());

  ASSERT_TRUE(
      jsep_transport_->SetRemoteJsepTransportDescription(desc, SdpType::kAnswer)
          .ok());
  EXPECT_EQ(0u, fake_ice_transport->remote_candidates().size());
  EXPECT_TRUE(jsep_transport_->AddRemoteCandidates(candidates).ok());
  EXPECT_EQ(candidates.size(), fake_ice_transport->remote_candidates().size());
}

enum class Scenario {
  kDtlsBeforeCallerSendOffer,
  kDtlsBeforeCallerSetAnswer,
  kDtlsAfterCallerSetAnswer,
};

class JsepTransport2HeaderExtensionTest
    : public JsepTransport2Test,
      public ::testing::WithParamInterface<std::tuple<Scenario, bool>> {
 protected:
  JsepTransport2HeaderExtensionTest() {}

  void CreateJsepTransportPair() {
    jsep_transport1_ = CreateJsepTransport2(/*rtcp_mux_enabled=*/true);
    jsep_transport2_ = CreateJsepTransport2(/*rtcp_mux_enabled=*/true);

    auto fake_dtls1 =
        static_cast<FakeDtlsTransport*>(jsep_transport1_->rtp_dtls_transport());
    auto fake_dtls2 =
        static_cast<FakeDtlsTransport*>(jsep_transport2_->rtp_dtls_transport());

    fake_dtls1->fake_ice_transport()->RegisterReceivedPacketCallback(
        this, [&](PacketTransportInternal* transport,
                  const ReceivedIpPacket& packet) {
          OnReadPacket1(transport, packet);
        });
    fake_dtls2->fake_ice_transport()->RegisterReceivedPacketCallback(
        this, [&](PacketTransportInternal* transport,
                  const ReceivedIpPacket& packet) {
          OnReadPacket2(transport, packet);
        });

    auto cert1 =
        RTCCertificate::Create(SSLIdentity::Create("session1", KT_DEFAULT));
    jsep_transport1_->rtp_dtls_transport()->SetLocalCertificate(cert1);
    auto cert2 =
        RTCCertificate::Create(SSLIdentity::Create("session1", KT_DEFAULT));
    jsep_transport2_->rtp_dtls_transport()->SetLocalCertificate(cert2);
  }

  void OnReadPacket1(PacketTransportInternal* transport,
                     const ReceivedIpPacket& packet) {
    RTC_LOG(LS_INFO) << "JsepTransport 1 Received a packet.";
    CompareHeaderExtensions(kPcmuFrameWithExtensions, packet.payload(),
                            recv_encrypted_headers1_, false);
    received_packet_count_++;
  }

  void OnReadPacket2(PacketTransportInternal* transport,
                     const ReceivedIpPacket& packet) {
    RTC_LOG(LS_INFO) << "JsepTransport 2 Received a packet.";
    CompareHeaderExtensions(kPcmuFrameWithExtensions, packet.payload(),
                            recv_encrypted_headers2_, false);
    received_packet_count_++;
  }

  void ConnectTransport() {
    auto rtp_dtls_transport1 =
        static_cast<FakeDtlsTransport*>(jsep_transport1_->rtp_dtls_transport());
    auto rtp_dtls_transport2 =
        static_cast<FakeDtlsTransport*>(jsep_transport2_->rtp_dtls_transport());
    rtp_dtls_transport1->SetDestination(rtp_dtls_transport2);
  }

  int GetRtpAuthLen() {
    bool use_gcm = std::get<1>(GetParam());
    if (use_gcm) {
      return 16;
    }
    return 10;
  }

  void TestSendRecvPacketWithEncryptedHeaderExtension() {
    TestOneWaySendRecvPacketWithEncryptedHeaderExtension(
        jsep_transport1_.get());
    TestOneWaySendRecvPacketWithEncryptedHeaderExtension(
        jsep_transport2_.get());
  }

  void TestOneWaySendRecvPacketWithEncryptedHeaderExtension(
      JsepTransport* sender_transport) {
    CopyOnWriteBuffer rtp_packet(
        kPcmuFrameWithExtensions,
        /*capacity=*/sizeof(kPcmuFrameWithExtensions) + GetRtpAuthLen());
    // In order to be able to run this test function multiple times we can not
    // use the same sequence number twice. Increase the sequence number by one.
    SetBE16(rtp_packet.AsMutableSpan().subspan(2), ++sequence_number_);

    int packet_count_before = received_packet_count_;
    AsyncSocketPacketOptions options;
    // Send a packet and verify that the packet can be successfully received and
    // decrypted.
    ASSERT_TRUE(sender_transport->rtp_transport()->SendRtpPacket(
        &rtp_packet, options, PF_SRTP_BYPASS));
    EXPECT_EQ(packet_count_before + 1, received_packet_count_);
  }

  int sequence_number_ = 0;
  int received_packet_count_ = 0;
  std::unique_ptr<JsepTransport> jsep_transport1_;
  std::unique_ptr<JsepTransport> jsep_transport2_;
  std::vector<RtpHeaderExtensionId> recv_encrypted_headers1_;
  std::vector<RtpHeaderExtensionId> recv_encrypted_headers2_;
};

// Test that the encrypted header extension works and can be changed in
// different scenarios.
TEST_P(JsepTransport2HeaderExtensionTest, EncryptedHeaderExtensionNegotiation) {
  Scenario scenario = std::get<0>(GetParam());
  bool use_gcm = std::get<1>(GetParam());
  CreateJsepTransportPair();
  recv_encrypted_headers1_.push_back(kHeaderExtensionIDs[0]);
  recv_encrypted_headers2_.push_back(kHeaderExtensionIDs[1]);

  if (use_gcm) {
    auto fake_dtls1 =
        static_cast<FakeDtlsTransport*>(jsep_transport1_->rtp_dtls_transport());
    auto fake_dtls2 =
        static_cast<FakeDtlsTransport*>(jsep_transport2_->rtp_dtls_transport());

    fake_dtls1->SetSrtpCryptoSuite(kSrtpAeadAes256Gcm);
    fake_dtls2->SetSrtpCryptoSuite(kSrtpAeadAes256Gcm);
  }

  if (scenario == Scenario::kDtlsBeforeCallerSendOffer) {
    ConnectTransport();
  }

  JsepTransportDescription offer_desc;
  offer_desc.encrypted_header_extension_ids = recv_encrypted_headers1_;
  ASSERT_TRUE(
      jsep_transport1_
          ->SetLocalJsepTransportDescription(offer_desc, SdpType::kOffer)
          .ok());
  ASSERT_TRUE(
      jsep_transport2_
          ->SetRemoteJsepTransportDescription(offer_desc, SdpType::kOffer)
          .ok());

  JsepTransportDescription answer_desc;
  answer_desc.encrypted_header_extension_ids = recv_encrypted_headers2_;
  ASSERT_TRUE(
      jsep_transport2_
          ->SetLocalJsepTransportDescription(answer_desc, SdpType::kAnswer)
          .ok());

  if (scenario == Scenario::kDtlsBeforeCallerSetAnswer) {
    ConnectTransport();
    // Sending packet from transport2 to transport1 should work when they are
    // partially configured.
    TestOneWaySendRecvPacketWithEncryptedHeaderExtension(
        /*sender_transport=*/jsep_transport2_.get());
  }

  ASSERT_TRUE(
      jsep_transport1_
          ->SetRemoteJsepTransportDescription(answer_desc, SdpType::kAnswer)
          .ok());

  if (scenario == Scenario::kDtlsAfterCallerSetAnswer) {
    ConnectTransport();
  }
  EXPECT_TRUE(jsep_transport1_->rtp_transport()->IsSrtpActive());
  EXPECT_TRUE(jsep_transport2_->rtp_transport()->IsSrtpActive());
  TestSendRecvPacketWithEncryptedHeaderExtension();

  // Change the encrypted header extension in a new offer/answer exchange.
  recv_encrypted_headers1_.clear();
  recv_encrypted_headers2_.clear();
  recv_encrypted_headers1_.push_back(kHeaderExtensionIDs[1]);
  recv_encrypted_headers2_.push_back(kHeaderExtensionIDs[0]);
  offer_desc.encrypted_header_extension_ids = recv_encrypted_headers1_;
  answer_desc.encrypted_header_extension_ids = recv_encrypted_headers2_;
  ASSERT_TRUE(
      jsep_transport1_
          ->SetLocalJsepTransportDescription(offer_desc, SdpType::kOffer)
          .ok());
  ASSERT_TRUE(
      jsep_transport2_
          ->SetRemoteJsepTransportDescription(offer_desc, SdpType::kOffer)
          .ok());
  ASSERT_TRUE(
      jsep_transport2_
          ->SetLocalJsepTransportDescription(answer_desc, SdpType::kAnswer)
          .ok());
  ASSERT_TRUE(
      jsep_transport1_
          ->SetRemoteJsepTransportDescription(answer_desc, SdpType::kAnswer)
          .ok());
  EXPECT_TRUE(jsep_transport1_->rtp_transport()->IsSrtpActive());
  EXPECT_TRUE(jsep_transport2_->rtp_transport()->IsSrtpActive());
  TestSendRecvPacketWithEncryptedHeaderExtension();
}

INSTANTIATE_TEST_SUITE_P(
    JsepTransport2Test,
    JsepTransport2HeaderExtensionTest,
    ::testing::Values(
        std::make_tuple(Scenario::kDtlsBeforeCallerSendOffer, true),
        std::make_tuple(Scenario::kDtlsBeforeCallerSetAnswer, true),
        std::make_tuple(Scenario::kDtlsAfterCallerSetAnswer, true),
        std::make_tuple(Scenario::kDtlsBeforeCallerSendOffer, false),
        std::make_tuple(Scenario::kDtlsBeforeCallerSetAnswer, false),
        std::make_tuple(Scenario::kDtlsAfterCallerSetAnswer, false)));

// This test verifies the ICE parameters are properly applied to the transports.
TEST_F(JsepTransport2Test, SetIceParametersWithRenomination) {
  jsep_transport_ = CreateJsepTransport2(/* rtcp_mux_enabled= */ true);

  JsepTransportDescription jsep_description;
  jsep_description.transport_desc = TransportDescription(kIceUfrag1, kIcePwd1);
  jsep_description.transport_desc.AddOption(ICE_OPTION_RENOMINATION);
  ASSERT_TRUE(
      jsep_transport_
          ->SetLocalJsepTransportDescription(jsep_description, SdpType::kOffer)
          .ok());
  auto fake_ice_transport = static_cast<FakeIceTransportInternal*>(
      jsep_transport_->rtp_dtls_transport()->ice_transport());
  EXPECT_EQ(ICEMODE_FULL, fake_ice_transport->remote_ice_mode());
  EXPECT_EQ(kIceUfrag1, fake_ice_transport->local_ice_parameters()->ufrag);
  EXPECT_EQ(kIcePwd1, fake_ice_transport->local_ice_parameters()->pwd);
  EXPECT_TRUE(fake_ice_transport->local_ice_parameters()->renomination);

  jsep_description.transport_desc = TransportDescription(kIceUfrag2, kIcePwd2);
  jsep_description.transport_desc.AddOption(ICE_OPTION_RENOMINATION);
  ASSERT_TRUE(jsep_transport_
                  ->SetRemoteJsepTransportDescription(jsep_description,
                                                      SdpType::kAnswer)
                  .ok());
  fake_ice_transport = static_cast<FakeIceTransportInternal*>(
      jsep_transport_->rtp_dtls_transport()->ice_transport());
  EXPECT_EQ(ICEMODE_FULL, fake_ice_transport->remote_ice_mode());
  EXPECT_EQ(kIceUfrag2, fake_ice_transport->remote_ice_parameters()->ufrag);
  EXPECT_EQ(kIcePwd2, fake_ice_transport->remote_ice_parameters()->pwd);
  EXPECT_TRUE(fake_ice_transport->remote_ice_parameters()->renomination);
}

}  // namespace
}  // namespace webrtc
