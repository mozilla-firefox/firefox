/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "audio/audio_receive_stream.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/audio/audio_frame.h"
#include "api/audio/audio_mixer.h"
#include "api/audio_codecs/audio_format.h"
#include "api/call/audio_sink.h"
#include "api/crypto/frame_decryptor_interface.h"
#include "api/environment/environment.h"
#include "api/frame_transformer_interface.h"
#include "api/neteq/neteq_factory.h"
#include "api/rtp_headers.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "audio/audio_send_stream.h"
#include "audio/audio_state.h"
#include "audio/channel_receive.h"
#include "audio/conversion.h"
#include "call/audio_state.h"
#include "call/rtp_config.h"
#include "call/rtp_stream_receiver_controller_interface.h"
#include "call/syncable.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/time_utils.h"
#include "system_wrappers/include/ntp_time.h"

namespace webrtc {

std::string AudioReceiveStreamInterface::Config::Rtp::ToString() const {
  StringBuilder ss;
  ss << "{remote_ssrc: " << remote_ssrc;
  ss << ", nack: " << nack.ToString();
  ss << ", rtcp: "
     << (rtcp_mode == RtcpMode::kCompound
             ? "compound"
             : (rtcp_mode == RtcpMode::kReducedSize ? "reducedSize" : "off"));
  ss << ", rtcp_event_observer: "
     << (rtcp_event_observer ? "(rtcp_event_observer)" : "nullptr");
  ss << '}';
  return ss.Release();
}

std::string AudioReceiveStreamInterface::Config::ToString() const {
  StringBuilder ss;
  ss << "{rtp: " << rtp.ToString();
  ss << ", rtcp_send_transport: "
     << (rtcp_send_transport ? "(Transport)" : "null");
  if (!sync_group.empty()) {
    ss << ", sync_group: " << sync_group;
  }
  ss << '}';
  return ss.Release();
}

namespace {
std::unique_ptr<voe::ChannelReceiveInterface> CreateChannelReceive(
    const Environment& env,
    AudioState* audio_state,
    NetEqFactory* neteq_factory,
    AudioReceiveStreamInterface::Config& config,
    PacketRouter* packet_router) {
  RTC_DCHECK(audio_state != nullptr);
  internal::AudioState* internal_audio_state =
      static_cast<internal::AudioState*>(audio_state);
  return voe::CreateChannelReceive(
      env, neteq_factory, internal_audio_state->audio_device_module(),
      config.rtcp_send_transport, config.rtp.remote_ssrc,
      config.jitter_buffer_max_packets, config.jitter_buffer_fast_accelerate,
      config.jitter_buffer_min_delay_ms, config.enable_non_sender_rtt,
      config.decoder_factory, std::move(config.frame_decryptor),
      config.crypto_options, std::move(config.frame_transformer),
      std::move(config.on_first_packet), config.rtp.rtcp_event_observer,
      packet_router, config.rtp.local_ssrc,
      std::move(config.on_frame_delivered_callback));
}
}  // namespace

AudioReceiveStreamImpl::AudioReceiveStreamImpl(
    const Environment& env,
    PacketRouter* absl_nonnull packet_router,
    NetEqFactory* absl_nullable neteq_factory,
    AudioReceiveStreamInterface::Config config,
    const scoped_refptr<AudioState>& audio_state)
    : env_(env),
      config_(std::move(config)),
      audio_state_(audio_state),
      channel_receive_(CreateChannelReceive(env,
                                            audio_state.get(),
                                            neteq_factory,
                                            config_,
                                            packet_router)) {
  Initialize();
}

AudioReceiveStreamImpl::AudioReceiveStreamImpl(
    const Environment& env,
    AudioReceiveStreamInterface::Config config,
    const scoped_refptr<AudioState>& audio_state,
    absl_nonnull std::unique_ptr<voe::ChannelReceiveInterface> channel_receive)
    : env_(env),
      config_(std::move(config)),
      audio_state_(audio_state),
      channel_receive_(std::move(channel_receive)) {
  Initialize();
}

void AudioReceiveStreamImpl::Initialize() {
  RTC_LOG(LS_INFO) << "AudioReceiveStreamImpl: " << config_.rtp.remote_ssrc;
  RTC_DCHECK(config_.decoder_factory != nullptr);
  RTC_DCHECK(config_.rtcp_send_transport != nullptr);
  RTC_DCHECK(audio_state_ != nullptr);
  RTC_DCHECK(channel_receive_ != nullptr);
  RTC_DCHECK_EQ(config_.rtp.remote_ssrc, channel_receive_->remote_ssrc());

  // Complete configuration.
  // TODO(solenberg): Config NACK history window (which is a packet count),
  // using the actual packet size for the configured codec.
  channel_receive_->SetNACKStatus(config_.rtp.nack.rtp_history_ms != 0,
                                  config_.rtp.nack.rtp_history_ms / 20);
  channel_receive_->SetRtcpMode(config_.rtp.rtcp_mode);
  channel_receive_->SetReceiveCodecs(config_.decoder_map);
  // `frame_transformer` and `frame_decryptor` have been given to
  // `channel_receive_` already.
}

AudioReceiveStreamImpl::~AudioReceiveStreamImpl() {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  RTC_LOG(LS_INFO) << "~AudioReceiveStreamImpl: " << remote_ssrc();
  Stop();
}

void AudioReceiveStreamImpl::RegisterWithTransport(
    RtpStreamReceiverControllerInterface* receiver_controller) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  RTC_DCHECK(!rtp_stream_receiver_);
  rtp_stream_receiver_ = receiver_controller->CreateReceiver(
      remote_ssrc(), channel_receive_.get());
}

