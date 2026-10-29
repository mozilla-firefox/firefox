/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/environment/environment.h"
#include "api/jsep.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "api/sctp_transport_interface.h"
#include "pc/media_session.h"
#include "pc/peer_connection.h"
#include "pc/peer_connection_wrapper.h"
#include "pc/sctp_transport.h"
#include "pc/session_description.h"
#include "pc/test/enable_fake_media.h"
#include "pc/test/mock_peer_connection_observers.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/thread.h"
#include "rtc_base/virtual_socket_server.h"
#include "test/create_test_environment.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/pc/sctp/fake_sctp_transport.h"
#include "test/run_loop.h"

namespace webrtc {

using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::Values;

using RTCConfiguration = PeerConnectionInterface::RTCConfiguration;
using RTCOfferAnswerOptions = PeerConnectionInterface::RTCOfferAnswerOptions;

namespace {

PeerConnectionFactoryDependencies CreatePeerConnectionFactoryDependencies() {
  PeerConnectionFactoryDependencies deps;
  deps.network_thread = Thread::Current();
  deps.signaling_thread = Thread::Current();
  EnableFakeMedia(deps);
  deps.sctp_factory = std::make_unique<FakeSctpTransportFactory>();
  return deps;
}

}  // namespace

class PeerConnectionWrapperForDataChannelTest : public PeerConnectionWrapper {
 public:
  using PeerConnectionWrapper::PeerConnectionWrapper;

  FakeSctpTransportFactory* sctp_transport_factory() {
    return sctp_transport_factory_;
  }

  void set_sctp_transport_factory(
      FakeSctpTransportFactory* sctp_transport_factory) {
    sctp_transport_factory_ = sctp_transport_factory;
  }

  std::optional<std::string> sctp_mid() {
    return GetInternalPeerConnection()->sctp_mid();
  }

  std::optional<std::string> sctp_transport_name() {
    return GetInternalPeerConnection()->sctp_transport_name();
  }

 private:
  FakeSctpTransportFactory* sctp_transport_factory_ = nullptr;
};

class PeerConnectionDataChannelBaseTest : public ::testing::Test {
 protected:
  typedef std::unique_ptr<PeerConnectionWrapperForDataChannelTest> WrapperPtr;

  explicit PeerConnectionDataChannelBaseTest(SdpSemantics sdp_semantics)
      : env_(CreateTestEnvironment()),
        vss_(new VirtualSocketServer()),
        main_(vss_.get()),
        sdp_semantics_(sdp_semantics) {}

  WrapperPtr CreatePeerConnection() {
    return CreatePeerConnection(RTCConfiguration());
  }

  WrapperPtr CreatePeerConnection(const RTCConfiguration& config) {
    return CreatePeerConnection(config,
                                PeerConnectionFactoryInterface::Options());
  }

  WrapperPtr CreatePeerConnection(
      const RTCConfiguration& config,
      const PeerConnectionFactoryInterface::Options factory_options) {
    return CreatePeerConnection(config, factory_options, "");
  }

  WrapperPtr CreatePeerConnection(absl::string_view field_trials) {
    return CreatePeerConnection(RTCConfiguration(), field_trials);
  }

  WrapperPtr CreatePeerConnection(const RTCConfiguration& config,
                                  absl::string_view field_trials) {
    return CreatePeerConnection(
        config, PeerConnectionFactoryInterface::Options(), field_trials);
  }

  WrapperPtr CreatePeerConnection(
      const RTCConfiguration& config,
      const PeerConnectionFactoryInterface::Options factory_options,
      absl::string_view field_trials) {
    auto factory_deps = CreatePeerConnectionFactoryDependencies();
    FakeSctpTransportFactory* fake_sctp_transport_factory =
        static_cast<FakeSctpTransportFactory*>(factory_deps.sctp_factory.get());
    factory_deps.env = env_;
    scoped_refptr<PeerConnectionFactoryInterface> pc_factory =
        CreateModularPeerConnectionFactory(std::move(factory_deps));
    pc_factory->SetOptions(factory_options);
    auto observer = std::make_unique<MockPeerConnectionObserver>();
    RTCConfiguration modified_config = config;
    modified_config.sdp_semantics = sdp_semantics_;
    PeerConnectionDependencies pc_deps(observer.get());
    if (!field_trials.empty()) {
      pc_deps.trials = CreateTestFieldTrialsPtr(field_trials);
    }
    auto result = pc_factory->CreatePeerConnectionOrError(modified_config,
                                                          std::move(pc_deps));
    if (!result.ok()) {
      return nullptr;
    }

    observer->SetPeerConnectionInterface(result.value().get());
    auto wrapper = std::make_unique<PeerConnectionWrapperForDataChannelTest>(
        pc_factory, result.MoveValue(), std::move(observer));
    wrapper->set_sctp_transport_factory(fake_sctp_transport_factory);
    return wrapper;
  }

