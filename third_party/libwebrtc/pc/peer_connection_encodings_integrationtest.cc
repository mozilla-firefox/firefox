/*
 *  Copyright 2023 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/audio_options.h"
#include "api/environment/environment.h"
#include "api/field_trials_view.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/rtc_error.h"
#include "api/rtp_parameters.h"
#include "api/rtp_sender_interface.h"
#include "api/rtp_transceiver_direction.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"
#include "api/test/rtc_error_matchers.h"
#include "api/units/data_rate.h"
#include "api/units/time_delta.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/sdp_video_format.h"
#include "media/base/codec.h"
#include "media/engine/fake_webrtc_video_engine.h"
#include "pc/session_description.h"
#include "pc/simulcast_description.h"
#include "pc/test/mock_peer_connection_observers.h"
#include "pc/test/peer_connection_test_wrapper.h"
#include "pc/test/simulcast_layer_util.h"
#include "rtc_base/checks.h"
#include "rtc_base/containers/flat_map.h"
#include "rtc_base/logging.h"
#include "rtc_base/physical_socket_server.h"
#include "rtc_base/thread.h"
#include "test/create_test_environment.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/wait_until.h"

using ::testing::AllOf;
using ::testing::AnyOf;
using ::testing::Contains;
using ::testing::Each;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::IsSupersetOf;
using ::testing::IsTrue;
using ::testing::Key;
using ::testing::Le;
using ::testing::Matcher;
using ::testing::Ne;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::Pair;
using ::testing::Pointer;
using ::testing::ResultOf;
using ::testing::SizeIs;
using ::testing::StrCaseEq;
using ::testing::StrEq;
using ::testing::UnorderedElementsAre;

namespace webrtc {

namespace {

// Most tests pass in 20-30 seconds, but some tests take longer such as AV1
// requiring additional ramp-up time (https://crbug.com/webrtc/15006) or SVC
// (LxTx_KEY) being slower than simulcast to send top spatial layer.
// TODO(https://crbug.com/webrtc/15076): Remove need for long ramp-up timeouts
// by using simulated time.
constexpr TimeDelta kLongTimeoutForRampingUp = TimeDelta::Minutes(1);

// The max bitrate 1500 kbps may be subject to change in the future. What we're
// interested in here is that all code paths that result in L1T3 result in the
// same target bitrate which does not exceed this limit.
constexpr DataRate kVp9ExpectedMaxBitrateForL1T3 =
    DataRate::KilobitsPerSec(1500);

auto EncoderImplementationIs(absl::string_view impl) {
  return Field("encoder_implementation",
               &RTCOutboundRtpStreamStats::encoder_implementation,
               Optional(StrEq(impl)));
}

template <typename M>
auto ScalabilityModeIs(M matcher) {
  return Field("scalability_mode", &RTCOutboundRtpStreamStats::scalability_mode,
               matcher);
}

template <typename M>
auto CodecIs(M matcher) {
  return Field("codec_id", &RTCOutboundRtpStreamStats::codec_id, matcher);
}

template <typename M>
auto RidIs(M matcher) {
  return Field("rid", &RTCOutboundRtpStreamStats::rid, matcher);
}

template <typename WidthMatcher, typename HeightMatcher>
auto ResolutionIs(WidthMatcher width_matcher, HeightMatcher height_matcher) {
  return AllOf(Field("frame_width", &RTCOutboundRtpStreamStats::frame_width,
                     width_matcher),
               Field("frame_height", &RTCOutboundRtpStreamStats::frame_height,
                     height_matcher));
}

template <typename M>
auto HeightIs(M matcher) {
  return Field("frame_height", &RTCOutboundRtpStreamStats::frame_height,
               matcher);
}

template <typename M>
auto BytesSentIs(M matcher) {
  return Field("bytes_sent", &RTCOutboundRtpStreamStats::bytes_sent, matcher);
}

template <typename M>
auto FramesEncodedIs(M matcher) {
  return Field("frames_encoded", &RTCOutboundRtpStreamStats::frames_encoded,
               matcher);
}

auto Active() {
  return Field("active", &RTCOutboundRtpStreamStats::active, true);
}

Matcher<scoped_refptr<const RTCStatsReport>> OutboundRtpStatsAre(
    Matcher<std::vector<RTCOutboundRtpStreamStats>> matcher) {
  return Pointer(ResultOf(
      "outbound_rtp",
      [&](const RTCStatsReport* report) {
        std::vector<const RTCOutboundRtpStreamStats*> stats =
            report->GetStatsOfType<RTCOutboundRtpStreamStats>();

        // Copy to a new vector.
        std::vector<RTCOutboundRtpStreamStats> stats_copy;
        stats_copy.reserve(stats.size());
        for (const auto* stat : stats) {
          stats_copy.emplace_back(*stat);
        }
        return stats_copy;
      },
      matcher));
}

auto HasOutboundRtpBytesSent(size_t num_layers, size_t num_active_layers) {
  return OutboundRtpStatsAre(AllOf(
      SizeIs(num_layers),
      testing::Contains(
          Field("bytes_sent", &RTCOutboundRtpStreamStats::bytes_sent, Gt(0)))
          .Times(num_active_layers)));
}

auto HasOutboundRtpBytesSent(size_t num_layers) {
  return HasOutboundRtpBytesSent(num_layers, num_layers);
}

flat_map<std::string, RTCOutboundRtpStreamStats> GetOutboundRtpStreamStatsByRid(
    scoped_refptr<const RTCStatsReport> report) {
  flat_map<std::string, RTCOutboundRtpStreamStats> result;
  auto stats = report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  for (const auto* outbound_rtp : stats) {
    result.emplace(
        std::make_pair(outbound_rtp->rid.value_or(""), *outbound_rtp));
  }
  return result;
}

struct StringParamToString {
  std::string operator()(const ::testing::TestParamInfo<std::string>& info) {
    return info.param;
  }
};

std::string GetCurrentCodecMimeType(
    scoped_refptr<const RTCStatsReport> report,
    const RTCOutboundRtpStreamStats& outbound_rtp) {
  return outbound_rtp.codec_id.has_value()
             ? *report->GetAs<RTCCodecStats>(*outbound_rtp.codec_id)->mime_type
             : "";
}

const RTCOutboundRtpStreamStats* FindOutboundRtpByRid(
    const std::vector<const RTCOutboundRtpStreamStats*>& outbound_rtps,
    const absl::string_view& rid) {
  for (const auto* outbound_rtp : outbound_rtps) {
    if (outbound_rtp->rid.has_value() && *outbound_rtp->rid == rid) {
      return outbound_rtp;
    }
  }
  return nullptr;
}

}  // namespace

class PeerConnectionEncodingsIntegrationTest : public ::testing::Test {
 public:
  PeerConnectionEncodingsIntegrationTest()
      : env_(CreateTestEnvironment()),
        background_thread_(std::make_unique<Thread>(&pss_)) {
    RTC_CHECK(background_thread_->Start());
  }

  scoped_refptr<PeerConnectionTestWrapper> CreatePc(
      absl::string_view field_trials = "") {
    auto pc_wrapper = make_ref_counted<PeerConnectionTestWrapper>(
        "pc", env_, &pss_, background_thread_.get());
    pc_wrapper->CreatePc({}, CreateBuiltinAudioEncoderFactory(),
                         CreateBuiltinAudioDecoderFactory(),
                         CreateTestFieldTrialsPtr(field_trials));
    return pc_wrapper;
  }

  scoped_refptr<RtpTransceiverInterface> AddTransceiverWithSimulcastLayers(
      scoped_refptr<PeerConnectionTestWrapper> local,
      scoped_refptr<PeerConnectionTestWrapper> remote,
      std::vector<SimulcastLayer> init_layers) {
    scoped_refptr<MediaStreamInterface> stream = local->GetUserMedia(
        /*audio=*/false, AudioOptions(), /*video=*/true,
        {.width = 1280, .height = 720});
    scoped_refptr<VideoTrackInterface> track = stream->GetVideoTracks()[0];

    RTCErrorOr<scoped_refptr<RtpTransceiverInterface>> transceiver_or_error =
        local->pc()->AddTransceiver(track, CreateTransceiverInit(init_layers));
    EXPECT_TRUE(transceiver_or_error.ok());
    return transceiver_or_error.value();
  }

  bool HasReceiverVideoCodecCapability(
      scoped_refptr<PeerConnectionTestWrapper> pc_wrapper,
      absl::string_view codec_name) {
    std::vector<RtpCodecCapability> codecs =
        pc_wrapper->pc_factory()
            ->GetRtpReceiverCapabilities(MediaType::VIDEO)
            .codecs;
    return std::find_if(codecs.begin(), codecs.end(),
                        [&codec_name](const RtpCodecCapability& codec) {
                          return absl::EqualsIgnoreCase(codec.name, codec_name);
                        }) != codecs.end();
  }

  std::vector<RtpCodecCapability> GetCapabilitiesAndRestrictToCodec(
      scoped_refptr<PeerConnectionTestWrapper> pc_wrapper,
      absl::string_view codec_name) {
    std::vector<RtpCodecCapability> codecs =
        pc_wrapper->pc_factory()
            ->GetRtpReceiverCapabilities(MediaType::VIDEO)
            .codecs;
    std::erase_if(codecs, [&codec_name](const RtpCodecCapability& codec) {
      return !codec.IsResiliencyCodec() &&
             !absl::EqualsIgnoreCase(codec.name, codec_name);
    });
    RTC_DCHECK(std::find_if(codecs.begin(), codecs.end(),
                            [&codec_name](const RtpCodecCapability& codec) {
                              return absl::EqualsIgnoreCase(codec.name,
                                                            codec_name);
                            }) != codecs.end());
    return codecs;
  }

  void ExchangeIceCandidates(
      scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper,
      scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper) {
    local_pc_wrapper->SubscribeOnIceCandidateReady(
        remote_pc_wrapper.get(),
        [remote_pc = remote_pc_wrapper.get()](const std::string& mid, int index,
                                              const std::string& candidate) {
          remote_pc->AddIceCandidate(mid, index, candidate);
        });
    remote_pc_wrapper->SubscribeOnIceCandidateReady(
        local_pc_wrapper.get(),
        [local_pc = local_pc_wrapper.get()](const std::string& mid, int index,
                                            const std::string& candidate) {
          local_pc->AddIceCandidate(mid, index, candidate);
        });
  }

  // Negotiate without any tweaks (does not work for simulcast loopback).
  void Negotiate(scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper,
                 scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper) {
    std::unique_ptr<SessionDescriptionInterface> offer =
        CreateOffer(local_pc_wrapper);
    scoped_refptr<MockSetSessionDescriptionObserver> p1 =
        SetLocalDescription(local_pc_wrapper, offer.get());
    scoped_refptr<MockSetSessionDescriptionObserver> p2 =
        SetRemoteDescription(remote_pc_wrapper, offer.get());
    EXPECT_TRUE(Await({p1, p2}));
    std::unique_ptr<SessionDescriptionInterface> answer =
        CreateAnswer(remote_pc_wrapper);
    p1 = SetLocalDescription(remote_pc_wrapper, answer.get());
    p2 = SetRemoteDescription(local_pc_wrapper, answer.get());
    EXPECT_TRUE(Await({p1, p2}));
  }

  void NegotiateWithSimulcastTweaks(
      scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper,
      scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper) {
    // Create and set offer for `local_pc_wrapper`.
    std::unique_ptr<SessionDescriptionInterface> offer =
        CreateOffer(local_pc_wrapper);
    scoped_refptr<MockSetSessionDescriptionObserver> p1 =
        SetLocalDescription(local_pc_wrapper, offer.get());
    // Modify the offer before handoff because `remote_pc_wrapper` only supports
    // receiving singlecast.
    SimulcastDescription simulcast_description = RemoveSimulcast(offer.get());
    scoped_refptr<MockSetSessionDescriptionObserver> p2 =
        SetRemoteDescription(remote_pc_wrapper, offer.get());
    EXPECT_TRUE(Await({p1, p2}));

    // Create and set answer for `remote_pc_wrapper`.
    std::unique_ptr<SessionDescriptionInterface> answer =
        CreateAnswer(remote_pc_wrapper);
    EXPECT_TRUE(answer);
    p1 = SetLocalDescription(remote_pc_wrapper, answer.get());
    // Modify the answer before handoff because `local_pc_wrapper` should still
    // send simulcast.
    MediaContentDescription* mcd_answer =
        answer->description()->contents()[0].media_description();
    mcd_answer->mutable_streams().clear();
    std::vector<SimulcastLayer> simulcast_layers =
        simulcast_description.send_layers().GetAllLayers();
    SimulcastLayerList& receive_layers =
        mcd_answer->simulcast_description().receive_layers();
    for (const auto& layer : simulcast_layers) {
      receive_layers.AddLayer(layer);
    }
    p2 = SetRemoteDescription(local_pc_wrapper, answer.get());
    EXPECT_TRUE(Await({p1, p2}));
  }

  scoped_refptr<const RTCStatsReport> GetStats(
      scoped_refptr<PeerConnectionTestWrapper> pc_wrapper) {
    auto callback = make_ref_counted<MockRTCStatsCollectorCallback>();
    pc_wrapper->pc()->GetStats(callback.get());
    RTC_CHECK(WaitUntil([&]() { return callback->called(); }, testing::IsTrue())
                  .ok());
    return callback->report();
  }

  [[nodiscard]] RTCErrorOr<scoped_refptr<const RTCStatsReport>> GetStatsUntil(
      scoped_refptr<PeerConnectionTestWrapper> pc_wrapper,
      Matcher<scoped_refptr<const RTCStatsReport>> matcher,
      WaitUntilSettings settings = {}) {
    return WaitUntil([&]() { return GetStats(pc_wrapper); }, std::move(matcher),
                     settings);
  }

 protected:
  std::unique_ptr<SessionDescriptionInterface> CreateOffer(
      scoped_refptr<PeerConnectionTestWrapper> pc_wrapper) {
    auto observer = make_ref_counted<MockCreateSessionDescriptionObserver>();
    pc_wrapper->pc()->CreateOffer(observer.get(), {});
    EXPECT_TRUE(WaitUntil([&] { return observer->called(); }));
    return observer->MoveDescription();
  }

  std::unique_ptr<SessionDescriptionInterface> CreateAnswer(
      scoped_refptr<PeerConnectionTestWrapper> pc_wrapper) {
    auto observer = make_ref_counted<MockCreateSessionDescriptionObserver>();
    pc_wrapper->pc()->CreateAnswer(observer.get(), {});
    EXPECT_TRUE(WaitUntil([&] { return observer->called(); }));
    return observer->MoveDescription();
  }

  scoped_refptr<MockSetSessionDescriptionObserver> SetLocalDescription(
      scoped_refptr<PeerConnectionTestWrapper> pc_wrapper,
      SessionDescriptionInterface* sdp) {
    auto observer = make_ref_counted<MockSetSessionDescriptionObserver>();
    pc_wrapper->pc()->SetLocalDescription(observer.get(),
                                          sdp->Clone().release());
    return observer;
  }

  scoped_refptr<MockSetSessionDescriptionObserver> SetRemoteDescription(
      scoped_refptr<PeerConnectionTestWrapper> pc_wrapper,
      SessionDescriptionInterface* sdp) {
    auto observer = make_ref_counted<MockSetSessionDescriptionObserver>();
    pc_wrapper->pc()->SetRemoteDescription(observer.get(),
                                           sdp->Clone().release());
    return observer;
  }

  // To avoid ICE candidates arriving before the remote endpoint has received
  // the offer it is important to SetLocalDescription() and
  // SetRemoteDescription() are kicked off without awaiting in-between. This
  // helper is used to await multiple observers.
  bool Await(
      std::vector<scoped_refptr<MockSetSessionDescriptionObserver>> observers) {
    for (auto& observer : observers) {
      auto result = WaitUntil([&] { return observer->called(); }, IsTrue());

      if (!result.ok() || !observer->result()) {
        return false;
      }
    }
    return true;
  }

  const Environment env_;
  PhysicalSocketServer pss_;
  std::unique_ptr<Thread> background_thread_;
};

