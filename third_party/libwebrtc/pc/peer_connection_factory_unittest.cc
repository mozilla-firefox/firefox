/*
 *  Copyright 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/peer_connection_factory.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "api/audio/audio_device.h"
#include "api/audio/audio_processing.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/audio_options.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/create_peerconnection_factory.h"
#include "api/data_channel_interface.h"
#include "api/enable_media_with_defaults.h"
#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/field_trials_view.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/test/mock_packet_socket_factory.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/video_decoder_factory_template.h"
#include "api/video_codecs/video_decoder_factory_template_dav1d_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_open_h264_adapter.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libaom_av1_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_open_h264_adapter.h"
#include "media/base/fake_frame_source.h"
#include "media/base/media_constants.h"
#include "modules/audio_processing/include/mock_audio_processing.h"
#include "p2p/base/port.h"
#include "p2p/base/port_allocator.h"
#include "p2p/test/fake_port_allocator.h"
#include "pc/connection_context.h"
#include "pc/test/fake_audio_capture_module.h"
#include "pc/test/fake_rtc_certificate_generator.h"
#include "pc/test/fake_video_track_renderer.h"
#include "pc/test/fake_video_track_source.h"
#include "rtc_base/event.h"
#include "rtc_base/internal/default_socket_server.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/network.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/socket_server.h"
#include "rtc_base/system/ignore_warnings.h"
#include "rtc_base/thread.h"
#include "test/create_test_environment.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"
#include "test/testsupport/file_utils.h"

namespace webrtc {
namespace {

using test::MockAudioProcessing;
using test::MockAudioProcessingBuilder;
using ::testing::_;
using ::testing::A;
using ::testing::AtLeast;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::UnorderedElementsAre;

constexpr char kStunIceServer[] = "stun:stun.l.google.com:19302";
constexpr char kTurnIceServer[] = "turn:test.com:1234";
constexpr char kTurnIceServerWithTransport[] = "turn:hello.com?transport=tcp";
constexpr char kSecureTurnIceServer[] = "turns:hello.com?transport=tcp";
constexpr char kSecureTurnIceServerWithoutTransportParam[] =
    "turns:hello.com:443";
constexpr char kSecureTurnIceServerWithoutTransportAndPortParam[] =
    "turns:hello.com";
constexpr char kTurnIceServerWithNoUsernameInUri[] = "turn:test.com:1234";
constexpr char kTurnPassword[] = "turnpassword";
constexpr int kDefaultStunPort = 3478;
constexpr int kDefaultStunTlsPort = 5349;
constexpr char kTurnUsername[] = "test";
constexpr char kStunIceServerWithIPv4Address[] = "stun:1.2.3.4:1234";
constexpr char kStunIceServerWithIPv4AddressWithoutPort[] = "stun:1.2.3.4";
constexpr char kStunIceServerWithIPv6Address[] = "stun:[2401:fa00:4::]:1234";
constexpr char kStunIceServerWithIPv6AddressWithoutPort[] =
    "stun:[2401:fa00:4::]";
constexpr char kTurnIceServerWithIPv6Address[] = "turn:[2401:fa00:4::]:1234";

class NullPeerConnectionObserver : public PeerConnectionObserver {
 public:
  ~NullPeerConnectionObserver() override = default;
  void OnSignalingChange(
      PeerConnectionInterface::SignalingState new_state) override {}
  void OnAddStream(scoped_refptr<MediaStreamInterface> stream) override {}
  void OnRemoveStream(scoped_refptr<MediaStreamInterface> stream) override {}
  void OnDataChannel(
      scoped_refptr<DataChannelInterface> data_channel) override {}
  void OnRenegotiationNeeded() override {}
  void OnIceConnectionChange(
      PeerConnectionInterface::IceConnectionState new_state) override {}
  void OnIceGatheringChange(
      PeerConnectionInterface::IceGatheringState new_state) override {}
  void OnIceCandidate(const IceCandidate* candidate) override {}
  void OnIceCandidateRemoved(const IceCandidate* candidate) override {}
};

class MockNetworkManager : public NetworkManager {
 public:
  MOCK_METHOD(void, StartUpdating, (), (override));
  MOCK_METHOD(void, StopUpdating, (), (override));
  MOCK_METHOD(std::vector<const Network*>, GetNetworks, (), (const, override));
  MOCK_METHOD(std::vector<const Network*>,
              GetAnyAddressNetworks,
              (),
              (override));
};

class PeerConnectionFactoryTest : public ::testing::Test {
 public:
  PeerConnectionFactoryTest()
      : socket_server_(CreateDefaultSocketServer()),
        main_thread_(socket_server_.get()) {}

 private:
  void SetUp() override {
    // Use fake audio device module since we're only testing the interface
    // level, and using a real one could make tests flaky e.g. when run in
    // parallel.
    factory_ = CreatePeerConnectionFactory(
        Thread::Current(), Thread::Current(),
        scoped_refptr<AudioDeviceModule>(FakeAudioCaptureModule::Create()),
        CreateBuiltinAudioEncoderFactory(), CreateBuiltinAudioDecoderFactory(),
        std::make_unique<VideoEncoderFactoryTemplate<
            LibvpxVp8EncoderTemplateAdapter, LibvpxVp9EncoderTemplateAdapter,
            OpenH264EncoderTemplateAdapter, LibaomAv1EncoderTemplateAdapter>>(),
        std::make_unique<VideoDecoderFactoryTemplate<
            LibvpxVp8DecoderTemplateAdapter, LibvpxVp9DecoderTemplateAdapter,
            OpenH264DecoderTemplateAdapter, Dav1dDecoderTemplateAdapter>>(),
        nullptr /* audio_mixer */, nullptr /* audio_processing */,
        nullptr /* audio_frame_processor */, CreateTestFieldTrialsPtr());

    ASSERT_TRUE(factory_.get() != nullptr);
    port_allocator_ = std::make_unique<FakePortAllocator>(
        CreateTestEnvironment(), socket_server_.get());
    raw_port_allocator_ = port_allocator_.get();
  }

 protected:
  void VerifyStunServers(ServerAddresses stun_servers) {
    EXPECT_EQ(stun_servers, raw_port_allocator_->stun_servers());
  }

  void VerifyTurnServers(std::vector<RelayServerConfig> turn_servers) {
    EXPECT_EQ(turn_servers.size(), raw_port_allocator_->turn_servers().size());
    for (size_t i = 0; i < turn_servers.size(); ++i) {
      ASSERT_EQ(1u, turn_servers[i].ports.size());
      EXPECT_EQ(1u, raw_port_allocator_->turn_servers()[i].ports.size());
      EXPECT_EQ(
          turn_servers[i].ports[0].address.ToString(),
          raw_port_allocator_->turn_servers()[i].ports[0].address.ToString());
      EXPECT_EQ(turn_servers[i].ports[0].proto,
                raw_port_allocator_->turn_servers()[i].ports[0].proto);
      EXPECT_EQ(turn_servers[i].credentials.username,
                raw_port_allocator_->turn_servers()[i].credentials.username);
      EXPECT_EQ(turn_servers[i].credentials.password,
                raw_port_allocator_->turn_servers()[i].credentials.password);
    }
  }

  void VerifyAudioCodecCapability(const RtpCodecCapability& codec) {
    EXPECT_EQ(codec.kind, MediaType::AUDIO);
    EXPECT_FALSE(codec.name.empty());
    EXPECT_GT(codec.clock_rate, 0);
    EXPECT_GT(codec.num_channels, 0);
  }

  void VerifyVideoCodecCapability(const RtpCodecCapability& codec,
                                  bool sender) {
    EXPECT_EQ(codec.kind, MediaType::VIDEO);
    EXPECT_FALSE(codec.name.empty());
    EXPECT_GT(codec.clock_rate, 0);
    if (sender) {
      if (codec.name == "VP8" || codec.name == "H264") {
        EXPECT_THAT(
            codec.scalability_modes,
            UnorderedElementsAre(ScalabilityMode::kL1T1, ScalabilityMode::kL1T2,
                                 ScalabilityMode::kL1T3))
            << "Codec: " << codec.name;
      } else if (codec.name == "VP9" || codec.name == "AV1") {
        EXPECT_THAT(
            codec.scalability_modes,
            UnorderedElementsAre(
                // clang-format off
                ScalabilityMode::kL1T1,
                ScalabilityMode::kL1T2,
                ScalabilityMode::kL1T3,
                ScalabilityMode::kL2T1,
                ScalabilityMode::kL2T1h,
                ScalabilityMode::kL2T1_KEY,
                ScalabilityMode::kL2T2,
                ScalabilityMode::kL2T2h,
                ScalabilityMode::kL2T2_KEY,
                ScalabilityMode::kL2T2_KEY_SHIFT,
                ScalabilityMode::kL2T3,
                ScalabilityMode::kL2T3h,
                ScalabilityMode::kL2T3_KEY,
                ScalabilityMode::kL3T1,
                ScalabilityMode::kL3T1h,
                ScalabilityMode::kL3T1_KEY,
                ScalabilityMode::kL3T2,
                ScalabilityMode::kL3T2h,
                ScalabilityMode::kL3T2_KEY,
                ScalabilityMode::kL3T3,
                ScalabilityMode::kL3T3h,
                ScalabilityMode::kL3T3_KEY,
                ScalabilityMode::kS2T1,
                ScalabilityMode::kS2T1h,
                ScalabilityMode::kS2T2,
                ScalabilityMode::kS2T2h,
                ScalabilityMode::kS2T3,
                ScalabilityMode::kS2T3h,
                ScalabilityMode::kS3T1,
                ScalabilityMode::kS3T1h,
                ScalabilityMode::kS3T2,
                ScalabilityMode::kS3T2h,
                ScalabilityMode::kS3T3,
                ScalabilityMode::kS3T3h)
            // clang-format on
            )
            << "Codec: " << codec.name;
      } else {
        EXPECT_TRUE(codec.scalability_modes.empty());
      }
    } else {
      EXPECT_TRUE(codec.scalability_modes.empty());
    }
  }

  std::unique_ptr<SocketServer> socket_server_;
  test::RunLoop main_thread_;
  scoped_refptr<PeerConnectionFactoryInterface> factory_;
  NullPeerConnectionObserver observer_;
  std::unique_ptr<FakePortAllocator> port_allocator_;
  // Since the PC owns the port allocator after it's been initialized,
  // this should only be used when known to be safe.
  FakePortAllocator* raw_port_allocator_;
};

