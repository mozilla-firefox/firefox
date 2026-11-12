/*
 *  Copyright 2020 The WebRTC project authors. All Rights Reserved.
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
#include <tuple>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/environment/environment_factory.h"
#include "api/jsep.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtc_event_log/rtc_event_log_factory.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_parameters.h"
#include "api/rtp_transceiver_direction.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "media/base/fake_media_engine.h"
#include "p2p/test/fake_port_allocator.h"
#include "pc/peer_connection_wrapper.h"
#include "pc/session_description.h"
#include "pc/test/enable_fake_media.h"
#include "pc/test/mock_peer_connection_observers.h"
#include "rtc_base/internal/default_socket_server.h"
#include "rtc_base/socket_server.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/thread.h"
#include "test/create_test_environment.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"

namespace webrtc {

using ::testing::AllOf;
using ::testing::Combine;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Return;
using ::testing::UnorderedElementsAre;
using ::testing::Values;

class PeerConnectionHeaderExtensionTest
    : public ::testing::TestWithParam<std::tuple<MediaType, SdpSemantics>> {
 protected:
  PeerConnectionHeaderExtensionTest()
      : socket_server_(CreateDefaultSocketServer()),
        main_thread_(socket_server_.get()),
        extensions_(
            {RtpHeaderExtensionCapability("uri1",
                                          RtpHeaderExtensionId(1),
                                          RtpTransceiverDirection::kStopped),
             RtpHeaderExtensionCapability("uri2",
                                          RtpHeaderExtensionId(2),
                                          RtpTransceiverDirection::kSendOnly),
             RtpHeaderExtensionCapability("uri3",
                                          RtpHeaderExtensionId(3),
                                          RtpTransceiverDirection::kRecvOnly),
             RtpHeaderExtensionCapability("uri4",
                                          RtpHeaderExtensionId(4),
                                          RtpTransceiverDirection::kSendRecv),
             RtpHeaderExtensionCapability("encrypted_uri",
                                          RtpHeaderExtensionId(5),
                                          /* preferred_encrypt= */ true,
                                          RtpTransceiverDirection::kStopped)}) {
  }

  std::unique_ptr<PeerConnectionWrapper> CreatePeerConnection(
      MediaType media_type,
      std::optional<SdpSemantics> semantics,
      std::string field_trials_string = "") {
    auto media_engine = std::make_unique<FakeMediaEngine>();
    if (media_type == MediaType::AUDIO)
      media_engine->fake_voice_engine()->SetRtpHeaderExtensions(extensions_);
    else
      media_engine->fake_video_engine()->SetRtpHeaderExtensions(extensions_);
    PeerConnectionFactoryDependencies factory_dependencies;
    factory_dependencies.env =
        CreateEnvironment(CreateTestFieldTrialsPtr(field_trials_string));
    factory_dependencies.network_thread = Thread::Current();
    factory_dependencies.signaling_thread = Thread::Current();
    EnableFakeMedia(factory_dependencies, std::move(media_engine));

    factory_dependencies.event_log_factory =
        std::make_unique<RtcEventLogFactory>();

    auto pc_factory =
        CreateModularPeerConnectionFactory(std::move(factory_dependencies));

    auto fake_port_allocator = std::make_unique<FakePortAllocator>(
        CreateTestEnvironment(), socket_server_.get());
    auto observer = std::make_unique<MockPeerConnectionObserver>();
    PeerConnectionInterface::RTCConfiguration config;
    if (semantics)
      config.sdp_semantics = *semantics;
    PeerConnectionDependencies pc_dependencies(observer.get());
    pc_dependencies.allocator = std::move(fake_port_allocator);
    auto result = pc_factory->CreatePeerConnectionOrError(
        config, std::move(pc_dependencies));
    EXPECT_TRUE(result.ok());
    observer->SetPeerConnectionInterface(result.value().get());
    return std::make_unique<PeerConnectionWrapper>(
        pc_factory, result.MoveValue(), std::move(observer));
  }

  std::unique_ptr<SocketServer> socket_server_;
  test::RunLoop main_thread_;
  std::vector<RtpHeaderExtensionCapability> extensions_;
};

class PeerConnectionHeaderExtensionUnifiedPlanTest
    : public PeerConnectionHeaderExtensionTest {};

TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       TransceiverOffersHeaderExtensions) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  std::unique_ptr<PeerConnectionWrapper> wrapper =
      CreatePeerConnection(media_type, semantics);
  auto transceiver = wrapper->AddTransceiver(media_type);
  EXPECT_EQ(transceiver->GetHeaderExtensionsToNegotiate(), extensions_);
}

