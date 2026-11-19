/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_EXPERIMENTS_RFC8888_CONGESTION_CONTROL_FEEDBACK_EXPERIMENT_H_
#define RTC_BASE_EXPERIMENTS_RFC8888_CONGESTION_CONTROL_FEEDBACK_EXPERIMENT_H_

#include <string>

#include "absl/strings/string_view.h"
#include "api/field_trials_view.h"

namespace webrtc {

inline constexpr absl::string_view kRfc8888CongestionControlFeedbackFieldTrial =
    "WebRTC-RFC8888CongestionControlFeedback";

// Returns true if support for RFC 8888 congestion control feedback is enabled.
//
// In Chromium builds (except on Android), support is enabled unless the
// "WebRTC-RFC8888CongestionControlFeedback" field trial is explicitly set to
// "Disabled". In all other builds, support is disabled unless the field trial
// is explicitly set to "Enabled". In other words, a field trial that is neither
// "Enabled" nor "Disabled" (e.g. unset, or only carrying parameters such as
// "force_send:true") means enabled in Chromium (non-Android) and disabled
// elsewhere.
//
// Note that this only controls whether RFC 8888 feedback is supported (i.e.
// accepted in an offer, negotiated, and acted upon). Whether RFC 8888 feedback
// is proactively offered is controlled separately by the "offer" parameter of
// the same field trial.
bool IsRfc8888CongestionControlFeedbackEnabled(
    const FieldTrialsView& field_trials);

// Returns the value of the "WebRTC-RFC8888CongestionControlFeedback" field
// trial, for parsing parameters such as "force_send" or "feedback_fraction".
//
// Where the feature is not enabled by default (see above) this is identical to
// `FieldTrialsView::Lookup()`: parameters are honored regardless of whether the
// trial is "Enabled" or "Disabled". Where the feature is enabled by default, an
// empty string is returned if the trial is explicitly "Disabled", so that
// parameters cannot re-activate parts of the feature when it has been turned
// off.
std::string Rfc8888CongestionControlFeedbackFieldTrialString(
    const FieldTrialsView& field_trials);

}  // namespace webrtc

#endif  // RTC_BASE_EXPERIMENTS_RFC8888_CONGESTION_CONTROL_FEEDBACK_EXPERIMENT_H_
