/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_processing/gain_controller2.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
#include <tuple>

#include "api/array_view.h"
#include "modules/audio_processing/agc2/agc2_testing_common.h"
#include "modules/audio_processing/audio_buffer.h"
#include "modules/audio_processing/test/audio_buffer_tools.h"
#include "modules/audio_processing/test/bitexactness_tools.h"
#include "rtc_base/checks.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace test {
namespace {

using ::testing::Eq;
using ::testing::Optional;

using Agc2Config = AudioProcessing::Config::GainController2;
using InputVolumeControllerConfig = InputVolumeController::Config;

// Sets all the samples in `ab` to `value`.
void SetAudioBufferSamples(float value, AudioBuffer& ab) {
  for (size_t k = 0; k < ab.num_channels(); ++k) {
    std::fill(ab.channels()[k], ab.channels()[k] + ab.num_frames(), value);
  }
}

float RunAgc2WithConstantInput(GainController2& agc2,
                               float input_level,
                               int num_frames,
                               int sample_rate_hz,
                               int num_channels = 1,
                               int applied_initial_volume = 0) {
  const int num_samples = rtc::CheckedDivExact(sample_rate_hz, 100);
  AudioBuffer ab(sample_rate_hz, num_channels, sample_rate_hz, num_channels,
                 sample_rate_hz, num_channels);

  // Give time to the level estimator to converge.
  for (int i = 0; i < num_frames + 1; ++i) {
    SetAudioBufferSamples(input_level, ab);
    const auto applied_volume = agc2.recommended_input_volume();
    agc2.Analyze(applied_volume.value_or(applied_initial_volume), ab);

    agc2.Process(/*speech_probability=*/absl::nullopt,
                 /*input_volume_changed=*/false, &ab);
  }

  // Return the last sample from the last processed frame.
  return ab.channels()[0][num_samples - 1];
}

std::unique_ptr<GainController2> CreateAgc2FixedDigitalMode(
    float fixed_gain_db,
    int sample_rate_hz) {
  Agc2Config config;
  config.adaptive_digital.enabled = false;
  config.fixed_digital.gain_db = fixed_gain_db;
  EXPECT_TRUE(GainController2::Validate(config));
  return std::make_unique<GainController2>(
      config, InputVolumeControllerConfig{}, sample_rate_hz,
      /*num_channels=*/1,
      /*use_internal_vad=*/true);
}

constexpr InputVolumeControllerConfig kTestInputVolumeControllerConfig{
    .clipped_level_min = 20,
    .clipped_level_step = 30,
    .clipped_ratio_threshold = 0.4,
    .clipped_wait_frames = 50,
    .enable_clipping_predictor = true,
    .target_range_max_dbfs = -6,
    .target_range_min_dbfs = -70,
    .update_input_volume_wait_frames = 100,
    .speech_probability_threshold = 0.9,
    .speech_ratio_threshold = 1,
};

}  // namespace

TEST(GainController2, CheckDefaultConfig) {
  Agc2Config config;
  EXPECT_TRUE(GainController2::Validate(config));
}

TEST(GainController2, CheckFixedDigitalConfig) {
  Agc2Config config;
  // Attenuation is not allowed.
  config.fixed_digital.gain_db = -5.0f;
  EXPECT_FALSE(GainController2::Validate(config));
  // No gain is allowed.
  config.fixed_digital.gain_db = 0.0f;
  EXPECT_TRUE(GainController2::Validate(config));
  // Positive gain is allowed.
  config.fixed_digital.gain_db = 15.0f;
  EXPECT_TRUE(GainController2::Validate(config));
}

TEST(GainController2, CheckHeadroomDb) {
  Agc2Config config;
  config.adaptive_digital.headroom_db = -1.0f;
  EXPECT_FALSE(GainController2::Validate(config));
  config.adaptive_digital.headroom_db = 0.0f;
  EXPECT_TRUE(GainController2::Validate(config));
  config.adaptive_digital.headroom_db = 5.0f;
  EXPECT_TRUE(GainController2::Validate(config));
}

TEST(GainController2, CheckMaxGainDb) {
  Agc2Config config;
  config.adaptive_digital.max_gain_db = -1.0f;
  EXPECT_FALSE(GainController2::Validate(config));
  config.adaptive_digital.max_gain_db = 0.0f;
  EXPECT_FALSE(GainController2::Validate(config));
  config.adaptive_digital.max_gain_db = 5.0f;
  EXPECT_TRUE(GainController2::Validate(config));
}