TEST_P(PeerConnectionHeaderExtensionTest,
       SenderReceiverCapabilitiesReturnNotStoppedExtensions) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  std::unique_ptr<PeerConnectionWrapper> wrapper =
      CreatePeerConnection(media_type, semantics);
  EXPECT_THAT(wrapper->pc_factory()
                  ->GetRtpSenderCapabilities(media_type)
                  .header_extensions,
              ElementsAre(Field(&RtpHeaderExtensionCapability::uri, "uri2"),
                          Field(&RtpHeaderExtensionCapability::uri, "uri3"),
                          Field(&RtpHeaderExtensionCapability::uri, "uri4")));
  EXPECT_EQ(wrapper->pc_factory()
                ->GetRtpReceiverCapabilities(media_type)
                .header_extensions,
            wrapper->pc_factory()
                ->GetRtpSenderCapabilities(media_type)
                .header_extensions);
}

TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       OffersUnstoppedDefaultExtensions) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  std::unique_ptr<PeerConnectionWrapper> wrapper =
      CreatePeerConnection(media_type, semantics);
  auto transceiver = wrapper->AddTransceiver(media_type);
  auto session_description = wrapper->CreateOffer();
  EXPECT_THAT(session_description->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3"),
                          Field(&RtpExtension::uri, "uri4")));
}

TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       OffersUnstoppedModifiedExtensions) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  std::unique_ptr<PeerConnectionWrapper> wrapper =
      CreatePeerConnection(media_type, semantics);
  auto transceiver = wrapper->AddTransceiver(media_type);
  auto modified_extensions = transceiver->GetHeaderExtensionsToNegotiate();
  modified_extensions[0].direction = RtpTransceiverDirection::kSendRecv;
  modified_extensions[3].direction = RtpTransceiverDirection::kStopped;
  EXPECT_TRUE(
      transceiver->SetHeaderExtensionsToNegotiate(modified_extensions).ok());
  auto session_description = wrapper->CreateOffer();
  EXPECT_THAT(session_description->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri1"),
                          Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3")));
}

TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       AnswersUnstoppedModifiedExtensions) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  std::unique_ptr<PeerConnectionWrapper> pc1 =
      CreatePeerConnection(media_type, semantics);
  std::unique_ptr<PeerConnectionWrapper> pc2 =
      CreatePeerConnection(media_type, semantics);
  auto transceiver1 = pc1->AddTransceiver(media_type);

  std::unique_ptr<SessionDescriptionInterface> offer =
      pc1->CreateOfferAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());
  pc2->SetRemoteDescription(std::move(offer));

  ASSERT_EQ(pc2->pc()->GetTransceivers().size(), 1u);
  auto transceiver2 = pc2->pc()->GetTransceivers()[0];
  auto modified_extensions = transceiver2->GetHeaderExtensionsToNegotiate();
  // Don't offer uri4.
  modified_extensions[3].direction = RtpTransceiverDirection::kStopped;
  transceiver2->SetHeaderExtensionsToNegotiate(modified_extensions);

  std::unique_ptr<SessionDescriptionInterface> answer =
      pc2->CreateAnswerAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());
  EXPECT_THAT(answer->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3")));
}

TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       NegotiatedExtensionsAreAccessible) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  std::unique_ptr<PeerConnectionWrapper> pc1 =
      CreatePeerConnection(media_type, semantics);
  auto transceiver1 = pc1->AddTransceiver(media_type);
  auto modified_extensions = transceiver1->GetHeaderExtensionsToNegotiate();
  modified_extensions[3].direction = RtpTransceiverDirection::kStopped;
  transceiver1->SetHeaderExtensionsToNegotiate(modified_extensions);
  std::unique_ptr<SessionDescriptionInterface> offer =
      pc1->CreateOfferAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());

  std::unique_ptr<PeerConnectionWrapper> pc2 =
      CreatePeerConnection(media_type, semantics);
  auto transceiver2 = pc2->AddTransceiver(media_type);
  pc2->SetRemoteDescription(std::move(offer));
  std::unique_ptr<SessionDescriptionInterface> answer =
      pc2->CreateAnswerAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());
  pc1->SetRemoteDescription(std::move(answer));

  // PC1 has exts 2-4 unstopped and PC2 has exts 1-3 unstopped -> ext 2, 3
  // survives.
  EXPECT_THAT(transceiver1->GetNegotiatedHeaderExtensions(),
              ElementsAre(Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped)));
}

TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       OfferedExtensionsArePerTransceiver) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  std::unique_ptr<PeerConnectionWrapper> pc1 =
      CreatePeerConnection(media_type, semantics);
  auto transceiver1 = pc1->AddTransceiver(media_type);
  auto transceiver2 = pc1->AddTransceiver(media_type);
  auto modified_extensions = transceiver1->GetHeaderExtensionsToNegotiate();
  modified_extensions[3].direction = RtpTransceiverDirection::kStopped;
  transceiver1->SetHeaderExtensionsToNegotiate(modified_extensions);

  auto session_description = pc1->CreateOffer();
  EXPECT_THAT(session_description->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3")));
  EXPECT_THAT(session_description->description()
                  ->contents()[1]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3"),
                          Field(&RtpExtension::uri, "uri4")));
}

TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       RemovalAfterRenegotiation) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  std::unique_ptr<PeerConnectionWrapper> pc1 =
      CreatePeerConnection(media_type, semantics);
  std::unique_ptr<PeerConnectionWrapper> pc2 =
      CreatePeerConnection(media_type, semantics);
  auto transceiver1 = pc1->AddTransceiver(media_type);

  std::unique_ptr<SessionDescriptionInterface> offer =
      pc1->CreateOfferAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());
  pc2->SetRemoteDescription(std::move(offer));
  std::unique_ptr<SessionDescriptionInterface> answer =
      pc2->CreateAnswerAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());
  pc1->SetRemoteDescription(std::move(answer));

  auto modified_extensions = transceiver1->GetHeaderExtensionsToNegotiate();
  modified_extensions[3].direction = RtpTransceiverDirection::kStopped;
  transceiver1->SetHeaderExtensionsToNegotiate(modified_extensions);
  auto session_description = pc1->CreateOffer();
  EXPECT_THAT(session_description->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3")));
}

TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       StoppedByDefaultExtensionCanBeActivatedByRemoteSdp) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  std::unique_ptr<PeerConnectionWrapper> pc1 =
      CreatePeerConnection(media_type, semantics);
  std::unique_ptr<PeerConnectionWrapper> pc2 =
      CreatePeerConnection(media_type, semantics);
  auto transceiver1 = pc1->AddTransceiver(media_type);

  std::unique_ptr<SessionDescriptionInterface> offer =
      pc1->CreateOfferAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());
  pc2->SetRemoteDescription(std::move(offer));
  std::unique_ptr<SessionDescriptionInterface> answer =
      pc2->CreateAnswerAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());
  std::string sdp;
  ASSERT_TRUE(answer->ToString(&sdp));
  // We support uri1 but it is stopped by default. Let the remote reactivate it.
  sdp += "a=extmap:15 uri1\r\n";
  auto modified_answer = CreateSessionDescription(SdpType::kAnswer, sdp);
  pc1->SetRemoteDescription(std::move(modified_answer));
  EXPECT_THAT(transceiver1->GetNegotiatedHeaderExtensions(),
              ElementsAre(Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped)));
}

TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       UnknownExtensionInRemoteOfferDoesNotShowUp) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  std::unique_ptr<PeerConnectionWrapper> pc =
      CreatePeerConnection(media_type, semantics);
  std::string sdp =
      "v=0\r\n"
      "o=- 0 3 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "a=fingerprint:sha-256 "
      "A7:24:72:CA:6E:02:55:39:BA:66:DF:6E:CC:4C:D8:B0:1A:BF:1A:56:65:7D:F4:03:"
      "AD:7E:77:43:2A:29:EC:93\r\n"
      "a=ice-ufrag:6HHHdzzeIhkE0CKj\r\n"
      "a=ice-pwd:XYDGVpfvklQIEnZ6YnyLsAew\r\n";
  if (media_type == MediaType::AUDIO) {
    sdp +=
        "m=audio 9 RTP/AVPF 111\r\n"
        "a=rtpmap:111 fake_audio_codec/8000\r\n";
  } else {
    sdp +=
        "m=video 9 RTP/AVPF 111\r\n"
        "a=rtpmap:111 fake_video_codec/90000\r\n";
  }
  sdp +=
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtcp-mux\r\n"
      "a=sendonly\r\n"
      "a=mid:audio\r\n"
      "a=setup:actpass\r\n"
      "a=extmap:1 urn:bogus\r\n";
  std::unique_ptr<SessionDescriptionInterface> offer =
      CreateSessionDescription(SdpType::kOffer, sdp);
  pc->SetRemoteDescription(std::move(offer));
  pc->CreateAnswerAndSetAsLocal(
      PeerConnectionInterface::RTCOfferAnswerOptions());
  ASSERT_GT(pc->pc()->GetTransceivers().size(), 0u);
  auto transceiver = pc->pc()->GetTransceivers()[0];
  auto negotiated = transceiver->GetNegotiatedHeaderExtensions();
  EXPECT_EQ(negotiated.size(),
            transceiver->GetHeaderExtensionsToNegotiate().size());
  // All extensions are stopped, the "bogus" one does not show up.
  for (const auto& extension : negotiated) {
    EXPECT_EQ(extension.direction, RtpTransceiverDirection::kStopped);
    EXPECT_NE(extension.uri, "urn:bogus");
  }
}

