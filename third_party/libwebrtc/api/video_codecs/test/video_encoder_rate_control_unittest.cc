/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "api/environment/environment.h"
#include "api/media_stream_interface.h"
#include "api/scoped_refptr.h"
#include "api/test/create_frame_generator.h"
#include "api/test/frame_generator_interface.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/frequency.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/i420_buffer.h"
#include "api/video/resolution.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video_codecs/libaom_av1_encoder_factory.h"
#include "api/video_codecs/test/temporal_layer_pattern_for_test.h"
#include "api/video_codecs/test/video_codec_test_utils.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_encoder_builders.h"
#include "api/video_codecs/video_encoder_builders_for_test.h"
#include "api/video_codecs/video_encoder_factory_interface.h"
#include "api/video_codecs/video_encoder_interface.h"
#include "api/video_codecs/video_encoding_general.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/qp_parser_for_test.h"
#include "test/testsupport/file_utils.h"
#include "test/testsupport/frame_reader.h"
#include "test/testsupport/pendulum_frame_generator.h"
#include "test/testsupport/switching_frame_reader.h"
#include "test/time_controller/simulated_time_controller.h"

// This file contains tests evaluating the rate control requirements in
// `api/video_codecs/g3doc/video_encoder_api_v2.md`.
// These are not meant to be exhaustive and are not intended for continuous
// performance testing. See e.g. test/video_codec_tester.h for such purposes.

namespace webrtc {
namespace {

// The CBR settings used by the tests in this file, see
// `VideoEncoderFactoryInterface::StaticEncoderSettings::Cbr`. The transmission
// delay the buffer sizes allow for also bounds how uneven a temporal layer
// allocation can reasonably be, since a frame asking for a significant part of
// that budget cannot be delivered in time.
constexpr TimeDelta kCbrMaxBufferSize = TimeDelta::Millis(1000);
constexpr TimeDelta kCbrTargetBufferSize = TimeDelta::Millis(600);
constexpr double kMaxIntraBitrateFactor = 3.0;

// Tracks accumulated data sizes and duration for actual encoded bytes and ideal
// CBR bytes.
struct AccumulatedData {
  DataSize actual = DataSize::Zero();
  DataSize ideal = DataSize::Zero();
  TimeDelta duration = TimeDelta::Zero();
  std::optional<double> psnr;
  int temporal_id = 0;
  bool is_keyframe = false;

  void Add(const AccumulatedData& other) {
    actual += other.actual;
    ideal += other.ideal;
    duration += other.duration;
  }

  void Subtract(const AccumulatedData& other) {
    actual -= other.actual;
    ideal -= other.ideal;
    duration -= other.duration;
  }

  double deviation_pct() const { return 100.0 * (actual / ideal - 1.0); }
};

class VideoEncoderRateControlTestBase : public ::testing::Test {
 protected:
  VideoEncoderRateControlTestBase() = default;

  bool SupportsCbr() const {
    return encoder_factory_->GetEncoderCapabilities()
        .bitrate_control()
        .rc_modes()
        .contains(VideoEncoderFactoryInterface::RateControlMode::kCbr);
  }

  bool SupportsCbrSetting(
      VideoEncoderFactoryInterface::CbrSetting setting) const {
    return encoder_factory_->GetEncoderCapabilities()
        .bitrate_control()
        .supported_cbr_settings()
        .contains(setting);
  }

  int MaxTemporalLayers() const {
    return encoder_factory_->GetEncoderCapabilities()
        .prediction_constraints()
        .max_temporal_layers();
  }

  bool SupportsTemporalLayers(int num_temporal_layers) const {
    return MaxTemporalLayers() >= num_temporal_layers;
  }

  int MaxSpatialLayers() const {
    return encoder_factory_->GetEncoderCapabilities()
        .prediction_constraints()
        .max_spatial_layers();
  }

  int NumReferenceBuffers() const {
    return encoder_factory_->GetEncoderCapabilities()
        .prediction_constraints()
        .num_buffers();
  }

  // TestDecoder is only needed in order to produce PSNR.
  void EnableDecoder() {
    decoder_factory_ = CreateTestDecoderFactory();
    test_decoder_ = std::make_unique<TestDecoder>(
        env_, decoder_factory_.get(), encoder_factory_->CodecName());
    RTC_CHECK(test_decoder_->IsSupported());
  }

  void SetUpCbrEncoder(
      Resolution resolution,
      double max_intra_bitrate_factor = kMaxIntraBitrateFactor) {
    ASSERT_TRUE(SupportsCbr());

    VideoEncoderFactoryInterface::StaticEncoderSettings static_settings =
        StaticEncoderSettingsBuilder()
            .MaxEncodeDimensions(resolution)
            .EncodingFormat({.sub_sampling = EncodingFormat::SubSampling::k420,
                             .bit_depth = 8})
            .CbrRcMode(kCbrMaxBufferSize, kCbrTargetBufferSize,
                       max_intra_bitrate_factor)
            .MaxNumberOfThreads(1)
            .Build();

    encoder_ = encoder_factory_->CreateEncoder(static_settings, {});
    RTC_CHECK(encoder_ != nullptr);
    frame_generator_ = CreateFrameGenerator();
    RTC_CHECK(frame_generator_ != nullptr);
    current_timestamp_ = Timestamp::Zero();
    is_first_frame_ = true;
    test_decoder_.reset();
    decoder_factory_.reset();
    encoded_frames_.clear();
    temporal_layer_pattern_.reset();
  }

  void SetFrameGenerator(
      std::unique_ptr<test::FrameGeneratorInterface> frame_generator) {
    RTC_CHECK(frame_generator != nullptr);
    frame_generator_ = std::move(frame_generator);
  }

  // Returns the next frame of the frame generator, scaled to `resolution`.
  scoped_refptr<VideoFrameBuffer> NextFrame(Resolution resolution) {
    test::FrameGeneratorInterface::VideoFrameData frame_data =
        frame_generator_->NextFrame();
    RTC_CHECK(frame_data.buffer != nullptr);
    if (frame_data.buffer->width() == resolution.width &&
        frame_data.buffer->height() == resolution.height) {
      return frame_data.buffer;
    }
    scoped_refptr<I420Buffer> scaled_buffer =
        I420Buffer::Create(resolution.width, resolution.height);
    scaled_buffer->ScaleFrom(*frame_data.buffer->ToI420());
    return scaled_buffer;
  }

  // Parameters for encoding a sequence of frames in rate control tests.
  struct EncodeSettings {
    int num_frames = 0;
    DataRate target_bitrate = DataRate::Zero();
    // Time interval between the start of successive frames.
    TimeDelta frame_interval = TimeDelta::Zero();
    // Nominal frame duration reported to the CBR rate controller. If omitted,
    // defaults to `frame_interval`. Must not be set together with
    // `temporal_layer_pattern`, which derives the duration of each frame from
    // `frame_interval`.
    std::optional<TimeDelta> frame_duration;
    Resolution resolution;
    VideoTrackInterface::ContentHint content_hint =
        VideoTrackInterface::ContentHint::kNone;
    // When true, repeats the same image buffer instead of generating new
    // frames.
    bool repeat_frame = false;
    // When set, frames are encoded as the temporal layer structure of this
    // pattern prescribes. Ownership is transferred to the fixture, which keeps
    // the pattern alive so that it can span several `Encode` calls; a
    // subsequent call leaving this unset continues the same pattern. If no
    // pattern has been set, all frames are encoded in a single temporal layer,
    // referencing and updating buffer 0.
    std::unique_ptr<TemporalLayerPatternForTest> temporal_layer_pattern;
  };