  // Accepts the same arguments as CreatePeerConnection and adds a default data
  // channel.
  template <typename... Args>
  WrapperPtr CreatePeerConnectionWithDataChannel(Args&&... args) {
    auto wrapper = CreatePeerConnection(std::forward<Args>(args)...);
    if (!wrapper) {
      return nullptr;
    }
    EXPECT_TRUE(wrapper->pc()->CreateDataChannelOrError("dc", nullptr).ok());
    return wrapper;
  }

  // Changes the SCTP data channel port on the given session description.
  void ChangeSctpPortOnDescription(SessionDescription* desc, int port) {
    auto* data_content = GetFirstDataContent(desc);
    RTC_DCHECK(data_content);
    auto* data_desc = data_content->media_description()->as_sctp();
    RTC_DCHECK(data_desc);
    data_desc->set_port(port);
  }

  const Environment env_;
  std::unique_ptr<VirtualSocketServer> vss_;
  test::RunLoop main_;
  const SdpSemantics sdp_semantics_;
};

class PeerConnectionDataChannelTest
    : public PeerConnectionDataChannelBaseTest,
      public ::testing::WithParamInterface<SdpSemantics> {
 protected:
  PeerConnectionDataChannelTest()
      : PeerConnectionDataChannelBaseTest(GetParam()) {}
};

class PeerConnectionDataChannelUnifiedPlanTest
    : public PeerConnectionDataChannelBaseTest {
 protected:
  PeerConnectionDataChannelUnifiedPlanTest()
      : PeerConnectionDataChannelBaseTest(SdpSemantics::kUnifiedPlan) {}
};

TEST_P(PeerConnectionDataChannelTest, InternalSctpTransportDeletedOnTeardown) {
  auto caller = CreatePeerConnectionWithDataChannel();

  ASSERT_TRUE(caller->SetLocalDescription(caller->CreateOffer()));
  EXPECT_TRUE(caller->sctp_transport_factory()->last_fake_sctp_transport());

  scoped_refptr<SctpTransportInterface> sctp_transport =
      caller->GetInternalPeerConnection()->GetSctpTransport();

  caller.reset();
  EXPECT_EQ(static_cast<SctpTransport*>(sctp_transport.get())->internal(),
            nullptr);
}

// Test that sctp_mid/sctp_transport_name (used for stats) are correct
// before and after BUNDLE is negotiated.
TEST_P(PeerConnectionDataChannelTest, SctpContentAndTransportNameSetCorrectly) {
  auto caller = CreatePeerConnection();
  auto callee = CreatePeerConnection();

  // Initially these fields should be empty.
  EXPECT_FALSE(caller->sctp_mid());
  EXPECT_FALSE(caller->sctp_transport_name());

  // Create offer with audio/video/data.
  // Default bundle policy is "balanced", so data should be using its own
  // transport.
  caller->AddAudioTrack("a");
  caller->AddVideoTrack("v");
  caller->pc()->CreateDataChannelOrError("dc", nullptr);

  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  const auto& offer_contents = offer->description()->contents();
  ASSERT_EQ(MediaType::AUDIO, offer_contents[0].media_description()->type());
  auto audio_mid = offer_contents[0].mid();
  ASSERT_EQ(MediaType::DATA, offer_contents[2].media_description()->type());
  auto data_mid = offer_contents[2].mid();

  ASSERT_TRUE(caller->SetLocalDescription(offer->Clone()));
  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));

  ASSERT_TRUE(caller->sctp_mid());
  EXPECT_EQ(data_mid, *caller->sctp_mid());
  ASSERT_TRUE(caller->sctp_transport_name());
  EXPECT_EQ(data_mid, *caller->sctp_transport_name());

  // Create answer that finishes BUNDLE negotiation, which means everything
  // should be bundled on the first transport (audio).
  RTCOfferAnswerOptions options;
  options.use_rtp_mux = true;
  ASSERT_TRUE(
      caller->SetRemoteDescription(callee->CreateAnswerAndSetAsLocal()));

  ASSERT_TRUE(caller->sctp_mid());
  EXPECT_EQ(data_mid, *caller->sctp_mid());
  ASSERT_TRUE(caller->sctp_transport_name());
  EXPECT_EQ(audio_mid, *caller->sctp_transport_name());
}

