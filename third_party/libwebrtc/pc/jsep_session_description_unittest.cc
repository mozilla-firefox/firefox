/*
 *  Copyright 2012 The WebRTC project authors. All Rights Reserved.
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
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "api/candidate.h"
#include "api/jsep.h"
#include "api/webrtc_sdp.h"
#include "media/base/codec.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/transport_info.h"
#include "pc/session_description.h"
#include "rtc_base/crypto_random.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/task_queue_for_test.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

using ::testing::NotNull;
using ::testing::Values;

namespace {
constexpr char kAudioMid[] = "audio";
constexpr char kVideoMid[] = "video";
constexpr char kCandidateUfrag[] = "ufrag";
constexpr char kCandidatePwd[] = "pwd";
constexpr char kCandidateUfragVoice[] = "ufrag_voice";
constexpr char kCandidatePwdVoice[] = "pwd_voice";
constexpr char kCandidateUfragVideo[] = "ufrag_video";
constexpr char kCandidatePwdVideo[] = "pwd_video";
constexpr char kCandidateFoundation[] = "a0+B/1";
constexpr uint32_t kCandidatePriority = 2130706432U;  // pref = 1.0
constexpr uint32_t kCandidateGeneration = 2;

// This creates a session description with both audio and video media contents.
// In SDP this is described by two m lines, one audio and one video.
std::unique_ptr<SessionDescription> CreateCricketSessionDescription() {
  auto desc = std::make_unique<SessionDescription>();

  // AudioContentDescription
  auto audio = std::make_unique<AudioContentDescription>();
  // VideoContentDescription
  auto video = std::make_unique<VideoContentDescription>();

  audio->AddCodec(CreateAudioCodec(103, "ISAC", 16000, 0));
  desc->AddContent(kAudioMid, MediaProtocolType::kRtp, std::move(audio));

  video->AddCodec(CreateVideoCodec(120, "VP8"));
  desc->AddContent(kVideoMid, MediaProtocolType::kRtp, std::move(video));

  desc->AddTransportInfo(TransportInfo(
      kAudioMid,
      TransportDescription(std::vector<std::string>(), kCandidateUfragVoice,
                           kCandidatePwdVoice, ICEMODE_FULL,
                           CONNECTIONROLE_NONE, nullptr)));
  desc->AddTransportInfo(TransportInfo(
      kVideoMid,
      TransportDescription(std::vector<std::string>(), kCandidateUfragVideo,
                           kCandidatePwdVideo, ICEMODE_FULL,
                           CONNECTIONROLE_NONE, nullptr)));
  return desc;
}

}  // namespace

class JsepSessionDescriptionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    int port = 1234;
    SocketAddress address("127.0.0.1", port++);
    Candidate candidate(ICE_CANDIDATE_COMPONENT_RTP, "udp", address, 1, "", "",
                        IceCandidateType::kHost, 0, "1");
    candidate_ = candidate;
    const std::string session_id = absl::StrCat(CreateRandomId64());
    const std::string session_version = absl::StrCat(CreateRandomId());
    jsep_desc_ =
        CreateSessionDescription(SdpType::kOffer, session_id, session_version,
                                 CreateCricketSessionDescription());
  }

  std::string Serialize(const SessionDescriptionInterface* desc) {
    std::string sdp = desc->ToString();
    EXPECT_FALSE(sdp.empty());
    return sdp;
  }

  std::unique_ptr<SessionDescriptionInterface> DeSerialize(
      const std::string& sdp) {
    std::unique_ptr<SessionDescriptionInterface> jsep_desc =
        SdpDeserialize(SdpType::kOffer, sdp);
    EXPECT_THAT(jsep_desc, NotNull());
    return jsep_desc;
  }

  Candidate candidate_;
  std::unique_ptr<SessionDescriptionInterface> jsep_desc_;
};

TEST_F(JsepSessionDescriptionTest, CloneDefault) {
  auto new_desc = jsep_desc_->Clone();
  EXPECT_EQ(jsep_desc_->GetType(), new_desc->GetType());
  std::string old_desc_string;
  std::string new_desc_string;
  EXPECT_TRUE(jsep_desc_->ToString(&old_desc_string));
  EXPECT_TRUE(new_desc->ToString(&new_desc_string));
  EXPECT_EQ(old_desc_string, new_desc_string);
  EXPECT_EQ(jsep_desc_->session_id(), new_desc->session_id());
  EXPECT_EQ(jsep_desc_->session_version(), new_desc->session_version());
}

TEST_F(JsepSessionDescriptionTest, CloneRollback) {
  auto jsep_desc = CreateRollbackSessionDescription(
      absl::StrCat(CreateRandomId64()), absl::StrCat(CreateRandomId64()));
  EXPECT_EQ(jsep_desc->GetType(), SdpType::kRollback);
  auto new_desc = jsep_desc->Clone();
  EXPECT_EQ(jsep_desc->GetType(), new_desc->GetType());
  EXPECT_EQ(jsep_desc->session_id(), new_desc->session_id());
  EXPECT_EQ(jsep_desc->session_version(), new_desc->session_version());
}

TEST_F(JsepSessionDescriptionTest, CloneWithCandidates) {
  Candidate candidate_v4(ICE_CANDIDATE_COMPONENT_RTP, "udp",
                         SocketAddress("192.168.1.5", 1234), kCandidatePriority,
                         "", "", IceCandidateType::kSrflx, kCandidateGeneration,
                         kCandidateFoundation);
  Candidate candidate_v6(ICE_CANDIDATE_COMPONENT_RTP, "udp",
                         SocketAddress("::1", 1234), kCandidatePriority, "", "",
                         IceCandidateType::kHost, kCandidateGeneration,
                         kCandidateFoundation);

  IceCandidate jice_v4("audio", 0, candidate_v4);
  IceCandidate jice_v6("audio", 0, candidate_v6);
  IceCandidate jice_v4_video("video", 0, candidate_v4);
  IceCandidate jice_v6_video("video", 0, candidate_v6);
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice_v4));
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice_v6));
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice_v4_video));
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice_v6_video));
  auto new_desc = jsep_desc_->Clone();
  EXPECT_EQ(jsep_desc_->GetType(), new_desc->GetType());
  ASSERT_EQ(jsep_desc_->number_of_mediasections(),
            new_desc->number_of_mediasections());
  for (size_t i = 0; i < jsep_desc_->number_of_mediasections(); ++i) {
    const IceCandidateCollection* old_collection = jsep_desc_->candidates(i);
    const IceCandidateCollection* new_collection = new_desc->candidates(i);
    ASSERT_EQ(old_collection->count(), new_collection->count());
    for (size_t j = 0; j < old_collection->count(); ++j) {
      const IceCandidate* old_candidate = old_collection->at(j);
      const IceCandidate* new_candidate = new_collection->at(j);
      EXPECT_EQ(old_candidate->ToString(), new_candidate->ToString());
    }
  }
  std::string old_desc_string;
  std::string new_desc_string;
  EXPECT_TRUE(jsep_desc_->ToString(&old_desc_string));
  EXPECT_TRUE(new_desc->ToString(&new_desc_string));
  EXPECT_EQ(old_desc_string, new_desc_string);
}

// Test that number_of_mediasections() returns the number of media contents in
// a session description.
TEST_F(JsepSessionDescriptionTest, CheckSessionDescription) {
  EXPECT_EQ(2u, jsep_desc_->number_of_mediasections());
}

TEST_F(JsepSessionDescriptionTest, IsValidMLineIndex) {
  ASSERT_GT(jsep_desc_->number_of_mediasections(), 0u);
  // Create a candidate with no mid and an illegal index.
  IceCandidate ice_candidate("", -1, candidate_);
  EXPECT_FALSE(jsep_desc_->AddCandidate(&ice_candidate));
  IceCandidate ice_candidate2("", jsep_desc_->number_of_mediasections(),
                              candidate_);
  EXPECT_FALSE(jsep_desc_->AddCandidate(&ice_candidate2));
}

// Test that we can add a candidate to a session description without MID.
TEST_F(JsepSessionDescriptionTest, AddCandidateWithoutMid) {
  IceCandidate jsep_candidate("", 0, candidate_);
  EXPECT_TRUE(jsep_desc_->AddCandidate(&jsep_candidate));
  const IceCandidateCollection* ice_candidates = jsep_desc_->candidates(0);
  ASSERT_TRUE(ice_candidates != nullptr);
  EXPECT_EQ(1u, ice_candidates->count());
  const IceCandidate* ice_candidate = ice_candidates->at(0);
  ASSERT_TRUE(ice_candidate != nullptr);
  candidate_.set_username(kCandidateUfragVoice);
  candidate_.set_password(kCandidatePwdVoice);
  EXPECT_TRUE(ice_candidate->candidate().IsEquivalent(candidate_));
  EXPECT_EQ(0, ice_candidate->sdp_mline_index());
  EXPECT_EQ("audio", ice_candidate->sdp_mid());
  EXPECT_EQ(0u, jsep_desc_->candidates(1)->count());
}

// Test that we can add and remove candidates to a session description with
// MID. Removing candidates requires MID.
TEST_F(JsepSessionDescriptionTest, AddAndRemoveIceCandidatesWithMid) {
  // mid and m-line index don't match, in this case mid is preferred.
  std::string mid = "video";
  IceCandidate jsep_candidate(mid, 0, candidate_);
  EXPECT_TRUE(jsep_desc_->AddCandidate(&jsep_candidate));
  EXPECT_EQ(0u, jsep_desc_->candidates(0)->count());
  const IceCandidateCollection* ice_candidates = jsep_desc_->candidates(1);
  ASSERT_THAT(ice_candidates, NotNull());
  EXPECT_EQ(1u, ice_candidates->count());
  const IceCandidate* ice_candidate = ice_candidates->at(0);
  ASSERT_THAT(ice_candidate, NotNull());
  candidate_.set_username(kCandidateUfragVideo);
  candidate_.set_password(kCandidatePwdVideo);
  EXPECT_TRUE(ice_candidate->candidate().IsEquivalent(candidate_));
  // The mline index should have been updated according to mid.
  EXPECT_EQ(1, ice_candidate->sdp_mline_index());

  EXPECT_EQ(1u, jsep_desc_->RemoveCandidate(ice_candidate));
  EXPECT_EQ(0u, jsep_desc_->candidates(0)->count());
  EXPECT_EQ(0u, jsep_desc_->candidates(1)->count());
}

// TODO(bugs.webrtc.org/8395): Remove this test and leave
// AddAndRemoveIceCandidatesWithMid.
TEST_F(JsepSessionDescriptionTest, AddAndRemoveCandidatesWithMid) {
  // mid and m-line index don't match, in this case mid is preferred.
  std::string mid = "video";
  IceCandidate jsep_candidate(mid, 0, candidate_);
  EXPECT_TRUE(jsep_desc_->AddCandidate(&jsep_candidate));
  EXPECT_EQ(0u, jsep_desc_->candidates(0)->count());
  const IceCandidateCollection* ice_candidates = jsep_desc_->candidates(1);
  ASSERT_TRUE(ice_candidates != nullptr);
  EXPECT_EQ(1u, ice_candidates->count());
  const IceCandidate* ice_candidate = ice_candidates->at(0);
  ASSERT_TRUE(ice_candidate != nullptr);
  candidate_.set_username(kCandidateUfragVideo);
  candidate_.set_password(kCandidatePwdVideo);
  EXPECT_TRUE(ice_candidate->candidate().IsEquivalent(candidate_));
  // The mline index should have been updated according to mid.
  EXPECT_EQ(1, ice_candidate->sdp_mline_index());

  EXPECT_EQ(1u, jsep_desc_->RemoveCandidate(ice_candidate));
  EXPECT_EQ(0u, jsep_desc_->candidates(0)->count());
  EXPECT_EQ(0u, jsep_desc_->candidates(1)->count());
}

TEST_F(JsepSessionDescriptionTest, AddCandidateAlreadyHasUfrag) {
  candidate_.set_username(kCandidateUfrag);
  candidate_.set_password(kCandidatePwd);
  IceCandidate jsep_candidate("audio", 0, candidate_);
  EXPECT_TRUE(jsep_desc_->AddCandidate(&jsep_candidate));
  const IceCandidateCollection* ice_candidates = jsep_desc_->candidates(0);
  ASSERT_TRUE(ice_candidates != nullptr);
  EXPECT_EQ(1u, ice_candidates->count());
  const IceCandidate* ice_candidate = ice_candidates->at(0);
  ASSERT_TRUE(ice_candidate != nullptr);
  candidate_.set_username(kCandidateUfrag);
  candidate_.set_password(kCandidatePwd);
  EXPECT_TRUE(ice_candidate->candidate().IsEquivalent(candidate_));

  EXPECT_EQ(0u, jsep_desc_->candidates(1)->count());
}

// Test that we can not add a candidate if there is no corresponding media
// content in the session description.
TEST_F(JsepSessionDescriptionTest, AddBadCandidate) {
  IceCandidate bad_candidate1("", 55, candidate_);
  EXPECT_FALSE(jsep_desc_->AddCandidate(&bad_candidate1));

  IceCandidate bad_candidate2("some weird mid", 0, candidate_);
  EXPECT_FALSE(jsep_desc_->AddCandidate(&bad_candidate2));
}

// Tests that repeatedly adding the same candidate, with or without credentials,
// does not increase the number of candidates in the description.
TEST_F(JsepSessionDescriptionTest, AddCandidateDuplicates) {
  IceCandidate jsep_candidate("", 0, candidate_);
  EXPECT_TRUE(jsep_desc_->AddCandidate(&jsep_candidate));
  EXPECT_EQ(1u, jsep_desc_->candidates(0)->count());

  // Add the same candidate again.  It should be ignored.
  EXPECT_TRUE(jsep_desc_->AddCandidate(&jsep_candidate));
  EXPECT_EQ(1u, jsep_desc_->candidates(0)->count());

  // Create a new candidate, identical except that the ufrag and pwd are now
  // populated.
  candidate_.set_username(kCandidateUfragVoice);
  candidate_.set_password(kCandidatePwdVoice);
  IceCandidate jsep_candidate_with_credentials("", 0, candidate_);

  // This should also be identified as redundant and ignored.
  EXPECT_TRUE(jsep_desc_->AddCandidate(&jsep_candidate_with_credentials));
  EXPECT_EQ(1u, jsep_desc_->candidates(0)->count());
}

// Test that the connection address is set to a hostname address after adding a
// hostname candidate.
TEST_F(JsepSessionDescriptionTest, AddHostnameCandidate) {
  Candidate c;
  c.set_component(ICE_CANDIDATE_COMPONENT_RTP);
  c.set_protocol(UDP_PROTOCOL_NAME);
  c.set_address(SocketAddress("example.local", 1234));
  c.set_type(IceCandidateType::kHost);
  const size_t audio_index = 0;
  IceCandidate hostname_candidate("audio", audio_index, c);
  EXPECT_TRUE(jsep_desc_->AddCandidate(&hostname_candidate));

  ASSERT_NE(nullptr, jsep_desc_->description());
  ASSERT_EQ(2u, jsep_desc_->description()->contents().size());
  const auto& content = jsep_desc_->description()->contents()[audio_index];
  EXPECT_EQ("0.0.0.0:9",
            content.media_description()->connection_address().ToString());
}

// Test that we can serialize a JsepSessionDescription and deserialize it again.
TEST_F(JsepSessionDescriptionTest, SerializeDeserialize) {
  std::string sdp = Serialize(jsep_desc_.get());

  auto parsed_jsep_desc = DeSerialize(sdp);
  EXPECT_EQ(2u, parsed_jsep_desc->number_of_mediasections());

  std::string parsed_sdp = Serialize(parsed_jsep_desc.get());
  EXPECT_EQ(sdp, parsed_sdp);
}

// Test that we can serialize a JsepSessionDescription when a hostname candidate
// is the default destination and deserialize it again. The connection address
// in the deserialized description should be the dummy address 0.0.0.0:9.
TEST_F(JsepSessionDescriptionTest, SerializeDeserializeWithHostnameCandidate) {
  Candidate c;
  c.set_component(ICE_CANDIDATE_COMPONENT_RTP);
  c.set_protocol(UDP_PROTOCOL_NAME);
  c.set_address(SocketAddress("example.local", 1234));
  c.set_type(IceCandidateType::kHost);
  const size_t audio_index = 0;
  const size_t video_index = 1;
  IceCandidate hostname_candidate_audio("audio", audio_index, c);
  IceCandidate hostname_candidate_video("video", video_index, c);
  EXPECT_TRUE(jsep_desc_->AddCandidate(&hostname_candidate_audio));
  EXPECT_TRUE(jsep_desc_->AddCandidate(&hostname_candidate_video));

  std::string sdp = Serialize(jsep_desc_.get());

  auto parsed_jsep_desc = DeSerialize(sdp);
  EXPECT_EQ(2u, parsed_jsep_desc->number_of_mediasections());

  ASSERT_NE(nullptr, parsed_jsep_desc->description());
  ASSERT_EQ(2u, parsed_jsep_desc->description()->contents().size());
  const auto& audio_content =
      parsed_jsep_desc->description()->contents()[audio_index];
  const auto& video_content =
      parsed_jsep_desc->description()->contents()[video_index];
  EXPECT_EQ("0.0.0.0:9",
            audio_content.media_description()->connection_address().ToString());
  EXPECT_EQ("0.0.0.0:9",
            video_content.media_description()->connection_address().ToString());
}

// Tests that we can serialize and deserialize a JsepSesssionDescription
// with candidates.
TEST_F(JsepSessionDescriptionTest, SerializeDeserializeWithCandidates) {
  std::string sdp = Serialize(jsep_desc_.get());

  // Add a candidate and check that the serialized result is different.
  IceCandidate jsep_candidate("audio", 0, candidate_);
  EXPECT_TRUE(jsep_desc_->AddCandidate(&jsep_candidate));
  std::string sdp_with_candidate = Serialize(jsep_desc_.get());
  EXPECT_NE(sdp, sdp_with_candidate);

  auto parsed_jsep_desc = DeSerialize(sdp_with_candidate);
  std::string parsed_sdp_with_candidate = Serialize(parsed_jsep_desc.get());

  EXPECT_EQ(sdp_with_candidate, parsed_sdp_with_candidate);
}

// Tests that a parsed description, with or without candidates, can be used on
// another thread than the one that parsed it. E.g. Chromium parses SDP on the
// main thread and then clones the description on the signaling thread.
TEST_F(JsepSessionDescriptionTest, CloneOnAnotherThreadAfterDeserialize) {
  std::unique_ptr<SessionDescriptionInterface> without_candidates =
      DeSerialize(Serialize(jsep_desc_.get()));
  IceCandidate jsep_candidate(kAudioMid, 0, candidate_);
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jsep_candidate));
  std::unique_ptr<SessionDescriptionInterface> with_candidates =
      DeSerialize(Serialize(jsep_desc_.get()));
  ASSERT_THAT(without_candidates, NotNull());
  ASSERT_THAT(with_candidates, NotNull());

  TaskQueueForTest queue;
  queue.SendTask([&] {
    EXPECT_EQ(Serialize(without_candidates->Clone().get()),
              Serialize(without_candidates.get()));
    EXPECT_EQ(Serialize(with_candidates->Clone().get()),
              Serialize(with_candidates.get()));
  });
}

// TODO(zhihuang): Modify these tests. These are used to verify that after
// adding the candidates, the connection_address field is set correctly. Modify
// those so that the "connection address" is tested directly.
// Tests serialization of SDP with only IPv6 candidates and verifies that IPv6
// is used as default address in c line according to preference.
TEST_F(JsepSessionDescriptionTest, SerializeSessionDescriptionWithIPv6Only) {
  // Stun has a high preference than local host.
  Candidate candidate1(ICE_CANDIDATE_COMPONENT_RTP, "udp",
                       SocketAddress("::1", 1234), kCandidatePriority, "", "",
                       IceCandidateType::kSrflx, kCandidateGeneration,
                       kCandidateFoundation);
  Candidate candidate2(ICE_CANDIDATE_COMPONENT_RTP, "udp",
                       SocketAddress("::2", 1235), kCandidatePriority, "", "",
                       IceCandidateType::kHost, kCandidateGeneration,
                       kCandidateFoundation);

  IceCandidate jice1("audio", 0, candidate1);
  IceCandidate jice2("audio", 0, candidate2);
  IceCandidate jice3("video", 0, candidate1);
  IceCandidate jice4("video", 0, candidate2);
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice1));
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice2));
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice3));
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice4));
  std::string message = Serialize(jsep_desc_.get());

  // Should have a c line like this one.
  EXPECT_NE(message.find("c=IN IP6 ::1"), std::string::npos);
  // Shouldn't have a IP4 c line.
  EXPECT_EQ(message.find("c=IN IP4"), std::string::npos);
}

// Tests serialization of SDP with both IPv4 and IPv6 candidates and
// verifies that IPv4 is used as default address in c line even if the
// preference of IPv4 is lower.
TEST_F(JsepSessionDescriptionTest,
       SerializeSessionDescriptionWithBothIPFamilies) {
  Candidate candidate_v4(ICE_CANDIDATE_COMPONENT_RTP, "udp",
                         SocketAddress("192.168.1.5", 1234), kCandidatePriority,
                         "", "", IceCandidateType::kSrflx, kCandidateGeneration,
                         kCandidateFoundation);
  Candidate candidate_v6(ICE_CANDIDATE_COMPONENT_RTP, "udp",
                         SocketAddress("::1", 1234), kCandidatePriority, "", "",
                         IceCandidateType::kHost, kCandidateGeneration,
                         kCandidateFoundation);

  IceCandidate jice_v4("audio", 0, candidate_v4);
  IceCandidate jice_v6("audio", 0, candidate_v6);
  IceCandidate jice_v4_video("video", 0, candidate_v4);
  IceCandidate jice_v6_video("video", 0, candidate_v6);
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice_v4));
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice_v6));
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice_v4_video));
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice_v6_video));
  std::string message = Serialize(jsep_desc_.get());

  // Should have a c line like this one.
  EXPECT_NE(message.find("c=IN IP4 192.168.1.5"), std::string::npos);
  // Shouldn't have a IP6 c line.
  EXPECT_EQ(message.find("c=IN IP6"), std::string::npos);
}

// Tests serialization of SDP with both UDP and TCP candidates and
// verifies that UDP is used as default address in c line even if the
// preference of UDP is lower.
TEST_F(JsepSessionDescriptionTest,
       SerializeSessionDescriptionWithBothProtocols) {
  // Stun has a high preference than local host.
  Candidate candidate1(ICE_CANDIDATE_COMPONENT_RTP, "tcp",
                       SocketAddress("::1", 1234), kCandidatePriority, "", "",
                       IceCandidateType::kSrflx, kCandidateGeneration,
                       kCandidateFoundation);
  Candidate candidate2(ICE_CANDIDATE_COMPONENT_RTP, "udp",
                       SocketAddress("fe80::1234:5678:abcd:ef12", 1235),
                       kCandidatePriority, "", "", IceCandidateType::kHost,
                       kCandidateGeneration, kCandidateFoundation);

  IceCandidate jice1("audio", 0, candidate1);
  IceCandidate jice2("audio", 0, candidate2);
  IceCandidate jice3("video", 0, candidate1);
  IceCandidate jice4("video", 0, candidate2);
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice1));
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice2));
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice3));
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice4));
  std::string message = Serialize(jsep_desc_.get());

  // Should have a c line like this one.
  EXPECT_NE(message.find("c=IN IP6 fe80::1234:5678:abcd:ef12"),
            std::string::npos);
  // Shouldn't have a IP4 c line.
  EXPECT_EQ(message.find("c=IN IP4"), std::string::npos);
}

// Tests serialization of SDP with only TCP candidates and verifies that
// null IPv4 is used as default address in c line.
TEST_F(JsepSessionDescriptionTest, SerializeSessionDescriptionWithTCPOnly) {
  // Stun has a high preference than local host.
  Candidate candidate1(ICE_CANDIDATE_COMPONENT_RTP, "tcp",
                       SocketAddress("::1", 1234), kCandidatePriority, "", "",
                       IceCandidateType::kSrflx, kCandidateGeneration,
                       kCandidateFoundation);
  Candidate candidate2(ICE_CANDIDATE_COMPONENT_RTP, "tcp",
                       SocketAddress("::2", 1235), kCandidatePriority, "", "",
                       IceCandidateType::kHost, kCandidateGeneration,
                       kCandidateFoundation);

  IceCandidate jice1("audio", 0, candidate1);
  IceCandidate jice2("audio", 0, candidate2);
  IceCandidate jice3("video", 0, candidate1);
  IceCandidate jice4("video", 0, candidate2);
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice1));
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice2));
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice3));
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice4));

  std::string message = Serialize(jsep_desc_.get());
  EXPECT_EQ(message.find("c=IN IP6 ::3"), std::string::npos);
  // Should have a c line like this one when no any default exists.
  EXPECT_NE(message.find("c=IN IP4 0.0.0.0"), std::string::npos);
}

// Tests that the connection address will be correctly set when the Candidate is
// removed.
TEST_F(JsepSessionDescriptionTest, RemoveCandidateAndSetConnectionAddress) {
  Candidate candidate1(ICE_CANDIDATE_COMPONENT_RTP, "udp",
                       SocketAddress("::1", 1234), kCandidatePriority, "", "",
                       IceCandidateType::kHost, kCandidateGeneration,
                       kCandidateFoundation);
  Candidate candidate2(ICE_CANDIDATE_COMPONENT_RTP, "tcp",
                       SocketAddress("::2", 1235), kCandidatePriority, "", "",
                       IceCandidateType::kHost, kCandidateGeneration,
                       kCandidateFoundation);
  Candidate candidate3(ICE_CANDIDATE_COMPONENT_RTP, "udp",
                       SocketAddress("192.168.1.1", 1236), kCandidatePriority,
                       "", "", IceCandidateType::kHost, kCandidateGeneration,
                       kCandidateFoundation);
  IceCandidate jice1("audio", 0, candidate1);
  IceCandidate jice2("audio", 0, candidate2);
  IceCandidate jice3("audio", 0, candidate3);

  size_t audio_index = 0;
  auto media_desc =
      jsep_desc_->description()->contents()[audio_index].media_description();

  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice1));
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice2));
  ASSERT_TRUE(jsep_desc_->AddCandidate(&jice3));

  EXPECT_EQ("192.168.1.1:1236", media_desc->connection_address().ToString());

  ASSERT_TRUE(jsep_desc_->RemoveCandidate(&jice3));
  EXPECT_EQ("[::1]:1234", media_desc->connection_address().ToString());

  ASSERT_TRUE(jsep_desc_->RemoveCandidate(&jice2));
  EXPECT_EQ("[::1]:1234", media_desc->connection_address().ToString());

  ASSERT_TRUE(jsep_desc_->RemoveCandidate(&jice1));
  EXPECT_EQ("0.0.0.0:9", media_desc->connection_address().ToString());
}

// Tests that candidates passed to Create() update the connection address of
// their media section the same way AddCandidate() does, and that media
// sections without candidates keep their connection address.
TEST_F(JsepSessionDescriptionTest,
       CreateWithCandidatesUpdatesConnectionAddress) {
  Candidate candidate(ICE_CANDIDATE_COMPONENT_RTP, "udp",
                      SocketAddress("192.168.1.5", 1234), kCandidatePriority,
                      "", "", IceCandidateType::kHost, kCandidateGeneration,
                      kCandidateFoundation);
  std::vector<IceCandidateCollection> candidates(2);
  candidates[0].add(std::make_unique<IceCandidate>(kAudioMid, 0, candidate));

  std::unique_ptr<SessionDescription> desc = CreateCricketSessionDescription();
  const SocketAddress video_address("10.0.0.1", 5678);
  desc->contents()[1].media_description()->set_connection_address(
      video_address);
  std::unique_ptr<SessionDescriptionInterface> jsep_desc =
      SessionDescriptionInterface::Create(SdpType::kOffer, std::move(desc),
                                          "id", "version",
                                          std::move(candidates));
  ASSERT_THAT(jsep_desc, NotNull());
  ASSERT_EQ(jsep_desc->number_of_mediasections(), 2u);
  EXPECT_EQ(jsep_desc->candidates(0)->count(), 1u);
  EXPECT_EQ(jsep_desc->candidates(1)->count(), 0u);
  const ContentInfos& contents = jsep_desc->description()->contents();
  EXPECT_EQ(contents[0].media_description()->connection_address().ToString(),
            "192.168.1.5:1234");
  EXPECT_EQ(contents[1].media_description()->connection_address(),
            video_address);
}

class EnumerateAllSdpTypesTest : public ::testing::Test,
                                 public ::testing::WithParamInterface<SdpType> {
};

TEST_P(EnumerateAllSdpTypesTest, TestIdentity) {
  SdpType type = GetParam();

  const char* str = SdpTypeToString(type);
  EXPECT_EQ(type, SdpTypeFromString(str));
}

INSTANTIATE_TEST_SUITE_P(JsepSessionDescriptionTest,
                         EnumerateAllSdpTypesTest,
                         Values(SdpType::kOffer,
                                SdpType::kPrAnswer,
                                SdpType::kAnswer));

}  // namespace webrtc
