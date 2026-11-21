/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/experiments/normalize_simulcast_size_experiment.h"

#include "test/explicit_key_value_config.h"
#include "test/gtest.h"

namespace webrtc {

using test::ExplicitKeyValueConfig;

TEST(NormalizeSimulcastSizeExperimentTest, GetExponent) {
  ExplicitKeyValueConfig field_trials(
      "WebRTC-NormalizeSimulcastResolution/Enabled-2/");
  EXPECT_EQ(2,
            NormalizeSimulcastSizeExperiment::GetBase2Exponent(field_trials));
}

TEST(NormalizeSimulcastSizeExperimentTest, GetExponentWithTwoParameters) {
  ExplicitKeyValueConfig field_trials(
      "WebRTC-NormalizeSimulcastResolution/Enabled-3-4/");
  EXPECT_EQ(3,
            NormalizeSimulcastSizeExperiment::GetBase2Exponent(field_trials));
}

TEST(NormalizeSimulcastSizeExperimentTest, GetExponentFailsIfNotEnabled) {
  ExplicitKeyValueConfig field_trials(
      "WebRTC-NormalizeSimulcastResolution/Disabled/");
  EXPECT_FALSE(
      NormalizeSimulcastSizeExperiment::GetBase2Exponent(field_trials));
}

TEST(NormalizeSimulcastSizeExperimentTest,
     GetExponentFailsForInvalidFieldTrial) {
  ExplicitKeyValueConfig field_trials(
      "WebRTC-NormalizeSimulcastResolution/Enabled-invalid/");
  EXPECT_FALSE(
      NormalizeSimulcastSizeExperiment::GetBase2Exponent(field_trials));
}

TEST(NormalizeSimulcastSizeExperimentTest,
     GetExponentFailsForNegativeOutOfBoundValue) {
  // Supported range: [0, 5].
  ExplicitKeyValueConfig field_trials(
      "WebRTC-NormalizeSimulcastResolution/Enabled--1/");
  EXPECT_FALSE(
      NormalizeSimulcastSizeExperiment::GetBase2Exponent(field_trials));
}

TEST(NormalizeSimulcastSizeExperimentTest,
     GetExponentFailsForPositiveOutOfBoundValue) {
  // Supported range: [0, 5].
  ExplicitKeyValueConfig field_trials(
      "WebRTC-NormalizeSimulcastResolution/Enabled-6/");
  EXPECT_FALSE(
      NormalizeSimulcastSizeExperiment::GetBase2Exponent(field_trials));
}

}  // namespace webrtc
