/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// This file contains tests that verify that field trials do what they're
// supposed to do.

#include <memory>
#include <set>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/strings/string_view.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/enable_media_with_defaults.h"
#include "api/environment/environment_factory.h"
#include "api/jsep.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "pc/peer_connection_wrapper.h"
#include "pc/session_description.h"
#include "pc/test/fake_audio_capture_module.h"
#include "pc/test/mock_peer_connection_observers.h"
#include "rtc_base/checks.h"
#include "rtc_base/internal/default_socket_server.h"
#include "rtc_base/socket_server.h"
#include "rtc_base/thread.h"
#include "system_wrappers/include/clock.h"
#include "test/create_test_field_trials.h"
#include "test/gtest.h"
#include "test/run_loop.h"

namespace webrtc {

using RTCConfiguration = PeerConnectionInterface::RTCConfiguration;

class PeerConnectionFieldTrialTest : public ::testing::Test {
 protected:
  typedef std::unique_ptr<PeerConnectionWrapper> WrapperPtr;

  PeerConnectionFieldTrialTest()
      : clock_(Clock::GetRealTimeClock()),
        socket_server_(CreateDefaultSocketServer()),
        main_thread_(socket_server_.get()) {
    PeerConnectionInterface::IceServer ice_server;
    ice_server.uri = "stun:stun.l.google.com:19302";
    config_.servers.push_back(ice_server);
    config_.sdp_semantics = SdpSemantics::kUnifiedPlan;
  }

  void TearDown() override { pc_factory_ = nullptr; }

  void CreatePCFactory(absl::string_view field_trials) {
    PeerConnectionFactoryDependencies pcf_deps;
    pcf_deps.signaling_thread = Thread::Current();
    pcf_deps.env = CreateEnvironment(CreateTestFieldTrialsPtr(field_trials));
    pcf_deps.adm = FakeAudioCaptureModule::Create();
    EnableMediaWithDefaults(pcf_deps);
    pc_factory_ = CreateModularPeerConnectionFactory(std::move(pcf_deps));

    // Allow ADAPTER_TYPE_LOOPBACK to create PeerConnections with loopback in
    // this test.
    RTC_DCHECK(pc_factory_);
    PeerConnectionFactoryInterface::Options options;
    options.network_ignore_mask = 0;
    pc_factory_->SetOptions(options);
  }

  WrapperPtr CreatePeerConnection() {
    auto observer = std::make_unique<MockPeerConnectionObserver>();
    auto result = pc_factory_->CreatePeerConnectionOrError(
        config_, PeerConnectionDependencies(observer.get()));
    RTC_CHECK(result.ok());

    observer->SetPeerConnectionInterface(result.value().get());
    return std::make_unique<PeerConnectionWrapper>(
        pc_factory_, result.MoveValue(), std::move(observer));
  }

  Clock* const clock_;
  std::unique_ptr<SocketServer> socket_server_;
  test::RunLoop main_thread_;
  scoped_refptr<PeerConnectionFactoryInterface> pc_factory_ = nullptr;
  PeerConnectionInterface::RTCConfiguration config_;
};

// Tests for the dependency descriptor field trial. The dependency descriptor
// field trial is implemented in media/engine/webrtc_video_engine.cc.
TEST_F(PeerConnectionFieldTrialTest, EnableDependencyDescriptorAdvertised) {
  CreatePCFactory("WebRTC-DependencyDescriptorAdvertised/Enabled/");

  WrapperPtr caller = CreatePeerConnection();
  caller->AddTransceiver(MediaType::VIDEO);

  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  auto contents1 = offer->description()->contents();
  ASSERT_EQ(1u, contents1.size());

  const MediaContentDescription* media_description1 =
      contents1[0].media_description();
  EXPECT_EQ(MediaType::VIDEO, media_description1->type());
  const RtpHeaderExtensions& rtp_header_extensions1 =
      media_description1->rtp_header_extensions();

  bool found =
      absl::c_find_if(
          rtp_header_extensions1, [](const RtpExtension& rtp_extension) {
            return rtp_extension.uri == RtpExtension::kDependencyDescriptorUri;
          }) != rtp_header_extensions1.end();
  EXPECT_TRUE(found);
}

// Tests that dependency descriptor RTP header extensions can be exchanged
// via SDP munging, even if dependency descriptor field trial is disabled.
#ifdef WEBRTC_WIN
// TODO: crbug.com/webrtc/15876 - Test is flaky on Windows machines.
#define MAYBE_InjectDependencyDescriptor DISABLED_InjectDependencyDescriptor
#else
#define MAYBE_InjectDependencyDescriptor InjectDependencyDescriptor
#endif
TEST_F(PeerConnectionFieldTrialTest, MAYBE_InjectDependencyDescriptor) {
  CreatePCFactory("WebRTC-DependencyDescriptorAdvertised/Disabled/");

  WrapperPtr caller = CreatePeerConnection();
  WrapperPtr callee = CreatePeerConnection();
  caller->AddTransceiver(MediaType::VIDEO);

  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  ContentInfos& contents1 = offer->description()->contents();
  ASSERT_EQ(1u, contents1.size());

  MediaContentDescription* media_description1 =
      contents1[0].media_description();
  EXPECT_EQ(MediaType::VIDEO, media_description1->type());
  RtpHeaderExtensions rtp_header_extensions1 =
      media_description1->rtp_header_extensions();

  bool found1 =
      absl::c_find_if(
          rtp_header_extensions1, [](const RtpExtension& rtp_extension) {
            return rtp_extension.uri == RtpExtension::kDependencyDescriptorUri;
          }) != rtp_header_extensions1.end();
  EXPECT_FALSE(found1);

  std::set<int> existing_ids;
  for (const RtpExtension& rtp_extension : rtp_header_extensions1) {
    existing_ids.insert(rtp_extension.id.value());
  }

  // Find the currently unused RTP header extension ID.
  int insert_id = 1;
  std::set<int>::const_iterator iter = existing_ids.begin();
  while (true) {
    if (iter == existing_ids.end()) {
      break;
    }
    if (*iter != insert_id) {
      break;
    }
    insert_id++;
    iter++;
  }

  rtp_header_extensions1.emplace_back(RtpExtension::kDependencyDescriptorUri,
                                      RtpHeaderExtensionId(insert_id));
  media_description1->set_rtp_header_extensions(rtp_header_extensions1);

  caller->SetLocalDescription(offer->Clone());

  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));
  std::unique_ptr<SessionDescriptionInterface> answer = callee->CreateAnswer();

  ContentInfos& contents2 = answer->description()->contents();
  ASSERT_EQ(1u, contents2.size());

  MediaContentDescription* media_description2 =
      contents2[0].media_description();
  EXPECT_EQ(MediaType::VIDEO, media_description2->type());
  RtpHeaderExtensions rtp_header_extensions2 =
      media_description2->rtp_header_extensions();

  bool found2 =
      absl::c_find_if(
          rtp_header_extensions2, [](const RtpExtension& rtp_extension) {
            return rtp_extension.uri == RtpExtension::kDependencyDescriptorUri;
          }) != rtp_header_extensions2.end();
  EXPECT_TRUE(found2);
}

}  // namespace webrtc
