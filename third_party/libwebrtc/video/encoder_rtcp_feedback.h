/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef VIDEO_ENCODER_RTCP_FEEDBACK_H_
#define VIDEO_ENCODER_RTCP_FEEDBACK_H_

#include <cstdint>
#include <vector>

#include "api/environment/environment.h"
#include "api/sequence_checker.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "rtc_base/system/no_unique_address.h"
#include "rtc_base/thread_annotations.h"
#include "video/video_stream_encoder_interface.h"

namespace webrtc {

class VideoStreamEncoderInterface;

// This class passes feedback (such as key frame requests) from the RtpRtcp
// module.
class EncoderRtcpFeedback : public RtcpIntraFrameObserver {
 public:
  EncoderRtcpFeedback(const Environment& env,
                      bool per_layer_keyframes,
                      const std::vector<uint32_t>& ssrcs,
                      VideoStreamEncoderInterface* encoder);
  ~EncoderRtcpFeedback() override = default;

  void OnReceivedIntraFrameRequest(uint32_t ssrc) override;

 private:
  const Environment env_;
  const std::vector<uint32_t> ssrcs_;
  const bool per_layer_keyframes_;
  VideoStreamEncoderInterface* const video_stream_encoder_;

  RTC_NO_UNIQUE_ADDRESS SequenceChecker packet_delivery_queue_;
  std::vector<Timestamp> time_last_packet_delivery_queue_
      RTC_GUARDED_BY(packet_delivery_queue_);

  const TimeDelta min_keyframe_send_interval_;
};

}  // namespace webrtc

#endif  // VIDEO_ENCODER_RTCP_FEEDBACK_H_
