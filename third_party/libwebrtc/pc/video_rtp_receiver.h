/*
 *  Copyright 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_VIDEO_RTP_RECEIVER_H_
#define PC_VIDEO_RTP_RECEIVER_H_

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
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "media/base/media_channel.h"
#include "pc/jitter_buffer_delay.h"
#include "pc/media_stream_track_proxy.h"
#include "pc/rtp_receiver.h"
#include "pc/video_rtp_track_source.h"
#include "pc/video_track.h"
#include "rtc_base/thread.h"
#include "rtc_base/thread_annotations.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {

class VideoRtpReceiver : public RtpReceiverBase {
 public:
  // An SSRC of 0 will create a receiver that will match the first SSRC it
  // sees. Must be called on signaling thread.
  VideoRtpReceiver(Thread* worker_thread,
                   absl::string_view receiver_id,
                   std::vector<std::string> streams_ids,
                   absl::AnyInvocable<RTCError()> enable_sframe_at_owner,
                   VideoMediaReceiveChannelInterface* media_channel = nullptr,
                   Clock* clock = Clock::GetRealTimeClock());
  // TODO(hbos): Remove this when streams() is removed.
  // https://crbug.com/webrtc/9480
  // This should be PLAN_B_ONLY; but this marking is deferred due to templating
  // issues
  VideoRtpReceiver(
      Thread* worker_thread,
      absl::string_view receiver_id,
      const std::vector<scoped_refptr<MediaStreamInterface>>& streams,
      absl::AnyInvocable<RTCError()> enable_sframe_at_owner,
      VideoMediaReceiveChannelInterface* media_channel = nullptr,
      Clock* clock = Clock::GetRealTimeClock());

  ~VideoRtpReceiver() override;

  scoped_refptr<VideoTrackInterface> video_track() const { return track_; }

  // RtpReceiverInterface implementation
  scoped_refptr<MediaStreamTrackInterface> track() const override {
    return track_;
  }
  scoped_refptr<DtlsTransportInterface> dtls_transport() const override;
  std::vector<std::string> stream_ids() const override;
  std::vector<scoped_refptr<MediaStreamInterface>> streams() const override;
  MediaType media_type() const override { return MediaType::VIDEO; }

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

  // Combines SetMediaChannel, GetSetupForMediaChannel and
  // GetSetupForUnsignaledMediaChannel.
  absl::AnyInvocable<void() &&> GetSetupForMediaChannel(
      std::optional<uint32_t> ssrc,
      VideoMediaReceiveChannelInterface* media_channel);

 private:
  absl::AnyInvocable<void() &&> GetRestartFunctionForMediaChannel(
      std::optional<uint32_t> ssrc) RTC_RUN_ON(&signaling_thread_checker_);
  void GetRestartFunctionForMediaChannel_w(
      std::optional<uint32_t> ssrc,
      MediaSourceInterface::SourceState state) RTC_RUN_ON(worker_thread_);
  void SetSink(VideoSinkInterface<VideoFrame>* sink) RTC_RUN_ON(worker_thread_);
  void SetMediaChannel_w(MediaReceiveChannelInterface* media_channel)
      RTC_RUN_ON(worker_thread_);
  VideoMediaReceiveChannelInterface* video_media_channel() const
      RTC_RUN_ON(worker_thread_);

  // VideoRtpTrackSource::Callback
  void OnGenerateKeyFrame();
  void OnEncodedSinkEnabled(bool enable);

  void SetEncodedSinkEnabled(bool enable) RTC_RUN_ON(worker_thread_);

  class SourceCallback : public VideoRtpTrackSource::Callback {
   public:
    explicit SourceCallback(VideoRtpReceiver* receiver) : receiver_(receiver) {}
    ~SourceCallback() override = default;

   private:
    void OnGenerateKeyFrame() override { receiver_->OnGenerateKeyFrame(); }
    void OnEncodedSinkEnabled(bool enable) override {
      receiver_->OnEncodedSinkEnabled(enable);
    }

    VideoRtpReceiver* const receiver_;
  } source_callback_{this};

  const std::string id_;
  // `source_` is held here to be able to change the state of the source when
  // the VideoRtpReceiver is stopped.
  const scoped_refptr<VideoRtpTrackSource> source_;
  const scoped_refptr<VideoTrackProxyWithInternal<VideoTrack>> track_;
  std::vector<scoped_refptr<MediaStreamInterface>> streams_
      RTC_GUARDED_BY(&signaling_thread_checker_);
  const int attachment_id_;
  scoped_refptr<DtlsTransportInterface> dtls_transport_
      RTC_GUARDED_BY(&signaling_thread_checker_);
  // Stores the minimum jitter buffer delay. Handles caching cases
  // if `SetJitterBufferMinimumDelay` is called before start.
  JitterBufferDelay delay_ RTC_GUARDED_BY(worker_thread_);

  // Records if we should generate a keyframe when `media_channel_` gets set up
  // or switched.
  bool saved_generate_keyframe_ RTC_GUARDED_BY(worker_thread_) = false;
  bool saved_encoded_sink_enabled_ RTC_GUARDED_BY(worker_thread_) = false;
};

}  // namespace webrtc

#endif  // PC_VIDEO_RTP_RECEIVER_H_
