/*
 *  Copyright 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/test/integration_test_helpers.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio_options.h"
#include "api/candidate.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/crypto/crypto_options.h"
#include "api/data_channel_interface.h"
#include "api/dtls_transport_interface.h"
#include "api/enable_media_with_defaults.h"
#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/payload_type.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtc_event_log/rtc_event_log_factory.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_parameters.h"
#include "api/rtp_receiver_interface.h"
#include "api/rtp_sender_interface.h"
#include "api/rtp_transceiver_direction.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/test/rtc_error_matchers.h"
#include "api/test/time_controller.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/video_rotation.h"
#include "logging/rtc_event_log/fake_rtc_event_log_factory.h"
#include "media/base/codec.h"
#include "media/base/stream_params.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/port.h"
#include "p2p/test/test_turn_customizer.h"
#include "p2p/test/test_turn_server.h"
#include "pc/peer_connection.h"
#include "pc/peer_connection_factory.h"
#include "pc/peer_connection_proxy.h"
#include "pc/session_description.h"
#include "pc/test/fake_audio_capture_module.h"
#include "pc/test/fake_periodic_video_source.h"
#include "pc/test/fake_periodic_video_track_source.h"
#include "pc/test/fake_rtc_certificate_generator.h"
#include "pc/test/fake_video_track_renderer.h"
#include "pc/test/mock_peer_connection_observers.h"
#include "pc/test/rtc_stats_obtainer.h"
#include "rtc_base/checks.h"
#include "rtc_base/containers/flat_map.h"
#include "rtc_base/crypto_random.h"
#include "rtc_base/fake_mdns_responder.h"
#include "rtc_base/fake_network.h"
#include "rtc_base/firewall_socket_server.h"
#include "rtc_base/logging.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/socket_factory.h"
#include "rtc_base/socket_server.h"
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
#include "test/time_controller/simulated_time_controller.h"
#include "test/wait_until.h"

namespace webrtc {

using testing::NotNull;

PeerConnectionInterface::RTCOfferAnswerOptions IceRestartOfferAnswerOptions() {
  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.ice_restart = true;
  return options;
}

void RemoveSsrcsAndMsids(std::unique_ptr<SessionDescriptionInterface>& sdp) {
  for (ContentInfo& content : sdp->description()->contents()) {
    content.media_description()->mutable_streams().clear();
  }
  sdp->description()->set_msid_signaling(0);
}

void RemoveSsrcsAndKeepMsids(
    std::unique_ptr<SessionDescriptionInterface>& sdp) {
  for (ContentInfo& content : sdp->description()->contents()) {
    std::string track_id;
    std::vector<std::string> stream_ids;
    if (!content.media_description()->streams().empty()) {
      const StreamParams& first_stream =
          content.media_description()->streams()[0];
      track_id = first_stream.id;
      stream_ids = first_stream.stream_ids();
    }
    content.media_description()->mutable_streams().clear();
    StreamParams new_stream;
    new_stream.id = track_id;
    new_stream.set_stream_ids(stream_ids);
    content.media_description()->AddStream(new_stream);
  }
}

void SetSdpType(std::unique_ptr<SessionDescriptionInterface>& sdp,
                SdpType sdpType) {
  std::string str;
  sdp->ToString(&str);
  sdp = CreateSessionDescription(sdpType, str);
}

void ReplaceFirstSsrc(StreamParams& stream, uint32_t ssrc) {
  stream.ssrcs[0] = ssrc;
  for (auto& group : stream.ssrc_groups) {
    group.ssrcs[0] = ssrc;
  }
}

int FindFirstMediaStatsIndexByKind(
    const std::string& kind,
    const std::vector<const RTCInboundRtpStreamStats*>& inbound_rtps) {
  for (size_t i = 0; i < inbound_rtps.size(); i++) {
    if (*inbound_rtps[i]->kind == kind) {
      return i;
    }
  }
  return -1;
}

TaskQueueMetronome::TaskQueueMetronome(TimeDelta tick_period)
    : tick_period_(tick_period) {}

TaskQueueMetronome::~TaskQueueMetronome() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
}
void TaskQueueMetronome::RequestCallOnNextTick(
    absl::AnyInvocable<void() &&> callback) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  callbacks_.push_back(std::move(callback));
  // Only schedule a tick callback for the first `callback` addition.
  // Schedule on the current task queue to comply with RequestCallOnNextTick
  // requirements.
  if (callbacks_.size() == 1) {
    TaskQueueBase::Current()->PostDelayedTask(
        SafeTask(safety_.flag(),
                 [this] {
                   RTC_DCHECK_RUN_ON(&sequence_checker_);
                   std::vector<absl::AnyInvocable<void() &&>> callbacks;
                   callbacks.swap(callbacks_);
                   for (auto& callback : callbacks)
                     std::move(callback)();
                 }),
        tick_period_);
  }
}

TimeDelta TaskQueueMetronome::TickPeriod() const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return tick_period_;
}

PeerConnectionIntegrationWrapper::PeerConnectionIntegrationWrapper(
    const std::string& debug_name,
    Environment env,
    internal::PeerConnectionIntegrationTestBase* test)
    : debug_name_(debug_name), env_(env), test_(test) {}

PeerConnection* PeerConnectionIntegrationWrapper::pc_internal() const {
  auto* pci =
      static_cast<PeerConnectionProxyWithInternal<PeerConnectionInterface>*>(
          pc());
  return static_cast<PeerConnection*>(pci->internal());
}

void PeerConnectionIntegrationWrapper::CreateAndSetAndSignalOffer() {
  std::unique_ptr<SessionDescriptionInterface> offer = CreateOfferAndWait();
  ASSERT_NE(nullptr, offer);
  EXPECT_TRUE(SetLocalDescriptionAndSendSdpMessage(std::move(offer)));
}

void PeerConnectionIntegrationWrapper::AddAudioVideoTracks() {
  AddAudioTrack();
  AddVideoTrack();
  ResetRtpSenderObservers();
}

scoped_refptr<AudioTrackInterface>
PeerConnectionIntegrationWrapper::CreateLocalAudioTrack() {
  AudioOptions options;
  // Disable highpass filter so that we can get all the test audio frames.
  options.highpass_filter = false;
  scoped_refptr<AudioSourceInterface> source =
      peer_connection_factory_->CreateAudioSource(options);
  // TODO(perkj): Test audio source when it is implemented. Currently audio
  // always use the default input.
  return peer_connection_factory_->CreateAudioTrack(CreateRandomUuid(),
                                                    source.get());
}

scoped_refptr<VideoTrackInterface>
PeerConnectionIntegrationWrapper::CreateLocalVideoTrack() {
  FakePeriodicVideoSource::Config config;
  config.timestamp_offset = env_.clock().CurrentTime();
  return CreateLocalVideoTrackInternal(config);
}

scoped_refptr<VideoTrackInterface>
PeerConnectionIntegrationWrapper::CreateLocalVideoTrackWithConfig(
    FakePeriodicVideoSource::Config config) {
  return CreateLocalVideoTrackInternal(config);
}

scoped_refptr<VideoTrackInterface>
PeerConnectionIntegrationWrapper::CreateLocalVideoTrackWithRotation(
    VideoRotation rotation) {
  FakePeriodicVideoSource::Config config;
  config.rotation = rotation;
  config.timestamp_offset = env_.clock().CurrentTime();
  return CreateLocalVideoTrackInternal(config);
}

scoped_refptr<RtpSenderInterface> PeerConnectionIntegrationWrapper::AddTrack(
    scoped_refptr<MediaStreamTrackInterface> track,
    const std::vector<std::string>& stream_ids) {
  EXPECT_TRUE(track);
  if (!track) {
    return nullptr;
  }
  auto result = pc()->AddTrack(track, stream_ids);
  EXPECT_EQ(RTCErrorType::NONE, result.error().type());
  if (result.ok()) {
    return result.MoveValue();
  } else {
    return nullptr;
  }
}

std::vector<scoped_refptr<RtpReceiverInterface>>
PeerConnectionIntegrationWrapper::GetReceiversOfType(
    webrtc::MediaType media_type) {
  std::vector<scoped_refptr<RtpReceiverInterface>> receivers;
  for (const auto& receiver : pc()->GetReceivers()) {
    if (receiver->media_type() == media_type) {
      receivers.push_back(receiver);
    }
  }
  return receivers;
}

scoped_refptr<RtpTransceiverInterface>
PeerConnectionIntegrationWrapper::GetFirstTransceiverOfType(
    webrtc::MediaType media_type) {
  for (auto transceiver : pc()->GetTransceivers()) {
    if (transceiver->receiver()->media_type() == media_type) {
      return transceiver;
    }
  }
  return nullptr;
}

bool PeerConnectionIntegrationWrapper::SignalingStateStable() {
  return pc()->signaling_state() == PeerConnectionInterface::kStable;
}

bool PeerConnectionIntegrationWrapper::IceGatheringStateComplete() {
  return pc()->ice_gathering_state() ==
         PeerConnectionInterface::kIceGatheringComplete;
}

void PeerConnectionIntegrationWrapper::CreateDataChannel() {
  CreateDataChannel(nullptr);
}

void PeerConnectionIntegrationWrapper::CreateDataChannel(
    const DataChannelInit* init) {
  CreateDataChannel(kDataChannelLabel, init);
}

void PeerConnectionIntegrationWrapper::CreateDataChannel(
    const std::string& label,
    const DataChannelInit* init) {
  auto data_channel_or_error = pc()->CreateDataChannelOrError(label, init);
  ASSERT_TRUE(data_channel_or_error.ok());
  data_channels_.push_back(data_channel_or_error.MoveValue());
  ASSERT_TRUE(data_channels_.back().get() != nullptr);
  data_observers_.push_back(
      std::make_unique<MockDataChannelObserver>(data_channels_.back().get()));
}

DataChannelInterface* PeerConnectionIntegrationWrapper::data_channel() {
  if (data_channels_.empty()) {
    return nullptr;
  }
  return data_channels_.back().get();
}

MockDataChannelObserver* PeerConnectionIntegrationWrapper::data_observer()
    const {
  if (data_observers_.empty()) {
    return nullptr;
  }
  return data_observers_.back().get();
}

std::unique_ptr<SessionDescriptionInterface>
PeerConnectionIntegrationWrapper::CreateAnswerForTest() {
  return CreateAnswer();
}

int PeerConnectionIntegrationWrapper::audio_frames_received() const {
  return fake_audio_capture_module_->frames_received();
}

int PeerConnectionIntegrationWrapper::min_video_frames_received_per_track()
    const {
  int min_frames = INT_MAX;
  if (fake_video_renderers_.empty()) {
    return 0;
  }

  for (const auto& pair : fake_video_renderers_) {
    min_frames = std::min(min_frames, pair.second->num_rendered_frames());
  }
  return min_frames;
}

scoped_refptr<MockStatsObserver>
PeerConnectionIntegrationWrapper::OldGetStatsForTrack(
    MediaStreamTrackInterface* track) {
  auto observer = make_ref_counted<MockStatsObserver>();
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  EXPECT_TRUE(peer_connection_->GetStats(
      observer.get(), nullptr,
      PeerConnectionInterface::kStatsOutputLevelStandard));
#pragma clang diagnostic pop
  EXPECT_THAT(test_->GetWaiter().Until([&] { return observer->called(); },
                                       ::testing::IsTrue()),
              IsRtcOk());
  return observer;
}

scoped_refptr<MockStatsObserver>
PeerConnectionIntegrationWrapper::OldGetStats() {
  return OldGetStatsForTrack(nullptr);
}

scoped_refptr<const RTCStatsReport>
PeerConnectionIntegrationWrapper::NewGetStats(WaitUntilSettings settings) {
  auto callback = make_ref_counted<MockRTCStatsCollectorCallback>();
  peer_connection_->GetStats(callback.get());
  EXPECT_THAT(test_->GetWaiter(settings).Until(
                  [&] { return callback->called(); }, ::testing::IsTrue()),
              IsRtcOk());
  return callback->report();
}

scoped_refptr<const RTCStatsReport>
PeerConnectionIntegrationWrapper::NewGetStats(test::RunLoop& run_loop) {
  scoped_refptr<const RTCStatsReport> report;
  auto callback = RTCStatsObtainer::Create(&report, run_loop.QuitClosure());
  peer_connection_->GetStats(callback.get());
  run_loop.Run();
  EXPECT_TRUE(report);
  return report;
}

scoped_refptr<const RTCStatsReport>
PeerConnectionIntegrationWrapper::NewGetStats(
    scoped_refptr<RtpReceiverInterface> selector,
    test::RunLoop& run_loop) {
  scoped_refptr<const RTCStatsReport> report;
  auto callback = RTCStatsObtainer::Create(&report, run_loop.QuitClosure());
  peer_connection_->GetStats(std::move(selector), callback);
  run_loop.Run();
  EXPECT_TRUE(report);
  return report;
}

std::string PeerConnectionIntegrationWrapper::DtlsCipher() {
  auto report = NewGetStats();
  if (!report)
    return "";
  auto stats = report->GetStatsOfType<RTCTransportStats>();
  if (stats.empty() || !stats[0]->dtls_cipher.has_value())
    return "";
  return *stats[0]->dtls_cipher;
}

std::string PeerConnectionIntegrationWrapper::SrtpCipher() {
  auto report = NewGetStats();
  if (!report)
    return "";
  auto stats = report->GetStatsOfType<RTCTransportStats>();
  if (stats.empty() || !stats[0]->srtp_cipher.has_value())
    return "";
  return *stats[0]->srtp_cipher;
}

int PeerConnectionIntegrationWrapper::rendered_width() {
  EXPECT_FALSE(fake_video_renderers_.empty());
  return fake_video_renderers_.empty()
             ? 0
             : fake_video_renderers_.begin()->second->width();
}

int PeerConnectionIntegrationWrapper::rendered_height() {
  EXPECT_FALSE(fake_video_renderers_.empty());
  return fake_video_renderers_.empty()
             ? 0
             : fake_video_renderers_.begin()->second->height();
}

double PeerConnectionIntegrationWrapper::rendered_aspect_ratio() {
  if (rendered_height() == 0) {
    return 0.0;
  }
  return static_cast<double>(rendered_width()) / rendered_height();
}

VideoRotation PeerConnectionIntegrationWrapper::rendered_rotation() {
  EXPECT_FALSE(fake_video_renderers_.empty());
  return fake_video_renderers_.empty()
             ? kVideoRotation_0
             : fake_video_renderers_.begin()->second->rotation();
}

int PeerConnectionIntegrationWrapper::local_rendered_width() {
  return local_video_renderer_ ? local_video_renderer_->width() : 0;
}

int PeerConnectionIntegrationWrapper::local_rendered_height() {
  return local_video_renderer_ ? local_video_renderer_->height() : 0;
}

double PeerConnectionIntegrationWrapper::local_rendered_aspect_ratio() {
  if (local_rendered_height() == 0) {
    return 0.0;
  }
  return static_cast<double>(local_rendered_width()) / local_rendered_height();
}

size_t PeerConnectionIntegrationWrapper::number_of_remote_streams() {
  if (!pc()) {
    return 0;
  }
  return pc()->remote_streams()->count();
}

StreamCollectionInterface* PeerConnectionIntegrationWrapper::remote_streams()
    const {
  if (!pc()) {
    ADD_FAILURE();
    return nullptr;
  }
  return pc()->remote_streams().get();
}

StreamCollectionInterface* PeerConnectionIntegrationWrapper::local_streams() {
  if (!pc()) {
    ADD_FAILURE();
    return nullptr;
  }
  return pc()->local_streams().get();
}

PeerConnectionInterface::SignalingState
PeerConnectionIntegrationWrapper::signaling_state() {
  return pc()->signaling_state();
}

PeerConnectionInterface::IceConnectionState
PeerConnectionIntegrationWrapper::ice_connection_state() {
  return pc()->ice_connection_state();
}

PeerConnectionInterface::IceConnectionState
PeerConnectionIntegrationWrapper::standardized_ice_connection_state() {
  return pc()->standardized_ice_connection_state();
}

PeerConnectionInterface::IceGatheringState
PeerConnectionIntegrationWrapper::ice_gathering_state() {
  return pc()->ice_gathering_state();
}

void PeerConnectionIntegrationWrapper::ResetRtpReceiverObservers() {
  for (const scoped_refptr<RtpReceiverInterface>& receiver :
       pc()->GetReceivers()) {
    receiver->SetObserver(nullptr);
  }
  rtp_receiver_observers_.clear();
  for (const scoped_refptr<RtpReceiverInterface>& receiver :
       pc()->GetReceivers()) {
    std::unique_ptr<MockRtpReceiverObserver> observer(
        new MockRtpReceiverObserver(receiver->media_type()));
    receiver->SetObserver(observer.get());
    rtp_receiver_observers_.push_back(std::move(observer));
  }
}

void PeerConnectionIntegrationWrapper::ResetRtpSenderObservers() {
  rtp_sender_observers_.clear();
  for (const scoped_refptr<RtpSenderInterface>& sender : pc()->GetSenders()) {
    std::unique_ptr<MockRtpSenderObserver> observer(
        new MockRtpSenderObserver(sender->media_type()));
    sender->SetObserver(observer.get());
    rtp_sender_observers_.push_back(std::move(observer));
  }
}

Candidate PeerConnectionIntegrationWrapper::last_candidate_gathered() const {
  if (last_gathered_ice_candidate_) {
    return last_gathered_ice_candidate_->candidate();
  }
  return Candidate();
}

void PeerConnectionIntegrationWrapper::SetMdnsResponder(
    std::unique_ptr<FakeMdnsResponder> mdns_responder) {
  RTC_DCHECK(mdns_responder != nullptr);
  mdns_responder_ = mdns_responder.get();
  network_manager()->set_mdns_responder(std::move(mdns_responder));
}

std::unique_ptr<SessionDescriptionInterface>
PeerConnectionIntegrationWrapper::CreateOfferAndWait() {
  auto observer = make_ref_counted<MockCreateSessionDescriptionObserver>();
  pc()->CreateOffer(observer.get(), offer_answer_options_);
  EXPECT_TRUE(test_->GetWaiter().Until([&] { return observer->called(); }));
  if (!observer->result()) {
    return nullptr;
  }
  auto description = observer->MoveDescription();
  if (generated_sdp_munger_) {
    generated_sdp_munger_(description);
  }
  return description;
}

bool PeerConnectionIntegrationWrapper::Rollback() {
  return SetRemoteDescription(CreateRollbackSessionDescription());
}

void PeerConnectionIntegrationWrapper::StartWatchingDelayStats() {
  // Get the baseline numbers for audio_packets and audio_delay.
  auto received_stats = NewGetStats();
  ASSERT_THAT(received_stats, NotNull());
  auto inbound_stats =
      received_stats->GetStatsOfType<RTCInboundRtpStreamStats>();
  ASSERT_FALSE(inbound_stats.empty());
  auto rtp_stats = inbound_stats[0];
  ASSERT_TRUE(rtp_stats->relative_packet_arrival_delay.has_value());
  ASSERT_TRUE(rtp_stats->packets_received.has_value());
  rtp_stats_id_ = rtp_stats->id();
  audio_packets_stat_ = *rtp_stats->packets_received;
  audio_delay_stat_ = *rtp_stats->relative_packet_arrival_delay;
  audio_samples_stat_ = *rtp_stats->total_samples_received;
  audio_concealed_stat_ = *rtp_stats->concealed_samples;
  initial_audio_samples_stat_ = audio_samples_stat_;
  initial_audio_concealed_stat_ = audio_concealed_stat_;
}

void PeerConnectionIntegrationWrapper::UpdateDelayStats(std::string tag,
                                                        int desc_size) {
  auto report = NewGetStats();
  auto rtp_stats = report->GetAs<RTCInboundRtpStreamStats>(rtp_stats_id_);
  ASSERT_TRUE(rtp_stats);
  auto delta_packets = *rtp_stats->packets_received - audio_packets_stat_;
  auto delta_rpad =
      *rtp_stats->relative_packet_arrival_delay - audio_delay_stat_;
  auto recent_delay = delta_packets > 0 ? delta_rpad / delta_packets : -1;
  // The purpose of these checks is to sound the alarm early if we introduce
  // serious regressions. The numbers are not acceptable for production, but
  // occur on slow bots.
  //
  // An average relative packet arrival delay over the renegotiation of
  // > 100 ms indicates that something is dramatically wrong, and will impact
  // quality for sure.
  // Worst bots:
  // linux_x86_dbg at 0.206
#if !defined(NDEBUG)
  EXPECT_GT(0.25, recent_delay) << tag << " size " << desc_size;
#else
  EXPECT_GT(0.1, recent_delay) << tag << " size " << desc_size;
#endif
  auto delta_samples = *rtp_stats->total_samples_received - audio_samples_stat_;
  auto delta_concealed = *rtp_stats->concealed_samples - audio_concealed_stat_;
  // These limits should be adjusted down as we improve:
  //
  // Concealing more than 4000 samples during a renegotiation is unacceptable.
  // But some bots are slow.

  // Worst bots:
  // linux_more_configs bot at conceal count 5184
  // android_arm_rel at conceal count 9241
  // linux_x86_dbg at 15174
#if !defined(NDEBUG)
  EXPECT_GT(18000U, delta_concealed) << "Concealed " << delta_concealed
                                     << " of " << delta_samples << " samples";
#else
  EXPECT_GT(15000U, delta_concealed) << "Concealed " << delta_concealed
                                     << " of " << delta_samples << " samples";
#endif
  // Concealing more than 20% of samples during a renegotiation is
  // unacceptable.
  // Worst bots:
  // Nondebug: Linux32 Release at conceal rate 0.606597 (CI run)
  // Debug: linux_x86_dbg bot at conceal rate 0.854
  //        internal bot at conceal rate 0.967 (b/294020344)
  // TODO(https://crbug.com/webrtc/15393): Improve audio quality during
  // renegotiation so that we can reduce these thresholds, 99% is not even
  // close to the 20% deemed unacceptable above or the 0% that would be ideal.
  // Require at least 2000 samples (roughly 2x 20ms packets) across
  // renegotiation steps.
  auto total_samples =
      *rtp_stats->total_samples_received - initial_audio_samples_stat_;
  auto total_concealed =
      *rtp_stats->concealed_samples - initial_audio_concealed_stat_;
  if (total_samples >= 2000) {
    audio_delay_stats_percentage_checked_ = true;
#if !defined(NDEBUG)
    EXPECT_LT(1.0 * total_concealed / total_samples, 0.99)
        << "Concealed " << total_concealed << " of " << total_samples
        << " samples";
#else
    EXPECT_LT(1.0 * total_concealed / total_samples, 0.7)
        << "Concealed " << total_concealed << " of " << total_samples
        << " samples";
#endif
  }
  // Increment trailing counters
  audio_packets_stat_ = *rtp_stats->packets_received;
  audio_delay_stat_ = *rtp_stats->relative_packet_arrival_delay;
  audio_samples_stat_ = *rtp_stats->total_samples_received;
  audio_concealed_stat_ = *rtp_stats->concealed_samples;
}

bool PeerConnectionIntegrationWrapper::SetRemoteDescription(
    std::unique_ptr<SessionDescriptionInterface> desc) {
  auto observer = make_ref_counted<FakeSetRemoteDescriptionObserver>();
  std::string sdp;
  EXPECT_TRUE(desc->ToString(&sdp));
  RTC_LOG(LS_INFO) << debug_name_
                   << ": SetRemoteDescription SDP: type=" << desc->GetType()
                   << " contents=\n"
                   << sdp;
  pc()->SetRemoteDescription(std::move(desc), observer);
  RemoveUnusedVideoRenderers();
  EXPECT_THAT(test_->GetWaiter().Until([&] { return observer->called(); },
                                       ::testing::IsTrue()),
              IsRtcOk());
  auto err = observer->error();
  if (!err.ok()) {
    RTC_LOG(LS_WARNING) << debug_name_ << " SetRemoteDescription: " << err;
  }
  return observer->error().ok();
}

void PeerConnectionIntegrationWrapper::NegotiateCorruptionDetectionHeader() {
  for (const auto& transceiver : pc()->GetTransceivers()) {
    if (transceiver->media_type() != webrtc::MediaType::VIDEO) {
      continue;
    }
    auto extensions = transceiver->GetHeaderExtensionsToNegotiate();
    for (auto& extension : extensions) {
      if (extension.uri == RtpExtension::kCorruptionDetectionUri) {
        extension.direction = RtpTransceiverDirection::kSendRecv;
      }
    }
    transceiver->SetHeaderExtensionsToNegotiate(extensions);
  }
}

uint32_t PeerConnectionIntegrationWrapper::GetCorruptionScoreCount() {
  scoped_refptr<const RTCStatsReport> report = NewGetStats();
  auto inbound_stream_stats =
      report->GetStatsOfType<RTCInboundRtpStreamStats>();
  for (const auto& stat : inbound_stream_stats) {
    if (*stat->kind == "video") {
      return stat->corruption_measurements.value_or(0);
    }
  }
  return 0;
}

uint32_t PeerConnectionIntegrationWrapper::GetReceivedFrameCount() {
  scoped_refptr<const RTCStatsReport> report = NewGetStats();
  auto inbound_stream_stats =
      report->GetStatsOfType<RTCInboundRtpStreamStats>();
  for (const auto& stat : inbound_stream_stats) {
    if (*stat->kind == "video") {
      return stat->frames_received.value_or(0);
    }
  }
  return 0;
}

std::optional<int> PeerConnectionIntegrationWrapper::tls_version() {
  return dtls_transport_information().tls_version();
}

std::optional<DtlsTransportTlsRole>
PeerConnectionIntegrationWrapper::dtls_transport_role() {
  return dtls_transport_information().role();
}

DtlsTransportInformation
PeerConnectionIntegrationWrapper::dtls_transport_information() {
  return network_thread_->BlockingCall([&] {
    return pc()->GetSctpTransport()->dtls_transport()->Information();
  });
}

bool PeerConnectionIntegrationWrapper::SetLocalDescriptionAndSendSdpMessage(
    std::unique_ptr<SessionDescriptionInterface> desc) {
  auto observer = make_ref_counted<MockSetSessionDescriptionObserver>();
  RTC_LOG(LS_INFO) << debug_name_ << ": SetLocalDescriptionAndSendSdpMessage";
  SdpType type = desc->GetType();
  std::string sdp;
  EXPECT_TRUE(desc->ToString(&sdp));
  RTC_LOG(LS_INFO) << debug_name_ << ": local SDP type=" << desc->GetType()
                   << " contents=\n"
                   << sdp;
  pc()->SetLocalDescription(observer.get(), desc.release());
  // Note - many tests depend on the SDP message being sent before
  // SetLocalDescription returns.
  SendSdpMessage(type, sdp);
  EXPECT_THAT(test_->GetWaiter().Until([&] { return observer->called(); },
                                       ::testing::IsTrue()),
              IsRtcOk());
  return observer->result();
}

bool PeerConnectionIntegrationWrapper::Init(
    const PeerConnectionFactory::Options* options,
    const PeerConnectionInterface::RTCConfiguration* config,
    PeerConnectionDependencies dependencies,
    SocketServer* socket_server,
    Thread* network_thread,
    std::unique_ptr<FakeRtcEventLogFactory> event_log_factory,
    bool reset_encoder_factory,
    bool reset_decoder_factory,
    bool create_media_engine) {
  // There's an error in this test code if Init ends up being called twice.
  RTC_DCHECK(!peer_connection_);
  RTC_DCHECK(!peer_connection_factory_);

  auto network_manager = std::make_unique<FakeNetworkManager>(network_thread);
  fake_network_manager_ = network_manager.get();
  fake_network_manager_->AddInterface(kDefaultLocalAddress);

  network_thread_ = network_thread;

  fake_audio_capture_module_ =
      FakeAudioCaptureModule::Create(test_->CreateThread("AudioCaptureThread"));

  if (!fake_audio_capture_module_) {
    return false;
  }
  Thread* const signaling_thread = Thread::Current();

  PeerConnectionFactoryDependencies pc_factory_dependencies;
  pc_factory_dependencies.network_thread = network_thread;
  pc_factory_dependencies.signaling_thread = signaling_thread;
  pc_factory_dependencies.socket_factory = socket_server;
  pc_factory_dependencies.network_manager = std::move(network_manager);
  pc_factory_dependencies.env = env_;
  pc_factory_dependencies.decode_metronome =
      std::make_unique<TaskQueueMetronome>(TimeDelta::Millis(8));

  pc_factory_dependencies.adm = fake_audio_capture_module_;
  if (create_media_engine) {
    // Standard creation method for APM may return a null pointer when
    // AudioProcessing is disabled with a build flag. Bypass that flag by
    // explicitly injecting the factory.
    pc_factory_dependencies.audio_processing_builder =
        std::make_unique<BuiltinAudioProcessingBuilder>();
    EnableMediaWithDefaults(pc_factory_dependencies);
  }

  if (reset_encoder_factory) {
    pc_factory_dependencies.video_encoder_factory.reset();
  }
  if (reset_decoder_factory) {
    pc_factory_dependencies.video_decoder_factory.reset();
  }

  if (event_log_factory) {
    event_log_factory_ = event_log_factory.get();
    pc_factory_dependencies.event_log_factory = std::move(event_log_factory);
  } else {
    pc_factory_dependencies.event_log_factory =
        std::make_unique<RtcEventLogFactory>();
  }
  peer_connection_factory_ =
      CreateModularPeerConnectionFactory(std::move(pc_factory_dependencies));

  if (!peer_connection_factory_) {
    fake_network_manager_ = nullptr;
    return false;
  }
  if (options) {
    peer_connection_factory_->SetOptions(*options);
  }
  if (config) {
    sdp_semantics_ = config->sdp_semantics;
  }

  peer_connection_ = CreatePeerConnection(config, std::move(dependencies));
  return peer_connection_.get() != nullptr;
}

scoped_refptr<PeerConnectionInterface>
PeerConnectionIntegrationWrapper::CreatePeerConnection(
    const PeerConnectionInterface::RTCConfiguration* config,
    PeerConnectionDependencies dependencies) {
  PeerConnectionInterface::RTCConfiguration modified_config;
  modified_config.sdp_semantics = sdp_semantics_;
  // If `config` is null, this will result in a default configuration being
  // used.
  if (config) {
    modified_config = *config;
  }
  // Disable resolution adaptation; we don't want it interfering with the
  // test results.
  // TODO(deadbeef): Do something more robust. Since we're testing for aspect
  // ratios and not specific resolutions, is this even necessary?
  modified_config.set_cpu_adaptation(false);

  dependencies.observer = this;
  auto peer_connection_or_error =
      peer_connection_factory_->CreatePeerConnectionOrError(
          modified_config, std::move(dependencies));
  return peer_connection_or_error.ok() ? peer_connection_or_error.MoveValue()
                                       : nullptr;
}

scoped_refptr<VideoTrackInterface>
PeerConnectionIntegrationWrapper::CreateLocalVideoTrackInternal(
    FakePeriodicVideoSource::Config config) {
  // Set max frame rate to 10fps to reduce the risk of test flakiness.
  // TODO(deadbeef): Do something more robust.
  config.frame_interval = TimeDelta::Millis(100);

  video_track_sources_.emplace_back(
      make_ref_counted<FakePeriodicVideoTrackSource>(config,
                                                     false /* remote */));
  scoped_refptr<VideoTrackInterface> track =
      peer_connection_factory_->CreateVideoTrack(video_track_sources_.back(),
                                                 CreateRandomUuid());
  if (!local_video_renderer_) {
    local_video_renderer_.reset(new FakeVideoTrackRenderer(track.get()));
  }
  return track;
}

