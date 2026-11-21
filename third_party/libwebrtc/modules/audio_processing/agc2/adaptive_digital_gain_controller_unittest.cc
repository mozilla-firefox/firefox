/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_processing/agc2/adaptive_digital_gain_controller.h"

#include <algorithm>
#include <memory>

#include "common_audio/include/audio_util.h"
#include "modules/audio_processing/agc2/agc2_common.h"
#include "modules/audio_processing/agc2/vector_float_frame.h"
#include "modules/audio_processing/include/audio_processing.h"
#include "modules/audio_processing/logging/apm_data_dumper.h"
#include "rtc_base/gunit.h"

namespace webrtc {
namespace {

constexpr int kMono = 1;
constexpr int kStereo = 2;
constexpr int kFrameLen10ms8kHz = 80;
constexpr int kFrameLen10ms48kHz = 480;

constexpr float kMaxSpeechProbability = 1.0f;

// Constants used in place of estimated noise levels.
constexpr float kNoNoiseDbfs = kMinLevelDbfs;
constexpr float kWithNoiseDbfs = -20.0f;

// Number of additional frames to process in the tests to ensure that the tested
// adaptation processes have converged.
constexpr int kNumExtraFrames = 10;

constexpr float GetMaxGainChangePerFrameDb(
    float max_gain_change_db_per_second) {
  return max_gain_change_db_per_second * kFrameDurationMs / 1000.0f;
}

using AdaptiveDigitalConfig =
    AudioProcessing::Config::GainController2::AdaptiveDigital;

constexpr AdaptiveDigitalConfig kDefaultConfig{};

// Helper to create initialized `AdaptiveDigitalGainController` objects.
struct GainApplierHelper {
  GainApplierHelper(const AdaptiveDigitalConfig& config,
                    int adjacent_speech_frames_threshold)
      : apm_data_dumper(0),
        gain_applier(std::make_unique<AdaptiveDigitalGainController>(
            &apm_data_dumper,
            config,
            adjacent_speech_frames_threshold)) {}
  ApmDataDumper apm_data_dumper;
  std::unique_ptr<AdaptiveDigitalGainController> gain_applier;
};

// Returns a `FrameInfo` sample to simulate noiseless speech detected with
// maximum probability and with level, headroom and limiter envelope chosen
// so that the resulting gain equals the default initial adaptive digital gain
// i.e., no gain adaptation is expected.
AdaptiveDigitalGainController::FrameInfo GetFrameInfoToNotAdapt(
    const AdaptiveDigitalConfig& config) {
  AdaptiveDigitalGainController::FrameInfo info;
  info.speech_probability = kMaxSpeechProbability;
  info.speech_level_dbfs = -config.initial_gain_db - config.headroom_db;
  info.speech_level_reliable = true;
  info.noise_rms_dbfs = kNoNoiseDbfs;
  info.headroom_db = config.headroom_db;
  info.limiter_envelope_dbfs = -2.0f;
  return info;
}

TEST(GainController2AdaptiveDigitalGainControllerTest,
     GainApplierShouldNotCrash) {
  GainApplierHelper helper(kDefaultConfig, kAdjacentSpeechFramesThreshold);
  // Make one call with reasonable audio level values and settings.
  VectorFloatFrame fake_audio(kStereo, kFrameLen10ms48kHz, 10000.0f);
  helper.gain_applier->Process(GetFrameInfoToNotAdapt(kDefaultConfig),
                               fake_audio.float_frame_view());
}

// Checks that the maximum allowed gain is applied.
TEST(GainController2AdaptiveDigitalGainControllerTest, MaxGainApplied) {
  constexpr int kNumFramesToAdapt =
      static_cast<int>(kDefaultConfig.max_gain_db /
                       GetMaxGainChangePerFrameDb(
                           kDefaultConfig.max_gain_change_db_per_second)) +
      kNumExtraFrames;
  constexpr AdaptiveDigitalConfig kConfig = AdaptiveDigitalConfig{
      // Increase from the default in order to reach the maximum gain.
      .max_output_noise_level_dbfs = -40.0f};
  GainApplierHelper helper(kConfig, kAdjacentSpeechFramesThreshold);
  AdaptiveDigitalGainController::FrameInfo info =
      GetFrameInfoToNotAdapt(kConfig);
  info.speech_level_dbfs = -60.0f;
  float applied_gain;
  for (int i = 0; i < kNumFramesToAdapt; ++i) {
    VectorFloatFrame fake_audio(kMono, kFrameLen10ms8kHz, 1.0f);
    helper.gain_applier->Process(info, fake_audio.float_frame_view());
    applied_gain = fake_audio.float_frame_view().channel(0)[0];
  }
  const float applied_gain_db = 20.0f * std::log10f(applied_gain);
  EXPECT_NEAR(applied_gain_db, kDefaultConfig.max_gain_db, 0.1f);
}

TEST(GainController2AdaptiveDigitalGainControllerTest, GainDoesNotChangeFast) {
  GainApplierHelper helper(kDefaultConfig, kAdjacentSpeechFramesThreshold);

  constexpr float initial_level_dbfs = -25.0f;
  constexpr float kMaxGainChangeDbPerFrame =
      GetMaxGainChangePerFrameDb(kDefaultConfig.max_gain_change_db_per_second);
  constexpr int kNumFramesToAdapt =
      static_cast<int>(initial_level_dbfs / kMaxGainChangeDbPerFrame) +
      kNumExtraFrames;

  const float max_change_per_frame_linear = DbToRatio(kMaxGainChangeDbPerFrame);

  float last_gain_linear = 1.f;
  for (int i = 0; i < kNumFramesToAdapt; ++i) {
    SCOPED_TRACE(i);
    VectorFloatFrame fake_audio(kMono, kFrameLen10ms8kHz, 1.0f);
    AdaptiveDigitalGainController::FrameInfo info =
        GetFrameInfoToNotAdapt(kDefaultConfig);
    info.speech_level_dbfs = initial_level_dbfs;
    helper.gain_applier->Process(info, fake_audio.float_frame_view());
    float current_gain_linear = fake_audio.float_frame_view().channel(0)[0];
    EXPECT_LE(std::abs(current_gain_linear - last_gain_linear),
              max_change_per_frame_linear);
    last_gain_linear = current_gain_linear;
  }

  // Check that the same is true when gain decreases as well.
  for (int i = 0; i < kNumFramesToAdapt; ++i) {
    SCOPED_TRACE(i);
    VectorFloatFrame fake_audio(kMono, kFrameLen10ms8kHz, 1.0f);
    AdaptiveDigitalGainController::FrameInfo info =
        GetFrameInfoToNotAdapt(kDefaultConfig);
    info.speech_level_dbfs = 0.f;
    helper.gain_applier->Process(info, fake_audio.float_frame_view());
    float current_gain_linear = fake_audio.float_frame_view().channel(0)[0];
    EXPECT_LE(std::abs(current_gain_linear - last_gain_linear),
              max_change_per_frame_linear);
    last_gain_linear = current_gain_linear;
  }
}

TEST(GainController2AdaptiveDigitalGainControllerTest, GainIsRampedInAFrame) {
  GainApplierHelper helper(kDefaultConfig, kAdjacentSpeechFramesThreshold);

  constexpr float initial_level_dbfs = -25.0f;

  VectorFloatFrame fake_audio(kMono, kFrameLen10ms48kHz, 1.0f);
  AdaptiveDigitalGainController::FrameInfo info =
      GetFrameInfoToNotAdapt(kDefaultConfig);
  info.speech_level_dbfs = initial_level_dbfs;
  helper.gain_applier->Process(info, fake_audio.float_frame_view());
  float maximal_difference = 0.0f;
  float current_value = 1.0f * DbToRatio(kDefaultConfig.initial_gain_db);
  for (const auto& x : fake_audio.float_frame_view().channel(0)) {
    const float difference = std::abs(x - current_value);
    maximal_difference = std::max(maximal_difference, difference);
    current_value = x;
  }

  const float max_change_per_frame_linear = DbToRatio(
      GetMaxGainChangePerFrameDb(kDefaultConfig.max_gain_change_db_per_second));
  const float max_change_per_sample =
      max_change_per_frame_linear / kFrameLen10ms48kHz;

  EXPECT_LE(maximal_difference, max_change_per_sample);
}

TEST(GainController2AdaptiveDigitalGainControllerTest, NoiseLimitsGain) {
  GainApplierHelper helper(kDefaultConfig, kAdjacentSpeechFramesThreshold);

  constexpr float initial_level_dbfs = -25.0f;
  constexpr int num_initial_frames =
      kDefaultConfig.initial_gain_db /
      GetMaxGainChangePerFrameDb(kDefaultConfig.max_gain_change_db_per_second);
  constexpr int num_frames = 50;

  ASSERT_GT(kWithNoiseDbfs, kDefaultConfig.max_output_noise_level_dbfs)
      << "kWithNoiseDbfs is too low";

  for (int i = 0; i < num_initial_frames + num_frames; ++i) {
    VectorFloatFrame fake_audio(kMono, kFrameLen10ms48kHz, 1.0f);
    AdaptiveDigitalGainController::FrameInfo info =
        GetFrameInfoToNotAdapt(kDefaultConfig);
    info.speech_level_dbfs = initial_level_dbfs;
    info.noise_rms_dbfs = kWithNoiseDbfs;
    helper.gain_applier->Process(info, fake_audio.float_frame_view());

    // Wait so that the adaptive gain applier has time to lower the gain.
    if (i > num_initial_frames) {
      const float maximal_ratio =
          *std::max_element(fake_audio.float_frame_view().channel(0).begin(),
                            fake_audio.float_frame_view().channel(0).end());

      EXPECT_NEAR(maximal_ratio, 1.0f, 0.001f);
    }
  }
}

TEST(GainController2AdaptiveDigitalGainControllerTest,
     CanHandlePositiveSpeechLevels) {
  GainApplierHelper helper(kDefaultConfig, kAdjacentSpeechFramesThreshold);

  // Make one call with positive audio level values and settings.
  VectorFloatFrame fake_audio(kStereo, kFrameLen10ms48kHz, 10000.0f);
  AdaptiveDigitalGainController::FrameInfo info =
      GetFrameInfoToNotAdapt(kDefaultConfig);
  info.speech_level_dbfs = 5.0f;
  helper.gain_applier->Process(info, fake_audio.float_frame_view());
}

TEST(GainController2AdaptiveDigitalGainControllerTest, AudioLevelLimitsGain) {
  GainApplierHelper helper(kDefaultConfig, kAdjacentSpeechFramesThreshold);

  constexpr float initial_level_dbfs = -25.0f;
  constexpr int num_initial_frames =
      kDefaultConfig.initial_gain_db /
      GetMaxGainChangePerFrameDb(kDefaultConfig.max_gain_change_db_per_second);
  constexpr int num_frames = 50;

  ASSERT_GT(kWithNoiseDbfs, kDefaultConfig.max_output_noise_level_dbfs)
      << "kWithNoiseDbfs is too low";

  for (int i = 0; i < num_initial_frames + num_frames; ++i) {
    VectorFloatFrame fake_audio(kMono, kFrameLen10ms48kHz, 1.0f);
    AdaptiveDigitalGainController::FrameInfo info =
        GetFrameInfoToNotAdapt(kDefaultConfig);
    info.speech_level_dbfs = initial_level_dbfs;
    info.limiter_envelope_dbfs = 1.0f;
    info.speech_level_reliable = false;
    helper.gain_applier->Process(info, fake_audio.float_frame_view());

    // Wait so that the adaptive gain applier has time to lower the gain.
    if (i > num_initial_frames) {
      const float maximal_ratio =
          *std::max_element(fake_audio.float_frame_view().channel(0).begin(),
                            fake_audio.float_frame_view().channel(0).end());

      EXPECT_NEAR(maximal_ratio, 1.0f, 0.001f);
    }
  }
}

class AdaptiveDigitalGainControllerParametrizedTest
    : public ::testing::TestWithParam<int> {
 protected:
  int adjacent_speech_frames_threshold() const { return GetParam(); }
};

TEST_P(AdaptiveDigitalGainControllerParametrizedTest,
       DoNotIncreaseGainWithTooFewSpeechFrames) {
  GainApplierHelper helper(kDefaultConfig, adjacent_speech_frames_threshold());

  // Lower the speech level so that the target gain will be increased.
  AdaptiveDigitalGainController::FrameInfo info =
      GetFrameInfoToNotAdapt(kDefaultConfig);
  info.speech_level_dbfs -= 12.0f;

  float prev_gain = 0.0f;
  for (int i = 0; i < adjacent_speech_frames_threshold(); ++i) {
    SCOPED_TRACE(i);
    VectorFloatFrame audio(kMono, kFrameLen10ms48kHz, 1.0f);
    helper.gain_applier->Process(info, audio.float_frame_view());
    const float gain = audio.float_frame_view().channel(0)[0];
    if (i > 0) {
      EXPECT_EQ(prev_gain, gain);  // No gain increase applied.
    }
    prev_gain = gain;
  }
}

TEST_P(AdaptiveDigitalGainControllerParametrizedTest,
       IncreaseGainWithEnoughSpeechFrames) {
  GainApplierHelper helper(kDefaultConfig, adjacent_speech_frames_threshold());

  // Lower the speech level so that the target gain will be increased.
  AdaptiveDigitalGainController::FrameInfo info =
      GetFrameInfoToNotAdapt(kDefaultConfig);
  info.speech_level_dbfs -= 12.0f;

  float prev_gain = 0.0f;
  for (int i = 0; i < adjacent_speech_frames_threshold(); ++i) {
    SCOPED_TRACE(i);
    VectorFloatFrame audio(kMono, kFrameLen10ms48kHz, 1.0f);
    helper.gain_applier->Process(info, audio.float_frame_view());
    prev_gain = audio.float_frame_view().channel(0)[0];
  }

  // Process one more speech frame.
  VectorFloatFrame audio(kMono, kFrameLen10ms48kHz, 1.0f);
  helper.gain_applier->Process(info, audio.float_frame_view());

  // An increased gain has been applied.
  EXPECT_GT(audio.float_frame_view().channel(0)[0], prev_gain);
}

INSTANTIATE_TEST_SUITE_P(
    GainController2,
    AdaptiveDigitalGainControllerParametrizedTest,
    ::testing::Values(1, 7, 31, kAdjacentSpeechFramesThreshold));

}  // namespace
}  // namespace webrtc