TEST_F(PeerConnectionEncodingsIntegrationTest,
       VP8_SingleEncodingDefaultsToL1T1) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers = CreateLayers({"f"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, "VP8");
  transceiver->SetCodecPreferences(codecs);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Wait until media is flowing.
  auto stats_result =
      GetStatsUntil(local_pc_wrapper, HasOutboundRtpBytesSent(1));
  ASSERT_THAT(stats_result, IsRtcOk());
  EXPECT_THAT(GetOutboundRtpStreamStatsByRid(stats_result.value()),
              ElementsAre(Pair("", ResolutionIs(1280, 720))));

  // Verify codec and scalability mode.
  scoped_refptr<const RTCStatsReport> report = stats_result.value();
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_THAT(outbound_rtps, SizeIs(1u));
  EXPECT_THAT(outbound_rtps, Contains(ResolutionIs(Le(1280), Le(720))));
  EXPECT_THAT(GetCurrentCodecMimeType(report, *outbound_rtps[0]),
              StrCaseEq("video/VP8"));
  EXPECT_THAT(*outbound_rtps[0]->scalability_mode, StrEq("L1T1"));
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       VP8_RejectsSvcAndDefaultsToL1T1) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers = CreateLayers({"f"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  // Restricting the local receive codecs will restrict what we offer and
  // hence the answer if it is a subset of our offer.
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(local_pc_wrapper, "VP8");
  transceiver->SetCodecPreferences(codecs);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);

  // Attempt SVC (L3T3_KEY). This is not possible because only VP8 is up for
  // negotiation and VP8 does not support it.
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  parameters.encodings[0].scalability_mode = "L3T3_KEY";
  parameters.encodings[0].scale_resolution_down_by = 1;
  EXPECT_FALSE(sender->SetParameters(parameters).ok());
  // `scalability_mode` remains unset because SetParameters() failed.
  parameters = sender->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  EXPECT_THAT(parameters.encodings[0].scalability_mode, Eq(std::nullopt));

  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Wait until media is flowing.
  ASSERT_THAT(GetStatsUntil(local_pc_wrapper, HasOutboundRtpBytesSent(1)),
              IsRtcOk());
  // When `scalability_mode` is not set, VP8 defaults to L1T1.
  scoped_refptr<const RTCStatsReport> report = GetStats(local_pc_wrapper);
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_THAT(outbound_rtps, SizeIs(1u));
  EXPECT_THAT(GetCurrentCodecMimeType(report, *outbound_rtps[0]),
              StrCaseEq("video/VP8"));
  EXPECT_THAT(*outbound_rtps[0]->scalability_mode, StrEq("L1T1"));
  // GetParameters() confirms `scalability_mode` is still not set.
  parameters = sender->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  EXPECT_THAT(parameters.encodings[0].scalability_mode, Eq(std::nullopt));
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       SetParametersWithScalabilityModeNotSupportedBySubsequentNegotiation) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers = CreateLayers({"f"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  // Restricting the local receive codecs will restrict what we offer and
  // hence the answer if it is a subset of our offer.
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(local_pc_wrapper, "VP8");
  transceiver->SetCodecPreferences(codecs);

  // Attempt SVC (L3T3_KEY). This is still possible because VP9 might be
  // available from the remote end.
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  parameters.encodings[0].scalability_mode = "L3T3_KEY";
  parameters.encodings[0].scale_resolution_down_by = 1;
  EXPECT_TRUE(sender->SetParameters(parameters).ok());

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);

  // `scalability_mode` is set to the VP8 default since that is what was
  // negotiated.
  parameters = sender->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  EXPECT_THAT(parameters.encodings[0].scalability_mode, Eq("L1T2"));

  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Wait until media is flowing.
  auto error_or_stats =
      GetStatsUntil(local_pc_wrapper, HasOutboundRtpBytesSent(1));
  ASSERT_THAT(error_or_stats, IsRtcOk());
  // When `scalability_mode` is not set, VP8 defaults to L1T1.
  scoped_refptr<const RTCStatsReport> report = error_or_stats.value();
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_THAT(outbound_rtps, SizeIs(1u));
  EXPECT_THAT(GetCurrentCodecMimeType(report, *outbound_rtps[0]),
              StrCaseEq("video/VP8"));
  EXPECT_THAT(*outbound_rtps[0]->scalability_mode, StrEq("L1T2"));
  // GetParameters() confirms `scalability_mode` is still not set.
  parameters = sender->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  EXPECT_THAT(parameters.encodings[0].scalability_mode, Eq("L1T2"));
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       VP8_FallbackFromSvcResultsInL1T2) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers = CreateLayers({"f"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  // Verify test assumption that VP8 is first in the list, but don't modify the
  // codec preferences because we want the sender to think SVC is a possibility.
  std::vector<RtpCodecCapability> codecs =
      local_pc_wrapper->pc_factory()
          ->GetRtpSenderCapabilities(MediaType::VIDEO)
          .codecs;
  EXPECT_THAT(codecs[0].name, StrCaseEq("VP8"));
  // Attempt SVC (L3T3_KEY), which is not possible with VP8, but the sender does
  // not yet know which codec we'll use so the parameters will be accepted.
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  parameters.encodings[0].scalability_mode = "L3T3_KEY";
  parameters.encodings[0].scale_resolution_down_by = 1;
  EXPECT_TRUE(sender->SetParameters(parameters).ok());
  // Verify fallback has not happened yet.
  parameters = sender->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  EXPECT_THAT(parameters.encodings[0].scalability_mode,
              Optional(std::string("L3T3_KEY")));

  // Negotiate, this results in VP8 being picked and fallback happening.
  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();
  // `scalaiblity_mode` is assigned the fallback value "L1T2" which is different
  // than the default of std::nullopt.
  parameters = sender->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  EXPECT_THAT(parameters.encodings[0].scalability_mode,
              Optional(std::string("L1T2")));

  // Wait until media is flowing, no significant time needed because we only
  // have one layer.
  ASSERT_THAT(GetStatsUntil(local_pc_wrapper, HasOutboundRtpBytesSent(1u)),
              IsRtcOk());
  // GetStats() confirms "L1T2" is used which is different than the "L1T1"
  // default or the "L3T3_KEY" that was attempted.
  scoped_refptr<const RTCStatsReport> report = GetStats(local_pc_wrapper);
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_THAT(outbound_rtps, SizeIs(1u));
  EXPECT_THAT(GetCurrentCodecMimeType(report, *outbound_rtps[0]),
              StrCaseEq("video/VP8"));
  EXPECT_THAT(*outbound_rtps[0]->scalability_mode, StrEq("L1T2"));

  // Now that we know VP8 is used, try setting L3T3 which should fail.
  parameters = sender->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  parameters.encodings[0].scalability_mode = "L3T3_KEY";
  parameters.encodings[0].scale_resolution_down_by = 1;
  EXPECT_FALSE(sender->SetParameters(parameters).ok());
}

// The legacy SVC path is triggered when VP9 us used, but `scalability_mode` has
// not been specified.
// TODO(https://crbug.com/webrtc/14889): When legacy VP9 SVC path has been
// deprecated and removed, update this test to assert that simulcast is used
// (i.e. VP9 is not treated differently than VP8).
TEST_F(PeerConnectionEncodingsIntegrationTest,
       VP9_LegacySvcWhenScalabilityModeNotSpecified) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"f", "h", "q"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, "VP9");
  transceiver->SetCodecPreferences(codecs);

  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  // Keep this legacy SVC test independent of simulcast's default scaling.
  for (RtpEncodingParameters& encoding : parameters.encodings) {
    encoding.scale_resolution_down_by = 1.0;
  }
  ASSERT_TRUE(sender->SetParameters(parameters).ok());

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Wait until media is flowing. We only expect a single RTP stream.
  // We expect to see bytes flowing almost immediately on the lowest layer.
  ASSERT_THAT(GetStatsUntil(local_pc_wrapper, HasOutboundRtpBytesSent(1u)),
              IsRtcOk());
  // Wait until scalability mode is reported and expected resolution reached.
  // Ramp up time may be significant.
  ASSERT_THAT(
      GetStatsUntil(
          local_pc_wrapper,
          OutboundRtpStatsAre(Contains(
              AllOf(RidIs("f"), ScalabilityModeIs("L3T3_KEY"), HeightIs(720)))),
          {.timeout = kLongTimeoutForRampingUp}),
      IsRtcOk());

  // Despite SVC being used on a single RTP stream, GetParameters() returns the
  // three encodings that we configured earlier (this is not spec-compliant but
  // it is how legacy SVC behaves).
  std::vector<RtpEncodingParameters> encodings =
      sender->GetParameters().encodings;
  ASSERT_EQ(encodings.size(), 3u);
  // When legacy SVC is used, `scalability_mode` is not specified.
  EXPECT_FALSE(encodings[0].scalability_mode.has_value());
  EXPECT_FALSE(encodings[1].scalability_mode.has_value());
  EXPECT_FALSE(encodings[2].scalability_mode.has_value());
}

// The spec-compliant way to configure SVC for a single stream. The expected
// outcome is the same as for the legacy SVC case except that we only have one
// encoding in GetParameters().
TEST_F(PeerConnectionEncodingsIntegrationTest,
       VP9_StandardSvcWithOnlyOneEncoding) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers = CreateLayers({"f"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, "VP9");
  transceiver->SetCodecPreferences(codecs);
  // Configure SVC, a.k.a. "L3T3_KEY".
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  parameters.encodings[0].scalability_mode = "L3T3_KEY";
  parameters.encodings[0].scale_resolution_down_by = 1;
  EXPECT_TRUE(sender->SetParameters(parameters).ok());

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Wait until media is flowing. We only expect a single RTP stream.
  // We expect to see bytes flowing almost immediately on the lowest layer.

  auto error_or_stats =
      GetStatsUntil(local_pc_wrapper,
                    AllOf(HasOutboundRtpBytesSent(1u),
                          OutboundRtpStatsAre(Contains(HeightIs(720)))),
                    {.timeout = kLongTimeoutForRampingUp});
  ASSERT_THAT(error_or_stats, IsRtcOk());
  // Verify codec and scalability mode.
  scoped_refptr<const RTCStatsReport> report = error_or_stats.value();
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_THAT(outbound_rtps, SizeIs(1u));
  EXPECT_THAT(outbound_rtps[0], ResolutionIs(1280, 720));
  EXPECT_THAT(outbound_rtps[0], RidIs(std::nullopt));
  EXPECT_THAT(GetCurrentCodecMimeType(report, *outbound_rtps[0]),
              StrCaseEq("video/VP9"));
  EXPECT_THAT(*outbound_rtps[0]->scalability_mode, StrEq("L3T3_KEY"));

  // GetParameters() is consistent with what we asked for and got.
  parameters = sender->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  EXPECT_THAT(parameters.encodings[0].scalability_mode,
              Optional(std::string("L3T3_KEY")));
}

// The {active,inactive,inactive} case is technically simulcast but since we
// only have one active stream, we're able to do SVC (multiple spatial layers
// is not supported if multiple encodings are active). The expected outcome is
// the same as above except we end up with two inactive RTP streams which are
// observable in GetStats().
TEST_F(PeerConnectionEncodingsIntegrationTest,
       VP9_StandardSvcWithSingleActiveEncoding) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"f", "h", "q"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, "VP9");
  transceiver->SetCodecPreferences(codecs);
  // Configure SVC, a.k.a. "L3T3_KEY".
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  parameters.encodings[0].scalability_mode = "L3T3_KEY";
  parameters.encodings[0].scale_resolution_down_by = 1;
  parameters.encodings[1].active = false;
  parameters.encodings[2].active = false;
  EXPECT_TRUE(sender->SetParameters(parameters).ok());

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Since the standard API is configuring simulcast we get three outbound-rtps,
  // but only one is active.
  // Wait until scalability mode is reported and expected resolution reached.
  // Ramp up time is significant.
  ASSERT_THAT(GetStatsUntil(local_pc_wrapper,
                            AllOf(HasOutboundRtpBytesSent(3, 1),
                                  OutboundRtpStatsAre(Contains(AllOf(
                                      RidIs("f"), ScalabilityModeIs("L3T3_KEY"),
                                      HeightIs(720))))),
                            {.timeout = kLongTimeoutForRampingUp}),
              IsRtcOk());

  // GetParameters() is consistent with what we asked for and got.
  parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  EXPECT_THAT(parameters.encodings[0].scalability_mode,
              Optional(StrEq("L3T3_KEY")));
  EXPECT_FALSE(parameters.encodings[1].scalability_mode.has_value());
  EXPECT_FALSE(parameters.encodings[2].scalability_mode.has_value());
}

