/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "audio/channel_receive.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "api/audio/audio_device.h"
#include "api/audio/audio_mixer.h"
#include "api/audio_codecs/audio_decoder_factory.h"
#include "api/audio_codecs/audio_format.h"
#include "api/call/audio_sink.h"
#include "api/call/transport.h"
#include "api/crypto/crypto_options.h"
#include "api/crypto/frame_decryptor_interface.h"
#include "api/environment/environment.h"
#include "api/frame_transformer_interface.h"
#include "api/make_ref_counted.h"
#include "api/media_types.h"
#include "api/neteq/default_neteq_factory.h"
#include "api/neteq/neteq.h"
#include "api/neteq/neteq_factory.h"
#include "api/rtc_event_log/rtc_event_log.h"
#include "api/rtp_headers.h"
#include "api/rtp_packet_info.h"
#include "api/rtp_packet_infos.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "audio/audio_level.h"
#include "audio/channel_receive_frame_transformer_delegate.h"
#include "audio/nack_tracker.h"
#include "audio/utility/audio_frame_operations.h"
#include "call/syncable.h"
#include "logging/rtc_event_log/events/rtc_event_audio_playout.h"
#include "logging/rtc_event_log/events/rtc_event_neteq_set_minimum_delay.h"
#include "logging/rtc_event_log/events/rtc_event_rtp_packet_incoming.h"
#include "modules/audio_coding/acm2/acm_resampler.h"
#include "modules/audio_coding/acm2/call_statistics.h"
#include "modules/audio_coding/include/audio_coding_module_typedefs.h"
#include "modules/pacing/packet_router.h"
#include "modules/rtp_rtcp/include/receive_statistics.h"
#include "modules/rtp_rtcp/include/remote_ntp_time_estimator.h"
#include "modules/rtp_rtcp/include/rtcp_statistics.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/absolute_capture_time_interpolator.h"
#include "modules/rtp_rtcp/source/capture_clock_offset_updater.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_config.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_impl2.h"
#include "rtc_base/buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "rtc_base/numerics/sequence_number_unwrapper.h"
#include "rtc_base/race_checker.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/system/no_unique_address.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/trace_event.h"
#include "system_wrappers/include/metrics.h"
#include "system_wrappers/include/ntp_time.h"