void PeerConnectionIntegrationWrapper::HandleIncomingOffer(
    const std::string& msg) {
  RTC_LOG(LS_INFO) << debug_name_ << ": HandleIncomingOffer";
  std::unique_ptr<SessionDescriptionInterface> desc =
      CreateSessionDescription(SdpType::kOffer, msg);
  if (received_sdp_munger_) {
    received_sdp_munger_(desc);
  }

  EXPECT_TRUE(SetRemoteDescription(std::move(desc)));
  // Setting a remote description may have changed the number of receivers,
  // so reset the receiver observers.
  ResetRtpReceiverObservers();
  if (remote_offer_handler_) {
    remote_offer_handler_();
  }
  std::unique_ptr<SessionDescriptionInterface> answer = CreateAnswer();
  ASSERT_NE(nullptr, answer);
  EXPECT_TRUE(SetLocalDescriptionAndSendSdpMessage(std::move(answer)));
}

void PeerConnectionIntegrationWrapper::HandleIncomingAnswer(
    SdpType type,
    const std::string& msg) {
  RTC_LOG(LS_INFO) << debug_name_ << ": HandleIncomingAnswer of type " << type;
  std::unique_ptr<SessionDescriptionInterface> desc =
      CreateSessionDescription(type, msg);
  if (received_sdp_munger_) {
    received_sdp_munger_(desc);
    if (!desc) {
      // Answer was "taken" by munger...so that it can be applied later ?
      RTC_LOG(LS_INFO) << debug_name_ << ": answer NOT applied";
      return;
    }
  }
  EXPECT_TRUE(SetRemoteDescription(std::move(desc)));
  // Set the RtpReceiverObserver after receivers are created.
  ResetRtpReceiverObservers();
}