// Exercise common path where `scalability_mode` is not specified until after
// negotiation, requiring us to recreate the stream when the number of streams
// changes from 1 (legacy SVC) to 3 (standard simulcast).
TEST_F(PeerConnectionEncodingsIntegrationTest,
       VP9_SwitchFromLegacySvcToStandardSingleActiveEncodingSvc) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"f", "h", "q"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, "VP9");
  transceiver->SetCodecPreferences(codecs);

  // The original negotiation triggers legacy SVC because we didn't specify
  // any scalability mode.
  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Switch to the standard mode. Despite only having a single active stream in
  // both cases, this internally reconfigures from 1 stream to 3 streams.
  // Test coverage for https://crbug.com/webrtc/15016.
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  parameters.encodings[0].active = true;
  parameters.encodings[0].scalability_mode = "L2T2_KEY";
  parameters.encodings[0].scale_resolution_down_by = 2.0;
  parameters.encodings[1].active = false;
  parameters.encodings[1].scalability_mode = std::nullopt;
  parameters.encodings[2].active = false;
  parameters.encodings[2].scalability_mode = std::nullopt;
  sender->SetParameters(parameters);

  // Since the standard API is configuring simulcast we get three outbound-rtps,
  // but only one is active.
  // Wait until scalability mode is reported and expected resolution reached.
  // Ramp up time may be significant.
  ASSERT_THAT(GetStatsUntil(local_pc_wrapper,
                            AllOf(HasOutboundRtpBytesSent(3, 1),
                                  OutboundRtpStatsAre(Contains(AllOf(
                                      RidIs("f"), ScalabilityModeIs("L2T2_KEY"),
                                      HeightIs(720 / 2))))),
                            {.timeout = kLongTimeoutForRampingUp}),
              IsRtcOk());

  // GetParameters() does not report any fallback.
  parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  EXPECT_THAT(parameters.encodings[0].scalability_mode,
              Optional(std::string("L2T2_KEY")));
  EXPECT_FALSE(parameters.encodings[1].scalability_mode.has_value());
  EXPECT_FALSE(parameters.encodings[2].scalability_mode.has_value());
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       VP9_SimulcastDeactiveActiveLayer_StandardSvc) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"q", "h", "f"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  constexpr absl::string_view kCodec = "VP9";
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, kCodec);
  transceiver->SetCodecPreferences(codecs);

  // Switch to the standard mode. Despite only having a single active stream in
  // both cases, this internally reconfigures from 1 stream to 3 streams.
  // Test coverage for https://crbug.com/webrtc/15016.
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  parameters.encodings[0].active = true;
  parameters.encodings[0].scalability_mode = "L1T3";
  parameters.encodings[0].scale_resolution_down_by = 4.0;
  parameters.encodings[1].active = true;
  parameters.encodings[1].scalability_mode = "L1T1";
  parameters.encodings[1].scale_resolution_down_by = 2.0;
  parameters.encodings[2].active = true;
  parameters.encodings[2].scalability_mode = "L1T1";
  parameters.encodings[2].scale_resolution_down_by = 1.0;
  EXPECT_TRUE(sender->SetParameters(parameters).ok());

  // The original negotiation triggers legacy SVC because we didn't specify
  // any scalability mode.
  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Since the standard API is configuring simulcast we get three outbound-rtps,
  // and two are active.
  ASSERT_THAT(
      WaitUntil(
          [&] {
            std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
                GetStats(local_pc_wrapper)
                    ->GetStatsOfType<RTCOutboundRtpStreamStats>();
            std::vector<size_t> bytes_sent;
            bytes_sent.reserve(outbound_rtps.size());
            for (const auto* outbound_rtp : outbound_rtps) {
              bytes_sent.push_back(outbound_rtp->bytes_sent.value_or(0));
            }
            return bytes_sent;
          },
          AllOf(SizeIs(3), Each(Gt(0))), {.timeout = kLongTimeoutForRampingUp}),
      IsRtcOk());
  scoped_refptr<const RTCStatsReport> report = GetStats(local_pc_wrapper);
  ASSERT_TRUE(report);
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  EXPECT_THAT(outbound_rtps,
              Each(EncoderImplementationIs(
                  "SimulcastEncoderAdapter (libvpx, libvpx, libvpx)")));

  // GetParameters() does not report any fallback.
  parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  EXPECT_THAT(parameters.encodings[0].scalability_mode,
              Optional(StrEq("L1T3")));
  EXPECT_THAT(parameters.encodings[1].scalability_mode,
              Optional(StrEq("L1T1")));
  EXPECT_THAT(parameters.encodings[2].scalability_mode,
              Optional(StrEq("L1T1")));
  EXPECT_THAT(parameters.encodings[2].scale_resolution_down_by, Eq(1.0));
  EXPECT_THAT(parameters.encodings[1].scale_resolution_down_by, Eq(2.0));
  EXPECT_THAT(parameters.encodings[0].scale_resolution_down_by, Eq(4.0));

  // Deactivate the active layer.
  parameters.encodings[2].active = false;
  EXPECT_TRUE(sender->SetParameters(parameters).ok());
  ASSERT_THAT(WaitUntil(
                  [&]() {
                    return GetStats(local_pc_wrapper)
                        ->GetStatsOfType<RTCOutboundRtpStreamStats>();
                  },
                  AllOf(Each(EncoderImplementationIs(
                            "SimulcastEncoderAdapter (libvpx, libvpx)")),
                        UnorderedElementsAre(ScalabilityModeIs("L1T3"),
                                             ScalabilityModeIs("L1T1"),
                                             ScalabilityModeIs(std::nullopt)))),
              IsRtcOk());
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       VP9_SimulcastMultiplLayersActive_StandardSvc) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"q", "h", "f"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, "VP9");
  transceiver->SetCodecPreferences(codecs);

  // Switch to the standard mode. Despite only having a single active stream in
  // both cases, this internally reconfigures from 1 stream to 3 streams.
  // Test coverage for https://crbug.com/webrtc/15016.
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  parameters.encodings[0].active = true;
  parameters.encodings[0].scalability_mode = "L1T3";
  parameters.encodings[0].scale_resolution_down_by = 4.0;
  parameters.encodings[1].active = true;
  parameters.encodings[1].scalability_mode = "L1T1";
  parameters.encodings[1].scale_resolution_down_by = 2.0;
  parameters.encodings[2].active = false;
  parameters.encodings[2].scalability_mode = std::nullopt;
  EXPECT_TRUE(sender->SetParameters(parameters).ok());

  // The original negotiation triggers legacy SVC because we didn't specify
  // any scalability mode.
  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Since the standard API is configuring simulcast we get three outbound-rtps,
  // and two are active.
  // Wait until scalability mode is reported and expected resolution reached.
  // Ramp up time may be significant.
  auto error_or_stats = GetStatsUntil(
      local_pc_wrapper,
      OutboundRtpStatsAre(
          IsSupersetOf({AllOf(RidIs("q"), ScalabilityModeIs("L1T3"),
                              HeightIs(720 / 4), BytesSentIs(Gt(0))),
                        AllOf(RidIs("h"), ScalabilityModeIs("L1T1"),
                              HeightIs(720 / 2), BytesSentIs(Gt(0)))})),
      {.timeout = kLongTimeoutForRampingUp});
  ASSERT_THAT(error_or_stats, IsRtcOk());
  scoped_refptr<const RTCStatsReport> report = error_or_stats.value();
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  EXPECT_THAT(outbound_rtps, Each(EncoderImplementationIs(
                                 "SimulcastEncoderAdapter (libvpx, libvpx)")));

  // GetParameters() does not report any fallback.
  parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  EXPECT_THAT(parameters.encodings[0].scalability_mode,
              Optional(StrEq("L1T3")));
  EXPECT_THAT(parameters.encodings[1].scalability_mode,
              Optional(StrEq("L1T1")));
  EXPECT_THAT(parameters.encodings[2].scalability_mode, Eq(std::nullopt));
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       VP9_Simulcast_SwitchToLegacySvc) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"f", "h", "q"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, "VP9");
  transceiver->SetCodecPreferences(codecs);

  // Switch to the standard mode. Despite only having a single active stream in
  // both cases, this internally reconfigures from 1 stream to 3 streams.
  // Test coverage for https://crbug.com/webrtc/15016.
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  parameters.encodings[0].active = false;
  parameters.encodings[1].active = true;
  parameters.encodings[1].scalability_mode = "L1T1";
  parameters.encodings[1].scale_resolution_down_by = 2.0;
  parameters.encodings[2].active = true;
  parameters.encodings[2].scalability_mode = "L1T3";
  parameters.encodings[2].scale_resolution_down_by = 4.0;
  EXPECT_TRUE(sender->SetParameters(parameters).ok());

  // The original negotiation triggers legacy SVC because we didn't specify
  // any scalability mode.
  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Since the standard API is configuring simulcast we get three outbound-rtps,
  // and two are active.
  // Wait until scalability mode is reported and expected resolution reached.
  // Ramp up time may be significant.
  auto error_or_stats = GetStatsUntil(
      local_pc_wrapper,
      OutboundRtpStatsAre(UnorderedElementsAre(
          AllOf(RidIs("q"), ScalabilityModeIs("L1T3"), HeightIs(720 / 4)),
          AllOf(RidIs("h"), ScalabilityModeIs("L1T1"), HeightIs(720 / 2)),
          AllOf(RidIs("f"), BytesSentIs(AnyOf(0, std::nullopt))))),
      {.timeout = kLongTimeoutForRampingUp});
  ASSERT_THAT(error_or_stats, IsRtcOk());

  // GetParameters() does not report any fallback.
  parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  EXPECT_THAT(parameters.encodings[0].scalability_mode, Eq(std::nullopt));
  EXPECT_THAT(parameters.encodings[1].scalability_mode,
              Optional(StrEq("L1T1")));
  EXPECT_THAT(parameters.encodings[2].scalability_mode,
              Optional(StrEq("L1T3")));

  // Switch to legacy SVC mode.
  parameters.encodings[0].active = true;
  parameters.encodings[0].scalability_mode = std::nullopt;
  parameters.encodings[0].scale_resolution_down_by = std::nullopt;
  parameters.encodings[1].active = true;
  parameters.encodings[1].scalability_mode = std::nullopt;
  parameters.encodings[1].scale_resolution_down_by = std::nullopt;
  parameters.encodings[2].active = false;
  parameters.encodings[2].scalability_mode = std::nullopt;
  parameters.encodings[2].scale_resolution_down_by = std::nullopt;

  EXPECT_TRUE(sender->SetParameters(parameters).ok());
  // Ensure that we are getting VGA at L1T3 from the "f" rid.
  EXPECT_THAT(
      GetStatsUntil(
          local_pc_wrapper,
          OutboundRtpStatsAre(Contains(AllOf(
              RidIs("f"), ScalabilityModeIs("L2T3_KEY"), HeightIs(720 / 2)))),
          {.timeout = kLongTimeoutForRampingUp}),
      IsRtcOk());
}

TEST_F(PeerConnectionEncodingsIntegrationTest, VP9_OneLayerActive_LegacySvc) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"f", "h", "q"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, "VP9");
  transceiver->SetCodecPreferences(codecs);

  // Sending L1T3 with legacy SVC mode means setting 1 layer active.
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  parameters.encodings[0].active = true;
  parameters.encodings[1].active = false;
  parameters.encodings[2].active = false;
  sender->SetParameters(parameters);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Ensure that we are getting 180P at L1T3 from the "f" rid.
  EXPECT_THAT(
      GetStatsUntil(
          local_pc_wrapper,
          OutboundRtpStatsAre(Contains(
              AllOf(RidIs("f"), ScalabilityModeIs("L1T3"), HeightIs(720 / 4)))),
          {.timeout = kLongTimeoutForRampingUp}),
      IsRtcOk());
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       VP9_AllLayersInactive_LegacySvc) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"f", "h", "q"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, "VP9");
  transceiver->SetCodecPreferences(codecs);

  // Legacy SVC mode and all layers inactive.
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  parameters.encodings[0].active = false;
  parameters.encodings[1].active = false;
  parameters.encodings[2].active = false;
  sender->SetParameters(parameters);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Ensure no media is flowing (1 second should be enough).
  Thread::Current()->SleepMs(1000);
  scoped_refptr<const RTCStatsReport> report = GetStats(local_pc_wrapper);
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_THAT(outbound_rtps, SizeIs(1u));
  EXPECT_EQ(*outbound_rtps[0]->bytes_sent, 0u);
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       VP9_AllLayersInactive_StandardSvc) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"f", "h", "q"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, "VP9");
  transceiver->SetCodecPreferences(codecs);

  // Standard mode and all layers inactive.
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  parameters.encodings[0].scalability_mode = "L3T3_KEY";
  parameters.encodings[0].scale_resolution_down_by = 1;
  parameters.encodings[0].active = false;
  parameters.encodings[1].active = false;
  parameters.encodings[2].active = false;
  sender->SetParameters(parameters);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Ensure no media is flowing (1 second should be enough).
  Thread::Current()->SleepMs(1000);
  scoped_refptr<const RTCStatsReport> report = GetStats(local_pc_wrapper);
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_THAT(outbound_rtps, SizeIs(3u));
  EXPECT_EQ(*outbound_rtps[0]->bytes_sent, 0u);
  EXPECT_EQ(*outbound_rtps[1]->bytes_sent, 0u);
  EXPECT_EQ(*outbound_rtps[2]->bytes_sent, 0u);
}

TEST_F(PeerConnectionEncodingsIntegrationTest, VP9_TargetBitrate_LegacyL1T3) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"f", "h", "q"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, "VP9");
  transceiver->SetCodecPreferences(codecs);

  // In legacy SVC, disabling the bottom two layers encodings is interpreted as
  // disabling the bottom two spatial layers resulting in L1T3.
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  // Keep this legacy SVC test independent of simulcast's default scaling.
  for (RtpEncodingParameters& encoding : parameters.encodings) {
    encoding.scale_resolution_down_by = 1.0;
  }
  parameters.encodings[0].active = false;
  parameters.encodings[1].active = false;
  parameters.encodings[2].active = true;
  sender->SetParameters(parameters);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Wait until 720p L1T3 has ramped up to 720p. It may take additional time
  // for the target bitrate to reach its maximum.
  ASSERT_THAT(
      GetStatsUntil(local_pc_wrapper,
                    OutboundRtpStatsAre(Contains(AllOf(
                        RidIs("f"), ScalabilityModeIs("L1T3"), HeightIs(720)))),
                    {.timeout = kLongTimeoutForRampingUp}),
      IsRtcOk());

  // The target bitrate typically reaches `kVp9ExpectedMaxBitrateForL1T3`
  // in a short period of time. However to reduce risk of flakiness in bot
  // environments, this test only fails if we we exceed the expected target.
  Thread::Current()->SleepMs(1000);
  scoped_refptr<const RTCStatsReport> report = GetStats(local_pc_wrapper);
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_THAT(outbound_rtps, SizeIs(1));
  DataRate target_bitrate =
      DataRate::BitsPerSec(*outbound_rtps[0]->target_bitrate);
  EXPECT_LE(target_bitrate.kbps(), kVp9ExpectedMaxBitrateForL1T3.kbps());
}

// Test coverage for https://crbug.com/1455039.
TEST_F(PeerConnectionEncodingsIntegrationTest, VP9_TargetBitrate_StandardL1T3) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"f", "h", "q"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, "VP9");
  transceiver->SetCodecPreferences(codecs);

  // With standard APIs, L1T3 is explicitly specified and the encodings refers
  // to the RTP streams, not the spatial layers. The end result should be
  // equivalent to the legacy L1T3 case.
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  parameters.encodings[0].active = true;
  parameters.encodings[0].scale_resolution_down_by = 1.0;
  parameters.encodings[0].scalability_mode = "L1T3";
  parameters.encodings[1].active = false;
  parameters.encodings[2].active = false;
  sender->SetParameters(parameters);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Wait until 720p L1T3 has ramped up to 720p. It may take additional time
  // for the target bitrate to reach its maximum.
  ASSERT_THAT(
      GetStatsUntil(local_pc_wrapper,
                    OutboundRtpStatsAre(Contains(AllOf(
                        RidIs("f"), ScalabilityModeIs("L1T3"), HeightIs(720)))),
                    {.timeout = kLongTimeoutForRampingUp}),
      IsRtcOk());

  // The target bitrate typically reaches `kVp9ExpectedMaxBitrateForL1T3`
  // in a short period of time. However to reduce risk of flakiness in bot
  // environments, this test only fails if we we exceed the expected target.
  Thread::Current()->SleepMs(1000);
  scoped_refptr<const RTCStatsReport> report = GetStats(local_pc_wrapper);
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_THAT(outbound_rtps, SizeIs(3));
  auto* outbound_rtp = FindOutboundRtpByRid(outbound_rtps, "f");
  ASSERT_TRUE(outbound_rtp);
  DataRate target_bitrate = DataRate::BitsPerSec(*outbound_rtp->target_bitrate);
  EXPECT_LE(target_bitrate.kbps(), kVp9ExpectedMaxBitrateForL1T3.kbps());
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       SimulcastProducesUniqueSsrcAndRtxSsrcsWhenConnected) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"f", "h", "q"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, "VP8");
  transceiver->SetCodecPreferences(codecs);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Wait until media is flowing on all three layers.
  // Ramp up time is needed before all three layers are sending.
  auto stats = GetStatsUntil(local_pc_wrapper, HasOutboundRtpBytesSent(3u),
                             {.timeout = kLongTimeoutForRampingUp});
  ASSERT_THAT(stats, IsRtcOk());
  // Verify SSRCs and RTX SSRCs.
  scoped_refptr<const RTCStatsReport> report = stats.MoveValue();
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_THAT(outbound_rtps, SizeIs(3u));

  std::set<uint32_t> ssrcs;
  std::set<uint32_t> rtx_ssrcs;
  for (const auto& outbound_rtp : outbound_rtps) {
    ASSERT_TRUE(outbound_rtp->ssrc.has_value());
    ASSERT_TRUE(outbound_rtp->rtx_ssrc.has_value());
    ssrcs.insert(*outbound_rtp->ssrc);
    rtx_ssrcs.insert(*outbound_rtp->rtx_ssrc);
  }
  EXPECT_EQ(ssrcs.size(), 3u);
  EXPECT_EQ(rtx_ssrcs.size(), 3u);
}