namespace webrtc {
namespace voe {

namespace {

constexpr double kAudioSampleDurationSeconds = 0.01;

// Video Sync.
constexpr TimeDelta kVoiceEngineMinMinPlayoutDelay = TimeDelta::Zero();
constexpr TimeDelta kVoiceEngineMaxMinPlayoutDelay = TimeDelta::Seconds(10);

std::unique_ptr<NetEq> CreateNetEq(
    NetEqFactory* neteq_factory,
    size_t jitter_buffer_max_packets,
    bool jitter_buffer_fast_playout,
    int jitter_buffer_min_delay_ms,
    const Environment& env,
    scoped_refptr<AudioDecoderFactory> decoder_factory) {
  NetEq::Config config;
  config.max_packets_in_buffer = jitter_buffer_max_packets;
  config.enable_fast_accelerate = jitter_buffer_fast_playout;
  config.enable_muted_state = true;
  config.min_delay_ms = jitter_buffer_min_delay_ms;
  if (neteq_factory) {
    return neteq_factory->Create(env, config, std::move(decoder_factory));
  }
  return DefaultNetEqFactory().Create(env, config, std::move(decoder_factory));
}

std::unique_ptr<ModuleRtpRtcpImpl2> CreateRtpRtcpModule(
    const Environment& env,
    ReceiveStatistics* receive_statistics,
    Transport* rtcp_send_transport,
    RtcpPacketTypeCounterObserver* rtcp_packet_type_counter_observer,
    bool enable_non_sender_rtt,
    uint32_t remote_ssrc,
    RtcpEventObserver* rtcp_event_observer,
    uint32_t local_ssrc) {
  RtpRtcpInterface::Configuration configuration = {
      .audio = true,
      .receiver_only = true,
      .receive_statistics = receive_statistics,
      .outgoing_transport = rtcp_send_transport,
      .rtcp_event_observer = rtcp_event_observer,
      .rtcp_packet_type_counter_observer = rtcp_packet_type_counter_observer,
      .rtcp_mode = RtcpMode::kCompound,
      .remote_ssrc = remote_ssrc,
      .non_sender_rtt_measurement = enable_non_sender_rtt,
  };

  std::unique_ptr<ModuleRtpRtcpImpl2> rtp_rtcp =
      ModuleRtpRtcpImpl2::CreateReceiveModule(
          env, configuration, [local_ssrc] { return local_ssrc; });
  return rtp_rtcp;
}

class ChannelReceive : public ChannelReceiveInterface,
                       public RtcpPacketTypeCounterObserver {
 public:
  ChannelReceive(const Environment& env,
                 NetEqFactory* absl_nullable neteq_factory,
                 AudioDeviceModule* absl_nonnull audio_device_module,
                 Transport* absl_nonnull rtcp_send_transport,
                 uint32_t remote_ssrc,
                 size_t jitter_buffer_max_packets,
                 bool jitter_buffer_fast_playout,
                 int jitter_buffer_min_delay_ms,
                 bool enable_non_sender_rtt,
                 scoped_refptr<AudioDecoderFactory> decoder_factory,
                 scoped_refptr<FrameDecryptorInterface> frame_decryptor,
                 const CryptoOptions& crypto_options,
                 scoped_refptr<FrameTransformerInterface> frame_transformer,
                 absl::AnyInvocable<void(uint32_t ssrc) &&> on_first_packet,
                 RtcpEventObserver* rtcp_event_observer,
                 PacketRouter* absl_nonnull packet_router,
                 uint32_t local_ssrc,
                 absl::AnyInvocable<void(const RtpPacketInfos&, Timestamp)
                                        const> on_frame_delivered_callback);
  ~ChannelReceive() override;

  void SetSink(AudioSinkInterface* sink) override;

  void SetReceiveCodecs(const std::map<int, SdpAudioFormat>& codecs) override;

  // API methods

  void StartPlayout() override;
  void StopPlayout() override;

  // Codecs
  std::optional<std::pair<int, SdpAudioFormat>> GetReceiveCodec()
      const override;

  void ReceivedRTCPPacket(const uint8_t* data, size_t length) override;

  // RtpPacketSinkInterface.
  void OnRtpPacket(const RtpPacketReceived& packet) override;

  // Muting, Volume and Level.
  void SetChannelOutputVolumeScaling(float scaling) override;
  int GetSpeechOutputLevelFullRange() const override;
  // See description of "totalAudioEnergy" in the WebRTC stats spec:
  // https://w3c.github.io/webrtc-stats/#dom-rtcmediastreamtrackstats-totalaudioenergy
  double GetTotalOutputEnergy() const override;
  double GetTotalOutputDuration() const override;

  // Stats.
  NetworkStatistics GetNetworkStatistics(
      bool get_and_clear_legacy_stats) const override;
  AudioDecodingCallStats GetDecodingCallStatistics() const override;

  // Audio+Video Sync.
  uint32_t GetDelayEstimate() const override;
  bool SetMinimumPlayoutDelay(TimeDelta delay) override;
  std::optional<Syncable::PlayoutInfo> GetPlayoutRtpTimestamp() const override;
  void SetEstimatedPlayoutNtpTimestamp(NtpTime ntp_time,
                                       Timestamp time) override;
  std::optional<int64_t> GetCurrentEstimatedPlayoutNtpTimestampMs(
      int64_t now_ms) const override;

  // Audio quality.
  bool SetBaseMinimumPlayoutDelayMs(int delay_ms) override;
  int GetBaseMinimumPlayoutDelayMs() const override;
  void SetMaximumBufferPackets(size_t max_packets) override;
  void SetFastAccelerate(bool enable) override;

  // Produces the transport-related timestamps; current_delay_ms is left unset.
  std::optional<Syncable::Info> GetSyncInfo() const override;

  ChannelReceiveStatistics GetRTCPStatistics() const override;
  void SetNACKStatus(bool enable, int max_packets) override;
  void SetRtcpMode(RtcpMode mode) override;
  void SetNonSenderRttMeasurement(bool enabled) override;

  AudioMixer::Source::AudioFrameInfo GetAudioFrameWithInfo(
      int sample_rate_hz,
      AudioFrame* audio_frame) override;

  int PreferredSampleRate() const override;

  // Sets a frame transformer between the depacketizer and the decoder, to
  // transform the received frames before decoding them.
  void SetDepacketizerToDecoderFrameTransformer(
      scoped_refptr<FrameTransformerInterface> frame_transformer) override;

  void SetFrameDecryptor(
      scoped_refptr<FrameDecryptorInterface> frame_decryptor) override;

  uint32_t remote_ssrc() const override;

  void RtcpPacketTypesCounterUpdated(
      uint32_t ssrc,
      const RtcpPacketTypeCounter& packet_counter) override;

 private:
  void ReceivePacket(const uint8_t* packet,
                     size_t packet_length,
                     const RTPHeader& header,
                     Timestamp receive_time) RTC_RUN_ON(worker_thread_checker_);
  int GetRtpTimestampRateHz() const;
  int GetRtpTimestampRateHzLocked() const
      RTC_EXCLUSIVE_LOCKS_REQUIRED(neteq_mutex_);

  void OnReceivedPayloadData(std::span<const uint8_t> payload,
                             const RTPHeader& header,
                             Timestamp receive_time)
      RTC_RUN_ON(worker_thread_checker_);

  void InitFrameTransformerDelegate(
      scoped_refptr<FrameTransformerInterface> frame_transformer)
      RTC_RUN_ON(worker_thread_checker_);

  // Thread checkers document and lock usage of some methods to specific threads
  // we know about. The goal is to eventually split up voe::ChannelReceive into
  // parts with single-threaded semantics, and thereby reduce the need for
  // locks.
  RTC_NO_UNIQUE_ADDRESS SequenceChecker worker_thread_checker_;

  const Environment env_;
  TaskQueueBase* const worker_thread_;

  // Methods accessed from audio and video threads are checked for sequential-
  // only access. We don't necessarily own and control these threads, so thread
  // checkers cannot be used. E.g. Chromium may transfer "ownership" from one
  // audio thread to another, but access is still sequential.
  RaceChecker audio_thread_race_checker_;

  bool playing_ RTC_GUARDED_BY(worker_thread_checker_) = false;

  // Indexed by payload type.
  std::map<uint8_t, int> payload_type_frequencies_
      RTC_GUARDED_BY(worker_thread_checker_);

  const std::unique_ptr<ReceiveStatistics> rtp_receive_statistics_;
  const std::unique_ptr<ModuleRtpRtcpImpl2> rtp_rtcp_;
  const uint32_t remote_ssrc_;
  const absl::AnyInvocable<void(const RtpPacketInfos&, Timestamp) const>
      on_frame_delivered_callback_;

  std::optional<uint32_t> last_received_rtp_timestamp_
      RTC_GUARDED_BY(&worker_thread_checker_);
  std::optional<Timestamp> last_received_rtp_system_time_
      RTC_GUARDED_BY(&worker_thread_checker_);

  mutable Mutex neteq_mutex_;
  const std::unique_ptr<NetEq> neteq_ RTC_GUARDED_BY(neteq_mutex_);
  // The system time of the last audio frame pulled from NetEq.
  std::optional<Timestamp> last_playout_time_ RTC_GUARDED_BY(neteq_mutex_);
  acm2::ResamplerHelper resampler_helper_
      RTC_GUARDED_BY(audio_thread_race_checker_);

  mutable Mutex call_stats_mutex_;
  acm2::CallStatistics call_stats_ RTC_GUARDED_BY(call_stats_mutex_);

  Mutex audio_sink_mutex_;
  AudioSinkInterface* audio_sink_ RTC_GUARDED_BY(audio_sink_mutex_) = nullptr;

  AudioLevel output_audio_level_;

  RemoteNtpTimeEstimator ntp_estimator_ RTC_GUARDED_BY(ts_stats_lock_);

  std::optional<NtpTime> playout_timestamp_ntp_
      RTC_GUARDED_BY(worker_thread_checker_);
  std::optional<Timestamp> playout_timestamp_ntp_time_
      RTC_GUARDED_BY(worker_thread_checker_);

  mutable Mutex ts_stats_lock_;

  RtpTimestampUnwrapper rtp_ts_wraparound_handler_
      RTC_GUARDED_BY(audio_thread_race_checker_);
  // The rtp timestamp of the first played out audio frame.
  int64_t capture_start_rtp_time_stamp_
      RTC_GUARDED_BY(audio_thread_race_checker_);
  // The capture ntp time (in local timebase) of the first played out audio
  // frame.
  int64_t capture_start_ntp_time_ms_ RTC_GUARDED_BY(ts_stats_lock_);

  AudioDeviceModule* const audio_device_module_;
  std::atomic<float> output_gain_;

  PacketRouter* const packet_router_;

  SequenceChecker construction_thread_;

  // E2EE Audio Frame Decryption
  scoped_refptr<FrameDecryptorInterface> frame_decryptor_
      RTC_GUARDED_BY(worker_thread_checker_);
  const CryptoOptions crypto_options_;

  AbsoluteCaptureTimeInterpolator absolute_capture_time_interpolator_
      RTC_GUARDED_BY(worker_thread_checker_);

  CaptureClockOffsetUpdater capture_clock_offset_updater_
      RTC_GUARDED_BY(ts_stats_lock_);

  scoped_refptr<ChannelReceiveFrameTransformerDelegate>
      frame_transformer_delegate_;

  // Counter that's used to control the frequency of reporting histograms
  // from the `GetAudioFrameWithInfo` callback.
  int audio_frame_interval_count_ RTC_GUARDED_BY(audio_thread_race_checker_) =
      0;
  // Controls how many callbacks we let pass by before reporting callback stats.
  // A value of 100 means 100 callbacks, each one of which represents 10ms worth
  // of data, so the stats reporting frequency will be 1Hz (modulo failures).
  constexpr static int kHistogramReportingInterval = 100;

  RtcpPacketTypeCounter rtcp_packet_type_counter_
      RTC_GUARDED_BY(worker_thread_checker_);

  std::map<int, SdpAudioFormat> payload_type_map_
      RTC_GUARDED_BY(worker_thread_checker_);

  std::unique_ptr<NackTracker> nack_tracker_
      RTC_GUARDED_BY(worker_thread_checker_);

  absl::AnyInvocable<void(uint32_t ssrc) &&> on_first_packet_
      RTC_GUARDED_BY(worker_thread_checker_);
};

void ChannelReceive::OnReceivedPayloadData(std::span<const uint8_t> payload,
                                           const RTPHeader& header,
                                           Timestamp receive_time) {
  if (!playing_) {
    // Avoid inserting into NetEQ when we are not playing. Count the
    // packet as discarded.

    // Tell source_tracker_ that the frame has been "delivered". Normally, this
    // happens in AudioReceiveStreamInterface when audio frames are pulled out,
    // but when playout is muted, nothing is pulling frames. The downside of
    // this approach is that frames delivered this way won't be delayed for
    // playout, and therefore will be unsynchronized with (a) audio delay when
    // playing and (b) any audio/video synchronization. But the alternative is
    // that muting playout also stops the SourceTracker from updating RtpSource
    // information.
    if (on_frame_delivered_callback_) {
      on_frame_delivered_callback_(
          RtpPacketInfos({RtpPacketInfo(header, receive_time)}),
          env_.clock().CurrentTime());
    }
    return;
  }

  if (payload.empty()) {
    return;
  }

  // Push the incoming payload (parsed and ready for decoding) into NetEq.
  MutexLock lock(&neteq_mutex_);
  if (neteq_->InsertPacket(header, payload,
                           RtpPacketInfo(header, receive_time)) != NetEq::kOK) {
    RTC_DLOG(LS_ERROR) << "ChannelReceive::OnReceivedPayloadData() unable to "
                          "insert packet into NetEq; PT = "
                       << static_cast<int>(header.payloadType);
  }
}

void ChannelReceive::InitFrameTransformerDelegate(
    scoped_refptr<FrameTransformerInterface> frame_transformer) {
  RTC_DCHECK(frame_transformer);
  RTC_DCHECK(!frame_transformer_delegate_);
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);

