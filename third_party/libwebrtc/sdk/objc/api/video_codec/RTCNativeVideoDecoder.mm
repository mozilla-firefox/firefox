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

#import "RTCNativeVideoDecoder.h"
#import "base/RTCMacros.h"
#import "helpers/NSString+StdString.h"
#include "rtc_base/checks.h"

@implementation RTC_OBJC_TYPE (RTCNativeVideoDecoder)

- (void)setCallback:(RTCVideoDecoderCallback)callback {
  RTC_DCHECK_NOTREACHED();
}

- (NSInteger)startDecodeWithNumberOfCores:(int)numberOfCores {
  RTC_DCHECK_NOTREACHED();
  return 0;
}

- (NSInteger)releaseDecoder {
  RTC_DCHECK_NOTREACHED();
  return 0;
}

// TODO: bugs.webrtc.org/15444 - Remove obsolete missingFrames param.
- (NSInteger)decode:(RTC_OBJC_TYPE(RTCEncodedImage) *)encodedImage
        missingFrames:(BOOL)missingFrames
    codecSpecificInfo:(nullable id<RTC_OBJC_TYPE(RTCCodecSpecificInfo)>)info
         renderTimeMs:(int64_t)renderTimeMs {
  RTC_DCHECK_NOTREACHED();
  return 0;
}

- (NSString *)implementationName {
  RTC_DCHECK_NOTREACHED();
  return nil;
}

@end