// Verify creation of PeerConnection using internal ADM, video factory and
// internal libjingle threads.
// TODO(henrika): disabling this test since relying on real audio can result in
// flaky tests and focus on details that are out of scope for you might expect
// for a PeerConnectionFactory unit test.
// See https://bugs.chromium.org/p/webrtc/issues/detail?id=7806 for details.
TEST(PeerConnectionFactoryTestInternal, DISABLED_CreatePCUsingInternalModules) {
  scoped_refptr<PeerConnectionFactoryInterface> factory(
      CreatePeerConnectionFactory(
          nullptr /* network_thread */, nullptr /* signaling_thread */,
          nullptr /* default_adm */, CreateBuiltinAudioEncoderFactory(),
          CreateBuiltinAudioDecoderFactory(),
          nullptr /* video_encoder_factory */,
          nullptr /* video_decoder_factory */, nullptr /* audio_mixer */,
          nullptr /* audio_processing */, nullptr /* audio_frame_processor */,
          CreateTestFieldTrialsPtr()));

  NullPeerConnectionObserver observer;
  PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;

  std::unique_ptr<FakeRTCCertificateGenerator> cert_generator(
      new FakeRTCCertificateGenerator());
  PeerConnectionDependencies pc_dependencies(&observer);
  pc_dependencies.cert_generator = std::move(cert_generator);
  auto result =
      factory->CreatePeerConnectionOrError(config, std::move(pc_dependencies));

  EXPECT_TRUE(result.ok());
}