std::unique_ptr<SessionDescriptionInterface>
PeerConnectionIntegrationWrapper::CreateAnswer() {
  auto observer = make_ref_counted<MockCreateSessionDescriptionObserver>();
  pc()->CreateAnswer(observer.get(), offer_answer_options_);
  EXPECT_THAT(test_->GetWaiter().Until([&] { return observer->called(); },
                                       ::testing::IsTrue()),
              IsRtcOk());
  if (!observer->result()) {
    return nullptr;
  }
  auto description = observer->MoveDescription();
  if (generated_sdp_munger_) {
    generated_sdp_munger_(description);
  }
  return description;
}

void PeerConnectionIntegrationWrapper::RemoveUnusedVideoRenderers() {
  if (sdp_semantics_ != SdpSemantics::kUnifiedPlan) {
    return;
  }
  auto transceivers = pc()->GetTransceivers();
  std::set<std::string> active_renderers;
  for (auto& transceiver : transceivers) {
    // Note - we don't check for direction here. This function is called
    // before direction is set, and in that case, we should not remove
    // the renderer.
    if (transceiver->receiver()->media_type() == webrtc::MediaType::VIDEO) {
      active_renderers.insert(transceiver->receiver()->track()->id());
    }
  }
  for (auto it = fake_video_renderers_.begin();
       it != fake_video_renderers_.end();) {
    // Remove fake video renderers belonging to any non-active transceivers.
    if (!active_renderers.count(it->first)) {
      it = fake_video_renderers_.erase(it);
    } else {
      it++;
    }
  }
}

