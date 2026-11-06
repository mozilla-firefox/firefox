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
#import "RTCVideoDecoderVP9.h"

#include "modules/video_coding/codecs/vp9/include/vp9.h"

@interface RTC_OBJC_TYPE (RTCVideoDecoderVP9Builder)
    : RTC_OBJC_TYPE(RTCNativeVideoDecoder) <RTC_OBJC_TYPE (RTCNativeVideoDecoderBuilder)>
@end

    @implementation RTC_OBJC_TYPE (RTCVideoDecoderVP9Builder)

    - (std::unique_ptr<webrtc::VideoDecoder>)build:(const webrtc::Environment&)env {
      return webrtc::VP9Decoder::Create();
    }

    @end

    @implementation RTC_OBJC_TYPE (RTCVideoDecoderVP9)

    + (id<RTC_OBJC_TYPE(RTCVideoDecoder)>)vp9Decoder {
#if defined(RTC_ENABLE_VP9)
      return [[RTC_OBJC_TYPE(RTCVideoDecoderVP9Builder) alloc] init];
#else
      return nil;
#endif
    }

    + (bool)isSupported {
#if defined(RTC_ENABLE_VP9)
      return true;
#else
      return false;
#endif
    }

    @end
