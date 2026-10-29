/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/enable_media_with_defaults.h"
#include "api/environment/environment.h"
#include "api/ice_transport_interface.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/scoped_refptr.h"
#include "api/units/time_delta.h"
#include "p2p/base/ice_transport_internal.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/transport_info.h"
#include "p2p/test/fake_port_allocator.h"
#include "pc/dtls_transport.h"
#include "pc/media_session.h"
#include "pc/peer_connection.h"
#include "pc/peer_connection_wrapper.h"
#include "pc/rtp_transceiver.h"
#include "pc/session_description.h"
#include "rtc_base/checks.h"
#include "rtc_base/internal/default_socket_server.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/socket_server.h"
#include "rtc_base/thread.h"
#include "test/create_test_environment.h"
#include "test/create_test_field_trials.h"
#include "test/gtest.h"
#include "test/run_loop.h"
#ifdef WEBRTC_ANDROID
#include "pc/test/android_test_initializer.h"
#endif
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/create_peerconnection_factory.h"
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
#include "pc/peer_connection_proxy.h"
#include "pc/test/fake_audio_capture_module.h"
#include "pc/test/mock_peer_connection_observers.h"
#include "rtc_base/fake_network.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/virtual_socket_server.h"
#include "system_wrappers/include/metrics.h"
#include "test/gmock.h"

namespace webrtc {

using RTCConfiguration = PeerConnectionInterface::RTCConfiguration;
using RTCOfferAnswerOptions = PeerConnectionInterface::RTCOfferAnswerOptions;

using ::testing::Combine;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::NotNull;
using ::testing::Pair;
using ::testing::SizeIs;
using ::testing::Values;

constexpr int kIceCandidatesTimeout = 10000;
constexpr int64_t kWaitTimeout = 10000;

class PeerConnectionWrapperForIceTest : public PeerConnectionWrapper {
 public:
  PeerConnectionWrapperForIceTest(
      scoped_refptr<PeerConnectionFactoryInterface> pc_factory,
      scoped_refptr<PeerConnectionInterface> pc,
      std::unique_ptr<MockPeerConnectionObserver> observer,
      FakeNetworkManager* network = nullptr)
      : PeerConnectionWrapper(std::move(pc_factory),
                              std::move(pc),
                              std::move(observer)),
        network_(network) {}

  std::unique_ptr<IceCandidate> CreateJsepCandidateForFirstTransport(
      Candidate* candidate) {
    RTC_DCHECK(pc()->remote_description());
    const auto* desc = pc()->remote_description()->description();
    RTC_DCHECK(!desc->contents().empty());
    const auto& first_content = desc->contents()[0];
    return CreateIceCandidate(first_content.mid(), -1, *candidate);
  }

  // Adds a new ICE candidate to the first transport.
  bool AddIceCandidate(Candidate* candidate) {
    return pc()->AddIceCandidate(
        CreateJsepCandidateForFirstTransport(candidate).get());
  }

  // Returns ICE candidates from the remote session description.
  std::vector<const IceCandidate*> GetIceCandidatesFromRemoteDescription() {
    const SessionDescriptionInterface* sdesc = pc()->remote_description();
    RTC_DCHECK(sdesc);
    std::vector<const IceCandidate*> candidates;
    for (size_t mline_index = 0; mline_index < sdesc->number_of_mediasections();
         mline_index++) {
      const auto* candidate_collection = sdesc->candidates(mline_index);
      for (size_t i = 0; i < candidate_collection->count(); i++) {
        candidates.push_back(candidate_collection->at(i));
      }
    }
    return candidates;
  }

  void AddInterface(const SocketAddress& iface) {
    network_thread()->BlockingCall([&] { network_->AddInterface(iface); });
  }

  void RemoveInterface(const SocketAddress& iface) {
    network_thread()->BlockingCall([&] { network_->RemoveInterface(iface); });
  }

  Thread* network_thread() {
    return GetInternalPeerConnection()->network_thread();
  }
  FakeNetworkManager* network() { return network_; }

 private:
  FakeNetworkManager* const network_;
};

class PeerConnectionIceBaseTest : public ::testing::Test {
 protected:
  typedef std::unique_ptr<PeerConnectionWrapperForIceTest> WrapperPtr;

  explicit PeerConnectionIceBaseTest(SdpSemantics sdp_semantics)
      : env_(CreateTestEnvironment()),
        network_thread_(new Thread(&vss_)),
        sdp_semantics_(sdp_semantics) {
    RTC_CHECK(network_thread_->Start());
#ifdef WEBRTC_ANDROID
    InitializeAndroidObjects();
#endif
  }

  WrapperPtr CreatePeerConnection() {
    return CreatePeerConnection(RTCConfiguration());
  }

  WrapperPtr CreatePeerConnection(absl::string_view field_trials) {
    return CreatePeerConnection(RTCConfiguration(), field_trials);
  }

  WrapperPtr CreatePeerConnection(const RTCConfiguration& config) {
    return CreatePeerConnection(config, "");
  }

  WrapperPtr CreatePeerConnection(const RTCConfiguration& config,
                                  absl::string_view field_trials) {
    PeerConnectionFactoryDependencies pcf_deps;
    pcf_deps.network_thread = network_thread_.get();
    pcf_deps.signaling_thread = Thread::Current();
    pcf_deps.socket_factory = &vss_;
    auto network_manager =
        std::make_unique<FakeNetworkManager>(pcf_deps.network_thread);
    auto* fake_network = network_manager.get();
    pcf_deps.network_manager = std::move(network_manager);
    pcf_deps.adm = FakeAudioCaptureModule::Create();
    pcf_deps.video_encoder_factory =
        std::make_unique<VideoEncoderFactoryTemplate<
            LibvpxVp8EncoderTemplateAdapter, LibvpxVp9EncoderTemplateAdapter,
            OpenH264EncoderTemplateAdapter, LibaomAv1EncoderTemplateAdapter>>();
    pcf_deps.video_decoder_factory =
        std::make_unique<VideoDecoderFactoryTemplate<
            LibvpxVp8DecoderTemplateAdapter, LibvpxVp9DecoderTemplateAdapter,
            OpenH264DecoderTemplateAdapter, Dav1dDecoderTemplateAdapter>>();
    pcf_deps.env = env_;
    EnableMediaWithDefaults(pcf_deps);
    scoped_refptr<PeerConnectionFactoryInterface> pc_factory =
        CreateModularPeerConnectionFactory(std::move(pcf_deps));

    RTCConfiguration modified_config = config;
    modified_config.set_port_allocator_flags(PORTALLOCATOR_DISABLE_TCP |
                                             PORTALLOCATOR_DISABLE_RELAY);
    modified_config.sdp_semantics = sdp_semantics_;
    auto observer = std::make_unique<MockPeerConnectionObserver>();
    PeerConnectionDependencies pc_dependencies(observer.get());
    if (!field_trials.empty()) {
      pc_dependencies.trials = CreateTestFieldTrialsPtr(field_trials);
    }
    auto result = pc_factory->CreatePeerConnectionOrError(
        modified_config, std::move(pc_dependencies));
    if (!result.ok()) {
      return nullptr;
    }

    observer->SetPeerConnectionInterface(result.value().get());
    return std::make_unique<PeerConnectionWrapperForIceTest>(
        std::move(pc_factory), result.MoveValue(), std::move(observer),
        fake_network);
  }

  // Accepts the same arguments as CreatePeerConnection and adds default audio
  // and video tracks.
  template <typename... Args>
  WrapperPtr CreatePeerConnectionWithAudioVideo(Args&&... args) {
    auto wrapper = CreatePeerConnection(std::forward<Args>(args)...);
    if (!wrapper) {
      return nullptr;
    }
    wrapper->AddAudioTrack("a");
    wrapper->AddVideoTrack("v");
    return wrapper;
  }

  Candidate CreateLocalUdpCandidate(const SocketAddress& address) {
    Candidate candidate;
    RTC_DCHECK_EQ(candidate.type(), IceCandidateType::kHost);
    candidate.set_component(ICE_CANDIDATE_COMPONENT_DEFAULT);
    candidate.set_protocol(UDP_PROTOCOL_NAME);
    candidate.set_address(address);
    return candidate;
  }

  // Remove all ICE ufrag/pwd lines from the given session description.
  void RemoveIceUfragPwd(SessionDescriptionInterface* sdesc) {
    SetIceUfragPwd(sdesc, "", "");
  }

  // Sets all ICE ufrag/pwds on the given session description.
  void SetIceUfragPwd(SessionDescriptionInterface* sdesc,
                      const std::string& ufrag,
                      const std::string& pwd) {
    auto* desc = sdesc->description();
    for (const auto& content : desc->contents()) {
      auto* transport_info = desc->GetTransportInfoByName(content.mid());
      transport_info->description.ice_ufrag = ufrag;
      transport_info->description.ice_pwd = pwd;
    }
  }