// These tests are regression tests for behavior that the API
// enables in a proper way. It conflicts with the behavior
// of the API to only offer non-stopped extensions.
TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       SdpMungingAnswerWithoutApiUsageEnablesExtensions) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  // Munging allowed: kRtpHeaderExtensionAdded (41)
  std::unique_ptr<PeerConnectionWrapper> pc =
      CreatePeerConnection(media_type, semantics,
                           "WebRTC-HeaderExtensionNegotiateMemory/Disabled/"
                           "WebRTC-NoSdpMangleAllowForTesting/Enabled,41/");
  std::string sdp =
      "v=0\r\n"
      "o=- 0 3 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "a=fingerprint:sha-256 "
      "A7:24:72:CA:6E:02:55:39:BA:66:DF:6E:CC:4C:D8:B0:1A:BF:1A:56:65:7D:F4:03:"
      "AD:7E:77:43:2A:29:EC:93\r\n"
      "a=ice-ufrag:6HHHdzzeIhkE0CKj\r\n"
      "a=ice-pwd:XYDGVpfvklQIEnZ6YnyLsAew\r\n";
  if (media_type == MediaType::AUDIO) {
    sdp +=
        "m=audio 9 RTP/AVPF 111\r\n"
        "a=rtpmap:111 fake_audio_codec/8000\r\n";
  } else {
    sdp +=
        "m=video 9 RTP/AVPF 111\r\n"
        "a=rtpmap:111 fake_video_codec/90000\r\n";
  }
  sdp +=
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtcp-mux\r\n"
      "a=sendrecv\r\n"
      "a=mid:audio\r\n"
      "a=setup:actpass\r\n"
      "a=extmap:1 uri1\r\n";
  std::unique_ptr<SessionDescriptionInterface> offer =
      CreateSessionDescription(SdpType::kOffer, sdp);
  pc->SetRemoteDescription(std::move(offer));
  std::unique_ptr<SessionDescriptionInterface> answer =
      pc->CreateAnswer(PeerConnectionInterface::RTCOfferAnswerOptions());
  std::string modified_sdp;
  ASSERT_TRUE(answer->ToString(&modified_sdp));
  modified_sdp += "a=extmap:1 uri1\r\n";
  auto modified_answer =
      CreateSessionDescription(SdpType::kAnswer, modified_sdp);
  ASSERT_TRUE(pc->SetLocalDescription(std::move(modified_answer)));

  auto session_description = pc->CreateOffer();
  EXPECT_THAT(session_description->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri1"),
                          Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3"),
                          Field(&RtpExtension::uri, "uri4")));
}
TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       SdpMungingAnswerWithoutApiUsageEnablesExtensionsWithMemory) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  // Munging allowed: kRtpHeaderExtensionAdded (41)
  std::unique_ptr<PeerConnectionWrapper> pc =
      CreatePeerConnection(media_type, semantics,
                           "WebRTC-HeaderExtensionNegotiateMemory/Enabled/"
                           "WebRTC-NoSdpMangleAllowForTesting/Enabled,41/");
  std::string sdp =
      "v=0\r\n"
      "o=- 0 3 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "a=fingerprint:sha-256 "
      "A7:24:72:CA:6E:02:55:39:BA:66:DF:6E:CC:4C:D8:B0:1A:BF:1A:56:65:7D:F4:03:"
      "AD:7E:77:43:2A:29:EC:93\r\n"
      "a=ice-ufrag:6HHHdzzeIhkE0CKj\r\n"
      "a=ice-pwd:XYDGVpfvklQIEnZ6YnyLsAew\r\n";
  if (media_type == MediaType::AUDIO) {
    sdp +=
        "m=audio 9 RTP/AVPF 111\r\n"
        "a=rtpmap:111 fake_audio_codec/8000\r\n";
  } else {
    sdp +=
        "m=video 9 RTP/AVPF 111\r\n"
        "a=rtpmap:111 fake_video_codec/90000\r\n";
  }
  sdp +=
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtcp-mux\r\n"
      "a=sendrecv\r\n"
      "a=mid:audio\r\n"
      "a=setup:actpass\r\n"
      "a=extmap:1 uri1\r\n";
  std::unique_ptr<SessionDescriptionInterface> offer =
      CreateSessionDescription(SdpType::kOffer, sdp);
  pc->SetRemoteDescription(std::move(offer));
  std::unique_ptr<SessionDescriptionInterface> answer =
      pc->CreateAnswer(PeerConnectionInterface::RTCOfferAnswerOptions());
  std::string modified_sdp;
  ASSERT_TRUE(answer->ToString(&modified_sdp));
  modified_sdp += "a=extmap:1 uri1\r\n";
  auto modified_answer =
      CreateSessionDescription(SdpType::kAnswer, modified_sdp);
  ASSERT_TRUE(pc->SetLocalDescription(std::move(modified_answer)));

  auto session_description = pc->CreateOffer();
  // With memory enabled, next offer will only contain the extensions
  // that were agreed to in previous offer/answer.
  EXPECT_THAT(session_description->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri1")));
}

TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       SdpMungingOfferWithoutApiUsageEnablesExtensions) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  // Munging allowed: kRtpHeaderExtensionAdded (41)
  std::unique_ptr<PeerConnectionWrapper> pc = CreatePeerConnection(
      media_type, semantics, "WebRTC-NoSdpMangleAllowForTesting/Enabled,41/");
  pc->AddTransceiver(media_type);

  std::unique_ptr<SessionDescriptionInterface> offer =
      pc->CreateOffer(PeerConnectionInterface::RTCOfferAnswerOptions());
  std::string modified_sdp;
  ASSERT_TRUE(offer->ToString(&modified_sdp));
  modified_sdp += "a=extmap:1 uri1\r\n";
  auto modified_offer = CreateSessionDescription(SdpType::kOffer, modified_sdp);
  ASSERT_TRUE(pc->SetLocalDescription(std::move(modified_offer)));

  auto offer2 =
      pc->CreateOffer(PeerConnectionInterface::RTCOfferAnswerOptions());
  EXPECT_THAT(offer2->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3"),
                          Field(&RtpExtension::uri, "uri4"),
                          Field(&RtpExtension::uri, "uri1")));
}

TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       EnablingExtensionsAfterRemoteOfferWithoutHeaderExtensionMemory) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  std::unique_ptr<PeerConnectionWrapper> pc = CreatePeerConnection(
      media_type, semantics, "WebRTC-HeaderExtensionNegotiateMemory/Disabled/");
  std::string sdp =
      "v=0\r\n"
      "o=- 0 3 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "a=fingerprint:sha-256 "
      "A7:24:72:CA:6E:02:55:39:BA:66:DF:6E:CC:4C:D8:B0:1A:BF:1A:56:65:7D:F4:03:"
      "AD:7E:77:43:2A:29:EC:93\r\n"
      "a=ice-ufrag:6HHHdzzeIhkE0CKj\r\n"
      "a=ice-pwd:XYDGVpfvklQIEnZ6YnyLsAew\r\n";
  if (media_type == MediaType::AUDIO) {
    sdp +=
        "m=audio 9 RTP/AVPF 111\r\n"
        "a=rtpmap:111 fake_audio_codec/8000\r\n";
  } else {
    sdp +=
        "m=video 9 RTP/AVPF 111\r\n"
        "a=rtpmap:111 fake_video_codec/90000\r\n";
  }
  sdp +=
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtcp-mux\r\n"
      "a=sendrecv\r\n"
      "a=mid:audio\r\n"
      "a=setup:actpass\r\n"
      "a=extmap:5 uri1\r\n";
  std::unique_ptr<SessionDescriptionInterface> offer =
      CreateSessionDescription(SdpType::kOffer, sdp);
  pc->SetRemoteDescription(std::move(offer));

  ASSERT_GT(pc->pc()->GetTransceivers().size(), 0u);
  auto transceiver = pc->pc()->GetTransceivers()[0];
  auto modified_extensions = transceiver->GetHeaderExtensionsToNegotiate();
  modified_extensions[0].direction = RtpTransceiverDirection::kSendRecv;
  transceiver->SetHeaderExtensionsToNegotiate(modified_extensions);

  pc->CreateAnswerAndSetAsLocal(
      PeerConnectionInterface::RTCOfferAnswerOptions());

  auto session_description = pc->CreateOffer();
  auto extensions = session_description->description()
                        ->contents()[0]
                        .media_description()
                        ->rtp_header_extensions();
  EXPECT_THAT(extensions, ElementsAre(Field(&RtpExtension::uri, "uri1"),
                                      Field(&RtpExtension::uri, "uri2"),
                                      Field(&RtpExtension::uri, "uri3"),
                                      Field(&RtpExtension::uri, "uri4")));
  // Check uri1's id still matches the remote id.
  EXPECT_EQ(extensions[0].id, RtpHeaderExtensionId(5));
}

TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       EnablingExtensionsAfterRemoteOfferWithHeaderExtensionMemory) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  std::unique_ptr<PeerConnectionWrapper> pc = CreatePeerConnection(
      media_type, semantics, "WebRTC-HeaderExtensionNegotiateMemory/Enabled/");
  std::string sdp =
      "v=0\r\n"
      "o=- 0 3 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "a=fingerprint:sha-256 "
      "A7:24:72:CA:6E:02:55:39:BA:66:DF:6E:CC:4C:D8:B0:1A:BF:1A:56:65:7D:F4:03:"
      "AD:7E:77:43:2A:29:EC:93\r\n"
      "a=ice-ufrag:6HHHdzzeIhkE0CKj\r\n"
      "a=ice-pwd:XYDGVpfvklQIEnZ6YnyLsAew\r\n";
  if (media_type == MediaType::AUDIO) {
    sdp +=
        "m=audio 9 RTP/AVPF 111\r\n"
        "a=rtpmap:111 fake_audio_codec/8000\r\n";
  } else {
    sdp +=
        "m=video 9 RTP/AVPF 111\r\n"
        "a=rtpmap:111 fake_video_codec/90000\r\n";
  }
  sdp +=
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtcp-mux\r\n"
      "a=sendrecv\r\n"
      "a=mid:audio\r\n"
      "a=setup:actpass\r\n"
      "a=extmap:5 uri1\r\n";
  std::unique_ptr<SessionDescriptionInterface> offer =
      CreateSessionDescription(SdpType::kOffer, sdp);
  pc->SetRemoteDescription(std::move(offer));

  ASSERT_GT(pc->pc()->GetTransceivers().size(), 0u);
  auto transceiver = pc->pc()->GetTransceivers()[0];
  auto modified_extensions = transceiver->GetHeaderExtensionsToNegotiate();
  modified_extensions[0].direction = RtpTransceiverDirection::kSendRecv;
  transceiver->SetHeaderExtensionsToNegotiate(modified_extensions);

  pc->CreateAnswerAndSetAsLocal(
      PeerConnectionInterface::RTCOfferAnswerOptions());

  auto session_description = pc->CreateOffer();
  auto extensions = session_description->description()
                        ->contents()[0]
                        .media_description()
                        ->rtp_header_extensions();
  EXPECT_THAT(extensions, ElementsAre(Field(&RtpExtension::uri, "uri1")));
  // Check uri1's id still matches the remote id.
  EXPECT_EQ(extensions[0].id, RtpHeaderExtensionId(5));
}

TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       SenderParametersReflectNegotiation) {
  SdpSemantics semantics;
  MediaType media_type;
  std::tie(media_type, semantics) = GetParam();
  std::unique_ptr<PeerConnectionWrapper> pc1 =
      CreatePeerConnection(media_type, semantics);
  std::unique_ptr<PeerConnectionWrapper> pc2 =
      CreatePeerConnection(media_type, semantics);
  auto transceiver1 = pc1->AddTransceiver(media_type);
  // Before connection, sender sender_parameters should be empty.
  {
    auto sender_parameters = pc1->pc()->GetSenders()[0]->GetParameters();
    EXPECT_THAT(sender_parameters.header_extensions, IsEmpty());
  }

  std::unique_ptr<SessionDescriptionInterface> offer =
      pc1->CreateOfferAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());
  pc2->SetRemoteDescription(std::move(offer));
  std::unique_ptr<SessionDescriptionInterface> answer =
      pc2->CreateAnswerAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());
  pc1->SetRemoteDescription(std::move(answer));
  {
    auto sender_parameters = pc1->pc()->GetSenders()[0]->GetParameters();
    // We expect to see all send or sendrecv extensions from the answer.
    EXPECT_THAT(sender_parameters.header_extensions,
                UnorderedElementsAre(Field(&RtpExtension::uri, "uri4"),
                                     Field(&RtpExtension::uri, "uri2"),
                                     Field(&RtpExtension::uri, "uri3")));
  }
}

TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       TransceiversAddedAfterFirstDoNotCopy) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  std::unique_ptr<PeerConnectionWrapper> pc1 = CreatePeerConnection(
      media_type, semantics, "WebRTC-HeaderExtensionNegotiateMemory/Disabled/");

  auto transceiver1 = pc1->AddTransceiver(media_type);
  auto modified_extensions = transceiver1->GetHeaderExtensionsToNegotiate();
  modified_extensions[3].direction = RtpTransceiverDirection::kStopped;
  transceiver1->SetHeaderExtensionsToNegotiate(modified_extensions);
  auto transceiver2 = pc1->AddTransceiver(media_type);

  auto session_description = pc1->CreateOffer();
  EXPECT_THAT(session_description->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3")));
  EXPECT_THAT(session_description->description()
                  ->contents()[1]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3"),
                          Field(&RtpExtension::uri, "uri4")));
}

TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       TransceiversAddedAfterFirstTransceiverDoNotCopyExtensionsFromStopped) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  std::unique_ptr<PeerConnectionWrapper> pc1 = CreatePeerConnection(
      media_type, semantics, "WebRTC-HeaderExtensionNegotiateMemory/Enabled/");
  auto transceiver1 = pc1->AddTransceiver(media_type);
  auto modified_extensions = transceiver1->GetHeaderExtensionsToNegotiate();
  modified_extensions[3].direction = RtpTransceiverDirection::kStopped;
  transceiver1->SetHeaderExtensionsToNegotiate(modified_extensions);
  // Create a description with two sections, and set it as local.
  auto session_description = pc1->CreateOffer();
  pc1->SetLocalDescription(std::move(session_description));
  // Stop the transceiver. That should make it ignored for copying purposes.
  transceiver1->StopStandard();
  auto transceiver2 = pc1->AddTransceiver(media_type);
  session_description = pc1->CreateOffer();

  ASSERT_THAT(session_description->description()->contents().size(), Eq(2));
  EXPECT_THAT(session_description->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3")));
  // the uri4 extension is enabled in the newly added transceiver
  EXPECT_THAT(session_description->description()
                  ->contents()[1]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3"),
                          Field(&RtpExtension::uri, "uri4")));
}

TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       NegotiatingOffThenOnWorks) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  std::unique_ptr<PeerConnectionWrapper> pc1 = CreatePeerConnection(
      media_type, semantics, "WebRTC-HeaderExtensionNegotiateMemory/Enabled/");
  std::unique_ptr<PeerConnectionWrapper> pc2 = CreatePeerConnection(
      media_type, semantics, "WebRTC-HeaderExtensionNegotiateMemory/Enabled/");
  auto transceiver1 = pc1->AddTransceiver(media_type);
  auto modified_extensions = transceiver1->GetHeaderExtensionsToNegotiate();
  modified_extensions[3].direction = RtpTransceiverDirection::kStopped;
  transceiver1->SetHeaderExtensionsToNegotiate(modified_extensions);

  std::unique_ptr<SessionDescriptionInterface> offer =
      pc1->CreateOfferAndSetAsLocal();
  pc2->SetRemoteDescription(std::move(offer));
  std::unique_ptr<SessionDescriptionInterface> answer =
      pc2->CreateAnswerAndSetAsLocal();
  EXPECT_THAT(answer->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3")));
  pc1->SetRemoteDescription(std::move(answer));
  modified_extensions = transceiver1->GetHeaderExtensionsToNegotiate();
  EXPECT_THAT(modified_extensions[3].direction,
              Eq(RtpTransceiverDirection::kStopped));
  modified_extensions[3].direction = RtpTransceiverDirection::kSendRecv;
  transceiver1->SetHeaderExtensionsToNegotiate(modified_extensions);
  offer = pc1->CreateOfferAndSetAsLocal();
  EXPECT_THAT(offer->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3"),
                          Field(&RtpExtension::uri, "uri4")));
  pc2->SetRemoteDescription(std::move(offer));
  answer = pc2->CreateAnswerAndSetAsLocal();
  EXPECT_THAT(answer->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3"),
                          Field(&RtpExtension::uri, "uri4")));
  pc1->SetRemoteDescription(std::move(answer));
}

TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       EncryptedHeaderExtensionsWorkWhenMemoryEnabled) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  std::unique_ptr<PeerConnectionWrapper> pc1 = CreatePeerConnection(
      media_type, semantics, "WebRTC-HeaderExtensionNegotiateMemory/Enabled/");
  std::unique_ptr<PeerConnectionWrapper> pc2 = CreatePeerConnection(
      media_type, semantics, "WebRTC-HeaderExtensionNegotiateMemory/Enabled/");
  auto transceiver1 = pc1->AddTransceiver(media_type);
  auto modified_extensions = transceiver1->GetHeaderExtensionsToNegotiate();
  // this just verifies that setup is correct
  ASSERT_THAT(modified_extensions[4].preferred_encrypt, Eq(true));
  ASSERT_THAT(modified_extensions[4].uri, Eq("encrypted_uri"));
  modified_extensions[4].direction = RtpTransceiverDirection::kSendRecv;
  transceiver1->SetHeaderExtensionsToNegotiate(modified_extensions);

  std::unique_ptr<SessionDescriptionInterface> offer =
      pc1->CreateOfferAndSetAsLocal();
  EXPECT_THAT(offer->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              Contains(Field(&RtpExtension::uri, "encrypted_uri")));
  pc2->SetRemoteDescription(std::move(offer));
  EXPECT_THAT(pc2->pc()
                  ->remote_description()
                  ->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              Contains(Field(&RtpExtension::uri, "encrypted_uri")));
  auto answerer_extensions =
      pc2->pc()->GetTransceivers()[0]->GetHeaderExtensionsToNegotiate();
  std::unique_ptr<SessionDescriptionInterface> answer =
      pc2->CreateAnswerAndSetAsLocal();
  EXPECT_THAT(answer->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              Contains(Field("uri", &RtpExtension::uri, "encrypted_uri")));
  pc1->SetRemoteDescription(std::move(answer));
  modified_extensions = transceiver1->GetNegotiatedHeaderExtensions();
  EXPECT_THAT(modified_extensions[4].direction,
              Eq(RtpTransceiverDirection::kSendRecv));
}

