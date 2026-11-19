/*
 *  Copyright 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/video_rtp_receiver.h"

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
#include "api/video/recordable_encoded_frame.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "media/base/media_channel.h"
#include "pc/media_stream_track_proxy.h"
#include "pc/rtp_receiver.h"
#include "pc/video_rtp_track_source.h"
#include "pc/video_track.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/thread.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {

VideoRtpReceiver::VideoRtpReceiver(
    Thread* worker_thread,
    absl::string_view receiver_id,
    std::vector<std::string> stream_ids,
    absl::AnyInvocable<RTCError()> enable_sframe_at_owner,
    VideoMediaReceiveChannelInterface* media_channel,
    Clock* clock)
    : VideoRtpReceiver(worker_thread,
                       receiver_id,
                       CreateStreamsFromIds(std::move(stream_ids)),
                       std::move(enable_sframe_at_owner),
                       media_channel,
                       clock) {}

VideoRtpReceiver::VideoRtpReceiver(
    Thread* worker_thread,
    absl::string_view receiver_id,
    const std::vector<scoped_refptr<MediaStreamInterface>>& streams,
    absl::AnyInvocable<RTCError()> enable_sframe_at_owner,
    VideoMediaReceiveChannelInterface* media_channel,
    Clock* clock)
    : RtpReceiverBase(worker_thread,
                      std::move(enable_sframe_at_owner),
                      media_channel,
                      clock),
      id_(receiver_id),
      source_(make_ref_counted<VideoRtpTrackSource>(&source_callback_)),
      track_(VideoTrackProxyWithInternal<VideoTrack>::Create(
          Thread::Current(),
          worker_thread,
          VideoTrack::Create(receiver_id, source_, worker_thread))),
      attachment_id_(GenerateUniqueId()) {
  RTC_DCHECK(worker_thread_);
  SetStreams(streams);
  RTC_DCHECK_EQ(source_->state(), MediaSourceInterface::kInitializing);
}

VideoRtpReceiver::~VideoRtpReceiver() {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
}

std::vector<std::string> VideoRtpReceiver::stream_ids() const {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  std::vector<std::string> stream_ids(streams_.size());
  for (size_t i = 0; i < streams_.size(); ++i)
    stream_ids[i] = streams_[i]->id();
  return stream_ids;
}

scoped_refptr<DtlsTransportInterface> VideoRtpReceiver::dtls_transport() const {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  return dtls_transport_;
}

std::vector<scoped_refptr<MediaStreamInterface>> VideoRtpReceiver::streams()
    const {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  return streams_;
}

RtpParameters VideoRtpReceiver::GetParameters() const {
  RTC_DCHECK_RUN_ON(worker_thread_);
  VideoMediaReceiveChannelInterface* video_channel = video_media_channel();
  if (!video_channel)
    return RtpParameters();
  auto current_ssrc = ssrc();
  return current_ssrc.has_value()
             ? video_channel->GetRtpReceiverParameters(current_ssrc.value())
             : video_channel->GetDefaultRtpReceiveParameters();
}

void VideoRtpReceiver::Stop() {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  RtpReceiverBase::Stop();
  source_->SetState(MediaSourceInterface::kEnded);
  track_->internal()->set_ended();
}

absl::AnyInvocable<void() &&>
VideoRtpReceiver::GetRestartFunctionForMediaChannel(
    std::optional<uint32_t> ssrc) {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  signaled_ssrc_s_ = ssrc;
  MediaSourceInterface::SourceState state = source_->state();
  source_->SetState(MediaSourceInterface::kLive);
  return [this, ssrc = std::move(ssrc), state]() mutable {
    RTC_DCHECK_RUN_ON(worker_thread_);
    GetRestartFunctionForMediaChannel_w(std::move(ssrc), state);
  };
}

void VideoRtpReceiver::GetRestartFunctionForMediaChannel_w(
    std::optional<uint32_t> ssrc,
    MediaSourceInterface::SourceState state) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  VideoMediaReceiveChannelInterface* video_channel = video_media_channel();
  if (!video_channel) {
    return;  // Can't restart.
  }

  const bool encoded_sink_enabled = saved_encoded_sink_enabled_;

  if (state != MediaSourceInterface::kInitializing) {
    if (ssrc == signaled_ssrc_)
      return;

    // Disconnect from a previous ssrc.
    SetSink(nullptr);

    if (encoded_sink_enabled)
      SetEncodedSinkEnabled(false);
  }

  // Set up the new ssrc.
  signaled_ssrc_ = std::move(ssrc);
  SetSink(source_->sink());
  if (encoded_sink_enabled) {
    SetEncodedSinkEnabled(true);
  }

  if (frame_transformer_ && video_channel) {
    video_channel->SetDepacketizerToDecoderFrameTransformer(
        signaled_ssrc_.value_or(0), frame_transformer_);
  }

  if (video_channel && signaled_ssrc_) {
    if (frame_decryptor_) {
      video_channel->SetFrameDecryptor(*signaled_ssrc_, frame_decryptor_);
    }

    video_channel->SetBaseMinimumPlayoutDelayMs(*signaled_ssrc_,
                                                delay_.GetMs());
  }
}

void VideoRtpReceiver::SetSink(VideoSinkInterface<VideoFrame>* sink) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  if (signaled_ssrc_) {
    video_media_channel()->SetSink(*signaled_ssrc_, sink);
  } else {
    video_media_channel()->SetDefaultSink(sink);
  }
}

absl::AnyInvocable<void() &&> VideoRtpReceiver::GetSetupForMediaChannel(
    uint32_t ssrc) {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  return GetRestartFunctionForMediaChannel(ssrc);
}

absl::AnyInvocable<void() &&>
VideoRtpReceiver::GetSetupForUnsignaledMediaChannel() {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  return GetRestartFunctionForMediaChannel(std::nullopt);
}

VideoMediaReceiveChannelInterface* VideoRtpReceiver::video_media_channel()
    const {
  MediaReceiveChannelInterface* channel = media_channel();
  return channel ? channel->AsVideoReceiveChannel() : nullptr;
}

void VideoRtpReceiver::set_stream_ids(std::vector<std::string> stream_ids) {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  SetStreams(CreateStreamsFromIds(std::move(stream_ids)));
}

void VideoRtpReceiver::set_transport(
    scoped_refptr<DtlsTransportInterface> dtls_transport) {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  dtls_transport_ = std::move(dtls_transport);
}

void VideoRtpReceiver::SetStreams(
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
      existing_stream->RemoveTrack(video_track());
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
      stream->AddTrack(video_track());
    }
  }
  streams_ = streams;
}

void VideoRtpReceiver::SetJitterBufferMinimumDelay(
    std::optional<double> delay_seconds) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  delay_.Set(delay_seconds);
  VideoMediaReceiveChannelInterface* video_channel = video_media_channel();
  if (video_channel && signaled_ssrc_) {
    video_channel->SetBaseMinimumPlayoutDelayMs(*signaled_ssrc_,
                                                delay_.GetMs());
  }
}

void VideoRtpReceiver::SetMediaChannel(
    MediaReceiveChannelInterface* media_channel) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_DCHECK(media_channel == nullptr ||
             media_channel->media_type() == media_type());

  SetMediaChannel_w(media_channel);
}

void VideoRtpReceiver::SetMediaChannel_w(
    MediaReceiveChannelInterface* media_channel) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  MediaReceiveChannelInterface* old_media_channel = this->media_channel();
  if (media_channel == old_media_channel)
    return;

  if (!media_channel) {
    SetSink(nullptr);
  }

  bool encoded_sink_enabled = saved_encoded_sink_enabled_;
  if (encoded_sink_enabled && old_media_channel) {
    // Turn off the old sink, if any.
    SetEncodedSinkEnabled(false);
  }

  set_media_channel(media_channel);

  if (media_channel) {
    VideoMediaReceiveChannelInterface* video_channel = video_media_channel();
    if (saved_generate_keyframe_) {
      // TODO(bugs.webrtc.org/8694): Stop using 0 to mean unsignalled SSRC
      video_channel->RequestRecvKeyFrame(signaled_ssrc_.value_or(0));
      saved_generate_keyframe_ = false;
    }
    if (encoded_sink_enabled) {
      SetEncodedSinkEnabled(true);
    }
    if (frame_transformer_) {
      video_channel->SetDepacketizerToDecoderFrameTransformer(
          signaled_ssrc_.value_or(0), frame_transformer_);
    }
  }

  if (!media_channel)
    source_->ClearCallback();
}

absl::AnyInvocable<void() &&> VideoRtpReceiver::GetSetupForMediaChannel(
    std::optional<uint32_t> ssrc,
    VideoMediaReceiveChannelInterface* media_channel) {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  RTC_DCHECK(media_channel);
  signaled_ssrc_s_ = ssrc;
  MediaSourceInterface::SourceState state = source_->state();
  source_->SetState(MediaSourceInterface::kLive);
  return [this, ssrc = std::move(ssrc), media_channel, state]() mutable {
    RTC_DCHECK_RUN_ON(worker_thread_);
    SetMediaChannel_w(media_channel);
    GetRestartFunctionForMediaChannel_w(std::move(ssrc), state);
  };
}

void VideoRtpReceiver::OnGenerateKeyFrame() {
  RTC_DCHECK_RUN_ON(worker_thread_);
  VideoMediaReceiveChannelInterface* video_channel = video_media_channel();
  if (!video_channel) {
    RTC_LOG(LS_ERROR)
        << "VideoRtpReceiver::OnGenerateKeyFrame: No video channel exists.";
    return;
  }
  // TODO(bugs.webrtc.org/8694): Stop using 0 to mean unsignalled SSRC
  video_channel->RequestRecvKeyFrame(signaled_ssrc_.value_or(0));
  // We need to remember to request generation of a new key frame if the media
  // channel changes, because there's no feedback whether the keyframe
  // generation has completed on the channel.
  saved_generate_keyframe_ = true;
}

void VideoRtpReceiver::OnEncodedSinkEnabled(bool enable) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  SetEncodedSinkEnabled(enable);
  // Always save the latest state of the callback in case the media_channel_
  // changes.
  saved_encoded_sink_enabled_ = enable;
}

void VideoRtpReceiver::SetEncodedSinkEnabled(bool enable) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  VideoMediaReceiveChannelInterface* video_channel = video_media_channel();
  if (!video_channel)
    return;

  // TODO(bugs.webrtc.org/8694): Stop using 0 to mean unsignalled SSRC
  const auto ssrc = signaled_ssrc_.value_or(0);

  if (enable) {
    video_channel->SetRecordableEncodedFrameCallback(
        ssrc, [source = source_](const RecordableEncodedFrame& frame) {
          source->BroadcastRecordableEncodedFrame(frame);
        });
  } else {
    video_channel->ClearRecordableEncodedFrameCallback(ssrc);
  }
}

}  // namespace webrtc
