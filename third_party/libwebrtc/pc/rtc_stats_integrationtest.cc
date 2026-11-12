/*
 *  Copyright 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/match.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/audio_options.h"
#include "api/data_channel_interface.h"
#include "api/environment/environment.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_receiver_interface.h"
#include "api/rtp_sender_interface.h"
#include "api/rtp_transceiver_direction.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/stats/attribute.h"
#include "api/stats/rtc_stats.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"
#include "api/test/rtc_error_matchers.h"
#include "api/units/time_delta.h"
#include "pc/rtc_stats_traversal.h"
#include "pc/test/peer_connection_test_wrapper.h"
#include "pc/test/rtc_stats_obtainer.h"
#include "rtc_base/checks.h"
#include "rtc_base/thread.h"
#include "rtc_base/trace_event.h"
#include "rtc_base/virtual_socket_server.h"
#include "test/create_test_environment.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"
#include "test/wait_until.h"

using ::testing::Contains;
using ::testing::IsTrue;
using ::testing::SizeIs;

namespace webrtc {

namespace {

constexpr int64_t kGetStatsTimeoutMs = 20000;

class RTCStatsIntegrationTest : public ::testing::Test {
 public:
  RTCStatsIntegrationTest()
      : env_(CreateTestEnvironment()),
        network_thread_(new Thread(&virtual_socket_server_)) {
    RTC_CHECK(network_thread_->Start());

    caller_ = make_ref_counted<PeerConnectionTestWrapper>(
        "caller", env_, &virtual_socket_server_, network_thread_.get());
    callee_ = make_ref_counted<PeerConnectionTestWrapper>(
        "callee", env_, &virtual_socket_server_, network_thread_.get());
  }

  void StartCall() { StartCall(""); }
  void StartCall(const char* field_trial_string) {
    // Create PeerConnections
    PeerConnectionInterface::RTCConfiguration config;
    config.sdp_semantics = SdpSemantics::kUnifiedPlan;
    PeerConnectionInterface::IceServer ice_server;
    ice_server.uri = "stun:1.1.1.1:3478";
    config.servers.push_back(ice_server);
    EXPECT_TRUE(
        caller_->CreatePc(config, CreateBuiltinAudioEncoderFactory(),
                          CreateBuiltinAudioDecoderFactory(),
                          CreateTestFieldTrialsPtr(field_trial_string)));
    EXPECT_TRUE(
        callee_->CreatePc(config, CreateBuiltinAudioEncoderFactory(),
                          CreateBuiltinAudioDecoderFactory(),
                          CreateTestFieldTrialsPtr(field_trial_string)));
    PeerConnectionTestWrapper::Connect(caller_.get(), callee_.get());

    // Get user media for audio and video
    caller_->GetAndAddUserMedia(true, AudioOptions(), true);
    callee_->GetAndAddUserMedia(true, AudioOptions(), true);

    // Create data channels
    DataChannelInit init;
    caller_->CreateDataChannel("data", init);
    callee_->CreateDataChannel("data", init);

    // Negotiate and wait for call to establish
    caller_->CreateOffer(PeerConnectionInterface::RTCOfferAnswerOptions());
    caller_->WaitForCallEstablished();
    callee_->WaitForCallEstablished();
  }

  scoped_refptr<const RTCStatsReport> GetStatsFromCaller() {
    return GetStats(caller_->pc());
  }
  scoped_refptr<const RTCStatsReport> GetStatsFromCaller(
      scoped_refptr<RtpSenderInterface> selector) {
    return GetStats(caller_->pc(), selector);
  }
  scoped_refptr<const RTCStatsReport> GetStatsFromCaller(
      scoped_refptr<RtpReceiverInterface> selector) {
    return GetStats(caller_->pc(), selector);
  }

  scoped_refptr<const RTCStatsReport> GetStatsFromCallee() {
    return GetStats(callee_->pc());
  }
  scoped_refptr<const RTCStatsReport> GetStatsFromCallee(
      scoped_refptr<RtpSenderInterface> selector) {
    return GetStats(callee_->pc(), selector);
  }
  scoped_refptr<const RTCStatsReport> GetStatsFromCallee(
      scoped_refptr<RtpReceiverInterface> selector) {
    return GetStats(callee_->pc(), selector);
  }

 protected:
  scoped_refptr<const RTCStatsReport> GetStats(PeerConnectionInterface* pc) {
    scoped_refptr<RTCStatsObtainer> stats_obtainer =
        RTCStatsObtainer::Create(nullptr, [this]() { run_loop_.Quit(); });
    pc->GetStats(stats_obtainer.get());
    run_loop_.RunFor(TimeDelta::Millis(kGetStatsTimeoutMs));
    EXPECT_TRUE(stats_obtainer->report());
    return stats_obtainer->report();
  }

  template <typename T>
  scoped_refptr<const RTCStatsReport> GetStats(PeerConnectionInterface* pc,
                                               scoped_refptr<T> selector) {
    scoped_refptr<RTCStatsObtainer> stats_obtainer =
        RTCStatsObtainer::Create(nullptr, [this]() { run_loop_.Quit(); });
    pc->GetStats(selector, stats_obtainer);
    run_loop_.RunFor(TimeDelta::Millis(kGetStatsTimeoutMs));
    EXPECT_TRUE(stats_obtainer->report());
    return stats_obtainer->report();
  }

  test::RunLoop run_loop_;
  const Environment env_;
  // `network_thread_` uses `virtual_socket_server_` so they must be
  // constructed/destructed in the correct order.
  VirtualSocketServer virtual_socket_server_;
  std::unique_ptr<Thread> network_thread_;
  scoped_refptr<PeerConnectionTestWrapper> caller_;
  scoped_refptr<PeerConnectionTestWrapper> callee_;
};

class RTCStatsVerifier {
 public:
  RTCStatsVerifier(const RTCStatsReport* report, const RTCStats* stats)
      : report_(report), stats_(stats), all_tests_successful_(true) {
    RTC_CHECK(report_);
    RTC_CHECK(stats_);
    for (const auto& attribute : stats_->Attributes()) {
      untested_attribute_names_.insert(attribute.name());
    }
  }

  template <typename T>
  void MarkAttributeTested(const std::optional<T>& field,
                           bool test_successful) {
    untested_attribute_names_.erase(stats_->GetAttribute(field).name());
    all_tests_successful_ &= test_successful;
  }

  template <typename T>
  void TestAttributeIsDefined(const std::optional<T>& field) {
    EXPECT_TRUE(field.has_value())
        << stats_->type() << "." << stats_->GetAttribute(field).name() << "["
        << stats_->id() << "] was undefined.";
    MarkAttributeTested(field, field.has_value());
  }

  template <typename T>
  void TestAttributeIsUndefined(const std::optional<T>& field) {
    Attribute attribute = stats_->GetAttribute(field);
    EXPECT_FALSE(field.has_value())
        << stats_->type() << "." << attribute.name() << "[" << stats_->id()
        << "] was defined (" << attribute.ToString() << ").";
    MarkAttributeTested(field, !field.has_value());
  }

  template <typename T>
  void TestAttributeIsPositive(const std::optional<T>& field) {
    Attribute attribute = stats_->GetAttribute(field);
    EXPECT_TRUE(field.has_value()) << stats_->type() << "." << attribute.name()
                                   << "[" << stats_->id() << "] was undefined.";
    if (!field.has_value()) {
      MarkAttributeTested(field, false);
      return;
    }
    bool is_positive = field.value() > T(0);
    EXPECT_TRUE(is_positive)
        << stats_->type() << "." << attribute.name() << "[" << stats_->id()
        << "] was not positive (" << attribute.ToString() << ").";
    MarkAttributeTested(field, is_positive);
  }

  template <typename T>
  void TestAttributeIsNonNegative(const std::optional<T>& field) {
    Attribute attribute = stats_->GetAttribute(field);
    EXPECT_TRUE(field.has_value()) << stats_->type() << "." << attribute.name()
                                   << "[" << stats_->id() << "] was undefined.";
    if (!field.has_value()) {
      MarkAttributeTested(field, false);
      return;
    }
    bool is_non_negative = field.value() >= T(0);
    EXPECT_TRUE(is_non_negative)
        << stats_->type() << "." << attribute.name() << "[" << stats_->id()
        << "] was not non-negative (" << attribute.ToString() << ").";
    MarkAttributeTested(field, is_non_negative);
  }

  template <typename T>
  void TestAttributeIsIDReference(const std::optional<T>& field,
                                  const char* expected_type) {
    TestAttributeIsIDReference(field, expected_type, false);
  }

  template <typename T>
  void TestAttributeIsOptionalIDReference(const std::optional<T>& field,
                                          const char* expected_type) {
    TestAttributeIsIDReference(field, expected_type, true);
  }

  bool ExpectAllAttributesSuccessfullyTested() {
    if (untested_attribute_names_.empty())
      return all_tests_successful_;
    for (const char* name : untested_attribute_names_) {
      EXPECT_TRUE(false) << stats_->type() << "." << name << "[" << stats_->id()
                         << "] was not tested.";
    }
    return false;
  }

 private:
  template <typename T>
  void TestAttributeIsIDReference(const std::optional<T>& field,
                                  const char* expected_type,
                                  bool optional) {
    if (optional && !field.has_value()) {
      MarkAttributeTested(field, true);
      return;
    }
    Attribute attribute = stats_->GetAttribute(field);
    bool valid_reference = false;
    if (attribute.has_value()) {
      if (attribute.holds_alternative<std::string>()) {
        // A single ID.
        const RTCStats* referenced_stats =
            report_->Get(attribute.get<std::string>());
        valid_reference =
            referenced_stats && referenced_stats->type() == expected_type;
      } else if (attribute.holds_alternative<std::vector<std::string>>()) {
        // A vector of IDs.
        valid_reference = true;
        for (const std::string& id :
             attribute.get<std::vector<std::string>>()) {
          const RTCStats* referenced_stats = report_->Get(id);
          if (!referenced_stats || referenced_stats->type() != expected_type) {
            valid_reference = false;
            break;
          }
        }
      }
    }
    EXPECT_TRUE(valid_reference)
        << stats_->type() << "." << attribute.name()
        << " is not a reference to an "
           "existing dictionary of type "
        << expected_type << " (value: " << attribute.ToString() << ").";
    MarkAttributeTested(field, valid_reference);
  }

  scoped_refptr<const RTCStatsReport> report_;
  const RTCStats* stats_;
  std::set<const char*> untested_attribute_names_;
  bool all_tests_successful_;
};

class RTCStatsReportVerifier {
 public:
  static std::set<const char*> StatsTypes() {
    std::set<const char*> stats_types;
    stats_types.insert(RTCCertificateStats::kType);
    stats_types.insert(RTCCodecStats::kType);
    stats_types.insert(RTCDataChannelStats::kType);
    stats_types.insert(RTCIceCandidatePairStats::kType);
    stats_types.insert(RTCLocalIceCandidateStats::kType);
    stats_types.insert(RTCRemoteIceCandidateStats::kType);
    stats_types.insert(RTCPeerConnectionStats::kType);
    stats_types.insert(RTCInboundRtpStreamStats::kType);
    stats_types.insert(RTCOutboundRtpStreamStats::kType);
    stats_types.insert(RTCTransportStats::kType);
    return stats_types;
  }

  explicit RTCStatsReportVerifier(const RTCStatsReport* report)
      : report_(report) {}

  void VerifyReport(std::vector<const char*> allowed_missing_stats) {
    std::set<const char*> missing_stats = StatsTypes();
    bool verify_successful = true;
    std::vector<const RTCTransportStats*> transport_stats =
        report_->GetStatsOfType<RTCTransportStats>();
    EXPECT_EQ(transport_stats.size(), 1U);
    std::string selected_candidate_pair_id =
        *transport_stats[0]->selected_candidate_pair_id;
    for (const RTCStats& stats : *report_) {
      missing_stats.erase(stats.type());
      if (stats.type() == RTCCertificateStats::kType) {
        verify_successful &=
            VerifyRTCCertificateStats(stats.cast_to<RTCCertificateStats>());
      } else if (stats.type() == RTCCodecStats::kType) {
        verify_successful &=
            VerifyRTCCodecStats(stats.cast_to<RTCCodecStats>());
      } else if (stats.type() == RTCDataChannelStats::kType) {
        verify_successful &=
            VerifyRTCDataChannelStats(stats.cast_to<RTCDataChannelStats>());
      } else if (stats.type() == RTCIceCandidatePairStats::kType) {
        verify_successful &= VerifyRTCIceCandidatePairStats(
            stats.cast_to<RTCIceCandidatePairStats>(),
            stats.id() == selected_candidate_pair_id);
      } else if (stats.type() == RTCLocalIceCandidateStats::kType) {
        verify_successful &= VerifyRTCLocalIceCandidateStats(
            stats.cast_to<RTCLocalIceCandidateStats>());
      } else if (stats.type() == RTCRemoteIceCandidateStats::kType) {
        verify_successful &= VerifyRTCRemoteIceCandidateStats(
            stats.cast_to<RTCRemoteIceCandidateStats>());
      } else if (stats.type() == RTCPeerConnectionStats::kType) {
        verify_successful &= VerifyRTCPeerConnectionStats(
            stats.cast_to<RTCPeerConnectionStats>());
      } else if (stats.type() == RTCInboundRtpStreamStats::kType) {
        verify_successful &= VerifyRTCInboundRtpStreamStats(
            stats.cast_to<RTCInboundRtpStreamStats>());
      } else if (stats.type() == RTCOutboundRtpStreamStats::kType) {
        verify_successful &= VerifyRTCOutboundRtpStreamStats(
            stats.cast_to<RTCOutboundRtpStreamStats>());
      } else if (stats.type() == RTCRemoteInboundRtpStreamStats::kType) {
        verify_successful &= VerifyRTCRemoteInboundRtpStreamStats(
            stats.cast_to<RTCRemoteInboundRtpStreamStats>());
      } else if (stats.type() == RTCRemoteOutboundRtpStreamStats::kType) {
        verify_successful &= VerifyRTCRemoteOutboundRtpStreamStats(
            stats.cast_to<RTCRemoteOutboundRtpStreamStats>());
      } else if (stats.type() == RTCAudioSourceStats::kType) {
        // RTCAudioSourceStats::kType and RTCVideoSourceStats::kType both have
        // the value "media-source", but they are distinguishable with pointer
        // equality (==). In JavaScript they would be distinguished with `kind`.
        verify_successful &=
            VerifyRTCAudioSourceStats(stats.cast_to<RTCAudioSourceStats>());
      } else if (stats.type() == RTCVideoSourceStats::kType) {
        // RTCAudioSourceStats::kType and RTCVideoSourceStats::kType both have
        // the value "media-source", but they are distinguishable with pointer
        // equality (==). In JavaScript they would be distinguished with `kind`.
        verify_successful &=
            VerifyRTCVideoSourceStats(stats.cast_to<RTCVideoSourceStats>());
      } else if (stats.type() == RTCTransportStats::kType) {
        verify_successful &=
            VerifyRTCTransportStats(stats.cast_to<RTCTransportStats>());
      } else if (stats.type() == RTCAudioPlayoutStats::kType) {
        verify_successful &=
            VerifyRTCAudioPlayoutStats(stats.cast_to<RTCAudioPlayoutStats>());
      } else {
        EXPECT_TRUE(false) << "Unrecognized stats type: " << stats.type();
        verify_successful = false;
      }
    }
    for (const char* missing : missing_stats) {
      if (!absl::c_linear_search(allowed_missing_stats, missing)) {
        verify_successful = false;
        EXPECT_TRUE(false) << "Missing expected stats type: " << missing;
      }
    }
    EXPECT_TRUE(verify_successful)
        << "One or more problems with the stats. This is the report:\n"
        << report_->ToJson();
  }

  bool VerifyRTCCertificateStats(const RTCCertificateStats& certificate) {
    RTCStatsVerifier verifier(report_.get(), &certificate);
    verifier.TestAttributeIsDefined(certificate.fingerprint);
    verifier.TestAttributeIsDefined(certificate.fingerprint_algorithm);
    verifier.TestAttributeIsDefined(certificate.base64_certificate);
    verifier.TestAttributeIsOptionalIDReference(
        certificate.issuer_certificate_id, RTCCertificateStats::kType);
    return verifier.ExpectAllAttributesSuccessfullyTested();
  }

  bool VerifyRTCCodecStats(const RTCCodecStats& codec) {
    RTCStatsVerifier verifier(report_.get(), &codec);
    verifier.TestAttributeIsIDReference(codec.transport_id,
                                        RTCTransportStats::kType);
    verifier.TestAttributeIsDefined(codec.payload_type);
    verifier.TestAttributeIsDefined(codec.mime_type);
    verifier.TestAttributeIsPositive<uint32_t>(codec.clock_rate);

    if (codec.mime_type->rfind("audio", 0) == 0)
      verifier.TestAttributeIsPositive<uint32_t>(codec.channels);
    else
      verifier.TestAttributeIsUndefined(codec.channels);

    // sdp_fmtp_line is an optional field.
    verifier.MarkAttributeTested(codec.sdp_fmtp_line, true);
    return verifier.ExpectAllAttributesSuccessfullyTested();
  }

  bool VerifyRTCDataChannelStats(const RTCDataChannelStats& data_channel) {
    RTCStatsVerifier verifier(report_.get(), &data_channel);
    verifier.TestAttributeIsDefined(data_channel.label);
    verifier.TestAttributeIsDefined(data_channel.protocol);
    verifier.TestAttributeIsDefined(data_channel.data_channel_identifier);
    verifier.TestAttributeIsDefined(data_channel.state);
    verifier.TestAttributeIsNonNegative<uint32_t>(data_channel.messages_sent);
    verifier.TestAttributeIsNonNegative<uint64_t>(data_channel.bytes_sent);
    verifier.TestAttributeIsNonNegative<uint32_t>(
        data_channel.messages_received);
    verifier.TestAttributeIsNonNegative<uint64_t>(data_channel.bytes_received);
    return verifier.ExpectAllAttributesSuccessfullyTested();
  }

  bool VerifyRTCIceCandidatePairStats(
      const RTCIceCandidatePairStats& candidate_pair,
      bool is_selected_pair) {
    RTCStatsVerifier verifier(report_.get(), &candidate_pair);
    verifier.TestAttributeIsIDReference(candidate_pair.transport_id,
                                        RTCTransportStats::kType);
    verifier.TestAttributeIsIDReference(candidate_pair.local_candidate_id,
                                        RTCLocalIceCandidateStats::kType);
    verifier.TestAttributeIsIDReference(candidate_pair.remote_candidate_id,
                                        RTCRemoteIceCandidateStats::kType);
    verifier.TestAttributeIsDefined(candidate_pair.state);
    verifier.TestAttributeIsNonNegative<uint64_t>(candidate_pair.priority);
    verifier.TestAttributeIsDefined(candidate_pair.nominated);
    verifier.TestAttributeIsDefined(candidate_pair.writable);
    verifier.TestAttributeIsNonNegative<uint64_t>(candidate_pair.packets_sent);
    verifier.TestAttributeIsNonNegative<uint64_t>(
        candidate_pair.packets_discarded_on_send);
    verifier.TestAttributeIsNonNegative<uint64_t>(
        candidate_pair.packets_received);
    verifier.TestAttributeIsNonNegative<uint64_t>(candidate_pair.bytes_sent);
    verifier.TestAttributeIsNonNegative<uint64_t>(
        candidate_pair.bytes_discarded_on_send);
    verifier.TestAttributeIsNonNegative<uint64_t>(
        candidate_pair.bytes_received);
    verifier.TestAttributeIsNonNegative<double>(
        candidate_pair.total_round_trip_time);
    verifier.TestAttributeIsNonNegative<double>(
        candidate_pair.current_round_trip_time);
    if (is_selected_pair) {
      verifier.TestAttributeIsNonNegative<double>(
          candidate_pair.available_outgoing_bitrate);
      // A pair should be nominated in order to be selected.
      EXPECT_TRUE(*candidate_pair.nominated);
    } else {
      verifier.TestAttributeIsUndefined(
          candidate_pair.available_outgoing_bitrate);
    }
    verifier.TestAttributeIsUndefined(
        candidate_pair.available_incoming_bitrate);
    verifier.TestAttributeIsNonNegative<uint64_t>(
        candidate_pair.requests_received);
    verifier.TestAttributeIsNonNegative<uint64_t>(candidate_pair.requests_sent);
    verifier.TestAttributeIsNonNegative<uint64_t>(
        candidate_pair.responses_received);
    verifier.TestAttributeIsNonNegative<uint64_t>(
        candidate_pair.responses_sent);
    verifier.TestAttributeIsNonNegative<uint64_t>(
        candidate_pair.consent_requests_sent);
    verifier.TestAttributeIsDefined(
        candidate_pair.last_packet_received_timestamp);
    verifier.TestAttributeIsDefined(candidate_pair.last_packet_sent_timestamp);

    return verifier.ExpectAllAttributesSuccessfullyTested();
  }

  bool VerifyRTCIceCandidateStats(const RTCIceCandidateStats& candidate) {
    RTCStatsVerifier verifier(report_.get(), &candidate);
    verifier.TestAttributeIsIDReference(candidate.transport_id,
                                        RTCTransportStats::kType);
    verifier.TestAttributeIsDefined(candidate.is_remote);
    if (*candidate.is_remote) {
      verifier.TestAttributeIsUndefined(candidate.network_type);
      verifier.TestAttributeIsUndefined(candidate.network_adapter_type);
      verifier.TestAttributeIsUndefined(candidate.vpn);
    } else {
      verifier.TestAttributeIsDefined(candidate.network_type);
      verifier.TestAttributeIsDefined(candidate.network_adapter_type);
      verifier.TestAttributeIsDefined(candidate.vpn);
    }
    verifier.TestAttributeIsDefined(candidate.ip);
    verifier.TestAttributeIsDefined(candidate.address);
    verifier.TestAttributeIsNonNegative<int32_t>(candidate.port);
    verifier.TestAttributeIsDefined(candidate.protocol);
    verifier.TestAttributeIsDefined(candidate.candidate_type);
    verifier.TestAttributeIsNonNegative<int32_t>(candidate.priority);
    verifier.TestAttributeIsUndefined(candidate.url);
    verifier.TestAttributeIsUndefined(candidate.relay_protocol);
    verifier.TestAttributeIsDefined(candidate.foundation);
    verifier.TestAttributeIsUndefined(candidate.related_address);
    verifier.TestAttributeIsUndefined(candidate.related_port);
    verifier.TestAttributeIsDefined(candidate.username_fragment);
    verifier.TestAttributeIsUndefined(candidate.tcp_type);
    verifier.TestAttributeIsUndefined(candidate.network_slice);
    return verifier.ExpectAllAttributesSuccessfullyTested();
  }

  bool VerifyRTCLocalIceCandidateStats(
      const RTCLocalIceCandidateStats& local_candidate) {
    return VerifyRTCIceCandidateStats(local_candidate);
  }

  bool VerifyRTCRemoteIceCandidateStats(
      const RTCRemoteIceCandidateStats& remote_candidate) {
    return VerifyRTCIceCandidateStats(remote_candidate);
  }

  bool VerifyRTCPeerConnectionStats(
      const RTCPeerConnectionStats& peer_connection) {
    RTCStatsVerifier verifier(report_.get(), &peer_connection);
    verifier.TestAttributeIsNonNegative<uint32_t>(
        peer_connection.data_channels_opened);
    verifier.TestAttributeIsNonNegative<uint32_t>(
        peer_connection.data_channels_closed);
    return verifier.ExpectAllAttributesSuccessfullyTested();
  }

  void VerifyRTCRtpStreamStats(const RTCRtpStreamStats& stream,
                               RTCStatsVerifier& verifier) {
    verifier.TestAttributeIsDefined(stream.ssrc);
    verifier.TestAttributeIsDefined(stream.kind);
    verifier.TestAttributeIsIDReference(stream.transport_id,
                                        RTCTransportStats::kType);
    verifier.TestAttributeIsIDReference(stream.codec_id, RTCCodecStats::kType);
  }

  void VerifyRTCSentRtpStreamStats(const RTCSentRtpStreamStats& sent_stream,
                                   RTCStatsVerifier& verifier) {
    VerifyRTCRtpStreamStats(sent_stream, verifier);
    verifier.TestAttributeIsNonNegative<uint64_t>(sent_stream.packets_sent);
    verifier.TestAttributeIsNonNegative<uint64_t>(sent_stream.bytes_sent);
  }

  bool VerifyRTCInboundRtpStreamStats(
      const RTCInboundRtpStreamStats& inbound_stream) {
    RTCStatsVerifier verifier(report_.get(), &inbound_stream);
    VerifyRTCReceivedRtpStreamStats(inbound_stream, verifier);
    verifier.TestAttributeIsOptionalIDReference(
        inbound_stream.remote_id, RTCRemoteOutboundRtpStreamStats::kType);
    verifier.TestAttributeIsDefined(inbound_stream.mid);
    verifier.TestAttributeIsDefined(inbound_stream.track_identifier);
    // TODO: bugs.webrtc.org/42225697 - move to RTCReceivedRtpStreamStats
    // when wiring the RFC 8888 feedback to stats.
    verifier.TestAttributeIsNonNegative<int64_t>(
        inbound_stream.packets_received_with_ect1);
    verifier.TestAttributeIsNonNegative<int64_t>(
        inbound_stream.packets_received_with_ce);
    // TODO: bugs.webrtc.org/437303401 - test two attributes below are defined
    // when RFC8888 CongestionControlFeedback is negotiated.
    verifier.TestAttributeIsUndefined<int64_t>(
        inbound_stream.packets_reported_as_lost);
    verifier.TestAttributeIsUndefined<int64_t>(
        inbound_stream.packets_reported_as_lost_but_recovered);
    if (inbound_stream.kind.has_value() && *inbound_stream.kind == "video") {
      verifier.TestAttributeIsNonNegative<uint64_t>(inbound_stream.qp_sum);
      verifier.TestAttributeIsDefined(inbound_stream.decoder_implementation);
      verifier.TestAttributeIsDefined(inbound_stream.power_efficient_decoder);
    } else {
      verifier.TestAttributeIsUndefined(inbound_stream.qp_sum);
      verifier.TestAttributeIsUndefined(inbound_stream.decoder_implementation);
      verifier.TestAttributeIsUndefined(inbound_stream.power_efficient_decoder);
    }

    // As long as the corruption detection RTP header extension is not activated
    // it should not aggregate any corruption score. The tests where this header
    // extension is enabled are located in pc/peer_connection_integrationtest.cc
    verifier.TestAttributeIsUndefined(
        inbound_stream.total_corruption_probability);
    verifier.TestAttributeIsUndefined(
        inbound_stream.total_squared_corruption_probability);
    verifier.TestAttributeIsUndefined(inbound_stream.corruption_measurements);
    verifier.TestAttributeIsNonNegative<uint32_t>(
        inbound_stream.packets_received);
    if (inbound_stream.kind.has_value() && *inbound_stream.kind == "audio") {
      verifier.TestAttributeIsNonNegative<uint64_t>(
          inbound_stream.packets_discarded);
      verifier.TestAttributeIsNonNegative<uint64_t>(
          inbound_stream.fec_packets_received);
      verifier.TestAttributeIsNonNegative<uint64_t>(
          inbound_stream.fec_packets_discarded);
      verifier.TestAttributeIsUndefined(inbound_stream.fec_bytes_received);
    } else {
      verifier.TestAttributeIsUndefined(inbound_stream.packets_discarded);
      // FEC stats are only present when FlexFEC was negotiated which is guarded
      // by the WebRTC-FlexFEC-03-Advertised/Enabled/ field trial and off by
      // default.
      verifier.TestAttributeIsUndefined(inbound_stream.fec_bytes_received);
      verifier.TestAttributeIsUndefined(inbound_stream.fec_packets_received);
      verifier.TestAttributeIsUndefined(inbound_stream.fec_packets_discarded);
      verifier.TestAttributeIsUndefined(inbound_stream.fec_ssrc);
    }
    verifier.TestAttributeIsNonNegative<uint64_t>(
        inbound_stream.bytes_received);
    verifier.TestAttributeIsNonNegative<uint64_t>(
        inbound_stream.header_bytes_received);
    verifier.TestAttributeIsDefined(
        inbound_stream.last_packet_received_timestamp);
    if (inbound_stream.frames_received.value_or(0) > 0) {
      verifier.TestAttributeIsNonNegative<uint32_t>(inbound_stream.frame_width);
      verifier.TestAttributeIsNonNegative<uint32_t>(
          inbound_stream.frame_height);
    } else {
      verifier.TestAttributeIsUndefined(inbound_stream.frame_width);
      verifier.TestAttributeIsUndefined(inbound_stream.frame_height);
    }
    if (inbound_stream.frames_per_second.has_value()) {
      verifier.TestAttributeIsNonNegative<double>(
          inbound_stream.frames_per_second);
    } else {
      verifier.TestAttributeIsUndefined(inbound_stream.frames_per_second);
    }
    verifier.TestAttributeIsNonNegative<double>(
        inbound_stream.jitter_buffer_delay);
    verifier.TestAttributeIsNonNegative<uint64_t>(
        inbound_stream.jitter_buffer_emitted_count);
    verifier.TestAttributeIsNonNegative<double>(
        inbound_stream.jitter_buffer_target_delay);
    verifier.TestAttributeIsNonNegative<double>(
        inbound_stream.jitter_buffer_minimum_delay);
    verifier.TestAttributeIsNonNegative<double>(
        inbound_stream.total_processing_delay);
    if (inbound_stream.kind.has_value() && *inbound_stream.kind == "video") {
      verifier.TestAttributeIsUndefined(inbound_stream.total_samples_received);
      verifier.TestAttributeIsUndefined(inbound_stream.concealed_samples);
      verifier.TestAttributeIsUndefined(
          inbound_stream.silent_concealed_samples);
      verifier.TestAttributeIsUndefined(inbound_stream.concealment_events);
      verifier.TestAttributeIsUndefined(
          inbound_stream.inserted_samples_for_deceleration);
      verifier.TestAttributeIsUndefined(
          inbound_stream.removed_samples_for_acceleration);
      verifier.TestAttributeIsUndefined(inbound_stream.audio_level);
      verifier.TestAttributeIsUndefined(inbound_stream.total_audio_energy);
      verifier.TestAttributeIsUndefined(inbound_stream.total_samples_duration);
      verifier.TestAttributeIsNonNegative<uint32_t>(
          inbound_stream.frames_received);
      verifier.TestAttributeIsNonNegative<uint32_t>(inbound_stream.fir_count);
      verifier.TestAttributeIsNonNegative<uint32_t>(inbound_stream.pli_count);
      verifier.TestAttributeIsNonNegative<uint32_t>(inbound_stream.nack_count);
    } else {
      verifier.TestAttributeIsUndefined(inbound_stream.fir_count);
      verifier.TestAttributeIsUndefined(inbound_stream.pli_count);
      verifier.TestAttributeIsUndefined(inbound_stream.nack_count);
      verifier.TestAttributeIsPositive<uint64_t>(
          inbound_stream.total_samples_received);
      verifier.TestAttributeIsNonNegative<uint64_t>(
          inbound_stream.concealed_samples);
      verifier.TestAttributeIsNonNegative<uint64_t>(
          inbound_stream.silent_concealed_samples);
      verifier.TestAttributeIsNonNegative<uint64_t>(
          inbound_stream.concealment_events);
      verifier.TestAttributeIsNonNegative<uint64_t>(
          inbound_stream.inserted_samples_for_deceleration);
      verifier.TestAttributeIsNonNegative<uint64_t>(
          inbound_stream.removed_samples_for_acceleration);
      verifier.TestAttributeIsNonNegative<double>(
          inbound_stream.jitter_buffer_target_delay);
      verifier.TestAttributeIsNonNegative<double>(
          inbound_stream.jitter_buffer_minimum_delay);
      verifier.TestAttributeIsPositive<double>(inbound_stream.audio_level);
      verifier.TestAttributeIsPositive<double>(
          inbound_stream.total_audio_energy);
      verifier.TestAttributeIsPositive<double>(
          inbound_stream.total_samples_duration);
      verifier.TestAttributeIsUndefined(inbound_stream.frames_received);
    }

    // RTX stats are typically only defined for video where RTX is negotiated.
    if (inbound_stream.kind.has_value() && *inbound_stream.kind == "video") {
      verifier.TestAttributeIsNonNegative<uint64_t>(
          inbound_stream.retransmitted_packets_received);
      verifier.TestAttributeIsNonNegative<uint64_t>(
          inbound_stream.retransmitted_bytes_received);
      verifier.TestAttributeIsNonNegative<uint32_t>(inbound_stream.rtx_ssrc);
    } else {
      verifier.TestAttributeIsUndefined(
          inbound_stream.retransmitted_packets_received);
      verifier.TestAttributeIsUndefined(
          inbound_stream.retransmitted_bytes_received);
      verifier.TestAttributeIsUndefined(inbound_stream.rtx_ssrc);
      verifier.TestAttributeIsUndefined(inbound_stream.fec_ssrc);
    }

    // Test runtime too short to get an estimate (at least two RTCP sender
    // reports need to be received).
    verifier.MarkAttributeTested(inbound_stream.estimated_playout_timestamp,
                                 true);
    if (inbound_stream.kind.has_value() && *inbound_stream.kind == "video") {
      verifier.TestAttributeIsDefined(inbound_stream.frames_decoded);
      verifier.TestAttributeIsDefined(inbound_stream.key_frames_decoded);
      verifier.TestAttributeIsNonNegative<uint32_t>(
          inbound_stream.frames_dropped);
      verifier.TestAttributeIsNonNegative<double>(
          inbound_stream.total_decode_time);
      verifier.TestAttributeIsNonNegative<double>(
          inbound_stream.total_assembly_time);
      verifier.TestAttributeIsDefined(
          inbound_stream.frames_assembled_from_multiple_packets);
      verifier.TestAttributeIsNonNegative<double>(
          inbound_stream.total_inter_frame_delay);
      verifier.TestAttributeIsNonNegative<double>(
          inbound_stream.total_squared_inter_frame_delay);
      verifier.TestAttributeIsNonNegative<uint32_t>(inbound_stream.pause_count);
      verifier.TestAttributeIsNonNegative<double>(
          inbound_stream.total_pauses_duration);
      verifier.TestAttributeIsNonNegative<uint32_t>(
          inbound_stream.freeze_count);
      verifier.TestAttributeIsNonNegative<double>(
          inbound_stream.total_freezes_duration);
      // The integration test is not set up to test screen share; don't require
      // this to be present.
      verifier.MarkAttributeTested(inbound_stream.content_type, true);
      verifier.TestAttributeIsUndefined(inbound_stream.jitter_buffer_flushes);
      verifier.TestAttributeIsUndefined(
          inbound_stream.delayed_packet_outage_samples);
      verifier.TestAttributeIsUndefined(
          inbound_stream.relative_packet_arrival_delay);
      verifier.TestAttributeIsUndefined(inbound_stream.interruption_count);
      verifier.TestAttributeIsUndefined(
          inbound_stream.total_interruption_duration);
      verifier.TestAttributeIsNonNegative<double>(
          inbound_stream.min_playout_delay);
      verifier.TestAttributeIsDefined(inbound_stream.goog_timing_frame_info);
    } else {
      verifier.TestAttributeIsUndefined(inbound_stream.frames_decoded);
      verifier.TestAttributeIsUndefined(inbound_stream.key_frames_decoded);
      verifier.TestAttributeIsUndefined(inbound_stream.frames_dropped);
      verifier.TestAttributeIsUndefined(inbound_stream.total_decode_time);
      verifier.TestAttributeIsUndefined(inbound_stream.total_assembly_time);
      verifier.TestAttributeIsUndefined(
          inbound_stream.frames_assembled_from_multiple_packets);
      verifier.TestAttributeIsUndefined(inbound_stream.total_inter_frame_delay);
      verifier.TestAttributeIsUndefined(
          inbound_stream.total_squared_inter_frame_delay);
      verifier.TestAttributeIsUndefined(inbound_stream.pause_count);
      verifier.TestAttributeIsUndefined(inbound_stream.total_pauses_duration);
      verifier.TestAttributeIsUndefined(inbound_stream.freeze_count);
      verifier.TestAttributeIsUndefined(inbound_stream.total_freezes_duration);
      verifier.TestAttributeIsUndefined(inbound_stream.content_type);
      verifier.TestAttributeIsNonNegative<uint64_t>(
          inbound_stream.jitter_buffer_flushes);
      verifier.TestAttributeIsNonNegative<uint64_t>(
          inbound_stream.delayed_packet_outage_samples);
      verifier.TestAttributeIsNonNegative<double>(
          inbound_stream.relative_packet_arrival_delay);
      verifier.TestAttributeIsNonNegative<uint32_t>(
          inbound_stream.interruption_count);
      verifier.TestAttributeIsNonNegative<double>(
          inbound_stream.total_interruption_duration);
      verifier.TestAttributeIsUndefined(inbound_stream.min_playout_delay);
      verifier.TestAttributeIsUndefined(inbound_stream.goog_timing_frame_info);
    }
    if (inbound_stream.kind.has_value() && *inbound_stream.kind == "audio") {
      verifier.TestAttributeIsDefined(inbound_stream.playout_id);
    } else {
      verifier.TestAttributeIsUndefined(inbound_stream.playout_id);
    }

    return verifier.ExpectAllAttributesSuccessfullyTested();
  }

  bool VerifyRTCOutboundRtpStreamStats(
      const RTCOutboundRtpStreamStats& outbound_stream) {
    RTCStatsVerifier verifier(report_.get(), &outbound_stream);
    VerifyRTCSentRtpStreamStats(outbound_stream, verifier);

    verifier.TestAttributeIsDefined(outbound_stream.mid);
    verifier.TestAttributeIsDefined(outbound_stream.active);
    if (outbound_stream.kind.has_value() && *outbound_stream.kind == "video") {
      verifier.TestAttributeIsIDReference(outbound_stream.media_source_id,
                                          RTCVideoSourceStats::kType);
      verifier.TestAttributeIsNonNegative<uint32_t>(outbound_stream.fir_count);
      verifier.TestAttributeIsNonNegative<uint32_t>(outbound_stream.pli_count);
      if (*outbound_stream.frames_encoded > 0) {
        verifier.TestAttributeIsNonNegative<uint64_t>(outbound_stream.qp_sum);
        verifier.TestAttributeIsDefined(outbound_stream.psnr_sum);
        verifier.TestAttributeIsNonNegative(outbound_stream.psnr_measurements);
      } else {
        verifier.TestAttributeIsUndefined(outbound_stream.qp_sum);
        verifier.TestAttributeIsUndefined(outbound_stream.psnr_sum);
        verifier.TestAttributeIsUndefined(outbound_stream.psnr_measurements);
      }
    } else {
      verifier.TestAttributeIsUndefined(outbound_stream.fir_count);
      verifier.TestAttributeIsUndefined(outbound_stream.pli_count);
      verifier.TestAttributeIsIDReference(outbound_stream.media_source_id,
                                          RTCAudioSourceStats::kType);
      verifier.TestAttributeIsUndefined(outbound_stream.qp_sum);
      verifier.TestAttributeIsUndefined(outbound_stream.psnr_sum);
      verifier.TestAttributeIsUndefined(outbound_stream.psnr_measurements);
    }

    verifier.TestAttributeIsNonNegative<uint32_t>(outbound_stream.nack_count);
    verifier.TestAttributeIsOptionalIDReference(
        outbound_stream.remote_id, RTCRemoteInboundRtpStreamStats::kType);
    verifier.TestAttributeIsNonNegative<double>(
        outbound_stream.total_packet_send_delay);
    verifier.TestAttributeIsNonNegative<uint64_t>(
        outbound_stream.retransmitted_packets_sent);
    verifier.TestAttributeIsNonNegative<uint64_t>(
        outbound_stream.header_bytes_sent);
    verifier.TestAttributeIsNonNegative<uint64_t>(
        outbound_stream.retransmitted_bytes_sent);
    verifier.TestAttributeIsNonNegative<double>(outbound_stream.target_bitrate);
    verifier.TestAttributeIsNonNegative<int64_t>(
        outbound_stream.packets_sent_with_ect1);
    if (outbound_stream.kind.has_value() && *outbound_stream.kind == "video") {
      verifier.TestAttributeIsDefined(outbound_stream.frames_encoded);
      verifier.TestAttributeIsDefined(outbound_stream.key_frames_encoded);
      verifier.TestAttributeIsNonNegative<double>(
          outbound_stream.total_encode_time);
      verifier.TestAttributeIsDefined(
          outbound_stream.quality_limitation_reason);
      verifier.TestAttributeIsDefined(
          outbound_stream.quality_limitation_durations);
      verifier.TestAttributeIsNonNegative<uint32_t>(
          outbound_stream.quality_limitation_resolution_changes);
      // The integration test is not set up to test screen share; don't require
      // this to be present.
      verifier.MarkAttributeTested(outbound_stream.content_type, true);
      verifier.TestAttributeIsDefined(outbound_stream.encoder_implementation);
      verifier.TestAttributeIsDefined(outbound_stream.power_efficient_encoder);
      // Unless an implementation-specific amount of time has passed and at
      // least one frame has been encoded, undefined is reported. Because it
      // is hard to tell what is the case here, we treat FPS as optional.
      // TODO(hbos): Update the tests to run until all implemented metrics
      // should be populated.
      if (outbound_stream.frames_per_second.has_value()) {
        verifier.TestAttributeIsNonNegative<double>(
            outbound_stream.frames_per_second);
      } else {
        verifier.TestAttributeIsUndefined(outbound_stream.frames_per_second);
      }
      verifier.TestAttributeIsNonNegative<uint32_t>(
          outbound_stream.frame_height);
      verifier.TestAttributeIsNonNegative<uint32_t>(
          outbound_stream.frame_width);
      verifier.TestAttributeIsNonNegative<uint32_t>(
          outbound_stream.frames_sent);
      verifier.TestAttributeIsNonNegative<uint32_t>(
          outbound_stream.huge_frames_sent);
      // RID is N/A because this test uses singlecast.
      verifier.TestAttributeIsUndefined(outbound_stream.rid);
      // In singlecast, the only encoding that exists has index 0.
      verifier.TestAttributeIsDefined(outbound_stream.encoding_index);
      EXPECT_TRUE(outbound_stream.encoding_index.has_value() &&
                  outbound_stream.encoding_index.value() == 0);
      verifier.TestAttributeIsDefined(outbound_stream.scalability_mode);
      verifier.TestAttributeIsNonNegative<uint32_t>(outbound_stream.rtx_ssrc);
    } else {
      verifier.TestAttributeIsUndefined(outbound_stream.frames_encoded);
      verifier.TestAttributeIsUndefined(outbound_stream.key_frames_encoded);
      verifier.TestAttributeIsUndefined(outbound_stream.total_encode_time);
      verifier.TestAttributeIsUndefined(
          outbound_stream.quality_limitation_reason);
      verifier.TestAttributeIsUndefined(
          outbound_stream.quality_limitation_durations);
      verifier.TestAttributeIsUndefined(
          outbound_stream.quality_limitation_resolution_changes);
      verifier.TestAttributeIsUndefined(outbound_stream.content_type);
      verifier.TestAttributeIsUndefined(outbound_stream.encoder_implementation);
      verifier.TestAttributeIsUndefined(
          outbound_stream.power_efficient_encoder);
      verifier.TestAttributeIsUndefined(outbound_stream.rid);
      verifier.TestAttributeIsUndefined(outbound_stream.encoding_index);
      verifier.TestAttributeIsUndefined(outbound_stream.frames_per_second);
      verifier.TestAttributeIsUndefined(outbound_stream.frame_height);
      verifier.TestAttributeIsUndefined(outbound_stream.frame_width);
      verifier.TestAttributeIsUndefined(outbound_stream.frames_sent);
      verifier.TestAttributeIsUndefined(outbound_stream.huge_frames_sent);
      verifier.TestAttributeIsUndefined(outbound_stream.scalability_mode);
      verifier.TestAttributeIsUndefined(outbound_stream.rtx_ssrc);
    }
    return verifier.ExpectAllAttributesSuccessfullyTested();
  }

  void VerifyRTCReceivedRtpStreamStats(
      const RTCReceivedRtpStreamStats& received_rtp,
      RTCStatsVerifier& verifier) {
    VerifyRTCRtpStreamStats(received_rtp, verifier);
    verifier.TestAttributeIsNonNegative<double>(received_rtp.jitter);
    verifier.TestAttributeIsDefined(received_rtp.packets_lost);
  }

  bool VerifyRTCRemoteInboundRtpStreamStats(
      const RTCRemoteInboundRtpStreamStats& remote_inbound_stream) {
    RTCStatsVerifier verifier(report_.get(), &remote_inbound_stream);
    VerifyRTCReceivedRtpStreamStats(remote_inbound_stream, verifier);
    verifier.TestAttributeIsDefined(remote_inbound_stream.fraction_lost);
    verifier.TestAttributeIsIDReference(remote_inbound_stream.local_id,
                                        RTCOutboundRtpStreamStats::kType);
    verifier.TestAttributeIsNonNegative<double>(
        remote_inbound_stream.total_round_trip_time);
    verifier.TestAttributeIsNonNegative<int32_t>(
        remote_inbound_stream.round_trip_time_measurements);
    // `round_trip_time_measurements` should always be defined since it's a
    // counter and it is implemented, but if it isn't the only sensible
    // interpretation for missing value is "no measurements", i.e. 0. If missing
    // the `verifier` will mark the attribute as such above and fail the test.
    if (remote_inbound_stream.round_trip_time_measurements.value_or(0) > 0) {
      verifier.TestAttributeIsNonNegative<double>(
          remote_inbound_stream.round_trip_time);
    } else {
      // It's possible to have an RTCP report without any round trip time
      // measurements yet, in which case this value is undefined.
      verifier.TestAttributeIsUndefined(remote_inbound_stream.round_trip_time);
    }
    // TODO: bugs.webrtc.org/42225697 - move to RTCReceivedRtpStreamStats
    // when wiring the RFC 8888 feedback to stats.
    verifier.TestAttributeIsUndefined<int64_t>(
        remote_inbound_stream.packets_received_with_ect1);
    verifier.TestAttributeIsUndefined<int64_t>(
        remote_inbound_stream.packets_received_with_ce);
    verifier.TestAttributeIsUndefined<int64_t>(
        remote_inbound_stream.packets_reported_as_lost);
    verifier.TestAttributeIsUndefined<int64_t>(
        remote_inbound_stream.packets_reported_as_lost_but_recovered);
    verifier.TestAttributeIsUndefined<int64_t>(
        remote_inbound_stream.packets_with_bleached_ect1_marking);

    return verifier.ExpectAllAttributesSuccessfullyTested();
  }

  bool VerifyRTCRemoteOutboundRtpStreamStats(
      const RTCRemoteOutboundRtpStreamStats& remote_outbound_stream) {
    RTCStatsVerifier verifier(report_.get(), &remote_outbound_stream);
    VerifyRTCRtpStreamStats(remote_outbound_stream, verifier);
    VerifyRTCSentRtpStreamStats(remote_outbound_stream, verifier);
    verifier.TestAttributeIsIDReference(remote_outbound_stream.local_id,
                                        RTCInboundRtpStreamStats::kType);
    verifier.TestAttributeIsNonNegative<double>(
        remote_outbound_stream.remote_timestamp);
    verifier.TestAttributeIsDefined(remote_outbound_stream.reports_sent);
    // RTT-related attributes need DLRR.
    verifier.MarkAttributeTested(remote_outbound_stream.round_trip_time, true);
    verifier.MarkAttributeTested(
        remote_outbound_stream.round_trip_time_measurements, true);
    verifier.MarkAttributeTested(remote_outbound_stream.total_round_trip_time,
                                 true);
    return verifier.ExpectAllAttributesSuccessfullyTested();
  }

  void VerifyRTCMediaSourceStats(const RTCMediaSourceStats& media_source,
                                 RTCStatsVerifier* verifier) {
    verifier->TestAttributeIsDefined(media_source.track_identifier);
    verifier->TestAttributeIsDefined(media_source.kind);
    if (media_source.kind.has_value()) {
      EXPECT_TRUE((*media_source.kind == "audio" &&
                   media_source.type() == RTCAudioSourceStats::kType) ||
                  (*media_source.kind == "video" &&
                   media_source.type() == RTCVideoSourceStats::kType));
    }
  }

  bool VerifyRTCAudioSourceStats(const RTCAudioSourceStats& audio_source) {
    RTCStatsVerifier verifier(report_.get(), &audio_source);
    VerifyRTCMediaSourceStats(audio_source, &verifier);
    // Audio level, unlike audio energy, only gets updated at a certain
    // frequency, so we don't require that one to be positive to avoid a race
    // (https://crbug.com/webrtc/10962).
    verifier.TestAttributeIsNonNegative<double>(audio_source.audio_level);
    verifier.TestAttributeIsPositive<double>(audio_source.total_audio_energy);
    verifier.TestAttributeIsPositive<double>(
        audio_source.total_samples_duration);
    // TODO(hbos): `echo_return_loss` and `echo_return_loss_enhancement` are
    // flaky on msan bot (sometimes defined, sometimes undefined). Should the
    // test run until available or is there a way to have it always be
    // defined? crbug.com/627816
    verifier.MarkAttributeTested(audio_source.echo_return_loss, true);
    verifier.MarkAttributeTested(audio_source.echo_return_loss_enhancement,
                                 true);
    return verifier.ExpectAllAttributesSuccessfullyTested();
  }

  bool VerifyRTCVideoSourceStats(const RTCVideoSourceStats& video_source) {
    RTCStatsVerifier verifier(report_.get(), &video_source);
    VerifyRTCMediaSourceStats(video_source, &verifier);
    // TODO(hbos): This integration test uses fakes that doesn't support
    // VideoTrackSourceInterface::Stats. When this is fixed we should
    // TestAttributeIsNonNegative<uint32_t>() for `width` and `height` instead
    // to reflect real code.
    verifier.TestAttributeIsUndefined(video_source.width);
    verifier.TestAttributeIsUndefined(video_source.height);
    verifier.TestAttributeIsNonNegative<uint32_t>(video_source.frames);
    verifier.TestAttributeIsNonNegative<double>(video_source.frames_per_second);
    return verifier.ExpectAllAttributesSuccessfullyTested();
  }

  bool VerifyRTCTransportStats(const RTCTransportStats& transport) {
    RTCStatsVerifier verifier(report_.get(), &transport);
    verifier.TestAttributeIsNonNegative<uint64_t>(transport.bytes_sent);
    verifier.TestAttributeIsNonNegative<uint64_t>(transport.packets_sent);
    verifier.TestAttributeIsNonNegative<uint64_t>(transport.bytes_received);
    verifier.TestAttributeIsNonNegative<uint64_t>(transport.packets_received);
    verifier.TestAttributeIsOptionalIDReference(
        transport.rtcp_transport_stats_id, RTCTransportStats::kType);
    verifier.TestAttributeIsDefined(transport.dtls_state);
    verifier.TestAttributeIsIDReference(transport.selected_candidate_pair_id,
                                        RTCIceCandidatePairStats::kType);
    verifier.TestAttributeIsIDReference(transport.local_certificate_id,
                                        RTCCertificateStats::kType);
    verifier.TestAttributeIsIDReference(transport.remote_certificate_id,
                                        RTCCertificateStats::kType);
    verifier.TestAttributeIsDefined(transport.tls_version);
    verifier.TestAttributeIsDefined(transport.dtls_cipher);
    verifier.TestAttributeIsDefined(transport.dtls_role);
    verifier.TestAttributeIsDefined(transport.srtp_cipher);
    verifier.TestAttributeIsPositive<uint32_t>(
        transport.selected_candidate_pair_changes);
    verifier.TestAttributeIsDefined(transport.ice_role);
    verifier.TestAttributeIsDefined(transport.ice_local_username_fragment);
    verifier.TestAttributeIsDefined(transport.ice_state);
    // TODO: bugs.webrtc.org/437303401 - Flip when enabling L4S by default.
    verifier.TestAttributeIsUndefined(transport.ccfb_messages_received);
    return verifier.ExpectAllAttributesSuccessfullyTested();
  }

  bool VerifyRTCAudioPlayoutStats(const RTCAudioPlayoutStats& audio_playout) {
    RTCStatsVerifier verifier(report_.get(), &audio_playout);
    verifier.TestAttributeIsDefined(audio_playout.kind);
    if (audio_playout.kind.has_value()) {
      EXPECT_EQ(*audio_playout.kind, "audio");
    }
    verifier.TestAttributeIsNonNegative<uint64_t>(
        audio_playout.synthesized_samples_events);
    verifier.TestAttributeIsNonNegative<double>(
        audio_playout.synthesized_samples_duration);
    verifier.TestAttributeIsNonNegative<uint64_t>(
        audio_playout.total_samples_count);
    verifier.TestAttributeIsNonNegative<double>(
        audio_playout.total_samples_duration);
    verifier.TestAttributeIsNonNegative<double>(
        audio_playout.total_playout_delay);
    return verifier.ExpectAllAttributesSuccessfullyTested();
  }

 private:
  scoped_refptr<const RTCStatsReport> report_;
};

#ifdef WEBRTC_HAVE_SCTP
TEST_F(RTCStatsIntegrationTest, GetStatsFromCaller) {
  StartCall();

  scoped_refptr<const RTCStatsReport> report = GetStatsFromCaller();
  RTCStatsReportVerifier(report.get()).VerifyReport({});
}

TEST_F(RTCStatsIntegrationTest, GetStatsFromCallee) {
  StartCall();

  scoped_refptr<const RTCStatsReport> report;
  // Wait for round trip time measurements to be defined.
  constexpr int kMaxWaitMs = 10000;
  auto GetStatsReportAndReturnTrueIfRttIsDefined = [&report, this] {
    report = GetStatsFromCallee();
    auto inbound_stats =
        report->GetStatsOfType<RTCRemoteInboundRtpStreamStats>();
    return !inbound_stats.empty() &&
           inbound_stats.front()->round_trip_time.has_value() &&
           inbound_stats.front()->round_trip_time_measurements.has_value();
  };
  EXPECT_TRUE(
      WaitUntil([&] { return GetStatsReportAndReturnTrueIfRttIsDefined(); },
                {.timeout = TimeDelta::Millis(kMaxWaitMs)}));
  RTCStatsReportVerifier(report.get()).VerifyReport({});
}

// A recvonly audio endpoint measures non-sender RTT (RRTR/DLRR) once rcvr-rtt
// is negotiated, surfacing round_trip_time on remote-outbound-rtp. The SDP
// negotiation itself is covered by the negotiation CL.
TEST_F(RTCStatsIntegrationTest, RcvrRttNegotiatedEnablesAudioRtt) {
  PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  PeerConnectionInterface::IceServer ice_server;
  ice_server.uri = "stun:1.1.1.1:3478";
  config.servers.push_back(ice_server);
  EXPECT_TRUE(caller_->CreatePc(config, CreateBuiltinAudioEncoderFactory(),
                                CreateBuiltinAudioDecoderFactory()));
  EXPECT_TRUE(callee_->CreatePc(config, CreateBuiltinAudioEncoderFactory(),
                                CreateBuiltinAudioDecoderFactory()));
  PeerConnectionTestWrapper::Connect(caller_.get(), callee_.get());

  // Caller sends audio; callee adds nothing, so it is negotiated recvonly.
  caller_->GetAndAddUserMedia(true, AudioOptions(), false);
  caller_->CreateOffer(PeerConnectionInterface::RTCOfferAnswerOptions());
  EXPECT_TRUE(caller_->WaitForConnection());
  EXPECT_TRUE(callee_->WaitForConnection());

  // The recvonly callee should measure non-sender RTT.
  // TODO(bugs.webrtc.org/516205747): runs against the virtual socket server
  // with a real clock (a valid RTT needs an RRTR exchange plus three SR
  // blocks), so this takes a few seconds; switch to a simulated clock when
  // the fixture supports it.
  scoped_refptr<const RTCStatsReport> report;
  constexpr TimeDelta kMaxWait = TimeDelta::Seconds(10);
  EXPECT_TRUE(WaitUntil(
      [&] {
        report = GetStatsFromCallee();
        for (const auto* stats :
             report->GetStatsOfType<RTCRemoteOutboundRtpStreamStats>()) {
          if (stats->round_trip_time_measurements.value_or(0) > 0u) {
            return true;
          }
        }
        return false;
      },
      {.timeout = kMaxWait}));
}

// With rcvr-rtt negotiated, a recvonly endpoint must report non-sender RTT on
// both the audio and video remote-outbound-rtp stats (video resolves the
// bugs.webrtc.org/12529 stats gap).
TEST_F(RTCStatsIntegrationTest, RcvrRttNegotiatedEnablesVideoRtt) {
  PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  PeerConnectionInterface::IceServer ice_server;
  ice_server.uri = "stun:1.1.1.1:3478";
  config.servers.push_back(ice_server);
  EXPECT_TRUE(caller_->CreatePc(config, CreateBuiltinAudioEncoderFactory(),
                                CreateBuiltinAudioDecoderFactory()));
  EXPECT_TRUE(callee_->CreatePc(config, CreateBuiltinAudioEncoderFactory(),
                                CreateBuiltinAudioDecoderFactory()));
  PeerConnectionTestWrapper::Connect(caller_.get(), callee_.get());

  // Caller sends audio and video; callee adds nothing, so it is recvonly.
  caller_->GetAndAddUserMedia(true, AudioOptions(), true);
  caller_->CreateOffer(PeerConnectionInterface::RTCOfferAnswerOptions());
  EXPECT_TRUE(caller_->WaitForConnection());
  EXPECT_TRUE(callee_->WaitForConnection());

  // TODO(bugs.webrtc.org/516205747): Switch to a simulated clock when the
  // fixture supports it. See the audio test above for more details.
  scoped_refptr<const RTCStatsReport> report;
  constexpr TimeDelta kMaxWait = TimeDelta::Seconds(10);
  // Both the audio and video remote-outbound-rtp must report a non-sender
  // RTT measurement, so at least two streams carry one.
  EXPECT_TRUE(WaitUntil(
      [&] {
        report = GetStatsFromCallee();
        int with_rtt = 0;
        for (const auto* stats :
             report->GetStatsOfType<RTCRemoteOutboundRtpStreamStats>()) {
          if (stats->round_trip_time_measurements.value_or(0) > 0u) {
            ++with_rtt;
          }
        }
        return with_rtt >= 2;
      },
      {.timeout = kMaxWait}));
}

// These tests exercise the integration of the stats selection algorithm inside
// of PeerConnection. See rtcstatstraveral_unittest.cc for more detailed stats
// traversal tests on particular stats graphs.
TEST_F(RTCStatsIntegrationTest, GetStatsWithSenderSelector) {
  StartCall();
  ASSERT_FALSE(caller_->pc()->GetSenders().empty());
  scoped_refptr<const RTCStatsReport> report =
      GetStatsFromCaller(caller_->pc()->GetSenders()[0]);
  std::vector<const char*> allowed_missing_stats = {
      // TODO(hbos): Include RTC[Audio/Video]ReceiverStats when implemented.
      // TODO(hbos): Include RTCRemoteOutboundRtpStreamStats when implemented.
      // TODO(hbos): Include RTCRtpContributingSourceStats when implemented.
      RTCInboundRtpStreamStats::kType,
      RTCPeerConnectionStats::kType,
      RTCDataChannelStats::kType,
  };
  RTCStatsReportVerifier(report.get()).VerifyReport(allowed_missing_stats);
  EXPECT_TRUE(report->size());
}

TEST_F(RTCStatsIntegrationTest, GetStatsWithReceiverSelector) {
  StartCall();

  ASSERT_FALSE(caller_->pc()->GetReceivers().empty());
  scoped_refptr<const RTCStatsReport> report =
      GetStatsFromCaller(caller_->pc()->GetReceivers()[0]);
  std::vector<const char*> allowed_missing_stats = {
      // TODO(hbos): Include RTC[Audio/Video]SenderStats when implemented.
      // TODO(hbos): Include RTCRemoteInboundRtpStreamStats when implemented.
      // TODO(hbos): Include RTCRtpContributingSourceStats when implemented.
      RTCOutboundRtpStreamStats::kType,
      RTCPeerConnectionStats::kType,
      RTCDataChannelStats::kType,
  };
  RTCStatsReportVerifier(report.get()).VerifyReport(allowed_missing_stats);
  EXPECT_TRUE(report->size());
}

TEST_F(RTCStatsIntegrationTest, GetStatsWithInvalidSenderSelector) {
  StartCall();

  ASSERT_FALSE(callee_->pc()->GetSenders().empty());
  // The selector is invalid for the caller because it belongs to the callee.
  auto invalid_selector = callee_->pc()->GetSenders()[0];
  scoped_refptr<const RTCStatsReport> report =
      GetStatsFromCaller(invalid_selector);
  EXPECT_FALSE(report->size());
}

TEST_F(RTCStatsIntegrationTest, GetStatsWithInvalidReceiverSelector) {
  StartCall();

  ASSERT_FALSE(callee_->pc()->GetReceivers().empty());
  // The selector is invalid for the caller because it belongs to the callee.
  auto invalid_selector = callee_->pc()->GetReceivers()[0];
  scoped_refptr<const RTCStatsReport> report =
      GetStatsFromCaller(invalid_selector);
  EXPECT_FALSE(report->size());
}

// TODO(bugs.webrtc.org/10041) For now this is equivalent to the following
// test GetsStatsWhileClosingPeerConnection, because pc() is closed by
// PeerConnectionTestWrapper. See: bugs.webrtc.org/9847
TEST_F(RTCStatsIntegrationTest,
       DISABLED_GetStatsWhileDestroyingPeerConnection) {
  StartCall();

  scoped_refptr<RTCStatsObtainer> stats_obtainer = RTCStatsObtainer::Create();
  caller_->pc()->GetStats(stats_obtainer.get());
  // This will destroy the peer connection.
  caller_ = nullptr;
  // Any pending stats requests should have completed in the act of destroying
  // the peer connection.
  ASSERT_TRUE(stats_obtainer->report());
}

TEST_F(RTCStatsIntegrationTest, GetsStatsWhileClosingPeerConnection) {
  StartCall();

  scoped_refptr<RTCStatsObtainer> stats_obtainer =
      RTCStatsObtainer::Create(nullptr, [&]() { run_loop_.Quit(); });
  caller_->pc()->GetStats(stats_obtainer.get());
  caller_->pc()->Close();
  run_loop_.Run();
  ASSERT_TRUE(stats_obtainer->report());
}

// GetStatsReferencedIds() is optimized to recognize what is or isn't a
// referenced ID based on dictionary type information and knowing what
// attributes are used as references, as opposed to iterating all attributes to
// find the ones with the "Id" or "Ids" suffix. As such, GetStatsReferencedIds()
// is tested as an integration test instead of a unit test in order to guard
// against adding new references and forgetting to update
// GetStatsReferencedIds().
TEST_F(RTCStatsIntegrationTest, GetStatsReferencedIds) {
  StartCall();

  scoped_refptr<const RTCStatsReport> report = GetStatsFromCallee();
  for (const RTCStats& stats : *report) {
    // Find all references by looking at all string attributes with the "Id" or
    // "Ids" suffix.
    std::set<const std::string*> expected_ids;
    for (const auto& attribute : stats.Attributes()) {
      if (!attribute.has_value())
        continue;
      if (attribute.holds_alternative<std::string>()) {
        if (absl::EndsWith(attribute.name(), "Id")) {
          expected_ids.insert(&attribute.get<std::string>());
        }
      } else if (attribute.holds_alternative<std::vector<std::string>>()) {
        if (absl::EndsWith(attribute.name(), "Ids")) {
          for (const std::string& id :
               attribute.get<std::vector<std::string>>())
            expected_ids.insert(&id);
        }
      }
    }

    std::vector<const std::string*> neighbor_ids = GetStatsReferencedIds(stats);
    EXPECT_EQ(neighbor_ids.size(), expected_ids.size());
    for (const std::string* neighbor_id : neighbor_ids) {
      EXPECT_THAT(expected_ids, Contains(neighbor_id));
    }
    for (const std::string* expected_id : expected_ids) {
      EXPECT_THAT(neighbor_ids, Contains(expected_id));
    }
  }
}

TEST_F(RTCStatsIntegrationTest, GetStatsContainsNoDuplicateAttributes) {
  StartCall();

  scoped_refptr<const RTCStatsReport> report = GetStatsFromCallee();
  for (const RTCStats& stats : *report) {
    std::set<std::string> attribute_names;
    for (const auto& attribute : stats.Attributes()) {
      EXPECT_TRUE(attribute_names.find(attribute.name()) ==
                  attribute_names.end())
          << attribute.name() << " is a duplicate!";
      attribute_names.insert(attribute.name());
    }
  }
}

TEST_F(RTCStatsIntegrationTest, GetStatsAfterClose) {
  StartCall();

  caller_->pc()->Close();

  scoped_refptr<const RTCStatsReport> report = GetStatsFromCaller();
  ASSERT_EQ(report->size(), 1u);
  EXPECT_EQ(report->begin()->type(), RTCPeerConnectionStats::kType);
}

TEST_F(RTCStatsIntegrationTest, ExperimentalTransportCcfbStats) {
  // CCFB negotiation is asymmetric: the generator sets the flag but doesn't
  // add it to codecs' feedback_params, while the parser adds it to codecs'
  // feedback_params. This causes false positive munging detection (71, 86)
  // during the internal SetLocalDescription roundtrip in
  // PeerConnectionTestWrapper.
  StartCall(
      "WebRTC-RFC8888CongestionControlFeedback/Enabled,offer:true/"
      "WebRTC-NoSdpMangleAllowForTesting/Enabled,71,86/");

  // This assumes all other stats are ok and tests the stats which should be
  // different under the field trial.
  scoped_refptr<const RTCStatsReport> report = GetStatsFromCaller();
  for (const RTCStats& stats : *report) {
    if (stats.type() == RTCTransportStats::kType) {
      const RTCTransportStats& transport(stats.cast_to<RTCTransportStats>());
      RTCStatsVerifier verifier(report.get(), &transport);
      verifier.TestAttributeIsNonNegative<int>(
          transport.ccfb_messages_received);
    } else if (stats.type() == RTCInboundRtpStreamStats::kType) {
      const RTCInboundRtpStreamStats& inbound(
          stats.cast_to<RTCInboundRtpStreamStats>());
      RTCStatsVerifier verifier(report.get(), &inbound);
      verifier.TestAttributeIsNonNegative<int64_t>(
          inbound.packets_received_with_ect1);
      verifier.TestAttributeIsNonNegative<int64_t>(
          inbound.packets_received_with_ce);
      verifier.TestAttributeIsNonNegative<int64_t>(
          inbound.packets_reported_as_lost);
      verifier.TestAttributeIsNonNegative<int64_t>(
          inbound.packets_reported_as_lost_but_recovered);
    } else if (stats.type() == RTCRemoteInboundRtpStreamStats::kType) {
      const RTCRemoteInboundRtpStreamStats& remote_inbound =
          stats.cast_to<RTCRemoteInboundRtpStreamStats>();
      RTCStatsVerifier verifier(report.get(), &remote_inbound);
      verifier.TestAttributeIsNonNegative<int64_t>(
          remote_inbound.packets_received_with_ect1);
      verifier.TestAttributeIsNonNegative<int64_t>(
          remote_inbound.packets_received_with_ce);
      verifier.TestAttributeIsNonNegative<int64_t>(
          remote_inbound.packets_reported_as_lost);
      verifier.TestAttributeIsNonNegative<int64_t>(
          remote_inbound.packets_reported_as_lost_but_recovered);
      verifier.TestAttributeIsNonNegative<int64_t>(
          remote_inbound.packets_with_bleached_ect1_marking);
    }
  }
}

class RTCStatsRtpLifetimeTest : public RTCStatsIntegrationTest {
 public:
  RTCStatsRtpLifetimeTest() : RTCStatsIntegrationTest() {
    EXPECT_TRUE(caller_->CreatePc({}, CreateBuiltinAudioEncoderFactory(),
                                  CreateBuiltinAudioDecoderFactory()));
    EXPECT_TRUE(callee_->CreatePc({}, CreateBuiltinAudioEncoderFactory(),
                                  CreateBuiltinAudioDecoderFactory()));
  }
};

TEST_F(RTCStatsRtpLifetimeTest, AudioOutboundRtpMissingBeforeStable) {
  // Caller to send audio.
  scoped_refptr<MediaStreamInterface> stream = caller_->GetUserMedia(
      /*audio=*/true, {}, /*video=*/false);
  scoped_refptr<AudioTrackInterface> track = stream->GetAudioTracks()[0];
  caller_->pc()->AddTransceiver(track, {});

  // Setting the offer is not enough to make the outbound-rtp appear.
  auto offer = caller_->AwaitCreateOffer();
  caller_->AwaitSetLocalDescription(offer.get());
  scoped_refptr<const RTCStatsReport> report = GetStats(caller_->pc());
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  EXPECT_THAT(outbound_rtps, SizeIs(0));

  // Once the O/A completes, the outbound-rtp immediately appears (the stats
  // cache is cleared).
  callee_->AwaitSetRemoteDescription(offer.get());
  auto answer = callee_->AwaitCreateAnswer();
  caller_->AwaitSetRemoteDescription(answer.get());
  report = GetStats(caller_->pc());
  outbound_rtps = report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  EXPECT_THAT(outbound_rtps, SizeIs(1));
}