void AudioReceiveStreamImpl::UnregisterFromTransport() {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  rtp_stream_receiver_.reset();
}

void AudioReceiveStreamImpl::Start() {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  if (playing_) {
    return;
  }
  RTC_LOG(LS_INFO) << "AudioReceiveStreamImpl::Start: " << remote_ssrc();
  channel_receive_->StartPlayout();
  playing_ = true;
  audio_state()->AddReceivingStream(this);
}

void AudioReceiveStreamImpl::Stop() {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  if (!playing_) {
    return;
  }
  RTC_LOG(LS_INFO) << "AudioReceiveStreamImpl::Stop: " << remote_ssrc();
  channel_receive_->StopPlayout();
  playing_ = false;
  audio_state()->RemoveReceivingStream(this);
}

bool AudioReceiveStreamImpl::IsRunning() const {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  return playing_;
}

void AudioReceiveStreamImpl::SetDepacketizerToDecoderFrameTransformer(
    scoped_refptr<FrameTransformerInterface> frame_transformer) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  channel_receive_->SetDepacketizerToDecoderFrameTransformer(
      std::move(frame_transformer));
}

void AudioReceiveStreamImpl::SetDecoderMap(
    std::map<int, SdpAudioFormat> decoder_map) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  config_.decoder_map = std::move(decoder_map);
  channel_receive_->SetReceiveCodecs(config_.decoder_map);
}

void AudioReceiveStreamImpl::SetNackHistory(int history_ms) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  RTC_DCHECK_GE(history_ms, 0);

  if (config_.rtp.nack.rtp_history_ms == history_ms)
    return;

  config_.rtp.nack.rtp_history_ms = history_ms;
  // TODO(solenberg): Config NACK history window (which is a packet count),
  // using the actual packet size for the configured codec.
  channel_receive_->SetNACKStatus(history_ms != 0, history_ms / 20);
}

void AudioReceiveStreamImpl::SetRtcpMode(RtcpMode mode) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);

  if (config_.rtp.rtcp_mode == mode)
    return;

  config_.rtp.rtcp_mode = mode;
  channel_receive_->SetRtcpMode(mode);
}

void AudioReceiveStreamImpl::SetNonSenderRttMeasurement(bool enabled) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  config_.enable_non_sender_rtt = enabled;
  channel_receive_->SetNonSenderRttMeasurement(enabled);
}

void AudioReceiveStreamImpl::SetFrameDecryptor(
    scoped_refptr<FrameDecryptorInterface> frame_decryptor) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  channel_receive_->SetFrameDecryptor(std::move(frame_decryptor));
}