  // Set ICE mode on the given session description.
  void SetIceMode(SessionDescriptionInterface* sdesc, const IceMode ice_mode) {
    auto* desc = sdesc->description();
    for (const auto& content : desc->contents()) {
      auto* transport_info = desc->GetTransportInfoByName(content.mid());
      transport_info->description.ice_mode = ice_mode;
    }
  }

  TransportDescription* GetFirstTransportDescription(
      SessionDescriptionInterface* sdesc) {
    auto* desc = sdesc->description();
    RTC_DCHECK(!desc->contents().empty());
    auto* transport_info =
        desc->GetTransportInfoByName(desc->contents()[0].mid());
    RTC_DCHECK(transport_info);
    return &transport_info->description;
  }

  const TransportDescription* GetFirstTransportDescription(
      const SessionDescriptionInterface* sdesc) {
    auto* desc = sdesc->description();
    RTC_DCHECK(!desc->contents().empty());
    auto* transport_info =
        desc->GetTransportInfoByName(desc->contents()[0].mid());
    RTC_DCHECK(transport_info);
    return &transport_info->description;
  }

  // TODO(qingsi): Rewrite this method in terms of the standard IceTransport
  // after it is implemented.
  IceRole GetIceRole(const WrapperPtr& pc_wrapper_ptr) {
    auto* pc_proxy =
        static_cast<PeerConnectionProxyWithInternal<PeerConnectionInterface>*>(
            pc_wrapper_ptr->pc());
    PeerConnection* pc = static_cast<PeerConnection*>(pc_proxy->internal());
    for (const auto& transceiver : pc->GetTransceiversInternal()) {
      if (transceiver->media_type() == MediaType::AUDIO) {
        auto dtls_transport =
            pc->transport_controller_s()->LookupDtlsTransportByMid(
                *transceiver->internal()->mid());
        return network_thread_->BlockingCall([&dtls_transport]() {
          return dtls_transport->ice_transport()->internal()->GetIceRole();
        });
      }
    }
    RTC_DCHECK_NOTREACHED();
    return ICEROLE_UNKNOWN;
  }

  // Returns a list of (ufrag, pwd) pairs in the order that they appear in
  // `description`, or the empty list if `description` is null.
  std::vector<std::pair<std::string, std::string>> GetIceCredentials(
      const SessionDescriptionInterface* description) {
    std::vector<std::pair<std::string, std::string>> ice_credentials;
    if (!description)
      return ice_credentials;
    const auto* desc = description->description();
    for (const auto& content_info : desc->contents()) {
      const auto* transport_info =
          desc->GetTransportInfoByName(content_info.mid());
      if (transport_info) {
        ice_credentials.push_back(
            std::make_pair(transport_info->description.ice_ufrag,
                           transport_info->description.ice_pwd));
      }
    }
    return ice_credentials;
  }

  bool AddCandidateToFirstTransport(Candidate* candidate,
                                    SessionDescriptionInterface* sdesc) {
    auto* desc = sdesc->description();
    RTC_DCHECK(!desc->contents().empty());
    const auto& first_content = desc->contents()[0];
    std::unique_ptr<IceCandidate> jsep_candidate =
        CreateIceCandidate(first_content.mid(), 0, *candidate);
    return sdesc->AddCandidate(jsep_candidate.get());
  }

  const Environment env_;
  test::RunLoop main_;
  VirtualSocketServer vss_;
  std::unique_ptr<Thread> network_thread_;
  const SdpSemantics sdp_semantics_;
};

class PeerConnectionIceTest
    : public PeerConnectionIceBaseTest,
      public ::testing::WithParamInterface<SdpSemantics> {
 protected:
  PeerConnectionIceTest() : PeerConnectionIceBaseTest(GetParam()) {
    metrics::Reset();
  }
};

::testing::AssertionResult AssertCandidatesEqual(const char* a_expr,
                                                 const char* b_expr,
                                                 const Candidate& a,
                                                 const Candidate& b) {
  StringBuilder failure_info;
  if (a.component() != b.component()) {
    failure_info << "\ncomponent: " << a.component() << " != " << b.component();
  }
  if (a.protocol() != b.protocol()) {
    failure_info << "\nprotocol: " << a.protocol() << " != " << b.protocol();
  }
  if (a.address() != b.address()) {
    failure_info << "\naddress: " << a.address().ToString()
                 << " != " << b.address().ToString();
  }
  if (a.type() != b.type()) {
    failure_info << "\ntype: " << a.type_name() << " != " << b.type_name();
  }
  std::string failure_info_str = failure_info.str();
  if (failure_info_str.empty()) {
    return ::testing::AssertionSuccess();
  } else {
    return ::testing::AssertionFailure()
           << a_expr << " and " << b_expr << " are not equal"
           << failure_info_str;
  }
}

TEST_P(PeerConnectionIceTest, OfferContainsGatheredCandidates) {
  const SocketAddress kLocalAddress("1.1.1.1", 0);

  auto caller = CreatePeerConnectionWithAudioVideo();
  caller->AddInterface(kLocalAddress);

  // Start ICE candidate gathering by setting the local offer.
  ASSERT_TRUE(caller->SetLocalDescription(caller->CreateOffer()));

  EXPECT_TRUE(caller->RunUntilIceGatheringDone(main_));

  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  EXPECT_LT(0u, caller->observer()->GetCandidatesByMline(0).size());
  EXPECT_EQ(caller->observer()->GetCandidatesByMline(0).size(),
            offer->candidates(0)->count());
  EXPECT_LT(0u, caller->observer()->GetCandidatesByMline(1).size());
  EXPECT_EQ(caller->observer()->GetCandidatesByMline(1).size(),
            offer->candidates(1)->count());
}

TEST_P(PeerConnectionIceTest, AnswerContainsGatheredCandidates) {
  const SocketAddress kCallerAddress("1.1.1.1", 0);

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();
  caller->AddInterface(kCallerAddress);

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));
  ASSERT_TRUE(callee->SetLocalDescription(callee->CreateAnswer()));

  EXPECT_TRUE(callee->RunUntilIceGatheringDone(main_));

  auto* answer = callee->pc()->local_description();
  EXPECT_LT(0u, caller->observer()->GetCandidatesByMline(0).size());
  EXPECT_EQ(callee->observer()->GetCandidatesByMline(0).size(),
            answer->candidates(0)->count());
  EXPECT_LT(0u, caller->observer()->GetCandidatesByMline(1).size());
  EXPECT_EQ(callee->observer()->GetCandidatesByMline(1).size(),
            answer->candidates(1)->count());
}

TEST_P(PeerConnectionIceTest,
       CanSetRemoteSessionDescriptionWithRemoteCandidates) {
  const SocketAddress kCallerAddress("1.1.1.1", 1111);

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOfferAndSetAsLocal();
  Candidate candidate = CreateLocalUdpCandidate(kCallerAddress);
  AddCandidateToFirstTransport(&candidate, offer.get());

  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));
  auto remote_candidates = callee->GetIceCandidatesFromRemoteDescription();
  ASSERT_EQ(1u, remote_candidates.size());
  EXPECT_PRED_FORMAT2(AssertCandidatesEqual, candidate,
                      remote_candidates[0]->candidate());
}

TEST_P(PeerConnectionIceTest, SetLocalDescriptionFailsIfNoIceCredentials) {
  auto caller = CreatePeerConnectionWithAudioVideo();

  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  RemoveIceUfragPwd(offer.get());

  EXPECT_FALSE(caller->SetLocalDescription(std::move(offer)));
}

TEST_P(PeerConnectionIceTest, SetRemoteDescriptionFailsIfNoIceCredentials) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOfferAndSetAsLocal();
  RemoveIceUfragPwd(offer.get());

  EXPECT_FALSE(callee->SetRemoteDescription(std::move(offer)));
}

// Test that doing an offer/answer exchange with no transport (i.e., no data
// channel or media) results in the ICE connection state staying at New.
TEST_P(PeerConnectionIceTest,
       OfferAnswerWithNoTransportsDoesNotChangeIceConnectionState) {
  auto caller = CreatePeerConnection();
  auto callee = CreatePeerConnection();

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));

  EXPECT_EQ(PeerConnectionInterface::kIceConnectionNew,
            caller->pc()->ice_connection_state());
  EXPECT_EQ(PeerConnectionInterface::kIceConnectionNew,
            callee->pc()->ice_connection_state());
}

// The following group tests that ICE candidates are not generated before
// SetLocalDescription is called on a PeerConnection.