TEST(GainController2, CheckInitialGainDb) {
  Agc2Config config;
  config.adaptive_digital.initial_gain_db = -1.0f;
  EXPECT_FALSE(GainController2::Validate(config));
  config.adaptive_digital.initial_gain_db = 0.0f;
  EXPECT_TRUE(GainController2::Validate(config));
  config.adaptive_digital.initial_gain_db = 5.0f;
  EXPECT_TRUE(GainController2::Validate(config));
}

TEST(GainController2, CheckAdaptiveDigitalMaxGainChangeSpeedConfig) {
  Agc2Config config;
  config.adaptive_digital.max_gain_change_db_per_second = -1.0f;
  EXPECT_FALSE(GainController2::Validate(config));
  config.adaptive_digital.max_gain_change_db_per_second = 0.0f;
  EXPECT_FALSE(GainController2::Validate(config));
  config.adaptive_digital.max_gain_change_db_per_second = 5.0f;
  EXPECT_TRUE(GainController2::Validate(config));
}

TEST(GainController2, CheckAdaptiveDigitalMaxOutputNoiseLevelConfig) {
  Agc2Config config;
  config.adaptive_digital.max_output_noise_level_dbfs = 5.0f;
  EXPECT_FALSE(GainController2::Validate(config));
  config.adaptive_digital.max_output_noise_level_dbfs = 0.0f;
  EXPECT_TRUE(GainController2::Validate(config));
  config.adaptive_digital.max_output_noise_level_dbfs = -5.0f;
  EXPECT_TRUE(GainController2::Validate(config));
}

TEST(GainController2,
     CheckGetRecommendedInputVolumeWhenInputVolumeControllerNotEnabled) {
  constexpr float kHighInputLevel = 32767.0f;
  constexpr float kLowInputLevel = 1000.0f;
  constexpr int kInitialInputVolume = 100;
  constexpr int kNumChannels = 2;
  constexpr int kNumFrames = 5;
  constexpr int kSampleRateHz = 16000;

  Agc2Config config;
  config.input_volume_controller.enabled = false;

  auto gain_controller = std::make_unique<GainController2>(
      config, InputVolumeControllerConfig{}, kSampleRateHz, kNumChannels,
      /*use_internal_vad=*/true);

  EXPECT_FALSE(gain_controller->recommended_input_volume().has_value());

  // Run AGC for a signal with no clipping or detected speech.
  RunAgc2WithConstantInput(*gain_controller, kLowInputLevel, kNumFrames,
                           kSampleRateHz, kNumChannels, kInitialInputVolume);

  EXPECT_FALSE(gain_controller->recommended_input_volume().has_value());

  // Run AGC for a signal with clipping.
  RunAgc2WithConstantInput(*gain_controller, kHighInputLevel, kNumFrames,
                           kSampleRateHz, kNumChannels, kInitialInputVolume);

  EXPECT_FALSE(gain_controller->recommended_input_volume().has_value());
}

TEST(
    GainController2,
    CheckGetRecommendedInputVolumeWhenInputVolumeControllerNotEnabledAndSpecificConfigUsed) {
  constexpr float kHighInputLevel = 32767.0f;
  constexpr float kLowInputLevel = 1000.0f;
  constexpr int kInitialInputVolume = 100;
  constexpr int kNumChannels = 2;
  constexpr int kNumFrames = 5;
  constexpr int kSampleRateHz = 16000;

  Agc2Config config;
  config.input_volume_controller.enabled = false;

  auto gain_controller = std::make_unique<GainController2>(
      config, kTestInputVolumeControllerConfig, kSampleRateHz, kNumChannels,
      /*use_internal_vad=*/true);

  EXPECT_FALSE(gain_controller->recommended_input_volume().has_value());

  // Run AGC for a signal with no clipping or detected speech.
  RunAgc2WithConstantInput(*gain_controller, kLowInputLevel, kNumFrames,
                           kSampleRateHz, kNumChannels, kInitialInputVolume);

  EXPECT_FALSE(gain_controller->recommended_input_volume().has_value());

  // Run AGC for a signal with clipping.
  RunAgc2WithConstantInput(*gain_controller, kHighInputLevel, kNumFrames,
                           kSampleRateHz, kNumChannels, kInitialInputVolume);

  EXPECT_FALSE(gain_controller->recommended_input_volume().has_value());
}