AudioReceiveStreamInterface::Stats AudioReceiveStreamImpl::GetStats(
    bool get_and_clear_legacy_stats) const {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  AudioReceiveStreamInterface::Stats stats;
  stats.remote_ssrc = remote_ssrc();

  auto receive_codec = channel_receive_->GetReceiveCodec();
  if (receive_codec) {
    stats.codec_name = receive_codec->second.name;
    stats.codec_payload_type = receive_codec->first;
  }

  ChannelReceiveStatistics channel_stats =
      channel_receive_->GetRTCPStatistics();
  stats.payload_bytes_received = channel_stats.payload_bytes_received;
  stats.header_and_padding_bytes_received =
      channel_stats.header_and_padding_bytes_received;
  stats.packets_received = channel_stats.packets_received;
  stats.packets_received_with_ect1 = channel_stats.packets_received_with_ect1;
  stats.packets_received_with_ce = channel_stats.packets_received_with_ce;
  stats.packets_lost = channel_stats.packets_lost;
  stats.jitter_ms = channel_stats.jitter_ms;
  stats.nacks_sent = channel_stats.nacks_sent;
  stats.capture_start_ntp_time_ms = channel_stats.capture_start_ntp_time_ms;
  stats.last_packet_received = channel_stats.last_packet_received;
  stats.last_sender_report_timestamp =
      channel_stats.last_sender_report_timestamp;
  stats.last_sender_report_utc_timestamp =
      channel_stats.last_sender_report_utc_timestamp;
  stats.last_sender_report_remote_utc_timestamp =
      channel_stats.last_sender_report_remote_utc_timestamp;
  stats.sender_reports_packets_sent = channel_stats.sender_reports_packets_sent;
  stats.sender_reports_bytes_sent = channel_stats.sender_reports_bytes_sent;
  stats.sender_reports_reports_count =
      channel_stats.sender_reports_reports_count;
  stats.round_trip_time = channel_stats.round_trip_time;
  stats.round_trip_time_measurements =
      channel_stats.round_trip_time_measurements;
  stats.total_round_trip_time = channel_stats.total_round_trip_time;

  stats.delay_estimate_ms = channel_receive_->GetDelayEstimate();
  stats.audio_level = channel_receive_->GetSpeechOutputLevelFullRange();
  stats.total_output_energy = channel_receive_->GetTotalOutputEnergy();
  stats.total_output_duration = channel_receive_->GetTotalOutputDuration();
  stats.estimated_playout_ntp_timestamp_ms =
      channel_receive_->GetCurrentEstimatedPlayoutNtpTimestampMs(
          env_.clock().TimeInMilliseconds());

  // Get jitter buffer and total delay (alg + jitter + playout) stats.
  auto ns = channel_receive_->GetNetworkStatistics(get_and_clear_legacy_stats);
  stats.packets_discarded = ns.packetsDiscarded;
  stats.fec_packets_received = ns.fecPacketsReceived;
  stats.fec_packets_discarded = ns.fecPacketsDiscarded;
  stats.jitter_buffer_ms = ns.currentBufferSize;
  stats.jitter_buffer_preferred_ms = ns.preferredBufferSize;
  stats.total_samples_received = ns.totalSamplesReceived;
  stats.concealed_samples = ns.concealedSamples;
  stats.silent_concealed_samples = ns.silentConcealedSamples;
  stats.concealment_events = ns.concealmentEvents;
  stats.jitter_buffer_delay_seconds =
      static_cast<double>(ns.jitterBufferDelayMs) /
      static_cast<double>(kNumMillisecsPerSec);
  stats.jitter_buffer_emitted_count = ns.jitterBufferEmittedCount;
  stats.jitter_buffer_target_delay_seconds =
      static_cast<double>(ns.jitterBufferTargetDelayMs) /
      static_cast<double>(kNumMillisecsPerSec);
  stats.jitter_buffer_minimum_delay_seconds =
      static_cast<double>(ns.jitterBufferMinimumDelayMs) /
      static_cast<double>(kNumMillisecsPerSec);
  stats.inserted_samples_for_deceleration = ns.insertedSamplesForDeceleration;
  stats.removed_samples_for_acceleration = ns.removedSamplesForAcceleration;
  stats.total_processing_delay_seconds =
      static_cast<double>(ns.totalProcessingDelayUs) /
      static_cast<double>(kNumMicrosecsPerSec);
  stats.expand_rate = Q14ToFloat(ns.currentExpandRate);
  stats.speech_expand_rate = Q14ToFloat(ns.currentSpeechExpandRate);
  stats.secondary_decoded_rate = Q14ToFloat(ns.currentSecondaryDecodedRate);
  stats.secondary_discarded_rate = Q14ToFloat(ns.currentSecondaryDiscardedRate);
  stats.accelerate_rate = Q14ToFloat(ns.currentAccelerateRate);
  stats.preemptive_expand_rate = Q14ToFloat(ns.currentPreemptiveRate);
  stats.jitter_buffer_flushes = ns.packetBufferFlushes;
  stats.delayed_packet_outage_samples = ns.delayedPacketOutageSamples;
  stats.relative_packet_arrival_delay_seconds =
      static_cast<double>(ns.relativePacketArrivalDelayMs) /
      static_cast<double>(kNumMillisecsPerSec);
  stats.interruption_count = ns.interruptionCount;
  stats.total_interruption_duration_ms = ns.totalInterruptionDurationMs;

  auto ds = channel_receive_->GetDecodingCallStatistics();
  stats.decoding_calls_to_silence_generator = ds.calls_to_silence_generator;
  stats.decoding_calls_to_neteq = ds.calls_to_neteq;
  stats.decoding_normal = ds.decoded_normal;
  stats.decoding_plc = ds.decoded_neteq_plc;
  stats.decoding_codec_plc = ds.decoded_codec_plc;
  stats.decoding_cng = ds.decoded_cng;
  stats.decoding_plc_cng = ds.decoded_plc_cng;
  stats.decoding_muted_output = ds.decoded_muted_output;

  return stats;
}