void PeerConnectionIntegrationWrapper::SendSdpMessage(SdpType type,
                                                      const std::string& msg) {
  if (signaling_delay_ms_ == 0) {
    RelaySdpMessageIfReceiverExists(type, msg);
  } else {
    Thread::Current()->PostDelayedTask(
        SafeTask(
            task_safety_.flag(),
            [this, type, msg] { RelaySdpMessageIfReceiverExists(type, msg); }),
        TimeDelta::Millis(signaling_delay_ms_));
  }
}

void PeerConnectionIntegrationWrapper::RelaySdpMessageIfReceiverExists(
    SdpType type,
    const std::string& msg) {
  if (signaling_message_receiver_) {
    signaling_message_receiver_->ReceiveSdpMessage(type, msg);
  }
}

void PeerConnectionIntegrationWrapper::SendIceMessage(
    const std::string& sdp_mid,
    int sdp_mline_index,
    const std::string& msg) {
  if (signaling_delay_ms_ == 0) {
    RelayIceMessageIfReceiverExists(sdp_mid, sdp_mline_index, msg);
  } else {
    Thread::Current()->PostDelayedTask(
        SafeTask(task_safety_.flag(),
                 [this, sdp_mid, sdp_mline_index, msg] {
                   RelayIceMessageIfReceiverExists(sdp_mid, sdp_mline_index,
                                                   msg);
                 }),
        TimeDelta::Millis(signaling_delay_ms_));
  }
}

