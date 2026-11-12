/*
 *  Copyright 2016 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import "RTCFieldTrials.h"

#include <memory>

#import "base/RTCLogging.h"

#include "api/environment/deprecated_global_field_trials.h"

NSString *const kRTCFieldTrialAudioForceABWENoTWCCKey =
    @"WebRTC-Audio-ABWENoTWCC";
NSString *const kRTCFieldTrialFlexFec03AdvertisedKey =
    @"WebRTC-FlexFEC-03-Advertised";
NSString *const kRTCFieldTrialFlexFec03Key = @"WebRTC-FlexFEC-03";
NSString *const kRTCFieldTrialMinimizeResamplingOnMobileKey =
    @"WebRTC-Audio-MinimizeResamplingOnMobile";
NSString *const kRTCFieldTrialUseNWPathMonitor =
    @"WebRTC-Network-UseNWPathMonitor";
NSString *const kRTCFieldTrialEnabledValue = @"Enabled";

// InitFieldTrialsFromString stores the char*, so the char array must outlive
// the application.
static char *gFieldTrialInitString = nullptr;

void RTCInitFieldTrialDictionary(
    NSDictionary<NSString *, NSString *> *fieldTrials) {
  if (!fieldTrials) {
    RTCLogWarning(@"No fieldTrials provided.");
    return;
  }
  // Assemble the keys and values into the field trial string.
  // We don't perform any extra format checking. That should be done by the
  // underlying WebRTC calls.
  NSMutableString *fieldTrialInitString = [NSMutableString string];
  for (NSString *key in fieldTrials) {
    NSString *fieldTrialEntry =
        [NSString stringWithFormat:@"%@/%@/", key, fieldTrials[key]];
    [fieldTrialInitString appendString:fieldTrialEntry];
  }
  size_t len = fieldTrialInitString.length + 1;
  if (gFieldTrialInitString != nullptr) {
    delete[] gFieldTrialInitString;
  }
  gFieldTrialInitString = new char[len];
  if (![fieldTrialInitString getCString:gFieldTrialInitString
                              maxLength:len
                               encoding:NSUTF8StringEncoding]) {
    RTCLogError(@"Failed to convert field trial string.");
    return;
  }
  webrtc::DeprecatedGlobalFieldTrials::Set(gFieldTrialInitString);
}