TEST_P(PeerConnectionIceTest, NoIceCandidatesBeforeSetLocalDescription) {
  const SocketAddress kLocalAddress("1.1.1.1", 0);

  auto caller = CreatePeerConnectionWithAudioVideo();
  caller->AddInterface(kLocalAddress);

  // Pump for 1 second and verify that no candidates are generated.
  main_.RunFor(TimeDelta::Millis(1000));

  EXPECT_EQ(0u, caller->observer()->candidates_.size());
}
TEST_P(PeerConnectionIceTest,
       NoIceCandidatesBeforeAnswerSetAsLocalDescription) {
  const SocketAddress kCallerAddress("1.1.1.1", 1111);

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();
  caller->AddInterface(kCallerAddress);

  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOfferAndSetAsLocal();
  Candidate candidate = CreateLocalUdpCandidate(kCallerAddress);
  AddCandidateToFirstTransport(&candidate, offer.get());
  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));

  // Pump for 1 second and verify that no candidates are generated.
  main_.RunFor(TimeDelta::Millis(1000));

  EXPECT_EQ(0u, callee->observer()->candidates_.size());
}

TEST_P(PeerConnectionIceTest, CannotAddCandidateWhenRemoteDescriptionNotSet) {
  const SocketAddress kCalleeAddress("1.1.1.1", 1111);

  auto caller = CreatePeerConnectionWithAudioVideo();
  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  ASSERT_THAT(offer, NotNull());
  ASSERT_THAT(offer->description()->contents(), SizeIs(2));
  Candidate candidate = CreateLocalUdpCandidate(kCalleeAddress);
  std::unique_ptr<IceCandidate> jsep_candidate = CreateIceCandidate(
      offer->description()->contents()[0].mid(), 0, candidate);

  EXPECT_FALSE(caller->pc()->AddIceCandidate(jsep_candidate.get()));

  caller->SetLocalDescription(std::move(offer));

  EXPECT_FALSE(caller->pc()->AddIceCandidate(jsep_candidate.get()));
}

TEST_P(PeerConnectionIceTest, CannotAddCandidateWhenPeerConnectionClosed) {
  const SocketAddress kCalleeAddress("1.1.1.1", 1111);

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));

  Candidate candidate = CreateLocalUdpCandidate(kCalleeAddress);
  auto* audio_content =
      GetFirstAudioContent(caller->pc()->local_description()->description());
  std::unique_ptr<IceCandidate> jsep_candidate =
      CreateIceCandidate(audio_content->mid(), 0, candidate);

  caller->pc()->Close();

  EXPECT_FALSE(caller->pc()->AddIceCandidate(jsep_candidate.get()));
}

TEST_P(PeerConnectionIceTest, DuplicateIceCandidateIgnoredWhenAdded) {
  const SocketAddress kCalleeAddress("1.1.1.1", 1111);

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));
  ASSERT_TRUE(
      caller->SetRemoteDescription(callee->CreateAnswerAndSetAsLocal()));

  Candidate candidate = CreateLocalUdpCandidate(kCalleeAddress);
  caller->AddIceCandidate(&candidate);
  EXPECT_TRUE(caller->AddIceCandidate(&candidate));
  EXPECT_EQ(1u, caller->GetIceCandidatesFromRemoteDescription().size());
}

// TODO(tommi): Re-enable after updating RTCPeerConnection-blockedPorts.html in
// Chromium (the test needs setRemoteDescription to succeed for an invalid
// candidate).
TEST_P(PeerConnectionIceTest, DISABLED_ErrorOnInvalidRemoteIceCandidateAdded) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();
  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));
  // Add a candidate to the remote description with a candidate that has an
  // invalid address (port number == 2).
  std::unique_ptr<SessionDescriptionInterface> answer =
      callee->CreateAnswerAndSetAsLocal();
  Candidate bad_candidate =
      CreateLocalUdpCandidate(SocketAddress("2.2.2.2", 2));
  RTC_LOG(LS_INFO) << "Bad candidate: " << bad_candidate.ToString();
  AddCandidateToFirstTransport(&bad_candidate, answer.get());
  // Now the call to SetRemoteDescription should fail.
  EXPECT_FALSE(caller->SetRemoteDescription(std::move(answer)));
}

TEST_P(PeerConnectionIceTest,
       CannotRemoveIceCandidatesWhenPeerConnectionClosed) {
  const SocketAddress kCalleeAddress("1.1.1.1", 1111);

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));

  Candidate candidate = CreateLocalUdpCandidate(kCalleeAddress);
  auto* audio_content =
      GetFirstAudioContent(caller->pc()->local_description()->description());
  std::unique_ptr<IceCandidate> ice_candidate =
      CreateIceCandidate(audio_content->mid(), 0, candidate);

  ASSERT_TRUE(caller->pc()->AddIceCandidate(ice_candidate.get()));

  caller->pc()->Close();
  EXPECT_FALSE(caller->pc()->RemoveIceCandidate(ice_candidate.get()));
}

TEST_P(PeerConnectionIceTest,
       AddRemoveCandidateWithEmptyTransportDoesNotCrash) {
  const SocketAddress kCalleeAddress("1.1.1.1", 1111);

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));
  ASSERT_TRUE(
      caller->SetRemoteDescription(callee->CreateAnswerAndSetAsLocal()));

  // `candidate.transport_name()` is empty.
  Candidate candidate = CreateLocalUdpCandidate(kCalleeAddress);
  auto* audio_content =
      GetFirstAudioContent(caller->pc()->local_description()->description());
  std::unique_ptr<IceCandidate> ice_candidate =
      CreateIceCandidate(audio_content->mid(), 0, candidate);
  EXPECT_TRUE(caller->pc()->AddIceCandidate(ice_candidate.get()));
  EXPECT_TRUE(caller->pc()->RemoveIceCandidate(ice_candidate.get()));
}

TEST_P(PeerConnectionIceTest, RemoveCandidateRemovesFromRemoteDescription) {
  const SocketAddress kCalleeAddress("1.1.1.1", 1111);

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));
  ASSERT_TRUE(
      caller->SetRemoteDescription(callee->CreateAnswerAndSetAsLocal()));

  Candidate candidate = CreateLocalUdpCandidate(kCalleeAddress);
  std::unique_ptr<IceCandidate> ice_candidate =
      caller->CreateJsepCandidateForFirstTransport(&candidate);

  ASSERT_TRUE(caller->pc()->AddIceCandidate(ice_candidate.get()));
  EXPECT_TRUE(caller->pc()->RemoveIceCandidate(ice_candidate.get()));
  EXPECT_EQ(0u, caller->GetIceCandidatesFromRemoteDescription().size());
}

// Test that if a candidate is added via AddIceCandidate and via an updated
// remote description, then both candidates appear in the stored remote
// description.
TEST_P(PeerConnectionIceTest,
       CandidateInSubsequentOfferIsAddedToRemoteDescription) {
  const SocketAddress kCallerAddress1("1.1.1.1", 1111);
  const SocketAddress kCallerAddress2("2.2.2.2", 2222);

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));
  ASSERT_TRUE(
      caller->SetRemoteDescription(callee->CreateAnswerAndSetAsLocal()));

  // Add one candidate via `AddIceCandidate`.
  Candidate candidate1 = CreateLocalUdpCandidate(kCallerAddress1);
  ASSERT_TRUE(callee->AddIceCandidate(&candidate1));

  // Add the second candidate via a reoffer.
  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  Candidate candidate2 = CreateLocalUdpCandidate(kCallerAddress2);
  AddCandidateToFirstTransport(&candidate2, offer.get());

  // Expect both candidates to appear in the callee's remote description.
  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));
  EXPECT_THAT(callee->GetIceCandidatesFromRemoteDescription(), SizeIs(2));
}

// The follow test verifies that SetLocal/RemoteDescription fails when an offer
// has either ICE ufrag/pwd too short or too long and succeeds otherwise.
// The standard (https://tools.ietf.org/html/rfc5245#section-15.4) says that
// pwd must be 22-256 characters and ufrag must be 4-256 characters.
TEST_P(PeerConnectionIceTest, VerifyUfragPwdLength) {
  auto set_local_description_with_ufrag_pwd_length = [this](int ufrag_len,
                                                            int pwd_len) {
    auto pc = CreatePeerConnectionWithAudioVideo();
    // Because local munging is forbidden by spec, we have to disable the
    // check for it.
    pc->GetInternalPeerConnection()->DisableSdpMungingChecksForTesting();
    std::unique_ptr<SessionDescriptionInterface> offer = pc->CreateOffer();
    SetIceUfragPwd(offer.get(), std::string(ufrag_len, 'x'),
                   std::string(pwd_len, 'x'));
    bool result = pc->SetLocalDescription(std::move(offer));
    pc->pc()->Close();
    return result;
  };

  auto set_remote_description_with_ufrag_pwd_length = [this](int ufrag_len,
                                                             int pwd_len) {
    auto pc = CreatePeerConnectionWithAudioVideo();
    std::unique_ptr<SessionDescriptionInterface> offer = pc->CreateOffer();
    SetIceUfragPwd(offer.get(), std::string(ufrag_len, 'x'),
                   std::string(pwd_len, 'x'));
    bool result = pc->SetRemoteDescription(std::move(offer));
    pc->pc()->Close();
    return result;
  };

  EXPECT_FALSE(set_local_description_with_ufrag_pwd_length(3, 22));
  EXPECT_FALSE(set_remote_description_with_ufrag_pwd_length(3, 22));
  EXPECT_FALSE(set_local_description_with_ufrag_pwd_length(257, 22));
  EXPECT_FALSE(set_remote_description_with_ufrag_pwd_length(257, 22));
  EXPECT_FALSE(set_local_description_with_ufrag_pwd_length(4, 21));
  EXPECT_FALSE(set_remote_description_with_ufrag_pwd_length(4, 21));
  EXPECT_FALSE(set_local_description_with_ufrag_pwd_length(4, 257));
  EXPECT_FALSE(set_remote_description_with_ufrag_pwd_length(4, 257));
  EXPECT_TRUE(set_local_description_with_ufrag_pwd_length(4, 22));
  EXPECT_TRUE(set_remote_description_with_ufrag_pwd_length(4, 22));
  EXPECT_TRUE(set_local_description_with_ufrag_pwd_length(256, 256));
  EXPECT_TRUE(set_remote_description_with_ufrag_pwd_length(256, 256));
}