TEST_F(RTCStatsRtpLifetimeTest, VideoOutboundRtpMissingBeforeStable) {
  // Caller to send video.
  scoped_refptr<MediaStreamInterface> stream = caller_->GetUserMedia(
      /*audio=*/false, {}, /*video=*/true);
  scoped_refptr<VideoTrackInterface> track = stream->GetVideoTracks()[0];
  caller_->pc()->AddTransceiver(track, {});

  // Setting the offer is not enough to make the outbound-rtp appear.
  auto offer = caller_->AwaitCreateOffer();
  caller_->AwaitSetLocalDescription(offer.get());
  scoped_refptr<const RTCStatsReport> report = GetStats(caller_->pc());
  std::vector<const RTCOutboundRtpStreamStats*> outbound_rtps =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  EXPECT_THAT(outbound_rtps, SizeIs(0));

  // Once the O/A completes, the outbound-rtp immediately appears (the stats
  // cache is cleared).
  callee_->AwaitSetRemoteDescription(offer.get());
  auto answer = callee_->AwaitCreateAnswer();
  caller_->AwaitSetRemoteDescription(answer.get());
  report = GetStats(caller_->pc());
  outbound_rtps = report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  EXPECT_THAT(outbound_rtps, SizeIs(1));
}

TEST_F(RTCStatsRtpLifetimeTest, AudioInboundRtpMissingBeforeFirstPacket) {
  // Caller to send audio.
  scoped_refptr<MediaStreamInterface> stream = caller_->GetUserMedia(
      /*audio=*/true, {}, /*video=*/false);
  scoped_refptr<AudioTrackInterface> track = stream->GetAudioTracks()[0];
  caller_->pc()->AddTransceiver(track, {});

  caller_->ListenForRemoteIceCandidates(callee_);
  callee_->ListenForRemoteIceCandidates(caller_);
  PeerConnectionTestWrapper::AwaitNegotiation(caller_.get(), callee_.get());

  // The m-section has been negotiated but no inbound-rtp should be present
  // since no packets have been received yet.
  scoped_refptr<const RTCStatsReport> report = GetStats(callee_->pc());
  std::vector<const RTCInboundRtpStreamStats*> inbound_rtps =
      report->GetStatsOfType<RTCInboundRtpStreamStats>();
  EXPECT_THAT(inbound_rtps, SizeIs(0));

  caller_->AwaitAddRemoteIceCandidates();
  callee_->AwaitAddRemoteIceCandidates();

  // Nothing is preventing packets from flowing, wait for inbound-rtp to appear.
  EXPECT_TRUE(WaitUntil(
      [&] {
        report = GetStats(callee_->pc());
        inbound_rtps = report->GetStatsOfType<RTCInboundRtpStreamStats>();
        return !inbound_rtps.empty();
      },
      {.timeout = TimeDelta::Millis(kGetStatsTimeoutMs)}));
  ASSERT_THAT(inbound_rtps, SizeIs(1));
  EXPECT_GT(inbound_rtps[0]->packets_received.value_or(0), 0u);
}

