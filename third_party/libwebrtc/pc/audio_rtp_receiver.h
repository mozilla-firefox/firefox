/*
 *  Copyright 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_AUDIO_RTP_RECEIVER_H_
#define PC_AUDIO_RTP_RECEIVER_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/dtls_transport_interface.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/rtc_error.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "media/base/media_channel.h"
#include "pc/audio_track.h"
#include "pc/jitter_buffer_delay.h"
#include "pc/media_stream_track_proxy.h"
#include "pc/remote_audio_source.h"
#include "pc/rtp_receiver.h"
#include "rtc_base/thread.h"
#include "rtc_base/thread_annotations.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {

class AudioRtpReceiver : public ObserverInterface,
                         public AudioSourceInterface::AudioObserver,
                         public RtpReceiverBase {
 public:
  // The constructor supports optionally passing the voice channel to the
  // instance at construction time without having to call `SetMediaChannel()`
  // on the worker thread straight after construction.
  // However, when using that, the assumption is that right after construction,
  // a call to either `GetSetupForUnsignaledMediaChannel` or
  // `GetSetupForMediaChannel` will be made, which will internally start the
  // source on the worker thread.
  AudioRtpReceiver(Thread* worker_thread,
                   absl::string_view receiver_id,
                   std::vector<std::string> stream_ids,
                   absl::AnyInvocable<RTCError()> enable_sframe_at_owner,
                   VoiceMediaReceiveChannelInterface* voice_channel = nullptr,
                   Clock* clock = Clock::GetRealTimeClock());
  // Note: This is a PlanB-only constructor.
  // TODO(https://crbug.com/webrtc/9480): Remove this when streams() is removed.
  // This should be PLAN_B_ONLY; but this marking is deferred due to templating
  // issues
  AudioRtpReceiver(
      Thread* worker_thread,
      absl::string_view receiver_id,
      const std::vector<scoped_refptr<MediaStreamInterface>>& streams,
      bool is_unified_plan,  // must always be set to false.
      VoiceMediaReceiveChannelInterface* media_channel = nullptr,
      Clock* clock = Clock::GetRealTimeClock());
  // TODO(https://crbug.com/webrtc/9480): Remove this when streams() is removed.
  // This should be PLAN_B_ONLY; but this marking is deferred due to templating
  // issues
  AudioRtpReceiver(
      Thread* worker_thread,
      absl::string_view receiver_id,
      const std::vector<scoped_refptr<MediaStreamInterface>>& streams,
      VoiceMediaReceiveChannelInterface* media_channel = nullptr,
      Clock* clock = Clock::GetRealTimeClock());
  ~AudioRtpReceiver() override;

  // ObserverInterface implementation
  void OnChanged() override;

  // AudioSourceInterface::AudioObserver implementation
  void OnSetVolume(double volume) override;

  scoped_refptr<AudioTrackInterface> audio_track() const { return track_; }

  // RtpReceiverInterface implementation
  scoped_refptr<MediaStreamTrackInterface> track() const override {
    return track_;
  }
  scoped_refptr<DtlsTransportInterface> dtls_transport() const override;
  std::vector<std::string> stream_ids() const override;
  std::vector<scoped_refptr<MediaStreamInterface>> streams() const override;

  webrtc::MediaType media_type() const override {
    return webrtc::MediaType::AUDIO;
  }

  std::string id() const override { return id_; }

  RtpParameters GetParameters() const override;

  // RtpReceiverInternal implementation.
  void Stop() override;
  absl::AnyInvocable<void() &&> GetSetupForMediaChannel(uint32_t ssrc) override;
  absl::AnyInvocable<void() &&> GetSetupForUnsignaledMediaChannel() override;
  void set_stream_ids(std::vector<std::string> stream_ids) override;
  void set_transport(
      scoped_refptr<DtlsTransportInterface> dtls_transport) override;
  void SetStreams(
      const std::vector<scoped_refptr<MediaStreamInterface>>& streams) override;

  void SetJitterBufferMinimumDelay(
      std::optional<double> delay_seconds) override;

  void SetMediaChannel(MediaReceiveChannelInterface* media_channel) override;

  int AttachmentId() const override { return attachment_id_; }

 private:
  AudioRtpReceiver(
      Thread* worker_thread,
      absl::string_view receiver_id,
      const std::vector<scoped_refptr<MediaStreamInterface>>& streams,
      absl::AnyInvocable<RTCError()> enable_sframe_at_owner,
      VoiceMediaReceiveChannelInterface* media_channel,
      RemoteAudioSource::OnAudioChannelGoneAction source_gone_action,
      Clock* clock = Clock::GetRealTimeClock());

  absl::AnyInvocable<void() &&> GetRestartFunctionForMediaChannel(
      std::optional<uint32_t> ssrc) RTC_RUN_ON(&signaling_thread_checker_);
  void GetRestartFunctionForMediaChannel_w(
      std::optional<uint32_t> ssrc,
      bool track_enabled,
      MediaSourceInterface::SourceState state,
      double volume) RTC_RUN_ON(worker_thread_);
  void Reconfigure(bool track_enabled, double volume)
      RTC_RUN_ON(worker_thread_);
  void SetOutputVolume_w(double volume) RTC_RUN_ON(worker_thread_);
  VoiceMediaReceiveChannelInterface* voice_media_channel() const
      RTC_RUN_ON(worker_thread_);

  const std::string id_;
  const scoped_refptr<RemoteAudioSource> source_;
  const scoped_refptr<AudioTrackProxyWithInternal<AudioTrack>> track_;
  std::vector<scoped_refptr<MediaStreamInterface>> streams_
      RTC_GUARDED_BY(&signaling_thread_checker_);
  bool cached_track_enabled_ RTC_GUARDED_BY(&signaling_thread_checker_);
  double cached_volume_ RTC_GUARDED_BY(&signaling_thread_checker_) = 1.0;
  const int attachment_id_;
  scoped_refptr<DtlsTransportInterface> dtls_transport_
      RTC_GUARDED_BY(&signaling_thread_checker_);
  // Stores and updates the playout delay. Handles caching cases if
  // `SetJitterBufferMinimumDelay` is called before start.
  JitterBufferDelay delay_ RTC_GUARDED_BY(worker_thread_);
};

}  // namespace webrtc

#endif  // PC_AUDIO_RTP_RECEIVER_H_