TEST_F(PeerConnectionFactoryTest, CheckRtpSenderAudioCapabilities) {
  RtpCapabilities audio_capabilities =
      factory_->GetRtpSenderCapabilities(MediaType::AUDIO);
  EXPECT_FALSE(audio_capabilities.codecs.empty());
  for (const auto& codec : audio_capabilities.codecs) {
    VerifyAudioCodecCapability(codec);
  }
  EXPECT_FALSE(audio_capabilities.header_extensions.empty());
  for (const auto& header_extension : audio_capabilities.header_extensions) {
    EXPECT_FALSE(header_extension.uri.empty());
  }
}

TEST_F(PeerConnectionFactoryTest, CheckRtpSenderVideoCapabilities) {
  RtpCapabilities video_capabilities =
      factory_->GetRtpSenderCapabilities(MediaType::VIDEO);
  EXPECT_FALSE(video_capabilities.codecs.empty());
  for (const auto& codec : video_capabilities.codecs) {
    VerifyVideoCodecCapability(codec, true);
  }
  EXPECT_FALSE(video_capabilities.header_extensions.empty());
  for (const auto& header_extension : video_capabilities.header_extensions) {
    EXPECT_FALSE(header_extension.uri.empty());
  }
}

TEST_F(PeerConnectionFactoryTest, CheckRtpSenderRtxEnabledCapabilities) {
  RtpCapabilities video_capabilities =
      factory_->GetRtpSenderCapabilities(MediaType::VIDEO);
  const auto it = std::find_if(
      video_capabilities.codecs.begin(), video_capabilities.codecs.end(),
      [](const auto& c) { return c.name == kRtxCodecName; });
  EXPECT_TRUE(it != video_capabilities.codecs.end());
}

TEST_F(PeerConnectionFactoryTest, CheckRtpSenderDataCapabilities) {
  RtpCapabilities data_capabilities =
      factory_->GetRtpSenderCapabilities(MediaType::DATA);
  EXPECT_TRUE(data_capabilities.codecs.empty());
  EXPECT_TRUE(data_capabilities.header_extensions.empty());
}

TEST_F(PeerConnectionFactoryTest, CheckRtpReceiverAudioCapabilities) {
  RtpCapabilities audio_capabilities =
      factory_->GetRtpReceiverCapabilities(MediaType::AUDIO);
  EXPECT_FALSE(audio_capabilities.codecs.empty());
  for (const auto& codec : audio_capabilities.codecs) {
    VerifyAudioCodecCapability(codec);
  }
  EXPECT_FALSE(audio_capabilities.header_extensions.empty());
  for (const auto& header_extension : audio_capabilities.header_extensions) {
    EXPECT_FALSE(header_extension.uri.empty());
  }
}

