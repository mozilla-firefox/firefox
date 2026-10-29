/*
 *  Copyright 2018 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/jsep_transport_controller.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/crypto/crypto_options.h"
#include "api/dtls_transport_interface.h"
#include "api/environment/environment.h"
#include "api/field_trials.h"
#include "api/ice_transport_interface.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtp_transport_factory.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/test/rtc_error_matchers.h"
#include "api/transport/data_channel_transport_interface.h"
#include "api/transport/enums.h"
#include "api/units/time_delta.h"
#include "p2p/base/ice_transport_internal.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/transport_info.h"
#include "p2p/dtls/dtls_transport_factory.h"
#include "p2p/dtls/dtls_transport_internal.h"
#include "p2p/dtls/fake_dtls_transport.h"
#include "p2p/test/fake_ice_transport.h"
#include "p2p/test/fake_port_allocator.h"
#include "pc/dtls_transport.h"
#include "pc/rtp_transport.h"
#include "pc/rtp_transport_internal.h"
#include "pc/scoped_operations_batcher.h"
#include "pc/session_description.h"
#include "pc/transport_stats.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/fake_ssl_identity.h"
#include "rtc_base/logging.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/ssl_certificate.h"
#include "rtc_base/ssl_fingerprint.h"
#include "rtc_base/ssl_identity.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "rtc_base/task_queue_for_test.h"
#include "rtc_base/thread.h"
#include "rtc_base/virtual_socket_server.h"
#include "system_wrappers/include/metrics.h"
#include "test/create_test_environment.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"
#include "test/wait_until.h"

namespace webrtc {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::Optional;
using ::testing::Pair;
using ::testing::SizeIs;

constexpr TimeDelta kTimeout = TimeDelta::Millis(100);
const char kIceUfrag1[] = "u0001";
const char kIcePwd1[] = "TESTICEPWD00000000000001";
const char kIceUfrag2[] = "u0002";
const char kIcePwd2[] = "TESTICEPWD00000000000002";
const char kIceUfrag3[] = "u0003";
const char kIcePwd3[] = "TESTICEPWD00000000000003";
const char kIceUfrag4[] = "u0004";
const char kIcePwd4[] = "TESTICEPWD00000000000004";
const char kAudioMid1[] = "audio1";
const char kAudioMid2[] = "audio2";
const char kVideoMid1[] = "video1";
const char kVideoMid2[] = "video2";
const char kDataMid1[] = "data1";

class FakeIceTransportFactory : public IceTransportFactory {
 public:
  ~FakeIceTransportFactory() override = default;
  scoped_refptr<IceTransportInterface> CreateIceTransport(
      const std::string& transport_name,
      int component,
      IceTransportInit init) override {
    return make_ref_counted<FakeIceTransport>(
        std::make_unique<FakeIceTransportInternal>(init.env(), transport_name,
                                                   component));
  }
};

class FakeDtlsTransportFactory : public DtlsTransportFactory {
 public:
  std::unique_ptr<DtlsTransportInternal> CreateDtlsTransport(
      scoped_refptr<IceTransportInterface> ice,
      const CryptoOptions& crypto_options,
      SSLProtocolVersion max_version) override {
    return std::make_unique<FakeDtlsTransport>(std::move(ice));
  }
};

// Unlike FakeDtlsTransport, notifies about DTLS role changes the same way that
// DtlsTransportInternalImpl does.
class RoleChangeNotifyingFakeDtlsTransport : public FakeDtlsTransport {
 public:
  using FakeDtlsTransport::FakeDtlsTransport;

  bool SetDtlsRole(SSLRole role) override {
    SSLRole current_role;
    const bool changed = !GetDtlsRole(&current_role) || current_role != role;
    FakeDtlsTransport::SetDtlsRole(role);
    if (changed) {
      SendDtlsRoleChange(this, role);
    }
    return true;
  }
};

class RoleChangeNotifyingFakeDtlsTransportFactory
    : public FakeDtlsTransportFactory {
 public:
  std::unique_ptr<DtlsTransportInternal> CreateDtlsTransport(
      scoped_refptr<IceTransportInterface> ice,
      const CryptoOptions& crypto_options,
      SSLProtocolVersion max_version) override {
    return std::make_unique<RoleChangeNotifyingFakeDtlsTransport>(
        std::move(ice));
  }
};

class JsepTransportControllerTest : public JsepTransportController::Observer,
                                    public ::testing::Test {
 public:
  JsepTransportControllerTest()
      : env_(CreateTestEnvironment({.field_trials = &field_trials_})),
        network_thread_(CreateNetworkThread()),
        signaling_thread_(Thread::Current()) {
    fake_ice_transport_factory_ = std::make_unique<FakeIceTransportFactory>();
    fake_dtls_transport_factory_ = std::make_unique<FakeDtlsTransportFactory>();
  }
  ~JsepTransportControllerTest() override {
    // The transport controller, the port allocator and the transports that
    // `custom_rtp_transport_factory_` keeps alive live on the network thread
    // and must be destroyed there.
    SendTask(network_thread_.get(), [&] {
      transport_controller_.reset();
      custom_rtp_transport_factory_.reset();
      port_allocator_ = std::nullopt;
    });
  }

  void CreateJsepTransportController(JsepTransportController::Config config) {
    CreateJsepTransportController(std::move(config), signaling_thread_);
  }

  // Passing `network_thread_` as `signaling_thread` tests the configuration
  // where the signaling and network threads are the same thread.
  void CreateJsepTransportController(JsepTransportController::Config config,
                                     Thread* absl_nonnull signaling_thread) {
    if (!port_allocator_) {
      port_allocator_.emplace(env_, network_thread_->socketserver(),
                              network_thread_.get());
    }
    config.transport_observer = this;
    config.rtcp_handler = [](const CopyOnWriteBuffer& packet,
                             int64_t packet_time_us) {
      RTC_DCHECK_NOTREACHED();
    };
    config.ice_transport_factory = fake_ice_transport_factory_.get();
    config.dtls_transport_factory = fake_dtls_transport_factory_.get();
    config.signal_ice_candidates_gathered =
        [this](absl::string_view transport,
               const std::vector<Candidate>& candidates) {
          PostToSignalingThread(
              [this, transport = std::string(transport), candidates] {
                OnCandidatesGathered(transport, candidates);
              });
        };
    config.signal_ice_connection_state = [this](IceConnectionState s) {
      PostToSignalingThread([this, s] { OnConnectionState(s); });
    };
    config.signal_connection_state =
        [this](PeerConnectionInterface::PeerConnectionState s) {
          PostToSignalingThread([this, s] { OnCombinedConnectionState(s); });
        };
    config.signal_standardized_ice_connection_state =
        [this](PeerConnectionInterface::IceConnectionState s) {
          PostToSignalingThread(
              [this, s] { OnStandardizedIceConnectionState(s); });
        };
    config.signal_ice_gathering_state = [this](IceGatheringState s) {
      PostToSignalingThread([this, s] { OnGatheringState(s); });
    };
    transport_controller_ = std::make_unique<JsepTransportController>(
        env_, signaling_thread, network_thread_.get(), &*port_allocator_,
        /*async_resolver_factory=*/nullptr,
        /*lna_permission_factory=*/nullptr, std::move(config));
  }

  // The transports must only be used on the network thread. The returned
  // pointers may be compared on the signaling thread.
  RtpTransportInternal* absl_nullable GetRtpTransport(absl::string_view mid) {
    return network_thread_->BlockingCall(
        [&] { return transport_controller_->GetRtpTransport(mid); });
  }

  DtlsTransportInternal* absl_nullable GetDtlsTransport(absl::string_view mid) {
    return network_thread_->BlockingCall(
        [&] { return transport_controller_->GetDtlsTransport(mid); });
  }

  FakeDtlsTransport* absl_nullable GetFakeDtlsTransport(absl::string_view mid) {
    return static_cast<FakeDtlsTransport*>(GetDtlsTransport(mid));
  }

  IceRole GetIceRole(FakeDtlsTransport* absl_nonnull fake_dtls) {
    return network_thread_->BlockingCall(
        [&] { return fake_dtls->fake_ice_transport()->GetIceRole(); });
  }

  // Checks that the DTLS transport that the signaling thread looks up for
  // `mid` is the one that the network thread uses.
  void ExpectLookupMatchesNetworkThread(absl::string_view mid) {
    EXPECT_EQ(transport_controller_->LookupDtlsTransportByMid(mid),
              network_thread_->BlockingCall([&] {
                return transport_controller_->LookupDtlsTransportByMid_n(mid);
              }))
        << "mid: " << mid;
  }

  // Rolls back the transports the same way as SdpOfferAnswerHandler::Rollback()
  // does: with a task in a batch for the network thread.
  RTCError RollbackTransports() {
    RTCError error;
    ScopedOperationsBatcher network_tasks(network_thread_.get());
    network_tasks.AddWithFinalizer(
        transport_controller_->MakeRollbackTask(&error));
    EXPECT_TRUE(network_tasks.Run().ok());
    return error;
  }

  std::unique_ptr<SessionDescription> CreateSessionDescriptionWithoutBundle() {
    auto description = std::make_unique<SessionDescription>();
    AddAudioSection(description.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                    ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
    AddVideoSection(description.get(), kVideoMid1, kIceUfrag1, kIcePwd1,
                    ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
    return description;
  }

  std::unique_ptr<SessionDescription>
  CreateSessionDescriptionWithBundleGroup() {
    auto description = CreateSessionDescriptionWithoutBundle();
    ContentGroup bundle_group(GROUP_TYPE_BUNDLE);
    bundle_group.AddContentName(kAudioMid1);
    bundle_group.AddContentName(kVideoMid1);
    description->AddGroup(bundle_group);

    return description;
  }

  std::unique_ptr<SessionDescription>
  CreateSessionDescriptionWithBundledData() {
    auto description = CreateSessionDescriptionWithoutBundle();
    AddDataSection(description.get(), kDataMid1, MediaProtocolType::kSctp,
                   kIceUfrag1, kIcePwd1, ICEMODE_FULL, CONNECTIONROLE_ACTPASS,
                   nullptr);
    ContentGroup bundle_group(GROUP_TYPE_BUNDLE);
    bundle_group.AddContentName(kAudioMid1);
    bundle_group.AddContentName(kVideoMid1);
    bundle_group.AddContentName(kDataMid1);
    description->AddGroup(bundle_group);
    return description;
  }

  // Creates an audio and a video section in a BUNDLE group, both using
  // `conn_role` and the fingerprint of `cert`.
  absl_nonnull std::unique_ptr<SessionDescription>
  CreateBundledDescriptionWithDtls(
      ConnectionRole conn_role,
      absl_nonnull scoped_refptr<RTCCertificate> cert) {
    auto description = std::make_unique<SessionDescription>();
    AddAudioSection(description.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                    ICEMODE_FULL, conn_role, cert);
    AddVideoSection(description.get(), kVideoMid1, kIceUfrag1, kIcePwd1,
                    ICEMODE_FULL, conn_role, cert);
    ContentGroup bundle_group(GROUP_TYPE_BUNDLE);
    bundle_group.AddContentName(kAudioMid1);
    bundle_group.AddContentName(kVideoMid1);
    description->AddGroup(bundle_group);
    return description;
  }

  void AddAudioSection(SessionDescription* description,
                       const std::string& mid,
                       const std::string& ufrag,
                       const std::string& pwd,
                       IceMode ice_mode,
                       ConnectionRole conn_role,
                       scoped_refptr<RTCCertificate> cert) {
    std::unique_ptr<AudioContentDescription> audio(
        new AudioContentDescription());
    // Set RTCP-mux to be true because the default policy is "mux required".
    audio->set_rtcp_mux(true);
    description->AddContent(mid, MediaProtocolType::kRtp,
                            /*rejected=*/false, std::move(audio));
    AddTransportInfo(description, mid, ufrag, pwd, ice_mode, conn_role, cert);
  }

  void AddVideoSection(SessionDescription* description,
                       const std::string& mid,
                       const std::string& ufrag,
                       const std::string& pwd,
                       IceMode ice_mode,
                       ConnectionRole conn_role,
                       scoped_refptr<RTCCertificate> cert) {
    std::unique_ptr<VideoContentDescription> video(
        new VideoContentDescription());
    // Set RTCP-mux to be true because the default policy is "mux required".
    video->set_rtcp_mux(true);
    description->AddContent(mid, MediaProtocolType::kRtp,
                            /*rejected=*/false, std::move(video));
    AddTransportInfo(description, mid, ufrag, pwd, ice_mode, conn_role, cert);
  }

  void AddDataSection(SessionDescription* description,
                      const std::string& mid,
                      MediaProtocolType protocol_type,
                      const std::string& ufrag,
                      const std::string& pwd,
                      IceMode ice_mode,
                      ConnectionRole conn_role,
                      scoped_refptr<RTCCertificate> cert) {
    RTC_CHECK(protocol_type == MediaProtocolType::kSctp);
    std::unique_ptr<SctpDataContentDescription> data(
        new SctpDataContentDescription());
    data->set_rtcp_mux(true);
    description->AddContent(mid, protocol_type,
                            /*rejected=*/false, std::move(data));
    AddTransportInfo(description, mid, ufrag, pwd, ice_mode, conn_role, cert);
  }

  void AddTransportInfo(SessionDescription* description,
                        const std::string& mid,
                        const std::string& ufrag,
                        const std::string& pwd,
                        IceMode ice_mode,
                        ConnectionRole conn_role,
                        scoped_refptr<RTCCertificate> cert) {
    std::unique_ptr<SSLFingerprint> fingerprint;
    if (cert) {
      fingerprint = SSLFingerprint::CreateFromCertificate(*cert);
    }

    TransportDescription transport_desc(std::vector<std::string>(), ufrag, pwd,
                                        ice_mode, conn_role, fingerprint.get());
    description->AddTransportInfo(TransportInfo(mid, transport_desc));
  }

  IceConfig CreateIceConfig(
      TimeDelta receiving_timeout,
      ContinualGatheringPolicy continual_gathering_policy) {
    IceConfig config;
    config.receiving_timeout = receiving_timeout;
    config.continual_gathering_policy = continual_gathering_policy;
    return config;
  }

  Candidate CreateCandidate(int component = ICE_CANDIDATE_COMPONENT_RTP) {
    Candidate c;
    c.set_address(SocketAddress("192.168.1.1", 8000));
    c.set_component(component);
    c.set_protocol(UDP_PROTOCOL_NAME);
    c.set_priority(1);
    return c;
  }

  void CreateLocalDescriptionAndCompleteConnection() {
    auto description = CreateSessionDescriptionWithBundleGroup();
    EXPECT_TRUE(
        transport_controller_
            ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
            .ok());
    transport_controller_->MaybeStartGathering(
        /*discard_candidate_pool=*/false,
        /*on_pooled_credentials=*/nullptr);

    SendTask(network_thread_.get(), [this] {
      auto fake_audio_dtls = static_cast<FakeDtlsTransport*>(
          transport_controller_->GetDtlsTransport(kAudioMid1));
      auto fake_video_dtls = static_cast<FakeDtlsTransport*>(
          transport_controller_->GetDtlsTransport(kVideoMid1));
      fake_audio_dtls->fake_ice_transport()->NotifyCandidateGathered(
          fake_audio_dtls->fake_ice_transport(), CreateCandidate());
      fake_video_dtls->fake_ice_transport()->NotifyCandidateGathered(
          fake_video_dtls->fake_ice_transport(), CreateCandidate());
      fake_audio_dtls->fake_ice_transport()->SetCandidatesGatheringComplete();
      fake_video_dtls->fake_ice_transport()->SetCandidatesGatheringComplete();
      fake_audio_dtls->fake_ice_transport()->SetConnectionCount(2);
      fake_video_dtls->fake_ice_transport()->SetConnectionCount(2);
      fake_audio_dtls->SetReceiving(true);
      fake_video_dtls->SetReceiving(true);
      fake_audio_dtls->SetWritable(true);
      fake_video_dtls->SetWritable(true);
      fake_audio_dtls->fake_ice_transport()->SetConnectionCount(1);
      fake_video_dtls->fake_ice_transport()->SetConnectionCount(1);
    });
  }

 protected:
  static absl_nonnull std::unique_ptr<Thread> CreateNetworkThread() {
    // The worker and network threads are joined into one thread. The
    // JsepTransportController only uses the network thread.
    // Pooled port allocator sessions create sockets. A virtual socket server
    // makes that work without network access, which e.g. the Fuchsia test
    // realm doesn't have.
    auto thread =
        std::make_unique<Thread>(std::make_unique<VirtualSocketServer>());
    thread->SetName("network", nullptr);
    thread->Start();
    return thread;
  }

  // The JsepTransportController fires the ICE signals on the network thread.
  // Like PeerConnection, forward them to the signaling thread, where the tests
  // check the state.
  void PostToSignalingThread(absl::AnyInvocable<void() &&> task) {
    Thread* const signaled_on_thread = Thread::Current();
    signaling_thread_->PostTask(
        SafeTask(signaling_safety_.flag(),
                 [this, signaled_on_thread, task = std::move(task)]() mutable {
                   ice_signaled_on_thread_ = signaled_on_thread;
                   std::move(task)();
                 }));
  }

  void OnConnectionState(IceConnectionState state) {
    connection_state_ = state;
    ++connection_state_signal_count_;
  }

  void OnStandardizedIceConnectionState(
      PeerConnectionInterface::IceConnectionState state) {
    ice_connection_state_ = state;
    ++ice_connection_state_signal_count_;
  }

  void OnCombinedConnectionState(
      PeerConnectionInterface::PeerConnectionState state) {
    RTC_LOG(LS_INFO) << "OnCombinedConnectionState: "
                     << static_cast<int>(state);
    combined_connection_state_ = state;
    ++combined_connection_state_signal_count_;
  }

  void OnGatheringState(IceGatheringState state) {
    gathering_state_ = state;
    ++gathering_state_signal_count_;
  }

  void OnCandidatesGathered(absl::string_view transport_name,
                            const Candidates& candidates) {
    std::string str_name(transport_name);
    candidates_[str_name].insert(candidates_[str_name].end(),
                                 candidates.begin(), candidates.end());
    ++candidates_signal_count_;
  }

  // JsepTransportController::Observer overrides.
  bool OnTransportChanged(
      absl::string_view mid,
      RtpTransportInternal* rtp_transport,
      scoped_refptr<DtlsTransport> dtls_transport,
      DataChannelTransportInterface* data_channel_transport) override {
    RTC_DCHECK_RUN_ON(network_thread_.get());
    std::string str_mid(mid);
    changed_rtp_transport_by_mid_[str_mid] = rtp_transport;
    changed_dtls_transport_by_mid_[str_mid] = dtls_transport;
    return true;
  }

  FieldTrials field_trials_ = CreateTestFieldTrials();
  Environment env_;
  test::RunLoop main_thread_;
  // Information received from signals from transport controller. Only used on
  // the signaling thread.
  IceConnectionState connection_state_ = kIceConnectionConnecting;
  PeerConnectionInterface::IceConnectionState ice_connection_state_ =
      PeerConnectionInterface::kIceConnectionNew;
  PeerConnectionInterface::PeerConnectionState combined_connection_state_ =
      PeerConnectionInterface::PeerConnectionState::kNew;
  bool receiving_ = false;
  IceGatheringState gathering_state_ = kIceGatheringNew;
  // transport_name => candidates
  std::map<std::string, Candidates> candidates_;
  // Counts of each signal emitted.
  int connection_state_signal_count_ = 0;
  int ice_connection_state_signal_count_ = 0;
  int combined_connection_state_signal_count_ = 0;
  int receiving_signal_count_ = 0;
  int gathering_state_signal_count_ = 0;
  int candidates_signal_count_ = 0;

  // `network_thread_` should be destroyed after `transport_controller_`
  const absl_nonnull std::unique_ptr<Thread> network_thread_;
  std::unique_ptr<FakeIceTransportFactory> fake_ice_transport_factory_;
  std::unique_ptr<FakeDtlsTransportFactory> fake_dtls_transport_factory_;
  std::optional<FakePortAllocator> port_allocator_;
  Thread* const signaling_thread_ = nullptr;
  // The thread that the last ICE signal was fired on.
  Thread* ice_signaled_on_thread_ = nullptr;
  // Used to verify the SignalRtpTransportChanged/SignalDtlsTransportChanged are
  // signaled correctly. Only used on the network thread.
  std::map<std::string, RtpTransportInternal*> changed_rtp_transport_by_mid_;
  std::map<std::string, scoped_refptr<DtlsTransport>>
      changed_dtls_transport_by_mid_;
  std::unique_ptr<RtpTransportFactory> custom_rtp_transport_factory_ = nullptr;
  // Transport controller needs to be destroyed first, because it may issue
  // callbacks that modify the changed_*_by_mid in the destructor.
  std::unique_ptr<JsepTransportController> transport_controller_;
  ScopedTaskSafety signaling_safety_;
};

TEST_F(JsepTransportControllerTest, GetRtpTransport) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithoutBundle();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());
  auto audio_rtp_transport = GetRtpTransport(kAudioMid1);
  auto video_rtp_transport = GetRtpTransport(kVideoMid1);
  EXPECT_NE(nullptr, audio_rtp_transport);
  EXPECT_NE(nullptr, video_rtp_transport);
  EXPECT_NE(audio_rtp_transport, video_rtp_transport);
  // Return nullptr for non-existing ones.
  EXPECT_EQ(nullptr, GetRtpTransport(kAudioMid2));
}