  // Pass a callback to ChannelReceive::OnReceivedPayloadData, to be called by
  // the delegate to receive transformed audio.
  ChannelReceiveFrameTransformerDelegate::ReceiveFrameCallback
      receive_audio_callback = [this](std::span<const uint8_t> packet,
                                      const RTPHeader& header,
                                      Timestamp receive_time) {
        RTC_DCHECK_RUN_ON(&worker_thread_checker_);
        OnReceivedPayloadData(packet, header, receive_time);
      };
  frame_transformer_delegate_ =
      make_ref_counted<ChannelReceiveFrameTransformerDelegate>(
          std::move(receive_audio_callback), std::move(frame_transformer),
          worker_thread_);
  frame_transformer_delegate_->Init();
}

AudioMixer::Source::AudioFrameInfo ChannelReceive::GetAudioFrameWithInfo(
    int sample_rate_hz,
    AudioFrame* audio_frame) {
  TRACE_EVENT_BEGIN1("webrtc", "ChannelReceive::GetAudioFrameWithInfo",
                     "sample_rate_hz", sample_rate_hz);
  RTC_DCHECK_RUNS_SERIALIZED(&audio_thread_race_checker_);

  Timestamp now = env_.clock().CurrentTime();
  env_.event_log().Log(std::make_unique<RtcEventAudioPlayout>(remote_ssrc_));

  const bool report_histograms =
      ++audio_frame_interval_count_ >= kHistogramReportingInterval;
  if (report_histograms) {
    audio_frame_interval_count_ = 0;
  }
  int target_delay_ms = 0;
  int jitter_buffer_delay_ms = 0;
  {
    MutexLock lock(&neteq_mutex_);
    if (neteq_->GetAudio(audio_frame) != NetEq::kOK) {
      RTC_DLOG(LS_ERROR)
          << "ChannelReceive::GetAudioFrame() PlayoutData10Ms() failed!";
      // In all likelihood, the audio in this frame is garbage. We return an
      // error so that the audio mixer module doesn't add it to the mix. As
      // a result, it won't be played out and the actions skipped here are
      // irrelevant.
      TRACE_EVENT_END1("webrtc", "ChannelReceive::GetAudioFrameWithInfo",
                       "error", 1);
      return AudioMixer::Source::AudioFrameInfo::kError;
    }
    last_playout_time_ = now;
    // Sample the delays for the histograms while the lock is held anyway.
    if (report_histograms) {
      target_delay_ms = neteq_->TargetDelayMs();
      jitter_buffer_delay_ms = neteq_->FilteredCurrentDelayMs();
    }
  }

  // Update the stats with NetEq's muted state before MaybeResample(), which
  // unmutes a frame whose resampled output can still hold the tail of the
  // audio before it.
  {
    MutexLock lock(&call_stats_mutex_);
    call_stats_.DecodedByNetEq(audio_frame->speech_type_, audio_frame->muted());
  }

  resampler_helper_.MaybeResample(sample_rate_hz, audio_frame);

  bool has_capture_time = false;
  for (const auto& packet_info : audio_frame->packet_infos_) {
    if (packet_info.absolute_capture_time().has_value()) {
      has_capture_time = true;
      break;
    }
  }

  if (capture_start_rtp_time_stamp_ < 0 && audio_frame->timestamp_ != 0) {
    // The first frame with a valid rtp timestamp.
    capture_start_rtp_time_stamp_ = audio_frame->timestamp_;
  }

  const bool has_valid_timestamp = capture_start_rtp_time_stamp_ >= 0;

  // Update capture clock offsets and NTP estimate under a single ts_stats_lock_
  // acquisition to minimize lock contention and avoid taking the lock inside
  // loops.
  if (has_capture_time || has_valid_timestamp) {
    MutexLock lock(&ts_stats_lock_);
    if (has_capture_time) {
      RtpPacketInfos::vector_type packet_infos;
      packet_infos.reserve(audio_frame->packet_infos_.size());
      for (const auto& packet_info : audio_frame->packet_infos_) {
        RtpPacketInfo new_packet_info(packet_info);
        if (packet_info.absolute_capture_time().has_value()) {
          new_packet_info.set_local_capture_clock_offset(
              CaptureClockOffsetUpdater::ConvertToTimeDelta(
                  capture_clock_offset_updater_
                      .AdjustEstimatedCaptureClockOffset(
                          packet_info.absolute_capture_time()
                              ->estimated_capture_clock_offset)));
        }
        packet_infos.push_back(std::move(new_packet_info));
      }
      audio_frame->packet_infos_ = RtpPacketInfos(std::move(packet_infos));
    }

    if (has_valid_timestamp) {
      // audio_frame.timestamp_ should be valid from now on.
      // Compute elapsed time.
      int64_t unwrap_timestamp =
          rtp_ts_wraparound_handler_.Unwrap(audio_frame->timestamp_);
      audio_frame->elapsed_time_ms_ =
          (unwrap_timestamp - capture_start_rtp_time_stamp_) /
          (GetRtpTimestampRateHz() / 1000);

      // Compute ntp time.
      audio_frame->ntp_time_ms_ =
          ntp_estimator_.Estimate(audio_frame->timestamp_);
      // `ntp_time_ms_` won't be valid until at least 2 RTCP SRs are received.
      if (audio_frame->ntp_time_ms_ > 0) {
        // Compute `capture_start_ntp_time_ms_` so that
        // `capture_start_ntp_time_ms_` + `elapsed_time_ms_` == `ntp_time_ms_`
        capture_start_ntp_time_ms_ =
            audio_frame->ntp_time_ms_ - audio_frame->elapsed_time_ms_;
      }
    }
  }

  {
    // Pass the audio buffers to an optional sink callback, before applying
    // scaling/panning, as that applies to the mix operation.
    // External recipients of the audio (e.g. via AudioTrack), will do their
    // own mixing/dynamic processing.
    MutexLock lock(&audio_sink_mutex_);
    if (audio_sink_ != nullptr) {
      AudioSinkInterface::Data data(
          audio_frame->data(), audio_frame->samples_per_channel_,
          audio_frame->sample_rate_hz_, audio_frame->num_channels_,
          audio_frame->timestamp_, &audio_frame->packet_infos_);
      audio_sink_->OnData(data);
    }
  }

  // Output volume scaling
  float output_gain = output_gain_.load();
  if (output_gain < 0.99f || output_gain > 1.01f) {
    // TODO(solenberg): Combine with mute state - this can cause clicks!
    AudioFrameOperations::ScaleWithSat(output_gain, audio_frame);
  }

  // Measure audio level (0-9)
  // TODO(henrik.lundin) Use the `muted` information here too.
  // TODO(deadbeef): Use RmsLevel for `output_audio_level_` (see
  // https://crbug.com/webrtc/7517).
  output_audio_level_.ComputeLevel(*audio_frame, kAudioSampleDurationSeconds);

  if (!audio_frame->packet_infos_.empty() && on_frame_delivered_callback_) {
    on_frame_delivered_callback_(audio_frame->packet_infos_, now);
  }

  if (report_histograms) {
    RTC_HISTOGRAM_COUNTS_1000("WebRTC.Audio.TargetJitterBufferDelayMs",
                              target_delay_ms);
    RTC_HISTOGRAM_COUNTS_1000("WebRTC.Audio.ReceiverJitterBufferDelayMs",
                              jitter_buffer_delay_ms);
  }

  TRACE_EVENT_END2("webrtc", "ChannelReceive::GetAudioFrameWithInfo", "gain",
                   output_gain, "muted", audio_frame->muted());
  return audio_frame->muted() ? AudioMixer::Source::AudioFrameInfo::kMuted
                              : AudioMixer::Source::AudioFrameInfo::kNormal;
}

int ChannelReceive::PreferredSampleRate() const {
  RTC_DCHECK_RUNS_SERIALIZED(&audio_thread_race_checker_);
  MutexLock lock(&neteq_mutex_);
  const std::optional<NetEq::DecoderFormat> decoder =
      neteq_->GetCurrentDecoderFormat();
  const int last_packet_sample_rate_hz = decoder ? decoder->sample_rate_hz : 0;
  // Return the bigger of playout and receive frequency in the ACM.
  return std::max(last_packet_sample_rate_hz,
                  neteq_->last_output_sample_rate_hz());
}

ChannelReceive::ChannelReceive(
    const Environment& env,
    NetEqFactory* absl_nullable neteq_factory,
    AudioDeviceModule* absl_nonnull audio_device_module,
    Transport* absl_nonnull rtcp_send_transport,
    uint32_t remote_ssrc,
    size_t jitter_buffer_max_packets,
    bool jitter_buffer_fast_playout,
    int jitter_buffer_min_delay_ms,
    bool enable_non_sender_rtt,
    scoped_refptr<AudioDecoderFactory> decoder_factory,
    scoped_refptr<FrameDecryptorInterface> frame_decryptor,
    const CryptoOptions& crypto_options,
    scoped_refptr<FrameTransformerInterface> frame_transformer,
    absl::AnyInvocable<void(uint32_t ssrc) &&> on_first_packet,
    RtcpEventObserver* rtcp_event_observer,
    PacketRouter* absl_nonnull packet_router,
    uint32_t local_ssrc,
    absl::AnyInvocable<void(const RtpPacketInfos&, Timestamp) const>
        on_frame_delivered_callback)
    : env_(env),
      worker_thread_(TaskQueueBase::Current()),
      rtp_receive_statistics_(ReceiveStatistics::Create(&env_.clock())),
      rtp_rtcp_(CreateRtpRtcpModule(env,
                                    rtp_receive_statistics_.get(),
                                    rtcp_send_transport,
                                    this,
                                    enable_non_sender_rtt,
                                    remote_ssrc,
                                    rtcp_event_observer,
                                    local_ssrc)),
      remote_ssrc_(remote_ssrc),
      on_frame_delivered_callback_(std::move(on_frame_delivered_callback)),
      neteq_(CreateNetEq(neteq_factory,
                         jitter_buffer_max_packets,
                         jitter_buffer_fast_playout,
                         jitter_buffer_min_delay_ms,
                         env_,
                         std::move(decoder_factory))),
      ntp_estimator_(&env_.clock()),
      capture_start_rtp_time_stamp_(-1),
      capture_start_ntp_time_ms_(-1),
      audio_device_module_(audio_device_module),
      output_gain_(1.0f),
      packet_router_(packet_router),
      frame_decryptor_(std::move(frame_decryptor)),
      crypto_options_(crypto_options),
      absolute_capture_time_interpolator_(&env_.clock()),
      on_first_packet_(std::move(on_first_packet)) {
  RTC_DCHECK(audio_device_module);
  RTC_DCHECK(packet_router_);

  rtp_receive_statistics_->EnableRetransmitDetection(remote_ssrc_, true);

  constexpr bool remb_candidate = false;
  packet_router_->AddReceiveRtpModule(rtp_rtcp_.get(), remb_candidate);

  if (frame_transformer)
    InitFrameTransformerDelegate(std::move(frame_transformer));
}

ChannelReceive::~ChannelReceive() {
  RTC_DCHECK_RUN_ON(&construction_thread_);

  packet_router_->RemoveReceiveRtpModule(rtp_rtcp_.get());

  // Resets the delegate's callback to ChannelReceive::OnReceivedPayloadData.
  if (frame_transformer_delegate_)
    frame_transformer_delegate_->Reset();

  StopPlayout();
}

void ChannelReceive::SetSink(AudioSinkInterface* sink) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  MutexLock lock(&audio_sink_mutex_);
  audio_sink_ = sink;
}

void ChannelReceive::StartPlayout() {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  playing_ = true;
}

void ChannelReceive::StopPlayout() {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  playing_ = false;
  output_audio_level_.ResetLevelFullRange();
  MutexLock lock(&neteq_mutex_);
  last_playout_time_.reset();
  neteq_->FlushBuffers();
  if (nack_tracker_) {
    nack_tracker_->Reset();
  }
}

uint32_t ChannelReceive::remote_ssrc() const {
  return remote_ssrc_;
}

std::optional<std::pair<int, SdpAudioFormat>> ChannelReceive::GetReceiveCodec()
    const {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  MutexLock lock(&neteq_mutex_);
  std::optional<NetEq::DecoderFormat> decoder =
      neteq_->GetCurrentDecoderFormat();
  if (!decoder) {
    return std::nullopt;
  }
  return std::make_pair(decoder->payload_type, decoder->sdp_format);
}

void ChannelReceive::SetReceiveCodecs(
    const std::map<int, SdpAudioFormat>& codecs) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  // Rebuild, so that payload types which are no longer negotiated are dropped
  // in OnRtpPacket(), consistent with `payload_type_map_` and NetEq.
  payload_type_frequencies_.clear();
  for (const auto& kv : codecs) {
    RTC_DCHECK_GE(kv.second.clockrate_hz, 1000);
    payload_type_frequencies_[kv.first] = kv.second.clockrate_hz;
  }
  payload_type_map_ = codecs;
  MutexLock lock(&neteq_mutex_);
  neteq_->SetCodecs(codecs);
}