TEST_F(RTCStatsRtpLifetimeTest, VideoInboundRtpMissingBeforeFirstPacket) {
  // Caller to send video.
  scoped_refptr<MediaStreamInterface> stream = caller_->GetUserMedia(
      /*audio=*/false, {}, /*video=*/true);
  scoped_refptr<VideoTrackInterface> track = stream->GetVideoTracks()[0];
  caller_->pc()->AddTransceiver(track, {});

  caller_->ListenForRemoteIceCandidates(callee_);
  callee_->ListenForRemoteIceCandidates(caller_);
  PeerConnectionTestWrapper::AwaitNegotiation(caller_.get(), callee_.get());

  // The m-section has been negotiated but no inbound-rtp should be present
  // since no packets have been received yet.
  scoped_refptr<const RTCStatsReport> report = GetStats(callee_->pc());
  std::vector<const RTCInboundRtpStreamStats*> inbound_rtps =
      report->GetStatsOfType<RTCInboundRtpStreamStats>();
  EXPECT_THAT(inbound_rtps, SizeIs(0));

  caller_->AwaitAddRemoteIceCandidates();
  callee_->AwaitAddRemoteIceCandidates();

  // Nothing is preventing packets from flowing, wait for inbound-rtp to appear.
  EXPECT_TRUE(WaitUntil(
      [&] {
        report = GetStats(callee_->pc());
        inbound_rtps = report->GetStatsOfType<RTCInboundRtpStreamStats>();
        return !inbound_rtps.empty();
      },
      {.timeout = TimeDelta::Millis(kGetStatsTimeoutMs)}));
  ASSERT_THAT(inbound_rtps, SizeIs(1));
  EXPECT_GT(inbound_rtps[0]->packets_received.value_or(0), 0u);
}

