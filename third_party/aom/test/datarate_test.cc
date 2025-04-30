/*
 * Copyright (c) 2016, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include "config/aom_config.h"

#include "gtest/gtest.h"
#include "test/acm_random.h"
#include "test/codec_factory.h"
#include "test/datarate_test.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/util.h"
#include "test/y4m_video_source.h"
#include "aom/aom_codec.h"

#if CONFIG_LIBYUV
#include "third_party/libyuv/include/libyuv/scale.h"
#endif

namespace datarate_test {
namespace {

#if CONFIG_LIBYUV
class ResizingVideoSource : public ::libaom_test::DummyVideoSource {
 public:
  ResizingVideoSource(const int width, const int height, const int input_width,
                      const int input_height, const std::string file_name,
                      int limit)
      : width_(width), height_(height), input_width_(input_width),
        input_height_(input_height), limit_(limit) {
    SetSize(width_, height_);
    img_input_ = aom_img_alloc(nullptr, AOM_IMG_FMT_I420, input_width_,
                               input_height_, 32);
    raw_size_ = input_width_ * input_height_ * 3 / 2;
    input_file_ = ::libaom_test::OpenTestDataFile(file_name);
  }

  ~ResizingVideoSource() override {
    aom_img_free(img_input_);
    fclose(input_file_);
  }

 protected:
  void FillFrame() override {
    // Read frame from input_file and scale up.
    ASSERT_NE(input_file_, nullptr);
    fread(img_input_->img_data, raw_size_, 1, input_file_);
    libyuv::I420Scale(
        img_input_->planes[AOM_PLANE_Y], img_input_->stride[AOM_PLANE_Y],
        img_input_->planes[AOM_PLANE_U], img_input_->stride[AOM_PLANE_U],
        img_input_->planes[AOM_PLANE_V], img_input_->stride[AOM_PLANE_V],
        input_width_, input_height_, img_->planes[AOM_PLANE_Y],
        img_->stride[AOM_PLANE_Y], img_->planes[AOM_PLANE_U],
        img_->stride[AOM_PLANE_U], img_->planes[AOM_PLANE_V],
        img_->stride[AOM_PLANE_V], width_, height_, libyuv::kFilterBox);
  }

  const int width_;
  const int height_;
  const int input_width_;
  const int input_height_;
  const int limit_;
  aom_image_t *img_input_;
  size_t raw_size_;
  FILE *input_file_;
};
#endif  // CONFIG_LIBYUV

// Params: test mode, speed, aq mode and index for bitrate array.
class DatarateTestLarge
    : public ::libaom_test::CodecTestWith4Params<libaom_test::TestMode, int,
                                                 unsigned int, int>,
      public DatarateTest {
 public:
  DatarateTestLarge() : DatarateTest(GET_PARAM(0)) {
    set_cpu_used_ = GET_PARAM(2);
    aq_mode_ = GET_PARAM(3);
  }

 protected:
  ~DatarateTestLarge() override = default;

  void SetUp() override {
    InitializeConfig(GET_PARAM(1));
    ResetModel();
  }

  virtual void BasicRateTargetingVBRTest() {
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.g_error_resilient = 0;
    cfg_.rc_end_usage = AOM_VBR;
    cfg_.g_lag_in_frames = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 140);
    const int bitrate_array[2] = { 400, 800 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(effective_datarate_, cfg_.rc_target_bitrate * 0.7)
        << " The datarate for the file is lower than target by too much!";
    // FIXME(jingning): Lower this test threshold after vbr mode can render
    // sufficiently accurate bit rate.
    ASSERT_LE(effective_datarate_, cfg_.rc_target_bitrate * 1.45)
        << " The datarate for the file is greater than target by too much!";
  }

  virtual void BasicRateTargetingCBRTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 1;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 140);
    const int bitrate_array[2] = { 150, 550 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(effective_datarate_, cfg_.rc_target_bitrate * 0.85)
        << " The datarate for the file is lower than target by too much!";
    ASSERT_LE(effective_datarate_, cfg_.rc_target_bitrate * 1.19)
        << " The datarate for the file is greater than target by too much!";
  }

#if CONFIG_LIBYUV
  // Test for an encoding mode that triggers an assert in nonrd_pickmode
  // (in av1_is_subpelmv_in_range), issue b:396169342.
  // The assert is triggered on a 2456x2054 resolution with settings defined
  // with the flag avif_mode_. This test upsamples a QVGA clip to the target
  // resolution, using libyuv for the scaling.
  virtual void BasicRateTargetingCBRAssertAvifModeTest() {
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    ResizingVideoSource video(2456, 2054, 320, 240,
                              "pixel_capture_w320h240.yuv", 100);
    const int bitrate_array[2] = { 1000, 2000 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    avif_mode_ = 1;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  }
#endif  // CONFIG_LIBYUV

  virtual void BasicRateTargetingCBRSpikeTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 2;
    cfg_.rc_max_quantizer = 56;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.kf_max_dist = 3000;
    cfg_.kf_min_dist = 3000;

    ::libaom_test::I420VideoSource video("desktopqvga2.320_240.yuv", 320, 240,
                                         30, 1, 0, 800);
    const int bitrate_array[2] = { 100, 200 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    max_perc_spike_ = 3.0;
    max_perc_spike_high_ = 8.0;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(effective_datarate_, cfg_.rc_target_bitrate * 0.85)
        << " The datarate for the file is lower than target by too much!";
    ASSERT_LE(effective_datarate_, cfg_.rc_target_bitrate * 1.19)
        << " The datarate for the file is greater than target by too much!";
    ASSERT_LE(num_spikes_, 10);
    ASSERT_LT(num_spikes_high_, 1);
  }

  virtual void BasicRateTargetingCBRDynamicBitrateTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 2;
    cfg_.rc_max_quantizer = 56;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.kf_max_dist = 3000;
    cfg_.kf_min_dist = 3000;

    ::libaom_test::I420VideoSource video("desktop1.320_180.yuv", 320, 180, 30,
                                         1, 0, 800);
    const int bitrate_array[2] = { 100, 200 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    target_bitrate_update_[0] = cfg_.rc_target_bitrate;
    target_bitrate_update_[1] = static_cast<int>(1.3 * cfg_.rc_target_bitrate);
    target_bitrate_update_[2] = static_cast<int>(0.7 * cfg_.rc_target_bitrate);
    frame_update_bitrate_ = 250;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < 3; i++) {
      ASSERT_GE(effective_datarate_dynamic_[i],
                target_bitrate_update_[i] * 0.85)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_dynamic_[i],
                target_bitrate_update_[i] * 1.20)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingMultiThreadCBRTest() {
    ::libaom_test::I420VideoSource video("niklas_640_480_30.yuv", 640, 480, 30,
                                         1, 0, 400);
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 1;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_threads = 4;

    const int bitrate_array[2] = { 250, 650 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    tile_columns_ = 2;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(static_cast<double>(cfg_.rc_target_bitrate),
              effective_datarate_ * 0.85)
        << " The datarate for the file exceeds the target by too much!";
    ASSERT_LE(static_cast<double>(cfg_.rc_target_bitrate),
              effective_datarate_ * 1.15)
        << " The datarate for the file missed the target!"
        << cfg_.rc_target_bitrate << " " << effective_datarate_;
  }

  virtual void ErrorResilienceOnSceneCuts() {
    if (GET_PARAM(4) > 0) return;
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.g_error_resilient = 1;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    cfg_.rc_target_bitrate = 500;
    ResetModel();
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(effective_datarate_, cfg_.rc_target_bitrate * 0.85)
        << " The datarate for the file is lower than target by too much!";
    ASSERT_LE(effective_datarate_, cfg_.rc_target_bitrate * 1.15)
        << " The datarate for the file is greater than target by too much!";
  }

  virtual void BasicRateTargetingCBRPeriodicKeyFrameTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 1;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    // Periodic keyframe
    cfg_.kf_max_dist = 50;

    ::libaom_test::I420VideoSource video("pixel_capture_w320h240.yuv", 320, 240,
                                         30, 1, 0, 310);
    const int bitrate_array[2] = { 150, 550 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(effective_datarate_, cfg_.rc_target_bitrate * 0.85)
        << " The datarate for the file is lower than target by too much!";
    ASSERT_LE(effective_datarate_, cfg_.rc_target_bitrate * 1.15)
        << " The datarate for the file is greater than target by too much!";
  }

  virtual void CBRPeriodicKeyFrameOnSceneCuts() {
    if (GET_PARAM(4) > 0) return;
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    // Periodic keyframe
    cfg_.kf_max_dist = 30;
    cfg_.kf_min_dist = 30;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    cfg_.rc_target_bitrate = 500;
    ResetModel();
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(effective_datarate_, cfg_.rc_target_bitrate * 0.85)
        << " The datarate for the file is lower than target by too much!";
    ASSERT_LE(effective_datarate_, cfg_.rc_target_bitrate * 1.3)
        << " The datarate for the file is greater than target by too much!";
  }

  virtual void BasicRateTargetingAQModeOnOffCBRTest() {
    if (GET_PARAM(4) > 0) return;
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 2;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;
    cfg_.g_pass = AOM_RC_ONE_PASS;
    cfg_.g_usage = AOM_USAGE_REALTIME;
    cfg_.kf_mode = AOM_KF_DISABLED;

    ::libaom_test::I420VideoSource video("pixel_capture_w320h240.yuv", 320, 240,
                                         30, 1, 0, 310);
    cfg_.rc_target_bitrate = 60;
    ResetModel();
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(effective_datarate_, cfg_.rc_target_bitrate * 0.85)
        << " The datarate for the file is lower than target by too much!";
    ASSERT_LE(effective_datarate_, cfg_.rc_target_bitrate * 1.15)
        << " The datarate for the file is greater than target by too much!";
  }

  virtual void BasicRateTargeting444CBRScreenTest() {
    ::libaom_test::Y4mVideoSource video("rush_hour_444.y4m", 0, 140);

    cfg_.g_profile = 1;
    cfg_.g_timebase = video.timebase();

    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 1;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;

    const int bitrate_array[2] = { 250, 650 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    screen_mode_ = true;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(static_cast<double>(cfg_.rc_target_bitrate),
              effective_datarate_ * 0.85)
        << " The datarate for the file exceeds the target by too much!";
    ASSERT_LE(static_cast<double>(cfg_.rc_target_bitrate),
              effective_datarate_ * 1.15)
        << " The datarate for the file missed the target!"
        << cfg_.rc_target_bitrate << " " << effective_datarate_;
  }

  virtual void BasicRateTargetingSuperresCBR() {
    ::libaom_test::I420VideoSource video("desktopqvga2.320_240.yuv", 320, 240,
                                         30, 1, 0, 800);

    cfg_.g_profile = 0;
    cfg_.g_timebase = video.timebase();

    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 1;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;

    cfg_.rc_superres_mode = AOM_SUPERRES_FIXED;
    cfg_.rc_superres_denominator = 16;
    cfg_.rc_superres_kf_denominator = 16;

    const int bitrate_array[2] = { 250, 650 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(static_cast<double>(cfg_.rc_target_bitrate),
              effective_datarate_ * 0.85)
        << " The datarate for the file exceeds the target by too much!";
    ASSERT_LE(static_cast<double>(cfg_.rc_target_bitrate),
              effective_datarate_ * 1.15)
        << " The datarate for the file missed the target!"
        << cfg_.rc_target_bitrate << " " << effective_datarate_;
  }

  virtual void BasicRateTargetingSuperresCBRMultiThreads() {
    ::libaom_test::I420VideoSource video("niklas_640_480_30.yuv", 640, 480, 30,
                                         1, 0, 400);

    cfg_.g_profile = 0;
    cfg_.g_timebase = video.timebase();

    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 1;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_threads = 2;

    cfg_.rc_superres_mode = AOM_SUPERRES_FIXED;
    cfg_.rc_superres_denominator = 16;
    cfg_.rc_superres_kf_denominator = 16;

    const int bitrate_array[2] = { 250, 650 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    tile_columns_ = 1;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(static_cast<double>(cfg_.rc_target_bitrate),
              effective_datarate_ * 0.85)
        << " The datarate for the file exceeds the target by too much!";
    ASSERT_LE(static_cast<double>(cfg_.rc_target_bitrate),
              effective_datarate_ * 1.15)
        << " The datarate for the file missed the target!"
        << cfg_.rc_target_bitrate << " " << effective_datarate_;
  }
};

// Params: test mode, speed, aq mode.
class DatarateTestFrameDropLarge
    : public ::libaom_test::CodecTestWith3Params<libaom_test::TestMode, int,
                                                 unsigned int>,
      public DatarateTest {
 public:
  DatarateTestFrameDropLarge() : DatarateTest(GET_PARAM(0)) {
    set_cpu_used_ = GET_PARAM(2);
    aq_mode_ = GET_PARAM(3);
  }

 protected:
  ~DatarateTestFrameDropLarge() override = default;

  void SetUp() override {
    InitializeConfig(GET_PARAM(1));
    ResetModel();
  }

  virtual void ChangingDropFrameThreshTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_undershoot_pct = 20;
    cfg_.rc_undershoot_pct = 20;
    cfg_.rc_dropframe_thresh = 10;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 50;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.rc_target_bitrate = 200;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 1;
    // TODO(marpan): Investigate datarate target failures with a smaller
    // keyframe interval (128).
    cfg_.kf_max_dist = 9999;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 100);

    const int kDropFrameThreshTestStep = 30;
    aom_codec_pts_t last_drop = 140;
    int last_num_drops = 0;
    for (int i = 40; i < 100; i += kDropFrameThreshTestStep) {
      cfg_.rc_dropframe_thresh = i;
      ResetModel();
      ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
      ASSERT_GE(effective_datarate_, cfg_.rc_target_bitrate * 0.85)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_, cfg_.rc_target_bitrate * 1.40)
          << " The datarate for the file is greater than target by too much!";
      if (last_drop > 0) {
        ASSERT_LE(first_drop_, last_drop)
            << " The first dropped frame for drop_thresh " << i
            << " > first dropped frame for drop_thresh "
            << i - kDropFrameThreshTestStep;
      }
      ASSERT_GE(num_drops_, last_num_drops * 0.7)
          << " The number of dropped frames for drop_thresh " << i
          << " < number of dropped frames for drop_thresh "
          << i - kDropFrameThreshTestStep;
      last_drop = first_drop_;
      last_num_drops = num_drops_;
    }
  }
};

// Check basic rate targeting for VBR mode.
TEST_P(DatarateTestLarge, BasicRateTargetingVBR) {
  BasicRateTargetingVBRTest();
}

// Check basic rate targeting for CBR.
TEST_P(DatarateTestLarge, BasicRateTargetingCBR) {
  BasicRateTargetingCBRTest();
}

// Check basic rate targeting for CBR, with 4 threads
TEST_P(DatarateTestLarge, BasicRateTargetingMultiThreadCBR) {
  BasicRateTargetingMultiThreadCBRTest();
}

// Check basic rate targeting for periodic key frame.
TEST_P(DatarateTestLarge, PeriodicKeyFrameCBR) {
  BasicRateTargetingCBRPeriodicKeyFrameTest();
}

// Check basic rate targeting for periodic key frame, aligned with scene change.
TEST_P(DatarateTestLarge, PeriodicKeyFrameCBROnSceneCuts) {
  CBRPeriodicKeyFrameOnSceneCuts();
}

// Check basic rate targeting with error resilience on for scene cuts.
TEST_P(DatarateTestLarge, ErrorResilienceOnSceneCuts) {
  ErrorResilienceOnSceneCuts();
}

// Check basic rate targeting for CBR, for 444 input screen mode.
#if defined(CONFIG_MAX_DECODE_PROFILE) && CONFIG_MAX_DECODE_PROFILE < 1
TEST_P(DatarateTestLarge, DISABLED_BasicRateTargeting444CBRScreen) {
#else
TEST_P(DatarateTestLarge, BasicRateTargeting444CBRScreen) {
#endif
  BasicRateTargeting444CBRScreenTest();
}

// Check basic rate targeting for Superres mode with CBR.
TEST_P(DatarateTestLarge, BasicRateTargetingSuperresCBR) {
  BasicRateTargetingSuperresCBR();
}

// Check basic rate targeting for Superres mode with CBR and multi-threads.
TEST_P(DatarateTestLarge, BasicRateTargetingSuperresCBRMultiThreads) {
  BasicRateTargetingSuperresCBRMultiThreads();
}

// Check that (1) the first dropped frame gets earlier and earlier
// as the drop frame threshold is increased, and (2) that the total number of
// frame drops does not decrease as we increase frame drop threshold.
// Use a lower qp-max to force some frame drops.
TEST_P(DatarateTestFrameDropLarge, ChangingDropFrameThresh) {
  ChangingDropFrameThreshTest();
}

TEST_P(DatarateTestLarge, BasicRateTargetingAQModeOnOffCBR) {
  BasicRateTargetingAQModeOnOffCBRTest();
}

class DatarateTestRealtime : public DatarateTestLarge {};

class DatarateTestFrameDropRealtime : public DatarateTestFrameDropLarge {};

// Params: aq mode.
class DatarateTestSpeedChangeRealtime
    : public ::libaom_test::CodecTestWith2Params<libaom_test::TestMode,
                                                 unsigned int>,
      public DatarateTest {
 public:
  DatarateTestSpeedChangeRealtime() : DatarateTest(GET_PARAM(0)) {
    aq_mode_ = GET_PARAM(1);
    speed_change_test_ = true;
  }

 protected:
  ~DatarateTestSpeedChangeRealtime() override = default;

  void SetUp() override {
    InitializeConfig(GET_PARAM(1));
    ResetModel();
  }

  virtual void ChangingSpeedTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_undershoot_pct = 20;
    cfg_.rc_undershoot_pct = 20;
    cfg_.rc_dropframe_thresh = 10;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 50;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.rc_target_bitrate = 200;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 1;
    // TODO(marpan): Investigate datarate target failures with a smaller
    // keyframe interval (128).
    cfg_.kf_max_dist = 9999;
    cfg_.rc_dropframe_thresh = 0;
    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 100);

    ResetModel();
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(effective_datarate_, cfg_.rc_target_bitrate * 0.83)
        << " The datarate for the file is lower than target by too much!";
    ASSERT_LE(effective_datarate_, cfg_.rc_target_bitrate * 1.35)
        << " The datarate for the file is greater than target by too much!";
  }
};

// Check basic rate targeting for VBR mode.
TEST_P(DatarateTestRealtime, BasicRateTargetingVBR) {
  BasicRateTargetingVBRTest();
}

// Check basic rate targeting for CBR.
TEST_P(DatarateTestRealtime, BasicRateTargetingCBR) {
  BasicRateTargetingCBRTest();
}

#if CONFIG_LIBYUV
// Check basic rate targeting for CBR, special case.
TEST_P(DatarateTestRealtime, BasicRateTargetingCBRAssertAvifMode) {
  BasicRateTargetingCBRAssertAvifModeTest();
}
#endif

// Check basic rate targeting for CBR. Use a longer clip,
// and verify #encode size spikes above threshold.
TEST_P(DatarateTestRealtime, BasicRateTargetingCBRSpike) {
  BasicRateTargetingCBRSpikeTest();
}

// Check basic rate targeting for CBR. Use a longer clip,
// and verify encoder can respnd and hit new bitrates updated
// within the stream.
TEST_P(DatarateTestRealtime, BasicRateTargetingCBRDynamicBitrate) {
  BasicRateTargetingCBRDynamicBitrateTest();
}

// Check basic rate targeting for CBR, with 4 threads
TEST_P(DatarateTestRealtime, BasicRateTargetingMultiThreadCBR) {
  BasicRateTargetingMultiThreadCBRTest();
}

// Check basic rate targeting for periodic key frame.
TEST_P(DatarateTestRealtime, PeriodicKeyFrameCBR) {
  BasicRateTargetingCBRPeriodicKeyFrameTest();
}

// Check basic rate targeting for periodic key frame, aligned with scene change.
TEST_P(DatarateTestRealtime, PeriodicKeyFrameCBROnSceneCuts) {
  CBRPeriodicKeyFrameOnSceneCuts();
}

// Check basic rate targeting with error resilience on for scene cuts.
TEST_P(DatarateTestRealtime, ErrorResilienceOnSceneCuts) {
  ErrorResilienceOnSceneCuts();
}

// Check basic rate targeting for CBR for 444 screen mode.
#if defined(CONFIG_MAX_DECODE_PROFILE) && CONFIG_MAX_DECODE_PROFILE < 1
TEST_P(DatarateTestRealtime, DISABLED_BasicRateTargeting444CBRScreen) {
#else
TEST_P(DatarateTestRealtime, BasicRateTargeting444CBRScreen) {
#endif
  BasicRateTargeting444CBRScreenTest();
}

// Check basic rate targeting for Superres mode with CBR.
TEST_P(DatarateTestRealtime, BasicRateTargetingSuperresCBR) {
  BasicRateTargetingSuperresCBR();
}

// Check basic rate targeting for Superres mode with CBR and multi-threads.
TEST_P(DatarateTestRealtime, BasicRateTargetingSuperresCBRMultiThreads) {
  BasicRateTargetingSuperresCBRMultiThreads();
}

// Check that (1) the first dropped frame gets earlier and earlier
// as the drop frame threshold is increased, and (2) that the total number of
// frame drops does not decrease as we increase frame drop threshold.
// Use a lower qp-max to force some frame drops.
TEST_P(DatarateTestFrameDropRealtime, ChangingDropFrameThresh) {
  ChangingDropFrameThreshTest();
}

TEST_P(DatarateTestSpeedChangeRealtime, ChangingSpeedTest) {
  ChangingSpeedTest();
}

class DatarateTestSetFrameQpRealtime
    : public DatarateTest,
      public ::testing::TestWithParam<const libaom_test::AV1CodecFactory *> {
 public:
  DatarateTestSetFrameQpRealtime() : DatarateTest(GetParam()), frame_(0) {}

 protected:
  ~DatarateTestSetFrameQpRealtime() override = default;

  void SetUp() override {
    InitializeConfig(libaom_test::kRealTime);
    ResetModel();
  }

  void PreEncodeFrameHook(::libaom_test::VideoSource *video,
                          ::libaom_test::Encoder *encoder) override {
    set_cpu_used_ = 7;
    DatarateTest::PreEncodeFrameHook(video, encoder);
    frame_qp_ = rnd_.PseudoUniform(63);
    encoder->Control(AV1E_SET_QUANTIZER_ONE_PASS, frame_qp_);
    frame_++;
  }

  void PostEncodeFrameHook(::libaom_test::Encoder *encoder) override {
    if (frame_ >= total_frames_) return;
    int qp = 0;
    encoder->Control(AOME_GET_LAST_QUANTIZER_64, &qp);
    ASSERT_EQ(qp, frame_qp_);
  }

 protected:
  int total_frames_;

 private:
  int frame_qp_;
  int frame_;
  libaom_test::ACMRandom rnd_;
};

TEST_P(DatarateTestSetFrameQpRealtime, SetFrameQpOnePass) {
  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 500;
  cfg_.rc_buf_sz = 1000;
  cfg_.rc_undershoot_pct = 20;
  cfg_.rc_undershoot_pct = 20;
  cfg_.rc_min_quantizer = 0;
  cfg_.rc_max_quantizer = 50;
  cfg_.rc_end_usage = AOM_CBR;
  cfg_.rc_target_bitrate = 200;
  cfg_.g_lag_in_frames = 0;
  cfg_.g_error_resilient = 1;
  cfg_.kf_max_dist = 9999;
  cfg_.rc_dropframe_thresh = 0;

  total_frames_ = 100;
  ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 100);

  ResetModel();
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

AV1_INSTANTIATE_TEST_SUITE(DatarateTestLarge,
                           ::testing::Values(::libaom_test::kRealTime),
                           ::testing::Range(5, 7), ::testing::Values(0, 3),
                           ::testing::Values(0, 1));

AV1_INSTANTIATE_TEST_SUITE(DatarateTestFrameDropLarge,
                           ::testing::Values(::libaom_test::kRealTime),
                           ::testing::Range(5, 7), ::testing::Values(0, 3));

AV1_INSTANTIATE_TEST_SUITE(DatarateTestRealtime,
                           ::testing::Values(::libaom_test::kRealTime),
                           ::testing::Range(7, 12), ::testing::Values(0, 3),
                           ::testing::Values(0, 1));

AV1_INSTANTIATE_TEST_SUITE(DatarateTestFrameDropRealtime,
                           ::testing::Values(::libaom_test::kRealTime),
                           ::testing::Range(7, 12), ::testing::Values(0, 3));

AV1_INSTANTIATE_TEST_SUITE(DatarateTestSpeedChangeRealtime,
                           ::testing::Values(::libaom_test::kRealTime),
                           ::testing::Values(0, 3));

INSTANTIATE_TEST_SUITE_P(
    AV1, DatarateTestSetFrameQpRealtime,
    ::testing::Values(
        static_cast<const libaom_test::CodecFactory *>(&libaom_test::kAV1)));

}  // namespace
}  // namespace datarate_test