::testing::AssertionResult AssertIpInCandidates(
    const char* address_expr,
    const char* candidates_expr,
    const SocketAddress& address,
    const std::vector<IceCandidate*> candidates) {
  StringBuilder candidate_hosts;
  for (const auto* candidate : candidates) {
    const auto& candidate_ip = candidate->candidate().address().ipaddr();
    if (candidate_ip == address.ipaddr()) {
      return ::testing::AssertionSuccess();
    }
    candidate_hosts << "\n" << candidate_ip.ToString();
  }
  return ::testing::AssertionFailure()
         << address_expr << " (host " << address.HostAsURIString()
         << ") not in " << candidates_expr
         << " which have the following address hosts:" << candidate_hosts.str();
}

TEST_P(PeerConnectionIceTest, CandidatesGeneratedForEachLocalInterface) {
  const SocketAddress kLocalAddress1("1.1.1.1", 0);
  const SocketAddress kLocalAddress2("2.2.2.2", 0);

  auto caller = CreatePeerConnectionWithAudioVideo();
  caller->AddInterface(kLocalAddress1);
  caller->AddInterface(kLocalAddress2);

  caller->CreateOfferAndSetAsLocal();
  EXPECT_TRUE(caller->RunUntilIceGatheringDone(main_));

  auto candidates = caller->observer()->GetCandidatesByMline(0);
  EXPECT_PRED_FORMAT2(AssertIpInCandidates, kLocalAddress1, candidates);
  EXPECT_PRED_FORMAT2(AssertIpInCandidates, kLocalAddress2, candidates);
}

TEST_P(PeerConnectionIceTest, TrickledSingleCandidateAddedToRemoteDescription) {
  const SocketAddress kCallerAddress("1.1.1.1", 1111);

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));

  Candidate candidate = CreateLocalUdpCandidate(kCallerAddress);
  callee->AddIceCandidate(&candidate);
  auto candidates = callee->GetIceCandidatesFromRemoteDescription();
  ASSERT_EQ(1u, candidates.size());
  EXPECT_PRED_FORMAT2(AssertCandidatesEqual, candidate,
                      candidates[0]->candidate());
}

TEST_P(PeerConnectionIceTest, TwoTrickledCandidatesAddedToRemoteDescription) {
  const SocketAddress kCalleeAddress1("1.1.1.1", 1111);
  const SocketAddress kCalleeAddress2("2.2.2.2", 2222);

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));
  ASSERT_TRUE(
      caller->SetRemoteDescription(callee->CreateAnswerAndSetAsLocal()));

  Candidate candidate1 = CreateLocalUdpCandidate(kCalleeAddress1);
  caller->AddIceCandidate(&candidate1);

  Candidate candidate2 = CreateLocalUdpCandidate(kCalleeAddress2);
  caller->AddIceCandidate(&candidate2);

  auto candidates = caller->GetIceCandidatesFromRemoteDescription();
  ASSERT_THAT(candidates, SizeIs(2));
  EXPECT_PRED_FORMAT2(AssertCandidatesEqual, candidate1,
                      candidates[0]->candidate());
  EXPECT_PRED_FORMAT2(AssertCandidatesEqual, candidate2,
                      candidates[1]->candidate());
}

TEST_P(PeerConnectionIceTest, AsyncAddIceCandidateIsAddedToRemoteDescription) {
  auto candidate = CreateLocalUdpCandidate(SocketAddress("1.1.1.1", 1111));

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));

  auto jsep_candidate =
      callee->CreateJsepCandidateForFirstTransport(&candidate);
  bool operation_completed = false;
  callee->pc()->AddIceCandidate(
      std::move(jsep_candidate),
      [&operation_completed, &run_loop = main_](RTCError result) {
        EXPECT_TRUE(result.ok());
        operation_completed = true;
        run_loop.Quit();
      });
  main_.RunFor(TimeDelta::Millis(kWaitTimeout));
  EXPECT_TRUE(operation_completed);

  auto candidates = callee->GetIceCandidatesFromRemoteDescription();
  ASSERT_EQ(1u, candidates.size());
  EXPECT_PRED_FORMAT2(AssertCandidatesEqual, candidate,
                      candidates[0]->candidate());
}

TEST_P(PeerConnectionIceTest,
       AsyncAddIceCandidateCompletesImmediatelyIfNoPendingOperation) {
  auto candidate = CreateLocalUdpCandidate(SocketAddress("1.1.1.1", 1111));

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));

  auto jsep_candidate =
      callee->CreateJsepCandidateForFirstTransport(&candidate);
  bool operation_completed = false;
  callee->pc()->AddIceCandidate(std::move(jsep_candidate),
                                [&operation_completed](RTCError result) {
                                  EXPECT_TRUE(result.ok());
                                  operation_completed = true;
                                });
  EXPECT_TRUE(operation_completed);
}

TEST_P(PeerConnectionIceTest,
       AsyncAddIceCandidateCompletesWhenPendingOperationCompletes) {
  auto candidate = CreateLocalUdpCandidate(SocketAddress("1.1.1.1", 1111));

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));

  // Chain an operation that will block AddIceCandidate() from executing.
  auto answer_observer = make_ref_counted<MockCreateSessionDescriptionObserver>(
      main_.QuitClosure());
  callee->pc()->CreateAnswer(answer_observer.get(), RTCOfferAnswerOptions());

  auto jsep_candidate =
      callee->CreateJsepCandidateForFirstTransport(&candidate);
  bool operation_completed = false;
  callee->pc()->AddIceCandidate(
      std::move(jsep_candidate),
      [&operation_completed](RTCError result) { operation_completed = true; });
  // The operation will not be able to complete until we let the run loop
  // process CreateAnswer().
  EXPECT_FALSE(operation_completed);
  main_.Run();
  EXPECT_TRUE(answer_observer->called());
  // As soon as it does, AddIceCandidate() will execute without delay, so it
  // must also have completed.
  EXPECT_TRUE(operation_completed);
}

TEST_P(PeerConnectionIceTest, AsyncAddIceCandidateCompletesInOrder) {
  auto candidate = CreateLocalUdpCandidate(SocketAddress("1.1.1.1", 1111));

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));

  // Chain an operation that will block both AddIceCandidate() calls, so that
  // they are queued behind it rather than executing immediately.
  auto answer_observer = make_ref_counted<MockCreateSessionDescriptionObserver>(
      main_.QuitClosure());
  callee->pc()->CreateAnswer(answer_observer.get(), RTCOfferAnswerOptions());

  std::vector<int> completion_order;
  for (int i = 0; i < 2; ++i) {
    callee->pc()->AddIceCandidate(
        callee->CreateJsepCandidateForFirstTransport(&candidate),
        [&completion_order, i](RTCError result) {
          completion_order.push_back(i);
        });
  }
  ASSERT_THAT(completion_order, IsEmpty());

  main_.Run();
  // The operations chain runs them in order, so they must also complete in
  // order.
  EXPECT_THAT(completion_order, ElementsAre(0, 1));
}

TEST_P(PeerConnectionIceTest,
       AsyncAddIceCandidateWaitsForResultDeliveryBeforeStartingNextOperation) {
  auto candidate = CreateLocalUdpCandidate(SocketAddress("1.1.1.1", 1111));

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();
  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));

  absl::AnyInvocable<void() &&> operation_complete_callback;
  callee->pc()->AddIceCandidate(
      callee->CreateJsepCandidateForFirstTransport(&candidate),
      [&operation_complete_callback](
          RTCError result, absl::AnyInvocable<void() &&> complete_callback) {
        EXPECT_TRUE(result.ok());
        operation_complete_callback = std::move(complete_callback);
      });

  ASSERT_TRUE(operation_complete_callback);
  auto queued_observer = make_ref_counted<MockCreateSessionDescriptionObserver>(
      main_.QuitClosure());
  callee->pc()->CreateOffer(queued_observer.get(), RTCOfferAnswerOptions());
  EXPECT_FALSE(queued_observer->called());

  std::move(operation_complete_callback)();
  main_.Run();
  EXPECT_TRUE(queued_observer->called());
}