TEST_P(PeerConnectionHeaderExtensionUnifiedPlanTest,
       MandatoryExtensionRemainsNegotiableAfterAnswerWithoutIt) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  extensions_.push_back(RtpHeaderExtensionCapability(
      RtpExtension::kMidUri, RtpHeaderExtensionId(6),
      RtpTransceiverDirection::kSendRecv));
  std::unique_ptr<PeerConnectionWrapper> pc1 = CreatePeerConnection(
      media_type, semantics, "WebRTC-HeaderExtensionNegotiateMemory/Enabled/");
  std::unique_ptr<PeerConnectionWrapper> pc2 = CreatePeerConnection(
      media_type, semantics, "WebRTC-HeaderExtensionNegotiateMemory/Enabled/");
  auto transceiver1 = pc1->AddTransceiver(media_type);

  pc2->SetRemoteDescription(pc1->CreateOfferAndSetAsLocal());
  std::unique_ptr<SessionDescriptionInterface> answer =
      pc2->CreateAnswerAndSetAsLocal();
  MediaContentDescription* answer_media =
      answer->description()->contents()[0].media_description();
  RtpHeaderExtensions answer_extensions = answer_media->rtp_header_extensions();
  std::erase_if(answer_extensions, [](const RtpExtension& extension) {
    return extension.uri == RtpExtension::kMidUri;
  });
  answer_media->set_rtp_header_extensions(answer_extensions);
  ASSERT_TRUE(pc1->SetRemoteDescription(std::move(answer)));

  EXPECT_THAT(transceiver1->GetNegotiatedHeaderExtensions(),
              Contains(AllOf(Field(&RtpHeaderExtensionCapability::uri,
                                   RtpExtension::kMidUri),
                             Field(&RtpHeaderExtensionCapability::direction,
                                   RtpTransceiverDirection::kStopped))));
  auto to_negotiate = transceiver1->GetHeaderExtensionsToNegotiate();
  EXPECT_THAT(to_negotiate,
              Contains(AllOf(Field(&RtpHeaderExtensionCapability::uri,
                                   RtpExtension::kMidUri),
                             Field(&RtpHeaderExtensionCapability::direction,
                                   RtpTransceiverDirection::kSendRecv))));
  EXPECT_TRUE(transceiver1->SetHeaderExtensionsToNegotiate(to_negotiate).ok());

  auto transceiver2 = pc1->AddTransceiver(media_type);
  EXPECT_TRUE(transceiver2
                  ->SetHeaderExtensionsToNegotiate(
                      transceiver2->GetHeaderExtensionsToNegotiate())
                  .ok());
}

INSTANTIATE_TEST_SUITE_P(
    ,
    PeerConnectionHeaderExtensionTest,
    Combine(Values(SdpSemantics::kPlanB_DEPRECATED, SdpSemantics::kUnifiedPlan),
            Values(MediaType::AUDIO, MediaType::VIDEO)),
    [](const ::testing::TestParamInfo<
        PeerConnectionHeaderExtensionTest::ParamType>& info) {
      MediaType media_type;
      SdpSemantics semantics;
      std::tie(media_type, semantics) = info.param;
      return (StringBuilder("With")
              << (semantics == SdpSemantics::kPlanB_DEPRECATED ? "PlanB"
                                                               : "UnifiedPlan")
              << "And" << (media_type == MediaType::AUDIO ? "Voice" : "Video")
              << "Engine")
          .str();
    });

INSTANTIATE_TEST_SUITE_P(
    ,
    PeerConnectionHeaderExtensionUnifiedPlanTest,
    Combine(Values(MediaType::AUDIO, MediaType::VIDEO),
            Values(SdpSemantics::kUnifiedPlan)),
    [](const ::testing::TestParamInfo<
        PeerConnectionHeaderExtensionUnifiedPlanTest::ParamType>& info) {
      MediaType media_type;
      SdpSemantics semantics;
      std::tie(media_type, semantics) = info.param;
      return (StringBuilder("With")
              << (semantics == SdpSemantics::kPlanB_DEPRECATED ? "PlanB"
                                                               : "UnifiedPlan")
              << "And" << (media_type == MediaType::AUDIO ? "Voice" : "Video")
              << "Engine")
          .str();
    });

}  // namespace webrtc