void ChannelReceive::OnRtpPacket(const RtpPacketReceived& packet) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  if (on_first_packet_) {
    auto cb = std::move(on_first_packet_);
    std::move(cb)(remote_ssrc_);
  }
  env_.event_log().Log(std::make_unique<RtcEventRtpPacketIncoming>(packet));
  Timestamp now = env_.clock().CurrentTime();

  last_received_rtp_timestamp_ = packet.Timestamp();
  last_received_rtp_system_time_ = now;

  const auto& it = payload_type_frequencies_.find(packet.PayloadType());
  if (it == payload_type_frequencies_.end())
    return;
  // TODO(bugs.webrtc.org/7135): Set payload_type_frequency earlier, when packet
  // is parsed.
  RtpPacketReceived packet_copy(packet);
  packet_copy.set_payload_type_frequency(it->second);
  if (nack_tracker_) {
    nack_tracker_->UpdateSampleRate(it->second);
    nack_tracker_->UpdateLastReceivedPacket(packet.SequenceNumber(),
                                            packet.Timestamp());
    std::vector<uint16_t> nack_list =
        nack_tracker_->GetNackList(rtp_rtcp_->LastRtt());
    if (!nack_list.empty()) {
      rtp_rtcp_->SendNACK(nack_list.data(), nack_list.size());
    }
  }

  rtp_receive_statistics_->OnRtpPacket(packet_copy);

  RTPHeader header;
  packet_copy.GetHeader(&header);

  // Interpolates absolute capture timestamp RTP header extension.
  header.extension.absolute_capture_time =
      absolute_capture_time_interpolator_.OnReceivePacket(
          AbsoluteCaptureTimeInterpolator::GetSource(header.ssrc,
                                                     header.arrOfCSRCs),
          header.timestamp,
          saturated_cast<uint32_t>(packet_copy.payload_type_frequency()),
          header.extension.absolute_capture_time);

  ReceivePacket(packet_copy.data(), packet_copy.size(), header,
                packet.arrival_time());
}