TEST_P(PeerConnectionIceTest,
       AsyncAddIceCandidateFailsBeforeSetRemoteDescription) {
  auto candidate = CreateLocalUdpCandidate(SocketAddress("1.1.1.1", 1111));

  auto caller = CreatePeerConnectionWithAudioVideo();
  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  ASSERT_THAT(offer, NotNull());
  ASSERT_THAT(offer->description()->contents(), SizeIs(2));
  std::unique_ptr<IceCandidate> jsep_candidate = CreateIceCandidate(
      offer->description()->contents()[0].mid(), 0, candidate);

  bool operation_completed = false;
  caller->pc()->AddIceCandidate(
      std::move(jsep_candidate),
      [&operation_completed, &run_loop = main_](RTCError result) {
        EXPECT_FALSE(result.ok());
        EXPECT_EQ(result.message(),
                  std::string("The remote description was null"));
        operation_completed = true;
        run_loop.Quit();
      });
  main_.RunFor(TimeDelta::Millis(kWaitTimeout));
  EXPECT_TRUE(operation_completed);
}

TEST_P(PeerConnectionIceTest,
       AsyncAddIceCandidateFailsIfPeerConnectionDestroyed) {
  auto candidate = CreateLocalUdpCandidate(SocketAddress("1.1.1.1", 1111));

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));

  // Chain an operation that will block AddIceCandidate() from executing.
  auto answer_observer = make_ref_counted<MockCreateSessionDescriptionObserver>(
      main_.QuitClosure());
  callee->pc()->CreateAnswer(answer_observer.get(), RTCOfferAnswerOptions());

  auto jsep_candidate =
      callee->CreateJsepCandidateForFirstTransport(&candidate);
  bool operation_completed = false;
  callee->pc()->AddIceCandidate(
      std::move(jsep_candidate), [&operation_completed](RTCError result) {
        EXPECT_FALSE(result.ok());
        EXPECT_EQ(
            result.message(),
            std::string(
                "AddIceCandidate failed because the session was shut down"));
        operation_completed = true;
      });
  // The operation will not be able to run until we let the run loop
  // process CreateAnswer(), giving us time to remove all references to the
  // PeerConnection.
  EXPECT_FALSE(operation_completed);
  // This should delete the callee PC.
  callee = nullptr;
  main_.RunFor(TimeDelta::Millis(kWaitTimeout));
  EXPECT_TRUE(answer_observer->called());
  EXPECT_TRUE(operation_completed);
}

TEST_P(PeerConnectionIceTest, LocalDescriptionUpdatedWhenContinualGathering) {
  const SocketAddress kLocalAddress("1.1.1.1", 0);

  RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  config.continual_gathering_policy =
      PeerConnectionInterface::GATHER_CONTINUALLY;
  auto caller = CreatePeerConnectionWithAudioVideo(config);
  caller->AddInterface(kLocalAddress);

  // Start ICE candidate gathering by setting the local offer.
  caller->observer()->SetOnIceCandidateCallback(main_.QuitClosure());
  ASSERT_TRUE(caller->SetLocalDescription(caller->CreateOffer()));

  // Since we're using continual gathering, we won't get "gathering done".
  main_.RunFor(TimeDelta::Millis(kIceCandidatesTimeout));
  caller->observer()->SetOnIceCandidateCallback(nullptr);
  EXPECT_GT(caller->pc()->local_description()->candidates(0)->count(), 0u);
}

// Test that when continual gathering is enabled, and a network interface goes
// down, the candidate is signaled as removed and removed from the local
// description.
TEST_P(PeerConnectionIceTest,
       LocalCandidatesRemovedWhenNetworkDownIfGatheringContinually) {
  const SocketAddress kLocalAddress("1.1.1.1", 0);

  RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  config.continual_gathering_policy =
      PeerConnectionInterface::GATHER_CONTINUALLY;
  auto caller = CreatePeerConnectionWithAudioVideo(config);
  caller->AddInterface(kLocalAddress);

  // Start ICE candidate gathering by setting the local offer.
  caller->observer()->SetOnIceCandidateCallback(main_.QuitClosure());
  ASSERT_TRUE(caller->SetLocalDescription(caller->CreateOffer()));
  main_.RunFor(TimeDelta::Millis(kIceCandidatesTimeout));
  caller->observer()->SetOnIceCandidateCallback(nullptr);
  EXPECT_GT(caller->pc()->local_description()->candidates(0)->count(), 0u);

  // Remove the only network interface, causing the PeerConnection to signal
  // the removal of all candidates derived from this interface.
  caller->RemoveInterface(kLocalAddress);

  if (caller->pc()->local_description()->candidates(0)->count() != 0) {
    caller->observer()->SetOnIceCandidateRemovedCallback(main_.QuitClosure());
    main_.RunFor(TimeDelta::Millis(kIceCandidatesTimeout));
    caller->observer()->SetOnIceCandidateRemovedCallback(nullptr);
  }
  EXPECT_EQ(caller->pc()->local_description()->candidates(0)->count(), 0u);
  EXPECT_LT(0, caller->observer()->num_candidates_removed_);
}

TEST_P(PeerConnectionIceTest,
       LocalCandidatesNotRemovedWhenNetworkDownIfGatheringOnce) {
  const SocketAddress kLocalAddress("1.1.1.1", 0);

  RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  config.continual_gathering_policy = PeerConnectionInterface::GATHER_ONCE;
  auto caller = CreatePeerConnectionWithAudioVideo(config);
  caller->AddInterface(kLocalAddress);

  // Start ICE candidate gathering by setting the local offer.
  ASSERT_TRUE(caller->SetLocalDescription(caller->CreateOffer()));

  EXPECT_TRUE(caller->RunUntilIceGatheringDone(main_));

  caller->RemoveInterface(kLocalAddress);

  // Verify that the local candidates are not removed;
  main_.RunFor(TimeDelta::Millis(1000));
  EXPECT_EQ(0, caller->observer()->num_candidates_removed_);
}

// The following group tests that when an offer includes a new ufrag or pwd
// (indicating an ICE restart) the old candidates are removed and new candidates
// added to the remote description.

TEST_P(PeerConnectionIceTest, IceRestartOfferClearsExistingCandidate) {
  const SocketAddress kCallerAddress("1.1.1.1", 1111);

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOfferAndSetAsLocal();
  Candidate candidate = CreateLocalUdpCandidate(kCallerAddress);
  AddCandidateToFirstTransport(&candidate, offer.get());

  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));

  RTCOfferAnswerOptions options;
  options.ice_restart = true;
  ASSERT_TRUE(
      callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal(options)));

  EXPECT_EQ(0u, callee->GetIceCandidatesFromRemoteDescription().size());
}
TEST_P(PeerConnectionIceTest,
       IceRestartOfferCandidateReplacesExistingCandidate) {
  const SocketAddress kFirstCallerAddress("1.1.1.1", 1111);
  const SocketAddress kRestartedCallerAddress("2.2.2.2", 2222);

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOfferAndSetAsLocal();
  Candidate old_candidate = CreateLocalUdpCandidate(kFirstCallerAddress);
  AddCandidateToFirstTransport(&old_candidate, offer.get());

  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));

  RTCOfferAnswerOptions options;
  options.ice_restart = true;
  auto restart_offer = caller->CreateOfferAndSetAsLocal(options);
  Candidate new_candidate = CreateLocalUdpCandidate(kRestartedCallerAddress);
  AddCandidateToFirstTransport(&new_candidate, restart_offer.get());

  ASSERT_TRUE(callee->SetRemoteDescription(std::move(restart_offer)));

  auto remote_candidates = callee->GetIceCandidatesFromRemoteDescription();
  ASSERT_EQ(1u, remote_candidates.size());
  EXPECT_PRED_FORMAT2(AssertCandidatesEqual, new_candidate,
                      remote_candidates[0]->candidate());
}

// Test that if there is not an ICE restart (i.e., nothing changes), then the
// answer to a later offer should have the same ufrag/pwd as the first answer.
TEST_P(PeerConnectionIceTest, LaterAnswerHasSameIceCredentialsIfNoIceRestart) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));
  ASSERT_TRUE(
      caller->SetRemoteDescription(callee->CreateAnswerAndSetAsLocal()));

  // Re-offer.
  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));

  std::unique_ptr<SessionDescriptionInterface> answer = callee->CreateAnswer();
  auto* answer_transport_desc = GetFirstTransportDescription(answer.get());
  auto* local_transport_desc =
      GetFirstTransportDescription(callee->pc()->local_description());

  EXPECT_EQ(answer_transport_desc->ice_ufrag, local_transport_desc->ice_ufrag);
  EXPECT_EQ(answer_transport_desc->ice_pwd, local_transport_desc->ice_pwd);
}