// Similar to the above test, but we never exchange ICE candidates such that
// simulcast never has a chance to "ramp up". Despite this, we should see one
// outbound-rtp per encoding.
TEST_F(PeerConnectionEncodingsIntegrationTest,
       SimulcastProducesUniqueSsrcAndRtxSsrcsWhenDisconnected) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();

  std::vector<SimulcastLayer> layers =
      CreateLayers({"f", "h", "q"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);

  // Inactive the middle layer.
  auto sender = transceiver->sender();
  auto parameters = sender->GetParameters();
  parameters.encodings[0].active = true;
  parameters.encodings[1].active = false;
  parameters.encodings[2].active = true;
  EXPECT_TRUE(sender->SetParameters(parameters).ok());

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);

  // We have three outbound-rtps.
  scoped_refptr<const RTCStatsReport> report = GetStats(local_pc_wrapper);
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_EQ(outbound_rtps.size(), 3u);
  // SSRC and RTX is unique.
  std::set<uint32_t> ssrcs;
  std::set<uint32_t> rtx_ssrcs;
  for (const auto& outbound_rtp : outbound_rtps) {
    ASSERT_TRUE(outbound_rtp->ssrc.has_value());
    ASSERT_TRUE(outbound_rtp->rtx_ssrc.has_value());
    ssrcs.insert(*outbound_rtp->ssrc);
    rtx_ssrcs.insert(*outbound_rtp->rtx_ssrc);
  }
  EXPECT_EQ(ssrcs.size(), 3u);
  EXPECT_EQ(rtx_ssrcs.size(), 3u);
  // RIDs and active are set.
  auto outbound_rtp_by_rid = GetOutboundRtpStreamStatsByRid(report);
  EXPECT_THAT(
      outbound_rtp_by_rid,
      UnorderedElementsAre(Pair("q", Active()), Pair("h", Not(Active())),
                           Pair("f", Active())));
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       EncodingParameterCodecIsEmptyWhenCreatedAudio) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::AUDIO);
  scoped_refptr<RtpTransceiverInterface> audio_transceiver =
      transceiver_or_error.MoveValue();
  RtpParameters parameters = audio_transceiver->sender()->GetParameters();
  EXPECT_FALSE(parameters.encodings[0].codec.has_value());
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       EncodingParameterCodecIsEmptyWhenCreatedVideo) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::VIDEO);
  scoped_refptr<RtpTransceiverInterface> video_transceiver =
      transceiver_or_error.MoveValue();
  RtpParameters parameters = video_transceiver->sender()->GetParameters();
  EXPECT_FALSE(parameters.encodings[0].codec.has_value());
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       EncodingParameterCodecIsSetByAddTransceiverAudio) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  scoped_refptr<MediaStreamInterface> stream = local_pc_wrapper->GetUserMedia(
      /*audio=*/true, {}, /*video=*/false, {});
  scoped_refptr<AudioTrackInterface> track = stream->GetAudioTracks()[0];

  std::optional<RtpCodecCapability> pcmu =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::AUDIO, "pcmu");
  ASSERT_TRUE(pcmu);

  RtpTransceiverInit init;
  init.direction = RtpTransceiverDirection::kSendOnly;
  RtpEncodingParameters encoding_parameters;
  encoding_parameters.codec = pcmu;
  init.send_encodings.push_back(encoding_parameters);

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(track, init);
  scoped_refptr<RtpTransceiverInterface> audio_transceiver =
      transceiver_or_error.MoveValue();
  RtpParameters parameters = audio_transceiver->sender()->GetParameters();
  EXPECT_EQ(*parameters.encodings[0].codec, *pcmu);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  ASSERT_TRUE(local_pc_wrapper->WaitForConnection());
  ASSERT_TRUE(remote_pc_wrapper->WaitForConnection());

  scoped_refptr<const RTCStatsReport> report = GetStats(local_pc_wrapper);
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_EQ(outbound_rtps.size(), 1u);
  std::string codec_name = GetCurrentCodecMimeType(report, *outbound_rtps[0]);
  EXPECT_STRCASEEQ(("audio/" + pcmu->name).c_str(), codec_name.c_str());
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       EncodingParameterCodecIsSetByAddTransceiverVideo) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  scoped_refptr<MediaStreamInterface> stream = local_pc_wrapper->GetUserMedia(
      /*audio=*/false, {}, /*video=*/true, {.width = 1280, .height = 720});
  scoped_refptr<VideoTrackInterface> track = stream->GetVideoTracks()[0];

  std::optional<RtpCodecCapability> vp9 =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::VIDEO, "vp9");
  ASSERT_TRUE(vp9);

  RtpTransceiverInit init;
  init.direction = RtpTransceiverDirection::kSendOnly;
  RtpEncodingParameters encoding_parameters;
  encoding_parameters.codec = vp9;
  encoding_parameters.scalability_mode = "L3T3";
  init.send_encodings.push_back(encoding_parameters);

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(track, init);
  scoped_refptr<RtpTransceiverInterface> audio_transceiver =
      transceiver_or_error.MoveValue();
  RtpParameters parameters = audio_transceiver->sender()->GetParameters();
  EXPECT_EQ(*parameters.encodings[0].codec, *vp9);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  auto error_or_stats =
      GetStatsUntil(local_pc_wrapper,
                    OutboundRtpStatsAre(Contains(ScalabilityModeIs("L3T3"))));
  ASSERT_THAT(error_or_stats, IsRtcOk());
  scoped_refptr<const RTCStatsReport> report = error_or_stats.MoveValue();
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_EQ(outbound_rtps.size(), 1u);
  std::string codec_name = GetCurrentCodecMimeType(report, *outbound_rtps[0]);
  EXPECT_STRCASEEQ(("video/" + vp9->name).c_str(), codec_name.c_str());
  EXPECT_EQ(outbound_rtps[0]->scalability_mode.value(), "L3T3");
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       EncodingParameterCodecIsSetBySetParametersBeforeNegotiationAudio) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  scoped_refptr<MediaStreamInterface> stream = local_pc_wrapper->GetUserMedia(
      /*audio=*/true, {}, /*video=*/false, {});
  scoped_refptr<AudioTrackInterface> track = stream->GetAudioTracks()[0];

  std::optional<RtpCodecCapability> pcmu =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::AUDIO, "pcmu");

  auto transceiver_or_error = local_pc_wrapper->pc()->AddTransceiver(track);
  scoped_refptr<RtpTransceiverInterface> audio_transceiver =
      transceiver_or_error.MoveValue();
  RtpParameters parameters = audio_transceiver->sender()->GetParameters();
  parameters.encodings[0].codec = pcmu;
  EXPECT_TRUE(audio_transceiver->sender()->SetParameters(parameters).ok());

  parameters = audio_transceiver->sender()->GetParameters();
  EXPECT_EQ(parameters.encodings[0].codec, pcmu);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  scoped_refptr<const RTCStatsReport> report = GetStats(local_pc_wrapper);
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_EQ(outbound_rtps.size(), 1u);
  std::string codec_name = GetCurrentCodecMimeType(report, *outbound_rtps[0]);
  EXPECT_STRCASEEQ(("audio/" + pcmu->name).c_str(), codec_name.c_str());
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       EncodingParameterCodecIsSetBySetParametersAfterNegotiationAudio) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  scoped_refptr<MediaStreamInterface> stream = local_pc_wrapper->GetUserMedia(
      /*audio=*/true, {}, /*video=*/false, {});
  scoped_refptr<AudioTrackInterface> track = stream->GetAudioTracks()[0];

  std::optional<RtpCodecCapability> pcmu =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::AUDIO, "pcmu");

  auto transceiver_or_error = local_pc_wrapper->pc()->AddTransceiver(track);
  scoped_refptr<RtpTransceiverInterface> audio_transceiver =
      transceiver_or_error.MoveValue();

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  scoped_refptr<const RTCStatsReport> report = GetStats(local_pc_wrapper);
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_EQ(outbound_rtps.size(), 1u);
  std::string codec_name = GetCurrentCodecMimeType(report, *outbound_rtps[0]);
  EXPECT_STRCASENE(("audio/" + pcmu->name).c_str(), codec_name.c_str());
  std::string last_codec_id = outbound_rtps[0]->codec_id.value();

  RtpParameters parameters = audio_transceiver->sender()->GetParameters();
  parameters.encodings[0].codec = pcmu;
  EXPECT_TRUE(audio_transceiver->sender()->SetParameters(parameters).ok());

  parameters = audio_transceiver->sender()->GetParameters();
  EXPECT_EQ(parameters.encodings[0].codec, pcmu);

  auto error_or_stats =
      GetStatsUntil(local_pc_wrapper,
                    OutboundRtpStatsAre(Contains(CodecIs(Ne(last_codec_id)))));
  ASSERT_THAT(error_or_stats, IsRtcOk());
  report = error_or_stats.MoveValue();
  outbound_rtps = report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_EQ(outbound_rtps.size(), 1u);
  codec_name = GetCurrentCodecMimeType(report, *outbound_rtps[0]);
  EXPECT_STRCASEEQ(("audio/" + pcmu->name).c_str(), codec_name.c_str());
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       EncodingParameterCodecIsSetBySetParametersBeforeNegotiationVideo) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  scoped_refptr<MediaStreamInterface> stream = local_pc_wrapper->GetUserMedia(
      /*audio=*/false, {}, /*video=*/true, {.width = 1280, .height = 720});
  scoped_refptr<VideoTrackInterface> track = stream->GetVideoTracks()[0];

  std::optional<RtpCodecCapability> vp9 =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::VIDEO, "vp9");

  auto transceiver_or_error = local_pc_wrapper->pc()->AddTransceiver(track);
  scoped_refptr<RtpTransceiverInterface> video_transceiver =
      transceiver_or_error.MoveValue();
  RtpParameters parameters = video_transceiver->sender()->GetParameters();
  parameters.encodings[0].codec = vp9;
  parameters.encodings[0].scalability_mode = "L3T3";
  EXPECT_TRUE(video_transceiver->sender()->SetParameters(parameters).ok());

  parameters = video_transceiver->sender()->GetParameters();
  EXPECT_EQ(parameters.encodings[0].codec, vp9);
  EXPECT_EQ(parameters.encodings[0].scalability_mode, "L3T3");

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  auto error_or_stats = GetStatsUntil(
      local_pc_wrapper, OutboundRtpStatsAre(Contains(AllOf(
                            ScalabilityModeIs("L3T3"), CodecIs(Ne(""))))));
  ASSERT_THAT(error_or_stats, IsRtcOk());
  scoped_refptr<const RTCStatsReport> report = error_or_stats.MoveValue();
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_EQ(outbound_rtps.size(), 1u);
  std::string codec_name = GetCurrentCodecMimeType(report, *outbound_rtps[0]);
  EXPECT_STRCASEEQ(("video/" + vp9->name).c_str(), codec_name.c_str());
  EXPECT_EQ(outbound_rtps[0]->scalability_mode.value_or(""), "L3T3");
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       EncodingParameterCodecIsSetBySetParametersAfterNegotiationVideo) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  scoped_refptr<MediaStreamInterface> stream = local_pc_wrapper->GetUserMedia(
      /*audio=*/false, {}, /*video=*/true, {.width = 1280, .height = 720});
  scoped_refptr<VideoTrackInterface> track = stream->GetVideoTracks()[0];

  std::optional<RtpCodecCapability> vp9 =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::VIDEO, "vp9");

  auto transceiver_or_error = local_pc_wrapper->pc()->AddTransceiver(track);
  scoped_refptr<RtpTransceiverInterface> video_transceiver =
      transceiver_or_error.MoveValue();

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  scoped_refptr<const RTCStatsReport> report = GetStats(local_pc_wrapper);
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_EQ(outbound_rtps.size(), 1u);
  std::string codec_name = GetCurrentCodecMimeType(report, *outbound_rtps[0]);
  EXPECT_STRCASENE(("audio/" + vp9->name).c_str(), codec_name.c_str());
  std::string last_codec_id = outbound_rtps[0]->codec_id.value();

  RtpParameters parameters = video_transceiver->sender()->GetParameters();
  parameters.encodings[0].codec = vp9;
  parameters.encodings[0].scalability_mode = "L3T3";
  EXPECT_TRUE(video_transceiver->sender()->SetParameters(parameters).ok());

  parameters = video_transceiver->sender()->GetParameters();
  EXPECT_EQ(parameters.encodings[0].codec, vp9);
  EXPECT_EQ(parameters.encodings[0].scalability_mode, "L3T3");

  auto error_or_stats = GetStatsUntil(
      local_pc_wrapper,
      OutboundRtpStatsAre(Contains(
          AllOf(ScalabilityModeIs("L3T3"), CodecIs(Ne(last_codec_id))))));
  ASSERT_THAT(error_or_stats, IsRtcOk());
  report = error_or_stats.MoveValue();
  outbound_rtps = report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_EQ(outbound_rtps.size(), 1u);
  codec_name = GetCurrentCodecMimeType(report, *outbound_rtps[0]);
  EXPECT_STRCASEEQ(("video/" + vp9->name).c_str(), codec_name.c_str());
  EXPECT_EQ(outbound_rtps[0]->scalability_mode.value(), "L3T3");
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       AddTransceiverRejectsUnknownCodecParameterAudio) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();

  RtpCodec dummy_codec;
  dummy_codec.kind = MediaType::AUDIO;
  dummy_codec.name = "FOOBAR";
  dummy_codec.clock_rate = 90000;
  dummy_codec.num_channels = 2;

  RtpTransceiverInit init;
  init.direction = RtpTransceiverDirection::kSendOnly;
  RtpEncodingParameters encoding_parameters;
  encoding_parameters.codec = dummy_codec;
  init.send_encodings.push_back(encoding_parameters);

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::AUDIO, init);
  EXPECT_FALSE(transceiver_or_error.ok());
  EXPECT_EQ(transceiver_or_error.error().type(),
            RTCErrorType::UNSUPPORTED_OPERATION);
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       AddTransceiverRejectsUnknownCodecParameterVideo) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();

  RtpCodec dummy_codec;
  dummy_codec.kind = MediaType::VIDEO;
  dummy_codec.name = "FOOBAR";
  dummy_codec.clock_rate = 90000;

  RtpTransceiverInit init;
  init.direction = RtpTransceiverDirection::kSendOnly;
  RtpEncodingParameters encoding_parameters;
  encoding_parameters.codec = dummy_codec;
  init.send_encodings.push_back(encoding_parameters);

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::VIDEO, init);
  EXPECT_FALSE(transceiver_or_error.ok());
  EXPECT_EQ(transceiver_or_error.error().type(),
            RTCErrorType::UNSUPPORTED_OPERATION);
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       SetParametersRejectsUnknownCodecParameterAudio) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();

  RtpCodec dummy_codec;
  dummy_codec.kind = MediaType::AUDIO;
  dummy_codec.name = "FOOBAR";
  dummy_codec.clock_rate = 90000;
  dummy_codec.num_channels = 2;

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::AUDIO);
  ASSERT_TRUE(transceiver_or_error.ok());
  scoped_refptr<RtpTransceiverInterface> audio_transceiver =
      transceiver_or_error.MoveValue();

  RtpParameters parameters = audio_transceiver->sender()->GetParameters();
  parameters.encodings[0].codec = dummy_codec;
  RTCError error = audio_transceiver->sender()->SetParameters(parameters);
  EXPECT_EQ(error.type(), RTCErrorType::INVALID_MODIFICATION);
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       SetParametersRejectsUnknownCodecParameterVideo) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();

  RtpCodec dummy_codec;
  dummy_codec.kind = MediaType::VIDEO;
  dummy_codec.name = "FOOBAR";
  dummy_codec.clock_rate = 90000;

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::VIDEO);
  ASSERT_TRUE(transceiver_or_error.ok());
  scoped_refptr<RtpTransceiverInterface> video_transceiver =
      transceiver_or_error.MoveValue();

  RtpParameters parameters = video_transceiver->sender()->GetParameters();
  parameters.encodings[0].codec = dummy_codec;
  RTCError error = video_transceiver->sender()->SetParameters(parameters);
  EXPECT_EQ(error.type(), RTCErrorType::INVALID_MODIFICATION);
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       SetParametersRejectsNonNegotiatedCodecParameterAudio) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::optional<RtpCodecCapability> opus =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::AUDIO, "opus");
  ASSERT_TRUE(opus);

  std::vector<RtpCodecCapability> not_opus_codecs =
      local_pc_wrapper->pc_factory()
          ->GetRtpSenderCapabilities(MediaType::AUDIO)
          .codecs;
  std::erase_if(not_opus_codecs, [&](const auto& codec) {
    return absl::EqualsIgnoreCase(codec.name, opus->name);
  });

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::AUDIO);
  ASSERT_TRUE(transceiver_or_error.ok());
  scoped_refptr<RtpTransceiverInterface> audio_transceiver =
      transceiver_or_error.MoveValue();
  ASSERT_TRUE(audio_transceiver->SetCodecPreferences(not_opus_codecs).ok());

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  RtpParameters parameters = audio_transceiver->sender()->GetParameters();
  parameters.encodings[0].codec = opus;
  RTCError error = audio_transceiver->sender()->SetParameters(parameters);
  EXPECT_EQ(error.type(), RTCErrorType::INVALID_MODIFICATION);
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       SetParametersRejectsNonRemotelyNegotiatedCodecParameterAudio) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::optional<RtpCodecCapability> opus =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::AUDIO, "opus");
  ASSERT_TRUE(opus);

  std::vector<RtpCodecCapability> not_opus_codecs =
      local_pc_wrapper->pc_factory()
          ->GetRtpSenderCapabilities(MediaType::AUDIO)
          .codecs;
  std::erase_if(not_opus_codecs, [&](const auto& codec) {
    return absl::EqualsIgnoreCase(codec.name, opus->name);
  });

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::AUDIO);
  ASSERT_TRUE(transceiver_or_error.ok());
  scoped_refptr<RtpTransceiverInterface> audio_transceiver =
      transceiver_or_error.MoveValue();

  // Negotiation, create offer and apply it
  std::unique_ptr<SessionDescriptionInterface> offer =
      CreateOffer(local_pc_wrapper);
  scoped_refptr<MockSetSessionDescriptionObserver> p1 =
      SetLocalDescription(local_pc_wrapper, offer.get());
  scoped_refptr<MockSetSessionDescriptionObserver> p2 =
      SetRemoteDescription(remote_pc_wrapper, offer.get());
  EXPECT_TRUE(Await({p1, p2}));

  // Update the remote transceiver to reject Opus
  std::vector<scoped_refptr<RtpTransceiverInterface>> remote_transceivers =
      remote_pc_wrapper->pc()->GetTransceivers();
  ASSERT_TRUE(!remote_transceivers.empty());
  scoped_refptr<RtpTransceiverInterface> remote_audio_transceiver =
      remote_transceivers[0];
  ASSERT_TRUE(
      remote_audio_transceiver->SetCodecPreferences(not_opus_codecs).ok());

  // Create answer and apply it
  std::unique_ptr<SessionDescriptionInterface> answer =
      CreateAnswer(remote_pc_wrapper);
  p1 = SetLocalDescription(remote_pc_wrapper, answer.get());
  p2 = SetRemoteDescription(local_pc_wrapper, answer.get());
  EXPECT_TRUE(Await({p1, p2}));

  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  RtpParameters parameters = audio_transceiver->sender()->GetParameters();
  parameters.encodings[0].codec = opus;
  RTCError error = audio_transceiver->sender()->SetParameters(parameters);
  EXPECT_EQ(error.type(), RTCErrorType::INVALID_MODIFICATION);
}