void PeerConnectionIntegrationWrapper::RelayIceMessageIfReceiverExists(
    const std::string& sdp_mid,
    int sdp_mline_index,
    const std::string& msg) {
  if (signaling_message_receiver_) {
    signaling_message_receiver_->ReceiveIceMessage(sdp_mid, sdp_mline_index,
                                                   msg);
  }
}

void PeerConnectionIntegrationWrapper::ReceiveSdpMessage(
    SdpType type,
    const std::string& msg) {
  if (type == SdpType::kOffer) {
    HandleIncomingOffer(msg);
  } else {
    HandleIncomingAnswer(type, msg);
  }
}

void PeerConnectionIntegrationWrapper::ReceiveIceMessage(
    const std::string& sdp_mid,
    int sdp_mline_index,
    const std::string& msg) {
  RTC_LOG(LS_INFO) << debug_name_ << ": ReceiveIceMessage";
  std::optional<RTCError> result;
  pc()->AddIceCandidate(absl::WrapUnique(CreateIceCandidate(
                            sdp_mid, sdp_mline_index, msg, nullptr)),
                        [&result](RTCError r) { result = r; });
  EXPECT_THAT(test_->GetWaiter().Until([&] { return result.has_value(); },
                                       ::testing::IsTrue()),
              IsRtcOk());
  EXPECT_TRUE(result.value().ok());
}

void PeerConnectionIntegrationWrapper::OnSignalingChange(
    PeerConnectionInterface::SignalingState new_state) {
  EXPECT_EQ(pc()->signaling_state(), new_state);
  peer_connection_signaling_state_history_.push_back(new_state);
}

void PeerConnectionIntegrationWrapper::OnAddTrack(
    scoped_refptr<RtpReceiverInterface> receiver,
    const std::vector<scoped_refptr<MediaStreamInterface>>& streams) {
  if (receiver->media_type() == webrtc::MediaType::VIDEO) {
    scoped_refptr<VideoTrackInterface> video_track(
        static_cast<VideoTrackInterface*>(receiver->track().get()));
    ASSERT_TRUE(fake_video_renderers_.find(video_track->id()) ==
                fake_video_renderers_.end());
    fake_video_renderers_[video_track->id()] =
        std::make_unique<FakeVideoTrackRenderer>(video_track.get());
  }
}

void PeerConnectionIntegrationWrapper::OnRemoveTrack(
    scoped_refptr<RtpReceiverInterface> receiver) {
  if (receiver->media_type() == webrtc::MediaType::VIDEO) {
    auto it = fake_video_renderers_.find(receiver->track()->id());
    if (it != fake_video_renderers_.end()) {
      fake_video_renderers_.erase(it);
    } else {
      RTC_LOG(LS_ERROR) << "OnRemoveTrack called for non-active renderer";
    }
  }
}

void PeerConnectionIntegrationWrapper::OnRenegotiationNeeded() {}

void PeerConnectionIntegrationWrapper::OnIceConnectionChange(
    PeerConnectionInterface::IceConnectionState new_state) {
  EXPECT_EQ(pc()->ice_connection_state(), new_state);
  ice_connection_state_history_.push_back(new_state);
}

void PeerConnectionIntegrationWrapper::OnStandardizedIceConnectionChange(
    PeerConnectionInterface::IceConnectionState new_state) {
  standardized_ice_connection_state_history_.push_back(new_state);
}

void PeerConnectionIntegrationWrapper::OnConnectionChange(
    PeerConnectionInterface::PeerConnectionState new_state) {
  peer_connection_state_history_.push_back(new_state);
  if (connection_change_callback_) {
    connection_change_callback_(new_state);
  }
}

void PeerConnectionIntegrationWrapper::OnIceGatheringChange(
    PeerConnectionInterface::IceGatheringState new_state) {
  EXPECT_EQ(pc()->ice_gathering_state(), new_state);
  ice_gathering_state_history_.push_back(new_state);
}

void PeerConnectionIntegrationWrapper::OnIceCandidate(
    const IceCandidate* candidate) {
  RTC_LOG(LS_INFO) << debug_name_ << ": OnIceCandidate";

  if (remote_async_dns_resolver_) {
    const auto& local_candidate = candidate->candidate();
    if (local_candidate.address().IsUnresolvedIP()) {
      RTC_DCHECK(local_candidate.is_local());
      const auto resolved_ip = mdns_responder_->GetMappedAddressForName(
          local_candidate.address().hostname());
      RTC_DCHECK(!resolved_ip.IsNil());
      remote_async_dns_resolved_addr_ = local_candidate.address();
      remote_async_dns_resolved_addr_.SetResolvedIP(resolved_ip);
      EXPECT_CALL(*remote_async_dns_resolver_, Start(_, _))
          .WillOnce([](const SocketAddress& addr,
                       absl::AnyInvocable<void()> callback) { callback(); });
      EXPECT_CALL(*remote_async_dns_resolver_, result())
          .WillOnce(ReturnRef(remote_async_dns_resolver_result_));
      EXPECT_CALL(remote_async_dns_resolver_result_, GetResolvedAddress(_, _))
          .WillOnce([this](int /*family*/, SocketAddress* addr) {
            *addr = remote_async_dns_resolved_addr_;
            return true;
          });
    }
  }

  // Check if we expected to have a candidate.
  EXPECT_GT(candidates_expected_, 1);
  candidates_expected_--;
  std::string ice_sdp = candidate->ToString();
  if (signaling_message_receiver_ == nullptr || !signal_ice_candidates_) {
    // Remote party may be deleted.
    return;
  }
  SendIceMessage(candidate->sdp_mid(), candidate->sdp_mline_index(), ice_sdp);
  last_gathered_ice_candidate_ =
      CreateIceCandidate(candidate->sdp_mid(), candidate->sdp_mline_index(),
                         candidate->candidate());
}

void PeerConnectionIntegrationWrapper::OnIceCandidateError(
    const std::string& address,
    int port,
    const std::string& url,
    int error_code,
    const std::string& error_text) {
  error_event_ =
      IceCandidateErrorEvent(address, port, url, error_code, error_text);
}

void PeerConnectionIntegrationWrapper::OnDataChannel(
    scoped_refptr<DataChannelInterface> data_channel) {
  RTC_LOG(LS_INFO) << debug_name_ << ": OnDataChannel";
  data_channels_.push_back(data_channel);
  data_observers_.push_back(
      std::make_unique<MockDataChannelObserver>(data_channel.get()));
}

bool PeerConnectionIntegrationWrapper::IdExists(
    const RtpHeaderExtensions& extensions,
    RtpHeaderExtensionId id) {
  for (const auto& extension : extensions) {
    if (extension.id == id) {
      return true;
    }
  }
  return false;
}