  void Encode(EncodeSettings settings) {
    ASSERT_TRUE(settings.temporal_layer_pattern == nullptr ||
                !settings.frame_duration);
    if (settings.temporal_layer_pattern != nullptr) {
      temporal_layer_pattern_ = std::move(settings.temporal_layer_pattern);
    }

    scoped_refptr<VideoFrameBuffer> frame;
    for (int i = 0; i < settings.num_frames; ++i) {
      if (frame == nullptr || !settings.repeat_frame) {
        frame = NextFrame(settings.resolution);
      }

      std::optional<TemporalLayerPatternForTest::FrameConfig> frame_config;
      if (temporal_layer_pattern_ != nullptr) {
        frame_config = temporal_layer_pattern_->NextFrameConfig();
      }
      const int temporal_id = frame_config ? frame_config->temporal_id : 0;
      // A temporal layer is given a share of the stream bitrate, and the
      // frames of the layer split that share between them. A layer that only
      // holds every fourth frame therefore gives each of its frames four times
      // the bit budget its share of the bitrate would suggest, which is what
      // `frame_budget_factor` accounts for. The duration always stays the
      // interval to the next frame of the stream.
      const TimeDelta frame_duration =
          settings.frame_duration.value_or(settings.frame_interval);
      const DataRate target_bitrate =
          frame_config ? settings.target_bitrate * frame_config->rate_factor
                       : settings.target_bitrate;
      const DataSize ideal_frame_size =
          settings.target_bitrate * frame_duration *
          (frame_config
               ? temporal_layer_pattern_->frame_budget_factor(temporal_id)
               : 1.0);

      EncOut out;
      Fb builder;
      builder.Res(settings.resolution)
          .T(temporal_id)
          .Cbr({.duration = frame_duration, .target_bitrate = target_bitrate})
          .Out(out);
      const bool is_keyframe = is_first_frame_;
      if (is_first_frame_) {
        builder.Key().Upd(0);
        is_first_frame_ = false;
      } else if (frame_config) {
        builder.Delta().Upd(frame_config->update_buffer);
        if (frame_config->reference_buffer) {
          builder.Ref({*frame_config->reference_buffer});
        }
      } else {
        builder.Delta().Ref({0}).Upd(0);
      }
      encoder_->Encode(
          frame,
          TemporalUnitSettings(settings.content_hint, current_timestamp_),
          ToVec({builder.Build()}));

      ASSERT_THAT(out, HasBitstreamAndMetaData());
      std::optional<double> psnr;
      if (test_decoder_ != nullptr) {
        VideoFrame decoded = test_decoder_->Decode(out.bitstream);
        psnr = Psnr(frame->ToI420(), decoded);
      }

      encoded_frames_.push_back(
          {.actual = DataSize::Bytes(out.bitstream.size()),
           .ideal = ideal_frame_size,
           .duration = frame_duration,
           .psnr = psnr,
           .temporal_id = temporal_id,
           .is_keyframe = is_keyframe});
      current_timestamp_ += settings.frame_interval;
      time_controller_.AdvanceTime(settings.frame_interval);
    }
  }

  void VerifyTotalDeviation(double max_deviation_pct) {
    AccumulatedData total;
    for (const auto& frame : encoded_frames_) {
      total.Add(frame);
    }
    double total_deviation_pct = total.deviation_pct();
    RTC_LOG(LS_VERBOSE) << "total_bytes=" << total.actual.bytes()
                        << " optimal=" << total.ideal.bytes()
                        << " deviation=" << total_deviation_pct << "%";
    EXPECT_NEAR(total_deviation_pct, 0.0, max_deviation_pct)
        << "Bitrate deviation " << total_deviation_pct
        << "% exceeded tolerance " << max_deviation_pct
        << "% (actual: " << total.actual.bytes()
        << " bytes, target: " << total.ideal.bytes() << " bytes)";
  }

  void VerifyFrameBasedSlidingWindowBitrateDeviation(
      int window_frames,
      double min_allowed_dev_pct,
      double max_allowed_dev_pct) {
    AccumulatedData window_data;
    std::deque<AccumulatedData> window;
    std::optional<double> max_window_dev_pct;
    std::optional<double> min_window_dev_pct;

    for (const auto& frame : encoded_frames_) {
      window.push_back(frame);
      window_data.Add(frame);

      if (window.size() > static_cast<size_t>(window_frames)) {
        window_data.Subtract(window.front());
        window.pop_front();
      }

      if (window.size() == static_cast<size_t>(window_frames)) {
        double window_dev_pct = window_data.deviation_pct();
        if (!max_window_dev_pct || window_dev_pct > *max_window_dev_pct) {
          max_window_dev_pct = window_dev_pct;
        }
        if (!min_window_dev_pct || window_dev_pct < *min_window_dev_pct) {
          min_window_dev_pct = window_dev_pct;
        }
      }
    }

    ASSERT_TRUE(min_window_dev_pct.has_value());
    ASSERT_TRUE(max_window_dev_pct.has_value());

    RTC_LOG(LS_VERBOSE) << "sliding " << window_frames
                        << "-frame window deviation range: ["
                        << *min_window_dev_pct << "%, " << *max_window_dev_pct
                        << "%]";

    EXPECT_GE(*min_window_dev_pct, min_allowed_dev_pct);
    EXPECT_LE(*max_window_dev_pct, max_allowed_dev_pct);
  }

  void VerifyTimeBasedSlidingWindowBitrateDeviation(
      TimeDelta window_duration,
      std::optional<double> min_allowed_dev_pct,
      double max_allowed_dev_pct) {
    AccumulatedData window_data;
    std::deque<AccumulatedData> window;
    std::optional<double> max_window_dev_pct;
    std::optional<double> min_window_dev_pct;

    for (const auto& frame : encoded_frames_) {
      window.push_back(frame);
      window_data.Add(frame);

      while (window_data.duration >= window_duration) {
        double window_dev_pct = window_data.deviation_pct();
        if (!max_window_dev_pct || window_dev_pct > *max_window_dev_pct) {
          max_window_dev_pct = window_dev_pct;
        }
        if (!min_window_dev_pct || window_dev_pct < *min_window_dev_pct) {
          min_window_dev_pct = window_dev_pct;
        }

        window_data.Subtract(window.front());
        window.pop_front();
      }
    }

    ASSERT_TRUE(min_window_dev_pct.has_value());
    ASSERT_TRUE(max_window_dev_pct.has_value());

    RTC_LOG(LS_VERBOSE) << "sliding " << window_duration.seconds()
                        << "s window deviation range: [" << *min_window_dev_pct
                        << "%, " << *max_window_dev_pct << "%]";

    if (min_allowed_dev_pct.has_value()) {
      EXPECT_GE(*min_window_dev_pct, *min_allowed_dev_pct);
    }
    EXPECT_LE(*max_window_dev_pct, max_allowed_dev_pct);
  }