TEST_F(PeerConnectionFactoryTest, CheckRtpReceiverVideoCapabilities) {
  RtpCapabilities video_capabilities =
      factory_->GetRtpReceiverCapabilities(MediaType::VIDEO);
  EXPECT_FALSE(video_capabilities.codecs.empty());
  for (const auto& codec : video_capabilities.codecs) {
    VerifyVideoCodecCapability(codec, false);
  }
  EXPECT_FALSE(video_capabilities.header_extensions.empty());
  for (const auto& header_extension : video_capabilities.header_extensions) {
    EXPECT_FALSE(header_extension.uri.empty());
  }
}

TEST_F(PeerConnectionFactoryTest, CheckRtpReceiverRtxEnabledCapabilities) {
  RtpCapabilities video_capabilities =
      factory_->GetRtpReceiverCapabilities(MediaType::VIDEO);
  const auto it = std::find_if(
      video_capabilities.codecs.begin(), video_capabilities.codecs.end(),
      [](const auto& c) { return c.name == kRtxCodecName; });
  EXPECT_TRUE(it != video_capabilities.codecs.end());
}

TEST_F(PeerConnectionFactoryTest, CheckRtpReceiverDataCapabilities) {
  RtpCapabilities data_capabilities =
      factory_->GetRtpReceiverCapabilities(MediaType::DATA);
  EXPECT_TRUE(data_capabilities.codecs.empty());
  EXPECT_TRUE(data_capabilities.header_extensions.empty());
}

// This test verifies creation of PeerConnection with valid STUN and TURN
// configuration. Also verifies the URL's parsed correctly as expected.
TEST_F(PeerConnectionFactoryTest, CreatePCUsingIceServers) {
  PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  PeerConnectionInterface::IceServer ice_server;
  ice_server.uri = kStunIceServer;
  config.servers.push_back(ice_server);
  ice_server.uri = kTurnIceServer;
  ice_server.username = kTurnUsername;
  ice_server.password = kTurnPassword;
  config.servers.push_back(ice_server);
  ice_server.uri = kTurnIceServerWithTransport;
  ice_server.username = kTurnUsername;
  ice_server.password = kTurnPassword;
  config.servers.push_back(ice_server);
  PeerConnectionDependencies pc_dependencies(&observer_);
  pc_dependencies.cert_generator =
      std::make_unique<FakeRTCCertificateGenerator>();
  pc_dependencies.allocator = std::move(port_allocator_);
  auto result =
      factory_->CreatePeerConnectionOrError(config, std::move(pc_dependencies));
  ASSERT_TRUE(result.ok());
  ServerAddresses stun_servers;
  SocketAddress stun1("stun.l.google.com", 19302);
  stun_servers.insert(stun1);
  VerifyStunServers(stun_servers);
  std::vector<RelayServerConfig> turn_servers;
  RelayServerConfig turn1("test.com", 1234, kTurnUsername, kTurnPassword,
                          PROTO_UDP);
  turn_servers.push_back(turn1);
  RelayServerConfig turn2("hello.com", kDefaultStunPort, kTurnUsername,
                          kTurnPassword, PROTO_TCP);
  turn_servers.push_back(turn2);
  VerifyTurnServers(turn_servers);
}

// This test verifies creation of PeerConnection with valid STUN and TURN
// configuration. Also verifies the list of URL's parsed correctly as expected.
TEST_F(PeerConnectionFactoryTest, CreatePCUsingIceServersUrls) {
  PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  PeerConnectionInterface::IceServer ice_server;
  ice_server.urls.push_back(kStunIceServer);
  ice_server.urls.push_back(kTurnIceServer);
  ice_server.urls.push_back(kTurnIceServerWithTransport);
  ice_server.username = kTurnUsername;
  ice_server.password = kTurnPassword;
  config.servers.push_back(ice_server);
  PeerConnectionDependencies pc_dependencies(&observer_);
  pc_dependencies.cert_generator =
      std::make_unique<FakeRTCCertificateGenerator>();
  pc_dependencies.allocator = std::move(port_allocator_);
  auto result =
      factory_->CreatePeerConnectionOrError(config, std::move(pc_dependencies));
  ASSERT_TRUE(result.ok());
  ServerAddresses stun_servers;
  SocketAddress stun1("stun.l.google.com", 19302);
  stun_servers.insert(stun1);
  VerifyStunServers(stun_servers);
  std::vector<RelayServerConfig> turn_servers;
  RelayServerConfig turn1("test.com", 1234, kTurnUsername, kTurnPassword,
                          PROTO_UDP);
  turn_servers.push_back(turn1);
  RelayServerConfig turn2("hello.com", kDefaultStunPort, kTurnUsername,
                          kTurnPassword, PROTO_TCP);
  turn_servers.push_back(turn2);
  VerifyTurnServers(turn_servers);
}

