/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import <Foundation/Foundation.h>

#import "RTCNativeVideoEncoder.h"
#import "base/RTCMacros.h"
#import "base/RTCVideoEncoder.h"

#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"
#include "media/base/codec.h"

// TODO: bugs.webrtc.org/15860 - Remove in favor of the RTCNativeVideoEncoderBuilder
@interface RTC_OBJC_TYPE (RTCWrappedNativeVideoEncoder) : RTC_OBJC_TYPE (RTCNativeVideoEncoder)

- (instancetype)initWithNativeEncoder:(std::unique_ptr<webrtc::VideoEncoder>)encoder;

/* This moves the ownership of the wrapped encoder to the caller. */
- (std::unique_ptr<webrtc::VideoEncoder>)releaseWrappedEncoder;

@end
