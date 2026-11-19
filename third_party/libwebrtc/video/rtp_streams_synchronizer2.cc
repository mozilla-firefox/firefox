/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/rtp_streams_synchronizer2.h"

#include <cstdint>
#include <optional>

#include "api/environment/environment.h"
#include "api/sequence_checker.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "call/syncable.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/rtp_to_ntp_estimator.h"
#include "rtc_base/task_utils/repeating_task.h"
#include "rtc_base/trace_event.h"
#include "system_wrappers/include/ntp_time.h"
#include "video/stream_synchronization.h"

namespace webrtc {
namespace internal {
namespace {
// Time interval for logging stats.
constexpr int64_t kStatsLogIntervalMs = 10000;
constexpr TimeDelta kSyncInterval = TimeDelta::Millis(1000);

bool UpdateMeasurements(StreamSynchronization::Measurements* stream,
                        const Syncable::Info& info) {
  stream->latest_timestamp = info.latest_received_capture_rtp_timestamp;
  stream->latest_receive_time_ms = info.latest_receive_time.ms();
  return stream->rtp_to_ntp.UpdateMeasurements(info.capture_time_ntp,
                                               info.capture_time_rtp) !=
         RtpToNtpEstimator::kInvalidMeasurement;
}

}  // namespace

RtpStreamsSynchronizer::RtpStreamsSynchronizer(const Environment& env,
                                               TaskQueueBase* network_queue,
                                               Syncable* syncable_video)
    : env_(env),
      network_queue_(network_queue),
      syncable_video_(syncable_video),
      last_stats_log_ms_(env_.clock().TimeInMilliseconds()) {
  RTC_DCHECK(syncable_video);
}

RtpStreamsSynchronizer::~RtpStreamsSynchronizer() {
  RTC_DCHECK_RUN_ON(network_queue_);
  repeating_task_.Stop();
}

void RtpStreamsSynchronizer::ConfigureSync(Syncable* syncable_audio) {
  RTC_DCHECK_RUN_ON(network_queue_);

  // Prevent expensive no-ops.
  if (syncable_audio == syncable_audio_)
    return;

  syncable_audio_ = syncable_audio;
  sync_.reset(nullptr);
  if (!syncable_audio_) {
    repeating_task_.Stop();
    return;
  }

  sync_.reset(
      new StreamSynchronization(syncable_video_->id(), syncable_audio_->id()));

  if (repeating_task_.Running())
    return;

  repeating_task_ =
      RepeatingTaskHandle::DelayedStart(network_queue_, kSyncInterval, [this] {
        UpdateDelay();
        return kSyncInterval;
      });
}

void RtpStreamsSynchronizer::UpdateDelay() {
  RTC_DCHECK_RUN_ON(network_queue_);

  if (!syncable_audio_)
    return;

  RTC_DCHECK(sync_.get());

  bool log_stats = false;
  const int64_t now_ms = env_.clock().TimeInMilliseconds();
  if (now_ms - last_stats_log_ms_ > kStatsLogIntervalMs) {
    last_stats_log_ms_ = now_ms;
    log_stats = true;
  }

  int64_t last_audio_receive_time_ms =
      audio_measurement_.latest_receive_time_ms;
  std::optional<Syncable::Info> audio_info = syncable_audio_->GetInfo();
  if (!audio_info || !UpdateMeasurements(&audio_measurement_, *audio_info)) {
    return;
  }

  if (last_audio_receive_time_ms == audio_measurement_.latest_receive_time_ms) {
    // No new audio packet has been received since last update.
    return;
  }

  int64_t last_video_receive_ms = video_measurement_.latest_receive_time_ms;
  std::optional<Syncable::Info> video_info = syncable_video_->GetInfo();
  if (!video_info || !UpdateMeasurements(&video_measurement_, *video_info)) {
    return;
  }

  if (last_video_receive_ms == video_measurement_.latest_receive_time_ms) {
    // No new video packet has been received since last update.
    return;
  }

  int relative_delay_ms;
  // Calculate how much later or earlier the audio stream is compared to video.
  if (!StreamSynchronization::ComputeRelativeDelay(
          audio_measurement_, video_measurement_, &relative_delay_ms)) {
    return;
  }

  if (log_stats) {
    RTC_LOG(LS_INFO) << "Sync info stats: " << now_ms
                     << ", {ssrc: " << sync_->audio_stream_id() << ", "
                     << "cur_delay: " << audio_info->current_delay
                     << "} {ssrc: " << sync_->video_stream_id() << ", "
                     << "cur_delay: " << video_info->current_delay
                     << "} {relative_delay_ms: " << relative_delay_ms << "} ";
  }

  TRACE_COUNTER1("webrtc", "SyncCurrentVideoDelay",
                 video_info->current_delay.ms());
  TRACE_COUNTER1("webrtc", "SyncCurrentAudioDelay",
                 audio_info->current_delay.ms());
  TRACE_COUNTER1("webrtc", "SyncRelativeDelay", relative_delay_ms);

  int target_audio_delay_ms = 0;
  int target_video_delay_ms = video_info->current_delay.ms();
  // Calculate the necessary extra audio delay and desired total video
  // delay to get the streams in sync.
  if (!sync_->ComputeDelays(relative_delay_ms, audio_info->current_delay.ms(),
                            &target_audio_delay_ms, &target_video_delay_ms)) {
    return;
  }

  if (log_stats) {
    RTC_LOG(LS_INFO) << "Sync delay stats: " << now_ms
                     << ", {ssrc: " << sync_->audio_stream_id() << ", "
                     << "target_delay_ms: " << target_audio_delay_ms
                     << "} {ssrc: " << sync_->video_stream_id() << ", "
                     << "target_delay_ms: " << target_video_delay_ms << "} ";
  }

  if (!syncable_audio_->SetMinimumPlayoutDelay(
          TimeDelta::Millis(target_audio_delay_ms))) {
    sync_->ReduceAudioDelay();
  }
  if (!syncable_video_->SetMinimumPlayoutDelay(
          TimeDelta::Millis(target_video_delay_ms))) {
    sync_->ReduceVideoDelay();
  }
}

// TODO(https://bugs.webrtc.org/7065): Move RtpToNtpEstimator out of
// RtpStreamsSynchronizer and into respective receive stream to always populate
// the estimated playout timestamp.
bool RtpStreamsSynchronizer::GetStreamSyncOffsetInMs(
    uint32_t rtp_timestamp,
    int64_t render_time_ms,
    int64_t* video_playout_ntp_ms,
    int64_t* stream_offset_ms,
    double* estimated_freq_khz) const {
  RTC_DCHECK_RUN_ON(network_queue_);

  if (!syncable_audio_)
    return false;

  std::optional<Syncable::PlayoutInfo> audio =
      syncable_audio_->GetPlayoutRtpTimestamp();
  if (!audio.has_value()) {
    return false;
  }

  NtpTime latest_audio_ntp =
      audio_measurement_.rtp_to_ntp.Estimate(audio->rtp_timestamp);
  if (!latest_audio_ntp.Valid()) {
    return false;
  }
  int64_t latest_audio_ntp_ms = latest_audio_ntp.ToMs();

  syncable_audio_->SetEstimatedPlayoutNtpTimestamp(latest_audio_ntp,
                                                   audio->time);

  NtpTime latest_video_ntp =
      video_measurement_.rtp_to_ntp.Estimate(rtp_timestamp);
  if (!latest_video_ntp.Valid()) {
    return false;
  }
  int64_t latest_video_ntp_ms = latest_video_ntp.ToMs();

  // Current audio ntp.
  Timestamp now = env_.clock().CurrentTime();
  latest_audio_ntp_ms += (now - audio->time).ms();

  // Remove video playout delay.
  int64_t time_to_render_ms = render_time_ms - now.ms();
  if (time_to_render_ms > 0) {
    latest_video_ntp_ms -= time_to_render_ms;
  }

  *video_playout_ntp_ms = latest_video_ntp_ms;
  *stream_offset_ms = latest_audio_ntp_ms - latest_video_ntp_ms;
  *estimated_freq_khz = video_measurement_.rtp_to_ntp.EstimatedFrequencyKhz();
  return true;
}

}  // namespace internal
}  // namespace webrtc