namespace internal {

PeerConnectionIntegrationTestBase::PeerConnectionIntegrationTestBase(
    Environment env,
    SdpSemantics sdp_semantics)
    : sdp_semantics_(sdp_semantics),
      env_(std::move(env)),
      ss_(new VirtualSocketServer()),
      fss_(new FirewallSocketServer(ss_.get(), nullptr, false)),
      network_thread_(new Thread(fss_.get())) {
  network_thread_->SetName("PCNetworkThread", this);
  RTC_CHECK(network_thread_->Start());
  metrics::Reset();
}

PeerConnectionIntegrationTestBase::PeerConnectionIntegrationTestBase(
    Environment env,
    SdpSemantics sdp_semantics,
    TimeController* time_controller)
    : sdp_semantics_(sdp_semantics), env_(std::move(env)) {
  ss_ = std::make_unique<VirtualSocketServer>();
  fss_ = std::make_unique<FirewallSocketServer>(ss_.get(), nullptr, false);
  network_thread_ = time_controller->CreateThreadWithSocketServer(
      "PCNetworkThread", fss_.get());
  network_thread_->SetName("PCNetworkThread", this);
  metrics::Reset();
}

PeerConnectionIntegrationTestBase::~PeerConnectionIntegrationTestBase() {
  // The PeerConnections should be deleted before the TurnCustomizers.
  // A TurnPort is created with a raw pointer to a TurnCustomizer. The
  // TurnPort has the same lifetime as the PeerConnection, so it's expected
  // that the TurnCustomizer outlives the life of the PeerConnection or else
  // when Send() is called it will hit a seg fault.
  DestroyPeerConnections();
}

bool PeerConnectionIntegrationTestBase::DtlsConnected() {
  // TODO(deadbeef): kIceConnectionConnected currently means both ICE and DTLS
  // are connected. This is an important distinction. Once we have separate
  // ICE and DTLS state, this check needs to use the DTLS state.
  return (callee()->ice_connection_state() ==
              PeerConnectionInterface::kIceConnectionConnected ||
          callee()->ice_connection_state() ==
              PeerConnectionInterface::kIceConnectionCompleted) &&
         (caller()->ice_connection_state() ==
              PeerConnectionInterface::kIceConnectionConnected ||
          caller()->ice_connection_state() ==
              PeerConnectionInterface::kIceConnectionCompleted);
}

void PeerConnectionIntegrationTestBase::SetFieldTrials(
    absl::string_view field_trials) {
  RTC_CHECK(caller_ == nullptr);
  RTC_CHECK(callee_ == nullptr);
  field_trials_ = std::string(field_trials);
}

void PeerConnectionIntegrationTestBase::SetFieldTrials(
    absl::string_view debug_name,
    absl::string_view field_trials) {
  RTC_CHECK(caller_ == nullptr);
  RTC_CHECK(callee_ == nullptr);
  field_trials_overrides_[std::string(debug_name)] = field_trials;
}

std::unique_ptr<PeerConnectionIntegrationWrapper>
PeerConnectionIntegrationTestBase::CreatePeerConnectionWrapper(
    const std::string& debug_name,
    const PeerConnectionFactory::Options* options,
    const RTCConfiguration* config,
    PeerConnectionDependencies dependencies,
    std::unique_ptr<FakeRtcEventLogFactory> event_log_factory,
    bool reset_encoder_factory,
    bool reset_decoder_factory,
    bool create_media_engine) {
  RTCConfiguration modified_config;
  if (config) {
    modified_config = *config;
  }
  modified_config.sdp_semantics = sdp_semantics_;
  if (!dependencies.cert_generator) {
    dependencies.cert_generator =
        std::make_unique<FakeRTCCertificateGenerator>();
  }
  std::string field_trials = field_trials_;
  EnvironmentFactory env = EnvironmentFactory(env_);

  auto it = field_trials_overrides_.find(debug_name);
  if (it != field_trials_overrides_.end()) {
    field_trials = it->second;
    dependencies.trials = CreateTestFieldTrialsPtr(it->second);
  }
  env.Set(CreateTestFieldTrialsPtr(field_trials));

  std::unique_ptr<PeerConnectionIntegrationWrapper> client =
      CreatePeerConnectionWrapperInternal(debug_name, env.Create());

  if (!client->Init(options, &modified_config, std::move(dependencies),
                    fss_.get(), network_thread_.get(),
                    std::move(event_log_factory), reset_encoder_factory,
                    reset_decoder_factory, create_media_engine)) {
    return nullptr;
  }
  return client;
}

std::unique_ptr<PeerConnectionIntegrationWrapper>
PeerConnectionIntegrationTestBase::
    CreatePeerConnectionWrapperWithFakeRtcEventLog(
        const std::string& debug_name,
        const PeerConnectionFactory::Options* options,
        const RTCConfiguration* config,
        PeerConnectionDependencies dependencies) {
  return CreatePeerConnectionWrapper(debug_name, options, config,
                                     std::move(dependencies),
                                     std::make_unique<FakeRtcEventLogFactory>(),
                                     /*reset_encoder_factory=*/false,
                                     /*reset_decoder_factory=*/false);
}

bool PeerConnectionIntegrationTestBase::
    CreatePeerConnectionWrappersWithSdpSemantics(
        SdpSemantics caller_semantics,
        SdpSemantics callee_semantics) {
  // Can't specify the sdp_semantics in the passed-in configuration since it
  // will be overwritten by CreatePeerConnectionWrapper with whatever is
  // stored in sdp_semantics_. So get around this by modifying the instance
  // variable before calling CreatePeerConnectionWrapper for the caller and
  // callee PeerConnections.
  SdpSemantics original_semantics = sdp_semantics_;
  sdp_semantics_ = caller_semantics;
  caller_ =
      CreatePeerConnectionWrapper(kCallerName, nullptr, nullptr,
                                  PeerConnectionDependencies(nullptr), nullptr,
                                  /*reset_encoder_factory=*/false,
                                  /*reset_decoder_factory=*/false);
  sdp_semantics_ = callee_semantics;
  callee_ =
      CreatePeerConnectionWrapper(kCalleeName, nullptr, nullptr,
                                  PeerConnectionDependencies(nullptr), nullptr,
                                  /*reset_encoder_factory=*/false,
                                  /*reset_decoder_factory=*/false);
  sdp_semantics_ = original_semantics;
  return caller_ && callee_;
}

bool PeerConnectionIntegrationTestBase::CreatePeerConnectionWrappersWithConfig(
    const PeerConnectionInterface::RTCConfiguration& caller_config,
    const PeerConnectionInterface::RTCConfiguration& callee_config,
    bool create_media_engine) {
  caller_ = CreatePeerConnectionWrapper(
      kCallerName, nullptr, &caller_config, PeerConnectionDependencies(nullptr),
      nullptr,
      /*reset_encoder_factory=*/false,
      /*reset_decoder_factory=*/false, create_media_engine);
  callee_ = CreatePeerConnectionWrapper(
      kCalleeName, nullptr, &callee_config, PeerConnectionDependencies(nullptr),
      nullptr,
      /*reset_encoder_factory=*/false,
      /*reset_decoder_factory=*/false, create_media_engine);
  return caller_ && callee_;
}

bool PeerConnectionIntegrationTestBase::
    CreatePeerConnectionWrappersWithConfigAndDeps(
        const PeerConnectionInterface::RTCConfiguration& caller_config,
        PeerConnectionDependencies caller_dependencies,
        const PeerConnectionInterface::RTCConfiguration& callee_config,
        PeerConnectionDependencies callee_dependencies) {
  caller_ = CreatePeerConnectionWrapper(kCallerName, nullptr, &caller_config,
                                        std::move(caller_dependencies), nullptr,
                                        /*reset_encoder_factory=*/false,
                                        /*reset_decoder_factory=*/false);
  callee_ = CreatePeerConnectionWrapper(kCalleeName, nullptr, &callee_config,
                                        std::move(callee_dependencies), nullptr,
                                        /*reset_encoder_factory=*/false,
                                        /*reset_decoder_factory=*/false);
  return caller_ && callee_;
}

bool PeerConnectionIntegrationTestBase::CreatePeerConnectionWrappersWithOptions(
    const PeerConnectionFactory::Options& caller_options,
    const PeerConnectionFactory::Options& callee_options) {
  caller_ =
      CreatePeerConnectionWrapper(kCallerName, &caller_options, nullptr,
                                  PeerConnectionDependencies(nullptr), nullptr,
                                  /*reset_encoder_factory=*/false,
                                  /*reset_decoder_factory=*/false);
  callee_ =
      CreatePeerConnectionWrapper(kCalleeName, &callee_options, nullptr,
                                  PeerConnectionDependencies(nullptr), nullptr,
                                  /*reset_encoder_factory=*/false,
                                  /*reset_decoder_factory=*/false);
  return caller_ && callee_;
}

bool PeerConnectionIntegrationTestBase::
    CreatePeerConnectionWrappersWithFakeRtcEventLog() {
  PeerConnectionInterface::RTCConfiguration default_config;
  caller_ = CreatePeerConnectionWrapperWithFakeRtcEventLog(
      kCallerName, nullptr, &default_config,
      PeerConnectionDependencies(nullptr));
  callee_ = CreatePeerConnectionWrapperWithFakeRtcEventLog(
      kCalleeName, nullptr, &default_config,
      PeerConnectionDependencies(nullptr));
  return caller_ && callee_;
}

std::unique_ptr<PeerConnectionIntegrationWrapper>
PeerConnectionIntegrationTestBase::
    CreatePeerConnectionWrapperWithAlternateKey() {
  std::unique_ptr<FakeRTCCertificateGenerator> cert_generator(
      new FakeRTCCertificateGenerator());
  cert_generator->use_alternate_key();

  PeerConnectionDependencies dependencies(nullptr);
  dependencies.cert_generator = std::move(cert_generator);
  return CreatePeerConnectionWrapper("New Peer", nullptr, nullptr,
                                     std::move(dependencies), nullptr,
                                     /*reset_encoder_factory=*/false,
                                     /*reset_decoder_factory=*/false);
}

bool PeerConnectionIntegrationTestBase::
    CreateOneDirectionalPeerConnectionWrappers(bool caller_to_callee) {
  caller_ =
      CreatePeerConnectionWrapper(kCallerName, nullptr, nullptr,
                                  PeerConnectionDependencies(nullptr), nullptr,
                                  /*reset_encoder_factory=*/!caller_to_callee,
                                  /*reset_decoder_factory=*/caller_to_callee);
  callee_ =
      CreatePeerConnectionWrapper(kCalleeName, nullptr, nullptr,
                                  PeerConnectionDependencies(nullptr), nullptr,
                                  /*reset_encoder_factory=*/caller_to_callee,
                                  /*reset_decoder_factory=*/!caller_to_callee);
  return caller_ && callee_;
}

bool PeerConnectionIntegrationTestBase::
    CreatePeerConnectionWrappersWithoutMediaEngine() {
  caller_ =
      CreatePeerConnectionWrapper(kCallerName, nullptr, nullptr,
                                  PeerConnectionDependencies(nullptr), nullptr,
                                  /*reset_encoder_factory=*/false,
                                  /*reset_decoder_factory=*/false,
                                  /*create_media_engine=*/false);
  callee_ =
      CreatePeerConnectionWrapper(kCalleeName, nullptr, nullptr,
                                  PeerConnectionDependencies(nullptr), nullptr,
                                  /*reset_encoder_factory=*/false,
                                  /*reset_decoder_factory=*/false,
                                  /*create_media_engine=*/false);
  return caller_ && callee_;
}

TestTurnServer* PeerConnectionIntegrationTestBase::CreateTurnServer(
    SocketAddress internal_address,
    SocketAddress external_address,
    ProtocolType type,
    const std::string& common_name) {
  Thread* thread = network_thread();
  SocketFactory* socket_factory = fss_.get();
  std::unique_ptr<TestTurnServer> turn_server;
  SendTask(network_thread(), [&] {
    turn_server = std::make_unique<TestTurnServer>(
        CreateTestEnvironment(), thread, socket_factory, internal_address,
        external_address, type,
        /*ignore_bad_certs=*/true, common_name);
  });
  turn_servers_.push_back(std::move(turn_server));
  // Interactions with the turn server should be done on the network thread.
  return turn_servers_.back().get();
}

TestTurnCustomizer* PeerConnectionIntegrationTestBase::CreateTurnCustomizer() {
  std::unique_ptr<TestTurnCustomizer> turn_customizer;
  SendTask(network_thread(),
           [&] { turn_customizer = std::make_unique<TestTurnCustomizer>(); });
  turn_customizers_.push_back(std::move(turn_customizer));
  // Interactions with the turn customizer should be done on the network
  // thread.
  return turn_customizers_.back().get();
}

void PeerConnectionIntegrationTestBase::ExpectTurnCustomizerCountersIncremented(
    TestTurnCustomizer* turn_customizer) {
  SendTask(network_thread(), [turn_customizer] {
    EXPECT_GT(turn_customizer->allow_channel_data_cnt_, 0u);
    EXPECT_GT(turn_customizer->modify_cnt_, 0u);
  });
}

void PeerConnectionIntegrationTestBase::ConnectFakeSignaling() {
  caller_->set_signaling_message_receiver(callee_.get());
  callee_->set_signaling_message_receiver(caller_.get());
}

void PeerConnectionIntegrationTestBase::ConnectFakeSignalingForSdpOnly() {
  ConnectFakeSignaling();
  SetSignalIceCandidates(false);
}

void PeerConnectionIntegrationTestBase::SetSignalingDelayMs(int delay_ms) {
  caller_->set_signaling_delay_ms(delay_ms);
  callee_->set_signaling_delay_ms(delay_ms);
}

void PeerConnectionIntegrationTestBase::SetSignalIceCandidates(bool signal) {
  caller_->set_signal_ice_candidates(signal);
  callee_->set_signal_ice_candidates(signal);
}

void PeerConnectionIntegrationTestBase::SendRtpDataWithRetries(
    DataChannelInterface* dc,
    const std::string& data,
    int retries) {
  for (int i = 0; i < retries; ++i) {
    dc->Send(DataBuffer(data));
  }
}

void PeerConnectionIntegrationTestBase::DestroyTurnServers() {
  ExecuteTask(*network_thread(), [this] {
    turn_servers_.clear();
    turn_customizers_.clear();
  });
}

void PeerConnectionIntegrationTestBase::DestroyThreads() {
  network_thread_.reset();
}

void PeerConnectionIntegrationTestBase::DestroyPeerConnections() {
  if (caller_) {
    caller_->set_signaling_message_receiver(nullptr);
    caller_->pc()->Close();
  }
  if (callee_) {
    callee_->set_signaling_message_receiver(nullptr);
    callee_->pc()->Close();
  }
  caller_.reset();
  callee_.reset();
}

PeerConnectionIntegrationWrapper*
PeerConnectionIntegrationTestBase::SetCallerPcWrapperAndReturnCurrent(
    std::unique_ptr<PeerConnectionIntegrationWrapper> wrapper) {
  PeerConnectionIntegrationWrapper* old = caller_.release();
  caller_ = std::move(wrapper);
  return old;
}

PeerConnectionIntegrationWrapper*
PeerConnectionIntegrationTestBase::SetCalleePcWrapperAndReturnCurrent(
    std::unique_ptr<PeerConnectionIntegrationWrapper> wrapper) {
  PeerConnectionIntegrationWrapper* old = callee_.release();
  callee_ = std::move(wrapper);
  return old;
}

bool PeerConnectionIntegrationTestBase::ExpectNewFrames(
    const MediaExpectations& media_expectations) {
  // Make sure there are no bogus tracks confusing the issue.
  caller()->RemoveUnusedVideoRenderers();
  callee()->RemoveUnusedVideoRenderers();
  // First initialize the expected frame counts based upon the current
  // frame count.
  int total_caller_audio_frames_expected = caller()->audio_frames_received();
  if (media_expectations.caller_audio_expectation_ ==
      MediaExpectations::kExpectSomeFrames) {
    total_caller_audio_frames_expected +=
        media_expectations.caller_audio_frames_expected_;
  }
  int total_caller_video_frames_expected =
      caller()->min_video_frames_received_per_track();
  if (media_expectations.caller_video_expectation_ ==
      MediaExpectations::kExpectSomeFrames) {
    total_caller_video_frames_expected +=
        media_expectations.caller_video_frames_expected_;
  }
  int total_callee_audio_frames_expected = callee()->audio_frames_received();
  if (media_expectations.callee_audio_expectation_ ==
      MediaExpectations::kExpectSomeFrames) {
    total_callee_audio_frames_expected +=
        media_expectations.callee_audio_frames_expected_;
  }
  int total_callee_video_frames_expected =
      callee()->min_video_frames_received_per_track();
  if (media_expectations.callee_video_expectation_ ==
      MediaExpectations::kExpectSomeFrames) {
    total_callee_video_frames_expected +=
        media_expectations.callee_video_frames_expected_;
  }

  // Wait for the expected frames.
  EXPECT_THAT(
      GetWaiter({.timeout = kMaxWaitForFrames})
          .Until(
              [&] {
                return caller()->audio_frames_received() >=
                           total_caller_audio_frames_expected &&
                       caller()->min_video_frames_received_per_track() >=
                           total_caller_video_frames_expected &&
                       callee()->audio_frames_received() >=
                           total_callee_audio_frames_expected &&
                       callee()->min_video_frames_received_per_track() >=
                           total_callee_video_frames_expected;
              },
              ::testing::IsTrue()),
      IsRtcOk());
  bool expectations_correct =
      caller()->audio_frames_received() >= total_caller_audio_frames_expected &&
      caller()->min_video_frames_received_per_track() >=
          total_caller_video_frames_expected &&
      callee()->audio_frames_received() >= total_callee_audio_frames_expected &&
      callee()->min_video_frames_received_per_track() >=
          total_callee_video_frames_expected;

  // After the combined wait, print out a more detailed message upon
  // failure.
  EXPECT_GE(caller()->audio_frames_received(),
            total_caller_audio_frames_expected);
  EXPECT_GE(caller()->min_video_frames_received_per_track(),
            total_caller_video_frames_expected);
  EXPECT_GE(callee()->audio_frames_received(),
            total_callee_audio_frames_expected);
  EXPECT_GE(callee()->min_video_frames_received_per_track(),
            total_callee_video_frames_expected);

  // We want to make sure nothing unexpected was received.
  if (media_expectations.caller_audio_expectation_ ==
      MediaExpectations::kExpectNoFrames) {
    EXPECT_EQ(caller()->audio_frames_received(),
              total_caller_audio_frames_expected);
    if (caller()->audio_frames_received() !=
        total_caller_audio_frames_expected) {
      expectations_correct = false;
    }
  }
  if (media_expectations.caller_video_expectation_ ==
      MediaExpectations::kExpectNoFrames) {
    EXPECT_EQ(caller()->min_video_frames_received_per_track(),
              total_caller_video_frames_expected);
    if (caller()->min_video_frames_received_per_track() !=
        total_caller_video_frames_expected) {
      expectations_correct = false;
    }
  }
  if (media_expectations.callee_audio_expectation_ ==
      MediaExpectations::kExpectNoFrames) {
    EXPECT_EQ(callee()->audio_frames_received(),
              total_callee_audio_frames_expected);
    if (callee()->audio_frames_received() !=
        total_callee_audio_frames_expected) {
      expectations_correct = false;
    }
  }
  if (media_expectations.callee_video_expectation_ ==
      MediaExpectations::kExpectNoFrames) {
    EXPECT_EQ(callee()->min_video_frames_received_per_track(),
              total_callee_video_frames_expected);
    if (callee()->min_video_frames_received_per_track() !=
        total_callee_video_frames_expected) {
      expectations_correct = false;
    }
  }
  return expectations_correct;
}

void PeerConnectionIntegrationTestBase::ClosePeerConnections() {
  if (caller())
    caller()->pc()->Close();
  if (callee())
    callee()->pc()->Close();
}

void PeerConnectionIntegrationTestBase::TestNegotiatedCipherSuite(
    const RTCConfiguration& caller_config,
    const RTCConfiguration& callee_config,
    int expected_cipher_suite) {
  ASSERT_TRUE(
      CreatePeerConnectionWrappersWithConfig(caller_config, callee_config));
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  WaitUntilSettings settings;
  settings.polling_interval = TimeDelta::Millis(100);
  if (auto* tc = time_controller()) {
    settings.clock = tc;
  }
  ASSERT_THAT(GetWaiter(settings).Until([&] { return DtlsConnected(); },
                                        ::testing::IsTrue()),
              IsRtcOk());
  EXPECT_THAT(GetWaiter(settings).Until(
                  [&] {
                    auto report = caller()->NewGetStats(settings);
                    if (!report) {
                      return false;
                    }
                    auto transport_stats =
                        report->GetStatsOfType<RTCTransportStats>();
                    if (transport_stats.empty()) {
                      return false;
                    }
                    return *transport_stats[0]->srtp_cipher ==
                           SrtpCryptoSuiteToName(expected_cipher_suite);
                  },
                  ::testing::IsTrue()),
              IsRtcOk());
}

void PeerConnectionIntegrationTestBase::TestGcmNegotiationUsesCipherSuite(
    bool local_gcm_enabled,
    bool remote_gcm_enabled,
    bool aes_ctr_enabled,
    int expected_cipher_suite) {
  RTCConfiguration caller_config;
  CryptoOptions caller_crypto;
  caller_crypto.srtp.enable_gcm_crypto_suites = local_gcm_enabled;
  caller_crypto.srtp.enable_aes128_sha1_80_crypto_cipher = aes_ctr_enabled;
  caller_config.crypto_options = caller_crypto;
  RTCConfiguration callee_config;
  CryptoOptions callee_crypto;
  callee_crypto.srtp.enable_gcm_crypto_suites = remote_gcm_enabled;
  callee_crypto.srtp.enable_aes128_sha1_80_crypto_cipher = aes_ctr_enabled;
  callee_config.crypto_options = callee_crypto;
  TestNegotiatedCipherSuite(caller_config, callee_config,
                            expected_cipher_suite);
}

std::unique_ptr<PeerConnectionIntegrationWrapper>
PeerConnectionIntegrationTestBase::CreatePeerConnectionWrapperInternal(
    const std::string& debug_name,
    Environment env) {
  return std::unique_ptr<PeerConnectionIntegrationWrapper>(
      new PeerConnectionIntegrationWrapper(debug_name, env, this));
}

}  // namespace internal