void ChannelReceive::ReceivePacket(const uint8_t* packet,
                                   size_t packet_length,
                                   const RTPHeader& header,
                                   Timestamp receive_time) {
  const uint8_t* payload = packet + header.headerLength;
  RTC_DCHECK_GE(packet_length, header.headerLength);
  size_t payload_length = packet_length - header.headerLength;

  size_t payload_data_length = payload_length - header.paddingLength;

  // E2EE Custom Audio Frame Decryption (This is optional).
  // Keep this buffer around for the lifetime of the OnReceivedPayloadData call.
  Buffer decrypted_audio_payload;
  if (frame_decryptor_ != nullptr) {
    const size_t max_plaintext_size = frame_decryptor_->GetMaxPlaintextByteSize(
        MediaType::AUDIO, payload_length);
    decrypted_audio_payload.SetSize(max_plaintext_size);

    const std::vector<uint32_t> csrcs(header.arrOfCSRCs,
                                      header.arrOfCSRCs + header.numCSRCs);
    const FrameDecryptorInterface::Result decrypt_result =
        frame_decryptor_->Decrypt(
            MediaType::AUDIO, csrcs,
            /*additional_data=*/{},
            std::span<const uint8_t>(payload, payload_data_length),
            decrypted_audio_payload);

    if (decrypt_result.IsOk()) {
      decrypted_audio_payload.SetSize(decrypt_result.bytes_written);
    } else {
      // Interpret failures as a silent frame.
      decrypted_audio_payload.SetSize(0);
    }

    payload = decrypted_audio_payload.data();
    payload_data_length = decrypted_audio_payload.size();
  } else if (crypto_options_.sframe.require_frame_encryption) {
    RTC_DLOG(LS_ERROR)
        << "FrameDecryptor required but not set, dropping packet";
    payload_data_length = 0;
  }

  std::span<const uint8_t> payload_data(payload, payload_data_length);
  if (frame_transformer_delegate_) {
    // Asynchronously transform the received payload. After the payload is
    // transformed, the delegate will call OnReceivedPayloadData to handle it.
    StringBuilder mime_type;
    auto it = payload_type_map_.find(header.payloadType);
    mime_type << MediaTypeToString(MediaType::AUDIO) << "/"
              << (it != payload_type_map_.end() ? it->second.name
                                                : "x-unknown");
    frame_transformer_delegate_->Transform(payload_data, header, remote_ssrc_,
                                           mime_type.str(), receive_time);
  } else {
    OnReceivedPayloadData(payload_data, header, receive_time);
  }
}