  // Verifies that the encoder acted on the requested temporal layer
  // allocation. Each layer is checked against the bit budget that was
  // requested for it, in the same way `VerifyTotalDeviation` checks the stream
  // as a whole. Additionally, since every sensible allocation gives the lower
  // temporal layers a larger per frame bit budget than the higher ones, the
  // encoded frames must follow that order too.
  void VerifyTemporalLayerAllocation(double max_deviation_pct) {
    ASSERT_TRUE(temporal_layer_pattern_ != nullptr);
    const int num_temporal_layers =
        temporal_layer_pattern_->num_temporal_layers();

    std::vector<AccumulatedData> per_layer(num_temporal_layers);
    std::vector<int> frames_per_layer(num_temporal_layers, 0);
    std::vector<double> psnr_sum_per_layer(num_temporal_layers, 0.0);
    for (const AccumulatedData& frame : encoded_frames_) {
      ASSERT_LT(frame.temporal_id, num_temporal_layers);
      per_layer[frame.temporal_id].Add(frame);
      ++frames_per_layer[frame.temporal_id];
      psnr_sum_per_layer[frame.temporal_id] += frame.psnr.value_or(0.0);
    }

    for (int tid = 0; tid < num_temporal_layers; ++tid) {
      ASSERT_GT(frames_per_layer[tid], 0);
      RTC_LOG(LS_VERBOSE) << "T" << tid << " frames=" << frames_per_layer[tid]
                          << " bytes/frame="
                          << per_layer[tid].actual.bytes() /
                                 frames_per_layer[tid]
                          << " deviation=" << per_layer[tid].deviation_pct()
                          << "% psnr="
                          << psnr_sum_per_layer[tid] / frames_per_layer[tid];
    }

    for (int tid = 0; tid < num_temporal_layers; ++tid) {
      const double deviation_pct = per_layer[tid].deviation_pct();
      EXPECT_NEAR(deviation_pct, 0.0, max_deviation_pct)
          << "T" << tid << " bitrate deviation " << deviation_pct
          << "% exceeded tolerance " << max_deviation_pct
          << "% (actual: " << per_layer[tid].actual.bytes()
          << " bytes, target: " << per_layer[tid].ideal.bytes() << " bytes)";
    }

    // Lower temporal layers are given a larger per frame bit budget, so the
    // encoded frames have to follow the same order. A distribution that asks
    // for the same budget on both sides of a layer boundary says nothing about
    // the order the frames should come out in, so those pairs are skipped.
    for (int tid = 1; tid < num_temporal_layers; ++tid) {
      const double requested_below =
          static_cast<double>(per_layer[tid - 1].ideal.bytes()) /
          frames_per_layer[tid - 1];
      const double requested_above =
          static_cast<double>(per_layer[tid].ideal.bytes()) /
          frames_per_layer[tid];
      if (requested_below <= requested_above) {
        continue;
      }
      EXPECT_GT(per_layer[tid - 1].actual.bytes() / frames_per_layer[tid - 1],
                per_layer[tid].actual.bytes() / frames_per_layer[tid])
          << "T" << (tid - 1) << " frames are not larger than T" << tid
          << " frames";
    }
  }

  // The mean PSNR of the frames belonging to temporal layer `temporal_id`.
  double MeanPsnrOfTemporalLayer(int temporal_id) const {
    double sum = 0.0;
    int count = 0;
    for (const AccumulatedData& frame : encoded_frames_) {
      if (frame.temporal_id == temporal_id) {
        RTC_CHECK(frame.psnr.has_value());
        sum += *frame.psnr;
        ++count;
      }
    }
    RTC_CHECK_GT(count, 0);
    return sum / count;
  }

  // The mean PSNR of all encoded frames.
  double MeanPsnr() const {
    double sum = 0.0;
    for (const AccumulatedData& frame : encoded_frames_) {
      RTC_CHECK(frame.psnr.has_value());
      sum += *frame.psnr;
    }
    RTC_CHECK(!encoded_frames_.empty());
    return sum / encoded_frames_.size();
  }

