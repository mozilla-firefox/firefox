/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/experiments/rfc8888_congestion_control_feedback_experiment.h"

#include <string>

#include "api/field_trials_view.h"

// RFC 8888 congestion control feedback is enabled by default in Chromium
// builds, except on Android.
#if defined(WEBRTC_CHROMIUM_BUILD) && !defined(WEBRTC_ANDROID)
#define RTC_RFC8888_ENABLED_BY_DEFAULT 1
#else
#define RTC_RFC8888_ENABLED_BY_DEFAULT 0
#endif

namespace webrtc {

bool IsRfc8888CongestionControlFeedbackEnabled(
    const FieldTrialsView& field_trials) {
#if RTC_RFC8888_ENABLED_BY_DEFAULT
  return !field_trials.IsDisabled(kRfc8888CongestionControlFeedbackFieldTrial);
#else
  return field_trials.IsEnabled(kRfc8888CongestionControlFeedbackFieldTrial);
#endif
}

std::string Rfc8888CongestionControlFeedbackFieldTrialString(
    const FieldTrialsView& field_trials) {
#if RTC_RFC8888_ENABLED_BY_DEFAULT
  if (field_trials.IsDisabled(kRfc8888CongestionControlFeedbackFieldTrial)) {
    return "";
  }
#endif
  return field_trials.Lookup(kRfc8888CongestionControlFeedbackFieldTrial);
}

}  // namespace webrtc
