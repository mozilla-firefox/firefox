/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <unordered_map>

#include "gtest/gtest.h"

#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/util.h"
#include "test/y4m_video_source.h"

namespace {
const unsigned int kFrames = 10;
const int kBitrate = 500;
const unsigned int kCqLevel = 18;

// List of psnr thresholds for different test combinations
// keys: test-mode, cpu-used, sharpness.
const std::unordered_map<
    int, std::unordered_map<int, std::unordered_map<int, double>>>
    kPsnrThreshold = { { static_cast<int>(::libaom_test::kTwoPassGood),
                         { { 2, { { 2, 37.6 }, { 5, 37.6 } } },
                           { 4, { { 2, 37.5 }, { 5, 37.5 } } },
                           { 6, { { 2, 37.2 }, { 5, 37.2 } } } } },
                       { static_cast<int>(::libaom_test::kAllIntra),
                         { { 3, { { 2, 42.2 }, { 5, 42.2 } } },
                           { 6, { { 2, 41.7 }, { 4, 41.9 }, { 5, 41.9 } } },
                           { 9, { { 2, 40.6 }, { 5, 40.6 } } } } } };

// This class is used to test sharpness parameter configured through control
// call using AOME_SET_SHARPNESS for different encoder configurations.
class SharpnessTest
    : public ::libaom_test::CodecTestWith3Params<libaom_test::TestMode, int,
                                                 int>,
      public ::libaom_test::EncoderTest {
 protected:
  SharpnessTest()
      : EncoderTest(GET_PARAM(0)), encoding_mode_(GET_PARAM(1)),
        cpu_used_(GET_PARAM(2)), sharpness_level_(GET_PARAM(3)), psnr_(0.0),
        nframes_(0) {}

  ~SharpnessTest() override = default;

  void SetUp() override {
    InitializeConfig(encoding_mode_);
    if (encoding_mode_ == ::libaom_test::kTwoPassGood) {
      cfg_.rc_target_bitrate = kBitrate;
      cfg_.g_lag_in_frames = 5;
    }
  }

  void BeginPassHook(unsigned int) override {
    psnr_ = 0.0;
    nframes_ = 0;
  }

  void PSNRPktHook(const aom_codec_cx_pkt_t *pkt) override {
    psnr_ += pkt->data.psnr.psnr[0];
    nframes_++;
  }

  void PreEncodeFrameHook(::libaom_test::VideoSource *video,
                          ::libaom_test::Encoder *encoder) override {
    if (video->frame() == 0) {
      encoder->Control(AOME_SET_CPUUSED, cpu_used_);
      encoder->Control(AOME_SET_SHARPNESS, sharpness_level_);
      if (encoding_mode_ == ::libaom_test::kTwoPassGood) {
        encoder->Control(AOME_SET_ENABLEAUTOALTREF, 1);
        encoder->Control(AOME_SET_ARNR_MAXFRAMES, 7);
        encoder->Control(AOME_SET_ARNR_STRENGTH, 5);
      } else if (encoding_mode_ == ::libaom_test::kAllIntra) {
        encoder->Control(AOME_SET_CQ_LEVEL, kCqLevel);
      }
    }
  }

  double GetAveragePsnr() const {
    if (nframes_) return psnr_ / nframes_;
    return 0.0;
  }

  double GetPsnrThreshold() {
    return kPsnrThreshold.at(encoding_mode_).at(cpu_used_).at(sharpness_level_);
  }

  void DoTest() {
    init_flags_ = AOM_CODEC_USE_PSNR;

    std::unique_ptr<libaom_test::VideoSource> video(
        new libaom_test::Y4mVideoSource("paris_352_288_30.y4m", 0, kFrames));
    ASSERT_NE(video, nullptr);

    ASSERT_NO_FATAL_FAILURE(RunLoop(video.get()));
    const double psnr = GetAveragePsnr();
    EXPECT_GT(psnr, GetPsnrThreshold())
        << "encoding mode = " << encoding_mode_ << ", cpu used = " << cpu_used_
        << ", sharpness level = " << sharpness_level_;
  }

 private:
  const libaom_test::TestMode encoding_mode_;
  const int cpu_used_;
  const int sharpness_level_;
  double psnr_;
  unsigned int nframes_;
};

class SharpnessTestLarge : public SharpnessTest {};

class SharpnessAllIntraTest : public SharpnessTest {};

class SharpnessAllIntraTestLarge : public SharpnessTest {};

TEST_P(SharpnessTestLarge, SharpnessPSNRTest) { DoTest(); }

TEST_P(SharpnessAllIntraTest, SharpnessPSNRTest) { DoTest(); }

TEST_P(SharpnessAllIntraTestLarge, SharpnessPSNRTest) { DoTest(); }

AV1_INSTANTIATE_TEST_SUITE(SharpnessTestLarge,
                           ::testing::Values(::libaom_test::kTwoPassGood),
                           ::testing::Values(2, 4, 6),  // cpu_used
                           ::testing::Values(2, 5));    // sharpness level

AV1_INSTANTIATE_TEST_SUITE(SharpnessAllIntraTest,
                           ::testing::Values(::libaom_test::kAllIntra),
                           ::testing::Values(6),   // cpu_used
                           ::testing::Values(4));  // sharpness level

AV1_INSTANTIATE_TEST_SUITE(SharpnessAllIntraTestLarge,
                           ::testing::Values(::libaom_test::kAllIntra),
                           ::testing::Values(3, 6, 9),  // cpu_used
                           ::testing::Values(2, 5));    // sharpness level
}  // namespace