TEST_P(PeerConnectionIceTest, RestartIceGeneratesNewCredentials) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));
  auto initial_ice_credentials =
      GetIceCredentials(caller->pc()->local_description());
  caller->pc()->RestartIce();
  ASSERT_TRUE(caller->CreateOfferAndSetAsLocal());
  auto restarted_ice_credentials =
      GetIceCredentials(caller->pc()->local_description());
  EXPECT_NE(initial_ice_credentials, restarted_ice_credentials);
}

TEST_P(PeerConnectionIceTest,
       RestartIceWhileLocalOfferIsPendingGeneratesNewCredentialsInNextOffer) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));
  auto initial_ice_credentials =
      GetIceCredentials(caller->pc()->local_description());
  // ICE restart becomes needed while an O/A is pending and `caller` is the
  // offerer.
  caller->pc()->RestartIce();
  ASSERT_TRUE(
      caller->SetRemoteDescription(callee->CreateAnswerAndSetAsLocal()));
  ASSERT_TRUE(caller->CreateOfferAndSetAsLocal());
  auto restarted_ice_credentials =
      GetIceCredentials(caller->pc()->local_description());
  EXPECT_NE(initial_ice_credentials, restarted_ice_credentials);
}

TEST_P(PeerConnectionIceTest,
       RestartIceWhileRemoteOfferIsPendingGeneratesNewCredentialsInNextOffer) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));
  auto initial_ice_credentials =
      GetIceCredentials(caller->pc()->local_description());
  ASSERT_TRUE(caller->SetRemoteDescription(callee->CreateOfferAndSetAsLocal()));
  // ICE restart becomes needed while an O/A is pending and `caller` is the
  // answerer.
  caller->pc()->RestartIce();
  ASSERT_TRUE(
      callee->SetRemoteDescription(caller->CreateAnswerAndSetAsLocal()));
  ASSERT_TRUE(caller->CreateOfferAndSetAsLocal());
  auto restarted_ice_credentials =
      GetIceCredentials(caller->pc()->local_description());
  EXPECT_NE(initial_ice_credentials, restarted_ice_credentials);
}

TEST_P(PeerConnectionIceTest, RestartIceTriggeredByRemoteSide) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));
  auto initial_ice_credentials =
      GetIceCredentials(caller->pc()->local_description());

  // Remote restart and O/A exchange with `caller` as the answerer should
  // restart ICE locally as well.
  callee->pc()->RestartIce();
  ASSERT_TRUE(callee->ExchangeOfferAnswerWith(caller.get()));

  auto restarted_ice_credentials =
      GetIceCredentials(caller->pc()->local_description());
  EXPECT_NE(initial_ice_credentials, restarted_ice_credentials);
}

TEST_P(PeerConnectionIceTest, RestartIceCausesNegotiationNeeded) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));
  caller->observer()->clear_legacy_renegotiation_needed();
  caller->observer()->clear_latest_negotiation_needed_event();
  caller->pc()->RestartIce();
  EXPECT_TRUE(caller->observer()->legacy_renegotiation_needed());
  EXPECT_TRUE(caller->observer()->has_negotiation_needed_event());
}

// In Unified Plan, "onnegotiationneeded" is spec-compliant, including not
// firing multipe times in a row, or firing when returning to the stable
// signaling state if negotiation is still needed. In Plan B it fires any time
// something changes. As such, some tests are SdpSemantics-specific.
class PeerConnectionIceTestUnifiedPlan : public PeerConnectionIceBaseTest {
 protected:
  PeerConnectionIceTestUnifiedPlan()
      : PeerConnectionIceBaseTest(SdpSemantics::kUnifiedPlan) {}
};

TEST_F(PeerConnectionIceTestUnifiedPlan,
       RestartIceWhileLocalOfferIsPendingCausesNegotiationNeededWhenStable) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));
  // ICE restart becomes needed while an O/A is pending and `caller` is the
  // offerer.
  caller->observer()->clear_legacy_renegotiation_needed();
  caller->observer()->clear_latest_negotiation_needed_event();
  caller->pc()->RestartIce();
  // In Unified Plan, the event should not fire until we are back in the stable
  // signaling state.
  EXPECT_FALSE(caller->observer()->legacy_renegotiation_needed());
  EXPECT_FALSE(caller->observer()->has_negotiation_needed_event());
  ASSERT_TRUE(
      caller->SetRemoteDescription(callee->CreateAnswerAndSetAsLocal()));
  EXPECT_TRUE(caller->observer()->legacy_renegotiation_needed());
  EXPECT_TRUE(caller->observer()->has_negotiation_needed_event());
}

TEST_F(PeerConnectionIceTestUnifiedPlan,
       RestartIceWhileRemoteOfferIsPendingCausesNegotiationNeededWhenStable) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  // Establish initial credentials as the caller.
  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));
  ASSERT_TRUE(caller->SetRemoteDescription(callee->CreateOfferAndSetAsLocal()));
  // ICE restart becomes needed while an O/A is pending and `caller` is the
  // answerer.
  caller->observer()->clear_legacy_renegotiation_needed();
  caller->observer()->clear_latest_negotiation_needed_event();
  caller->pc()->RestartIce();
  // In Unified Plan, the event should not fire until we are back in the stable
  // signaling state.
  EXPECT_FALSE(caller->observer()->legacy_renegotiation_needed());
  EXPECT_FALSE(caller->observer()->has_negotiation_needed_event());
  ASSERT_TRUE(
      callee->SetRemoteDescription(caller->CreateAnswerAndSetAsLocal()));
  EXPECT_TRUE(caller->observer()->legacy_renegotiation_needed());
  EXPECT_TRUE(caller->observer()->has_negotiation_needed_event());
}

TEST_F(PeerConnectionIceTestUnifiedPlan,
       RestartIceTriggeredByRemoteSideCauseNegotiationNotNeeded) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));
  // Local restart.
  caller->pc()->RestartIce();
  caller->observer()->clear_legacy_renegotiation_needed();
  caller->observer()->clear_latest_negotiation_needed_event();
  // Remote restart and O/A exchange with `caller` as the answerer should
  // restart ICE locally as well.
  callee->pc()->RestartIce();
  ASSERT_TRUE(callee->ExchangeOfferAnswerWith(caller.get()));
  // Having restarted ICE by the remote offer, we do not need to renegotiate ICE
  // credentials when back in the stable signaling state.
  EXPECT_FALSE(caller->observer()->legacy_renegotiation_needed());
  EXPECT_FALSE(caller->observer()->has_negotiation_needed_event());
}

TEST_F(PeerConnectionIceTestUnifiedPlan,
       RestartIceTwiceDoesNotFireNegotiationNeededTwice) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));
  caller->pc()->RestartIce();
  EXPECT_TRUE(caller->observer()->legacy_renegotiation_needed());
  EXPECT_TRUE(caller->observer()->has_negotiation_needed_event());
  caller->observer()->clear_legacy_renegotiation_needed();
  caller->observer()->clear_latest_negotiation_needed_event();
  caller->pc()->RestartIce();
  EXPECT_FALSE(caller->observer()->legacy_renegotiation_needed());
  EXPECT_FALSE(caller->observer()->has_negotiation_needed_event());
}

// In Plan B, "onnegotiationneeded" is not spec-compliant, firing based on if
// something changed rather than if negotiation is needed. In Unified Plan it
// fires according to spec. As such, some tests are SdpSemantics-specific.
class PeerConnectionIceTestPlanB : public PeerConnectionIceBaseTest {
 protected:
  PeerConnectionIceTestPlanB()
      : PeerConnectionIceBaseTest(SdpSemantics::kPlanB_DEPRECATED) {}
};

TEST_F(PeerConnectionIceTestPlanB,
       RestartIceWhileOfferIsPendingCausesNegotiationNeededImmediately) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));
  caller->observer()->clear_legacy_renegotiation_needed();
  caller->observer()->clear_latest_negotiation_needed_event();
  caller->pc()->RestartIce();
  EXPECT_TRUE(caller->observer()->legacy_renegotiation_needed());
  EXPECT_TRUE(caller->observer()->has_negotiation_needed_event());
  caller->observer()->clear_legacy_renegotiation_needed();
  caller->observer()->clear_latest_negotiation_needed_event();
  ASSERT_TRUE(
      caller->SetRemoteDescription(callee->CreateAnswerAndSetAsLocal()));
  // In Plan B, the event fired early so we don't expect it to fire now. This is
  // not spec-compliant but follows the pattern of existing Plan B behavior.
  EXPECT_FALSE(caller->observer()->legacy_renegotiation_needed());
  EXPECT_FALSE(caller->observer()->has_negotiation_needed_event());
}