TEST(GainController2,
     CheckGetRecommendedInputVolumeWhenInputVolumeControllerEnabled) {
  constexpr float kHighInputLevel = 32767.0f;
  constexpr float kLowInputLevel = 1000.0f;
  constexpr int kInitialInputVolume = 100;
  constexpr int kNumChannels = 2;
  constexpr int kNumFrames = 5;
  constexpr int kSampleRateHz = 16000;

  Agc2Config config;
  config.input_volume_controller.enabled = true;
  config.adaptive_digital.enabled = true;

  auto gain_controller = std::make_unique<GainController2>(
      config, InputVolumeControllerConfig{}, kSampleRateHz, kNumChannels,
      /*use_internal_vad=*/true);

  EXPECT_FALSE(gain_controller->recommended_input_volume().has_value());

  // Run AGC for a signal with no clipping or detected speech.
  RunAgc2WithConstantInput(*gain_controller, kLowInputLevel, kNumFrames,
                           kSampleRateHz, kNumChannels, kInitialInputVolume);

  EXPECT_TRUE(gain_controller->recommended_input_volume().has_value());

  // Run AGC for a signal with clipping.
  RunAgc2WithConstantInput(*gain_controller, kHighInputLevel, kNumFrames,
                           kSampleRateHz, kNumChannels, kInitialInputVolume);

  EXPECT_TRUE(gain_controller->recommended_input_volume().has_value());
}

TEST(
    GainController2,
    CheckGetRecommendedInputVolumeWhenInputVolumeControllerEnabledAndSpecificConfigUsed) {
  constexpr float kHighInputLevel = 32767.0f;
  constexpr float kLowInputLevel = 1000.0f;
  constexpr int kInitialInputVolume = 100;
  constexpr int kNumChannels = 2;
  constexpr int kNumFrames = 5;
  constexpr int kSampleRateHz = 16000;

  Agc2Config config;
  config.input_volume_controller.enabled = true;
  config.adaptive_digital.enabled = true;

  auto gain_controller = std::make_unique<GainController2>(
      config, kTestInputVolumeControllerConfig, kSampleRateHz, kNumChannels,
      /*use_internal_vad=*/true);

  EXPECT_FALSE(gain_controller->recommended_input_volume().has_value());

  // Run AGC for a signal with no clipping or detected speech.
  RunAgc2WithConstantInput(*gain_controller, kLowInputLevel, kNumFrames,
                           kSampleRateHz, kNumChannels, kInitialInputVolume);

  EXPECT_TRUE(gain_controller->recommended_input_volume().has_value());

  // Run AGC for a signal with clipping.
  RunAgc2WithConstantInput(*gain_controller, kHighInputLevel, kNumFrames,
                           kSampleRateHz, kNumChannels, kInitialInputVolume);

  EXPECT_TRUE(gain_controller->recommended_input_volume().has_value());
}

// Checks that the default config is applied.
TEST(GainController2, ApplyDefaultConfig) {
  auto gain_controller2 = std::make_unique<GainController2>(
      Agc2Config{}, InputVolumeControllerConfig{},
      /*sample_rate_hz=*/16000, /*num_channels=*/2,
      /*use_internal_vad=*/true);
  EXPECT_TRUE(gain_controller2.get());
}

TEST(GainController2FixedDigital, GainShouldChangeOnSetGain) {
  constexpr float kInputLevel = 1000.0f;
  constexpr size_t kNumFrames = 5;
  constexpr size_t kSampleRateHz = 8000;
  constexpr float kGain0Db = 0.0f;
  constexpr float kGain20Db = 20.0f;

  auto agc2_fixed = CreateAgc2FixedDigitalMode(kGain0Db, kSampleRateHz);

  // Signal level is unchanged with 0 db gain.
  EXPECT_FLOAT_EQ(RunAgc2WithConstantInput(*agc2_fixed, kInputLevel, kNumFrames,
                                           kSampleRateHz),
                  kInputLevel);

  // +20 db should increase signal by a factor of 10.
  agc2_fixed->SetFixedGainDb(kGain20Db);
  EXPECT_FLOAT_EQ(RunAgc2WithConstantInput(*agc2_fixed, kInputLevel, kNumFrames,
                                           kSampleRateHz),
                  kInputLevel * 10);
}