TEST_F(JsepTransportControllerTest, GetDtlsTransport) {
  JsepTransportController::Config config;
  config.rtcp_mux_policy = PeerConnectionInterface::kRtcpMuxPolicyNegotiate;
  CreateJsepTransportController(std::move(config));
  auto description = CreateSessionDescriptionWithoutBundle();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());
  EXPECT_NE(nullptr, GetDtlsTransport(kAudioMid1));
  EXPECT_NE(nullptr,
            transport_controller_->LookupDtlsTransportByMid(kAudioMid1));
  EXPECT_NE(nullptr, GetDtlsTransport(kVideoMid1));
  EXPECT_NE(nullptr,
            transport_controller_->LookupDtlsTransportByMid(kVideoMid1));
  // Lookup for all MIDs should return different transports (no bundle)
  EXPECT_NE(transport_controller_->LookupDtlsTransportByMid(kAudioMid1),
            transport_controller_->LookupDtlsTransportByMid(kVideoMid1));
  // Return nullptr for non-existing ones.
  EXPECT_EQ(nullptr, GetDtlsTransport(kVideoMid2));
  EXPECT_EQ(nullptr,
            transport_controller_->LookupDtlsTransportByMid(kVideoMid2));
  // Take a pointer to a transport, shut down the transport controller,
  // and verify that the resulting container is empty.
  auto dtls_transport =
      transport_controller_->LookupDtlsTransportByMid(kVideoMid1);
  DtlsTransport* my_transport =
      static_cast<DtlsTransport*>(dtls_transport.get());
  EXPECT_EQ(DtlsTransportState::kNew, my_transport->Information().state());
  SendTask(network_thread_.get(), [&] { transport_controller_.reset(); });
  EXPECT_EQ(DtlsTransportState::kClosed, my_transport->Information().state());
}

TEST_F(JsepTransportControllerTest, GetDtlsTransportWithRtcpMux) {
  JsepTransportController::Config config;
  config.rtcp_mux_policy = PeerConnectionInterface::kRtcpMuxPolicyRequire;
  CreateJsepTransportController(std::move(config));
  auto description = CreateSessionDescriptionWithoutBundle();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());
  EXPECT_NE(nullptr, GetDtlsTransport(kAudioMid1));
  EXPECT_NE(nullptr, GetDtlsTransport(kVideoMid1));
}

// LookupDtlsTransportByMid() reads the transport states that are cached on the
// signaling thread. They must follow the transports that the network thread
// uses when a mid gets bundled onto another transport.
TEST_F(JsepTransportControllerTest, LookupDtlsTransportByMidFollowsBundling) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithBundleGroup();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());
  // With the balanced bundle policy, the offer doesn't bundle the transports.
  ExpectLookupMatchesNetworkThread(kAudioMid1);
  ExpectLookupMatchesNetworkThread(kVideoMid1);
  EXPECT_NE(transport_controller_->LookupDtlsTransportByMid(kAudioMid1),
            transport_controller_->LookupDtlsTransportByMid(kVideoMid1));

  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, description.get(),
                                         description.get())
                  .ok());
  ExpectLookupMatchesNetworkThread(kAudioMid1);
  ExpectLookupMatchesNetworkThread(kVideoMid1);
  EXPECT_EQ(transport_controller_->LookupDtlsTransportByMid(kAudioMid1),
            transport_controller_->LookupDtlsTransportByMid(kVideoMid1));
  std::optional<JsepTransportController::TransportState> video_state =
      transport_controller_->GetTransportState(kVideoMid1);
  ASSERT_TRUE(video_state);
  EXPECT_EQ(video_state->transport_name, kAudioMid1);
}

// A rejected mid has no transport state, even though its transport is kept
// alive in case of a rollback. After the rollback, the mid uses the transport
// again.
TEST_F(JsepTransportControllerTest,
       LookupDtlsTransportByMidAfterRejectAndRollback) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  std::unique_ptr<SessionDescription> remote_answer(local_offer->Clone());
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());
  scoped_refptr<DtlsTransport> transport =
      transport_controller_->LookupDtlsTransportByMid(kAudioMid1);
  EXPECT_NE(transport, nullptr);

  std::unique_ptr<SessionDescription> local_reoffer(local_offer->Clone());
  local_reoffer->contents()[0].rejected = true;
  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kOffer, local_reoffer.get(),
                                        remote_answer.get())
                  .ok());
  EXPECT_FALSE(transport_controller_->GetTransportState(kAudioMid1));
  ExpectLookupMatchesNetworkThread(kAudioMid1);

  EXPECT_TRUE(RollbackTransports().ok());
  EXPECT_EQ(transport_controller_->LookupDtlsTransportByMid(kAudioMid1),
            transport);
  ExpectLookupMatchesNetworkThread(kAudioMid1);
}

TEST_F(JsepTransportControllerTest, MakeCloseTaskClearsTransportStates) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithoutBundle();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());
  EXPECT_NE(transport_controller_->LookupDtlsTransportByMid(kAudioMid1),
            nullptr);

  absl::AnyInvocable<void() &&> close_task =
      transport_controller_->MakeCloseTask();
  EXPECT_EQ(transport_controller_->LookupDtlsTransportByMid(kAudioMid1),
            nullptr);
  EXPECT_FALSE(transport_controller_->GetTransportState(kVideoMid1));
  SendTask(network_thread_.get(), [&] { std::move(close_task)(); });
}

TEST_F(JsepTransportControllerTest, SetIceConfig) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithoutBundle();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());

  SendTask(network_thread_.get(), [&] {
    transport_controller_->SetIceConfig(
        CreateIceConfig(kTimeout, GATHER_CONTINUALLY));
  });
  FakeDtlsTransport* fake_audio_dtls = GetFakeDtlsTransport(kAudioMid1);
  ASSERT_NE(nullptr, fake_audio_dtls);
  SendTask(network_thread_.get(), [&] {
    EXPECT_EQ(kTimeout,
              fake_audio_dtls->fake_ice_transport()->receiving_timeout());
    EXPECT_TRUE(fake_audio_dtls->fake_ice_transport()->gather_continually());
  });

  // Test that value stored in controller is applied to new transports.
  AddAudioSection(description.get(), kAudioMid2, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);

  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());
  fake_audio_dtls = GetFakeDtlsTransport(kAudioMid2);
  ASSERT_NE(nullptr, fake_audio_dtls);
  SendTask(network_thread_.get(), [&] {
    EXPECT_EQ(kTimeout,
              fake_audio_dtls->fake_ice_transport()->receiving_timeout());
    EXPECT_TRUE(fake_audio_dtls->fake_ice_transport()->gather_continually());
  });
}

// Tests the getter and setter of the ICE restart flag.
TEST_F(JsepTransportControllerTest, NeedIceRestart) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithoutBundle();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());
  // TODO(tommi): Note that _now_ we set `remote`. (was not set before).
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, description.get(),
                                         description.get())
                  .ok());

  // Initially NeedsIceRestart should return false.
  EXPECT_FALSE(transport_controller_->NeedsIceRestart(kAudioMid1));
  EXPECT_FALSE(transport_controller_->NeedsIceRestart(kVideoMid1));
  // Set the needs-ice-restart flag, synchronize the transport states and verify
  // NeedsIceRestart starts returning true.
  transport_controller_->SetTransportStates(network_thread_->BlockingCall([&] {
    transport_controller_->SetNeedsIceRestartFlag();
    return transport_controller_->GetTransportStates_n();
  }));
  EXPECT_TRUE(transport_controller_->NeedsIceRestart(kAudioMid1));
  EXPECT_TRUE(transport_controller_->NeedsIceRestart(kVideoMid1));
  // For a nonexistent transport, false should be returned.
  EXPECT_FALSE(transport_controller_->NeedsIceRestart(kVideoMid2));

  // Reset the ice_ufrag/ice_pwd for audio.
  auto audio_transport_info = description->GetTransportInfoByName(kAudioMid1);
  audio_transport_info->description.ice_ufrag = kIceUfrag2;
  audio_transport_info->description.ice_pwd = kIcePwd2;
  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kOffer, description.get(),
                                        description.get())
                  .ok());
  // Because the ICE is only restarted for audio, NeedsIceRestart is expected to
  // return false for audio and true for video.
  EXPECT_FALSE(transport_controller_->NeedsIceRestart(kAudioMid1));
  EXPECT_TRUE(transport_controller_->NeedsIceRestart(kVideoMid1));
}

TEST_F(JsepTransportControllerTest, MaybeStartGathering) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithBundleGroup();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());
  // After setting the local description, we should be able to start gathering
  // candidates.
  transport_controller_->MaybeStartGathering(/*discard_candidate_pool=*/false,
                                             /*on_pooled_credentials=*/nullptr);
  EXPECT_THAT(
      WaitUntil([&] { return gathering_state_; },
                ::testing::Eq(kIceGatheringGathering), {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(1, gathering_state_signal_count_);
}

TEST_F(JsepTransportControllerTest, MaybeStartGatheringDiscardsCandidatePool) {
  CreateJsepTransportController(JsepTransportController::Config());
  SendTask(network_thread_.get(), [&] {
    port_allocator_->SetConfiguration(
        /*stun_servers=*/{}, /*turn_servers=*/{}, /*candidate_pool_size=*/2,
        NO_PRUNE, /*turn_customizer=*/nullptr,
        /*stun_candidate_keepalive_interval=*/std::nullopt);
  });
  auto description = CreateSessionDescriptionWithBundleGroup();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());
  // The credentials are delivered asynchronously on the signaling thread.
  std::optional<std::vector<IceParameters>> credentials;
  auto on_pooled_credentials = [&](std::vector<IceParameters> c) {
    credentials = std::move(c);
  };
  // The fake ICE transports don't take sessions from the pool.
  transport_controller_->MaybeStartGathering(/*discard_candidate_pool=*/false,
                                             on_pooled_credentials);
  EXPECT_THAT(WaitUntil([&] { return credentials.has_value(); }, IsTrue(),
                        {.timeout = kTimeout}),
              IsRtcOk());
  EXPECT_THAT(credentials, Optional(SizeIs(2)));

  credentials.reset();
  transport_controller_->MaybeStartGathering(/*discard_candidate_pool=*/true,
                                             on_pooled_credentials);
  EXPECT_THAT(WaitUntil([&] { return credentials.has_value(); }, IsTrue(),
                        {.timeout = kTimeout}),
              IsRtcOk());
  EXPECT_THAT(credentials, Optional(IsEmpty()));

  SendTask(network_thread_.get(),
           [&] { EXPECT_EQ(port_allocator_->GetPooledSession(), nullptr); });
}

TEST_F(JsepTransportControllerTest, DiscardCandidatePool) {
  CreateJsepTransportController(JsepTransportController::Config());
  SendTask(network_thread_.get(), [&] {
    port_allocator_->SetConfiguration(
        /*stun_servers=*/{}, /*turn_servers=*/{}, /*candidate_pool_size=*/1,
        NO_PRUNE, /*turn_customizer=*/nullptr,
        /*stun_candidate_keepalive_interval=*/std::nullopt);
    EXPECT_NE(port_allocator_->GetPooledSession(), nullptr);
  });
  transport_controller_->DiscardCandidatePool();
  // Tasks posted to the network thread run in order, so this task runs after
  // the pool has been discarded.
  SendTask(network_thread_.get(),
           [&] { EXPECT_EQ(port_allocator_->GetPooledSession(), nullptr); });
}

TEST_F(JsepTransportControllerTest, AddRemoveRemoteCandidates) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithoutBundle();
  transport_controller_->SetLocalDescription(SdpType::kOffer, description.get(),
                                             nullptr);
  transport_controller_->SetRemoteDescription(
      SdpType::kAnswer, description.get(), description.get());
  auto fake_audio_dtls = GetFakeDtlsTransport(kAudioMid1);
  ASSERT_NE(nullptr, fake_audio_dtls);
  Candidates candidates;
  candidates.push_back(CreateCandidate());
  SendTask(network_thread_.get(), [&] {
    EXPECT_TRUE(
        transport_controller_->AddRemoteCandidates(kAudioMid1, candidates)
            .ok());
    EXPECT_EQ(
        1U, fake_audio_dtls->fake_ice_transport()->remote_candidates().size());
  });

  IceCandidate ice_candidate(kAudioMid1, -1, candidates[0]);
  EXPECT_TRUE(transport_controller_->RemoveRemoteCandidate(&ice_candidate));
  SendTask(network_thread_.get(), [&] {
    EXPECT_EQ(
        0U, fake_audio_dtls->fake_ice_transport()->remote_candidates().size());
  });
}