TEST_F(PeerConnectionIceTestPlanB,
       RestartIceTwiceDoesFireNegotiationNeededTwice) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));
  caller->observer()->clear_legacy_renegotiation_needed();
  caller->observer()->clear_latest_negotiation_needed_event();
  caller->pc()->RestartIce();
  EXPECT_TRUE(caller->observer()->legacy_renegotiation_needed());
  EXPECT_TRUE(caller->observer()->has_negotiation_needed_event());
  caller->observer()->clear_legacy_renegotiation_needed();
  caller->observer()->clear_latest_negotiation_needed_event();
  caller->pc()->RestartIce();
  // In Plan B, the event fires every time something changed, even if we have
  // already fired the event. This is not spec-compliant but follows the same
  // pattern of existing Plan B behavior.
  EXPECT_TRUE(caller->observer()->legacy_renegotiation_needed());
  EXPECT_TRUE(caller->observer()->has_negotiation_needed_event());
}

// The following parameterized test verifies that if an offer is sent with a
// modified ICE ufrag and/or ICE pwd, then the answer should identify that the
// other side has initiated an ICE restart and generate a new ufrag and pwd.
// RFC 5245 says: "If the offer contained a change in the a=ice-ufrag or
// a=ice-pwd attributes compared to the previous SDP from the peer, it
// indicates that ICE is restarting for this media stream."

class PeerConnectionIceUfragPwdAnswerTest
    : public PeerConnectionIceBaseTest,
      public ::testing::WithParamInterface<
          std::tuple<SdpSemantics, std::tuple<bool, bool>>> {
 protected:
  PeerConnectionIceUfragPwdAnswerTest()
      : PeerConnectionIceBaseTest(std::get<0>(GetParam())) {
    auto param = std::get<1>(GetParam());
    offer_new_ufrag_ = std::get<0>(param);
    offer_new_pwd_ = std::get<1>(param);
  }

  bool offer_new_ufrag_;
  bool offer_new_pwd_;
};

TEST_P(PeerConnectionIceUfragPwdAnswerTest, TestIncludedInAnswer) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));
  ASSERT_TRUE(
      caller->SetRemoteDescription(callee->CreateAnswerAndSetAsLocal()));

  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  auto* offer_transport_desc = GetFirstTransportDescription(offer.get());
  if (offer_new_ufrag_) {
    offer_transport_desc->ice_ufrag += "+new";
  }
  if (offer_new_pwd_) {
    offer_transport_desc->ice_pwd += "+new";
  }

  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));

  std::unique_ptr<SessionDescriptionInterface> answer = callee->CreateAnswer();
  auto* answer_transport_desc = GetFirstTransportDescription(answer.get());
  auto* local_transport_desc =
      GetFirstTransportDescription(callee->pc()->local_description());

  EXPECT_NE(answer_transport_desc->ice_ufrag, local_transport_desc->ice_ufrag);
  EXPECT_NE(answer_transport_desc->ice_pwd, local_transport_desc->ice_pwd);
}

INSTANTIATE_TEST_SUITE_P(
    PeerConnectionIceTest,
    PeerConnectionIceUfragPwdAnswerTest,
    Combine(Values(SdpSemantics::kPlanB_DEPRECATED, SdpSemantics::kUnifiedPlan),
            Values(std::make_pair(true, true),      // Both changed.
                   std::make_pair(true, false),     // Only ufrag changed.
                   std::make_pair(false, true))));  // Only pwd changed.

// Test that if an ICE restart is offered on one media section, then the answer
// will only change ICE ufrag/pwd for that section and keep the other sections
// the same.
// Note that this only works if we have disabled BUNDLE, otherwise all media
// sections will share the same transport.
TEST_P(PeerConnectionIceTest,
       CreateAnswerHasNewUfragPwdForOnlyMediaSectionWhichRestarted) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));
  ASSERT_TRUE(
      caller->SetRemoteDescription(callee->CreateAnswerAndSetAsLocal()));

  RTCOfferAnswerOptions disable_bundle_options;
  disable_bundle_options.use_rtp_mux = false;

  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOffer(disable_bundle_options);

  // Signal ICE restart on the first media section.
  auto* offer_transport_desc = GetFirstTransportDescription(offer.get());
  offer_transport_desc->ice_ufrag += "+new";
  offer_transport_desc->ice_pwd += "+new";

  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));

  std::unique_ptr<SessionDescriptionInterface> answer =
      callee->CreateAnswer(disable_bundle_options);
  const auto& answer_transports = answer->description()->transport_infos();
  const auto& local_transports =
      callee->pc()->local_description()->description()->transport_infos();

  EXPECT_NE(answer_transports[0].description.ice_ufrag,
            local_transports[0].description.ice_ufrag);
  EXPECT_NE(answer_transports[0].description.ice_pwd,
            local_transports[0].description.ice_pwd);
  EXPECT_EQ(answer_transports[1].description.ice_ufrag,
            local_transports[1].description.ice_ufrag);
  EXPECT_EQ(answer_transports[1].description.ice_pwd,
            local_transports[1].description.ice_pwd);
}

// Test that when the initial offerer (caller) uses the lite implementation of
// ICE and the callee uses the full implementation, the caller takes the
// CONTROLLED role and the callee takes the CONTROLLING role. This is specified
// in RFC5245 Section 5.1.1.
TEST_P(PeerConnectionIceTest,
       OfferFromLiteIceControlledAndAnswerFromFullIceControlling) {
  // Munging allowed: kIceMode (23)
  auto caller = CreatePeerConnectionWithAudioVideo(
      "WebRTC-NoSdpMangleAllowForTesting/Enabled,23/");
  auto callee = CreatePeerConnectionWithAudioVideo(
      "WebRTC-NoSdpMangleAllowForTesting/Enabled,23/");

  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  SetIceMode(offer.get(), IceMode::ICEMODE_LITE);
  ASSERT_TRUE(caller->SetLocalDescription(offer->Clone()));
  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));

  std::unique_ptr<SessionDescriptionInterface> answer = callee->CreateAnswer();
  SetIceMode(answer.get(), IceMode::ICEMODE_FULL);
  ASSERT_TRUE(callee->SetLocalDescription(answer->Clone()));
  ASSERT_TRUE(caller->SetRemoteDescription(std::move(answer)));

  EXPECT_EQ(ICEROLE_CONTROLLED, GetIceRole(caller));
  EXPECT_EQ(ICEROLE_CONTROLLING, GetIceRole(callee));
}

// Test that when the caller and the callee both use the lite implementation of
// ICE, the initial offerer (caller) takes the CONTROLLING role and the callee
// takes the CONTROLLED role. This is specified in RFC5245 Section 5.1.1.
TEST_P(PeerConnectionIceTest,
       OfferFromLiteIceControllingAndAnswerFromLiteIceControlled) {
  // Munging allowed: kIceMode (23)
  auto caller = CreatePeerConnectionWithAudioVideo(
      "WebRTC-NoSdpMangleAllowForTesting/Enabled,23/");
  auto callee = CreatePeerConnectionWithAudioVideo(
      "WebRTC-NoSdpMangleAllowForTesting/Enabled,23/");

  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  SetIceMode(offer.get(), IceMode::ICEMODE_LITE);
  ASSERT_TRUE(caller->SetLocalDescription(offer->Clone()));
  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));

  std::unique_ptr<SessionDescriptionInterface> answer = callee->CreateAnswer();
  SetIceMode(answer.get(), IceMode::ICEMODE_LITE);
  ASSERT_TRUE(callee->SetLocalDescription(answer->Clone()));
  ASSERT_TRUE(caller->SetRemoteDescription(std::move(answer)));

  EXPECT_EQ(ICEROLE_CONTROLLING, GetIceRole(caller));
  EXPECT_EQ(ICEROLE_CONTROLLED, GetIceRole(callee));
}

INSTANTIATE_TEST_SUITE_P(PeerConnectionIceTest,
                         PeerConnectionIceTest,
                         Values(SdpSemantics::kPlanB_DEPRECATED,
                                SdpSemantics::kUnifiedPlan));

class PeerConnectionIceConfigTest : public ::testing::Test {
 public:
  PeerConnectionIceConfigTest()
      : socket_server_(CreateDefaultSocketServer()),
        network_thread_(new Thread(socket_server_.get())) {
    RTC_CHECK(network_thread_->Start());
  }