TEST(GainController2FixedDigital, ChangeFixedGainShouldBeFastAndTimeInvariant) {
  // Number of frames required for the fixed gain controller to adapt on the
  // input signal when the gain changes.
  constexpr size_t kNumFrames = 5;

  constexpr float kInputLevel = 1000.0f;
  constexpr size_t kSampleRateHz = 8000;
  constexpr float kGainDbLow = 0.0f;
  constexpr float kGainDbHigh = 25.0f;
  static_assert(kGainDbLow < kGainDbHigh, "");

  auto agc2_fixed = CreateAgc2FixedDigitalMode(kGainDbLow, kSampleRateHz);

  // Start with a lower gain.
  const float output_level_pre = RunAgc2WithConstantInput(
      *agc2_fixed, kInputLevel, kNumFrames, kSampleRateHz);

  // Increase gain.
  agc2_fixed->SetFixedGainDb(kGainDbHigh);
  static_cast<void>(RunAgc2WithConstantInput(*agc2_fixed, kInputLevel,
                                             kNumFrames, kSampleRateHz));

  // Back to the lower gain.
  agc2_fixed->SetFixedGainDb(kGainDbLow);
  const float output_level_post = RunAgc2WithConstantInput(
      *agc2_fixed, kInputLevel, kNumFrames, kSampleRateHz);

  EXPECT_EQ(output_level_pre, output_level_post);
}

class FixedDigitalTest
    : public ::testing::TestWithParam<std::tuple<float, float, int, bool>> {
 protected:
  float gain_db_min() const { return std::get<0>(GetParam()); }
  float gain_db_max() const { return std::get<1>(GetParam()); }
  int sample_rate_hz() const { return std::get<2>(GetParam()); }
  bool saturation_expected() const { return std::get<3>(GetParam()); }
};

TEST_P(FixedDigitalTest, CheckSaturationBehaviorWithLimiter) {
  for (const float gain_db : test::LinSpace(gain_db_min(), gain_db_max(), 10)) {
    SCOPED_TRACE(gain_db);
    auto agc2_fixed = CreateAgc2FixedDigitalMode(gain_db, sample_rate_hz());
    const float processed_sample =
        RunAgc2WithConstantInput(*agc2_fixed, /*input_level=*/32767.0f,
                                 /*num_frames=*/5, sample_rate_hz());
    if (saturation_expected()) {
      EXPECT_FLOAT_EQ(processed_sample, 32767.0f);
    } else {
      EXPECT_LT(processed_sample, 32767.0f);
    }
  }
}

static_assert(test::kLimiterMaxInputLevelDbFs < 10, "");
INSTANTIATE_TEST_SUITE_P(
    GainController2,
    FixedDigitalTest,
    ::testing::Values(
        // When gain < `test::kLimiterMaxInputLevelDbFs`, the limiter will not
        // saturate the signal (at any sample rate).
        std::make_tuple(0.1f,
                        test::kLimiterMaxInputLevelDbFs - 0.01f,
                        8000,
                        false),
        std::make_tuple(0.1,
                        test::kLimiterMaxInputLevelDbFs - 0.01f,
                        48000,
                        false),
        // When gain > `test::kLimiterMaxInputLevelDbFs`, the limiter will
        // saturate the signal (at any sample rate).
        std::make_tuple(test::kLimiterMaxInputLevelDbFs + 0.01f,
                        10.0f,
                        8000,
                        true),
        std::make_tuple(test::kLimiterMaxInputLevelDbFs + 0.01f,
                        10.0f,
                        48000,
                        true)));

// Processes a test audio file and checks that the gain applied at the end of
// the recording is close to the expected value.
TEST(GainController2, CheckFinalGainWithAdaptiveDigitalController) {
  constexpr int kSampleRateHz = AudioProcessing::kSampleRate48kHz;
  constexpr int kStereo = 2;

  // Create AGC2 enabling only the adaptive digital controller.
  Agc2Config config;
  config.fixed_digital.gain_db = 0.0f;
  config.adaptive_digital.enabled = true;
  GainController2 agc2(config, /*input_volume_controller_config=*/{},
                       kSampleRateHz, kStereo,
                       /*use_internal_vad=*/true);

  test::InputAudioFile input_file(
      test::GetApmCaptureTestVectorFileName(kSampleRateHz),
      /*loop_at_end=*/true);
  const StreamConfig stream_config(kSampleRateHz, kStereo);

  // Init buffers.
  constexpr int kFrameDurationMs = 10;
  std::vector<float> frame(kStereo * stream_config.num_frames());
  AudioBuffer audio_buffer(kSampleRateHz, kStereo, kSampleRateHz, kStereo,
                           kSampleRateHz, kStereo);

  // Simulate.
  constexpr float kGainDb = -6.0f;
  const float gain = std::pow(10.0f, kGainDb / 20.0f);
  constexpr int kDurationMs = 10000;
  constexpr int kNumFramesToProcess = kDurationMs / kFrameDurationMs;
  for (int i = 0; i < kNumFramesToProcess; ++i) {
    ReadFloatSamplesFromStereoFile(stream_config.num_frames(),
                                   stream_config.num_channels(), &input_file,
                                   frame);
    // Apply a fixed gain to the input audio.
    for (float& x : frame) {
      x *= gain;
    }
    test::CopyVectorToAudioBuffer(stream_config, frame, &audio_buffer);
    agc2.Process(/*speech_probability=*/absl::nullopt,
                 /*input_volume_changed=*/false, &audio_buffer);
  }

  // Estimate the applied gain by processing a probing frame.
  SetAudioBufferSamples(/*value=*/1.0f, audio_buffer);
  agc2.Process(/*speech_probability=*/absl::nullopt,
               /*input_volume_changed=*/false, &audio_buffer);
  const float applied_gain_db =
      20.0f * std::log10(audio_buffer.channels_const()[0][0]);

  constexpr float kExpectedGainDb = 7.0f;
  constexpr float kToleranceDb = 0.3f;
  EXPECT_NEAR(applied_gain_db, kExpectedGainDb, kToleranceDb);
}