TEST_P(PeerConnectionDataChannelTest,
       CreateOfferWithNoDataChannelsGivesNoDataSection) {
  auto caller = CreatePeerConnection();
  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  EXPECT_THAT(offer->description()->contents(), IsEmpty());
}

TEST_P(PeerConnectionDataChannelTest,
       CreateAnswerWithRemoteSctpDataChannelIncludesDataSection) {
  auto caller = CreatePeerConnectionWithDataChannel();
  auto callee = CreatePeerConnection();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));

  std::unique_ptr<SessionDescriptionInterface> answer = callee->CreateAnswer();
  ASSERT_THAT(answer, NotNull());
  auto* data_content = GetFirstDataContent(answer->description());
  ASSERT_TRUE(data_content);
  EXPECT_FALSE(data_content->rejected);
  EXPECT_TRUE(
      answer->description()->GetTransportInfoByName(data_content->mid()));
}

TEST_P(PeerConnectionDataChannelTest, SctpPortPropagatedFromSdpToTransport) {
  constexpr int kNewSendPort = 9998;
  constexpr int kNewRecvPort = 7775;

  // Munging allowed: kDataChannelSctpPort (102)
  auto caller = CreatePeerConnectionWithDataChannel();
  auto callee = CreatePeerConnectionWithDataChannel(
      "WebRTC-NoSdpMangleAllowForTesting/Enabled,102/");

  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  ChangeSctpPortOnDescription(offer->description(), kNewSendPort);
  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));

  std::unique_ptr<SessionDescriptionInterface> answer = callee->CreateAnswer();
  ChangeSctpPortOnDescription(answer->description(), kNewRecvPort);
  std::string sdp;
  answer->ToString(&sdp);
  ASSERT_TRUE(callee->SetLocalDescription(std::move(answer)));
  auto* callee_transport =
      callee->sctp_transport_factory()->last_fake_sctp_transport();
  ASSERT_TRUE(callee_transport);
  EXPECT_EQ(kNewSendPort, callee_transport->remote_port());
  EXPECT_EQ(kNewRecvPort, callee_transport->local_port());
}

TEST_P(PeerConnectionDataChannelTest, ModernSdpSyntaxByDefault) {
  PeerConnectionInterface::RTCOfferAnswerOptions options;
  auto caller = CreatePeerConnectionWithDataChannel();
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOffer(options);
  EXPECT_FALSE(
      GetFirstSctpDataContentDescription(offer->description())->use_sctpmap());
  std::string sdp;
  offer->ToString(&sdp);
  RTC_LOG(LS_ERROR) << sdp;
  EXPECT_THAT(sdp, HasSubstr(" UDP/DTLS/SCTP webrtc-datachannel"));
  EXPECT_THAT(sdp, Not(HasSubstr("a=sctpmap:")));
}

TEST_P(PeerConnectionDataChannelTest, ObsoleteSdpSyntaxIfSet) {
  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.use_obsolete_sctp_sdp = true;
  auto caller = CreatePeerConnectionWithDataChannel();
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOffer(options);
  EXPECT_TRUE(
      GetFirstSctpDataContentDescription(offer->description())->use_sctpmap());
  std::string sdp;
  offer->ToString(&sdp);
  EXPECT_THAT(sdp, Not(HasSubstr(" UDP/DTLS/SCTP webrtc-datachannel")));
  EXPECT_THAT(sdp, HasSubstr("a=sctpmap:"));
}

INSTANTIATE_TEST_SUITE_P(PeerConnectionDataChannelTest,
                         PeerConnectionDataChannelTest,
                         Values(SdpSemantics::kPlanB_DEPRECATED,
                                SdpSemantics::kUnifiedPlan));

}  // namespace webrtc