void ChannelReceive::ReceivedRTCPPacket(const uint8_t* data, size_t length) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  // Deliver RTCP packet to RTP/RTCP module for parsing
  rtp_rtcp_->IncomingRtcpPacket(std::span(data, length));

  std::optional<TimeDelta> rtt = rtp_rtcp_->LastRtt();
  if (!rtt.has_value()) {
    // Waiting for valid RTT.
    return;
  }

  std::optional<RtpRtcpInterface::SenderReportStats> last_sr =
      rtp_rtcp_->GetSenderReportStats();
  if (!last_sr.has_value()) {
    // Waiting for RTCP.
    return;
  }

  {
    MutexLock lock(&ts_stats_lock_);
    ntp_estimator_.UpdateRtcpTimestamp(*rtt, last_sr->last_remote_ntp_timestamp,
                                       last_sr->last_remote_rtp_timestamp);
    std::optional<int64_t> remote_to_local_clock_offset =
        ntp_estimator_.EstimateRemoteToLocalClockOffset();
    if (remote_to_local_clock_offset.has_value()) {
      capture_clock_offset_updater_.SetRemoteToLocalClockOffset(
          *remote_to_local_clock_offset);
    }
  }
}

int ChannelReceive::GetSpeechOutputLevelFullRange() const {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  return output_audio_level_.LevelFullRange();
}

double ChannelReceive::GetTotalOutputEnergy() const {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  return output_audio_level_.TotalEnergy();
}

double ChannelReceive::GetTotalOutputDuration() const {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  return output_audio_level_.TotalDuration();
}

void ChannelReceive::SetChannelOutputVolumeScaling(float scaling) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  output_gain_.store(scaling);
}

ChannelReceiveStatistics ChannelReceive::GetRTCPStatistics() const {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  ChannelReceiveStatistics stats;

  // The jitter statistics is updated for each received RTP packet and is based
  // on received packets.
  RtpReceiveStats rtp_stats;
  StreamStatistician* statistician =
      rtp_receive_statistics_->GetStatistician(remote_ssrc_);
  if (statistician) {
    rtp_stats = statistician->GetStats();
  }

  stats.packets_lost = rtp_stats.packets_lost;
  stats.jitter_ms = rtp_stats.interarrival_jitter.ms();

  // Data counters.
  if (statistician) {
    stats.payload_bytes_received = rtp_stats.packet_counter.payload_bytes;
    stats.header_and_padding_bytes_received =
        rtp_stats.packet_counter.header_bytes +
        rtp_stats.packet_counter.padding_bytes;
    stats.packets_received = rtp_stats.packet_counter.packets;
    stats.packets_received_with_ect1 =
        rtp_stats.packet_counter.packets_with_ect1;
    stats.packets_received_with_ce = rtp_stats.packet_counter.packets_with_ce;
    stats.last_packet_received = rtp_stats.last_packet_received;
  }

  stats.nacks_sent = rtcp_packet_type_counter_.nack_packets;

  // Timestamps.
  {
    MutexLock lock(&ts_stats_lock_);
    stats.capture_start_ntp_time_ms = capture_start_ntp_time_ms_;
  }

  std::optional<RtpRtcpInterface::SenderReportStats> rtcp_sr_stats =
      rtp_rtcp_->GetSenderReportStats();
  if (rtcp_sr_stats.has_value()) {
    stats.last_sender_report_timestamp = rtcp_sr_stats->last_arrival_timestamp;
    stats.last_sender_report_utc_timestamp =
        Clock::NtpToUtc(rtcp_sr_stats->last_arrival_ntp_timestamp);
    stats.last_sender_report_remote_utc_timestamp =
        Clock::NtpToUtc(rtcp_sr_stats->last_remote_ntp_timestamp);
    stats.sender_reports_packets_sent = rtcp_sr_stats->packets_sent;
    stats.sender_reports_bytes_sent = rtcp_sr_stats->bytes_sent;
    stats.sender_reports_reports_count = rtcp_sr_stats->reports_count;
  }

  std::optional<RtpRtcpInterface::NonSenderRttStats> non_sender_rtt_stats =
      rtp_rtcp_->GetNonSenderRttStats();
  if (non_sender_rtt_stats.has_value()) {
    stats.round_trip_time = non_sender_rtt_stats->round_trip_time;
    stats.round_trip_time_measurements =
        non_sender_rtt_stats->round_trip_time_measurements;
    stats.total_round_trip_time = non_sender_rtt_stats->total_round_trip_time;
  }

  return stats;
}

void ChannelReceive::SetNACKStatus(bool enable, int max_packets) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  // None of these functions can fail.
  if (enable) {
    rtp_receive_statistics_->SetMaxReorderingThreshold(remote_ssrc_,
                                                       max_packets);
    nack_tracker_ = std::make_unique<NackTracker>(max_packets);
  } else {
    rtp_receive_statistics_->SetMaxReorderingThreshold(
        remote_ssrc_, kDefaultMaxReorderingThreshold);
    nack_tracker_.reset();
  }
}

void ChannelReceive::SetRtcpMode(RtcpMode mode) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  rtp_rtcp_->SetRTCPStatus(mode);
}

void ChannelReceive::SetNonSenderRttMeasurement(bool enabled) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  rtp_rtcp_->SetNonSenderRttMeasurement(enabled);
}

void ChannelReceive::RtcpPacketTypesCounterUpdated(
    uint32_t ssrc,
    const RtcpPacketTypeCounter& packet_counter) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  if (ssrc != remote_ssrc_) {
    return;
  }
  rtcp_packet_type_counter_ = packet_counter;
}

