/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 *
 */

#import <Foundation/Foundation.h>

#import "RTCMacros.h"
#import "RTCNativeVideoDecoder.h"
#import "RTCNativeVideoDecoderBuilder+Native.h"
#import "RTCVideoDecoderVP8.h"

#include "modules/video_coding/codecs/vp8/include/vp8.h"

@interface RTC_OBJC_TYPE (RTCVideoDecoderVP8Builder)
    : RTC_OBJC_TYPE(RTCNativeVideoDecoder) <RTC_OBJC_TYPE (RTCNativeVideoDecoderBuilder)>
@end

    @implementation RTC_OBJC_TYPE (RTCVideoDecoderVP8Builder)

    - (std::unique_ptr<webrtc::VideoDecoder>)build:(const webrtc::Environment&)env {
      return webrtc::CreateVp8Decoder(env);
    }

    @end

    @implementation RTC_OBJC_TYPE (RTCVideoDecoderVP8)

    + (id<RTC_OBJC_TYPE(RTCVideoDecoder)>)vp8Decoder {
      return [[RTC_OBJC_TYPE(RTCVideoDecoderVP8Builder) alloc] init];
    }

    @end