// Test coverage for https://crbug.com/webrtc/391340599.
// Some web apps add non-standard FMTP parameters to video codecs and because
// they get successfully negotiated due to being ignored by SDP rules, they show
// up in GetParameters().codecs. Using SetParameters() with such codecs should
// still work.
TEST_F(PeerConnectionEncodingsIntegrationTest,
       SetParametersAcceptsMungedCodecFromGetParameters) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper =
      CreatePc("WebRTC-NoSdpMangleAllowForTesting/Enabled,85/");
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper =
      CreatePc("WebRTC-NoSdpMangleAllowForTesting/Enabled,85/");
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::VIDEO);
  ASSERT_TRUE(transceiver_or_error.ok());
  scoped_refptr<RtpTransceiverInterface> video_transceiver =
      transceiver_or_error.MoveValue();

  std::unique_ptr<SessionDescriptionInterface> offer =
      CreateOffer(local_pc_wrapper);
  // Munge a new parameter for VP8 in the offer.
  auto* mcd = offer->description()->contents()[0].media_description();
  ASSERT_THAT(mcd, NotNull());
  std::vector<Codec> codecs = mcd->codecs();
  ASSERT_THAT(codecs, Contains(Field(&Codec::name, "VP8")));
  auto vp8_codec = absl::c_find_if(
      codecs, [](const Codec& codec) { return codec.name == "VP8"; });
  vp8_codec->params.emplace("non-standard-param", "true");
  mcd->set_codecs(codecs);

  scoped_refptr<MockSetSessionDescriptionObserver> p1 =
      SetLocalDescription(local_pc_wrapper, offer.get());
  scoped_refptr<MockSetSessionDescriptionObserver> p2 =
      SetRemoteDescription(remote_pc_wrapper, offer.get());
  EXPECT_TRUE(Await({p1, p2}));

  // Create answer and apply it
  std::unique_ptr<SessionDescriptionInterface> answer =
      CreateAnswer(remote_pc_wrapper);
  mcd = answer->description()->contents()[0].media_description();
  ASSERT_THAT(mcd, NotNull());
  codecs = mcd->codecs();
  ASSERT_THAT(codecs, Contains(Field(&Codec::name, "VP8")));
  vp8_codec = absl::c_find_if(
      codecs, [](const Codec& codec) { return codec.name == "VP8"; });
  vp8_codec->params.emplace("non-standard-param", "true");
  mcd->set_codecs(codecs);
  p1 = SetLocalDescription(remote_pc_wrapper, answer.get());
  p2 = SetRemoteDescription(local_pc_wrapper, answer.get());
  EXPECT_TRUE(Await({p1, p2}));

  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  RtpParameters parameters = video_transceiver->sender()->GetParameters();
  auto it = absl::c_find_if(
      parameters.codecs, [](const auto& codec) { return codec.name == "VP8"; });
  ASSERT_NE(it, parameters.codecs.end());
  RtpCodecParameters& vp8_codec_from_parameters = *it;
  EXPECT_THAT(vp8_codec_from_parameters.parameters,
              Contains(Pair("non-standard-param", "true")));
  parameters.encodings[0].codec = vp8_codec_from_parameters;

  EXPECT_THAT(video_transceiver->sender()->SetParameters(parameters),
              IsRtcOk());
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       SetParametersRejectsNonNegotiatedCodecParameterVideo) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::optional<RtpCodecCapability> vp8 =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::VIDEO, "vp8");
  ASSERT_TRUE(vp8);

  std::vector<RtpCodecCapability> not_vp8_codecs =
      local_pc_wrapper->pc_factory()
          ->GetRtpSenderCapabilities(MediaType::VIDEO)
          .codecs;
  std::erase_if(not_vp8_codecs, [&](const auto& codec) {
    return absl::EqualsIgnoreCase(codec.name, vp8->name);
  });

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::VIDEO);
  ASSERT_TRUE(transceiver_or_error.ok());
  scoped_refptr<RtpTransceiverInterface> video_transceiver =
      transceiver_or_error.MoveValue();
  ASSERT_TRUE(video_transceiver->SetCodecPreferences(not_vp8_codecs).ok());

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  RtpParameters parameters = video_transceiver->sender()->GetParameters();
  parameters.encodings[0].codec = vp8;
  RTCError error = video_transceiver->sender()->SetParameters(parameters);
  EXPECT_EQ(error.type(), RTCErrorType::INVALID_MODIFICATION);
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       SetParametersRejectsNonRemotelyNegotiatedCodecParameterVideo) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::optional<RtpCodecCapability> vp8 =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::VIDEO, "vp8");
  ASSERT_TRUE(vp8);

  std::vector<RtpCodecCapability> not_vp8_codecs =
      local_pc_wrapper->pc_factory()
          ->GetRtpSenderCapabilities(MediaType::VIDEO)
          .codecs;
  std::erase_if(not_vp8_codecs, [&](const auto& codec) {
    return absl::EqualsIgnoreCase(codec.name, vp8->name);
  });

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::VIDEO);
  ASSERT_TRUE(transceiver_or_error.ok());
  scoped_refptr<RtpTransceiverInterface> video_transceiver =
      transceiver_or_error.MoveValue();

  // Negotiation, create offer and apply it
  std::unique_ptr<SessionDescriptionInterface> offer =
      CreateOffer(local_pc_wrapper);
  scoped_refptr<MockSetSessionDescriptionObserver> p1 =
      SetLocalDescription(local_pc_wrapper, offer.get());
  scoped_refptr<MockSetSessionDescriptionObserver> p2 =
      SetRemoteDescription(remote_pc_wrapper, offer.get());
  EXPECT_TRUE(Await({p1, p2}));

  // Update the remote transceiver to reject VP8
  std::vector<scoped_refptr<RtpTransceiverInterface>> remote_transceivers =
      remote_pc_wrapper->pc()->GetTransceivers();
  ASSERT_TRUE(!remote_transceivers.empty());
  scoped_refptr<RtpTransceiverInterface> remote_video_transceiver =
      remote_transceivers[0];
  ASSERT_TRUE(
      remote_video_transceiver->SetCodecPreferences(not_vp8_codecs).ok());

  // Create answer and apply it
  std::unique_ptr<SessionDescriptionInterface> answer =
      CreateAnswer(remote_pc_wrapper);
  p1 = SetLocalDescription(remote_pc_wrapper, answer.get());
  p2 = SetRemoteDescription(local_pc_wrapper, answer.get());
  EXPECT_TRUE(Await({p1, p2}));

  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  RtpParameters parameters = video_transceiver->sender()->GetParameters();
  parameters.encodings[0].codec = vp8;
  RTCError error = video_transceiver->sender()->SetParameters(parameters);
  EXPECT_EQ(error.type(), RTCErrorType::INVALID_MODIFICATION);
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       EncodingParametersCodecRemovedAfterNegotiationAudio) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::optional<RtpCodecCapability> opus =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::AUDIO, "opus");
  ASSERT_TRUE(opus);

  std::vector<RtpCodecCapability> not_opus_codecs =
      local_pc_wrapper->pc_factory()
          ->GetRtpSenderCapabilities(MediaType::AUDIO)
          .codecs;
  std::erase_if(not_opus_codecs, [&](const auto& codec) {
    return absl::EqualsIgnoreCase(codec.name, opus->name);
  });

  RtpTransceiverInit init;
  init.direction = RtpTransceiverDirection::kSendOnly;
  RtpEncodingParameters encoding_parameters;
  encoding_parameters.codec = opus;
  init.send_encodings.push_back(encoding_parameters);

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::AUDIO, init);
  ASSERT_TRUE(transceiver_or_error.ok());
  scoped_refptr<RtpTransceiverInterface> audio_transceiver =
      transceiver_or_error.MoveValue();

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  RtpParameters parameters = audio_transceiver->sender()->GetParameters();
  EXPECT_EQ(parameters.encodings[0].codec, opus);

  ASSERT_TRUE(audio_transceiver->SetCodecPreferences(not_opus_codecs).ok());
  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);

  parameters = audio_transceiver->sender()->GetParameters();
  EXPECT_FALSE(parameters.encodings[0].codec);
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       EncodingParametersRedEnabledBeforeNegotiationAudio) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<RtpCodecCapability> send_codecs =
      local_pc_wrapper->pc_factory()
          ->GetRtpSenderCapabilities(MediaType::AUDIO)
          .codecs;

  std::optional<RtpCodecCapability> opus =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::AUDIO, "opus");
  ASSERT_TRUE(opus);

  std::optional<RtpCodecCapability> red =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::AUDIO, "red");
  ASSERT_TRUE(red);

  RtpTransceiverInit init;
  init.direction = RtpTransceiverDirection::kSendOnly;
  RtpEncodingParameters encoding_parameters;
  encoding_parameters.codec = opus;
  init.send_encodings.push_back(encoding_parameters);

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::AUDIO, init);
  ASSERT_TRUE(transceiver_or_error.ok());
  scoped_refptr<RtpTransceiverInterface> audio_transceiver =
      transceiver_or_error.MoveValue();

  // Preferring RED over Opus should enable RED with Opus encoding.
  send_codecs[0] = red.value();
  send_codecs[1] = opus.value();

  ASSERT_TRUE(audio_transceiver->SetCodecPreferences(send_codecs).ok());
  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  RtpParameters parameters = audio_transceiver->sender()->GetParameters();
  EXPECT_EQ(parameters.encodings[0].codec, opus);
  EXPECT_EQ(parameters.codecs[0].name, red->name);

  // Check that it's possible to switch back to Opus without RED.
  send_codecs[0] = opus.value();
  send_codecs[1] = red.value();

  ASSERT_TRUE(audio_transceiver->SetCodecPreferences(send_codecs).ok());
  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);

  parameters = audio_transceiver->sender()->GetParameters();
  EXPECT_EQ(parameters.encodings[0].codec, opus);
  EXPECT_EQ(parameters.codecs[0].name, opus->name);
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       EncodingParametersRedEnabledBeforeNegotiationAudioWithFieldTrial) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper =
      CreatePc("WebRTC-PayloadTypesInTransport/Enabled/");
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper =
      CreatePc("WebRTC-PayloadTypesInTransport/Enabled/");
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<RtpCodecCapability> send_codecs =
      local_pc_wrapper->pc_factory()
          ->GetRtpSenderCapabilities(MediaType::AUDIO)
          .codecs;

  std::optional<RtpCodecCapability> opus =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::AUDIO, "opus");
  ASSERT_TRUE(opus);

  std::optional<RtpCodecCapability> red =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::AUDIO, "red");
  ASSERT_TRUE(red);

  RtpTransceiverInit init;
  init.direction = RtpTransceiverDirection::kSendOnly;
  RtpEncodingParameters encoding_parameters;
  encoding_parameters.codec = opus;
  init.send_encodings.push_back(encoding_parameters);

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::AUDIO, init);
  ASSERT_TRUE(transceiver_or_error.ok());
  scoped_refptr<RtpTransceiverInterface> audio_transceiver =
      transceiver_or_error.MoveValue();

  // Preferring RED over Opus should enable RED with Opus encoding.
  send_codecs[0] = red.value();
  send_codecs[1] = opus.value();

  ASSERT_TRUE(audio_transceiver->SetCodecPreferences(send_codecs).ok());
  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  RtpParameters parameters = audio_transceiver->sender()->GetParameters();
  EXPECT_EQ(parameters.encodings[0].codec, opus);
  EXPECT_EQ(parameters.codecs[0].name, red->name);

  // Check that it's possible to switch back to Opus without RED.
  send_codecs[0] = opus.value();
  send_codecs[1] = red.value();

  ASSERT_TRUE(audio_transceiver->SetCodecPreferences(send_codecs).ok());
  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);

  parameters = audio_transceiver->sender()->GetParameters();
  EXPECT_EQ(parameters.encodings[0].codec, opus);
  EXPECT_EQ(parameters.codecs[0].name, opus->name);
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       SetParametersRejectsScalabilityModeForSelectedCodec) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();

  std::optional<RtpCodecCapability> vp8 =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::VIDEO, "vp8");
  ASSERT_TRUE(vp8);

  RtpTransceiverInit init;
  init.direction = RtpTransceiverDirection::kSendOnly;
  RtpEncodingParameters encoding_parameters;
  encoding_parameters.codec = vp8;
  encoding_parameters.scalability_mode = "L1T3";
  init.send_encodings.push_back(encoding_parameters);

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::VIDEO, init);
  ASSERT_TRUE(transceiver_or_error.ok());
  scoped_refptr<RtpTransceiverInterface> video_transceiver =
      transceiver_or_error.MoveValue();

  RtpParameters parameters = video_transceiver->sender()->GetParameters();
  parameters.encodings[0].scalability_mode = "L3T3";
  RTCError error = video_transceiver->sender()->SetParameters(parameters);
  EXPECT_EQ(error.type(), RTCErrorType::INVALID_MODIFICATION);
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       EncodingParametersCodecRemovedByNegotiationVideo) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::optional<RtpCodecCapability> vp8 =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::VIDEO, "vp8");
  ASSERT_TRUE(vp8);

  std::vector<RtpCodecCapability> not_vp8_codecs =
      local_pc_wrapper->pc_factory()
          ->GetRtpSenderCapabilities(MediaType::VIDEO)
          .codecs;
  std::erase_if(not_vp8_codecs, [&](const auto& codec) {
    return absl::EqualsIgnoreCase(codec.name, vp8->name);
  });

  RtpTransceiverInit init;
  init.direction = RtpTransceiverDirection::kSendOnly;
  RtpEncodingParameters encoding_parameters;
  encoding_parameters.rid = "h";
  encoding_parameters.codec = vp8;
  encoding_parameters.scale_resolution_down_by = 2;
  init.send_encodings.push_back(encoding_parameters);
  encoding_parameters.rid = "f";
  encoding_parameters.scale_resolution_down_by = 1;
  init.send_encodings.push_back(encoding_parameters);

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::VIDEO, init);
  ASSERT_TRUE(transceiver_or_error.ok());
  scoped_refptr<RtpTransceiverInterface> video_transceiver =
      transceiver_or_error.MoveValue();

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  RtpParameters parameters = video_transceiver->sender()->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 2u);
  EXPECT_EQ(parameters.encodings[0].codec, vp8);
  EXPECT_EQ(parameters.encodings[1].codec, vp8);

  ASSERT_TRUE(video_transceiver->SetCodecPreferences(not_vp8_codecs).ok());
  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);

  parameters = video_transceiver->sender()->GetParameters();
  EXPECT_FALSE(parameters.encodings[0].codec);
  EXPECT_FALSE(parameters.encodings[1].codec);
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       AddTransceiverRejectsMixedCodecSimulcast) {
  // Mixed Codec Simulcast is not yet supported, so we ensure that we reject
  // such parameters.
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::optional<RtpCodecCapability> vp8 =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::VIDEO, "vp8");
  ASSERT_TRUE(vp8);
  std::optional<RtpCodecCapability> vp9 =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::VIDEO, "vp9");
  ASSERT_TRUE(vp9);

  RtpTransceiverInit init;
  init.direction = RtpTransceiverDirection::kSendOnly;
  RtpEncodingParameters encoding_parameters;
  encoding_parameters.rid = "h";
  encoding_parameters.codec = vp8;
  encoding_parameters.scale_resolution_down_by = 2;
  init.send_encodings.push_back(encoding_parameters);
  encoding_parameters.rid = "f";
  encoding_parameters.codec = vp9;
  encoding_parameters.scale_resolution_down_by = 1;
  init.send_encodings.push_back(encoding_parameters);

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::VIDEO, init);
  ASSERT_FALSE(transceiver_or_error.ok());
  EXPECT_EQ(transceiver_or_error.error().type(),
            RTCErrorType::UNSUPPORTED_OPERATION);
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       AddTransceiverAcceptsMixedCodecSimulcast) {
  // Enable WIP mixed codec simulcast support
  std::string field_trials = "WebRTC-MixedCodecSimulcast/Enabled/";
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper =
      CreatePc(field_trials);
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper =
      CreatePc(field_trials);
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::optional<RtpCodecCapability> vp8 =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::VIDEO, "vp8");
  ASSERT_TRUE(vp8);
  std::optional<RtpCodecCapability> vp9 =
      local_pc_wrapper->FindFirstSendCodecWithName(MediaType::VIDEO, "vp9");
  ASSERT_TRUE(vp9);

  RtpTransceiverInit init;
  init.direction = RtpTransceiverDirection::kSendOnly;
  RtpEncodingParameters encoding_parameters;
  encoding_parameters.rid = "h";
  encoding_parameters.codec = vp8;
  encoding_parameters.scale_resolution_down_by = 2;
  init.send_encodings.push_back(encoding_parameters);
  encoding_parameters.rid = "f";
  encoding_parameters.codec = vp9;
  encoding_parameters.scale_resolution_down_by = 1;
  init.send_encodings.push_back(encoding_parameters);

  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::VIDEO, init);
  ASSERT_TRUE(transceiver_or_error.ok());
}

