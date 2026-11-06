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

#import "RTCWrappedNativeVideoEncoder.h"
#import "base/RTCMacros.h"

@implementation RTC_OBJC_TYPE (RTCWrappedNativeVideoEncoder) {
  std::unique_ptr<webrtc::VideoEncoder> _wrappedEncoder;
}

- (instancetype)initWithNativeEncoder:(std::unique_ptr<webrtc::VideoEncoder>)encoder {
  if (self = [super init]) {
    _wrappedEncoder = std::move(encoder);
  }

  return self;
}

- (std::unique_ptr<webrtc::VideoEncoder>)releaseWrappedEncoder {
  return std::move(_wrappedEncoder);
}

@end