void AudioReceiveStreamImpl::SetSink(AudioSinkInterface* sink) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  channel_receive_->SetSink(sink);
}

void AudioReceiveStreamImpl::SetGain(float gain) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  channel_receive_->SetChannelOutputVolumeScaling(gain);
}

void AudioReceiveStreamImpl::SetJitterBufferMaxPackets(size_t max_packets) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  channel_receive_->SetMaximumBufferPackets(max_packets);
}

void AudioReceiveStreamImpl::SetJitterBufferFastAccelerate(
    bool fast_accelerate) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  channel_receive_->SetFastAccelerate(fast_accelerate);
}

bool AudioReceiveStreamImpl::SetBaseMinimumPlayoutDelayMs(int delay_ms) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  return channel_receive_->SetBaseMinimumPlayoutDelayMs(delay_ms);
}

int AudioReceiveStreamImpl::GetBaseMinimumPlayoutDelayMs() const {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  return channel_receive_->GetBaseMinimumPlayoutDelayMs();
}

AudioMixer::Source::AudioFrameInfo
AudioReceiveStreamImpl::GetAudioFrameWithInfo(int sample_rate_hz,
                                              AudioFrame* audio_frame) {
  return channel_receive_->GetAudioFrameWithInfo(sample_rate_hz, audio_frame);
}

int AudioReceiveStreamImpl::Ssrc() const {
  return remote_ssrc();
}

int AudioReceiveStreamImpl::PreferredSampleRate() const {
  return channel_receive_->PreferredSampleRate();
}

uint32_t AudioReceiveStreamImpl::id() const {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  return remote_ssrc();
}

std::optional<Syncable::Info> AudioReceiveStreamImpl::GetInfo() const {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  return channel_receive_->GetSyncInfo();
}

std::optional<Syncable::PlayoutInfo>
AudioReceiveStreamImpl::GetPlayoutRtpTimestamp() const {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  return channel_receive_->GetPlayoutRtpTimestamp();
}

void AudioReceiveStreamImpl::SetEstimatedPlayoutNtpTimestamp(NtpTime ntp_time,
                                                             Timestamp time) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  channel_receive_->SetEstimatedPlayoutNtpTimestamp(ntp_time, time);
}

bool AudioReceiveStreamImpl::SetMinimumPlayoutDelay(TimeDelta delay) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  return channel_receive_->SetMinimumPlayoutDelay(delay);
}

void AudioReceiveStreamImpl::DeliverRtcp(std::span<const uint8_t> packet) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  channel_receive_->ReceivedRTCPPacket(packet.data(), packet.size());
}

void AudioReceiveStreamImpl::SetSyncGroup(absl::string_view sync_group) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  config_.sync_group = std::string(sync_group);
}

const std::string& AudioReceiveStreamImpl::sync_group() const {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  return config_.sync_group;
}

uint32_t AudioReceiveStreamImpl::remote_ssrc() const {
  return channel_receive_->remote_ssrc();
}

internal::AudioState* AudioReceiveStreamImpl::audio_state() const {
  auto* audio_state = static_cast<internal::AudioState*>(audio_state_.get());
  RTC_DCHECK(audio_state);
  return audio_state;
}
}  // namespace webrtc