TEST_F(PeerConnectionEncodingsIntegrationTest, ScaleToParameterChecking) {
  scoped_refptr<PeerConnectionTestWrapper> pc_wrapper = CreatePc();

  // AddTransceiver: If `scale_resolution_down_to` is specified on any encoding
  // it must be specified on all encodings.
  RtpTransceiverInit init;
  RtpEncodingParameters encoding;
  encoding.scale_resolution_down_to = std::nullopt;
  init.send_encodings.push_back(encoding);
  encoding.scale_resolution_down_to = {.width = 1280, .height = 720};
  init.send_encodings.push_back(encoding);
  auto transceiver_or_error =
      pc_wrapper->pc()->AddTransceiver(MediaType::VIDEO, init);
  EXPECT_FALSE(transceiver_or_error.ok());
  EXPECT_EQ(transceiver_or_error.error().type(),
            RTCErrorType::UNSUPPORTED_OPERATION);

  // AddTransceiver: Width and height must not be zero.
  init.send_encodings[0].scale_resolution_down_to = {.width = 1280,
                                                     .height = 0};
  init.send_encodings[1].scale_resolution_down_to = {.width = 0, .height = 720};
  transceiver_or_error =
      pc_wrapper->pc()->AddTransceiver(MediaType::VIDEO, init);
  EXPECT_FALSE(transceiver_or_error.ok());
  EXPECT_EQ(transceiver_or_error.error().type(),
            RTCErrorType::UNSUPPORTED_OPERATION);

  // AddTransceiver: Specifying both `scale_resolution_down_to` and
  // `scale_resolution_down_by` is allowed (the latter is ignored).
  init.send_encodings[0].scale_resolution_down_to = {.width = 640,
                                                     .height = 480};
  init.send_encodings[0].scale_resolution_down_by = 1.0;
  init.send_encodings[1].scale_resolution_down_to = {.width = 1280,
                                                     .height = 720};
  init.send_encodings[1].scale_resolution_down_by = 2.0;
  transceiver_or_error =
      pc_wrapper->pc()->AddTransceiver(MediaType::VIDEO, init);
  ASSERT_TRUE(transceiver_or_error.ok());

  // SetParameters: If `scale_resolution_down_to` is specified on any active
  // encoding it must be specified on all active encodings.
  auto sender = transceiver_or_error.value()->sender();
  auto parameters = sender->GetParameters();
  parameters.encodings[0].scale_resolution_down_to = {.width = 640,
                                                      .height = 480};
  parameters.encodings[1].scale_resolution_down_to = std::nullopt;
  auto error = sender->SetParameters(parameters);
  EXPECT_FALSE(error.ok());
  EXPECT_EQ(error.type(), RTCErrorType::INVALID_MODIFICATION);
  // But it's OK not to specify `scale_resolution_down_to` on an inactive
  // encoding.
  parameters = sender->GetParameters();
  parameters.encodings[0].scale_resolution_down_to = {.width = 640,
                                                      .height = 480};
  parameters.encodings[1].active = false;
  parameters.encodings[1].scale_resolution_down_to = std::nullopt;
  error = sender->SetParameters(parameters);
  EXPECT_TRUE(error.ok());

  // SetParameters: Width and height must not be zero.
  sender = transceiver_or_error.value()->sender();
  parameters = sender->GetParameters();
  parameters.encodings[0].scale_resolution_down_to = {.width = 1280,
                                                      .height = 0};
  parameters.encodings[1].active = true;
  parameters.encodings[1].scale_resolution_down_to = {.width = 0,
                                                      .height = 720};
  error = sender->SetParameters(parameters);
  EXPECT_FALSE(error.ok());
  EXPECT_EQ(error.type(), RTCErrorType::INVALID_MODIFICATION);

  // SetParameters: Specifying both `scale_resolution_down_to` and
  // `scale_resolution_down_by` is allowed (the latter is ignored).
  parameters = sender->GetParameters();
  parameters.encodings[0].scale_resolution_down_to = {.width = 640,
                                                      .height = 480};
  parameters.encodings[0].scale_resolution_down_by = 2.0;
  parameters.encodings[1].scale_resolution_down_to = {.width = 1280,
                                                      .height = 720};
  parameters.encodings[1].scale_resolution_down_by = 1.0;
  error = sender->SetParameters(parameters);
  EXPECT_TRUE(error.ok());
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       ScaleResolutionDownByIsIgnoredWhenScaleToIsSpecified) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();

  scoped_refptr<MediaStreamInterface> stream = local_pc_wrapper->GetUserMedia(
      /*audio=*/false, {}, /*video=*/true, {.width = 640, .height = 360});
  scoped_refptr<VideoTrackInterface> track = stream->GetVideoTracks()[0];

  // Configure contradicting scaling factors (180p vs 360p).
  RtpTransceiverInit init;
  RtpEncodingParameters encoding;
  encoding.scale_resolution_down_by = 2.0;
  encoding.scale_resolution_down_to = {.width = 640, .height = 360};
  init.send_encodings.push_back(encoding);
  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(track, init);

  // Negotiate singlecast.
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);
  Negotiate(local_pc_wrapper, remote_pc_wrapper);

  // Confirm 640x360 is sent.
  // If `scale_resolution_down_by` was not ignored we would never ramp up to
  // full resolution.
  EXPECT_THAT(
      GetStatsUntil(local_pc_wrapper,
                    OutboundRtpStatsAre(ElementsAre(ResolutionIs(640, 360))),
                    {.timeout = kLongTimeoutForRampingUp}),
      IsRtcOk());
}

// Tests that use the standard path (specifying both `scalability_mode` and
// `scale_resolution_down_by` or `scale_resolution_down_to`) should pass for all
// codecs.
class PeerConnectionEncodingsIntegrationParameterizedTest
    : public PeerConnectionEncodingsIntegrationTest,
      public ::testing::WithParamInterface<std::string> {
 public:
  PeerConnectionEncodingsIntegrationParameterizedTest()
      : codec_name_(GetParam()), mime_type_("video/" + codec_name_) {}

  // Work-around for the fact that whether or not AV1 is supported is not known
  // at compile-time so we have to skip tests early if missing.
  // TODO(https://crbug.com/webrtc/15011): Increase availability of AV1 or make
  // it possible to check support at compile-time.
  bool SkipTestDueToAv1Missing(
      scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper) {
    if (codec_name_ == "AV1" &&
        !HasReceiverVideoCodecCapability(local_pc_wrapper, "AV1")) {
      RTC_LOG(LS_WARNING) << "\n***\nAV1 is not available, skipping test.\n***";
      return true;
    }
    return false;
  }

 protected:
  const std::string codec_name_;  // E.g. "VP9"
  const std::string mime_type_;   // E.g. "video/VP9"
};

TEST_P(PeerConnectionEncodingsIntegrationParameterizedTest, AllLayersInactive) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  if (SkipTestDueToAv1Missing(local_pc_wrapper)) {
    return;
  }
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"f", "h", "q"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, codec_name_);
  transceiver->SetCodecPreferences(codecs);

  // Standard mode and all layers inactive.
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  parameters.encodings[0].scalability_mode = "L1T3";
  parameters.encodings[0].scale_resolution_down_by = 1;
  parameters.encodings[0].active = false;
  parameters.encodings[1].active = false;
  parameters.encodings[2].active = false;
  sender->SetParameters(parameters);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Ensure no media is flowing (1 second should be enough).
  Thread::Current()->SleepMs(1000);
  scoped_refptr<const RTCStatsReport> report = GetStats(local_pc_wrapper);
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_THAT(outbound_rtps, SizeIs(3u));
  EXPECT_EQ(*outbound_rtps[0]->bytes_sent, 0u);
  EXPECT_EQ(*outbound_rtps[1]->bytes_sent, 0u);
  EXPECT_EQ(*outbound_rtps[2]->bytes_sent, 0u);
}

// Configure 4:2:1 using `scale_resolution_down_by`.
TEST_P(PeerConnectionEncodingsIntegrationParameterizedTest, Simulcast) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  if (SkipTestDueToAv1Missing(local_pc_wrapper)) {
    return;
  }
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"q", "h", "f"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, codec_name_);
  transceiver->SetCodecPreferences(codecs);

  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  parameters.encodings[0].scalability_mode = "L1T3";
  parameters.encodings[0].scale_resolution_down_by = 4;
  parameters.encodings[1].scalability_mode = "L1T3";
  parameters.encodings[1].scale_resolution_down_by = 2;
  parameters.encodings[2].scalability_mode = "L1T3";
  parameters.encodings[2].scale_resolution_down_by = 1;
  sender->SetParameters(parameters);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // GetParameters() does not report any fallback.
  parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));

  EXPECT_TRUE(parameters.encodings[0].scalability_mode.has_value());
  EXPECT_THAT(parameters.encodings[0].scalability_mode,
              Optional(std::string("L1T3")));
  EXPECT_TRUE(parameters.encodings[1].scalability_mode.has_value());
  EXPECT_THAT(parameters.encodings[1].scalability_mode,
              Optional(std::string("L1T3")));
  EXPECT_TRUE(parameters.encodings[2].scalability_mode.has_value());
  EXPECT_THAT(parameters.encodings[2].scalability_mode,
              Optional(std::string("L1T3")));

  // Wait until media is flowing on all three layers.
  // Ramp up time is needed before all three layers are sending.
  auto error_or_report =
      GetStatsUntil(local_pc_wrapper, HasOutboundRtpBytesSent(3u),
                    {.timeout = kLongTimeoutForRampingUp});
  ASSERT_THAT(error_or_report, IsRtcOk());
  // Verify codec and scalability mode.
  scoped_refptr<const RTCStatsReport> report = error_or_report.value();
  auto outbound_rtp_by_rid = GetOutboundRtpStreamStatsByRid(report);
  EXPECT_THAT(outbound_rtp_by_rid,
              UnorderedElementsAre(Pair("q", ResolutionIs(320, 180)),
                                   Pair("h", ResolutionIs(640, 360)),
                                   Pair("f", ResolutionIs(1280, 720))));
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_THAT(outbound_rtps, SizeIs(3u));
  EXPECT_THAT(GetCurrentCodecMimeType(report, *outbound_rtps[0]),
              StrCaseEq(mime_type_));
  EXPECT_THAT(GetCurrentCodecMimeType(report, *outbound_rtps[1]),
              StrCaseEq(mime_type_));
  EXPECT_THAT(GetCurrentCodecMimeType(report, *outbound_rtps[2]),
              StrCaseEq(mime_type_));
  EXPECT_THAT(report, OutboundRtpStatsAre(
                          Each(ScalabilityModeIs(Optional(StrEq("L1T3"))))));
}

// Configure 4:2:1 using `scale_resolution_down_to`.
TEST_P(PeerConnectionEncodingsIntegrationParameterizedTest,
       SimulcastWithScaleTo) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  if (SkipTestDueToAv1Missing(local_pc_wrapper)) {
    return;
  }
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"q", "h", "f"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, codec_name_);
  transceiver->SetCodecPreferences(codecs);

  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  parameters.encodings[0].scalability_mode = "L1T3";
  parameters.encodings[0].scale_resolution_down_to = {.width = 320,
                                                      .height = 180};
  parameters.encodings[1].scalability_mode = "L1T3";
  parameters.encodings[1].scale_resolution_down_to = {.width = 640,
                                                      .height = 360};
  parameters.encodings[2].scalability_mode = "L1T3";
  parameters.encodings[2].scale_resolution_down_to = {.width = 1280,
                                                      .height = 720};
  sender->SetParameters(parameters);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // GetParameters() does not report any fallback.
  parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  EXPECT_THAT(parameters.encodings[0].scalability_mode,
              Optional(std::string("L1T3")));
  EXPECT_THAT(parameters.encodings[1].scalability_mode,
              Optional(std::string("L1T3")));
  EXPECT_THAT(parameters.encodings[2].scalability_mode,
              Optional(std::string("L1T3")));

  // Wait until media is flowing on all three layers AND scalability mode is
  // reported. Ramp up time is needed before all three layers are sending.
  auto error_or_report = GetStatsUntil(
      local_pc_wrapper,
      AllOf(HasOutboundRtpBytesSent(3u),
            OutboundRtpStatsAre(
                Each(ScalabilityModeIs(Optional(StrEq("L1T3")))))),
      {.timeout = kLongTimeoutForRampingUp});
  ASSERT_THAT(error_or_report, IsRtcOk());
  // Verify codec and scalability mode.
  scoped_refptr<const RTCStatsReport> report = error_or_report.value();
  auto outbound_rtp_by_rid = GetOutboundRtpStreamStatsByRid(report);
  EXPECT_THAT(
      outbound_rtp_by_rid,
      UnorderedElementsAre(
          Pair("q", AllOf(ResolutionIs(320, 180),
                          ScalabilityModeIs(Optional(StrEq("L1T3"))))),
          Pair("h", AllOf(ResolutionIs(640, 360),
                          ScalabilityModeIs(Optional(StrEq("L1T3"))))),
          Pair("f", AllOf(ResolutionIs(1280, 720),
                          ScalabilityModeIs(Optional(StrEq("L1T3")))))));
  // Verify codec.
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_THAT(outbound_rtps, SizeIs(3u));
  EXPECT_THAT(GetCurrentCodecMimeType(report, *outbound_rtps[0]),
              StrCaseEq(mime_type_));
  EXPECT_THAT(GetCurrentCodecMimeType(report, *outbound_rtps[1]),
              StrCaseEq(mime_type_));
  EXPECT_THAT(GetCurrentCodecMimeType(report, *outbound_rtps[2]),
              StrCaseEq(mime_type_));
}

// Simulcast starting in 720p 4:2:1 then changing to {180p, 360p, 540p} using
// the `scale_resolution_down_by` API.
TEST_P(PeerConnectionEncodingsIntegrationParameterizedTest,
       SimulcastScaleDownByNoLongerPowerOfTwo) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  if (SkipTestDueToAv1Missing(local_pc_wrapper)) {
    return;
  }
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"q", "h", "f"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, codec_name_);
  transceiver->SetCodecPreferences(codecs);
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();

  // Configure {180p, 360p, 720p}.
  RtpParameters parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  parameters.encodings[0].scalability_mode = "L1T1";
  parameters.encodings[0].scale_resolution_down_by = 4.0;
  parameters.encodings[1].scalability_mode = "L1T1";
  parameters.encodings[1].scale_resolution_down_by = 2.0;
  parameters.encodings[2].scalability_mode = "L1T1";
  parameters.encodings[2].scale_resolution_down_by = 1.0;
  sender->SetParameters(parameters);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Wait for media to flow on all layers.
  // Needed repro step of https://crbug.com/webrtc/369654168: When the same
  // LibvpxVp9Encoder instance was used to first produce simulcast and later for
  // a single encoding, the previously used simulcast index (= 2) would still be
  // set when producing 180p since non-simulcast config does not reset this,
  // resulting in the 180p encoding freezing and the 540p encoding having double
  // frame rate and toggling between 180p and 540p in resolution.
  ASSERT_THAT(GetStatsUntil(local_pc_wrapper, HasOutboundRtpBytesSent(3u),
                            {.timeout = kLongTimeoutForRampingUp}),
              IsRtcOk());

  // Configure {180p, 360p, 540p}.
  parameters = sender->GetParameters();
  parameters.encodings[0].scale_resolution_down_by = 4.0;
  parameters.encodings[1].scale_resolution_down_by = 2.0;
  parameters.encodings[2].scale_resolution_down_by = 1.333333;
  sender->SetParameters(parameters);

  // Wait for the new resolutions to be produced.
  auto encoding_resolutions_result =
      WaitUntil([&] { return GetStats(local_pc_wrapper); },
                OutboundRtpStatsAre(UnorderedElementsAre(
                    AllOf(RidIs("q"), ResolutionIs(320, 180)),
                    AllOf(RidIs("h"), ResolutionIs(640, 360)),
                    AllOf(RidIs("f"), ResolutionIs(960, 540)))),
                {.timeout = kLongTimeoutForRampingUp});
  ASSERT_THAT(encoding_resolutions_result, IsRtcOk());

  auto outbound_rtp_by_rid =
      GetOutboundRtpStreamStatsByRid(encoding_resolutions_result.value());
  ASSERT_THAT(outbound_rtp_by_rid,
              UnorderedElementsAre(Key("q"), Key("h"), Key("f")));

  // Ensure frames continue to be encoded post reconfiguration.
  uint64_t frames_encoded_q =
      outbound_rtp_by_rid.at("q").frames_encoded.value();
  uint64_t frames_encoded_h =
      outbound_rtp_by_rid.at("h").frames_encoded.value();
  uint64_t frames_encoded_f =
      outbound_rtp_by_rid.at("f").frames_encoded.value();
  EXPECT_THAT(
      GetStatsUntil(
          local_pc_wrapper,
          OutboundRtpStatsAre(UnorderedElementsAre(
              AllOf(RidIs("q"), FramesEncodedIs(Gt(frames_encoded_q))),
              AllOf(RidIs("h"), FramesEncodedIs(Gt(frames_encoded_h))),
              AllOf(RidIs("f"), FramesEncodedIs(Gt(frames_encoded_f))))),
          {.timeout = kLongTimeoutForRampingUp}),
      IsRtcOk());
}