  GlobalSimulatedTimeController time_controller_{Timestamp::Zero()};
  Environment env_{CreateTestEnvironment({.time = &time_controller_})};
  std::unique_ptr<VideoEncoderFactoryInterface> encoder_factory_;
  std::unique_ptr<VideoEncoderInterface> encoder_;
  std::unique_ptr<VideoDecoderFactory> decoder_factory_;
  std::unique_ptr<TestDecoder> test_decoder_;
  std::unique_ptr<test::FrameGeneratorInterface> frame_generator_;
  std::unique_ptr<TemporalLayerPatternForTest> temporal_layer_pattern_;
  std::vector<AccumulatedData> encoded_frames_;
  Timestamp current_timestamp_ = Timestamp::Zero();
  bool is_first_frame_ = true;
};

class VideoEncoderRateControlTest
    : public VideoEncoderRateControlTestBase,
      public ::testing::WithParamInterface<FactoryCreator> {
 protected:
  void SetUp() override { encoder_factory_ = GetParam()(); }
};

TEST_P(VideoEncoderRateControlTest, ConstantQpMatchesBitstreamAndEncoderQp) {
  VideoEncoderFactoryInterface::Capabilities capabilities =
      encoder_factory_->GetEncoderCapabilities();
  if (!capabilities.bitrate_control().rc_modes().contains(
          VideoEncoderFactoryInterface::RateControlMode::kCqp)) {
    GTEST_SKIP() << "Encoder does not support CQP mode.";
  }

  int min_qp = capabilities.bitrate_control().min_qp();
  int max_qp = capabilities.bitrate_control().max_qp();

  VideoEncoderFactoryInterface::StaticEncoderSettings static_settings =
      StaticEncoderSettingsBuilder()
          .MaxEncodeDimensions(kDefaultResolution)
          .EncodingFormat({.sub_sampling = EncodingFormat::SubSampling::k420,
                           .bit_depth = 8})
          .CqpRcMode()
          .MaxNumberOfThreads(1)
          .Build();

  QpParserForTest qp_parser;
  std::unique_ptr<test::FrameGeneratorInterface> frame_generator =
      CreateFrameGenerator();
  std::unique_ptr<VideoEncoderInterface> enc =
      encoder_factory_->CreateEncoder(static_settings, {});
  ASSERT_NE(enc, nullptr);

  int64_t timestamp_ms = 0;
  bool is_first_frame = true;

  for (int qp = min_qp; qp <= max_qp; ++qp) {
    scoped_refptr<VideoFrameBuffer> frame = frame_generator->NextFrame().buffer;
    EncOut out;
    if (is_first_frame) {
      enc->Encode(
          frame, TemporalUnitSettings(Timestamp::Millis(timestamp_ms)),
          ToVec({Fb().Cqp(qp).Res(kDefaultResolution).Upd(0).Key().Out(out)}));
      is_first_frame = false;
    } else {
      enc->Encode(
          frame, TemporalUnitSettings(Timestamp::Millis(timestamp_ms)),
          ToVec(
              {Fb().Cqp(qp).Res(kDefaultResolution).Ref({0}).Upd(0).Out(out)}));
    }
    timestamp_ms += 100;

    ASSERT_THAT(out, HasBitstreamAndMetaData());
    const EncodedData& ed = std::get<EncodedData>(out.res);
    // libaom quantizer resolution has step 4 across the 0-255 qindex range.
    EXPECT_NEAR(ed.encoded_qp, qp, 4);

    std::optional<uint32_t> parsed_qp = qp_parser.Parse(
        encoder_factory_->CodecName(), /*spatial_idx=*/0, out.bitstream);
    ASSERT_TRUE(parsed_qp.has_value())
        << "Failed to parse QP from bitstream for codec "
        << encoder_factory_->CodecName() << " at target QP " << qp;
    EXPECT_EQ(*parsed_qp, static_cast<uint32_t>(ed.encoded_qp));
  }
}

constexpr Resolution kQvgaResolution = {.width = 320, .height = 180};
constexpr Resolution kVgaResolution = {.width = 640, .height = 360};
constexpr Resolution kHdResolution = {.width = 1280, .height = 720};

struct FixedBitrateTestParams {
  std::string name;
  Resolution resolution;
  DataRate target_bitrate;
  TimeDelta duration;
  double max_deviation_pct;
};

class FixedBitrateRateControlTest
    : public VideoEncoderRateControlTestBase,
      public ::testing::WithParamInterface<
          std::tuple<FactoryCreator, FixedBitrateTestParams>> {
 protected:
  void SetUp() override { encoder_factory_ = std::get<0>(GetParam())(); }
};

TEST_P(FixedBitrateRateControlTest, AdheresToTargetBitrate) {
  if (!SupportsCbr()) {
    GTEST_SKIP() << "Encoder does not support CBR mode.";
  }
  const FixedBitrateTestParams& params = std::get<1>(GetParam());
  SetUpCbrEncoder(params.resolution);

  constexpr TimeDelta kFrameInterval = 1 / Frequency::Hertz(30);
  const int num_frames =
      (params.duration.us() + kFrameInterval.us() / 2) / kFrameInterval.us();

  Encode({.num_frames = num_frames,
          .target_bitrate = params.target_bitrate,
          .frame_interval = kFrameInterval,
          .resolution = params.resolution});
  VerifyTotalDeviation(params.max_deviation_pct);
}

// Verifies that the intra frame allowance has an effect, by comparing the
// keyframe produced with the smallest possible allowance against the one
// produced with the default allowance.
TEST_P(VideoEncoderRateControlTest, MaxIntraBitrateFactorLimitsKeyframeSize) {
  if (!SupportsCbr()) {
    GTEST_SKIP() << "Encoder does not support CBR mode.";
  }
  if (!SupportsCbrSetting(
          VideoEncoderFactoryInterface::CbrSetting::kMaxIntraBitrateFactor)) {
    GTEST_SKIP() << "Encoder does not limit the size of intra frames.";
  }

  constexpr TimeDelta kFrameInterval = 1 / Frequency::Hertz(30);
  constexpr DataRate kTargetBitrate = DataRate::KilobitsPerSec(300);

  auto encode_keyframe = [&](double max_intra_bitrate_factor) {
    SetUpCbrEncoder(kQvgaResolution, max_intra_bitrate_factor);
    Encode({.num_frames = 1,
            .target_bitrate = kTargetBitrate,
            .frame_interval = kFrameInterval,
            .resolution = kQvgaResolution});
    return encoded_frames_.front().actual;
  };

  DataSize keyframe_with_min_allowance = encode_keyframe(1.0);
  DataSize keyframe_with_default_allowance =
      encode_keyframe(kMaxIntraBitrateFactor);
  EXPECT_LT(keyframe_with_min_allowance, keyframe_with_default_allowance);
}

// Verifies that dynamically changing the bitrate target follows the target
// within reasonable bounds, evaluated over a 2-second sliding window and
// overall accumulated bytes.
TEST_P(VideoEncoderRateControlTest, ChangingBitrateTargetVga) {
  if (!SupportsCbr()) {
    GTEST_SKIP() << "Encoder does not support CBR mode.";
  }
  SetUpCbrEncoder(kVgaResolution);

  constexpr TimeDelta kFrameInterval = 1 / Frequency::Hertz(30);
  constexpr int kWindowFrames = 60;  // 2-second sliding window (30 fps).

  // 1. 500 kbps for 2s (60 frames).
  Encode({.num_frames = 60,
          .target_bitrate = DataRate::KilobitsPerSec(500),
          .frame_interval = kFrameInterval,
          .resolution = kVgaResolution});
  // 2. Drop to 100 kbps for 1s (30 frames).
  Encode({.num_frames = 30,
          .target_bitrate = DataRate::KilobitsPerSec(100),
          .frame_interval = kFrameInterval,
          .resolution = kVgaResolution});
  // 3. Increase by 50 kbps every 200 ms (6 frames) until reaching 500 kbps.
  for (int rate_kbps = 150; rate_kbps < 500; rate_kbps += 50) {
    Encode({.num_frames = 6,
            .target_bitrate = DataRate::KilobitsPerSec(rate_kbps),
            .frame_interval = kFrameInterval,
            .resolution = kVgaResolution});
  }
  // 4. Stay at 500 kbps for 1s (30 frames).
  Encode({.num_frames = 30,
          .target_bitrate = DataRate::KilobitsPerSec(500),
          .frame_interval = kFrameInterval,
          .resolution = kVgaResolution});

  // (1) Check that deviation from optimal behavior over any 2-second sliding
  // window does not exceed 20%.
  VerifyFrameBasedSlidingWindowBitrateDeviation(kWindowFrames,
                                                /*min_allowed_dev_pct=*/-20.0,
                                                /*max_allowed_dev_pct=*/20.0);

  // (2) Check that the sum of bytes sent is within 5% of the optimal behavior.
  VerifyTotalDeviation(/*max_deviation_pct=*/5.0);
}

// Verifies that the CBR targets are adhered to even when input frame rate
// changes. Evaluated over a 2-second sliding window and overall accumulated
// bytes.
TEST_P(VideoEncoderRateControlTest, ChangingFramerateVga) {
  if (!SupportsCbr()) {
    GTEST_SKIP() << "Encoder does not support CBR mode.";
  }
  SetUpCbrEncoder(kVgaResolution);

  constexpr DataRate kTargetBitrate = DataRate::KilobitsPerSec(500);

  auto encode_segment = [&](Frequency framerate, TimeDelta duration) {
    TimeDelta frame_interval = 1 / framerate;
    int num_frames =
        (duration.us() + frame_interval.us() / 2) / frame_interval.us();
    Encode({.num_frames = num_frames,
            .target_bitrate = kTargetBitrate,
            .frame_interval = frame_interval,
            .resolution = kVgaResolution});
  };

  // 1. 30 fps for 2s.
  encode_segment(Frequency::Hertz(30), TimeDelta::Seconds(2));
  // 2. Drop to 10 fps for 1s.
  encode_segment(Frequency::Hertz(10), TimeDelta::Seconds(1));
  // 3. Step up gradually back to 30 fps (15 fps, 20 fps, 25 fps for 200ms
  // each).
  encode_segment(Frequency::Hertz(15), TimeDelta::Millis(200));
  encode_segment(Frequency::Hertz(20), TimeDelta::Millis(200));
  encode_segment(Frequency::Hertz(25), TimeDelta::Millis(200));
  // 4. Stay at 30 fps for 2s.
  encode_segment(Frequency::Hertz(30), TimeDelta::Seconds(2));

  // (1) Check that deviation from optimal behavior over any 1-second sliding
  // window does not exceed 20%.
  VerifyTimeBasedSlidingWindowBitrateDeviation(TimeDelta::Seconds(1),
                                               /*min_allowed_dev_pct=*/-20.0,
                                               /*max_allowed_dev_pct=*/20.0);

  // (2) Check that the sum of bytes sent is within 5% of the optimal behavior.
  VerifyTotalDeviation(/*max_deviation_pct=*/5.0);
}

// Verifies that the encoder adheres to CBR target bitrate when the video input
// periodically switches between different camera views every 5 seconds.
TEST_P(VideoEncoderRateControlTest, CameraSwitchingHd) {
  if (!SupportsCbr()) {
    GTEST_SKIP() << "Encoder does not support CBR mode.";
  }

  constexpr Resolution kResolution = kHdResolution;
  constexpr Frequency kFramerate = Frequency::Hertz(30);
  constexpr TimeDelta kFrameInterval = 1 / kFramerate;
  constexpr DataRate kTargetBitrate = DataRate::KilobitsPerSec(2000);
  constexpr TimeDelta kDuration = TimeDelta::Seconds(30);
  constexpr TimeDelta kSwitchInterval = TimeDelta::Seconds(5);
  const int num_frames =
      (kDuration.us() + kFrameInterval.us() / 2) / kFrameInterval.us();

  const std::vector<std::string> clip_paths = {
      test::ResourcePath("ConferenceMotion_1280_720_50", "yuv"),
      test::ResourcePath("FourPeople_1280x720_30", "yuv"),
      test::ResourcePath("reference_less_video_test_file", "y4m"),
  };

  std::unique_ptr<test::FrameGeneratorInterface> generator =
      test::CreateSwitchingFrameGenerator(
          clip_paths,
          {.width = static_cast<size_t>(kResolution.width),
           .height = static_cast<size_t>(kResolution.height)},
          kFramerate.hertz(), kSwitchInterval,
          test::YuvFrameReaderImpl::RepeatMode::kPingPong);

  SetUpCbrEncoder(kResolution);
  SetFrameGenerator(std::move(generator));
  Encode({.num_frames = num_frames,
          .target_bitrate = kTargetBitrate,
          .frame_interval = kFrameInterval,
          .resolution = kResolution});

  VerifyTotalDeviation(/*max_deviation_pct=*/5.0);
}

// Verified that the encoder adheres to CBR target bitrate when the video input
// has very high complexity both in terms of motion and high frequency detail.
TEST_P(VideoEncoderRateControlTest, SyntheticChaoticMotionStressHd) {
  constexpr Resolution kResolution = {.width = 1280, .height = 720};
  constexpr Frequency kFramerate = Frequency::Hertz(30);
  constexpr TimeDelta kFrameInterval = 1 / kFramerate;
  constexpr DataRate kTargetBitrate = DataRate::KilobitsPerSec(2000);
  constexpr TimeDelta kDuration = TimeDelta::Seconds(10);
  const int num_frames =
      (kDuration.us() + kFrameInterval.us() / 2) / kFrameInterval.us();

  test::PendulumFrameGenerator::Config config;
  config.target_resolution = {
      .width = static_cast<size_t>(kResolution.width),
      .height = static_cast<size_t>(kResolution.height)};
  config.fps = kFramerate.hertz();
  config.min_zoom = 1.2;
  config.max_zoom = 3.0;
  config.zoom_speed = 0.3;
  config.noise_level = 20;

  SetUpCbrEncoder(kResolution);
  SetFrameGenerator(std::make_unique<test::PendulumFrameGenerator>(config));
  Encode({.num_frames = num_frames,
          .target_bitrate = kTargetBitrate,
          .frame_interval = kFrameInterval,
          .resolution = kResolution});

  VerifyTotalDeviation(/*max_deviation_pct=*/5.0);
}

// The number of groups of pictures a sequence has to cover. The base layer
// contributes a single frame to each, so this is also the number of base layer
// frames the per layer measurements are based on. Eight is enough to be within
// a few percentage points of the value this converges to, well inside the
// tolerances below.
constexpr int kMinGroupsOfPictures = 8;

// How far a single temporal layer may be off the share of the bitrate it was
// allocated. This is wider than the tolerance on the stream as a whole because
// the encoder is free to move bits between the layers as long as the total
// holds, and because a layer holds only a fraction of the bits, so the cost of
// starting the sequence weighs more heavily on it. The worst layer measured
// over the sequences below is 15% off.
//
// TODO(bugs.webrtc.org/496266459): Most of what is left is that start-up cost,
// which the encoder works off over a window far longer than these sequences;
// running them for 30s instead brings the worst layer to 5.6%. That triples
// the runtime of these tests, so it is not worth it until the tolerance has to
// be this tight to catch something.
constexpr double kMaxLayerDeviationPct = 20.0;

// A frame cannot be given an arbitrarily large share of the bitrate: the rate
// controller has to keep its buffer from draining, so a single frame asking
// for a significant part of the buffer will simply not be delivered. Temporal
// layer distributions are only exercised while they stay below this.
constexpr TimeDelta kMaxFrameBudget = kCbrTargetBufferSize / 4;

struct TemporalLayerTestParams {
  std::string name;
  Resolution resolution;
  DataRate target_bitrate;
  // Returns the bitrate fractions for a given number of temporal layers, see
  // `TemporalLayerPatternForTest`.
  std::vector<double> (*distribution)(int num_temporal_layers);
};

class TemporalLayerRateControlTest
    : public VideoEncoderRateControlTestBase,
      public ::testing::WithParamInterface<
          std::tuple<FactoryCreator, TemporalLayerTestParams>> {
 protected:
  void SetUp() override { encoder_factory_ = std::get<0>(GetParam())(); }
};

// Verifies that the encoder adheres to the target bitrate when the bit budget
// is distributed over a temporal layer structure, and that it acts on the
// requested per temporal layer distribution, for every temporal layer count
// the encoder supports.
TEST_P(TemporalLayerRateControlTest, AdheresToLayerAllocation) {
  if (!SupportsCbr()) {
    GTEST_SKIP() << "Encoder does not support CBR mode.";
  }
  if (!SupportsTemporalLayers(2)) {
    GTEST_SKIP() << "Encoder does not support temporal layers.";
  }
  const TemporalLayerTestParams& params = std::get<1>(GetParam());

  constexpr TimeDelta kFrameInterval = 1 / Frequency::Hertz(30);
  constexpr TimeDelta kMinDuration = TimeDelta::Seconds(10);

  for (int num_temporal_layers = 2; num_temporal_layers <= MaxTemporalLayers();
       ++num_temporal_layers) {
    SCOPED_TRACE(num_temporal_layers);

    auto pattern = std::make_unique<TemporalLayerPatternForTest>(
        num_temporal_layers, NumReferenceBuffers(),
        params.distribution(num_temporal_layers));
    // The base layer budget only grows with the number of layers, so no
    // higher layer count is realizable either.
    if (kFrameInterval * pattern->frame_budget_factor(0) > kMaxFrameBudget) {
      break;
    }

    const int num_frames =
        std::max<int>(kMinDuration / kFrameInterval,
                      kMinGroupsOfPictures *
                          TemporalLayerPatternForTest::FramesPerGroupOfPictures(
                              num_temporal_layers));

    SetUpCbrEncoder(params.resolution);
    EnableDecoder();
    Encode({.num_frames = num_frames,
            .target_bitrate = params.target_bitrate,
            .frame_interval = kFrameInterval,
            .resolution = params.resolution,
            .temporal_layer_pattern = std::move(pattern)});

    VerifyTotalDeviation(/*max_deviation_pct=*/5.0);
    VerifyTemporalLayerAllocation(kMaxLayerDeviationPct);

    // A receiver that only decodes the base layer sees a lower frame rate at a
    // higher quality per frame, so base layer frames must not be worse than
    // the average frame.
    EXPECT_GT(MeanPsnrOfTemporalLayer(0), MeanPsnr());
  }
}

// Verifies that the encoder adheres to the target bitrate when the bit budget
// is distributed over spatial layers, and that each spatial layer gets the
// share it was allocated, for every spatial layer count the encoder supports.
//
// This uses the simplest spatial structure, SxT1: every temporal unit holds a
// frame for each spatial layer, predicted from the same layer in the previous
// temporal unit and from the layer below in the same temporal unit. Unlike the
// scalability modes of that name, all layers have the same resolution, since
// only the bitrate allocation is of interest here.
TEST_P(VideoEncoderRateControlTest, SpatialLayerAllocation) {
  if (!SupportsCbr()) {
    GTEST_SKIP() << "Encoder does not support CBR mode.";
  }
  if (MaxSpatialLayers() < 2) {
    GTEST_SKIP() << "Encoder does not support spatial layers.";
  }

  constexpr Resolution kResolution = kQvgaResolution;
  constexpr TimeDelta kFrameInterval = 1 / Frequency::Hertz(30);
  constexpr DataRate kTargetBitrate = DataRate::KilobitsPerSec(300);
  constexpr int kNumTemporalUnits = TimeDelta::Seconds(10) / kFrameInterval;

  for (int num_spatial_layers = 2; num_spatial_layers <= MaxSpatialLayers();
       ++num_spatial_layers) {
    SCOPED_TRACE(num_spatial_layers);
    SetUpCbrEncoder(kResolution);

    // Each layer is given a larger share of the bitrate than the one below
    // it, as it would be if it had a higher resolution. The exact split does
    // not matter, only that the layers are asked for different amounts.
    const int weight_sum = num_spatial_layers * (num_spatial_layers + 1) / 2;
    std::vector<DataRate> layer_bitrates;
    for (int sid = 0; sid < num_spatial_layers; ++sid) {
      layer_bitrates.push_back(kTargetBitrate * (sid + 1) / weight_sum);
    }

    std::vector<AccumulatedData> per_layer(num_spatial_layers);
    for (int tu = 0; tu < kNumTemporalUnits; ++tu) {
      std::vector<EncOut> outs(num_spatial_layers);
      std::vector<VideoEncoderInterface::FrameEncodeSettings> frame_settings;
      for (int sid = 0; sid < num_spatial_layers; ++sid) {
        Fb builder;
        builder.Res(kResolution)
            .S(sid)
            .Cbr({.duration = kFrameInterval,
                  .target_bitrate = layer_bitrates[sid]})
            .Upd(sid)
            .Out(outs[sid]);
        if (tu == 0 && sid == 0) {
          builder.Key();
        } else {
          std::vector<int> references;
          if (tu > 0) {
            references.push_back(sid);
          }
          if (sid > 0) {
            references.push_back(sid - 1);
          }
          builder.Delta().Ref(references);
        }
        frame_settings.push_back(builder.Build());
      }
      encoder_->Encode(NextFrame(kResolution),
                       TemporalUnitSettings(current_timestamp_),
                       std::move(frame_settings));

      for (int sid = 0; sid < num_spatial_layers; ++sid) {
        ASSERT_THAT(outs[sid], HasBitstreamAndMetaData());
        const AccumulatedData frame = {
            .actual = DataSize::Bytes(outs[sid].bitstream.size()),
            .ideal = layer_bitrates[sid] * kFrameInterval,
            .duration = kFrameInterval,
            .is_keyframe = tu == 0 && sid == 0};
        encoded_frames_.push_back(frame);
        per_layer[sid].Add(frame);
      }
      current_timestamp_ += kFrameInterval;
      time_controller_.AdvanceTime(kFrameInterval);
    }

    VerifyTotalDeviation(/*max_deviation_pct=*/5.0);
    // The keyframe is part of the base layer, so that layer carries most of
    // the cost of starting the sequence, and the more layers the bitrate is
    // split over, the smaller its share and the more that cost weighs.
    for (int sid = 0; sid < num_spatial_layers; ++sid) {
      EXPECT_NEAR(per_layer[sid].deviation_pct(), 0.0, 8.0)
          << "S" << sid << " (actual: " << per_layer[sid].actual.bytes()
          << " bytes, target: " << per_layer[sid].ideal.bytes() << " bytes)";
    }
  }
}

// TODO(bugs.webrtc.org/496266459): Add tempo-spatial layer allocation tests,
// e.g. structures where not all temporal units have all spatial layers.

// Verifies that the encoder behaves well in screenshare scenarios with mostly
// static content combined with intermittent slide changes at high resolution.
TEST_P(VideoEncoderRateControlTest, ScreenshareSlideChangesFullHd) {
  if (!SupportsCbr()) {
    GTEST_SKIP() << "Encoder does not support CBR mode.";
  }

  constexpr Resolution kFullHdResolution = {.width = 1920, .height = 1080};
  constexpr Resolution kResolution = kFullHdResolution;
  constexpr Frequency kFramerate = Frequency::Hertz(30);
  constexpr TimeDelta kFrameInterval = 1 / kFramerate;
  constexpr DataRate kTargetBitrate = DataRate::KilobitsPerSec(2500);
  constexpr TimeDelta kSlideDuration = TimeDelta::Seconds(2);
  constexpr TimeDelta kTotalDuration = TimeDelta::Seconds(6);
  const int num_frames =
      (kTotalDuration.us() + kFrameInterval.us() / 2) / kFrameInterval.us();
  const int frames_per_slide =
      (kSlideDuration.us() + kFrameInterval.us() / 2) / kFrameInterval.us();

  const std::vector<std::string> slides = {
      test::ResourcePath("web_screenshot_1850_1110", "yuv"),
      test::ResourcePath("presentation_1850_1110", "yuv"),
      test::ResourcePath("difficult_photo_1850_1110", "yuv"),
  };
  std::unique_ptr<test::FrameGeneratorInterface> slide_generator =
      test::CreateFromYuvFileFrameGenerator(slides, /*width=*/1850,
                                            /*height=*/1110, frames_per_slide);

  SetUpCbrEncoder(kResolution);
  EnableDecoder();
  SetFrameGenerator(std::move(slide_generator));

  AccumulatedData total;
  double sum_delta_psnr = 0.0;
  int delta_count = 0;
  for (int i = 0; i < num_frames; ++i) {
    Encode({.num_frames = 1,
            .target_bitrate = kTargetBitrate,
            .frame_interval = kFrameInterval,
            .resolution = kResolution,
            .content_hint = VideoTrackInterface::ContentHint::kDetailed});
    size_t frame_idx = encoded_frames_.size() - 1;
    DataSize size = encoded_frames_[frame_idx].actual;
    int offset_in_slide = i % frames_per_slide;
    if (offset_in_slide == 0) {
      // Transition frame to a new slide.
      EXPECT_GT(size.bytes(), 0);
      EXPECT_LE(size, kTargetBitrate * TimeDelta::Millis(575));
    } else {
      // Delta frames during static hold or refinement.
      EXPECT_LE(size, kTargetBitrate * TimeDelta::Millis(330));
      if (encoded_frames_[frame_idx].psnr.has_value()) {
        sum_delta_psnr += *encoded_frames_[frame_idx].psnr;
        ++delta_count;
      }
    }
    total.Add(encoded_frames_[frame_idx]);
  }

  // Total bytes should stay safely within the allocated CBR channel budget.
  EXPECT_GT(total.actual.bytes(), 0);
  EXPECT_LE(total.actual, total.ideal * 1.05);

  // Quality must remain high during static slide periods despite low bitrate.
  ASSERT_GT(delta_count, 0);
  EXPECT_GT(sum_delta_psnr / delta_count, 40.0);
}

// Verifies that the encoder adheres to CBR targets during screenshare scrolling
// with alternating scrolling and paused intervals.
TEST_P(VideoEncoderRateControlTest, ScreenshareScrollingHd) {
  if (!SupportsCbr()) {
    GTEST_SKIP() << "Encoder does not support CBR mode.";
  }

  constexpr Resolution kResolution = kHdResolution;
  constexpr Frequency kFramerate = Frequency::Hertz(30);
  constexpr TimeDelta kFrameInterval = 1 / kFramerate;
  constexpr DataRate kTargetBitrate = DataRate::KilobitsPerSec(2000);
  constexpr TimeDelta kScrollDuration = TimeDelta::Seconds(2);
  constexpr TimeDelta kPauseDuration = TimeDelta::Seconds(1);
  constexpr TimeDelta kTotalDuration = TimeDelta::Seconds(6);
  const int num_frames =
      (kTotalDuration.us() + kFrameInterval.us() / 2) / kFrameInterval.us();

  const std::vector<std::string> slides = {
      test::ResourcePath("difficult_photo_1850_1110", "yuv"),
      test::ResourcePath("web_screenshot_1850_1110", "yuv"),
  };

  std::unique_ptr<test::FrameGeneratorInterface> scroll_generator =
      test::CreateScrollingInputFromYuvFilesFrameGenerator(
          &env_.clock(), slides, /*source_width=*/1850,
          /*source_height=*/1110, kResolution.width, kResolution.height,
          kScrollDuration.ms(), kPauseDuration.ms());

  SetUpCbrEncoder(kResolution);
  EnableDecoder();
  SetFrameGenerator(std::move(scroll_generator));
  Encode({.num_frames = num_frames,
          .target_bitrate = kTargetBitrate,
          .frame_interval = kFrameInterval,
          .resolution = kResolution,
          .content_hint = VideoTrackInterface::ContentHint::kDetailed});

  // During screenshare scrolling, bitrate undershoot during pause intervals is
  // expected and allowed; only verify that overshoot is bounded.
  VerifyTimeBasedSlidingWindowBitrateDeviation(
      TimeDelta::Seconds(3),
      /*min_allowed_dev_pct=*/std::nullopt,
      /*max_allowed_dev_pct=*/20.0);

  AccumulatedData total;
  std::optional<double> min_pause_psnr;
  double sum_pause_psnr = 0.0;
  int pause_count = 0;
  // 60 frames scroll (2s), 30 frames pause (1s), repeated.
  for (size_t i = 0; i < encoded_frames_.size(); ++i) {
    total.Add(encoded_frames_[i]);
    int mod = i % 90;
    if (mod >= 60 && encoded_frames_[i].psnr.has_value()) {
      double p = *encoded_frames_[i].psnr;
      min_pause_psnr = min_pause_psnr ? std::min(*min_pause_psnr, p) : p;
      sum_pause_psnr += p;
      ++pause_count;
    }
  }

  // Total bytes should stay safely within the allocated CBR channel budget.
  EXPECT_GT(total.actual.bytes(), 0);
  EXPECT_LE(total.actual, total.ideal * 1.05);

  // Quality must not dip when bitrate drops during pauses.
  ASSERT_TRUE(min_pause_psnr.has_value());
  EXPECT_GT(*min_pause_psnr, 33.0);
  EXPECT_GT(sum_pause_psnr / pause_count, 40.0);
}

// Verifies that the rate controller behaves well when entering "Zero-Hz" mode
// (where static frames are encoded at 1 Hz repeat rate while each frame's
// encoded CBR duration remains 1/target_fps) and subsequent resumption of
// regular motion.
TEST_P(VideoEncoderRateControlTest, ScreenshareZeroHzHd) {
  if (!SupportsCbr()) {
    GTEST_SKIP() << "Encoder does not support CBR mode.";
  }

  constexpr Resolution kResolution = kHdResolution;
  constexpr Frequency kFramerate = Frequency::Hertz(30);
  constexpr TimeDelta kFrameInterval = 1 / kFramerate;
  constexpr DataRate kTargetBitrate = DataRate::KilobitsPerSec(1500);

  SetUpCbrEncoder(kResolution);
  EnableDecoder();
  SetFrameGenerator(test::CreateFromYuvFileFrameGenerator(
      {test::ResourcePath("FourPeople_1280x720_30", "yuv")}, kResolution.width,
      kResolution.height, /*frame_repeat_count=*/1));

  // Phase 1: 2 seconds of normal 30 fps motion.
  constexpr int kPhase1Frames = 60;
  Encode({.num_frames = kPhase1Frames,
          .target_bitrate = kTargetBitrate,
          .frame_interval = kFrameInterval,
          .resolution = kResolution,
          .content_hint = VideoTrackInterface::ContentHint::kDetailed});

  // Phase 2: Enter Zero-Hz mode. We hold a static frame and encode at 1 Hz
  // (timestamp advances by 1s), but CBR duration is still 1/target_fps
  // (33.3ms).
  constexpr int kZeroHzFrames = 3;
  constexpr TimeDelta kZeroHzRepeatPeriod = TimeDelta::Seconds(1);

  size_t phase2_start_idx = encoded_frames_.size();
  Encode({
      .num_frames = kZeroHzFrames,
      .target_bitrate = kTargetBitrate,
      .frame_interval = kZeroHzRepeatPeriod,
      .frame_duration = kFrameInterval,
      .resolution = kResolution,
      .content_hint = VideoTrackInterface::ContentHint::kDetailed,
      .repeat_frame = true,
  });

  for (size_t i = phase2_start_idx; i < phase2_start_idx + kZeroHzFrames; ++i) {
    // Each 1 Hz frame must not burst to fill the entire 1-second gap; its ideal
    // CBR allocation is 1 nominal frame duration (target_bitrate *
    // kFrameInterval).
    EXPECT_LE(encoded_frames_[i].actual,
              kTargetBitrate * kFrameInterval * 125 / 100);
    // Quality must remain high for static repeat frames.
    ASSERT_TRUE(encoded_frames_[i].psnr.has_value());
    EXPECT_GT(*encoded_frames_[i].psnr, 40.0);
  }

  // Phase 3: Resume motion at 30 fps for 3 seconds.
  constexpr int kPhase3Frames = 90;
  Encode({.num_frames = kPhase3Frames,
          .target_bitrate = kTargetBitrate,
          .frame_interval = kFrameInterval,
          .resolution = kResolution,
          .content_hint = VideoTrackInterface::ContentHint::kDetailed});

  // Verify that the encoder survived Zero-Hz and after resuming motion,
  // the Phase 3 frames converge to the target bitrate.
  AccumulatedData phase3_total;
  for (size_t i = phase2_start_idx + kZeroHzFrames; i < encoded_frames_.size();
       ++i) {
    phase3_total.Add(encoded_frames_[i]);
  }
  RTC_LOG(LS_VERBOSE) << "Phase 3 total deviation: "
                      << phase3_total.deviation_pct()
                      << "% (actual=" << phase3_total.actual.bytes()
                      << " bytes, ideal=" << phase3_total.ideal.bytes()
                      << " bytes)";
  EXPECT_NEAR(phase3_total.deviation_pct(), 0.0, 5.0);
}

std::unique_ptr<VideoEncoderFactoryInterface> CreateLibaomAv1EncoderFactory() {
  return std::make_unique<LibaomAv1EncoderFactory>();
}

INSTANTIATE_TEST_SUITE_P(LibaomAv1,
                         VideoEncoderRateControlTest,
                         ::testing::Values(CreateLibaomAv1EncoderFactory));

const FixedBitrateTestParams kFixedBitrateConfigs[] = {
    {"VgaNormalBitrate", kVgaResolution, DataRate::KilobitsPerSec(500),
     TimeDelta::Seconds(5), 6.0},
    {"VgaLowBitrate", kVgaResolution, DataRate::KilobitsPerSec(100),
     TimeDelta::Seconds(10), 6.0},
    {"VgaHighBitrate", kVgaResolution, DataRate::KilobitsPerSec(1500),
     TimeDelta::Seconds(5), 5.0},
    {"QvgaNormalBitrate", kQvgaResolution, DataRate::KilobitsPerSec(125),
     TimeDelta::Seconds(5), 7.0},
    {"QvgaLowBitrate", kQvgaResolution, DataRate::KilobitsPerSec(25),
     TimeDelta::Seconds(10), 10.0},
    // TODO(bugs.webrtc.org/496266459): Bring this back to 5%. Most of what it
    // covers is the cost of starting a sequence, which the encoder works off
    // over a window far longer than the five seconds measured here, so a
    // longer measurement rather than a wider tolerance is the way down.
    {"QvgaHighBitrate", kQvgaResolution, DataRate::KilobitsPerSec(375),
     TimeDelta::Seconds(5), 7.0},
    {"HdNormalBitrate", kHdResolution, DataRate::KilobitsPerSec(2000),
     TimeDelta::Seconds(5), 5.0},
    {"HdLowBitrate", kHdResolution, DataRate::KilobitsPerSec(400),
     TimeDelta::Seconds(10), 5.0},
    {"HdHighBitrate", kHdResolution, DataRate::KilobitsPerSec(6000),
     TimeDelta::Seconds(5), 5.0},
};

std::string FixedBitrateTestName(
    const ::testing::TestParamInfo<
        std::tuple<FactoryCreator, FixedBitrateTestParams>>& info) {
  return std::get<1>(info.param).name;
}

INSTANTIATE_TEST_SUITE_P(
    LibaomAv1,
    FixedBitrateRateControlTest,
    ::testing::Combine(::testing::Values(CreateLibaomAv1EncoderFactory),
                       ::testing::ValuesIn(kFixedBitrateConfigs)),
    FixedBitrateTestName);

const TemporalLayerTestParams kTemporalLayerConfigs[] = {
    // Halving the per frame bit budget for every temporal layer makes it
    // proportional to the prediction distance, see `GeometricDistribution`.
    // The base layer frame budget then grows as `2^N/(N+1)` frame intervals -
    // 67 ms at three layers, but 533 ms at seven - so this stops short of
    // `max_temporal_layers()` once it exceeds `kMaxFrameBudget`.
    {"GeometricVga", kVgaResolution, DataRate::KilobitsPerSec(600),
     [](int num_temporal_layers) {
       return TemporalLayerPatternForTest::GeometricDistribution(
           num_temporal_layers, /*ratio=*/0.5);
     }},
    // Only spans `N:1` between the base and top layer instead of the geometric
    // `2^(N-1):1`, so it stays realizable all the way up. The pattern period
    // doubles for every added layer, which makes covering enough groups of
    // pictures expensive at high layer counts, hence the lower resolution.
    {"LinearQvga", kQvgaResolution, DataRate::KilobitsPerSec(300),
     &TemporalLayerPatternForTest::LinearDistribution},
};

std::string TemporalLayerTestName(
    const ::testing::TestParamInfo<
        std::tuple<FactoryCreator, TemporalLayerTestParams>>& info) {
  return std::get<1>(info.param).name;
}

INSTANTIATE_TEST_SUITE_P(
    LibaomAv1,
    TemporalLayerRateControlTest,
    ::testing::Combine(::testing::Values(CreateLibaomAv1EncoderFactory),
                       ::testing::ValuesIn(kTemporalLayerConfigs)),
    TemporalLayerTestName);

}  // namespace
}  // namespace webrtc