TEST_F(PeerConnectionFactoryTest, CreatePCUsingNoUsernameInUri) {
  PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  PeerConnectionInterface::IceServer ice_server;
  ice_server.uri = kStunIceServer;
  config.servers.push_back(ice_server);
  ice_server.uri = kTurnIceServerWithNoUsernameInUri;
  ice_server.username = kTurnUsername;
  ice_server.password = kTurnPassword;
  config.servers.push_back(ice_server);
  PeerConnectionDependencies pc_dependencies(&observer_);
  pc_dependencies.cert_generator =
      std::make_unique<FakeRTCCertificateGenerator>();
  pc_dependencies.allocator = std::move(port_allocator_);
  auto result =
      factory_->CreatePeerConnectionOrError(config, std::move(pc_dependencies));
  ASSERT_TRUE(result.ok());
  std::vector<RelayServerConfig> turn_servers;
  RelayServerConfig turn("test.com", 1234, kTurnUsername, kTurnPassword,
                         PROTO_UDP);
  turn_servers.push_back(turn);
  VerifyTurnServers(turn_servers);
}

// This test verifies the PeerConnection created properly with TURN url which
// has transport parameter in it.
TEST_F(PeerConnectionFactoryTest, CreatePCUsingTurnUrlWithTransportParam) {
  PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  PeerConnectionInterface::IceServer ice_server;
  ice_server.uri = kTurnIceServerWithTransport;
  ice_server.username = kTurnUsername;
  ice_server.password = kTurnPassword;
  config.servers.push_back(ice_server);
  PeerConnectionDependencies pc_dependencies(&observer_);
  pc_dependencies.cert_generator =
      std::make_unique<FakeRTCCertificateGenerator>();
  pc_dependencies.allocator = std::move(port_allocator_);
  auto result =
      factory_->CreatePeerConnectionOrError(config, std::move(pc_dependencies));
  ASSERT_TRUE(result.ok());
  std::vector<RelayServerConfig> turn_servers;
  RelayServerConfig turn("hello.com", kDefaultStunPort, kTurnUsername,
                         kTurnPassword, PROTO_TCP);
  turn_servers.push_back(turn);
  VerifyTurnServers(turn_servers);
}

TEST_F(PeerConnectionFactoryTest, CreatePCUsingSecureTurnUrl) {
  PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  PeerConnectionInterface::IceServer ice_server;
  ice_server.uri = kSecureTurnIceServer;
  ice_server.username = kTurnUsername;
  ice_server.password = kTurnPassword;
  config.servers.push_back(ice_server);
  ice_server.uri = kSecureTurnIceServerWithoutTransportParam;
  ice_server.username = kTurnUsername;
  ice_server.password = kTurnPassword;
  config.servers.push_back(ice_server);
  ice_server.uri = kSecureTurnIceServerWithoutTransportAndPortParam;
  ice_server.username = kTurnUsername;
  ice_server.password = kTurnPassword;
  config.servers.push_back(ice_server);
  PeerConnectionDependencies pc_dependencies(&observer_);
  pc_dependencies.cert_generator =
      std::make_unique<FakeRTCCertificateGenerator>();
  pc_dependencies.allocator = std::move(port_allocator_);
  auto result =
      factory_->CreatePeerConnectionOrError(config, std::move(pc_dependencies));
  ASSERT_TRUE(result.ok());
  std::vector<RelayServerConfig> turn_servers;
  RelayServerConfig turn1("hello.com", kDefaultStunTlsPort, kTurnUsername,
                          kTurnPassword, PROTO_TLS);
  turn_servers.push_back(turn1);
  // TURNS with transport param should be default to tcp.
  RelayServerConfig turn2("hello.com", 443, kTurnUsername, kTurnPassword,
                          PROTO_TLS);
  turn_servers.push_back(turn2);
  RelayServerConfig turn3("hello.com", kDefaultStunTlsPort, kTurnUsername,
                          kTurnPassword, PROTO_TLS);
  turn_servers.push_back(turn3);
  VerifyTurnServers(turn_servers);
}

TEST_F(PeerConnectionFactoryTest, CreatePCUsingIPLiteralAddress) {
  PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  PeerConnectionInterface::IceServer ice_server;
  ice_server.uri = kStunIceServerWithIPv4Address;
  config.servers.push_back(ice_server);
  ice_server.uri = kStunIceServerWithIPv4AddressWithoutPort;
  config.servers.push_back(ice_server);
  ice_server.uri = kStunIceServerWithIPv6Address;
  config.servers.push_back(ice_server);
  ice_server.uri = kStunIceServerWithIPv6AddressWithoutPort;
  config.servers.push_back(ice_server);
  ice_server.uri = kTurnIceServerWithIPv6Address;
  ice_server.username = kTurnUsername;
  ice_server.password = kTurnPassword;
  config.servers.push_back(ice_server);
  PeerConnectionDependencies pc_dependencies(&observer_);
  pc_dependencies.cert_generator =
      std::make_unique<FakeRTCCertificateGenerator>();
  pc_dependencies.allocator = std::move(port_allocator_);
  auto result =
      factory_->CreatePeerConnectionOrError(config, std::move(pc_dependencies));
  ASSERT_TRUE(result.ok());
  ServerAddresses stun_servers;
  SocketAddress stun1("1.2.3.4", 1234);
  stun_servers.insert(stun1);
  SocketAddress stun2("1.2.3.4", 3478);
  stun_servers.insert(stun2);  // Default port
  SocketAddress stun3("2401:fa00:4::", 1234);
  stun_servers.insert(stun3);
  SocketAddress stun4("2401:fa00:4::", 3478);
  stun_servers.insert(stun4);  // Default port
  VerifyStunServers(stun_servers);

  std::vector<RelayServerConfig> turn_servers;
  RelayServerConfig turn1("2401:fa00:4::", 1234, kTurnUsername, kTurnPassword,
                          PROTO_UDP);
  turn_servers.push_back(turn1);
  VerifyTurnServers(turn_servers);
}