TEST_F(JsepTransportControllerTest, SetAndGetLocalCertificate) {
  CreateJsepTransportController(JsepTransportController::Config());

  scoped_refptr<RTCCertificate> certificate1 =
      RTCCertificate::Create(SSLIdentity::Create("session1", KT_DEFAULT));
  scoped_refptr<RTCCertificate> returned_certificate;

  auto description = std::make_unique<SessionDescription>();
  AddAudioSection(description.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, certificate1);

  // Apply the local certificate.
  EXPECT_TRUE(transport_controller_->SetLocalCertificate(certificate1));
  // Apply the local description.
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());
  returned_certificate = network_thread_->BlockingCall(
      [&] { return transport_controller_->GetLocalCertificate(kAudioMid1); });
  EXPECT_TRUE(returned_certificate);
  EXPECT_EQ(certificate1->identity()->certificate().ToPEMString(),
            returned_certificate->identity()->certificate().ToPEMString());

  // Should fail if called for a nonexistant transport.
  EXPECT_EQ(nullptr, network_thread_->BlockingCall([&] {
    return transport_controller_->GetLocalCertificate(kVideoMid1);
  }));

  // Shouldn't be able to change the identity once set.
  scoped_refptr<RTCCertificate> certificate2 =
      RTCCertificate::Create(SSLIdentity::Create("session2", KT_DEFAULT));
  EXPECT_FALSE(transport_controller_->SetLocalCertificate(certificate2));
}

// The certificate is applied on the network thread after SetLocalCertificate()
// has returned. Transports that already exist get it too.
TEST_F(JsepTransportControllerTest,
       SetLocalCertificateAppliesToExistingTransports) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithoutBundle();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());
  FakeDtlsTransport* fake_audio_dtls = GetFakeDtlsTransport(kAudioMid1);
  ASSERT_NE(fake_audio_dtls, nullptr);
  SendTask(network_thread_.get(),
           [&] { EXPECT_FALSE(fake_audio_dtls->IsDtlsActive()); });

  scoped_refptr<RTCCertificate> certificate =
      RTCCertificate::Create(SSLIdentity::Create("session", KT_DEFAULT));
  EXPECT_TRUE(transport_controller_->SetLocalCertificate(certificate));
  // Tasks posted to the network thread run in order, so this task runs after
  // the certificate has been applied.
  SendTask(network_thread_.get(), [&] {
    EXPECT_EQ(transport_controller_->GetLocalCertificate(kAudioMid1),
              certificate);
    EXPECT_TRUE(fake_audio_dtls->IsDtlsActive());
  });
}

TEST_F(JsepTransportControllerTest, SetLocalCertificateRejectsNull) {
  CreateJsepTransportController(JsepTransportController::Config());
  EXPECT_FALSE(transport_controller_->SetLocalCertificate(nullptr));
  // A rejected certificate doesn't prevent setting one later.
  EXPECT_TRUE(transport_controller_->SetLocalCertificate(
      RTCCertificate::Create(SSLIdentity::Create("session", KT_DEFAULT))));
}

// When the signaling and network threads are the same thread, the certificate
// is applied before SetLocalCertificate() returns, so a description with its
// fingerprint can be applied right after, in the same task.
TEST_F(JsepTransportControllerTest,
       SetLocalCertificateAppliesRightAwayWithMergedThreads) {
  CreateJsepTransportController(JsepTransportController::Config(),
                                /*signaling_thread=*/network_thread_.get());
  scoped_refptr<RTCCertificate> certificate =
      RTCCertificate::Create(SSLIdentity::Create("session", KT_DEFAULT));
  auto description = std::make_unique<SessionDescription>();
  AddAudioSection(description.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, certificate);
  SendTask(network_thread_.get(), [&] {
    EXPECT_TRUE(transport_controller_->SetLocalCertificate(certificate));
    EXPECT_THAT(transport_controller_->SetLocalDescription(
                    SdpType::kOffer, description.get(), nullptr),
                IsRtcOk());
    EXPECT_EQ(transport_controller_->GetLocalCertificate(kAudioMid1),
              certificate);
  });
}

TEST_F(JsepTransportControllerTest, GetRemoteSSLCertChain) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithBundleGroup();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());
  FakeSSLCertificate fake_certificate("fake_data");

  auto fake_audio_dtls = GetFakeDtlsTransport(kAudioMid1);
  std::unique_ptr<SSLCertChain> returned_cert_chain =
      network_thread_->BlockingCall([&] {
        fake_audio_dtls->SetRemoteSSLCertificate(&fake_certificate);
        return transport_controller_->GetRemoteSSLCertChain(kAudioMid1);
      });
  ASSERT_TRUE(returned_cert_chain);
  ASSERT_EQ(1u, returned_cert_chain->GetSize());
  EXPECT_EQ(fake_certificate.ToPEMString(),
            returned_cert_chain->Get(0).ToPEMString());

  // Should fail if called for a nonexistant transport.
  EXPECT_FALSE(network_thread_->BlockingCall([&] {
    return transport_controller_->GetRemoteSSLCertChain(kAudioMid2);
  }));
}

TEST_F(JsepTransportControllerTest, GetDtlsRole) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto offer_certificate =
      RTCCertificate::Create(SSLIdentity::Create("offer", KT_DEFAULT));
  auto answer_certificate =
      RTCCertificate::Create(SSLIdentity::Create("answer", KT_DEFAULT));
  transport_controller_->SetLocalCertificate(offer_certificate);

  auto offer_desc = std::make_unique<SessionDescription>();
  AddAudioSection(offer_desc.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, offer_certificate);
  auto answer_desc = std::make_unique<SessionDescription>();
  AddAudioSection(answer_desc.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, answer_certificate);

  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, offer_desc.get(), nullptr)
          .ok());

  std::optional<SSLRole> role = transport_controller_->GetDtlsRole(kAudioMid1);
  // The DTLS role is not decided yet.
  EXPECT_FALSE(role);
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, offer_desc.get(),
                                         answer_desc.get())
                  .ok());
  role = transport_controller_->GetDtlsRole(kAudioMid1);

  ASSERT_TRUE(role);
  EXPECT_EQ(SSL_CLIENT, *role);
}

// A DTLS role change while a remote answer is applied (here, while the video
// section gets bundled onto the audio transport) posts a snapshot of the
// transport states to the signaling thread. That snapshot is older than the one
// that SetRemoteDescription() stores and must not replace it.
TEST_F(JsepTransportControllerTest, DtlsRoleNotOverwrittenByMidHopSnapshot) {
  fake_dtls_transport_factory_ =
      std::make_unique<RoleChangeNotifyingFakeDtlsTransportFactory>();
  CreateJsepTransportController(JsepTransportController::Config());
  auto offer_certificate =
      RTCCertificate::Create(SSLIdentity::Create("offer", KT_DEFAULT));
  auto answer_certificate =
      RTCCertificate::Create(SSLIdentity::Create("answer", KT_DEFAULT));
  EXPECT_TRUE(transport_controller_->SetLocalCertificate(offer_certificate));

  // With the balanced bundle policy, the offer creates one transport per m=
  // section and the answer bundles them.
  auto offer = CreateBundledDescriptionWithDtls(CONNECTIONROLE_ACTPASS,
                                                offer_certificate);
  auto answer = CreateBundledDescriptionWithDtls(CONNECTIONROLE_PASSIVE,
                                                 answer_certificate);
  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kOffer, offer.get(), nullptr)
                  .ok());
  EXPECT_TRUE(
      transport_controller_
          ->SetRemoteDescription(SdpType::kAnswer, offer.get(), answer.get())
          .ok());
  // Run the tasks that were posted to the signaling thread.
  main_thread_.Flush();

  EXPECT_THAT(transport_controller_->GetDtlsRole(kAudioMid1),
              Optional(SSL_CLIENT));
  EXPECT_THAT(transport_controller_->GetDtlsRole(kVideoMid1),
              Optional(SSL_CLIENT));
}

// The DTLS role can also be set outside of SetLocalDescription() and
// SetRemoteDescription(), e.g. when a DTLS ClientHello arrives before the
// answer has been applied. If the snapshot that is posted for that change is
// still queued when a description is applied, it must not replace the newer
// snapshot.
TEST_F(JsepTransportControllerTest,
       DtlsRoleNotOverwrittenBySnapshotQueuedBeforeDescription) {
  fake_dtls_transport_factory_ =
      std::make_unique<RoleChangeNotifyingFakeDtlsTransportFactory>();
  CreateJsepTransportController(JsepTransportController::Config());
  auto offer_certificate =
      RTCCertificate::Create(SSLIdentity::Create("offer", KT_DEFAULT));
  auto answer_certificate =
      RTCCertificate::Create(SSLIdentity::Create("answer", KT_DEFAULT));
  EXPECT_TRUE(transport_controller_->SetLocalCertificate(offer_certificate));

  auto offer = CreateBundledDescriptionWithDtls(CONNECTIONROLE_ACTPASS,
                                                offer_certificate);
  auto answer = CreateBundledDescriptionWithDtls(CONNECTIONROLE_ACTIVE,
                                                 answer_certificate);
  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kOffer, offer.get(), nullptr)
                  .ok());

  // Simulate an early ClientHello, which makes the audio transport the DTLS
  // server before the answer is applied.
  SendTask(network_thread_.get(), [&] {
    transport_controller_->GetDtlsTransport(kAudioMid1)
        ->SetDtlsRole(SSL_SERVER);
  });
  // Apply the answer before the posted snapshot has been processed.
  EXPECT_TRUE(
      transport_controller_
          ->SetRemoteDescription(SdpType::kAnswer, offer.get(), answer.get())
          .ok());
  main_thread_.Flush();

  EXPECT_THAT(transport_controller_->GetDtlsRole(kAudioMid1),
              Optional(SSL_SERVER));
  EXPECT_THAT(transport_controller_->GetDtlsRole(kVideoMid1),
              Optional(SSL_SERVER));
}

TEST_F(JsepTransportControllerTest, GetStats) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithBundleGroup();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());

  SendTask(network_thread_.get(), [&] {
    TransportStats stats;
    EXPECT_TRUE(transport_controller_->GetStats(kAudioMid1, &stats));
    EXPECT_EQ(kAudioMid1, stats.transport_name);
    EXPECT_EQ(1u, stats.channel_stats.size());
    // Return false for non-existing transport.
    EXPECT_FALSE(transport_controller_->GetStats(kAudioMid2, &stats));
  });
}

