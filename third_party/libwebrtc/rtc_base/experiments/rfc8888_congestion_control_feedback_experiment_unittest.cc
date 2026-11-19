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

#include "api/field_trials.h"
#include "test/create_test_field_trials.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

// Must match the condition in
// rfc8888_congestion_control_feedback_experiment.cc.
#if defined(WEBRTC_CHROMIUM_BUILD) && !defined(WEBRTC_ANDROID)
constexpr bool kEnabledByDefault = true;
#else
constexpr bool kEnabledByDefault = false;
#endif

TEST(Rfc8888CongestionControlFeedbackExperimentTest, EnabledWhenEnabled) {
  FieldTrials field_trials =
      CreateTestFieldTrials("WebRTC-RFC8888CongestionControlFeedback/Enabled/");
  EXPECT_TRUE(IsRfc8888CongestionControlFeedbackEnabled(field_trials));
  EXPECT_EQ(Rfc8888CongestionControlFeedbackFieldTrialString(field_trials),
            "Enabled");
}

TEST(Rfc8888CongestionControlFeedbackExperimentTest,
     EnabledWhenEnabledWithParameters) {
  FieldTrials field_trials = CreateTestFieldTrials(
      "WebRTC-RFC8888CongestionControlFeedback/Enabled,offer:true/");
  EXPECT_TRUE(IsRfc8888CongestionControlFeedbackEnabled(field_trials));
  EXPECT_EQ(Rfc8888CongestionControlFeedbackFieldTrialString(field_trials),
            "Enabled,offer:true");
}

TEST(Rfc8888CongestionControlFeedbackExperimentTest, DisabledWhenDisabled) {
  FieldTrials field_trials = CreateTestFieldTrials(
      "WebRTC-RFC8888CongestionControlFeedback/Disabled,force_send:true/");
  EXPECT_FALSE(IsRfc8888CongestionControlFeedbackEnabled(field_trials));
  // Outside Chromium, parameters are honored regardless of the Enabled/Disabled
  // prefix. In Chromium, an explicit "Disabled" also discards the parameters.
  EXPECT_EQ(Rfc8888CongestionControlFeedbackFieldTrialString(field_trials),
            kEnabledByDefault ? "" : "Disabled,force_send:true");
}

TEST(Rfc8888CongestionControlFeedbackExperimentTest, DefaultWhenUnset) {
  FieldTrials field_trials = CreateTestFieldTrials();
  EXPECT_EQ(IsRfc8888CongestionControlFeedbackEnabled(field_trials),
            kEnabledByDefault);
  EXPECT_EQ(Rfc8888CongestionControlFeedbackFieldTrialString(field_trials), "");
}

TEST(Rfc8888CongestionControlFeedbackExperimentTest,
     DefaultWhenNeitherEnabledNorDisabled) {
  // A trial that only carries parameters is neither "Enabled" nor "Disabled".
  FieldTrials field_trials = CreateTestFieldTrials(
      "WebRTC-RFC8888CongestionControlFeedback/force_send:true/");
  EXPECT_EQ(IsRfc8888CongestionControlFeedbackEnabled(field_trials),
            kEnabledByDefault);
  // Parameters are always honored in this case.
  EXPECT_EQ(Rfc8888CongestionControlFeedbackFieldTrialString(field_trials),
            "force_send:true");
}

}  // namespace
}  // namespace webrtc