PeerConnectionIntegrationBaseTest::PeerConnectionIntegrationBaseTest(
    SdpSemantics sdp_semantics)
    : internal::PeerConnectionIntegrationTestBase(CreateTestEnvironment(),
                                                  sdp_semantics) {}

PeerConnectionIntegrationBaseTest::~PeerConnectionIntegrationBaseTest() {
  DestroyPeerConnections();
  DestroyTurnServers();
}

void PeerConnectionIntegrationBaseTest::ExecuteTask(
    TaskQueueBase& task_queue,
    absl::AnyInvocable<void()> task) {
  task_queue.PostTask([this, task = std::move(task)]() mutable {
    task();
    run_loop_.task_queue()->PostTask(run_loop_.QuitClosure());
  });
  run_loop_.Run();
}

Waiter PeerConnectionIntegrationBaseTest::GetWaiter(
    WaitUntilSettings overrides) {
  return Waiter(overrides);
}

std::unique_ptr<Thread> PeerConnectionIntegrationBaseTest::CreateThread(
    absl::string_view name) {
  auto thread = Thread::Create();
  thread->Start();
  return thread;
}

PeerConnectionIntegrationTestWithSimulatedTime::
    PeerConnectionIntegrationTestWithSimulatedTime(SdpSemantics sdp_semantics)
    : PeerConnectionIntegrationTestWithSimulatedTime(
          sdp_semantics,
          std::make_unique<GlobalSimulatedTimeController>(
              Timestamp::Seconds(1000))) {}