// This test verifies the captured stream is rendered locally using a
// local video track.
TEST_F(PeerConnectionFactoryTest, LocalRendering) {
  scoped_refptr<FakeVideoTrackSource> source =
      FakeVideoTrackSource::Create(/*is_screencast=*/false);

  FakeFrameSource frame_source(1280, 720, TimeDelta::Seconds(1) / 30,
                               Timestamp::Zero());

  ASSERT_TRUE(source.get() != nullptr);
  scoped_refptr<VideoTrackInterface> track(
      factory_->CreateVideoTrack(source, "testlabel"));
  ASSERT_TRUE(track.get() != nullptr);
  FakeVideoTrackRenderer local_renderer(track.get());

  EXPECT_EQ(0, local_renderer.num_rendered_frames());
  source->InjectFrame(frame_source.GetFrame());
  EXPECT_EQ(1, local_renderer.num_rendered_frames());
  EXPECT_FALSE(local_renderer.black_frame());

  track->set_enabled(false);
  source->InjectFrame(frame_source.GetFrame());
  EXPECT_EQ(2, local_renderer.num_rendered_frames());
  EXPECT_TRUE(local_renderer.black_frame());

  track->set_enabled(true);
  source->InjectFrame(frame_source.GetFrame());
  EXPECT_EQ(3, local_renderer.num_rendered_frames());
  EXPECT_FALSE(local_renderer.black_frame());
}

TEST(PeerConnectionFactoryDependenciesTest,
     CanInjectFieldTrialsWithEnvironment) {
  std::unique_ptr<FieldTrialsView> field_trials = CreateTestFieldTrialsPtr("");
  ASSERT_THAT(field_trials, NotNull());
  FieldTrialsView* raw_field_trials = field_trials.get();

  PeerConnectionFactoryDependencies pcf_dependencies;
  pcf_dependencies.env = CreateEnvironment(std::move(field_trials));
  pcf_dependencies.adm = FakeAudioCaptureModule::Create();
  EnableMediaWithDefaults(pcf_dependencies);

  scoped_refptr<PeerConnectionFactory> pcf =
      PeerConnectionFactory::Create(std::move(pcf_dependencies));
  EXPECT_EQ(&pcf->field_trials(), raw_field_trials);
}

TEST(PeerConnectionFactoryDependenciesTest, UsesNetworkManager) {
  constexpr TimeDelta kWaitTimeout = TimeDelta::Seconds(10);
  auto mock_network_manager = std::make_unique<NiceMock<MockNetworkManager>>();

  Event called;
  EXPECT_CALL(*mock_network_manager, StartUpdating())
      .Times(AtLeast(1))
      .WillRepeatedly([&] { called.Set(); });

  PeerConnectionFactoryDependencies pcf_dependencies;
  pcf_dependencies.network_manager = std::move(mock_network_manager);
  pcf_dependencies.env = CreateTestEnvironment();

  scoped_refptr<PeerConnectionFactoryInterface> pcf =
      CreateModularPeerConnectionFactory(std::move(pcf_dependencies));

  PeerConnectionInterface::RTCConfiguration config;
  config.ice_candidate_pool_size = 2;
  NullPeerConnectionObserver observer;
  auto pc = pcf->CreatePeerConnectionOrError(
      config, PeerConnectionDependencies(&observer));
  ASSERT_TRUE(pc.ok());

  called.Wait(kWaitTimeout);
}