// Simulcast starting in 720p 4:2:1 then changing to {180p, 360p, 540p} using
// the `scale_resolution_down_to` API.
TEST_P(PeerConnectionEncodingsIntegrationParameterizedTest,
       SimulcastScaleToNoLongerPowerOfTwo) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  if (SkipTestDueToAv1Missing(local_pc_wrapper)) {
    return;
  }
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"q", "h", "f"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, codec_name_);
  transceiver->SetCodecPreferences(codecs);
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();

  // Configure {180p, 360p, 720p}.
  RtpParameters parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  parameters.encodings[0].scalability_mode = "L1T1";
  parameters.encodings[0].scale_resolution_down_to = {.width = 320,
                                                      .height = 180};
  parameters.encodings[1].scalability_mode = "L1T1";
  parameters.encodings[1].scale_resolution_down_to = {.width = 640,
                                                      .height = 360};
  parameters.encodings[2].scalability_mode = "L1T1";
  parameters.encodings[2].scale_resolution_down_to = {.width = 1280,
                                                      .height = 720};
  sender->SetParameters(parameters);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Wait for media to flow on all layers.
  // Needed repro step of https://crbug.com/webrtc/369654168: When the same
  // LibvpxVp9Encoder instance was used to first produce simulcast and later for
  // a single encoding, the previously used simulcast index (= 2) would still be
  // set when producing 180p since non-simulcast config does not reset this,
  // resulting in the 180p encoding freezing and the 540p encoding having double
  // frame rate and toggling between 180p and 540p in resolution.
  ASSERT_THAT(GetStatsUntil(local_pc_wrapper, HasOutboundRtpBytesSent(3u),
                            {.timeout = kLongTimeoutForRampingUp}),
              IsRtcOk());

  // Configure {180p, 360p, 540p}.
  parameters = sender->GetParameters();
  parameters.encodings[0].scale_resolution_down_to = {.width = 320,
                                                      .height = 180};
  parameters.encodings[1].scale_resolution_down_to = {.width = 640,
                                                      .height = 360};
  parameters.encodings[2].scale_resolution_down_to = {.width = 960,
                                                      .height = 540};
  sender->SetParameters(parameters);

  // Wait for the new resolutions to be produced.
  auto error_or_stats =
      GetStatsUntil(local_pc_wrapper,
                    OutboundRtpStatsAre(UnorderedElementsAre(
                        AllOf(RidIs("q"), ResolutionIs(320, 180)),
                        AllOf(RidIs("h"), ResolutionIs(640, 360)),
                        AllOf(RidIs("f"), ResolutionIs(960, 540)))),
                    {.timeout = kLongTimeoutForRampingUp});
  ASSERT_THAT(error_or_stats, IsRtcOk());

  auto outbound_rtp_by_rid =
      GetOutboundRtpStreamStatsByRid(error_or_stats.value());
  ASSERT_THAT(outbound_rtp_by_rid,
              UnorderedElementsAre(Pair("q", BytesSentIs(Ne(std::nullopt))),
                                   Pair("h", BytesSentIs(Ne(std::nullopt))),
                                   Pair("f", BytesSentIs(Ne(std::nullopt)))));

  // Ensure frames continue to be encoded post reconfiguration.
  EXPECT_THAT(
      GetStatsUntil(
          local_pc_wrapper,
          OutboundRtpStatsAre(UnorderedElementsAre(
              AllOf(RidIs("q"),
                    BytesSentIs(
                        Gt(outbound_rtp_by_rid.at("q").bytes_sent.value()))),
              AllOf(RidIs("h"),
                    BytesSentIs(
                        Gt(outbound_rtp_by_rid.at("h").bytes_sent.value()))),
              AllOf(RidIs("f"),
                    BytesSentIs(
                        Gt(outbound_rtp_by_rid.at("f").bytes_sent.value()))))),
          {.timeout = kLongTimeoutForRampingUp}),
      IsRtcOk());
}

// The code path that disables layers based on resolution size should NOT run
// when `scale_resolution_down_to` is specified. (It shouldn't run in any case
// but that is an existing legacy code and non-compliance problem that we don't
// have to repeat here.)
TEST_P(PeerConnectionEncodingsIntegrationParameterizedTest,
       LowResolutionSimulcastWithScaleTo) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  if (SkipTestDueToAv1Missing(local_pc_wrapper)) {
    return;
  }
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"q", "h", "f"}, /*active=*/true);

  // Configure {20p,40p,80p} with 2:1 aspect ratio.
  RtpTransceiverInit init;
  RtpEncodingParameters encoding;
  encoding.scalability_mode = "L1T3";
  encoding.rid = "q";
  encoding.scale_resolution_down_to = {.width = 40, .height = 20};
  init.send_encodings.push_back(encoding);
  encoding.rid = "h";
  encoding.scale_resolution_down_to = {.width = 80, .height = 40};
  init.send_encodings.push_back(encoding);
  encoding.rid = "f";
  encoding.scale_resolution_down_to = {.width = 160, .height = 80};
  init.send_encodings.push_back(encoding);
  scoped_refptr<MediaStreamInterface> stream = local_pc_wrapper->GetUserMedia(
      /*audio=*/false, {}, /*video=*/true, {.width = 160, .height = 80});
  scoped_refptr<VideoTrackInterface> track = stream->GetVideoTracks()[0];
  auto transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(track, init);
  ASSERT_TRUE(transceiver_or_error.ok());
  scoped_refptr<RtpTransceiverInterface> transceiver =
      transceiver_or_error.value();

  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, codec_name_);
  transceiver->SetCodecPreferences(codecs);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Wait for media to flow on all layers.
  ASSERT_THAT(GetStatsUntil(local_pc_wrapper, HasOutboundRtpBytesSent(3u)),
              IsRtcOk());
  // q=20p, h=40p, f=80p.
  EXPECT_THAT(GetStatsUntil(local_pc_wrapper,
                            OutboundRtpStatsAre(UnorderedElementsAre(
                                AllOf(RidIs("q"), ResolutionIs(40, 20)),
                                AllOf(RidIs("h"), ResolutionIs(80, 40)),
                                AllOf(RidIs("f"), ResolutionIs(160, 80)))),
                            {.timeout = kLongTimeoutForRampingUp}),
              IsRtcOk());
}

TEST_P(PeerConnectionEncodingsIntegrationParameterizedTest,
       SimulcastEncodingStopWhenRtpEncodingChangeToInactive) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  if (SkipTestDueToAv1Missing(local_pc_wrapper)) {
    return;
  }
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"q", "h", "f"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(local_pc_wrapper, codec_name_);
  transceiver->SetCodecPreferences(codecs);

  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  ASSERT_EQ(parameters.encodings[0].rid, "q");
  parameters.encodings[0].scalability_mode = "L1T3";
  parameters.encodings[0].scale_resolution_down_by = 4;
  ASSERT_EQ(parameters.encodings[1].rid, "h");
  parameters.encodings[1].scalability_mode = "L1T3";
  parameters.encodings[1].scale_resolution_down_by = 2;
  ASSERT_EQ(parameters.encodings[2].rid, "f");
  parameters.encodings[2].scalability_mode = "L1T3";
  parameters.encodings[2].scale_resolution_down_by = 1;
  sender->SetParameters(parameters);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  ASSERT_THAT(GetStatsUntil(local_pc_wrapper,
                            OutboundRtpStatsAre(Contains(
                                AllOf(RidIs("f"), FramesEncodedIs(Gt(0))))),
                            {.timeout = kLongTimeoutForRampingUp}),
              IsRtcOk());

  // Switch higest layer to Inactive.
  parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(3));
  parameters.encodings[2].active = false;
  sender->SetParameters(parameters);
  auto error_or_stats = GetStatsUntil(
      local_pc_wrapper,
      OutboundRtpStatsAre(Contains(AllOf(RidIs("f"), Not(Active())))),
      {.timeout = kLongTimeoutForRampingUp});
  ASSERT_THAT(error_or_stats, IsRtcOk());

  auto outbound_rtp_by_rid =
      GetOutboundRtpStreamStatsByRid(error_or_stats.value());
  int encoded_frames_f = outbound_rtp_by_rid.at("f").frames_encoded.value();
  int encoded_frames_h = outbound_rtp_by_rid.at("h").frames_encoded.value();
  int encoded_frames_q = outbound_rtp_by_rid.at("q").frames_encoded.value();

  // Wait until the encoder has encoded another 10 frames on lower layers.
  ASSERT_THAT(
      GetStatsUntil(
          local_pc_wrapper,
          OutboundRtpStatsAre(UnorderedElementsAre(
              AllOf(RidIs("q"), FramesEncodedIs(Gt(encoded_frames_q + 10))),
              AllOf(RidIs("h"), FramesEncodedIs(Gt(encoded_frames_h + 10))),
              AllOf(RidIs("f"), FramesEncodedIs(Le(encoded_frames_f + 2))))),
          {.timeout = kLongTimeoutForRampingUp}),
      IsRtcOk());
}

TEST_P(PeerConnectionEncodingsIntegrationParameterizedTest,
       ScaleToDownscaleAndThenUpscale) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  if (SkipTestDueToAv1Missing(local_pc_wrapper)) {
    return;
  }
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers = CreateLayers({"f"}, /*active=*/true);

  // This transceiver receives a 1280x720 source.
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, codec_name_);
  transceiver->SetCodecPreferences(codecs);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Request 640x360, which is the same as scaling down by 2.
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(1));
  parameters.encodings[0].scalability_mode = "L1T3";
  parameters.encodings[0].scale_resolution_down_to = {.width = 640,
                                                      .height = 360};
  sender->SetParameters(parameters);
  // Confirm 640x360 is sent.
  ASSERT_THAT(
      GetStatsUntil(local_pc_wrapper,
                    OutboundRtpStatsAre(ElementsAre(ResolutionIs(640, 360))),
                    {.timeout = kLongTimeoutForRampingUp}),
      IsRtcOk());

  // Test coverage for https://crbug.com/webrtc/361477261:
  // Due initial frame dropping, OnFrameDroppedDueToSize() should have created
  // some resolution restrictions by now. With 720p input frame, restriction is
  // 540p which is not observable when sending 360p, but it prevents us from
  // immediately sending 720p. Restrictions will be lifted after a few seconds
  // (when good QP is reported by QualityScaler) and 720p should be sent. The
  // bug was not reconfiguring the encoder when restrictions were updated so the
  // restrictions at the time of the SetParameter() call were made indefinite.

  // Request the full 1280x720 resolution.
  parameters = sender->GetParameters();
  parameters.encodings[0].scale_resolution_down_to = {.width = 1280,
                                                      .height = 720};
  sender->SetParameters(parameters);
  // Confirm 1280x720 is sent.
  EXPECT_THAT(
      GetStatsUntil(local_pc_wrapper,
                    OutboundRtpStatsAre(ElementsAre(ResolutionIs(1280, 720))),
                    {.timeout = kLongTimeoutForRampingUp}),
      IsRtcOk());
}

TEST_P(PeerConnectionEncodingsIntegrationParameterizedTest,
       ScaleToIsOrientationAgnostic) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  if (SkipTestDueToAv1Missing(local_pc_wrapper)) {
    return;
  }
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers = CreateLayers({"f"}, /*active=*/true);

  // This transceiver receives a 1280x720 source.
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, codec_name_);
  transceiver->SetCodecPreferences(codecs);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // 360x640 is the same as 640x360 due to orientation agnosticism.
  // The orientation is determined by the frame (1280x720): landscape.
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(1));
  parameters.encodings[0].scale_resolution_down_to = {.width = 360,
                                                      .height = 640};
  sender->SetParameters(parameters);
  // Confirm 640x360 is sent.
  EXPECT_THAT(
      GetStatsUntil(local_pc_wrapper,
                    OutboundRtpStatsAre(ElementsAre(ResolutionIs(640, 360)))),
      IsRtcOk());
}

TEST_P(PeerConnectionEncodingsIntegrationParameterizedTest,
       ScaleToMaintainsAspectRatio) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  if (SkipTestDueToAv1Missing(local_pc_wrapper)) {
    return;
  }
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers = CreateLayers({"f"}, /*active=*/true);

  // This transceiver receives a 1280x720 source.
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> codecs =
      GetCapabilitiesAndRestrictToCodec(remote_pc_wrapper, codec_name_);
  transceiver->SetCodecPreferences(codecs);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Restrict height more than width, the scaling factor needed on height should
  // also be applied on the width in order to maintain the frame aspect ratio.
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(1));
  parameters.encodings[0].scale_resolution_down_to = {.width = 1280,
                                                      .height = 360};
  sender->SetParameters(parameters);
  // Confirm 640x360 is sent.
  EXPECT_THAT(
      GetStatsUntil(local_pc_wrapper,
                    OutboundRtpStatsAre(ElementsAre(ResolutionIs(640, 360)))),
      IsRtcOk());
}

INSTANTIATE_TEST_SUITE_P(StandardPath,
                         PeerConnectionEncodingsIntegrationParameterizedTest,
                         ::testing::Values("VP8",
                                           "VP9",
#if defined(WEBRTC_USE_H264)
                                           "H264",
#endif  // defined(WEBRTC_USE_H264)
                                           "AV1"),
                         StringParamToString());

// These tests use fake encoders and decoders, allowing testing of codec
// preferences, SDP negotiation and get/setParamaters(). But because the codecs
// implementations are fake, these tests do not encode or decode any frames.
class PeerConnectionEncodingsFakeCodecsIntegrationTest
    : public PeerConnectionEncodingsIntegrationTest {
 public:
#ifdef RTC_ENABLE_H265
  scoped_refptr<PeerConnectionTestWrapper> CreatePcWithFakeH265(
      std::unique_ptr<FieldTrialsView> field_trials = nullptr) {
    std::unique_ptr<FakeWebRtcVideoEncoderFactory> video_encoder_factory =
        std::make_unique<FakeWebRtcVideoEncoderFactory>();
    video_encoder_factory->AddSupportedVideoCodec(
        SdpVideoFormat("H265",
                       {{"profile-id", "1"},
                        {"tier-flag", "0"},
                        {"level-id", "156"},
                        {"tx-mode", "SRST"}},
                       {ScalabilityMode::kL1T1}));
    std::unique_ptr<FakeWebRtcVideoDecoderFactory> video_decoder_factory =
        std::make_unique<FakeWebRtcVideoDecoderFactory>();
    video_decoder_factory->AddSupportedVideoCodecType("H265");
    auto pc_wrapper = make_ref_counted<PeerConnectionTestWrapper>(
        "pc", env_, &pss_, background_thread_.get());
    pc_wrapper->CreatePc(
        {}, CreateBuiltinAudioEncoderFactory(),
        CreateBuiltinAudioDecoderFactory(), std::move(video_encoder_factory),
        std::move(video_decoder_factory), std::move(field_trials));
    return pc_wrapper;
  }
#endif  // RTC_ENABLE_H265

  // Creates a PC where we have H264 with one sendonly, one recvonly and one
  // sendrecv "profile-level-id". The sendrecv one is constrained baseline.
  scoped_refptr<PeerConnectionTestWrapper> CreatePcWithUnidirectionalH264(
      std::unique_ptr<FieldTrialsView> field_trials = nullptr) {
    std::unique_ptr<FakeWebRtcVideoEncoderFactory> video_encoder_factory =
        std::make_unique<FakeWebRtcVideoEncoderFactory>();
    SdpVideoFormat h264_constrained_baseline =
        SdpVideoFormat("H264",
                       {{"level-asymmetry-allowed", "1"},
                        {"packetization-mode", "1"},
                        {"profile-level-id", "42f00b"}},  // sendrecv
                       {ScalabilityMode::kL1T1});
    video_encoder_factory->AddSupportedVideoCodec(h264_constrained_baseline);
    video_encoder_factory->AddSupportedVideoCodec(
        SdpVideoFormat("H264",
                       {{"level-asymmetry-allowed", "1"},
                        {"packetization-mode", "1"},
                        {"profile-level-id", "640034"}},  // sendonly
                       {ScalabilityMode::kL1T1}));
    std::unique_ptr<FakeWebRtcVideoDecoderFactory> video_decoder_factory =
        std::make_unique<FakeWebRtcVideoDecoderFactory>();
    video_decoder_factory->AddSupportedVideoCodec(h264_constrained_baseline);
    video_decoder_factory->AddSupportedVideoCodec(
        SdpVideoFormat("H264",
                       {{"level-asymmetry-allowed", "1"},
                        {"packetization-mode", "1"},
                        {"profile-level-id", "f4001f"}},  // recvonly
                       {ScalabilityMode::kL1T1}));
    auto pc_wrapper = make_ref_counted<PeerConnectionTestWrapper>(
        "pc", env_, &pss_, background_thread_.get());
    pc_wrapper->CreatePc(
        {}, CreateBuiltinAudioEncoderFactory(),
        CreateBuiltinAudioDecoderFactory(), std::move(video_encoder_factory),
        std::move(video_decoder_factory), std::move(field_trials));
    return pc_wrapper;
  }

  std::string LocalDescriptionStr(PeerConnectionTestWrapper* pc_wrapper) {
    const SessionDescriptionInterface* local_description =
        pc_wrapper->pc()->local_description();
    if (!local_description) {
      return "";
    }
    std::string str;
    if (!local_description->ToString(&str)) {
      return "";
    }
    return str;
  }
};

