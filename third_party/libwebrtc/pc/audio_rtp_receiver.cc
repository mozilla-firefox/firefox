/*
 *  Copyright 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/audio_rtp_receiver.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/dtls_transport_interface.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/rtc_error.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "media/base/media_channel.h"
#include "pc/audio_track.h"
#include "pc/media_stream_track_proxy.h"
#include "pc/remote_audio_source.h"
#include "pc/rtp_receiver.h"
#include "rtc_base/checks.h"
#include "rtc_base/thread.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {

AudioRtpReceiver::AudioRtpReceiver(
    Thread* worker_thread,
    absl::string_view receiver_id,
    std::vector<std::string> stream_ids,
    absl::AnyInvocable<RTCError()> enable_sframe_at_owner,
    VoiceMediaReceiveChannelInterface* voice_channel,
    Clock* clock)
    : AudioRtpReceiver(worker_thread,
                       receiver_id,
                       CreateStreamsFromIds(std::move(stream_ids)),
                       std::move(enable_sframe_at_owner),
                       voice_channel,
                       RemoteAudioSource::OnAudioChannelGoneAction::kSurvive,
                       clock) {}

AudioRtpReceiver::AudioRtpReceiver(
    Thread* worker_thread,
    absl::string_view receiver_id,
    const std::vector<scoped_refptr<MediaStreamInterface>>& streams,
    bool is_unified_plan,
    VoiceMediaReceiveChannelInterface* media_channel,
    Clock* clock)
    : AudioRtpReceiver(worker_thread,
                       receiver_id,
                       streams,
                       nullptr,
                       media_channel,
                       RemoteAudioSource::OnAudioChannelGoneAction::kEnd,
                       clock) {
  RTC_DCHECK(!is_unified_plan);
}

AudioRtpReceiver::AudioRtpReceiver(
    Thread* worker_thread,
    absl::string_view receiver_id,
    const std::vector<scoped_refptr<MediaStreamInterface>>& streams,
    VoiceMediaReceiveChannelInterface* media_channel,
    Clock* clock)
    : AudioRtpReceiver(worker_thread,
                       receiver_id,
                       streams,
                       nullptr,
                       media_channel,
                       RemoteAudioSource::OnAudioChannelGoneAction::kSurvive,
                       clock) {}

AudioRtpReceiver::AudioRtpReceiver(
    Thread* worker_thread,
    absl::string_view receiver_id,
    const std::vector<scoped_refptr<MediaStreamInterface>>& streams,
    absl::AnyInvocable<RTCError()> enable_sframe_at_owner,
    VoiceMediaReceiveChannelInterface* voice_channel,
    RemoteAudioSource::OnAudioChannelGoneAction source_gone_action,
    Clock* clock)
    : RtpReceiverBase(worker_thread, std::move(enable_sframe_at_owner), clock),
      id_(receiver_id),
      source_(make_ref_counted<RemoteAudioSource>(worker_thread,
                                                  source_gone_action)),
      track_(AudioTrackProxyWithInternal<AudioTrack>::Create(
          Thread::Current(),
          AudioTrack::Create(receiver_id, source_))),
      media_channel_(voice_channel),
      cached_track_enabled_(track_->internal()->enabled()),
      attachment_id_(GenerateUniqueId()),
      worker_thread_safety_(PendingTaskSafetyFlag::CreateDetachedInactive()) {
  RTC_DCHECK(worker_thread_);
  RTC_DCHECK(track_->GetSource()->remote());
  track_->RegisterObserver(this);
  track_->GetSource()->RegisterAudioObserver(this);
  SetStreams(streams);
}

AudioRtpReceiver::~AudioRtpReceiver() {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  RTC_DCHECK(!media_channel_);

  track_->GetSource()->UnregisterAudioObserver(this);
  track_->UnregisterObserver(this);
}

void AudioRtpReceiver::OnChanged() {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  const bool enabled = track_->internal()->enabled();
  if (cached_track_enabled_ == enabled)
    return;
  cached_track_enabled_ = enabled;
  worker_thread_->PostTask(SafeTask(worker_thread_safety_,
                                    [this, enabled, volume = cached_volume_]() {
                                      RTC_DCHECK_RUN_ON(worker_thread_);
                                      Reconfigure(enabled, volume);
                                    }));
}

void AudioRtpReceiver::SetOutputVolume_w(double volume) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_DCHECK_GE(volume, 0.0);
  RTC_DCHECK_LE(volume, 10.0);

  if (!media_channel_)
    return;

  signaled_ssrc_ ? media_channel_->SetOutputVolume(*signaled_ssrc_, volume)
                 : media_channel_->SetDefaultOutputVolume(volume);
}

void AudioRtpReceiver::OnSetVolume(double volume) {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  RTC_DCHECK_GE(volume, 0);
  RTC_DCHECK_LE(volume, 10);

  bool track_enabled = track_->internal()->enabled();
  // Update the cached_volume_ even when stopped, to allow clients to set
  // the volume before starting/restarting, eg see crbug.com/1272566.
  cached_volume_ = volume;
  // When the track is disabled, the volume of the source, which is the
  // corresponding WebRtc Voice Engine channel will be 0. So we do not
  // allow setting the volume to the source when the track is disabled.
  if (!track_enabled)
    return;

  // SetVolume() does not return a result, so there's no need to block.
  worker_thread_->PostTask(SafeTask(worker_thread_safety_, [this, volume]() {
    RTC_DCHECK_RUN_ON(worker_thread_);
    SetOutputVolume_w(volume);
  }));
}

scoped_refptr<DtlsTransportInterface> AudioRtpReceiver::dtls_transport() const {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  return dtls_transport_;
}

std::vector<std::string> AudioRtpReceiver::stream_ids() const {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  std::vector<std::string> stream_ids(streams_.size());
  for (size_t i = 0; i < streams_.size(); ++i)
    stream_ids[i] = streams_[i]->id();
  return stream_ids;
}

std::vector<scoped_refptr<MediaStreamInterface>> AudioRtpReceiver::streams()
    const {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  return streams_;
}

RtpParameters AudioRtpReceiver::GetParameters() const {
  RTC_DCHECK_RUN_ON(worker_thread_);
  if (!media_channel_)
    return RtpParameters();
  auto current_ssrc = ssrc();
  return current_ssrc.has_value()
             ? media_channel_->GetRtpReceiverParameters(current_ssrc.value())
             : media_channel_->GetDefaultRtpReceiveParameters();
}

void AudioRtpReceiver::Stop() {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  source_->SetState(MediaSourceInterface::kEnded);
  track_->internal()->set_ended();
}

absl::AnyInvocable<void() &&>
AudioRtpReceiver::GetRestartFunctionForMediaChannel(
    std::optional<uint32_t> ssrc) {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  signaled_ssrc_s_ = ssrc;
  bool enabled = track_->internal()->enabled();
  MediaSourceInterface::SourceState state = source_->state();
  source_->SetState(MediaSourceInterface::kLive);
  // Like `enabled`, the volume is a snapshot of the state when the restart
  // function is created.
  return [this, ssrc = std::move(ssrc), enabled, state,
          volume = cached_volume_]() mutable {
    RTC_DCHECK_RUN_ON(worker_thread_);
    GetRestartFunctionForMediaChannel_w(std::move(ssrc), enabled, state,
                                        volume);
  };
}

void AudioRtpReceiver::GetRestartFunctionForMediaChannel_w(
    std::optional<uint32_t> ssrc,
    bool track_enabled,
    MediaSourceInterface::SourceState state,
    double volume) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  if (!media_channel_)
    return;  // Can't restart.

  // Make sure the safety flag is marked as `alive` for cases where the media
  // channel was provided via the ctor and not an explicit call to
  // SetMediaChannel.
  worker_thread_safety_->SetAlive();

  if (state != MediaSourceInterface::kInitializing) {
    if (signaled_ssrc_ == ssrc)
      return;
    source_->Stop(media_channel_, signaled_ssrc_);
  }

  signaled_ssrc_ = std::move(ssrc);
  source_->Start(media_channel_, signaled_ssrc_);
  if (signaled_ssrc_) {
    media_channel_->SetBaseMinimumPlayoutDelayMs(*signaled_ssrc_,
                                                 delay_.GetMs());
  }

  Reconfigure(track_enabled, volume);
}

absl::AnyInvocable<void() &&> AudioRtpReceiver::GetSetupForMediaChannel(
    uint32_t ssrc) {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  return GetRestartFunctionForMediaChannel(ssrc);
}

absl::AnyInvocable<void() &&>
AudioRtpReceiver::GetSetupForUnsignaledMediaChannel() {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  return GetRestartFunctionForMediaChannel(std::nullopt);
}

MediaReceiveChannelInterface* AudioRtpReceiver::media_channel() const {
  RTC_DCHECK_RUN_ON(worker_thread_);
  return media_channel_;
}

void AudioRtpReceiver::set_stream_ids(std::vector<std::string> stream_ids) {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  SetStreams(CreateStreamsFromIds(std::move(stream_ids)));
}

void AudioRtpReceiver::set_transport(
    scoped_refptr<DtlsTransportInterface> dtls_transport) {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  dtls_transport_ = std::move(dtls_transport);
}

void AudioRtpReceiver::SetStreams(
    const std::vector<scoped_refptr<MediaStreamInterface>>& streams) {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  // Remove remote track from any streams that are going away.
  for (const auto& existing_stream : streams_) {
    bool removed = true;
    for (const auto& stream : streams) {
      if (existing_stream->id() == stream->id()) {
        RTC_DCHECK_EQ(existing_stream.get(), stream.get());
        removed = false;
        break;
      }
    }
    if (removed) {
      existing_stream->RemoveTrack(audio_track());
    }
  }
  // Add remote track to any streams that are new.
  for (const auto& stream : streams) {
    bool added = true;
    for (const auto& existing_stream : streams_) {
      if (stream->id() == existing_stream->id()) {
        RTC_DCHECK_EQ(stream.get(), existing_stream.get());
        added = false;
        break;
      }
    }
    if (added) {
      stream->AddTrack(audio_track());
    }
  }
  streams_ = streams;
}

void AudioRtpReceiver::Reconfigure(bool track_enabled, double volume) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_DCHECK(media_channel_);

  SetOutputVolume_w(track_enabled ? volume : 0);

  if (signaled_ssrc_ && frame_decryptor_) {
    // Reattach the frame decryptor if we were reconfigured.
    media_channel_->SetFrameDecryptor(*signaled_ssrc_, frame_decryptor_);
  }

  if (frame_transformer_) {
    media_channel_->SetDepacketizerToDecoderFrameTransformer(
        signaled_ssrc_.value_or(0), frame_transformer_);
  }
}

void AudioRtpReceiver::SetJitterBufferMinimumDelay(
    std::optional<double> delay_seconds) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  delay_.Set(delay_seconds);
  if (media_channel_ && signaled_ssrc_)
    media_channel_->SetBaseMinimumPlayoutDelayMs(*signaled_ssrc_,
                                                 delay_.GetMs());
}

void AudioRtpReceiver::SetMediaChannel(
    MediaReceiveChannelInterface* media_channel) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_DCHECK(media_channel == nullptr ||
             media_channel->media_type() == media_type());
  if (!media_channel && media_channel_)
    SetOutputVolume_w(0.0);

  media_channel ? worker_thread_safety_->SetAlive()
                : worker_thread_safety_->SetNotAlive();
  media_channel_ =
      static_cast<VoiceMediaReceiveChannelInterface*>(media_channel);
}

}  // namespace webrtc