PeerConnectionIntegrationTestWithSimulatedTime::
    ~PeerConnectionIntegrationTestWithSimulatedTime() {
  DestroyPeerConnections();
  DestroyTurnServers();
  time_controller_->AdvanceTime(TimeDelta::Zero());
  // Explicitly destroy threads before time_controller_ is destroyed.
  // Simulated threads hold a pointer to the time controller and will
  // attempt to unregister from it in their destructor. Since time_controller_
  // is owned by this derived class, it will be destroyed BEFORE the base
  // class destroys the threads, causing a crash if we don't do this here.
  DestroyThreads();
}

void PeerConnectionIntegrationTestWithSimulatedTime::ExecuteTask(
    TaskQueueBase& task_queue,
    absl::AnyInvocable<void()> task) {
  task_queue.PostTask(std::move(task));
  time_controller_->AdvanceTime(TimeDelta::Zero());
}

Waiter PeerConnectionIntegrationTestWithSimulatedTime::GetWaiter(
    WaitUntilSettings overrides) {
  overrides.clock = time_controller_.get();
  return Waiter(overrides);
}

std::unique_ptr<Thread>
PeerConnectionIntegrationTestWithSimulatedTime::CreateThread(
    absl::string_view name) {
  return time_controller_->CreateThread(std::string(name), nullptr);
}

PeerConnectionIntegrationTestWithSimulatedTime::
    PeerConnectionIntegrationTestWithSimulatedTime(
        SdpSemantics sdp_semantics,
        std::unique_ptr<GlobalSimulatedTimeController> time_controller)
    : internal::PeerConnectionIntegrationTestBase(
          [](TimeController* tc) {
            CreateTestEnvironmentOptions options;
            options.time = tc;
            return CreateTestEnvironment(std::move(options));
          }(time_controller.get()),
          sdp_semantics,
          time_controller.get()),
      time_controller_(std::move(time_controller)) {}

// Tests whether a parameter set contains duplicate payload types.
// Copied from sdp_offer_answer.cc
RTCError FindDuplicateCodecParameters(
    const RtpCodecParameters codec_parameters,
    flat_map<PayloadType, RtpCodecParameters>& payload_to_codec_parameters) {
  auto existing_codec_parameters =
      payload_to_codec_parameters.find(codec_parameters.payload_type);
  if (existing_codec_parameters != payload_to_codec_parameters.end() &&
      codec_parameters != existing_codec_parameters->second) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                         << "A BUNDLE group contains a codec collision for "
                         << "payload_type='" << codec_parameters.payload_type
                         << "'. All codecs must share the same type, "
                         << "encoding name, clock rate and parameters.");
  }
  payload_to_codec_parameters.try_emplace(codec_parameters.payload_type,
                                          codec_parameters);
  return RTCError::OK();
}

// Tests whether a session description contains conflicting descriptions
// for a payload type.
// Copied from sdp_offer_answer.cc
RTCError ValidateBundledPayloadTypes(const SessionDescription& description) {
  // https://www.rfc-editor.org/rfc/rfc8843#name-payload-type-pt-value-reuse
  // ... all codecs associated with the payload type number MUST share an
  // identical codec configuration. This means that the codecs MUST share
  // the same media type, encoding name, clock rate, and any parameter
  // that can affect the codec configuration and packetization.
  std::vector<const ContentGroup*> bundle_groups =
      description.GetGroupsByName(GROUP_TYPE_BUNDLE);
  for (const ContentGroup* bundle_group : bundle_groups) {
    flat_map<PayloadType, RtpCodecParameters> payload_to_codec_parameters;
    for (const std::string& content_name : bundle_group->content_names()) {
      const ContentInfo* content_description =
          description.GetContentByName(content_name);
      if (content_description == nullptr) {
        return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                             << "A BUNDLE group contains a MID='"
                             << content_name << "' matching no m= section.");
      }
      const MediaContentDescription* media_description =
          content_description->media_description();
      RTC_DCHECK(media_description);
      if (content_description->rejected || !media_description->has_codecs()) {
        continue;
      }
      const MediaType type = media_description->type();
      if (type == MediaType::AUDIO || type == MediaType::VIDEO) {
        for (const Codec& c : media_description->codecs()) {
          RTCError error = FindDuplicateCodecParameters(
              c.ToCodecParameters(), payload_to_codec_parameters);
          if (!error.ok()) {
            return error;
          }
        }
      }
    }
  }
  return RTCError::OK();
}

}  // namespace webrtc