TEST(PeerConnectionFactoryDependenciesTest, UsesPacketSocketFactory) {
  constexpr TimeDelta kWaitTimeout = TimeDelta::Seconds(10);
  auto mock_socket_factory =
      std::make_unique<NiceMock<MockPacketSocketFactory>>();

  Event called;
  EXPECT_CALL(*mock_socket_factory, CreateUdpSocket)
      .WillOnce([&] {
        called.Set();
        return nullptr;
      })
      .WillRepeatedly([] { return nullptr; });

  PeerConnectionFactoryDependencies pcf_dependencies;
  pcf_dependencies.packet_socket_factory = std::move(mock_socket_factory);
  pcf_dependencies.env = CreateTestEnvironment();

  scoped_refptr<PeerConnectionFactoryInterface> pcf =
      CreateModularPeerConnectionFactory(std::move(pcf_dependencies));

  // By default, localhost addresses are ignored, which makes tests fail if test
  // machine is offline.
  PeerConnectionFactoryInterface::Options options;
  options.network_ignore_mask = 0;
  pcf->SetOptions(options);

  PeerConnectionInterface::RTCConfiguration config;
  config.ice_candidate_pool_size = 2;
  NullPeerConnectionObserver observer;
  auto pc = pcf->CreatePeerConnectionOrError(
      config, PeerConnectionDependencies(&observer));
  ASSERT_TRUE(pc.ok());

  called.Wait(kWaitTimeout);
}