 protected:
  void SetUp() override {
    pc_factory_ = CreatePeerConnectionFactory(
        network_thread_.get(), Thread::Current(),
        FakeAudioCaptureModule::Create(), CreateBuiltinAudioEncoderFactory(),
        CreateBuiltinAudioDecoderFactory(),
        std::make_unique<VideoEncoderFactoryTemplate<
            LibvpxVp8EncoderTemplateAdapter, LibvpxVp9EncoderTemplateAdapter,
            OpenH264EncoderTemplateAdapter, LibaomAv1EncoderTemplateAdapter>>(),
        std::make_unique<VideoDecoderFactoryTemplate<
            LibvpxVp8DecoderTemplateAdapter, LibvpxVp9DecoderTemplateAdapter,
            OpenH264DecoderTemplateAdapter, Dav1dDecoderTemplateAdapter>>(),
        nullptr /* audio_mixer */, nullptr /* audio_processing */,
        nullptr /* audio_frame_processor */, CreateTestFieldTrialsPtr());
  }
  void CreatePeerConnection(const RTCConfiguration& config) {
    auto port_allocator = network_thread_->BlockingCall([&] {
      return std::make_unique<FakePortAllocator>(CreateTestEnvironment(),
                                                 socket_server_.get());
    });
    port_allocator_ = port_allocator.get();
    PeerConnectionDependencies pc_dependencies(&observer_);
    pc_dependencies.allocator = std::move(port_allocator);
    auto result = pc_factory_->CreatePeerConnectionOrError(
        config, std::move(pc_dependencies));
    EXPECT_TRUE(result.ok());
    pc_ = result.MoveValue();
  }

  test::RunLoop main_thread_;
  std::unique_ptr<SocketServer> socket_server_;
  std::unique_ptr<Thread> network_thread_;
  scoped_refptr<PeerConnectionFactoryInterface> pc_factory_ = nullptr;
  scoped_refptr<PeerConnectionInterface> pc_ = nullptr;
  FakePortAllocator* port_allocator_ = nullptr;

  MockPeerConnectionObserver observer_;
};

TEST_F(PeerConnectionIceConfigTest, SetStunCandidateKeepaliveInterval) {
  RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  config.stun_candidate_keepalive_interval = 123;
  config.ice_candidate_pool_size = 1;
  CreatePeerConnection(config);
  ASSERT_NE(port_allocator_, nullptr);
  EXPECT_EQ(network_thread_->BlockingCall([this] {
    return port_allocator_->stun_candidate_keepalive_interval();
  }),
            TimeDelta::Millis(123));
  config.stun_candidate_keepalive_interval = 321;
  ASSERT_TRUE(pc_->SetConfiguration(config).ok());
  EXPECT_EQ(network_thread_->BlockingCall([this] {
    return port_allocator_->stun_candidate_keepalive_interval();
  }),
            TimeDelta::Millis(321));
}

TEST_F(PeerConnectionIceConfigTest, SetStableWritableConnectionInterval) {
  RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  config.stable_writable_connection_ping_interval_ms = 3500;
  CreatePeerConnection(config);
  EXPECT_TRUE(pc_->SetConfiguration(config).ok());
  EXPECT_EQ(pc_->GetConfiguration().stable_writable_connection_ping_interval_ms,
            config.stable_writable_connection_ping_interval_ms);
}

TEST_F(PeerConnectionIceConfigTest,
       SetStableWritableConnectionInterval_FailsValidation) {
  RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  CreatePeerConnection(config);
  ASSERT_TRUE(pc_->SetConfiguration(config).ok());
  config.stable_writable_connection_ping_interval_ms = 5000;
  config.ice_check_interval_strong_connectivity = 7500;
  EXPECT_FALSE(pc_->SetConfiguration(config).ok());
}

TEST_F(PeerConnectionIceConfigTest,
       SetStableWritableConnectionInterval_DefaultValue_FailsValidation) {
  RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  CreatePeerConnection(config);
  ASSERT_TRUE(pc_->SetConfiguration(config).ok());
  config.ice_check_interval_strong_connectivity = 2500;
  EXPECT_TRUE(pc_->SetConfiguration(config).ok());
  config.ice_check_interval_strong_connectivity = 2501;
  EXPECT_FALSE(pc_->SetConfiguration(config).ok());
}

TEST_P(PeerConnectionIceTest, IceCredentialsCreateOffer) {
  RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  config.ice_candidate_pool_size = 1;
  auto pc = CreatePeerConnectionWithAudioVideo(config);
  ASSERT_NE(pc->GetInternalPeerConnection()->port_allocator(), nullptr);
  std::unique_ptr<SessionDescriptionInterface> offer = pc->CreateOffer();
  auto credentials = pc->network_thread()->BlockingCall([&] {
    return pc->GetInternalPeerConnection()
        ->port_allocator()
        ->GetPooledIceCredentials();
  });
  ASSERT_EQ(1u, credentials.size());

  auto* desc = offer->description();
  for (const auto& content : desc->contents()) {
    auto* transport_info = desc->GetTransportInfoByName(content.mid());
    EXPECT_EQ(transport_info->description.ice_ufrag, credentials[0].ufrag);
    EXPECT_EQ(transport_info->description.ice_pwd, credentials[0].pwd);
  }
}

TEST_P(PeerConnectionIceTest, IceCredentialsCreateAnswer) {
  RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  config.ice_candidate_pool_size = 1;
  auto pc = CreatePeerConnectionWithAudioVideo(config);
  ASSERT_NE(pc->GetInternalPeerConnection()->port_allocator(), nullptr);
  std::unique_ptr<SessionDescriptionInterface> offer = pc->CreateOffer();
  ASSERT_TRUE(pc->SetRemoteDescription(std::move(offer)));
  std::unique_ptr<SessionDescriptionInterface> answer = pc->CreateAnswer();

  auto credentials = pc->network_thread()->BlockingCall([&] {
    return pc->GetInternalPeerConnection()
        ->port_allocator()
        ->GetPooledIceCredentials();
  });
  ASSERT_EQ(1u, credentials.size());

  auto* desc = answer->description();
  for (const auto& content : desc->contents()) {
    auto* transport_info = desc->GetTransportInfoByName(content.mid());
    EXPECT_EQ(transport_info->description.ice_ufrag, credentials[0].ufrag);
    EXPECT_EQ(transport_info->description.ice_pwd, credentials[0].pwd);
  }
}

// Applies an offer and its answer in the same task, so that the remote answer
// is applied before the pooled ICE credentials that gathering for the offer
// reports arrive. Those credentials can include the ones the transport uses,
// since the transport relabels whichever pooled session it takes. They must not
// be used once the answer has discarded the candidate pool, otherwise the ICE
// restart offer below would keep the current credentials.
TEST_P(PeerConnectionIceTest, IceRestartAfterRemoteAnswerChangesCredentials) {
  RTCConfiguration config;
  config.ice_candidate_pool_size = 2;
  auto caller = CreatePeerConnection(config);
  caller->AddAudioTrack("a");
  auto callee = CreatePeerConnection();

  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  ASSERT_TRUE(offer);
  ASSERT_TRUE(callee->SetRemoteDescription(offer->Clone()));
  std::unique_ptr<SessionDescriptionInterface> answer = callee->CreateAnswer();
  ASSERT_TRUE(answer);

  auto sld_observer = make_ref_counted<FakeSetLocalDescriptionObserver>();
  auto srd_observer = make_ref_counted<FakeSetRemoteDescriptionObserver>();
  caller->pc()->SetLocalDescription(std::move(offer), sld_observer);
  caller->pc()->SetRemoteDescription(std::move(answer), srd_observer);
  ASSERT_TRUE(sld_observer->called());
  EXPECT_TRUE(sld_observer->error().ok());
  ASSERT_TRUE(srd_observer->called());
  EXPECT_TRUE(srd_observer->error().ok());
  // Run the tasks posted to the network thread, then their replies.
  caller->network_thread()->BlockingCall([] {});
  main_.Flush();

  RTCOfferAnswerOptions options;
  options.ice_restart = true;
  std::unique_ptr<SessionDescriptionInterface> restart_offer =
      caller->CreateOffer(options);
  ASSERT_TRUE(restart_offer);
  EXPECT_NE(GetFirstTransportDescription(restart_offer.get())->ice_ufrag,
            GetFirstTransportDescription(caller->pc()->local_description())
                ->ice_ufrag);
}

// Regression test for https://bugs.chromium.org/p/webrtc/issues/detail?id=4728
TEST_P(PeerConnectionIceTest, CloseDoesNotTransitionGatheringStateToComplete) {
  auto pc = CreatePeerConnectionWithAudioVideo();
  pc->pc()->Close();
  EXPECT_FALSE(pc->IsIceGatheringDone());
  EXPECT_EQ(PeerConnectionInterface::kIceGatheringNew,
            pc->pc()->ice_gathering_state());
}

TEST_P(PeerConnectionIceTest, PrefersMidOverMLineIndex) {
  const SocketAddress kCalleeAddress("1.1.1.1", 1111);

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));
  ASSERT_TRUE(
      caller->SetRemoteDescription(callee->CreateAnswerAndSetAsLocal()));

  Candidate candidate = CreateLocalUdpCandidate(kCalleeAddress);
  auto* audio_content =
      GetFirstAudioContent(caller->pc()->local_description()->description());
  std::unique_ptr<IceCandidate> ice_candidate =
      CreateIceCandidate(audio_content->mid(), 65535, candidate);
  EXPECT_TRUE(caller->pc()->AddIceCandidate(ice_candidate.get()));
  EXPECT_TRUE(caller->pc()->RemoveIceCandidate(ice_candidate.get()));
}

}  // namespace webrtc