#ifdef RTC_ENABLE_H265
TEST_F(PeerConnectionEncodingsFakeCodecsIntegrationTest, H265Singlecast) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper =
      CreatePcWithFakeH265();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper =
      CreatePcWithFakeH265();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  scoped_refptr<RtpTransceiverInterface> transceiver =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::VIDEO).MoveValue();
  std::vector<RtpCodecCapability> preferred_codecs =
      GetCapabilitiesAndRestrictToCodec(local_pc_wrapper, "H265");
  transceiver->SetCodecPreferences(preferred_codecs);

  Negotiate(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Verify codec.
  scoped_refptr<const RTCStatsReport> report = GetStats(local_pc_wrapper);
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_THAT(outbound_rtps, SizeIs(1u));
  EXPECT_THAT(GetCurrentCodecMimeType(report, *outbound_rtps[0]),
              StrCaseEq("video/H265"));
}

TEST_F(PeerConnectionEncodingsFakeCodecsIntegrationTest, H265Simulcast) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper =
      CreatePcWithFakeH265();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper =
      CreatePcWithFakeH265();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"q", "h", "f"}, /*active=*/true);

  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> preferred_codecs =
      GetCapabilitiesAndRestrictToCodec(local_pc_wrapper, "H265");
  transceiver->SetCodecPreferences(preferred_codecs);

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Wait until all outbound RTPs exist.
  EXPECT_THAT(
      GetStatsUntil(local_pc_wrapper, OutboundRtpStatsAre(UnorderedElementsAre(
                                          AllOf(RidIs("q")), AllOf(RidIs("h")),
                                          AllOf(RidIs("f"))))),
      IsRtcOk());

  // Verify codec.
  scoped_refptr<const RTCStatsReport> report = GetStats(local_pc_wrapper);
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_THAT(outbound_rtps, SizeIs(3u));
  EXPECT_THAT(GetCurrentCodecMimeType(report, *outbound_rtps[0]),
              StrCaseEq("video/H265"));
  EXPECT_THAT(GetCurrentCodecMimeType(report, *outbound_rtps[1]),
              StrCaseEq("video/H265"));
  EXPECT_THAT(GetCurrentCodecMimeType(report, *outbound_rtps[2]),
              StrCaseEq("video/H265"));
}

TEST_F(PeerConnectionEncodingsFakeCodecsIntegrationTest,
       H265SetParametersIgnoresLevelId) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper =
      CreatePcWithFakeH265();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper =
      CreatePcWithFakeH265();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers = CreateLayers({"f"}, /*active=*/true);

  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  std::vector<RtpCodecCapability> preferred_codecs =
      GetCapabilitiesAndRestrictToCodec(local_pc_wrapper, "H265");
  transceiver->SetCodecPreferences(preferred_codecs);
  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();

  NegotiateWithSimulcastTweaks(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // This includes non-codecs like rtx, red and flexfec too so we need to find
  // H265.
  std::vector<RtpCodecCapability> sender_codecs =
      local_pc_wrapper->pc_factory()
          ->GetRtpSenderCapabilities(MediaType::VIDEO)
          .codecs;
  auto it = std::find_if(sender_codecs.begin(), sender_codecs.end(),
                         [](const RtpCodecCapability codec_capability) {
                           return codec_capability.name == "H265";
                         });
  ASSERT_NE(it, sender_codecs.end());
  RtpCodecCapability& h265_codec = *it;

  // SetParameters() without changing level-id.
  EXPECT_EQ(h265_codec.parameters["level-id"], "156");
  {
    RtpParameters parameters = sender->GetParameters();
    ASSERT_THAT(parameters.encodings, SizeIs(1));
    parameters.encodings[0].codec = h265_codec;
    ASSERT_THAT(sender->SetParameters(parameters), IsRtcOk());
  }
  // SetParameters() with a lower level-id.
  h265_codec.parameters["level-id"] = "30";
  {
    RtpParameters parameters = sender->GetParameters();
    ASSERT_THAT(parameters.encodings, SizeIs(1));
    parameters.encodings[0].codec = h265_codec;
    ASSERT_THAT(sender->SetParameters(parameters), IsRtcOk());
  }
  // SetParameters() with a higher level-id.
  h265_codec.parameters["level-id"] = "180";
  {
    RtpParameters parameters = sender->GetParameters();
    ASSERT_THAT(parameters.encodings, SizeIs(1));
    parameters.encodings[0].codec = h265_codec;
    ASSERT_THAT(sender->SetParameters(parameters), IsRtcOk());
  }
}
#endif  // RTC_ENABLE_H265

TEST_F(PeerConnectionEncodingsFakeCodecsIntegrationTest,
       H264UnidirectionalNegotiation) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper =
      CreatePcWithUnidirectionalH264();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper =
      CreatePcWithUnidirectionalH264();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  scoped_refptr<RtpTransceiverInterface> transceiver =
      local_pc_wrapper->pc()->AddTransceiver(MediaType::VIDEO).MoveValue();

  // Filter on codec name and assert that sender capabilities have codecs for
  // {sendrecv, sendonly} and the receiver capabilities have codecs for
  // {sendrecv, recvonly}.
  std::vector<RtpCodecCapability> send_codecs =
      local_pc_wrapper->pc_factory()
          ->GetRtpSenderCapabilities(MediaType::VIDEO)
          .codecs;
  std::erase_if(send_codecs, [](const RtpCodecCapability& codec) {
    return codec.name != "H264";
  });
  std::vector<RtpCodecCapability> recv_codecs =
      local_pc_wrapper->pc_factory()
          ->GetRtpReceiverCapabilities(MediaType::VIDEO)
          .codecs;
  std::erase_if(recv_codecs, [](const RtpCodecCapability& codec) {
    RTC_LOG(LS_ERROR) << codec.name;
    return codec.name != "H264";
  });
  ASSERT_THAT(send_codecs, SizeIs(2u));
  ASSERT_THAT(recv_codecs, SizeIs(2u));
  EXPECT_EQ(send_codecs[0], recv_codecs[0]);
  EXPECT_NE(send_codecs[1], recv_codecs[1]);
  RtpCodecCapability& sendrecv_codec = send_codecs[0];
  RtpCodecCapability& sendonly_codec = send_codecs[1];
  RtpCodecCapability& recvonly_codec = recv_codecs[1];

  // Preferring sendonly + recvonly on a sendrecv transceiver is the same as
  // not having any preferences, meaning the sendrecv codec (not listed) is the
  // one being negotiated.
  std::vector<RtpCodecCapability> preferred_codecs = {sendonly_codec,
                                                      recvonly_codec};
  EXPECT_THAT(transceiver->SetCodecPreferences(preferred_codecs), IsRtcOk());
  EXPECT_THAT(
      transceiver->SetDirectionWithError(RtpTransceiverDirection::kSendRecv),
      IsRtcOk());
  Negotiate(local_pc_wrapper, remote_pc_wrapper);
  std::string local_sdp = LocalDescriptionStr(local_pc_wrapper.get());
  EXPECT_THAT(local_sdp,
              HasSubstr(sendrecv_codec.parameters["profile-level-id"]));
  EXPECT_THAT(local_sdp,
              Not(HasSubstr(sendonly_codec.parameters["profile-level-id"])));
  EXPECT_THAT(local_sdp,
              Not(HasSubstr(recvonly_codec.parameters["profile-level-id"])));

  // Prefer all codecs and expect that the SDP offer contains the relevant
  // codecs after filtering. Complete O/A each time.
  preferred_codecs = {sendrecv_codec, sendonly_codec, recvonly_codec};
  EXPECT_THAT(transceiver->SetCodecPreferences(preferred_codecs), IsRtcOk());
  // Transceiver direction: sendrecv.
  EXPECT_THAT(
      transceiver->SetDirectionWithError(RtpTransceiverDirection::kSendRecv),
      IsRtcOk());
  Negotiate(local_pc_wrapper, remote_pc_wrapper);
  local_sdp = LocalDescriptionStr(local_pc_wrapper.get());
  EXPECT_THAT(local_sdp,
              HasSubstr(sendrecv_codec.parameters["profile-level-id"]));
  EXPECT_THAT(local_sdp,
              Not(HasSubstr(sendonly_codec.parameters["profile-level-id"])));
  EXPECT_THAT(local_sdp,
              Not(HasSubstr(recvonly_codec.parameters["profile-level-id"])));
  // Transceiver direction: sendonly.
  EXPECT_THAT(
      transceiver->SetDirectionWithError(RtpTransceiverDirection::kSendOnly),
      IsRtcOk());
  Negotiate(local_pc_wrapper, remote_pc_wrapper);
  local_sdp = LocalDescriptionStr(local_pc_wrapper.get());
  EXPECT_THAT(local_sdp,
              HasSubstr(sendrecv_codec.parameters["profile-level-id"]));
  EXPECT_THAT(local_sdp,
              HasSubstr(sendonly_codec.parameters["profile-level-id"]));
  EXPECT_THAT(local_sdp,
              Not(HasSubstr(recvonly_codec.parameters["profile-level-id"])));
  // Transceiver direction: recvonly.
  EXPECT_THAT(
      transceiver->SetDirectionWithError(RtpTransceiverDirection::kRecvOnly),
      IsRtcOk());
  Negotiate(local_pc_wrapper, remote_pc_wrapper);
  local_sdp = LocalDescriptionStr(local_pc_wrapper.get());
  EXPECT_THAT(local_sdp,
              HasSubstr(sendrecv_codec.parameters["profile-level-id"]));
  EXPECT_THAT(local_sdp,
              Not(HasSubstr(sendonly_codec.parameters["profile-level-id"])));
  EXPECT_THAT(local_sdp,
              HasSubstr(recvonly_codec.parameters["profile-level-id"]));

  // Test that offering a sendonly codec on a sendonly transceiver is possible.
  // - Note that we don't complete the negotiation this time because we're not
  //   capable of receiving the codec.
  preferred_codecs = {sendonly_codec};
  EXPECT_THAT(transceiver->SetCodecPreferences(preferred_codecs), IsRtcOk());
  EXPECT_THAT(
      transceiver->SetDirectionWithError(RtpTransceiverDirection::kSendOnly),
      IsRtcOk());
  std::unique_ptr<SessionDescriptionInterface> offer =
      CreateOffer(local_pc_wrapper);
  EXPECT_TRUE(Await({SetLocalDescription(local_pc_wrapper, offer.get())}));
  local_sdp = LocalDescriptionStr(local_pc_wrapper.get());
  EXPECT_THAT(local_sdp,
              Not(HasSubstr(sendrecv_codec.parameters["profile-level-id"])));
  EXPECT_THAT(local_sdp,
              HasSubstr(sendonly_codec.parameters["profile-level-id"]));
  EXPECT_THAT(local_sdp,
              Not(HasSubstr(recvonly_codec.parameters["profile-level-id"])));
  // Test that offering recvonly codec on a recvonly transceiver is possible.
  // - Note that we don't complete the negotiation this time because we're not
  //   capable of sending the codec.
  preferred_codecs = {recvonly_codec};
  EXPECT_THAT(transceiver->SetCodecPreferences(preferred_codecs), IsRtcOk());
  EXPECT_THAT(
      transceiver->SetDirectionWithError(RtpTransceiverDirection::kRecvOnly),
      IsRtcOk());
  offer = CreateOffer(local_pc_wrapper);
  EXPECT_TRUE(Await({SetLocalDescription(local_pc_wrapper, offer.get())}));
  local_sdp = LocalDescriptionStr(local_pc_wrapper.get());
  EXPECT_THAT(local_sdp,
              Not(HasSubstr(sendrecv_codec.parameters["profile-level-id"])));
  EXPECT_THAT(local_sdp,
              Not(HasSubstr(sendonly_codec.parameters["profile-level-id"])));
  EXPECT_THAT(local_sdp,
              HasSubstr(recvonly_codec.parameters["profile-level-id"]));
}

// Regression test for https://issues.chromium.org/issues/399667359
TEST_F(PeerConnectionEncodingsIntegrationTest,
       SimulcastNotSupportedGetParametersDoesNotCrash) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  std::vector<SimulcastLayer> layers =
      CreateLayers({"f", "q"}, /*active=*/true);
  scoped_refptr<RtpTransceiverInterface> transceiver =
      AddTransceiverWithSimulcastLayers(local_pc_wrapper, remote_pc_wrapper,
                                        layers);
  // Negotiate - receiver will reject simulcast, so the 2nd layer will be
  // disabled
  Negotiate(local_pc_wrapper, remote_pc_wrapper);
  // Negotiate again without simulcast.
  Negotiate(local_pc_wrapper, remote_pc_wrapper);

  RtpParameters parameters = transceiver->sender()->GetParameters();
  EXPECT_TRUE(transceiver->sender()->SetParameters(parameters).ok());
}

TEST_F(PeerConnectionEncodingsIntegrationTest,
       QualityLimitationReasonReportsBandwidthInMaintainResolution) {
  scoped_refptr<PeerConnectionTestWrapper> local_pc_wrapper = CreatePc();
  scoped_refptr<PeerConnectionTestWrapper> remote_pc_wrapper = CreatePc();
  ExchangeIceCandidates(local_pc_wrapper, remote_pc_wrapper);

  scoped_refptr<MediaStreamInterface> stream = local_pc_wrapper->GetUserMedia(
      /*audio=*/false, {}, /*video=*/true, {.width = 1280, .height = 720},
      /*fps=*/10);
  scoped_refptr<VideoTrackInterface> track = stream->GetVideoTracks()[0];

  RTCErrorOr<scoped_refptr<RtpTransceiverInterface>> transceiver_or_error =
      local_pc_wrapper->pc()->AddTransceiver(track);
  ASSERT_THAT(transceiver_or_error, IsRtcOk());
  scoped_refptr<RtpTransceiverInterface> transceiver =
      transceiver_or_error.MoveValue();

  scoped_refptr<RtpSenderInterface> sender = transceiver->sender();
  RtpParameters parameters = sender->GetParameters();
  ASSERT_THAT(parameters.encodings, SizeIs(1));
  parameters.degradation_preference =
      DegradationPreference::MAINTAIN_RESOLUTION;
  parameters.encodings[0].max_bitrate_bps = 10000;
  EXPECT_THAT(sender->SetParameters(parameters), IsRtcOk());

  Negotiate(local_pc_wrapper, remote_pc_wrapper);
  local_pc_wrapper->WaitForConnection();
  remote_pc_wrapper->WaitForConnection();

  // Wait for 30 frames of 720p to be sent, this should give some impression of
  // what the frame rate and limitations are.
  auto error_or_stats = GetStatsUntil(
      local_pc_wrapper,
      OutboundRtpStatsAre(
          UnorderedElementsAre(AllOf(HeightIs(720), FramesEncodedIs(Gt(30))))),
      {.timeout = kLongTimeoutForRampingUp});
  ASSERT_THAT(error_or_stats, IsRtcOk());
  scoped_refptr<const RTCStatsReport> report = error_or_stats.MoveValue();
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_THAT(outbound_rtps, SizeIs(1u));
  // 720p is guaranteed because of "maintain-resolution", but if the test setup
  // is correct, less than 80% of fps should be achieved due to BW limitation.
  EXPECT_EQ(outbound_rtps[0]->frame_width.value_or(0), 1280u);
  EXPECT_EQ(outbound_rtps[0]->frame_height.value_or(0), 720u);
  EXPECT_LT(outbound_rtps[0]->frames_per_second.value_or(0), 8);
  // The limitation should be reported correctly.
  EXPECT_EQ(outbound_rtps[0]->quality_limitation_reason, "bandwidth");
}

}  // namespace webrtc
