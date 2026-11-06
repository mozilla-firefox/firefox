/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
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
#import "RTCVideoDecoderAV1.h"

#include "modules/video_coding/codecs/av1/dav1d_decoder.h"

@interface RTC_OBJC_TYPE (RTCVideoDecoderAV1Builder)
    : RTC_OBJC_TYPE(RTCNativeVideoDecoder) <RTC_OBJC_TYPE (RTCNativeVideoDecoderBuilder)>
@end

    @implementation RTC_OBJC_TYPE (RTCVideoDecoderAV1Builder)

    - (std::unique_ptr<webrtc::VideoDecoder>)build:(const webrtc::Environment&)env {
      return webrtc::CreateDav1dDecoder();
    }

    @end

    @implementation RTC_OBJC_TYPE (RTCVideoDecoderAV1)

    + (id<RTC_OBJC_TYPE(RTCVideoDecoder)>)av1Decoder {
      return [[RTC_OBJC_TYPE(RTCVideoDecoderAV1Builder) alloc] init];
    }

    @end