#if RTC_DCHECK_IS_ON && GTEST_HAS_DEATH_TEST && !defined(WEBRTC_ANDROID)
// Checks that `GainController2` crashes in debug mode if it runs its internal
// VAD and the speech probability values are provided by the caller.
TEST(GainController2DeathTest,
     DebugCrashIfUseInternalVadAndSpeechProbabilityGiven) {
  constexpr int kSampleRateHz = AudioProcessing::kSampleRate48kHz;
  constexpr int kStereo = 2;
  AudioBuffer audio_buffer(kSampleRateHz, kStereo, kSampleRateHz, kStereo,
                           kSampleRateHz, kStereo);
  // Create AGC2 so that the interval VAD is also created.
  GainController2 agc2(/*config=*/{.adaptive_digital = {.enabled = true}},
                       /*input_volume_controller_config=*/{}, kSampleRateHz,
                       kStereo,
                       /*use_internal_vad=*/true);

  EXPECT_DEATH(agc2.Process(/*speech_probability=*/0.123f,
                            /*input_volume_changed=*/false, &audio_buffer),
               "");
}
#endif

// Processes a test audio file and checks that the injected speech probability
// is not ignored when the internal VAD is not used.
TEST(GainController2,
     CheckInjectedVadProbabilityUsedWithAdaptiveDigitalController) {
  constexpr int kSampleRateHz = AudioProcessing::kSampleRate48kHz;
  constexpr int kStereo = 2;

  // Create AGC2 enabling only the adaptive digital controller.
  Agc2Config config;
  config.fixed_digital.gain_db = 0.0f;
  config.adaptive_digital.enabled = true;
  GainController2 agc2(config, /*input_volume_controller_config=*/{},
                       kSampleRateHz, kStereo,
                       /*use_internal_vad=*/false);
  GainController2 agc2_reference(config, /*input_volume_controller_config=*/{},
                                 kSampleRateHz, kStereo,
                                 /*use_internal_vad=*/true);

  test::InputAudioFile input_file(
      test::GetApmCaptureTestVectorFileName(kSampleRateHz),
      /*loop_at_end=*/true);
  const StreamConfig stream_config(kSampleRateHz, kStereo);

  // Init buffers.
  constexpr int kFrameDurationMs = 10;
  std::vector<float> frame(kStereo * stream_config.num_frames());
  AudioBuffer audio_buffer(kSampleRateHz, kStereo, kSampleRateHz, kStereo,
                           kSampleRateHz, kStereo);
  AudioBuffer audio_buffer_reference(kSampleRateHz, kStereo, kSampleRateHz,
                                     kStereo, kSampleRateHz, kStereo);
  // Simulate.
  constexpr float kGainDb = -6.0f;
  const float gain = std::pow(10.0f, kGainDb / 20.0f);
  constexpr int kDurationMs = 10000;
  constexpr int kNumFramesToProcess = kDurationMs / kFrameDurationMs;
  constexpr float kSpeechProbabilities[] = {1.0f, 0.3f};
  constexpr float kEpsilon = 0.0001f;
  bool all_samples_zero = true;
  bool all_samples_equal = true;
  for (int i = 0, j = 0; i < kNumFramesToProcess; ++i, j = 1 - j) {
    ReadFloatSamplesFromStereoFile(stream_config.num_frames(),
                                   stream_config.num_channels(), &input_file,
                                   frame);
    // Apply a fixed gain to the input audio.
    for (float& x : frame) {
      x *= gain;
    }
    test::CopyVectorToAudioBuffer(stream_config, frame, &audio_buffer);
    agc2.Process(kSpeechProbabilities[j], /*input_volume_changed=*/false,
                 &audio_buffer);
    test::CopyVectorToAudioBuffer(stream_config, frame,
                                  &audio_buffer_reference);
    agc2_reference.Process(/*speech_probability=*/absl::nullopt,
                           /*input_volume_changed=*/false,
                           &audio_buffer_reference);
    // Check the output buffers.
    for (int i = 0; i < kStereo; ++i) {
      for (int j = 0; j < static_cast<int>(audio_buffer.num_frames()); ++j) {
        all_samples_zero &=
            fabs(audio_buffer.channels_const()[i][j]) < kEpsilon;
        all_samples_equal &=
            fabs(audio_buffer.channels_const()[i][j] -
                 audio_buffer_reference.channels_const()[i][j]) < kEpsilon;
      }
    }
  }
  EXPECT_FALSE(all_samples_zero);
  EXPECT_FALSE(all_samples_equal);
}