TEST_F(RTCStatsRtpLifetimeTest, InboundRtpForEarlyMedia) {
  // Dummy m-section needed for DTLS to be established. Early media is not
  // possible before then. Also exchange ICE candidates.
  RtpTransceiverInit init;
  init.direction = RtpTransceiverDirection::kSendOnly;
  caller_->pc()->AddTransceiver(MediaType::VIDEO, init);
  caller_->ListenForRemoteIceCandidates(callee_);
  callee_->ListenForRemoteIceCandidates(caller_);
  PeerConnectionTestWrapper::AwaitNegotiation(caller_.get(), callee_.get());
  caller_->AwaitAddRemoteIceCandidates();
  callee_->AwaitAddRemoteIceCandidates();

  // In a follow-up exchange, offer to receive.
  init.direction = RtpTransceiverDirection::kRecvOnly;
  caller_->pc()->AddTransceiver(MediaType::VIDEO, init);
  auto offer = caller_->AwaitCreateOffer();
  caller_->AwaitSetLocalDescription(offer.get());
  callee_->AwaitSetRemoteDescription(offer.get());
  // Answer to send.
  scoped_refptr<MediaStreamInterface> stream = callee_->GetUserMedia(
      /*audio=*/false, {}, /*video=*/true);
  scoped_refptr<VideoTrackInterface> track = stream->GetVideoTracks()[0];
  auto transceivers = callee_->pc()->GetTransceivers();
  ASSERT_EQ(transceivers.size(), 2u);
  transceivers[1]->sender()->SetTrack(track.get());
  EXPECT_THAT(transceivers[1]->SetDirectionWithError(
                  RtpTransceiverDirection::kSendOnly),
              IsRtcOk());
  auto answer = callee_->AwaitCreateAnswer();
  callee_->AwaitSetLocalDescription(answer.get());

  // We never set the remote answer...
  ASSERT_EQ(caller_->pc()->signaling_state(),
            PeerConnectionInterface::SignalingState::kHaveLocalOffer);
  // But because of early media, we're still able to receive packets.
  // - Whether or not we unmute the track in response to this is outside the
  //   scope of this stats test.
  scoped_refptr<const RTCStatsReport> report;
  std::vector<const RTCInboundRtpStreamStats*> inbound_rtps;
  EXPECT_TRUE(WaitUntil(
      [&] {
        report = GetStats(caller_->pc());
        inbound_rtps = report->GetStatsOfType<RTCInboundRtpStreamStats>();
        return !inbound_rtps.empty();
      },
      {.timeout = TimeDelta::Millis(kGetStatsTimeoutMs)}));
  ASSERT_THAT(inbound_rtps, SizeIs(1));
  EXPECT_GT(inbound_rtps[0]->packets_received.value_or(0), 0u);
}
#endif  // WEBRTC_HAVE_SCTP

}  // namespace

}  // namespace webrtc
