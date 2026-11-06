/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
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
#include "rtc_base/checks.h"

@implementation RTC_OBJC_TYPE (RTCNativeVideoEncoder)

- (void)setCallback:(RTCVideoEncoderCallback)callback {
  RTC_DCHECK_NOTREACHED();
}

- (NSInteger)startEncodeWithSettings:(RTC_OBJC_TYPE(RTCVideoEncoderSettings) *)settings
                       numberOfCores:(int)numberOfCores {
  RTC_DCHECK_NOTREACHED();
  return 0;
}

- (NSInteger)releaseEncoder {
  RTC_DCHECK_NOTREACHED();
  return 0;
}

- (NSInteger)encode:(RTC_OBJC_TYPE(RTCVideoFrame) *)frame
    codecSpecificInfo:(nullable id<RTC_OBJC_TYPE(RTCCodecSpecificInfo)>)info
           frameTypes:(NSArray<NSNumber *> *)frameTypes {
  RTC_DCHECK_NOTREACHED();
  return 0;
}

- (int)setBitrate:(uint32_t)bitrateKbit framerate:(uint32_t)framerate {
  RTC_DCHECK_NOTREACHED();
  return 0;
}

- (NSString *)implementationName {
  RTC_DCHECK_NOTREACHED();
  return nil;
}

- (nullable RTC_OBJC_TYPE(RTCVideoEncoderQpThresholds) *)scalingSettings {
  RTC_DCHECK_NOTREACHED();
  return nil;
}

- (NSInteger)resolutionAlignment {
  RTC_DCHECK_NOTREACHED();
  return 1;
}

- (BOOL)applyAlignmentToAllSimulcastLayers {
  RTC_DCHECK_NOTREACHED();
  return NO;
}

- (BOOL)supportsNativeHandle {
  RTC_DCHECK_NOTREACHED();
  return NO;
}
@end