// Processes a test audio file and checks that the output is equal when
// an injected speech probability from `VoiceActivityDetectorWrapper` and
// the speech probability computed by the internal VAD are the same.
TEST(GainController2,
     CheckEqualResultFromInjectedVadProbabilityWithAdaptiveDigitalController) {
  constexpr int kSampleRateHz = AudioProcessing::kSampleRate48kHz;
  constexpr int kStereo = 2;

  // Create AGC2 enabling only the adaptive digital controller.
  Agc2Config config;
  config.fixed_digital.gain_db = 0.0f;
  config.adaptive_digital.enabled = true;
  GainController2 agc2(config, /*input_volume_controller_config=*/{},
                       kSampleRateHz, kStereo,
                       /*use_internal_vad=*/false);
  GainController2 agc2_reference(config, /*input_volume_controller_config=*/{},
                                 kSampleRateHz, kStereo,
                                 /*use_internal_vad=*/true);
  VoiceActivityDetectorWrapper vad(GetAvailableCpuFeatures(), kSampleRateHz);
  test::InputAudioFile input_file(
      test::GetApmCaptureTestVectorFileName(kSampleRateHz),
      /*loop_at_end=*/true);
  const StreamConfig stream_config(kSampleRateHz, kStereo);

  // Init buffers.
  constexpr int kFrameDurationMs = 10;
  std::vector<float> frame(kStereo * stream_config.num_frames());
  AudioBuffer audio_buffer(kSampleRateHz, kStereo, kSampleRateHz, kStereo,
                           kSampleRateHz, kStereo);
  AudioBuffer audio_buffer_reference(kSampleRateHz, kStereo, kSampleRateHz,
                                     kStereo, kSampleRateHz, kStereo);

  // Simulate.
  constexpr float kGainDb = -6.0f;
  const float gain = std::pow(10.0f, kGainDb / 20.0f);
  constexpr int kDurationMs = 10000;
  constexpr int kNumFramesToProcess = kDurationMs / kFrameDurationMs;
  for (int i = 0; i < kNumFramesToProcess; ++i) {
    ReadFloatSamplesFromStereoFile(stream_config.num_frames(),
                                   stream_config.num_channels(), &input_file,
                                   frame);
    // Apply a fixed gain to the input audio.
    for (float& x : frame) {
      x *= gain;
    }
    test::CopyVectorToAudioBuffer(stream_config, frame,
                                  &audio_buffer_reference);
    agc2_reference.Process(absl::nullopt, /*input_volume_changed=*/false,
                           &audio_buffer_reference);
    test::CopyVectorToAudioBuffer(stream_config, frame, &audio_buffer);
    float speech_probability = vad.Analyze(AudioFrameView<const float>(
        audio_buffer.channels(), audio_buffer.num_channels(),
        audio_buffer.num_frames()));
    agc2.Process(speech_probability, /*input_volume_changed=*/false,
                 &audio_buffer);
    // Check the output buffer.
    for (int i = 0; i < kStereo; ++i) {
      for (int j = 0; j < static_cast<int>(audio_buffer.num_frames()); ++j) {
        EXPECT_FLOAT_EQ(audio_buffer.channels_const()[i][j],
                        audio_buffer_reference.channels_const()[i][j]);
      }
    }
  }
}

}  // namespace test
}  // namespace webrtc