TEST(PeerConnectionFactoryDependenciesTest,
     CreatesAudioProcessingWithProvidedBuilder) {
  auto ap_factory = std::make_unique<MockAudioProcessingBuilder>();
  auto audio_processing = make_ref_counted<NiceMock<MockAudioProcessing>>();
  // Validate that provided audio_processing is used by expecting that a request
  // to start AEC Dump with unnatural size limit is propagated to the
  // `audio_processing`.
  EXPECT_CALL(*audio_processing, CreateAndAttachAecDump(A<FILE*>(), 24'242, _));
  EXPECT_CALL(*ap_factory, Build).WillOnce(Return(audio_processing));

  PeerConnectionFactoryDependencies pcf_dependencies;
  pcf_dependencies.adm = FakeAudioCaptureModule::Create();
  pcf_dependencies.audio_processing_builder = std::move(ap_factory);
  pcf_dependencies.env = CreateTestEnvironment();
  EnableMediaWithDefaults(pcf_dependencies);

  scoped_refptr<PeerConnectionFactoryInterface> pcf =
      CreateModularPeerConnectionFactory(std::move(pcf_dependencies));
  // Provide a valid file to avoid triggering the null pointer guard.
  // The AEC dump machinery takes ownership of the file and closes it.
  std::string temp_filename =
      test::TempFilename(test::OutputPath(), "aec_dump");
  pcf->StartAecDump(fopen(temp_filename.c_str(), "wb"), 24'242);
  // Destroy the PCF to ensure the file is closed before attempting removal.
  pcf = nullptr;
  test::RemoveFile(temp_filename);
}

TEST(PeerConnectionFactoryDependenciesTest, RepeatMediaEngineInitialization) {
  scoped_refptr<AudioDeviceModule> adm = FakeAudioCaptureModule::Create();
  PeerConnectionFactoryDependencies pcf_dependencies;
  pcf_dependencies.adm = adm;
  pcf_dependencies.signaling_thread = Thread::Current();
  pcf_dependencies.network_thread = Thread::Current();
  pcf_dependencies.env = CreateTestEnvironment();
  EnableMediaWithDefaults(pcf_dependencies);

  scoped_refptr<PeerConnectionFactoryInterface> pcf =
      CreateModularPeerConnectionFactory(std::move(pcf_dependencies));

  for (int i = 0; i < 10; ++i) {
    EXPECT_FALSE(adm->Initialized());
    PeerConnectionInterface::RTCConfiguration config;
    NullPeerConnectionObserver observer;
    auto pc = pcf->CreatePeerConnectionOrError(
        config, PeerConnectionDependencies(&observer));
    ASSERT_TRUE(pc.ok());
    EXPECT_TRUE(adm->Initialized());
  }
  EXPECT_FALSE(adm->Initialized());
}

#if !defined(WEBRTC_CHROMIUM_BUILD) && !defined(WEBRTC_WEBKIT_BUILD)
TEST(PeerConnectionFactoryDependenciesTest,
     CreateAudioSourceAppliesOptionsToAudioProcessing) {
  auto ap_factory = std::make_unique<MockAudioProcessingBuilder>();
  auto audio_processing = make_ref_counted<NiceMock<MockAudioProcessing>>();

  // Capture the sequence of applied configurations to verify value-toggling
  // and options persistence.
  std::vector<bool> aec_enabled_sequence;
  EXPECT_CALL(*audio_processing, ApplyConfig(_))
      .WillRepeatedly([&](const AudioProcessing::Config& config) {
        aec_enabled_sequence.push_back(config.echo_canceller.enabled);
      });
  EXPECT_CALL(*ap_factory, Build).WillOnce(Return(audio_processing));

  PeerConnectionFactoryDependencies pcf_dependencies;
  pcf_dependencies.adm = FakeAudioCaptureModule::Create();
  pcf_dependencies.audio_processing_builder = std::move(ap_factory);
  pcf_dependencies.signaling_thread = Thread::Current();
  pcf_dependencies.network_thread = Thread::Current();
  pcf_dependencies.env = CreateTestEnvironment();
  EnableMediaWithDefaults(pcf_dependencies);

  scoped_refptr<PeerConnectionFactoryInterface> pcf =
      CreateModularPeerConnectionFactory(std::move(pcf_dependencies));

  // 1. Create AudioSource with custom options (AEC disabled).
  AudioOptions options;
  options.echo_cancellation = false;
  auto source = pcf->CreateAudioSource(options);

  // Verify no Init/ApplyConfig has been called yet (lazy initialization).
  EXPECT_TRUE(aec_enabled_sequence.empty());

  // 2. Create PeerConnection to trigger media engine initialization.
  PeerConnectionInterface::RTCConfiguration config;
  NullPeerConnectionObserver observer;
  auto pc_or_error = pcf->CreatePeerConnectionOrError(
      config, PeerConnectionDependencies(&observer));
  ASSERT_TRUE(pc_or_error.ok());

  // Verify the exact sequence of applied configurations:
  // - 1st Init (defaults): true (default option is applied)
  // - 2nd Applied Custom: false (custom options from CreateAudioSource are
  // applied)
  ASSERT_EQ(aec_enabled_sequence.size(), 2u);
  EXPECT_EQ(aec_enabled_sequence[0], true);
  EXPECT_EQ(aec_enabled_sequence[1], false);

  pcf = nullptr;
}
#endif

TEST(ConnectionContextTest,
     WorkerThreadDefaultsToNetworkThreadWhenWorkerThreadNotSet) {
  Environment env = CreateTestEnvironment();
  PeerConnectionFactoryDependencies dependencies;
  dependencies.signaling_thread = Thread::Current();
  auto context = ConnectionContext::Create(env, &dependencies);
  ASSERT_THAT(context, NotNull());
  EXPECT_THAT(context->network_thread(), NotNull());
  EXPECT_EQ(context->worker_thread(), context->network_thread());
}

TEST(ConnectionContextTest,
     WorkerThreadThatCanCreateSocketsIsUsedAsNetworkThread) {
  Environment env = CreateTestEnvironment();
  std::unique_ptr<Thread> worker_thread = Thread::CreateWithSocketServer();
  worker_thread->Start();
  PeerConnectionFactoryDependencies dependencies;
  dependencies.signaling_thread = Thread::Current();
  // The test intentionally sets the deprecated member without setting
  // `network_thread`, like some applications still do.
  RTC_PUSH_IGNORING_WDEPRECATED_DECLARATIONS()
  dependencies.worker_thread = worker_thread.get();
  RTC_POP_IGNORING_WDEPRECATED_DECLARATIONS()
  auto context = ConnectionContext::Create(env, &dependencies);
  ASSERT_THAT(context, NotNull());
  EXPECT_EQ(context->network_thread(), worker_thread.get());
}

TEST(ConnectionContextTest,
     WorkerThreadThatCannotCreateSocketsIsNotUsedAsNetworkThread) {
  Environment env = CreateTestEnvironment();
  // Thread::Create() uses a NullSocketServer, which can't create sockets.
  std::unique_ptr<Thread> worker_thread = Thread::Create();
  worker_thread->Start();
  PeerConnectionFactoryDependencies dependencies;
  dependencies.signaling_thread = Thread::Current();
  // The test intentionally sets the deprecated member without setting
  // `network_thread`, like some applications still do.
  RTC_PUSH_IGNORING_WDEPRECATED_DECLARATIONS()
  dependencies.worker_thread = worker_thread.get();
  RTC_POP_IGNORING_WDEPRECATED_DECLARATIONS()
  auto context = ConnectionContext::Create(env, &dependencies);
  ASSERT_THAT(context, NotNull());
  EXPECT_EQ(context->worker_thread(), worker_thread.get());
  EXPECT_NE(context->network_thread(), worker_thread.get());
}

TEST(ConnectionContextTest,
     WorkerThreadThatCanCreateSocketsIsNotUsedWhenNetworkThreadIsSet) {
  Environment env = CreateTestEnvironment();
  std::unique_ptr<Thread> network_thread = Thread::CreateWithSocketServer();
  network_thread->Start();
  std::unique_ptr<Thread> worker_thread = Thread::CreateWithSocketServer();
  worker_thread->Start();
  PeerConnectionFactoryDependencies dependencies;
  dependencies.signaling_thread = Thread::Current();
  dependencies.network_thread = network_thread.get();
  // The test intentionally sets the deprecated member in order to verify that
  // `network_thread` takes precedence over it.
  RTC_PUSH_IGNORING_WDEPRECATED_DECLARATIONS()
  dependencies.worker_thread = worker_thread.get();
  RTC_POP_IGNORING_WDEPRECATED_DECLARATIONS()
  auto context = ConnectionContext::Create(env, &dependencies);
  ASSERT_THAT(context, NotNull());
  EXPECT_EQ(context->network_thread(), network_thread.get());
  EXPECT_EQ(context->worker_thread(), worker_thread.get());
}

}  // namespace
}  // namespace webrtc
