/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_processing/agc2/speech_level_estimator.h"

#include <memory>

#include "modules/audio_processing/agc2/agc2_common.h"
#include "modules/audio_processing/include/audio_processing.h"
#include "modules/audio_processing/logging/apm_data_dumper.h"
#include "rtc_base/gunit.h"

namespace webrtc {
namespace {

using AdaptiveDigitalConfig =
    AudioProcessing::Config::GainController2::AdaptiveDigital;

// Number of speech frames that the level estimator must observe in order to
// become confident about the estimated level.
constexpr int kNumFramesToConfidence =
    kLevelEstimatorTimeToConfidenceMs / kFrameDurationMs;
static_assert(kNumFramesToConfidence > 0, "");

constexpr float kConvergenceSpeedTestsLevelTolerance = 0.5f;

// Provides the `vad_level` value `num_iterations` times to `level_estimator`.
void RunOnConstantLevel(int num_iterations,
                        float rms_dbfs,
                        float peak_dbfs,
                        float speech_probability,
                        SpeechLevelEstimator& level_estimator) {
  for (int i = 0; i < num_iterations; ++i) {
    level_estimator.Update(rms_dbfs, peak_dbfs, speech_probability);
  }
}

constexpr float kNoSpeechProbability = 0.0f;
constexpr float kLowSpeechProbability = kVadConfidenceThreshold / 2.0f;
constexpr float kMaxSpeechProbability = 1.0f;

// Level estimator with data dumper.
struct TestLevelEstimator {
  explicit TestLevelEstimator(int adjacent_speech_frames_threshold)
      : data_dumper(0),
        estimator(std::make_unique<SpeechLevelEstimator>(
            &data_dumper,
            AdaptiveDigitalConfig{},
            adjacent_speech_frames_threshold)),
        initial_speech_level_dbfs(estimator->level_dbfs()),
        level_rms_dbfs(initial_speech_level_dbfs / 2.0f),
        level_peak_dbfs(initial_speech_level_dbfs / 3.0f) {
    RTC_DCHECK_LT(level_rms_dbfs, level_peak_dbfs);
    RTC_DCHECK_LT(initial_speech_level_dbfs, level_rms_dbfs);
    RTC_DCHECK_GT(level_rms_dbfs - initial_speech_level_dbfs, 5.0f)
        << "Adjust `level_rms_dbfs` so that the difference from the initial "
           "level is wide enough for the tests";
  }
  ApmDataDumper data_dumper;
  std::unique_ptr<SpeechLevelEstimator> estimator;
  const float initial_speech_level_dbfs;
  const float level_rms_dbfs;
  const float level_peak_dbfs;
};

// Checks that the level estimator converges to a constant input speech level.
TEST(GainController2SpeechLevelEstimator, LevelStabilizes) {
  TestLevelEstimator level_estimator(/*adjacent_speech_frames_threshold=*/1);
  RunOnConstantLevel(/*num_iterations=*/kNumFramesToConfidence,
                     level_estimator.level_rms_dbfs,
                     level_estimator.level_peak_dbfs, kMaxSpeechProbability,
                     *level_estimator.estimator);
  const float estimated_level_dbfs = level_estimator.estimator->level_dbfs();
  RunOnConstantLevel(/*num_iterations=*/1, level_estimator.level_rms_dbfs,
                     level_estimator.level_peak_dbfs, kMaxSpeechProbability,
                     *level_estimator.estimator);
  EXPECT_NEAR(level_estimator.estimator->level_dbfs(), estimated_level_dbfs,
              0.1f);
}

// Checks that the level controller does not become confident when too few
// speech frames are observed.
TEST(GainController2SpeechLevelEstimator, IsNotConfident) {
  TestLevelEstimator level_estimator(/*adjacent_speech_frames_threshold=*/1);
  RunOnConstantLevel(/*num_iterations=*/kNumFramesToConfidence / 2,
                     level_estimator.level_rms_dbfs,
                     level_estimator.level_peak_dbfs, kMaxSpeechProbability,
                     *level_estimator.estimator);
  EXPECT_FALSE(level_estimator.estimator->is_confident());
}

// Checks that the level controller becomes confident when enough speech frames
// are observed.
TEST(GainController2SpeechLevelEstimator, IsConfident) {
  TestLevelEstimator level_estimator(/*adjacent_speech_frames_threshold=*/1);
  RunOnConstantLevel(/*num_iterations=*/kNumFramesToConfidence,
                     level_estimator.level_rms_dbfs,
                     level_estimator.level_peak_dbfs, kMaxSpeechProbability,
                     *level_estimator.estimator);
  EXPECT_TRUE(level_estimator.estimator->is_confident());
}

// Checks that the estimated level is not affected by the level of non-speech
// frames.
TEST(GainController2SpeechLevelEstimator, EstimatorIgnoresNonSpeechFrames) {
  TestLevelEstimator level_estimator(/*adjacent_speech_frames_threshold=*/1);
  // Simulate speech.
  RunOnConstantLevel(/*num_iterations=*/kNumFramesToConfidence,
                     level_estimator.level_rms_dbfs,
                     level_estimator.level_peak_dbfs, kMaxSpeechProbability,
                     *level_estimator.estimator);
  const float estimated_level_dbfs = level_estimator.estimator->level_dbfs();
  // Simulate full-scale non-speech.
  RunOnConstantLevel(/*num_iterations=*/kNumFramesToConfidence,
                     /*rms_dbfs=*/0.0f, /*peak_dbfs=*/0.0f,
                     kNoSpeechProbability, *level_estimator.estimator);
  // No estimated level change is expected.
  EXPECT_FLOAT_EQ(level_estimator.estimator->level_dbfs(),
                  estimated_level_dbfs);
}

// Checks the convergence speed of the estimator before it becomes confident.
TEST(GainController2SpeechLevelEstimator, ConvergenceSpeedBeforeConfidence) {
  TestLevelEstimator level_estimator(/*adjacent_speech_frames_threshold=*/1);
  RunOnConstantLevel(/*num_iterations=*/kNumFramesToConfidence,
                     level_estimator.level_rms_dbfs,
                     level_estimator.level_peak_dbfs, kMaxSpeechProbability,
                     *level_estimator.estimator);
  EXPECT_NEAR(level_estimator.estimator->level_dbfs(),
              level_estimator.level_rms_dbfs,
              kConvergenceSpeedTestsLevelTolerance);
}

// Checks the convergence speed of the estimator after it becomes confident.
TEST(GainController2SpeechLevelEstimator, ConvergenceSpeedAfterConfidence) {
  TestLevelEstimator level_estimator(/*adjacent_speech_frames_threshold=*/1);
  // Reach confidence using the initial level estimate.
  RunOnConstantLevel(
      /*num_iterations=*/kNumFramesToConfidence,
      /*rms_dbfs=*/level_estimator.initial_speech_level_dbfs,
      /*peak_dbfs=*/level_estimator.initial_speech_level_dbfs + 6.0f,
      kMaxSpeechProbability, *level_estimator.estimator);
  // No estimate change should occur, but confidence is achieved.
  ASSERT_FLOAT_EQ(level_estimator.estimator->level_dbfs(),
                  level_estimator.initial_speech_level_dbfs);
  ASSERT_TRUE(level_estimator.estimator->is_confident());
  // After confidence.
  constexpr float kConvergenceTimeAfterConfidenceNumFrames = 700;  // 7 seconds.
  static_assert(
      kConvergenceTimeAfterConfidenceNumFrames > kNumFramesToConfidence, "");
  RunOnConstantLevel(
      /*num_iterations=*/kConvergenceTimeAfterConfidenceNumFrames,
      level_estimator.level_rms_dbfs, level_estimator.level_peak_dbfs,
      kMaxSpeechProbability, *level_estimator.estimator);
  EXPECT_NEAR(level_estimator.estimator->level_dbfs(),
              level_estimator.level_rms_dbfs,
              kConvergenceSpeedTestsLevelTolerance);
}

class SpeechLevelEstimatorParametrization
    : public ::testing::TestWithParam<int> {
 protected:
  int adjacent_speech_frames_threshold() const { return GetParam(); }
};

TEST_P(SpeechLevelEstimatorParametrization, DoNotAdaptToShortSpeechSegments) {
  TestLevelEstimator level_estimator(adjacent_speech_frames_threshold());
  const float initial_level = level_estimator.estimator->level_dbfs();
  ASSERT_LT(initial_level, level_estimator.level_peak_dbfs);
  for (int i = 0; i < adjacent_speech_frames_threshold() - 1; ++i) {
    SCOPED_TRACE(i);
    level_estimator.estimator->Update(level_estimator.level_rms_dbfs,
                                      level_estimator.level_peak_dbfs,
                                      kMaxSpeechProbability);
    EXPECT_EQ(initial_level, level_estimator.estimator->level_dbfs());
  }
  level_estimator.estimator->Update(level_estimator.level_rms_dbfs,
                                    level_estimator.level_peak_dbfs,
                                    kLowSpeechProbability);
  EXPECT_EQ(initial_level, level_estimator.estimator->level_dbfs());
}

TEST_P(SpeechLevelEstimatorParametrization, AdaptToEnoughSpeechSegments) {
  TestLevelEstimator level_estimator(adjacent_speech_frames_threshold());
  const float initial_level = level_estimator.estimator->level_dbfs();
  ASSERT_LT(initial_level, level_estimator.level_peak_dbfs);
  for (int i = 0; i < adjacent_speech_frames_threshold(); ++i) {
    level_estimator.estimator->Update(level_estimator.level_rms_dbfs,
                                      level_estimator.level_peak_dbfs,
                                      kMaxSpeechProbability);
  }
  EXPECT_LT(initial_level, level_estimator.estimator->level_dbfs());
}

INSTANTIATE_TEST_SUITE_P(GainController2,
                         SpeechLevelEstimatorParametrization,
                         ::testing::Values(1, 9, 17));

}  // namespace
}  // namespace webrtc
