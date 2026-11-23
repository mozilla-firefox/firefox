/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/encoder_rtcp_feedback.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

#include "api/environment/environment.h"
#include "api/sequence_checker.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/video_frame_type.h"
#include "rtc_base/checks.h"
#include "rtc_base/experiments/keyframe_interval_settings.h"
#include "rtc_base/logging.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {

namespace {
constexpr int kMinKeyframeSendIntervalMs = 300;
}  // namespace

EncoderRtcpFeedback::EncoderRtcpFeedback(const Environment& env,
                                         bool per_layer_keyframes,
                                         const std::vector<uint32_t>& ssrcs,
                                         VideoStreamEncoderInterface* encoder)
    : env_(env),
      ssrcs_(ssrcs),
      per_layer_keyframes_(per_layer_keyframes),
      video_stream_encoder_(encoder),
      time_last_packet_delivery_queue_(per_layer_keyframes ? ssrcs.size() : 1,
                                       Timestamp::Zero()),
      min_keyframe_send_interval_(
          TimeDelta::Millis(KeyframeIntervalSettings(env_.field_trials())
                                .MinKeyframeSendIntervalMs()
                                .value_or(kMinKeyframeSendIntervalMs))) {
  RTC_DCHECK(!ssrcs.empty());
  packet_delivery_queue_.Detach();
}

// Called via Call::DeliverRtcp.
void EncoderRtcpFeedback::OnReceivedIntraFrameRequest(uint32_t ssrc) {
  RTC_DCHECK_RUN_ON(&packet_delivery_queue_);
  RTC_DCHECK(std::find(ssrcs_.begin(), ssrcs_.end(), ssrc) != ssrcs_.end());

  auto it = std::find(ssrcs_.begin(), ssrcs_.end(), ssrc);
  if (it == ssrcs_.end()) {
    RTC_LOG(LS_WARNING) << "SSRC " << ssrc << " not found.";
    return;
  }
  size_t ssrc_index =
      per_layer_keyframes_ ? std::distance(ssrcs_.begin(), it) : 0;
  RTC_CHECK_LE(ssrc_index, time_last_packet_delivery_queue_.size());
  const Timestamp now = env_.clock().CurrentTime();
  if (time_last_packet_delivery_queue_[ssrc_index] +
          min_keyframe_send_interval_ >
      now)
    return;

  time_last_packet_delivery_queue_[ssrc_index] = now;

  std::vector<VideoFrameType> layers(ssrcs_.size(),
                                     VideoFrameType::kVideoFrameDelta);
  if (!per_layer_keyframes_) {
    // Always produce key frame for all streams.
    video_stream_encoder_->SendKeyFrame();
  } else {
    // Determine on which layer we ask for key frames.
    layers[ssrc_index] = VideoFrameType::kVideoFrameKey;
    video_stream_encoder_->SendKeyFrame(layers);
  }
}

}  // namespace webrtc