void ChannelReceive::SetDepacketizerToDecoderFrameTransformer(
    scoped_refptr<FrameTransformerInterface> frame_transformer) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  if (!frame_transformer) {
    RTC_DCHECK_NOTREACHED() << "Not setting the transformer?";
    return;
  }
  if (frame_transformer_delegate_) {
    // Depending on when the channel is created, the transformer might be set
    // twice. Don't replace the delegate if it was already initialized.
    // TODO(crbug.com/webrtc/15674): Prevent multiple calls during
    // reconfiguration.
    RTC_CHECK_EQ(frame_transformer_delegate_->FrameTransformer(),
                 frame_transformer);
    return;
  }

  InitFrameTransformerDelegate(std::move(frame_transformer));
}

void ChannelReceive::SetFrameDecryptor(
    scoped_refptr<FrameDecryptorInterface> frame_decryptor) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  frame_decryptor_ = std::move(frame_decryptor);
}

NetworkStatistics ChannelReceive::GetNetworkStatistics(
    bool get_and_clear_legacy_stats) const {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  NetworkStatistics acm_stat;
  NetEqNetworkStatistics neteq_stat;
  MutexLock lock(&neteq_mutex_);
  if (get_and_clear_legacy_stats) {
    // NetEq function always returns zero, so we don't check the return value.
    neteq_->NetworkStatistics(&neteq_stat);

    acm_stat.currentExpandRate = neteq_stat.expand_rate;
    acm_stat.currentSpeechExpandRate = neteq_stat.speech_expand_rate;
    acm_stat.currentPreemptiveRate = neteq_stat.preemptive_rate;
    acm_stat.currentAccelerateRate = neteq_stat.accelerate_rate;
    acm_stat.currentSecondaryDecodedRate = neteq_stat.secondary_decoded_rate;
    acm_stat.currentSecondaryDiscardedRate =
        neteq_stat.secondary_discarded_rate;
    acm_stat.meanWaitingTimeMs = neteq_stat.mean_waiting_time_ms;
    acm_stat.maxWaitingTimeMs = neteq_stat.max_waiting_time_ms;
  } else {
    neteq_stat = neteq_->CurrentNetworkStatistics();
    acm_stat.currentExpandRate = 0;
    acm_stat.currentSpeechExpandRate = 0;
    acm_stat.currentPreemptiveRate = 0;
    acm_stat.currentAccelerateRate = 0;
    acm_stat.currentSecondaryDecodedRate = 0;
    acm_stat.currentSecondaryDiscardedRate = 0;
    acm_stat.meanWaitingTimeMs = -1;
    acm_stat.maxWaitingTimeMs = 1;
  }
  acm_stat.currentBufferSize = neteq_stat.current_buffer_size_ms;
  acm_stat.preferredBufferSize = neteq_stat.preferred_buffer_size_ms;
  acm_stat.jitterPeaksFound = neteq_stat.jitter_peaks_found ? true : false;

  NetEqLifetimeStatistics neteq_lifetime_stat = neteq_->GetLifetimeStatistics();
  acm_stat.totalSamplesReceived = neteq_lifetime_stat.total_samples_received;
  acm_stat.concealedSamples = neteq_lifetime_stat.concealed_samples;
  acm_stat.silentConcealedSamples =
      neteq_lifetime_stat.silent_concealed_samples;
  acm_stat.concealmentEvents = neteq_lifetime_stat.concealment_events;
  acm_stat.jitterBufferDelayMs = neteq_lifetime_stat.jitter_buffer_delay_ms;
  acm_stat.jitterBufferTargetDelayMs =
      neteq_lifetime_stat.jitter_buffer_target_delay_ms;
  acm_stat.jitterBufferMinimumDelayMs =
      neteq_lifetime_stat.jitter_buffer_minimum_delay_ms;
  acm_stat.jitterBufferEmittedCount =
      neteq_lifetime_stat.jitter_buffer_emitted_count;
  acm_stat.delayedPacketOutageSamples =
      neteq_lifetime_stat.delayed_packet_outage_samples;
  acm_stat.relativePacketArrivalDelayMs =
      neteq_lifetime_stat.relative_packet_arrival_delay_ms;
  acm_stat.interruptionCount = neteq_lifetime_stat.interruption_count;
  acm_stat.totalInterruptionDurationMs =
      neteq_lifetime_stat.total_interruption_duration_ms;
  acm_stat.insertedSamplesForDeceleration =
      neteq_lifetime_stat.inserted_samples_for_deceleration;
  acm_stat.removedSamplesForAcceleration =
      neteq_lifetime_stat.removed_samples_for_acceleration;
  acm_stat.fecPacketsReceived = neteq_lifetime_stat.fec_packets_received;
  acm_stat.fecPacketsDiscarded = neteq_lifetime_stat.fec_packets_discarded;
  acm_stat.totalProcessingDelayUs =
      neteq_lifetime_stat.total_processing_delay_us;
  acm_stat.packetsDiscarded = neteq_lifetime_stat.packets_discarded;

  NetEqOperationsAndState neteq_operations_and_state =
      neteq_->GetOperationsAndState();
  acm_stat.packetBufferFlushes =
      neteq_operations_and_state.packet_buffer_flushes;
  return acm_stat;
}

AudioDecodingCallStats ChannelReceive::GetDecodingCallStatistics() const {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  MutexLock lock(&call_stats_mutex_);
  return call_stats_.GetDecodingStatistics();
}

uint32_t ChannelReceive::GetDelayEstimate() const {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  // Return the current jitter buffer delay + playout delay.
  uint16_t delay_ms = 0;
  audio_device_module_->PlayoutDelay(&delay_ms);
  MutexLock lock(&neteq_mutex_);
  return neteq_->FilteredCurrentDelayMs() + delay_ms;
}

bool ChannelReceive::SetMinimumPlayoutDelay(TimeDelta delay) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  // Limit to range accepted by both VoE and ACM, so we're at least getting as
  // close as possible, instead of failing.
  delay = std::clamp(delay, kVoiceEngineMinMinPlayoutDelay,
                     kVoiceEngineMaxMinPlayoutDelay);
  MutexLock lock(&neteq_mutex_);
  if (!neteq_->SetMinimumDelay(delay.ms())) {
    RTC_DLOG(LS_ERROR)
        << "SetMinimumPlayoutDelay() failed to set min playout delay " << delay;
    return false;
  }
  return true;
}

