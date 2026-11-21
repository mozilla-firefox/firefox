/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_processing/aec3/matched_filter.h"

// Defines WEBRTC_ARCH_X86_FAMILY, used below.
#include "rtc_base/system/arch.h"

#if defined(WEBRTC_ARCH_X86_FAMILY)
#include <emmintrin.h>
#endif
#include <algorithm>
#include <string>

#include "modules/audio_processing/aec3/aec3_common.h"
#include "modules/audio_processing/aec3/decimator.h"
#include "modules/audio_processing/aec3/render_delay_buffer.h"
#include "modules/audio_processing/logging/apm_data_dumper.h"
#include "modules/audio_processing/test/echo_canceller_test_tools.h"
#include "rtc_base/random.h"
#include "rtc_base/strings/string_builder.h"
#include "system_wrappers/include/cpu_features_wrapper.h"
#include "test/field_trial.h"
#include "test/gtest.h"

namespace webrtc {
namespace aec3 {
namespace {

std::string ProduceDebugText(size_t delay, size_t down_sampling_factor) {
  rtc::StringBuilder ss;
  ss << "Delay: " << delay;
  ss << ", Down sampling factor: " << down_sampling_factor;
  return ss.Release();
}

constexpr size_t kNumMatchedFilters = 10;
constexpr size_t kDownSamplingFactors[] = {2, 4, 8};
constexpr size_t kWindowSizeSubBlocks = 32;
constexpr size_t kAlignmentShiftSubBlocks = kWindowSizeSubBlocks * 3 / 4;

}  // namespace

class MatchedFilterTest : public ::testing::TestWithParam<bool> {};

#if defined(WEBRTC_HAS_NEON)
// Verifies that the optimized methods for NEON are similar to their reference
// counterparts.
TEST_P(MatchedFilterTest, TestNeonOptimizations) {
  Random random_generator(42U);
  constexpr float kSmoothing = 0.7f;
  const bool kComputeAccumulatederror = GetParam();
  for (auto down_sampling_factor : kDownSamplingFactors) {
    const size_t sub_block_size = kBlockSize / down_sampling_factor;

    std::vector<float> x(2000);
    RandomizeSampleVector(&random_generator, x);
    std::vector<float> y(sub_block_size);
    std::vector<float> h_NEON(512);
    std::vector<float> h(512);
    std::vector<float> accumulated_error(512);
    std::vector<float> accumulated_error_NEON(512);
    std::vector<float> scratch_memory(512);

    int x_index = 0;
    for (int k = 0; k < 1000; ++k) {
      RandomizeSampleVector(&random_generator, y);

      bool filters_updated = false;
      float error_sum = 0.f;
      bool filters_updated_NEON = false;
      float error_sum_NEON = 0.f;

      MatchedFilterCore_NEON(x_index, h.size() * 150.f * 150.f, kSmoothing, x,
                             y, h_NEON, &filters_updated_NEON, &error_sum_NEON,
                             kComputeAccumulatederror, accumulated_error_NEON,
                             scratch_memory);

      MatchedFilterCore(x_index, h.size() * 150.f * 150.f, kSmoothing, x, y, h,
                        &filters_updated, &error_sum, kComputeAccumulatederror,
                        accumulated_error);

      EXPECT_EQ(filters_updated, filters_updated_NEON);
      EXPECT_NEAR(error_sum, error_sum_NEON, error_sum / 100000.f);

      for (size_t j = 0; j < h.size(); ++j) {
        EXPECT_NEAR(h[j], h_NEON[j], 0.00001f);
      }

      if (kComputeAccumulatederror) {
        for (size_t j = 0; j < accumulated_error.size(); ++j) {
          float difference =
              std::abs(accumulated_error[j] - accumulated_error_NEON[j]);
          float relative_difference = accumulated_error[j] > 0
                                          ? difference / accumulated_error[j]
                                          : difference;
          EXPECT_NEAR(relative_difference, 0.0f, 0.02f);
        }
      }

      x_index = (x_index + sub_block_size) % x.size();
    }
  }
}
#endif

#if defined(WEBRTC_ARCH_X86_FAMILY)
// Verifies that the optimized methods for SSE2 are bitexact to their reference
// counterparts.
TEST_P(MatchedFilterTest, TestSse2Optimizations) {
  const bool kComputeAccumulatederror = GetParam();
  bool use_sse2 = (GetCPUInfo(kSSE2) != 0);
  if (use_sse2) {
    Random random_generator(42U);
    constexpr float kSmoothing = 0.7f;
    for (auto down_sampling_factor : kDownSamplingFactors) {
      const size_t sub_block_size = kBlockSize / down_sampling_factor;
      std::vector<float> x(2000);
      RandomizeSampleVector(&random_generator, x);
      std::vector<float> y(sub_block_size);
      std::vector<float> h_SSE2(512);
      std::vector<float> h(512);
      std::vector<float> accumulated_error(512 / 4);
      std::vector<float> accumulated_error_SSE2(512 / 4);
      std::vector<float> scratch_memory(512);
      int x_index = 0;
      for (int k = 0; k < 1000; ++k) {
        RandomizeSampleVector(&random_generator, y);

        bool filters_updated = false;
        float error_sum = 0.f;
        bool filters_updated_SSE2 = false;
        float error_sum_SSE2 = 0.f;

        MatchedFilterCore_SSE2(x_index, h.size() * 150.f * 150.f, kSmoothing, x,
                               y, h_SSE2, &filters_updated_SSE2,
                               &error_sum_SSE2, kComputeAccumulatederror,
                               accumulated_error_SSE2, scratch_memory);

        MatchedFilterCore(x_index, h.size() * 150.f * 150.f, kSmoothing, x, y,
                          h, &filters_updated, &error_sum,
                          kComputeAccumulatederror, accumulated_error);

        EXPECT_EQ(filters_updated, filters_updated_SSE2);
        EXPECT_NEAR(error_sum, error_sum_SSE2, error_sum / 100000.f);

        for (size_t j = 0; j < h.size(); ++j) {
          EXPECT_NEAR(h[j], h_SSE2[j], 0.00001f);
        }

        for (size_t j = 0; j < accumulated_error.size(); ++j) {
          float difference =
              std::abs(accumulated_error[j] - accumulated_error_SSE2[j]);
          float relative_difference = accumulated_error[j] > 0
                                          ? difference / accumulated_error[j]
                                          : difference;
          EXPECT_NEAR(relative_difference, 0.0f, 0.00001f);
        }

        x_index = (x_index + sub_block_size) % x.size();
      }
    }
  }
}

TEST_P(MatchedFilterTest, TestAvx2Optimizations) {
  bool use_avx2 = (GetCPUInfo(kAVX2) != 0);
  const bool kComputeAccumulatederror = GetParam();
  if (use_avx2) {
    Random random_generator(42U);
    constexpr float kSmoothing = 0.7f;
    for (auto down_sampling_factor : kDownSamplingFactors) {
      const size_t sub_block_size = kBlockSize / down_sampling_factor;
      std::vector<float> x(2000);
      RandomizeSampleVector(&random_generator, x);
      std::vector<float> y(sub_block_size);
      std::vector<float> h_AVX2(512);
      std::vector<float> h(512);
      std::vector<float> accumulated_error(512 / 4);
      std::vector<float> accumulated_error_AVX2(512 / 4);
      std::vector<float> scratch_memory(512);
      int x_index = 0;
      for (int k = 0; k < 1000; ++k) {
        RandomizeSampleVector(&random_generator, y);
        bool filters_updated = false;
        float error_sum = 0.f;
        bool filters_updated_AVX2 = false;
        float error_sum_AVX2 = 0.f;
        MatchedFilterCore_AVX2(x_index, h.size() * 150.f * 150.f, kSmoothing, x,
                               y, h_AVX2, &filters_updated_AVX2,
                               &error_sum_AVX2, kComputeAccumulatederror,
                               accumulated_error_AVX2, scratch_memory);
        MatchedFilterCore(x_index, h.size() * 150.f * 150.f, kSmoothing, x, y,
                          h, &filters_updated, &error_sum,
                          kComputeAccumulatederror, accumulated_error);
        EXPECT_EQ(filters_updated, filters_updated_AVX2);
        EXPECT_NEAR(error_sum, error_sum_AVX2, error_sum / 100000.f);
        for (size_t j = 0; j < h.size(); ++j) {
          EXPECT_NEAR(h[j], h_AVX2[j], 0.00001f);
        }
        for (size_t j = 0; j < accumulated_error.size(); j += 4) {
          float difference =
              std::abs(accumulated_error[j] - accumulated_error_AVX2[j]);
          float relative_difference = accumulated_error[j] > 0
                                          ? difference / accumulated_error[j]
                                          : difference;
          EXPECT_NEAR(relative_difference, 0.0f, 0.00001f);
        }
        x_index = (x_index + sub_block_size) % x.size();
      }
    }
  }
}

#endif

// Verifies that the (optimized) function MaxSquarePeakIndex() produces output
// equal to the corresponding std-functions.
TEST(MatchedFilter, MaxSquarePeakIndex) {
  Random random_generator(42U);
  constexpr int kMaxLength = 128;
  constexpr int kNumIterationsPerLength = 256;
  for (int length = 1; length < kMaxLength; ++length) {
    std::vector<float> y(length);
    for (int i = 0; i < kNumIterationsPerLength; ++i) {
      RandomizeSampleVector(&random_generator, y);

      size_t lag_from_function = MaxSquarePeakIndex(y);
      size_t lag_from_std = std::distance(
          y.begin(),
          std::max_element(y.begin(), y.end(), [](float a, float b) -> bool {
            return a * a < b * b;
          }));
      EXPECT_EQ(lag_from_function, lag_from_std);
    }
  }
}

// Verifies that the matched filter produces proper lag estimates for
// artificially delayed signals.
TEST_P(MatchedFilterTest, LagEstimation) {
  const bool kDetectPreEcho = GetParam();
  Random random_generator(42U);
  constexpr size_t kNumChannels = 1;
  constexpr int kSampleRateHz = 48000;
  constexpr size_t kNumBands = NumBandsForRate(kSampleRateHz);

  for (auto down_sampling_factor : kDownSamplingFactors) {
    const size_t sub_block_size = kBlockSize / down_sampling_factor;

    Block render(kNumBands, kNumChannels);
    std::vector<std::vector<float>> capture(
        1, std::vector<float>(kBlockSize, 0.f));
    ApmDataDumper data_dumper(0);
    for (size_t delay_samples : {5, 64, 150, 200, 800, 1000}) {
      SCOPED_TRACE(ProduceDebugText(delay_samples, down_sampling_factor));
      EchoCanceller3Config config;
      config.delay.down_sampling_factor = down_sampling_factor;
      config.delay.num_filters = kNumMatchedFilters;
      Decimator capture_decimator(down_sampling_factor);
      DelayBuffer<float> signal_delay_buffer(down_sampling_factor *
                                             delay_samples);
      MatchedFilter filter(
          &data_dumper, DetectOptimization(), sub_block_size,
          kWindowSizeSubBlocks, kNumMatchedFilters, kAlignmentShiftSubBlocks,
          150, config.delay.delay_estimate_smoothing,
          config.delay.delay_estimate_smoothing_delay_found,
          config.delay.delay_candidate_detection_threshold, kDetectPreEcho);

      std::unique_ptr<RenderDelayBuffer> render_delay_buffer(
          RenderDelayBuffer::Create(config, kSampleRateHz, kNumChannels));

      // Analyze the correlation between render and capture.
      for (size_t k = 0; k < (600 + delay_samples / sub_block_size); ++k) {
        for (size_t band = 0; band < kNumBands; ++band) {
          for (size_t channel = 0; channel < kNumChannels; ++channel) {
            RandomizeSampleVector(&random_generator,
                                  render.View(band, channel));
          }
        }
        signal_delay_buffer.Delay(render.View(/*band=*/0, /*channel=*/0),
                                  capture[0]);
        render_delay_buffer->Insert(render);

        if (k == 0) {
          render_delay_buffer->Reset();
        }

        render_delay_buffer->PrepareCaptureProcessing();
        std::array<float, kBlockSize> downsampled_capture_data;
        rtc::ArrayView<float> downsampled_capture(
            downsampled_capture_data.data(), sub_block_size);
        capture_decimator.Decimate(capture[0], downsampled_capture);
        filter.Update(render_delay_buffer->GetDownsampledRenderBuffer(),
                      downsampled_capture, /*use_slow_smoothing=*/false);
      }

      // Obtain the lag estimates.
      auto lag_estimate = filter.GetBestLagEstimate();
      EXPECT_TRUE(lag_estimate.has_value());

      // Verify that the expected most accurate lag estimate is correct.
      if (lag_estimate.has_value()) {
        EXPECT_EQ(delay_samples, lag_estimate->lag);
        EXPECT_EQ(delay_samples, lag_estimate->pre_echo_lag);
      }
    }
  }
}

// Test the pre echo estimation.
TEST_P(MatchedFilterTest, PreEchoEstimation) {
  const bool kDetectPreEcho = GetParam();
  Random random_generator(42U);
  constexpr size_t kNumChannels = 1;
  constexpr int kSampleRateHz = 48000;
  constexpr size_t kNumBands = NumBandsForRate(kSampleRateHz);

  for (auto down_sampling_factor : kDownSamplingFactors) {
    const size_t sub_block_size = kBlockSize / down_sampling_factor;

    Block render(kNumBands, kNumChannels);
    std::vector<std::vector<float>> capture(
        1, std::vector<float>(kBlockSize, 0.f));
    std::vector<float> capture_with_pre_echo(kBlockSize, 0.f);
    ApmDataDumper data_dumper(0);
    // data_dumper.SetActivated(true);
    size_t pre_echo_delay_samples = 20e-3 * 16000 / down_sampling_factor;
    size_t echo_delay_samples = 50e-3 * 16000 / down_sampling_factor;
    EchoCanceller3Config config;
    config.delay.down_sampling_factor = down_sampling_factor;
    config.delay.num_filters = kNumMatchedFilters;
    Decimator capture_decimator(down_sampling_factor);
    DelayBuffer<float> signal_echo_delay_buffer(down_sampling_factor *
                                                echo_delay_samples);
    DelayBuffer<float> signal_pre_echo_delay_buffer(down_sampling_factor *
                                                    pre_echo_delay_samples);
    MatchedFilter filter(
        &data_dumper, DetectOptimization(), sub_block_size,
        kWindowSizeSubBlocks, kNumMatchedFilters, kAlignmentShiftSubBlocks, 150,
        config.delay.delay_estimate_smoothing,
        config.delay.delay_estimate_smoothing_delay_found,
        config.delay.delay_candidate_detection_threshold, kDetectPreEcho);
    std::unique_ptr<RenderDelayBuffer> render_delay_buffer(
        RenderDelayBuffer::Create(config, kSampleRateHz, kNumChannels));
    // Analyze the correlation between render and capture.
    for (size_t k = 0; k < (600 + echo_delay_samples / sub_block_size); ++k) {
      for (size_t band = 0; band < kNumBands; ++band) {
        for (size_t channel = 0; channel < kNumChannels; ++channel) {
          RandomizeSampleVector(&random_generator, render.View(band, channel));
        }
      }
      signal_echo_delay_buffer.Delay(render.View(0, 0), capture[0]);
      signal_pre_echo_delay_buffer.Delay(render.View(0, 0),
                                         capture_with_pre_echo);
      for (size_t k = 0; k < capture[0].size(); ++k) {
        constexpr float gain_pre_echo = 0.8f;
        capture[0][k] += gain_pre_echo * capture_with_pre_echo[k];
      }
      render_delay_buffer->Insert(render);
      if (k == 0) {
        render_delay_buffer->Reset();
      }
      render_delay_buffer->PrepareCaptureProcessing();
      std::array<float, kBlockSize> downsampled_capture_data;
      rtc::ArrayView<float> downsampled_capture(downsampled_capture_data.data(),
                                                sub_block_size);
      capture_decimator.Decimate(capture[0], downsampled_capture);
      filter.Update(render_delay_buffer->GetDownsampledRenderBuffer(),
                    downsampled_capture, /*use_slow_smoothing=*/false);
    }
    // Obtain the lag estimates.
    auto lag_estimate = filter.GetBestLagEstimate();
    EXPECT_TRUE(lag_estimate.has_value());
    // Verify that the expected most accurate lag estimate is correct.
    if (lag_estimate.has_value()) {
      EXPECT_EQ(echo_delay_samples, lag_estimate->lag);
      if (kDetectPreEcho) {
        // The pre echo delay is estimated in a subsampled domain and a larger
        // error is allowed.
        EXPECT_NEAR(pre_echo_delay_samples, lag_estimate->pre_echo_lag, 4);
      } else {
        // The pre echo delay fallback to the highest mached filter peak when
        // its detection is disabled.
        EXPECT_EQ(echo_delay_samples, lag_estimate->pre_echo_lag);
      }
    }
  }
}

// Verifies that the matched filter does not produce reliable and accurate
// estimates for uncorrelated render and capture signals.
TEST_P(MatchedFilterTest, LagNotReliableForUncorrelatedRenderAndCapture) {
  const bool kDetectPreEcho = GetParam();
  constexpr size_t kNumChannels = 1;
  constexpr int kSampleRateHz = 48000;
  constexpr size_t kNumBands = NumBandsForRate(kSampleRateHz);
  Random random_generator(42U);
  for (auto down_sampling_factor : kDownSamplingFactors) {
    EchoCanceller3Config config;
    config.delay.down_sampling_factor = down_sampling_factor;
    config.delay.num_filters = kNumMatchedFilters;
    const size_t sub_block_size = kBlockSize / down_sampling_factor;

    Block render(kNumBands, kNumChannels);
    std::array<float, kBlockSize> capture_data;
    rtc::ArrayView<float> capture(capture_data.data(), sub_block_size);
    std::fill(capture.begin(), capture.end(), 0.f);
    ApmDataDumper data_dumper(0);
    std::unique_ptr<RenderDelayBuffer> render_delay_buffer(
        RenderDelayBuffer::Create(config, kSampleRateHz, kNumChannels));
    MatchedFilter filter(
        &data_dumper, DetectOptimization(), sub_block_size,
        kWindowSizeSubBlocks, kNumMatchedFilters, kAlignmentShiftSubBlocks, 150,
        config.delay.delay_estimate_smoothing,
        config.delay.delay_estimate_smoothing_delay_found,
        config.delay.delay_candidate_detection_threshold, kDetectPreEcho);

    // Analyze the correlation between render and capture.
    for (size_t k = 0; k < 100; ++k) {
      RandomizeSampleVector(&random_generator,
                            render.View(/*band=*/0, /*channel=*/0));
      RandomizeSampleVector(&random_generator, capture);
      render_delay_buffer->Insert(render);
      filter.Update(render_delay_buffer->GetDownsampledRenderBuffer(), capture,
                    false);
    }

    // Obtain the best lag estimate and Verify that no lag estimates are
    // reliable.
    auto best_lag_estimates = filter.GetBestLagEstimate();
    EXPECT_FALSE(best_lag_estimates.has_value());
  }
}

// Verifies that the matched filter does not produce updated lag estimates for
// render signals of low level.
TEST_P(MatchedFilterTest, LagNotUpdatedForLowLevelRender) {
  const bool kDetectPreEcho = GetParam();
  Random random_generator(42U);
  constexpr size_t kNumChannels = 1;
  constexpr int kSampleRateHz = 48000;
  constexpr size_t kNumBands = NumBandsForRate(kSampleRateHz);

  for (auto down_sampling_factor : kDownSamplingFactors) {
    const size_t sub_block_size = kBlockSize / down_sampling_factor;

    Block render(kNumBands, kNumChannels);
    std::vector<std::vector<float>> capture(
        1, std::vector<float>(kBlockSize, 0.f));
    ApmDataDumper data_dumper(0);
    EchoCanceller3Config config;
    MatchedFilter filter(
        &data_dumper, DetectOptimization(), sub_block_size,
        kWindowSizeSubBlocks, kNumMatchedFilters, kAlignmentShiftSubBlocks, 150,
        config.delay.delay_estimate_smoothing,
        config.delay.delay_estimate_smoothing_delay_found,
        config.delay.delay_candidate_detection_threshold, kDetectPreEcho);
    std::unique_ptr<RenderDelayBuffer> render_delay_buffer(
        RenderDelayBuffer::Create(EchoCanceller3Config(), kSampleRateHz,
                                  kNumChannels));
    Decimator capture_decimator(down_sampling_factor);

    // Analyze the correlation between render and capture.
    for (size_t k = 0; k < 100; ++k) {
      RandomizeSampleVector(&random_generator, render.View(0, 0));
      for (auto& render_k : render.View(0, 0)) {
        render_k *= 149.f / 32767.f;
      }
      std::copy(render.begin(0, 0), render.end(0, 0), capture[0].begin());
      std::array<float, kBlockSize> downsampled_capture_data;
      rtc::ArrayView<float> downsampled_capture(downsampled_capture_data.data(),
                                                sub_block_size);
      capture_decimator.Decimate(capture[0], downsampled_capture);
      filter.Update(render_delay_buffer->GetDownsampledRenderBuffer(),
                    downsampled_capture, false);
    }

    // Verify that no lag estimate has been produced.
    auto lag_estimate = filter.GetBestLagEstimate();
    EXPECT_FALSE(lag_estimate.has_value());
  }
}

INSTANTIATE_TEST_SUITE_P(_, MatchedFilterTest, testing::Values(true, false));

#if RTC_DCHECK_IS_ON && GTEST_HAS_DEATH_TEST && !defined(WEBRTC_ANDROID)

class MatchedFilterDeathTest : public ::testing::TestWithParam<bool> {};

// Verifies the check for non-zero windows size.
TEST_P(MatchedFilterDeathTest, ZeroWindowSize) {
  const bool kDetectPreEcho = GetParam();
  ApmDataDumper data_dumper(0);
  EchoCanceller3Config config;
  EXPECT_DEATH(MatchedFilter(&data_dumper, DetectOptimization(), 16, 0, 1, 1,
                             150, config.delay.delay_estimate_smoothing,
                             config.delay.delay_estimate_smoothing_delay_found,
                             config.delay.delay_candidate_detection_threshold,
                             kDetectPreEcho),
               "");
}

// Verifies the check for non-null data dumper.
TEST_P(MatchedFilterDeathTest, NullDataDumper) {
  const bool kDetectPreEcho = GetParam();
  EchoCanceller3Config config;
  EXPECT_DEATH(MatchedFilter(nullptr, DetectOptimization(), 16, 1, 1, 1, 150,
                             config.delay.delay_estimate_smoothing,
                             config.delay.delay_estimate_smoothing_delay_found,
                             config.delay.delay_candidate_detection_threshold,
                             kDetectPreEcho),
               "");
}

// Verifies the check for that the sub block size is a multiple of 4.
// TODO(peah): Activate the unittest once the required code has been landed.
TEST_P(MatchedFilterDeathTest, DISABLED_BlockSizeMultipleOf4) {
  const bool kDetectPreEcho = GetParam();
  ApmDataDumper data_dumper(0);
  EchoCanceller3Config config;
  EXPECT_DEATH(MatchedFilter(&data_dumper, DetectOptimization(), 15, 1, 1, 1,
                             150, config.delay.delay_estimate_smoothing,
                             config.delay.delay_estimate_smoothing_delay_found,
                             config.delay.delay_candidate_detection_threshold,
                             kDetectPreEcho),
               "");
}

// Verifies the check for that there is an integer number of sub blocks that add
// up to a block size.
// TODO(peah): Activate the unittest once the required code has been landed.
TEST_P(MatchedFilterDeathTest, DISABLED_SubBlockSizeAddsUpToBlockSize) {
  const bool kDetectPreEcho = GetParam();
  ApmDataDumper data_dumper(0);
  EchoCanceller3Config config;
  EXPECT_DEATH(MatchedFilter(&data_dumper, DetectOptimization(), 12, 1, 1, 1,
                             150, config.delay.delay_estimate_smoothing,
                             config.delay.delay_estimate_smoothing_delay_found,
                             config.delay.delay_candidate_detection_threshold,
                             kDetectPreEcho),
               "");
}

INSTANTIATE_TEST_SUITE_P(_,
                         MatchedFilterDeathTest,
                         testing::Values(true, false));

#endif

}  // namespace aec3
}  // namespace webrtc
