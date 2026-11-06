/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "sdk/objc/native/src/objc_video_frame.h"

#include "rtc_base/time_utils.h"
#include "sdk/objc/native/src/objc_frame_buffer.h"

namespace webrtc {

RTC_OBJC_TYPE(RTCVideoFrame) * ToObjCVideoFrame(const VideoFrame &frame) {
  RTC_OBJC_TYPE(RTCVideoFrame) *videoFrame = [[RTC_OBJC_TYPE(RTCVideoFrame) alloc]
      initWithBuffer:ToObjCVideoFrameBuffer(frame.video_frame_buffer())
            rotation:RTCVideoRotation(frame.rotation())
         timeStampNs:frame.timestamp_us() * rtc::kNumNanosecsPerMicrosec];
  videoFrame.timeStamp = frame.rtp_timestamp();

  return videoFrame;
}

}  // namespace webrtc