std::optional<Syncable::PlayoutInfo> ChannelReceive::GetPlayoutRtpTimestamp()
    const {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);

  std::optional<uint32_t> playout_timestamp;
  Timestamp last_playout_time = Timestamp::Zero();
  int rtp_timestamp_rate_hz = 0;
  {
    MutexLock lock(&neteq_mutex_);
    if (!last_playout_time_) {
      return std::nullopt;
    }
    last_playout_time = *last_playout_time_;
    playout_timestamp = neteq_->GetPlayoutTimestamp();
    rtp_timestamp_rate_hz = GetRtpTimestampRateHzLocked();
  }

  if (!playout_timestamp) {
    // This can happen if this channel has not received any RTP packets. In
    // this case, NetEq is not capable of computing a playout timestamp.
    return std::nullopt;
  }

  uint16_t delay_ms = 0;
  if (audio_device_module_->PlayoutDelay(&delay_ms) == -1) {
    RTC_DLOG(LS_WARNING)
        << "ChannelReceive::GetPlayoutRtpTimestamp() failed to read"
           " playout delay from the ADM";
    return std::nullopt;
  }

  uint32_t adjusted_playout_timestamp =
      *playout_timestamp - (delay_ms * (rtp_timestamp_rate_hz / 1000));
  return Syncable::PlayoutInfo{
      .time = last_playout_time,
      .rtp_timestamp = adjusted_playout_timestamp,
  };
}

void ChannelReceive::SetEstimatedPlayoutNtpTimestamp(NtpTime ntp_time,
                                                     Timestamp time) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  playout_timestamp_ntp_ = ntp_time;
  playout_timestamp_ntp_time_ = time;
}

std::optional<int64_t> ChannelReceive::GetCurrentEstimatedPlayoutNtpTimestampMs(
    int64_t now_ms) const {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  if (!playout_timestamp_ntp_ || !playout_timestamp_ntp_time_)
    return std::nullopt;

  int64_t elapsed_ms = now_ms - playout_timestamp_ntp_time_->ms();
  return playout_timestamp_ntp_->ToMs() + elapsed_ms;
}

bool ChannelReceive::SetBaseMinimumPlayoutDelayMs(int delay_ms) {
  env_.event_log().Log(
      std::make_unique<RtcEventNetEqSetMinimumDelay>(remote_ssrc_, delay_ms));
  MutexLock lock(&neteq_mutex_);
  return neteq_->SetBaseMinimumDelayMs(delay_ms);
}

int ChannelReceive::GetBaseMinimumPlayoutDelayMs() const {
  MutexLock lock(&neteq_mutex_);
  return neteq_->GetBaseMinimumDelayMs();
}

void ChannelReceive::SetMaximumBufferPackets(size_t max_packets) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  MutexLock lock(&neteq_mutex_);
  neteq_->SetMaximumBufferPackets(max_packets);
}

void ChannelReceive::SetFastAccelerate(bool enable) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  MutexLock lock(&neteq_mutex_);
  neteq_->SetFastAccelerate(enable);
}

std::optional<Syncable::Info> ChannelReceive::GetSyncInfo() const {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  Syncable::Info info;
  std::optional<RtpRtcpInterface::SenderReportStats> last_sr =
      rtp_rtcp_->GetSenderReportStats();
  if (!last_sr.has_value()) {
    return std::nullopt;
  }
  info.capture_time_ntp = last_sr->last_remote_ntp_timestamp;
  info.capture_time_rtp = last_sr->last_remote_rtp_timestamp;

  if (!last_received_rtp_timestamp_ || !last_received_rtp_system_time_) {
    return std::nullopt;
  }
  info.latest_received_capture_rtp_timestamp = *last_received_rtp_timestamp_;
  info.latest_receive_time = *last_received_rtp_system_time_;

  uint16_t delay_ms = 0;
  audio_device_module_->PlayoutDelay(&delay_ms);

  MutexLock lock(&neteq_mutex_);
  int jitter_buffer_delay = neteq_->FilteredCurrentDelayMs();
  info.current_delay = TimeDelta::Millis(jitter_buffer_delay + delay_ms);

  return info;
}

int ChannelReceive::GetRtpTimestampRateHz() const {
  MutexLock lock(&neteq_mutex_);
  return GetRtpTimestampRateHzLocked();
}

int ChannelReceive::GetRtpTimestampRateHzLocked() const {
  const std::optional<NetEq::DecoderFormat> decoder_format =
      neteq_->GetCurrentDecoderFormat();

  // Default to the playout frequency if we've not gotten any packets yet.
  // TODO(ossu): Zero clock rate can only happen if we've added an external
  // decoder for a format we don't support internally. Remove once that way of
  // adding decoders is gone!
  // TODO(kwiberg): `decoder_format->sdp_format.clockrate_hz` is an RTP
  // clock rate as it should, but `neteq_->last_output_sample_rate_hz()` is a
  // codec sample rate, which is not always the same thing.
  return (decoder_format && decoder_format->sdp_format.clockrate_hz != 0)
             ? decoder_format->sdp_format.clockrate_hz
             : neteq_->last_output_sample_rate_hz();
}

}  // namespace

std::unique_ptr<ChannelReceiveInterface> CreateChannelReceive(
    const Environment& env,
    NetEqFactory* absl_nullable neteq_factory,
    AudioDeviceModule* absl_nonnull audio_device_module,
    Transport* absl_nonnull rtcp_send_transport,
    uint32_t remote_ssrc,
    size_t jitter_buffer_max_packets,
    bool jitter_buffer_fast_playout,
    int jitter_buffer_min_delay_ms,
    bool enable_non_sender_rtt,
    scoped_refptr<AudioDecoderFactory> decoder_factory,
    scoped_refptr<FrameDecryptorInterface> frame_decryptor,
    const CryptoOptions& crypto_options,
    scoped_refptr<FrameTransformerInterface> frame_transformer,
    absl::AnyInvocable<void(uint32_t ssrc) &&> on_first_packet,
    RtcpEventObserver* rtcp_event_observer,
    PacketRouter* absl_nonnull packet_router,
    uint32_t local_ssrc,
    absl::AnyInvocable<void(const RtpPacketInfos&, Timestamp) const>
        on_frame_delivered_callback) {
  return std::make_unique<ChannelReceive>(
      env, neteq_factory, audio_device_module, rtcp_send_transport, remote_ssrc,
      jitter_buffer_max_packets, jitter_buffer_fast_playout,
      jitter_buffer_min_delay_ms, enable_non_sender_rtt,
      std::move(decoder_factory), std::move(frame_decryptor), crypto_options,
      std::move(frame_transformer), std::move(on_first_packet),
      rtcp_event_observer, packet_router, local_ssrc,
      std::move(on_frame_delivered_callback));
}

}  // namespace voe
}  // namespace webrtc