TEST_F(JsepTransportControllerTest, SignalConnectionStateFailed) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithoutBundle();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());

  SendTask(network_thread_.get(), [&] {
    auto fake_ice = static_cast<FakeIceTransportInternal*>(
        transport_controller_->GetDtlsTransport(kAudioMid1)->ice_transport());
    fake_ice->SetCandidatesGatheringComplete();
    fake_ice->SetConnectionCount(1);
    // The connection stats will be failed if there is no active connection.
    fake_ice->SetConnectionCount(0);
  });
  EXPECT_THAT(
      WaitUntil([&] { return connection_state_; },
                ::testing::Eq(kIceConnectionFailed), {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(1, connection_state_signal_count_);
  EXPECT_THAT(
      WaitUntil([&] { return ice_connection_state_; },
                ::testing::Eq(PeerConnectionInterface::kIceConnectionFailed),
                {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(1, ice_connection_state_signal_count_);
  EXPECT_THAT(
      WaitUntil(
          [&] { return combined_connection_state_; },
          ::testing::Eq(PeerConnectionInterface::PeerConnectionState::kFailed),
          {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(1, combined_connection_state_signal_count_);
}

TEST_F(JsepTransportControllerTest,
       SignalConnectionStateConnectedNoMediaTransport) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithoutBundle();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());

  auto fake_audio_dtls = GetFakeDtlsTransport(kAudioMid1);
  auto fake_video_dtls = GetFakeDtlsTransport(kVideoMid1);

  // First, have one transport connect, and another fail, to ensure that
  // the first transport connecting didn't trigger a "connected" state signal.
  // We should only get a signal when all are connected.
  SendTask(network_thread_.get(), [&] {
    fake_audio_dtls->fake_ice_transport()->SetConnectionCount(1);
    fake_audio_dtls->SetWritable(true);
    fake_audio_dtls->fake_ice_transport()->SetCandidatesGatheringComplete();
    // Decrease the number of the connection to trigger the signal.
    fake_video_dtls->fake_ice_transport()->SetConnectionCount(1);
    fake_video_dtls->fake_ice_transport()->SetConnectionCount(0);
    fake_video_dtls->fake_ice_transport()->SetCandidatesGatheringComplete();
  });

  EXPECT_THAT(
      WaitUntil([&] { return connection_state_; },
                ::testing::Eq(kIceConnectionFailed), {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(1, connection_state_signal_count_);
  EXPECT_THAT(
      WaitUntil([&] { return ice_connection_state_; },
                ::testing::Eq(PeerConnectionInterface::kIceConnectionFailed),
                {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(2, ice_connection_state_signal_count_);
  EXPECT_THAT(
      WaitUntil(
          [&] { return combined_connection_state_; },
          ::testing::Eq(PeerConnectionInterface::PeerConnectionState::kFailed),
          {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(2, combined_connection_state_signal_count_);

  SendTask(network_thread_.get(), [&] {
    fake_audio_dtls->SetDtlsState(DtlsTransportState::kConnected);
    fake_video_dtls->SetDtlsState(DtlsTransportState::kConnected);
    // Set the connection count to be 2 and the
    // webrtc::FakeIceTransportInternal will set the transport state to be
    // STATE_CONNECTING.
    fake_video_dtls->fake_ice_transport()->SetConnectionCount(2);
    fake_video_dtls->SetWritable(true);
  });
  EXPECT_THAT(
      WaitUntil([&] { return connection_state_; },
                ::testing::Eq(kIceConnectionConnected), {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(2, connection_state_signal_count_);
  EXPECT_THAT(
      WaitUntil([&] { return ice_connection_state_; },
                ::testing::Eq(PeerConnectionInterface::kIceConnectionConnected),
                {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(3, ice_connection_state_signal_count_);
  EXPECT_THAT(
      WaitUntil([&] { return combined_connection_state_; },
                ::testing::Eq(
                    PeerConnectionInterface::PeerConnectionState::kConnected),
                {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(3, combined_connection_state_signal_count_);
}

TEST_F(JsepTransportControllerTest, SignalConnectionStateComplete) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithoutBundle();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());

  auto fake_audio_dtls = GetFakeDtlsTransport(kAudioMid1);
  auto fake_video_dtls = GetFakeDtlsTransport(kVideoMid1);

  // First, have one transport connect, and another fail, to ensure that
  // the first transport connecting didn't trigger a "connected" state signal.
  // We should only get a signal when all are connected.
  SendTask(network_thread_.get(), [&] {
    fake_audio_dtls->fake_ice_transport()->SetTransportState(
        IceTransportState::kCompleted,
        IceTransportStateInternal::STATE_COMPLETED);
    fake_audio_dtls->SetWritable(true);
    fake_audio_dtls->fake_ice_transport()->SetCandidatesGatheringComplete();
  });

  EXPECT_THAT(
      WaitUntil([&] { return ice_connection_state_; },
                ::testing::Eq(PeerConnectionInterface::kIceConnectionChecking),
                {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(1, ice_connection_state_signal_count_);
  EXPECT_THAT(
      WaitUntil([&] { return combined_connection_state_; },
                ::testing::Eq(
                    PeerConnectionInterface::PeerConnectionState::kConnecting),
                {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(1, combined_connection_state_signal_count_);

  SendTask(network_thread_.get(), [&] {
    fake_video_dtls->fake_ice_transport()->SetTransportState(
        IceTransportState::kFailed, IceTransportStateInternal::STATE_FAILED);
    fake_video_dtls->fake_ice_transport()->SetCandidatesGatheringComplete();
  });

  EXPECT_THAT(
      WaitUntil([&] { return connection_state_; },
                ::testing::Eq(kIceConnectionFailed), {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(1, connection_state_signal_count_);
  EXPECT_THAT(
      WaitUntil([&] { return ice_connection_state_; },
                ::testing::Eq(PeerConnectionInterface::kIceConnectionFailed),
                {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(2, ice_connection_state_signal_count_);
  EXPECT_THAT(
      WaitUntil(
          [&] { return combined_connection_state_; },
          ::testing::Eq(PeerConnectionInterface::PeerConnectionState::kFailed),
          {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(2, combined_connection_state_signal_count_);

  SendTask(network_thread_.get(), [&] {
    fake_audio_dtls->SetDtlsState(DtlsTransportState::kConnected);
    fake_video_dtls->SetDtlsState(DtlsTransportState::kConnected);
    // Set the connection count to be 1 and the
    // webrtc::FakeIceTransportInternal will set the transport state to be
    // STATE_COMPLETED.
    fake_video_dtls->fake_ice_transport()->SetTransportState(
        IceTransportState::kCompleted,
        IceTransportStateInternal::STATE_COMPLETED);
    fake_video_dtls->SetWritable(true);
  });
  EXPECT_THAT(
      WaitUntil([&] { return connection_state_; },
                ::testing::Eq(kIceConnectionCompleted), {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(3, connection_state_signal_count_);
  EXPECT_THAT(
      WaitUntil([&] { return ice_connection_state_; },
                ::testing::Eq(PeerConnectionInterface::kIceConnectionCompleted),
                {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(3, ice_connection_state_signal_count_);
  EXPECT_THAT(
      WaitUntil([&] { return combined_connection_state_; },
                ::testing::Eq(
                    PeerConnectionInterface::PeerConnectionState::kConnected),
                {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(3, combined_connection_state_signal_count_);
}

TEST_F(JsepTransportControllerTest, SignalIceGatheringStateGathering) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithoutBundle();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());

  auto fake_audio_dtls = GetFakeDtlsTransport(kAudioMid1);
  SendTask(network_thread_.get(), [&] {
    fake_audio_dtls->fake_ice_transport()->MaybeStartGathering();
  });
  // Should be in the gathering state as soon as any transport starts gathering.
  EXPECT_THAT(
      WaitUntil([&] { return gathering_state_; },
                ::testing::Eq(kIceGatheringGathering), {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(1, gathering_state_signal_count_);
}

TEST_F(JsepTransportControllerTest, SignalIceGatheringStateComplete) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithoutBundle();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());

  auto fake_audio_dtls = GetFakeDtlsTransport(kAudioMid1);
  auto fake_video_dtls = GetFakeDtlsTransport(kVideoMid1);

  SendTask(network_thread_.get(), [&] {
    fake_audio_dtls->fake_ice_transport()->MaybeStartGathering();
  });
  EXPECT_THAT(
      WaitUntil([&] { return gathering_state_; },
                ::testing::Eq(kIceGatheringGathering), {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(1, gathering_state_signal_count_);

  // Have one transport finish gathering, to make sure gathering
  // completion wasn't signalled if only one transport finished gathering.
  SendTask(network_thread_.get(), [&] {
    fake_audio_dtls->fake_ice_transport()->SetCandidatesGatheringComplete();
  });
  main_thread_.Flush();
  EXPECT_EQ(1, gathering_state_signal_count_);

  SendTask(network_thread_.get(), [&] {
    fake_video_dtls->fake_ice_transport()->MaybeStartGathering();
  });
  main_thread_.Flush();
  EXPECT_THAT(
      WaitUntil([&] { return gathering_state_; },
                ::testing::Eq(kIceGatheringGathering), {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(1, gathering_state_signal_count_);

  SendTask(network_thread_.get(), [&] {
    fake_video_dtls->fake_ice_transport()->SetCandidatesGatheringComplete();
  });
  EXPECT_THAT(
      WaitUntil([&] { return gathering_state_; },
                ::testing::Eq(kIceGatheringComplete), {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(2, gathering_state_signal_count_);
}

// Test that when the last transport that hasn't finished connecting and/or
// gathering is destroyed, the aggregate state jumps to "completed". This can
// happen if, for example, we have an audio and video transport, the audio
// transport completes, then we start bundling video on the audio transport.
TEST_F(JsepTransportControllerTest,
       SignalingWhenLastIncompleteTransportDestroyed) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithBundleGroup();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());

  auto fake_audio_dtls = GetFakeDtlsTransport(kAudioMid1);
  auto fake_video_dtls = GetFakeDtlsTransport(kVideoMid1);
  EXPECT_NE(fake_audio_dtls, fake_video_dtls);

  SendTask(network_thread_.get(), [&] {
    fake_audio_dtls->fake_ice_transport()->MaybeStartGathering();
  });
  EXPECT_THAT(
      WaitUntil([&] { return gathering_state_; },
                ::testing::Eq(kIceGatheringGathering), {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(1, gathering_state_signal_count_);

  // Let the audio transport complete.
  SendTask(network_thread_.get(), [&] {
    fake_audio_dtls->SetWritable(true);
    fake_audio_dtls->fake_ice_transport()->SetCandidatesGatheringComplete();
    fake_audio_dtls->fake_ice_transport()->SetConnectionCount(1);
    fake_audio_dtls->SetDtlsState(DtlsTransportState::kConnected);
  });
  main_thread_.Flush();
  EXPECT_EQ(1, gathering_state_signal_count_);

  // Set the remote description and enable the bundle.
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, description.get(),
                                         description.get())
                  .ok());
  // The BUNDLE should be enabled, the incomplete video transport should be
  // deleted and the states should be updated.
  fake_video_dtls = GetFakeDtlsTransport(kVideoMid1);
  EXPECT_EQ(fake_audio_dtls, fake_video_dtls);
  EXPECT_THAT(
      WaitUntil([&] { return connection_state_; },
                ::testing::Eq(kIceConnectionCompleted), {.timeout = kTimeout}),
      IsRtcOk());
  // Process the other state signals as well.
  main_thread_.Flush();
  EXPECT_EQ(PeerConnectionInterface::kIceConnectionCompleted,
            ice_connection_state_);
  EXPECT_EQ(PeerConnectionInterface::PeerConnectionState::kConnected,
            combined_connection_state_);
  EXPECT_THAT(
      WaitUntil([&] { return gathering_state_; },
                ::testing::Eq(kIceGatheringComplete), {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(2, gathering_state_signal_count_);
}

// Test that states immediately return to "new" if all transports are
// discarded. This should happen at offer time, even though the transport
// controller may keep the transport alive in case of rollback.
TEST_F(JsepTransportControllerTest,
       IceStatesReturnToNewWhenTransportsDiscarded) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = std::make_unique<SessionDescription>();
  AddAudioSection(description.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, description.get(),
                                         description.get())
                  .ok());

  // Trigger and verify initial non-new states.
  auto fake_audio_dtls = GetFakeDtlsTransport(kAudioMid1);
  SendTask(network_thread_.get(), [&] {
    fake_audio_dtls->fake_ice_transport()->MaybeStartGathering();
    fake_audio_dtls->fake_ice_transport()->SetTransportState(
        IceTransportState::kChecking,
        IceTransportStateInternal::STATE_CONNECTING);
  });
  EXPECT_THAT(
      WaitUntil([&] { return ice_connection_state_; },
                ::testing::Eq(PeerConnectionInterface::kIceConnectionChecking),
                {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(1, ice_connection_state_signal_count_);
  EXPECT_THAT(
      WaitUntil([&] { return combined_connection_state_; },
                ::testing::Eq(
                    PeerConnectionInterface::PeerConnectionState::kConnecting),
                {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(1, combined_connection_state_signal_count_);
  EXPECT_THAT(
      WaitUntil([&] { return gathering_state_; },
                ::testing::Eq(kIceGatheringGathering), {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(1, gathering_state_signal_count_);

  // Reject m= section which should disconnect the transport and return states
  // to "new".
  description->contents()[0].rejected = true;
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kOffer, description.get(),
                                         description.get())
                  .ok());
  EXPECT_THAT(
      WaitUntil([&] { return ice_connection_state_; },
                ::testing::Eq(PeerConnectionInterface::kIceConnectionNew),
                {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(2, ice_connection_state_signal_count_);
  EXPECT_THAT(WaitUntil([&] { return combined_connection_state_; },
                        ::testing::Eq(
                            PeerConnectionInterface::PeerConnectionState::kNew),
                        {.timeout = kTimeout}),
              IsRtcOk());
  EXPECT_EQ(2, combined_connection_state_signal_count_);
  EXPECT_THAT(WaitUntil([&] { return gathering_state_; },
                        ::testing::Eq(kIceGatheringNew), {.timeout = kTimeout}),
              IsRtcOk());
  EXPECT_EQ(2, gathering_state_signal_count_);

  // For good measure, rollback the offer and verify that states return to
  // their previous values.
  EXPECT_TRUE(RollbackTransports().ok());
  EXPECT_THAT(
      WaitUntil([&] { return ice_connection_state_; },
                ::testing::Eq(PeerConnectionInterface::kIceConnectionChecking),
                {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(3, ice_connection_state_signal_count_);
  EXPECT_THAT(
      WaitUntil([&] { return combined_connection_state_; },
                ::testing::Eq(
                    PeerConnectionInterface::PeerConnectionState::kConnecting),
                {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(3, combined_connection_state_signal_count_);
  EXPECT_THAT(
      WaitUntil([&] { return gathering_state_; },
                ::testing::Eq(kIceGatheringGathering), {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(3, gathering_state_signal_count_);
}

TEST_F(JsepTransportControllerTest, SignalCandidatesGathered) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithBundleGroup();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());
  transport_controller_->MaybeStartGathering(/*discard_candidate_pool=*/false,
                                             /*on_pooled_credentials=*/nullptr);

  auto fake_audio_dtls = GetFakeDtlsTransport(kAudioMid1);
  SendTask(network_thread_.get(), [&] {
    fake_audio_dtls->fake_ice_transport()->NotifyCandidateGathered(
        fake_audio_dtls->fake_ice_transport(), CreateCandidate());
  });
  EXPECT_THAT(WaitUntil([&] { return candidates_signal_count_; },
                        ::testing::Eq(1), {.timeout = kTimeout}),
              IsRtcOk());
  EXPECT_EQ(1u, candidates_[kAudioMid1].size());
}

TEST_F(JsepTransportControllerTest, IceSignalingOccursOnNetworkThread) {
  EXPECT_EQ(ice_signaled_on_thread_, nullptr);
  CreateJsepTransportController(JsepTransportController::Config());
  CreateLocalDescriptionAndCompleteConnection();

  // connecting --> connected --> completed
  EXPECT_THAT(
      WaitUntil([&] { return connection_state_; },
                ::testing::Eq(kIceConnectionCompleted), {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(2, connection_state_signal_count_);

  // new --> gathering --> complete
  EXPECT_THAT(
      WaitUntil([&] { return gathering_state_; },
                ::testing::Eq(kIceGatheringComplete), {.timeout = kTimeout}),
      IsRtcOk());
  EXPECT_EQ(2, gathering_state_signal_count_);

  EXPECT_THAT(WaitUntil([&] { return candidates_[kAudioMid1].size(); },
                        ::testing::Eq(1u), {.timeout = kTimeout}),
              IsRtcOk());
  EXPECT_THAT(WaitUntil([&] { return candidates_[kVideoMid1].size(); },
                        ::testing::Eq(1u), {.timeout = kTimeout}),
              IsRtcOk());
  EXPECT_EQ(2, candidates_signal_count_);

  EXPECT_EQ(ice_signaled_on_thread_, network_thread_.get());
}

// Test that if the TransportController was created with the
// `redetermine_role_on_ice_restart` parameter set to false, the role is *not*
// redetermined on an ICE restart.
TEST_F(JsepTransportControllerTest, IceRoleNotRedetermined) {
  JsepTransportController::Config config;
  config.redetermine_role_on_ice_restart = false;

  CreateJsepTransportController(std::move(config));
  // Let the `transport_controller_` be the controlled side initially.
  auto remote_offer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_offer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  auto local_answer = std::make_unique<SessionDescription>();
  AddAudioSection(local_answer.get(), kAudioMid1, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);

  EXPECT_TRUE(
      transport_controller_
          ->SetRemoteDescription(SdpType::kOffer, nullptr, remote_offer.get())
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kAnswer, local_answer.get(),
                                        remote_offer.get())
                  .ok());

  auto fake_dtls = GetFakeDtlsTransport(kAudioMid1);
  EXPECT_EQ(ICEROLE_CONTROLLED, GetIceRole(fake_dtls));

  // New offer will trigger the ICE restart.
  auto restart_local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(restart_local_offer.get(), kAudioMid1, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kOffer,
                                        restart_local_offer.get(),
                                        remote_offer.get())
                  .ok());
  EXPECT_EQ(ICEROLE_CONTROLLED, GetIceRole(fake_dtls));
}

// Tests ICE-Lite mode in remote answer.
TEST_F(JsepTransportControllerTest, SetIceRoleWhenIceLiteInRemoteAnswer) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  auto fake_dtls = GetFakeDtlsTransport(kAudioMid1);
  EXPECT_EQ(ICEROLE_CONTROLLING, GetIceRole(fake_dtls));
  SendTask(network_thread_.get(), [&] {
    EXPECT_EQ(ICEMODE_FULL, fake_dtls->fake_ice_transport()->remote_ice_mode());
  });

  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kAudioMid1, kIceUfrag2, kIcePwd2,
                  ICEMODE_LITE, CONNECTIONROLE_PASSIVE, nullptr);
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());
  EXPECT_EQ(ICEROLE_CONTROLLING, GetIceRole(fake_dtls));
  SendTask(network_thread_.get(), [&] {
    EXPECT_EQ(ICEMODE_LITE, fake_dtls->fake_ice_transport()->remote_ice_mode());
  });
}

// Tests that the ICE role remains "controlling" if a subsequent offer that
// does an ICE restart is received from an ICE lite endpoint. Regression test
// for: https://crbug.com/710760
TEST_F(JsepTransportControllerTest,
       IceRoleIsControllingAfterIceRestartFromIceLiteEndpoint) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto remote_offer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_offer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_LITE, CONNECTIONROLE_ACTPASS, nullptr);
  auto local_answer = std::make_unique<SessionDescription>();
  AddAudioSection(local_answer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  // Initial Offer/Answer exchange. If the remote offerer is ICE-Lite, then the
  // local side is the controlling.
  EXPECT_TRUE(
      transport_controller_
          ->SetRemoteDescription(SdpType::kOffer, nullptr, remote_offer.get())
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kAnswer, local_answer.get(),
                                        remote_offer.get())
                  .ok());
  auto fake_dtls = GetFakeDtlsTransport(kAudioMid1);
  EXPECT_EQ(ICEROLE_CONTROLLING, GetIceRole(fake_dtls));

  // In the subsequence remote offer triggers an ICE restart.
  auto remote_offer2 = std::make_unique<SessionDescription>();
  AddAudioSection(remote_offer2.get(), kAudioMid1, kIceUfrag2, kIcePwd2,
                  ICEMODE_LITE, CONNECTIONROLE_ACTPASS, nullptr);
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kOffer, local_answer.get(),
                                         remote_offer2.get())
                  .ok());
  auto local_answer2 = std::make_unique<SessionDescription>();
  AddAudioSection(local_answer2.get(), kAudioMid1, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kAnswer, local_answer2.get(),
                                        remote_offer2.get())
                  .ok());
  fake_dtls = GetFakeDtlsTransport(kAudioMid1);
  // The local side is still the controlling role since the remote side is using
  // ICE-Lite.
  EXPECT_EQ(ICEROLE_CONTROLLING, GetIceRole(fake_dtls));
}

// Tests that the SDP has more than one audio/video m= sections.
TEST_F(JsepTransportControllerTest, MultipleMediaSectionsOfSameTypeWithBundle) {
  CreateJsepTransportController(JsepTransportController::Config());
  ContentGroup bundle_group(GROUP_TYPE_BUNDLE);
  bundle_group.AddContentName(kAudioMid1);
  bundle_group.AddContentName(kAudioMid2);
  bundle_group.AddContentName(kVideoMid1);
  bundle_group.AddContentName(kDataMid1);

  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(local_offer.get(), kAudioMid2, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kVideoMid1, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddDataSection(local_offer.get(), kDataMid1, MediaProtocolType::kSctp,
                 kIceUfrag1, kIcePwd1, ICEMODE_FULL, CONNECTIONROLE_ACTPASS,
                 nullptr);

  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  AddAudioSection(remote_answer.get(), kAudioMid2, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  AddVideoSection(remote_answer.get(), kVideoMid1, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  AddDataSection(remote_answer.get(), kDataMid1, MediaProtocolType::kSctp,
                 kIceUfrag1, kIcePwd1, ICEMODE_FULL, CONNECTIONROLE_PASSIVE,
                 nullptr);

  local_offer->AddGroup(bundle_group);
  remote_answer->AddGroup(bundle_group);

  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());
  // Verify that all the sections are bundled on kAudio1.
  auto transport1 = GetRtpTransport(kAudioMid1);
  auto transport2 = GetRtpTransport(kAudioMid2);
  auto transport3 = GetRtpTransport(kVideoMid1);
  auto transport4 = GetRtpTransport(kDataMid1);
  EXPECT_EQ(transport1, transport2);
  EXPECT_EQ(transport1, transport3);
  EXPECT_EQ(transport1, transport4);

  EXPECT_EQ(transport_controller_->LookupDtlsTransportByMid(kAudioMid1),
            transport_controller_->LookupDtlsTransportByMid(kVideoMid1));

  // Verify the OnRtpTransport/DtlsTransportChanged signals are fired correctly.
  SendTask(network_thread_.get(), [&] {
    auto it = changed_rtp_transport_by_mid_.find(kAudioMid2);
    ASSERT_TRUE(it != changed_rtp_transport_by_mid_.end());
    EXPECT_EQ(transport1, it->second);
    it = changed_rtp_transport_by_mid_.find(kAudioMid2);
    ASSERT_TRUE(it != changed_rtp_transport_by_mid_.end());
    EXPECT_EQ(transport1, it->second);
    it = changed_rtp_transport_by_mid_.find(kVideoMid1);
    ASSERT_TRUE(it != changed_rtp_transport_by_mid_.end());
    EXPECT_EQ(transport1, it->second);
    // Verify the DtlsTransport for the SCTP data channel is reset correctly.
    auto it2 = changed_dtls_transport_by_mid_.find(kDataMid1);
    ASSERT_TRUE(it2 != changed_dtls_transport_by_mid_.end());
  });
}

TEST_F(JsepTransportControllerTest, MultipleBundleGroups) {
  static const char kMid1Audio[] = "1_audio";
  static const char kMid2Video[] = "2_video";
  static const char kMid3Audio[] = "3_audio";
  static const char kMid4Video[] = "4_video";

  CreateJsepTransportController(JsepTransportController::Config());
  ContentGroup bundle_group1(GROUP_TYPE_BUNDLE);
  bundle_group1.AddContentName(kMid1Audio);
  bundle_group1.AddContentName(kMid2Video);
  ContentGroup bundle_group2(GROUP_TYPE_BUNDLE);
  bundle_group2.AddContentName(kMid3Audio);
  bundle_group2.AddContentName(kMid4Video);

  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid2Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(local_offer.get(), kMid3Audio, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid4Video, kIceUfrag4, kIcePwd4,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  local_offer->AddGroup(bundle_group1);
  local_offer->AddGroup(bundle_group2);

  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid2Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(remote_answer.get(), kMid3Audio, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid4Video, kIceUfrag4, kIcePwd4,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  remote_answer->AddGroup(bundle_group1);
  remote_answer->AddGroup(bundle_group2);

  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());

  // Verify that (kMid1Audio,kMid2Video) and (kMid3Audio,kMid4Video) form two
  // distinct bundled groups.
  auto mid1_transport = GetRtpTransport(kMid1Audio);
  auto mid2_transport = GetRtpTransport(kMid2Video);
  auto mid3_transport = GetRtpTransport(kMid3Audio);
  auto mid4_transport = GetRtpTransport(kMid4Video);
  EXPECT_EQ(mid1_transport, mid2_transport);
  EXPECT_EQ(mid3_transport, mid4_transport);
  EXPECT_NE(mid1_transport, mid3_transport);

  SendTask(network_thread_.get(), [&] {
    auto it = changed_rtp_transport_by_mid_.find(kMid1Audio);
    ASSERT_TRUE(it != changed_rtp_transport_by_mid_.end());
    EXPECT_EQ(it->second, mid1_transport);

    it = changed_rtp_transport_by_mid_.find(kMid2Video);
    ASSERT_TRUE(it != changed_rtp_transport_by_mid_.end());
    EXPECT_EQ(it->second, mid2_transport);

    it = changed_rtp_transport_by_mid_.find(kMid3Audio);
    ASSERT_TRUE(it != changed_rtp_transport_by_mid_.end());
    EXPECT_EQ(it->second, mid3_transport);

    it = changed_rtp_transport_by_mid_.find(kMid4Video);
    ASSERT_TRUE(it != changed_rtp_transport_by_mid_.end());
    EXPECT_EQ(it->second, mid4_transport);
  });
}

TEST_F(JsepTransportControllerTest,
       MultipleBundleGroupsInOfferButOnlyASingleGroupInAnswer) {
  static const char kMid1Audio[] = "1_audio";
  static const char kMid2Video[] = "2_video";
  static const char kMid3Audio[] = "3_audio";
  static const char kMid4Video[] = "4_video";

  CreateJsepTransportController(JsepTransportController::Config());
  ContentGroup bundle_group1(GROUP_TYPE_BUNDLE);
  bundle_group1.AddContentName(kMid1Audio);
  bundle_group1.AddContentName(kMid2Video);
  ContentGroup bundle_group2(GROUP_TYPE_BUNDLE);
  bundle_group2.AddContentName(kMid3Audio);
  bundle_group2.AddContentName(kMid4Video);

  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid2Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(local_offer.get(), kMid3Audio, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid4Video, kIceUfrag4, kIcePwd4,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  // The offer has both groups.
  local_offer->AddGroup(bundle_group1);
  local_offer->AddGroup(bundle_group2);

  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid2Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(remote_answer.get(), kMid3Audio, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid4Video, kIceUfrag4, kIcePwd4,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  // The answer only has a single group! This is what happens when talking to an
  // endpoint that does not have support for multiple BUNDLE groups.
  remote_answer->AddGroup(bundle_group1);

  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());

  // Verify that (kMid1Audio,kMid2Video) form a bundle group, but that
  // kMid3Audio and kMid4Video are unbundled.
  auto mid1_transport = GetRtpTransport(kMid1Audio);
  auto mid2_transport = GetRtpTransport(kMid2Video);
  auto mid3_transport = GetRtpTransport(kMid3Audio);
  auto mid4_transport = GetRtpTransport(kMid4Video);
  EXPECT_EQ(mid1_transport, mid2_transport);
  EXPECT_NE(mid3_transport, mid4_transport);
  EXPECT_NE(mid1_transport, mid3_transport);
  EXPECT_NE(mid1_transport, mid4_transport);
}

TEST_F(JsepTransportControllerTest, MultipleBundleGroupsIllegallyChangeGroup) {
  static const char kMid1Audio[] = "1_audio";
  static const char kMid2Video[] = "2_video";
  static const char kMid3Audio[] = "3_audio";
  static const char kMid4Video[] = "4_video";

  CreateJsepTransportController(JsepTransportController::Config());
  // Offer groups (kMid1Audio,kMid2Video) and (kMid3Audio,kMid4Video).
  ContentGroup offer_bundle_group1(GROUP_TYPE_BUNDLE);
  offer_bundle_group1.AddContentName(kMid1Audio);
  offer_bundle_group1.AddContentName(kMid2Video);
  ContentGroup offer_bundle_group2(GROUP_TYPE_BUNDLE);
  offer_bundle_group2.AddContentName(kMid3Audio);
  offer_bundle_group2.AddContentName(kMid4Video);
  // Answer groups (kMid1Audio,kMid4Video) and (kMid3Audio,kMid2Video), i.e. the
  // second group members have switched places. This should get rejected.
  ContentGroup answer_bundle_group1(GROUP_TYPE_BUNDLE);
  answer_bundle_group1.AddContentName(kMid1Audio);
  answer_bundle_group1.AddContentName(kMid4Video);
  ContentGroup answer_bundle_group2(GROUP_TYPE_BUNDLE);
  answer_bundle_group2.AddContentName(kMid3Audio);
  answer_bundle_group2.AddContentName(kMid2Video);

  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid2Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(local_offer.get(), kMid3Audio, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid4Video, kIceUfrag4, kIcePwd4,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  local_offer->AddGroup(offer_bundle_group1);
  local_offer->AddGroup(offer_bundle_group2);

  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid2Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(remote_answer.get(), kMid3Audio, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid4Video, kIceUfrag4, kIcePwd4,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  remote_answer->AddGroup(answer_bundle_group1);
  remote_answer->AddGroup(answer_bundle_group2);

  // Accept offer.
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  // Reject answer!
  EXPECT_FALSE(transport_controller_
                   ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                          remote_answer.get())
                   .ok());
}

TEST_F(JsepTransportControllerTest, MultipleBundleGroupsInvalidSubsets) {
  static const char kMid1Audio[] = "1_audio";
  static const char kMid2Video[] = "2_video";
  static const char kMid3Audio[] = "3_audio";
  static const char kMid4Video[] = "4_video";

  CreateJsepTransportController(JsepTransportController::Config());
  // Offer groups (kMid1Audio,kMid2Video) and (kMid3Audio,kMid4Video).
  ContentGroup offer_bundle_group1(GROUP_TYPE_BUNDLE);
  offer_bundle_group1.AddContentName(kMid1Audio);
  offer_bundle_group1.AddContentName(kMid2Video);
  ContentGroup offer_bundle_group2(GROUP_TYPE_BUNDLE);
  offer_bundle_group2.AddContentName(kMid3Audio);
  offer_bundle_group2.AddContentName(kMid4Video);
  // Answer groups (kMid1Audio) and (kMid2Video), i.e. the second group was
  // moved from the first group. This should get rejected.
  ContentGroup answer_bundle_group1(GROUP_TYPE_BUNDLE);
  answer_bundle_group1.AddContentName(kMid1Audio);
  ContentGroup answer_bundle_group2(GROUP_TYPE_BUNDLE);
  answer_bundle_group2.AddContentName(kMid2Video);

  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid2Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(local_offer.get(), kMid3Audio, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid4Video, kIceUfrag4, kIcePwd4,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  local_offer->AddGroup(offer_bundle_group1);
  local_offer->AddGroup(offer_bundle_group2);

  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid2Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(remote_answer.get(), kMid3Audio, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid4Video, kIceUfrag4, kIcePwd4,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  remote_answer->AddGroup(answer_bundle_group1);
  remote_answer->AddGroup(answer_bundle_group2);

  // Accept offer.
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  // Reject answer!
  EXPECT_FALSE(transport_controller_
                   ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                          remote_answer.get())
                   .ok());
}

TEST_F(JsepTransportControllerTest, MultipleBundleGroupsInvalidOverlap) {
  static const char kMid1Audio[] = "1_audio";
  static const char kMid2Video[] = "2_video";
  static const char kMid3Audio[] = "3_audio";

  CreateJsepTransportController(JsepTransportController::Config());
  // Offer groups (kMid1Audio,kMid3Audio) and (kMid2Video,kMid3Audio), i.e.
  // kMid3Audio is in both groups - this is illegal.
  ContentGroup offer_bundle_group1(GROUP_TYPE_BUNDLE);
  offer_bundle_group1.AddContentName(kMid1Audio);
  offer_bundle_group1.AddContentName(kMid3Audio);
  ContentGroup offer_bundle_group2(GROUP_TYPE_BUNDLE);
  offer_bundle_group2.AddContentName(kMid2Video);
  offer_bundle_group2.AddContentName(kMid3Audio);

  auto offer = std::make_unique<SessionDescription>();
  AddAudioSection(offer.get(), kMid1Audio, kIceUfrag1, kIcePwd1, ICEMODE_FULL,
                  CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(offer.get(), kMid2Video, kIceUfrag2, kIcePwd2, ICEMODE_FULL,
                  CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(offer.get(), kMid3Audio, kIceUfrag3, kIcePwd3, ICEMODE_FULL,
                  CONNECTIONROLE_ACTPASS, nullptr);
  offer->AddGroup(offer_bundle_group1);
  offer->AddGroup(offer_bundle_group2);

  // Reject offer, both if set as local or remote.
  EXPECT_FALSE(transport_controller_
                   ->SetLocalDescription(SdpType::kOffer, offer.get(), nullptr)
                   .ok());
  EXPECT_FALSE(
      transport_controller_
          ->SetRemoteDescription(SdpType::kOffer, offer.get(), offer.get())
          .ok());
}

TEST_F(JsepTransportControllerTest, MultipleBundleGroupsUnbundleFirstMid) {
  static const char kMid1Audio[] = "1_audio";
  static const char kMid2Audio[] = "2_audio";
  static const char kMid3Audio[] = "3_audio";
  static const char kMid4Video[] = "4_video";
  static const char kMid5Video[] = "5_video";
  static const char kMid6Video[] = "6_video";

  CreateJsepTransportController(JsepTransportController::Config());
  // Offer groups (kMid1Audio,kMid2Audio,kMid3Audio) and
  // (kMid4Video,kMid5Video,kMid6Video).
  ContentGroup offer_bundle_group1(GROUP_TYPE_BUNDLE);
  offer_bundle_group1.AddContentName(kMid1Audio);
  offer_bundle_group1.AddContentName(kMid2Audio);
  offer_bundle_group1.AddContentName(kMid3Audio);
  ContentGroup offer_bundle_group2(GROUP_TYPE_BUNDLE);
  offer_bundle_group2.AddContentName(kMid4Video);
  offer_bundle_group2.AddContentName(kMid5Video);
  offer_bundle_group2.AddContentName(kMid6Video);
  // Answer groups (kMid2Audio,kMid3Audio) and (kMid5Video,kMid6Video), i.e.
  // we've moved the first MIDs out of the groups.
  ContentGroup answer_bundle_group1(GROUP_TYPE_BUNDLE);
  answer_bundle_group1.AddContentName(kMid2Audio);
  answer_bundle_group1.AddContentName(kMid3Audio);
  ContentGroup answer_bundle_group2(GROUP_TYPE_BUNDLE);
  answer_bundle_group2.AddContentName(kMid5Video);
  answer_bundle_group2.AddContentName(kMid6Video);

  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(local_offer.get(), kMid2Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(local_offer.get(), kMid3Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid4Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid5Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid6Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  local_offer->AddGroup(offer_bundle_group1);
  local_offer->AddGroup(offer_bundle_group2);

  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(remote_answer.get(), kMid2Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(remote_answer.get(), kMid3Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid4Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid5Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid6Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  remote_answer->AddGroup(answer_bundle_group1);
  remote_answer->AddGroup(answer_bundle_group2);

  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());

  auto mid1_transport = GetRtpTransport(kMid1Audio);
  auto mid2_transport = GetRtpTransport(kMid2Audio);
  auto mid3_transport = GetRtpTransport(kMid3Audio);
  auto mid4_transport = GetRtpTransport(kMid4Video);
  auto mid5_transport = GetRtpTransport(kMid5Video);
  auto mid6_transport = GetRtpTransport(kMid6Video);
  EXPECT_NE(mid1_transport, mid2_transport);
  EXPECT_EQ(mid2_transport, mid3_transport);
  EXPECT_NE(mid4_transport, mid5_transport);
  EXPECT_EQ(mid5_transport, mid6_transport);
  EXPECT_NE(mid1_transport, mid4_transport);
  EXPECT_NE(mid2_transport, mid5_transport);
}

TEST_F(JsepTransportControllerTest, MultipleBundleGroupsChangeFirstMid) {
  static const char kMid1Audio[] = "1_audio";
  static const char kMid2Audio[] = "2_audio";
  static const char kMid3Audio[] = "3_audio";
  static const char kMid4Video[] = "4_video";
  static const char kMid5Video[] = "5_video";
  static const char kMid6Video[] = "6_video";

  CreateJsepTransportController(JsepTransportController::Config());
  // Offer groups (kMid1Audio,kMid2Audio,kMid3Audio) and
  // (kMid4Video,kMid5Video,kMid6Video).
  ContentGroup offer_bundle_group1(GROUP_TYPE_BUNDLE);
  offer_bundle_group1.AddContentName(kMid1Audio);
  offer_bundle_group1.AddContentName(kMid2Audio);
  offer_bundle_group1.AddContentName(kMid3Audio);
  ContentGroup offer_bundle_group2(GROUP_TYPE_BUNDLE);
  offer_bundle_group2.AddContentName(kMid4Video);
  offer_bundle_group2.AddContentName(kMid5Video);
  offer_bundle_group2.AddContentName(kMid6Video);
  // Answer groups (kMid2Audio,kMid1Audio,kMid3Audio) and
  // (kMid5Video,kMid6Video,kMid4Video), i.e. we've changed which MID is first
  // but accept the whole group.
  ContentGroup answer_bundle_group1(GROUP_TYPE_BUNDLE);
  answer_bundle_group1.AddContentName(kMid2Audio);
  answer_bundle_group1.AddContentName(kMid1Audio);
  answer_bundle_group1.AddContentName(kMid3Audio);
  ContentGroup answer_bundle_group2(GROUP_TYPE_BUNDLE);
  answer_bundle_group2.AddContentName(kMid5Video);
  answer_bundle_group2.AddContentName(kMid6Video);
  answer_bundle_group2.AddContentName(kMid4Video);

  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(local_offer.get(), kMid2Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(local_offer.get(), kMid3Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid4Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid5Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid6Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  local_offer->AddGroup(offer_bundle_group1);
  local_offer->AddGroup(offer_bundle_group2);

  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(remote_answer.get(), kMid2Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(remote_answer.get(), kMid3Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid4Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid5Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid6Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  remote_answer->AddGroup(answer_bundle_group1);
  remote_answer->AddGroup(answer_bundle_group2);

  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());

  // The fact that we accept this answer is actually a bug. If we accept the
  // first MID to be in the group, we should also accept that it is the tagged
  // one.
  // TODO(https://crbug.com/webrtc/12699): When this issue is fixed, change this
  // to EXPECT_FALSE and remove the below expectations about transports.
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());
  auto mid1_transport = GetRtpTransport(kMid1Audio);
  auto mid2_transport = GetRtpTransport(kMid2Audio);
  auto mid3_transport = GetRtpTransport(kMid3Audio);
  auto mid4_transport = GetRtpTransport(kMid4Video);
  auto mid5_transport = GetRtpTransport(kMid5Video);
  auto mid6_transport = GetRtpTransport(kMid6Video);
  EXPECT_NE(mid1_transport, mid4_transport);
  EXPECT_EQ(mid1_transport, mid2_transport);
  EXPECT_EQ(mid2_transport, mid3_transport);
  EXPECT_EQ(mid4_transport, mid5_transport);
  EXPECT_EQ(mid5_transport, mid6_transport);
}

TEST_F(JsepTransportControllerTest,
       MultipleBundleGroupsSectionsAddedInSubsequentOffer) {
  static const char kMid1Audio[] = "1_audio";
  static const char kMid2Audio[] = "2_audio";
  static const char kMid3Audio[] = "3_audio";
  static const char kMid4Video[] = "4_video";
  static const char kMid5Video[] = "5_video";
  static const char kMid6Video[] = "6_video";

  CreateJsepTransportController(JsepTransportController::Config());
  // Start by grouping (kMid1Audio,kMid2Audio) and (kMid4Video,kMid4f5Video).
  ContentGroup bundle_group1(GROUP_TYPE_BUNDLE);
  bundle_group1.AddContentName(kMid1Audio);
  bundle_group1.AddContentName(kMid2Audio);
  ContentGroup bundle_group2(GROUP_TYPE_BUNDLE);
  bundle_group2.AddContentName(kMid4Video);
  bundle_group2.AddContentName(kMid5Video);

  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(local_offer.get(), kMid2Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid4Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid5Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  local_offer->AddGroup(bundle_group1);
  local_offer->AddGroup(bundle_group2);

  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(remote_answer.get(), kMid2Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid4Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid5Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  remote_answer->AddGroup(bundle_group1);
  remote_answer->AddGroup(bundle_group2);

  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());

  // Add kMid3Audio and kMid6Video to the respective audio/video bundle groups.
  ContentGroup new_bundle_group1(GROUP_TYPE_BUNDLE);
  bundle_group1.AddContentName(kMid3Audio);
  ContentGroup new_bundle_group2(GROUP_TYPE_BUNDLE);
  bundle_group2.AddContentName(kMid6Video);

  auto subsequent_offer = std::make_unique<SessionDescription>();
  AddAudioSection(subsequent_offer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(subsequent_offer.get(), kMid2Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(subsequent_offer.get(), kMid3Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(subsequent_offer.get(), kMid4Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(subsequent_offer.get(), kMid5Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(subsequent_offer.get(), kMid6Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  subsequent_offer->AddGroup(bundle_group1);
  subsequent_offer->AddGroup(bundle_group2);
  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kOffer, subsequent_offer.get(),
                                        remote_answer.get())
                  .ok());
  auto mid1_transport = GetRtpTransport(kMid1Audio);
  auto mid2_transport = GetRtpTransport(kMid2Audio);
  auto mid3_transport = GetRtpTransport(kMid3Audio);
  auto mid4_transport = GetRtpTransport(kMid4Video);
  auto mid5_transport = GetRtpTransport(kMid5Video);
  auto mid6_transport = GetRtpTransport(kMid6Video);
  EXPECT_NE(mid1_transport, mid4_transport);
  EXPECT_EQ(mid1_transport, mid2_transport);
  EXPECT_EQ(mid2_transport, mid3_transport);
  EXPECT_EQ(mid4_transport, mid5_transport);
  EXPECT_EQ(mid5_transport, mid6_transport);
}

TEST_F(JsepTransportControllerTest,
       MultipleBundleGroupsCombinedInSubsequentOffer) {
  static const char kMid1Audio[] = "1_audio";
  static const char kMid2Audio[] = "2_audio";
  static const char kMid3Video[] = "3_video";
  static const char kMid4Video[] = "4_video";

  CreateJsepTransportController(JsepTransportController::Config());
  // Start by grouping (kMid1Audio,kMid2Audio) and (kMid3Video,kMid4Video).
  ContentGroup bundle_group1(GROUP_TYPE_BUNDLE);
  bundle_group1.AddContentName(kMid1Audio);
  bundle_group1.AddContentName(kMid2Audio);
  ContentGroup bundle_group2(GROUP_TYPE_BUNDLE);
  bundle_group2.AddContentName(kMid3Video);
  bundle_group2.AddContentName(kMid4Video);

  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(local_offer.get(), kMid2Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid3Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid4Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  local_offer->AddGroup(bundle_group1);
  local_offer->AddGroup(bundle_group2);

  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(remote_answer.get(), kMid2Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid3Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid4Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  remote_answer->AddGroup(bundle_group1);
  remote_answer->AddGroup(bundle_group2);

  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());

  // Switch to grouping (kMid1Audio,kMid2Audio,kMid3Video,kMid4Video).
  // This is a illegal without first removing m= sections from their groups.
  ContentGroup new_bundle_group(GROUP_TYPE_BUNDLE);
  new_bundle_group.AddContentName(kMid1Audio);
  new_bundle_group.AddContentName(kMid2Audio);
  new_bundle_group.AddContentName(kMid3Video);
  new_bundle_group.AddContentName(kMid4Video);

  auto subsequent_offer = std::make_unique<SessionDescription>();
  AddAudioSection(subsequent_offer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(subsequent_offer.get(), kMid2Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(subsequent_offer.get(), kMid3Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(subsequent_offer.get(), kMid4Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  subsequent_offer->AddGroup(new_bundle_group);
  EXPECT_FALSE(transport_controller_
                   ->SetLocalDescription(SdpType::kOffer,
                                         subsequent_offer.get(),
                                         remote_answer.get())
                   .ok());
}

TEST_F(JsepTransportControllerTest,
       MultipleBundleGroupsSplitInSubsequentOffer) {
  static const char kMid1Audio[] = "1_audio";
  static const char kMid2Audio[] = "2_audio";
  static const char kMid3Video[] = "3_video";
  static const char kMid4Video[] = "4_video";

  CreateJsepTransportController(JsepTransportController::Config());
  // Start by grouping (kMid1Audio,kMid2Audio,kMid3Video,kMid4Video).
  ContentGroup bundle_group(GROUP_TYPE_BUNDLE);
  bundle_group.AddContentName(kMid1Audio);
  bundle_group.AddContentName(kMid2Audio);
  bundle_group.AddContentName(kMid3Video);
  bundle_group.AddContentName(kMid4Video);

  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(local_offer.get(), kMid2Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid3Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid4Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  local_offer->AddGroup(bundle_group);

  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(remote_answer.get(), kMid2Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid3Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid4Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  remote_answer->AddGroup(bundle_group);

  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());

  // Switch to grouping (kMid1Audio,kMid2Audio) and (kMid3Video,kMid4Video).
  // This is a illegal without first removing m= sections from their groups.
  ContentGroup new_bundle_group1(GROUP_TYPE_BUNDLE);
  new_bundle_group1.AddContentName(kMid1Audio);
  new_bundle_group1.AddContentName(kMid2Audio);
  ContentGroup new_bundle_group2(GROUP_TYPE_BUNDLE);
  new_bundle_group2.AddContentName(kMid3Video);
  new_bundle_group2.AddContentName(kMid4Video);

  auto subsequent_offer = std::make_unique<SessionDescription>();
  AddAudioSection(subsequent_offer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(subsequent_offer.get(), kMid2Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(subsequent_offer.get(), kMid3Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(subsequent_offer.get(), kMid4Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  subsequent_offer->AddGroup(new_bundle_group1);
  subsequent_offer->AddGroup(new_bundle_group2);
  EXPECT_FALSE(transport_controller_
                   ->SetLocalDescription(SdpType::kOffer,
                                         subsequent_offer.get(),
                                         remote_answer.get())
                   .ok());
}

TEST_F(JsepTransportControllerTest,
       MultipleBundleGroupsShuffledInSubsequentOffer) {
  static const char kMid1Audio[] = "1_audio";
  static const char kMid2Audio[] = "2_audio";
  static const char kMid3Video[] = "3_video";
  static const char kMid4Video[] = "4_video";

  CreateJsepTransportController(JsepTransportController::Config());
  // Start by grouping (kMid1Audio,kMid2Audio) and (kMid3Video,kMid4Video).
  ContentGroup bundle_group1(GROUP_TYPE_BUNDLE);
  bundle_group1.AddContentName(kMid1Audio);
  bundle_group1.AddContentName(kMid2Audio);
  ContentGroup bundle_group2(GROUP_TYPE_BUNDLE);
  bundle_group2.AddContentName(kMid3Video);
  bundle_group2.AddContentName(kMid4Video);

  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(local_offer.get(), kMid2Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid3Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid4Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  local_offer->AddGroup(bundle_group1);
  local_offer->AddGroup(bundle_group2);

  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(remote_answer.get(), kMid2Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid3Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(remote_answer.get(), kMid4Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  remote_answer->AddGroup(bundle_group1);
  remote_answer->AddGroup(bundle_group2);

  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());

  // Switch to grouping (kMid1Audio,kMid3Video) and (kMid2Audio,kMid3Video).
  // This is a illegal without first removing m= sections from their groups.
  ContentGroup new_bundle_group1(GROUP_TYPE_BUNDLE);
  new_bundle_group1.AddContentName(kMid1Audio);
  new_bundle_group1.AddContentName(kMid3Video);
  ContentGroup new_bundle_group2(GROUP_TYPE_BUNDLE);
  new_bundle_group2.AddContentName(kMid2Audio);
  new_bundle_group2.AddContentName(kMid4Video);

  auto subsequent_offer = std::make_unique<SessionDescription>();
  AddAudioSection(subsequent_offer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(subsequent_offer.get(), kMid2Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(subsequent_offer.get(), kMid3Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(subsequent_offer.get(), kMid4Video, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  subsequent_offer->AddGroup(new_bundle_group1);
  subsequent_offer->AddGroup(new_bundle_group2);
  EXPECT_FALSE(transport_controller_
                   ->SetLocalDescription(SdpType::kOffer,
                                         subsequent_offer.get(),
                                         remote_answer.get())
                   .ok());
}

// Tests that only a subset of all the m= sections are bundled.
TEST_F(JsepTransportControllerTest, BundleSubsetOfMediaSections) {
  CreateJsepTransportController(JsepTransportController::Config());
  ContentGroup bundle_group(GROUP_TYPE_BUNDLE);
  bundle_group.AddContentName(kAudioMid1);
  bundle_group.AddContentName(kVideoMid1);

  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(local_offer.get(), kAudioMid2, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kVideoMid1, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);

  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  AddAudioSection(remote_answer.get(), kAudioMid2, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  AddVideoSection(remote_answer.get(), kVideoMid1, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);

  local_offer->AddGroup(bundle_group);
  remote_answer->AddGroup(bundle_group);
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());

  // Verifiy that only `kAudio1` and `kVideo1` are bundled.
  auto transport1 = GetRtpTransport(kAudioMid1);
  auto transport2 = GetRtpTransport(kAudioMid2);
  auto transport3 = GetRtpTransport(kVideoMid1);
  EXPECT_NE(transport1, transport2);
  EXPECT_EQ(transport1, transport3);

  SendTask(network_thread_.get(), [&] {
    auto it = changed_rtp_transport_by_mid_.find(kVideoMid1);
    ASSERT_TRUE(it != changed_rtp_transport_by_mid_.end());
    EXPECT_EQ(transport1, it->second);
    it = changed_rtp_transport_by_mid_.find(kAudioMid2);
    EXPECT_TRUE(transport2 == it->second);
  });
}

// Tests that the initial offer/answer only have data section and audio/video
// sections are added in the subsequent offer.
TEST_F(JsepTransportControllerTest, BundleOnDataSectionInSubsequentOffer) {
  CreateJsepTransportController(JsepTransportController::Config());
  ContentGroup bundle_group(GROUP_TYPE_BUNDLE);
  bundle_group.AddContentName(kDataMid1);

  auto local_offer = std::make_unique<SessionDescription>();
  AddDataSection(local_offer.get(), kDataMid1, MediaProtocolType::kSctp,
                 kIceUfrag1, kIcePwd1, ICEMODE_FULL, CONNECTIONROLE_ACTPASS,
                 nullptr);
  auto remote_answer = std::make_unique<SessionDescription>();
  AddDataSection(remote_answer.get(), kDataMid1, MediaProtocolType::kSctp,
                 kIceUfrag1, kIcePwd1, ICEMODE_FULL, CONNECTIONROLE_PASSIVE,
                 nullptr);
  local_offer->AddGroup(bundle_group);
  remote_answer->AddGroup(bundle_group);

  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());
  auto data_transport = GetRtpTransport(kDataMid1);

  // Add audio/video sections in subsequent offer.
  AddAudioSection(local_offer.get(), kAudioMid1, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kVideoMid1, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(remote_answer.get(), kAudioMid1, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  AddVideoSection(remote_answer.get(), kVideoMid1, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);

  // Reset the bundle group and do another offer/answer exchange.
  bundle_group.AddContentName(kAudioMid1);
  bundle_group.AddContentName(kVideoMid1);
  local_offer->RemoveGroupByName(GROUP_TYPE_BUNDLE);
  local_offer->AddGroup(bundle_group);

  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kOffer, local_offer.get(),
                                        remote_answer.get())
                  .ok());
  remote_answer->RemoveGroupByName(GROUP_TYPE_BUNDLE);
  remote_answer->AddGroup(bundle_group);
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());

  auto audio_transport = GetRtpTransport(kAudioMid1);
  auto video_transport = GetRtpTransport(kVideoMid1);
  EXPECT_EQ(data_transport, audio_transport);
  EXPECT_EQ(data_transport, video_transport);
}

TEST_F(JsepTransportControllerTest, VideoDataRejectedInAnswer) {
  CreateJsepTransportController(JsepTransportController::Config());
  ContentGroup bundle_group(GROUP_TYPE_BUNDLE);
  bundle_group.AddContentName(kAudioMid1);
  bundle_group.AddContentName(kVideoMid1);
  bundle_group.AddContentName(kDataMid1);

  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kVideoMid1, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddDataSection(local_offer.get(), kDataMid1, MediaProtocolType::kSctp,
                 kIceUfrag3, kIcePwd3, ICEMODE_FULL, CONNECTIONROLE_ACTPASS,
                 nullptr);

  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  AddVideoSection(remote_answer.get(), kVideoMid1, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  AddDataSection(remote_answer.get(), kDataMid1, MediaProtocolType::kSctp,
                 kIceUfrag3, kIcePwd3, ICEMODE_FULL, CONNECTIONROLE_PASSIVE,
                 nullptr);
  // Reject video and data section.
  remote_answer->contents()[1].rejected = true;
  remote_answer->contents()[2].rejected = true;

  local_offer->AddGroup(bundle_group);
  remote_answer->AddGroup(bundle_group);

  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());

  // Verify the RtpTransport/DtlsTransport is destroyed correctly.
  EXPECT_EQ(nullptr, GetRtpTransport(kVideoMid1));
  EXPECT_EQ(nullptr, GetDtlsTransport(kDataMid1));
  // Verify the signals are fired correctly.
  SendTask(network_thread_.get(), [&] {
    auto it = changed_rtp_transport_by_mid_.find(kVideoMid1);
    ASSERT_TRUE(it != changed_rtp_transport_by_mid_.end());
    EXPECT_EQ(nullptr, it->second);
    auto it2 = changed_dtls_transport_by_mid_.find(kDataMid1);
    ASSERT_TRUE(it2 != changed_dtls_transport_by_mid_.end());
    EXPECT_EQ(nullptr, it2->second);
  });
}

// Tests that changing the bundled MID in subsequent offer/answer exchange is
// not supported.
// TODO(bugs.webrtc.org/6704): Change this test to expect success once issue is
// fixed
TEST_F(JsepTransportControllerTest, ChangeBundledMidNotSupported) {
  CreateJsepTransportController(JsepTransportController::Config());
  ContentGroup bundle_group(GROUP_TYPE_BUNDLE);
  bundle_group.AddContentName(kAudioMid1);
  bundle_group.AddContentName(kVideoMid1);

  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kVideoMid1, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);

  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  AddVideoSection(remote_answer.get(), kVideoMid1, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);

  local_offer->AddGroup(bundle_group);
  remote_answer->AddGroup(bundle_group);
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());
  EXPECT_EQ(GetRtpTransport(kAudioMid1), GetRtpTransport(kVideoMid1));

  // Reorder the bundle group.
  EXPECT_TRUE(bundle_group.RemoveContentName(kAudioMid1));
  bundle_group.AddContentName(kAudioMid1);
  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kOffer, local_offer.get(),
                                        remote_answer.get())
                  .ok());
  // The answerer uses the new bundle group and now the bundle mid is changed to
  // `kVideo1`.
  remote_answer->RemoveGroupByName(GROUP_TYPE_BUNDLE);
  remote_answer->AddGroup(bundle_group);
  EXPECT_FALSE(transport_controller_
                   ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                          remote_answer.get())
                   .ok());
}
// Test that rejecting only the first m= section of a BUNDLE group is treated as
// an error, but rejecting all of them works as expected.
TEST_F(JsepTransportControllerTest, RejectFirstContentInBundleGroup) {
  CreateJsepTransportController(JsepTransportController::Config());
  ContentGroup bundle_group(GROUP_TYPE_BUNDLE);
  bundle_group.AddContentName(kAudioMid1);
  bundle_group.AddContentName(kVideoMid1);
  bundle_group.AddContentName(kDataMid1);

  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kVideoMid1, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddDataSection(local_offer.get(), kDataMid1, MediaProtocolType::kSctp,
                 kIceUfrag3, kIcePwd3, ICEMODE_FULL, CONNECTIONROLE_ACTPASS,
                 nullptr);

  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  AddVideoSection(remote_answer.get(), kVideoMid1, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  AddDataSection(remote_answer.get(), kDataMid1, MediaProtocolType::kSctp,
                 kIceUfrag3, kIcePwd3, ICEMODE_FULL, CONNECTIONROLE_PASSIVE,
                 nullptr);
  // Reject audio content in answer.
  remote_answer->contents()[0].rejected = true;

  local_offer->AddGroup(bundle_group);
  remote_answer->AddGroup(bundle_group);

  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_FALSE(transport_controller_
                   ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                          remote_answer.get())
                   .ok());

  // Reject all the contents.
  remote_answer->contents()[1].rejected = true;
  remote_answer->contents()[2].rejected = true;
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());
  EXPECT_EQ(nullptr, GetRtpTransport(kAudioMid1));
  EXPECT_EQ(nullptr, GetRtpTransport(kVideoMid1));
  EXPECT_EQ(nullptr, GetDtlsTransport(kDataMid1));
}

TEST_F(JsepTransportControllerTest,
       RejectMissingContentInStaleRemoteOfferBundleGroup) {
  CreateJsepTransportController(JsepTransportController::Config());

  auto local_offer = CreateSessionDescriptionWithBundleGroup();
  std::unique_ptr<SessionDescription> remote_answer(local_offer->Clone());
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());

  auto remote_reoffer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_reoffer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  remote_reoffer->contents()[0].rejected = true;

  RTCError error = transport_controller_->SetRemoteDescription(
      SdpType::kOffer, local_offer.get(), remote_reoffer.get());
  EXPECT_FALSE(error.ok());
  EXPECT_EQ(RTCErrorType::INVALID_PARAMETER, error.type());
}

// Tests that applying non-RTCP-mux offer would fail when kRtcpMuxPolicyRequire
// is used.
TEST_F(JsepTransportControllerTest, ApplyNonRtcpMuxOfferWhenMuxingRequired) {
  JsepTransportController::Config config;
  config.rtcp_mux_policy = PeerConnectionInterface::kRtcpMuxPolicyRequire;
  CreateJsepTransportController(std::move(config));
  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);

  local_offer->contents()[0].media_description()->set_rtcp_mux(false);
  // Applying a non-RTCP-mux offer is expected to fail.
  EXPECT_FALSE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
}

// Tests that applying non-RTCP-mux answer would fail when kRtcpMuxPolicyRequire
// is used.
TEST_F(JsepTransportControllerTest, ApplyNonRtcpMuxAnswerWhenMuxingRequired) {
  JsepTransportController::Config config;
  config.rtcp_mux_policy = PeerConnectionInterface::kRtcpMuxPolicyRequire;
  CreateJsepTransportController(std::move(config));
  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());

  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  // Applying a non-RTCP-mux answer is expected to fail.
  remote_answer->contents()[0].media_description()->set_rtcp_mux(false);
  EXPECT_FALSE(transport_controller_
                   ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                          remote_answer.get())
                   .ok());
}

// This tests that the BUNDLE group in answer should be a subset of the offered
// group.
TEST_F(JsepTransportControllerTest,
       AddContentToBundleGroupInAnswerNotSupported) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto local_offer = CreateSessionDescriptionWithoutBundle();
  auto remote_answer = CreateSessionDescriptionWithoutBundle();

  ContentGroup offer_bundle_group(GROUP_TYPE_BUNDLE);
  offer_bundle_group.AddContentName(kAudioMid1);
  local_offer->AddGroup(offer_bundle_group);

  ContentGroup answer_bundle_group(GROUP_TYPE_BUNDLE);
  answer_bundle_group.AddContentName(kAudioMid1);
  answer_bundle_group.AddContentName(kVideoMid1);
  remote_answer->AddGroup(answer_bundle_group);
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_FALSE(transport_controller_
                   ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                          remote_answer.get())
                   .ok());
}

// This tests that the BUNDLE group with non-existing MID should be rejectd.
TEST_F(JsepTransportControllerTest, RejectBundleGroupWithNonExistingMid) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto local_offer = CreateSessionDescriptionWithoutBundle();
  auto remote_answer = CreateSessionDescriptionWithoutBundle();

  ContentGroup invalid_bundle_group(GROUP_TYPE_BUNDLE);
  // The BUNDLE group is invalid because there is no data section in the
  // description.
  invalid_bundle_group.AddContentName(kDataMid1);
  local_offer->AddGroup(invalid_bundle_group);
  remote_answer->AddGroup(invalid_bundle_group);

  EXPECT_FALSE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_FALSE(transport_controller_
                   ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                          remote_answer.get())
                   .ok());
}

// This tests that an answer shouldn't be able to remove an m= section from an
// established group without rejecting it.
TEST_F(JsepTransportControllerTest, RemoveContentFromBundleGroup) {
  CreateJsepTransportController(JsepTransportController::Config());

  auto local_offer = CreateSessionDescriptionWithBundleGroup();
  auto remote_answer = CreateSessionDescriptionWithBundleGroup();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());

  // Do an re-offer/answer.
  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kOffer, local_offer.get(),
                                        remote_answer.get())
                  .ok());
  auto new_answer = CreateSessionDescriptionWithoutBundle();
  ContentGroup new_bundle_group(GROUP_TYPE_BUNDLE);
  //  The answer removes video from the BUNDLE group without rejecting it is
  //  invalid.
  new_bundle_group.AddContentName(kAudioMid1);
  new_answer->AddGroup(new_bundle_group);

  // Applying invalid answer is expected to fail.
  EXPECT_FALSE(transport_controller_
                   ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                          new_answer.get())
                   .ok());

  // Rejected the video content.
  auto video_content = new_answer->GetContentByName(kVideoMid1);
  ASSERT_TRUE(video_content);
  video_content->rejected = true;
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         new_answer.get())
                  .ok());
}

// Test that the JsepTransportController can process a new local and remote
// description that changes the tagged BUNDLE group with the max-bundle policy
// specified.
// This is a regression test for bugs.webrtc.org/9954
TEST_F(JsepTransportControllerTest, ChangeTaggedMediaSectionMaxBundle) {
  CreateJsepTransportController(JsepTransportController::Config());

  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  ContentGroup bundle_group(GROUP_TYPE_BUNDLE);
  bundle_group.AddContentName(kAudioMid1);
  local_offer->AddGroup(bundle_group);
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());

  std::unique_ptr<SessionDescription> remote_answer(local_offer->Clone());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());

  std::unique_ptr<SessionDescription> local_reoffer(local_offer->Clone());
  local_reoffer->contents()[0].rejected = true;
  AddVideoSection(local_reoffer.get(), kVideoMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  local_reoffer->RemoveGroupByName(GROUP_TYPE_BUNDLE);
  ContentGroup new_bundle_group(GROUP_TYPE_BUNDLE);
  new_bundle_group.AddContentName(kVideoMid1);
  local_reoffer->AddGroup(new_bundle_group);

  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kOffer, local_reoffer.get(),
                                        remote_answer.get())
                  .ok());
  std::unique_ptr<SessionDescription> remote_reanswer(local_reoffer->Clone());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_reoffer.get(),
                                         remote_reanswer.get())
                  .ok());
}

TEST_F(JsepTransportControllerTest, RollbackRestoresRejectedTransport) {
  static const char kMid1Audio[] = "1_audio";

  // Perform initial offer/answer.
  CreateJsepTransportController(JsepTransportController::Config());
  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  std::unique_ptr<SessionDescription> remote_answer(local_offer->Clone());
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());

  auto mid1_transport = GetRtpTransport(kMid1Audio);

  // Apply a reoffer which rejects the m= section, causing the transport to be
  // set to null.
  auto local_reoffer = std::make_unique<SessionDescription>();
  AddAudioSection(local_reoffer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  local_reoffer->contents()[0].rejected = true;

  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kOffer, local_reoffer.get(),
                                        remote_answer.get())
                  .ok());
  auto old_mid1_transport = mid1_transport;
  mid1_transport = GetRtpTransport(kMid1Audio);
  EXPECT_EQ(nullptr, mid1_transport);

  // Rolling back shouldn't just create a new transport for MID 1, it should
  // restore the old transport.
  EXPECT_TRUE(RollbackTransports().ok());
  mid1_transport = GetRtpTransport(kMid1Audio);
  EXPECT_EQ(old_mid1_transport, mid1_transport);
}

// If an offer with a modified BUNDLE group causes a MID->transport mapping to
// change, rollback should restore the previous mapping.
TEST_F(JsepTransportControllerTest, RollbackRestoresPreviousTransportMapping) {
  static const char kMid1Audio[] = "1_audio";
  static const char kMid2Audio[] = "2_audio";
  static const char kMid3Audio[] = "3_audio";

  // Perform an initial offer/answer to establish a (kMid1Audio,kMid2Audio)
  // group.
  CreateJsepTransportController(JsepTransportController::Config());
  ContentGroup bundle_group(GROUP_TYPE_BUNDLE);
  bundle_group.AddContentName(kMid1Audio);
  bundle_group.AddContentName(kMid2Audio);

  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid2Audio, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(local_offer.get(), kMid3Audio, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  local_offer->AddGroup(bundle_group);

  std::unique_ptr<SessionDescription> remote_answer(local_offer->Clone());

  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());

  auto mid1_transport = GetRtpTransport(kMid1Audio);
  auto mid2_transport = GetRtpTransport(kMid2Audio);
  auto mid3_transport = GetRtpTransport(kMid3Audio);
  EXPECT_EQ(mid1_transport, mid2_transport);
  EXPECT_NE(mid1_transport, mid3_transport);

  // Apply a reoffer adding kMid3Audio to the group; transport mapping should
  // change, even without an answer, since this is an existing group.
  bundle_group.AddContentName(kMid3Audio);
  auto local_reoffer = std::make_unique<SessionDescription>();
  AddAudioSection(local_reoffer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_reoffer.get(), kMid2Audio, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddAudioSection(local_reoffer.get(), kMid3Audio, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  local_reoffer->AddGroup(bundle_group);

  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kOffer, local_reoffer.get(),
                                        remote_answer.get())
                  .ok());

  // Store the old transport pointer and verify that the offer actually changed
  // transports.
  auto old_mid3_transport = mid3_transport;
  mid1_transport = GetRtpTransport(kMid1Audio);
  mid2_transport = GetRtpTransport(kMid2Audio);
  mid3_transport = GetRtpTransport(kMid3Audio);
  EXPECT_EQ(mid1_transport, mid2_transport);
  EXPECT_EQ(mid1_transport, mid3_transport);

  // Rolling back shouldn't just create a new transport for MID 3, it should
  // restore the old transport.
  EXPECT_TRUE(RollbackTransports().ok());
  mid3_transport = GetRtpTransport(kMid3Audio);
  EXPECT_EQ(old_mid3_transport, mid3_transport);
}

// Test that if an offer adds a MID to a specific BUNDLE group and is then
// rolled back, it can be added to a different BUNDLE group in a new offer.
// This is effectively testing that rollback resets the BundleManager state.
TEST_F(JsepTransportControllerTest, RollbackAndAddToDifferentBundleGroup) {
  static const char kMid1Audio[] = "1_audio";
  static const char kMid2Audio[] = "2_audio";
  static const char kMid3Audio[] = "3_audio";

  // Perform an initial offer/answer to establish two bundle groups, each with
  // one MID.
  CreateJsepTransportController(JsepTransportController::Config());
  ContentGroup bundle_group1(GROUP_TYPE_BUNDLE);
  bundle_group1.AddContentName(kMid1Audio);
  ContentGroup bundle_group2(GROUP_TYPE_BUNDLE);
  bundle_group2.AddContentName(kMid2Audio);

  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(local_offer.get(), kMid2Audio, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  local_offer->AddGroup(bundle_group1);
  local_offer->AddGroup(bundle_group2);

  std::unique_ptr<SessionDescription> remote_answer(local_offer->Clone());

  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());

  // Apply an offer that adds kMid3Audio to the first BUNDLE group.,
  ContentGroup modified_bundle_group1(GROUP_TYPE_BUNDLE);
  modified_bundle_group1.AddContentName(kMid1Audio);
  modified_bundle_group1.AddContentName(kMid3Audio);
  auto subsequent_offer_1 = std::make_unique<SessionDescription>();
  AddAudioSection(subsequent_offer_1.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(subsequent_offer_1.get(), kMid2Audio, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(subsequent_offer_1.get(), kMid3Audio, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  subsequent_offer_1->AddGroup(modified_bundle_group1);
  subsequent_offer_1->AddGroup(bundle_group2);

  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kOffer,
                                        subsequent_offer_1.get(),
                                        remote_answer.get())
                  .ok());

  auto mid1_transport = GetRtpTransport(kMid1Audio);
  auto mid2_transport = GetRtpTransport(kMid2Audio);
  auto mid3_transport = GetRtpTransport(kMid3Audio);
  EXPECT_NE(mid1_transport, mid2_transport);
  EXPECT_EQ(mid1_transport, mid3_transport);

  // Rollback and expect the transport to be reset.
  EXPECT_TRUE(RollbackTransports().ok());
  EXPECT_EQ(nullptr, GetRtpTransport(kMid3Audio));

  // Apply an offer that adds kMid3Audio to the second BUNDLE group.,
  ContentGroup modified_bundle_group2(GROUP_TYPE_BUNDLE);
  modified_bundle_group2.AddContentName(kMid2Audio);
  modified_bundle_group2.AddContentName(kMid3Audio);
  auto subsequent_offer_2 = std::make_unique<SessionDescription>();
  AddAudioSection(subsequent_offer_2.get(), kMid1Audio, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(subsequent_offer_2.get(), kMid2Audio, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(subsequent_offer_2.get(), kMid3Audio, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  subsequent_offer_2->AddGroup(bundle_group1);
  subsequent_offer_2->AddGroup(modified_bundle_group2);

  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kOffer,
                                        subsequent_offer_2.get(),
                                        remote_answer.get())
                  .ok());

  mid1_transport = GetRtpTransport(kMid1Audio);
  mid2_transport = GetRtpTransport(kMid2Audio);
  mid3_transport = GetRtpTransport(kMid3Audio);
  EXPECT_NE(mid1_transport, mid2_transport);
  EXPECT_EQ(mid2_transport, mid3_transport);
}

// Test that a bundle-only offer without rtcp-mux in the bundle-only section
// is accepted.
TEST_F(JsepTransportControllerTest, BundleOnlySectionDoesNotNeedRtcpMux) {
  CreateJsepTransportController(JsepTransportController::Config());
  ContentGroup bundle_group(GROUP_TYPE_BUNDLE);
  bundle_group.AddContentName(kAudioMid1);
  bundle_group.AddContentName(kVideoMid1);

  auto offer = std::make_unique<SessionDescription>();
  AddAudioSection(offer.get(), kAudioMid1, kIceUfrag1, kIcePwd1, ICEMODE_FULL,
                  CONNECTIONROLE_ACTPASS, nullptr);
  AddVideoSection(offer.get(), kVideoMid1, kIceUfrag1, kIcePwd1, ICEMODE_FULL,
                  CONNECTIONROLE_ACTPASS, nullptr);
  offer->AddGroup(bundle_group);

  // Remove rtcp-mux and set bundle-only on the second content.
  offer->contents()[1].media_description()->set_rtcp_mux(false);
  offer->contents()[1].bundle_only = true;

  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kOffer, nullptr, offer.get())
                  .ok());
}

// Test that with max-bundle a single unbundled m-line is accepted.
TEST_F(JsepTransportControllerTest,
       MaxBundleDoesNotRequireBundleForFirstMline) {
  auto config = JsepTransportController::Config();
  config.bundle_policy = PeerConnectionInterface::kBundlePolicyMaxBundle;
  CreateJsepTransportController(std::move(config));

  auto offer = std::make_unique<SessionDescription>();
  AddAudioSection(offer.get(), kAudioMid1, kIceUfrag1, kIcePwd1, ICEMODE_FULL,
                  CONNECTIONROLE_ACTPASS, nullptr);
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kOffer, nullptr, offer.get())
                  .ok());
}

TEST_F(JsepTransportControllerTest, RtpTransportCountHistogramNoBundle) {
  metrics::Reset();
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithoutBundle();
  transport_controller_->SetLocalDescription(SdpType::kOffer, description.get(),
                                             nullptr);
  transport_controller_->SetRemoteDescription(
      SdpType::kAnswer, description.get(), description.get());
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.PeerConnection.RtpTransportCount"));
  // We expect 2 transports
  EXPECT_METRIC_THAT(
      metrics::Samples("WebRTC.PeerConnection.RtpTransportCount"),
      ElementsAre(Pair(2, 1)));
}

TEST_F(JsepTransportControllerTest, RtpTransportCountHistogramWithBundleGroup) {
  metrics::Reset();
  CreateJsepTransportController(JsepTransportController::Config());
  auto description = CreateSessionDescriptionWithBundleGroup();
  transport_controller_->SetLocalDescription(SdpType::kOffer, description.get(),
                                             nullptr);
  transport_controller_->SetRemoteDescription(
      SdpType::kAnswer, description.get(), description.get());
  EXPECT_METRIC_EQ(
      1, metrics::NumSamples("WebRTC.PeerConnection.RtpTransportCount"));
  // We expect 1 transport
  EXPECT_METRIC_THAT(
      metrics::Samples("WebRTC.PeerConnection.RtpTransportCount"),
      ElementsAre(Pair(1, 1)));
}

TEST_F(JsepTransportControllerTest,
       IceRoleRemainsControllingAfterIceLiteSequence) {
  CreateJsepTransportController(JsepTransportController::Config());
  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);

  // 1. Send Offer
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());
  // Check Role -> Controlling
  auto fake_dtls = GetFakeDtlsTransport(kAudioMid1);
  EXPECT_EQ(ICEROLE_CONTROLLING, GetIceRole(fake_dtls));

  // 2. Receive Answer
  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kAudioMid1, kIceUfrag2, kIcePwd2,
                  ICEMODE_LITE, CONNECTIONROLE_PASSIVE, nullptr);
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());
  // Check Role -> no change.
  EXPECT_EQ(ICEROLE_CONTROLLING, GetIceRole(fake_dtls));

  // 3. Receive Offer
  auto remote_offer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_offer.get(), kAudioMid1, kIceUfrag3, kIcePwd3,
                  ICEMODE_LITE, CONNECTIONROLE_ACTPASS, nullptr);
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kOffer, local_offer.get(),
                                         remote_offer.get())
                  .ok());
  // Check Role -> no change.
  EXPECT_EQ(ICEROLE_CONTROLLING, GetIceRole(fake_dtls));

  // 4. Send Answer
  auto local_answer = std::make_unique<SessionDescription>();
  AddAudioSection(local_answer.get(), kAudioMid1, kIceUfrag4, kIcePwd4,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kAnswer, local_answer.get(),
                                        remote_offer.get())
                  .ok());
  // Check Role -> should still be ICEROLE_CONTROLLING.
  EXPECT_EQ(ICEROLE_CONTROLLING, GetIceRole(fake_dtls));
}

TEST_F(JsepTransportControllerTest, CustomRtpTransportFactory) {
  class CustomRtpTransportFactory : public RtpTransportFactory {
   public:
    explicit CustomRtpTransportFactory(const FieldTrialsView& field_trials)
        : field_trials_view_(field_trials) {}
    std::unique_ptr<RtpTransport> CreateRtpTransport(
        absl::string_view transport_name,
        std::unique_ptr<DtlsTransportInternal> rtp_dtls_transport,
        std::unique_ptr<DtlsTransportInternal> rtcp_dtls_transport) override {
      auto transport = std::make_unique<RtpTransport>(
          /*rtcp_mux_enabled*/ false, field_trials_view_);
      transport->SetRtpPacketTransport(rtp_dtls_transport.get());
      if (transport_name == kAudioMid1) {
        audio_transport_ = transport.get();
        audio_rtp_dtls_transport_ = std::move(rtp_dtls_transport);
        audio_rtcp_dtls_transport_ = std::move(rtcp_dtls_transport);
      } else if (transport_name == kVideoMid1) {
        video_transport_ = transport.get();
        video_rtp_dtls_transport_ = std::move(rtp_dtls_transport);
        video_rtcp_dtls_transport_ = std::move(rtcp_dtls_transport);
      }
      return transport;
    }

    RtpTransport* audio_transport_;
    RtpTransport* video_transport_;

   private:
    // These DtlsTransportInternals just need to be kept alive.
    std::unique_ptr<DtlsTransportInternal> audio_rtp_dtls_transport_;
    std::unique_ptr<DtlsTransportInternal> audio_rtcp_dtls_transport_;
    std::unique_ptr<DtlsTransportInternal> video_rtp_dtls_transport_;
    std::unique_ptr<DtlsTransportInternal> video_rtcp_dtls_transport_;
    const FieldTrialsView& field_trials_view_;
  };

  custom_rtp_transport_factory_ =
      std::make_unique<CustomRtpTransportFactory>(field_trials_);
  CustomRtpTransportFactory* factory = static_cast<CustomRtpTransportFactory*>(
      custom_rtp_transport_factory_.get());

  JsepTransportController::Config config;
  config.rtp_transport_factory = custom_rtp_transport_factory_.get();
  CreateJsepTransportController(std::move(config));
  auto description = CreateSessionDescriptionWithoutBundle();
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, description.get(), nullptr)
          .ok());
  auto audio_rtp_transport = GetRtpTransport(kAudioMid1);
  auto video_rtp_transport = GetRtpTransport(kVideoMid1);
  EXPECT_NE(nullptr, audio_rtp_transport);
  EXPECT_EQ(factory->audio_transport_, audio_rtp_transport);
  EXPECT_NE(nullptr, video_rtp_transport);
  EXPECT_EQ(factory->video_transport_, video_rtp_transport);
}

TEST_F(JsepTransportControllerTest, IceRolePreservedOnSubsequentAnswer) {
  CreateJsepTransportController(JsepTransportController::Config());

  // Local Offer (Full ICE) -> Controlling
  auto local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(local_offer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  EXPECT_TRUE(
      transport_controller_
          ->SetLocalDescription(SdpType::kOffer, local_offer.get(), nullptr)
          .ok());

  auto fake_dtls = GetFakeDtlsTransport(kAudioMid1);
  ASSERT_NE(fake_dtls, nullptr);
  EXPECT_EQ(ICEROLE_CONTROLLING, GetIceRole(fake_dtls));

  // Remote Answer -> Established
  auto remote_answer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_answer.get(), kAudioMid1, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kAnswer, local_offer.get(),
                                         remote_answer.get())
                  .ok());

  // Subsequent Remote Offer -> Local Answer (No ICE Restart)
  auto remote_reoffer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_reoffer.get(), kAudioMid1, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  EXPECT_TRUE(transport_controller_
                  ->SetRemoteDescription(SdpType::kOffer, local_offer.get(),
                                         remote_reoffer.get())
                  .ok());

  auto local_reanswer = std::make_unique<SessionDescription>();
  AddAudioSection(local_reanswer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kAnswer, local_reanswer.get(),
                                        remote_reoffer.get())
                  .ok());

  EXPECT_EQ(ICEROLE_CONTROLLING, GetIceRole(fake_dtls));
}

TEST_F(JsepTransportControllerTest, IceRolePreservedOnSubsequentOffer) {
  CreateJsepTransportController(JsepTransportController::Config());

  // Remote Offer -> Local Answer -> Controlled
  auto remote_offer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_offer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  EXPECT_TRUE(
      transport_controller_
          ->SetRemoteDescription(SdpType::kOffer, nullptr, remote_offer.get())
          .ok());

  auto local_answer = std::make_unique<SessionDescription>();
  AddAudioSection(local_answer.get(), kAudioMid1, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kAnswer, local_answer.get(),
                                        remote_offer.get())
                  .ok());

  auto fake_dtls = GetFakeDtlsTransport(kAudioMid1);
  ASSERT_NE(fake_dtls, nullptr);
  EXPECT_EQ(ICEROLE_CONTROLLED, GetIceRole(fake_dtls));

  // Subsequent Local Offer (No ICE Restart)
  auto local_reoffer = std::make_unique<SessionDescription>();
  AddAudioSection(local_reoffer.get(), kAudioMid1, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kOffer, local_reoffer.get(),
                                        local_answer.get())
                  .ok());

  EXPECT_EQ(ICEROLE_CONTROLLED, GetIceRole(fake_dtls));
}

TEST_F(JsepTransportControllerTest, IceRoleRedeterminedOnIceRestart) {
  CreateJsepTransportController(JsepTransportController::Config());

  // Remote Offer -> Local Answer -> Controlled
  auto remote_offer = std::make_unique<SessionDescription>();
  AddAudioSection(remote_offer.get(), kAudioMid1, kIceUfrag1, kIcePwd1,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  EXPECT_TRUE(
      transport_controller_
          ->SetRemoteDescription(SdpType::kOffer, nullptr, remote_offer.get())
          .ok());

  auto local_answer = std::make_unique<SessionDescription>();
  AddAudioSection(local_answer.get(), kAudioMid1, kIceUfrag2, kIcePwd2,
                  ICEMODE_FULL, CONNECTIONROLE_PASSIVE, nullptr);
  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kAnswer, local_answer.get(),
                                        remote_offer.get())
                  .ok());

  auto fake_dtls = GetFakeDtlsTransport(kAudioMid1);
  ASSERT_NE(fake_dtls, nullptr);
  EXPECT_EQ(ICEROLE_CONTROLLED, GetIceRole(fake_dtls));

  // Subsequent Local Offer with ICE Restart -> Controlling
  auto restart_local_offer = std::make_unique<SessionDescription>();
  AddAudioSection(restart_local_offer.get(), kAudioMid1, kIceUfrag3, kIcePwd3,
                  ICEMODE_FULL, CONNECTIONROLE_ACTPASS, nullptr);
  EXPECT_TRUE(transport_controller_
                  ->SetLocalDescription(SdpType::kOffer,
                                        restart_local_offer.get(),
                                        remote_offer.get())
                  .ok());

  EXPECT_EQ(ICEROLE_CONTROLLING, GetIceRole(fake_dtls));
}

}  // namespace
}  // namespace webrtc
