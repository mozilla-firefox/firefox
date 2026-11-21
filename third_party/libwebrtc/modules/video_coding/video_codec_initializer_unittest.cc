/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/include/video_codec_initializer.h"

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "absl/types/optional.h"
#include "api/scoped_refptr.h"
#include "api/test/mock_fec_controller_override.h"
#include "api/video/builtin_video_bitrate_allocator_factory.h"
#include "api/video/video_bitrate_allocation.h"
#include "api/video/video_bitrate_allocator.h"
#include "api/video/video_bitrate_allocator_factory.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/vp8_temporal_layers.h"
#include "api/video_codecs/vp8_temporal_layers_factory.h"
#include "modules/video_coding/codecs/vp9/include/vp9_globals.h"
#include "rtc_base/checks.h"
#include "test/explicit_key_value_config.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

namespace {
static const int kDefaultWidth = 1280;
static const int kDefaultHeight = 720;
static const int kDefaultFrameRate = 30;
static const uint32_t kDefaultMinBitrateBps = 60000;
static const uint32_t kDefaultTargetBitrateBps = 2000000;
static const uint32_t kDefaultMaxBitrateBps = 2000000;
static const uint32_t kDefaultMinTransmitBitrateBps = 400000;
static const int kDefaultMaxQp = 48;
static const uint32_t kScreenshareTl0BitrateBps = 120000;
static const uint32_t kScreenshareConferenceTl0BitrateBps = 200000;
static const uint32_t kScreenshareCodecTargetBitrateBps = 200000;
static const uint32_t kScreenshareDefaultFramerate = 5;
// Bitrates for the temporal layers of the higher screenshare simulcast stream.
static const uint32_t kHighScreenshareTl0Bps = 800000;
static const uint32_t kHighScreenshareTl1Bps = 1200000;
}  // namespace

// TODO(sprang): Extend coverage to handle the rest of the codec initializer.
class VideoCodecInitializerTest : public ::testing::Test {
 public:
  VideoCodecInitializerTest() {}
  virtual ~VideoCodecInitializerTest() {}

 protected:
  void SetUpFor(VideoCodecType type,
                absl::optional<int> num_simulcast_streams,
                absl::optional<int> num_spatial_streams,
                int num_temporal_streams,
                bool screenshare) {
    config_ = VideoEncoderConfig();
    config_.codec_type = type;

    if (screenshare) {
      config_.min_transmit_bitrate_bps = kDefaultMinTransmitBitrateBps;
      config_.content_type = VideoEncoderConfig::ContentType::kScreen;
    }

    if (num_simulcast_streams.has_value()) {
      config_.number_of_streams = num_simulcast_streams.value();
    }
    if (type == VideoCodecType::kVideoCodecVP8) {
      ASSERT_FALSE(num_spatial_streams.has_value());
      VideoCodecVP8 vp8_settings = VideoEncoder::GetDefaultVp8Settings();
      vp8_settings.numberOfTemporalLayers = num_temporal_streams;
      config_.encoder_specific_settings = rtc::make_ref_counted<
          webrtc::VideoEncoderConfig::Vp8EncoderSpecificSettings>(vp8_settings);
    } else if (type == VideoCodecType::kVideoCodecVP9) {
      ASSERT_TRUE(num_spatial_streams.has_value());
      VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
      vp9_settings.numberOfSpatialLayers = num_spatial_streams.value();
      vp9_settings.numberOfTemporalLayers = num_temporal_streams;
      config_.encoder_specific_settings = rtc::make_ref_counted<
          webrtc::VideoEncoderConfig::Vp9EncoderSpecificSettings>(vp9_settings);
    }
  }

  void InitializeCodec() {
    frame_buffer_controller_.reset();
    codec_out_ =
        VideoCodecInitializer::SetupCodec(field_trials_, config_, streams_);
    bitrate_allocator_ = CreateBuiltinVideoBitrateAllocatorFactory()
                             ->CreateVideoBitrateAllocator(codec_out_);
    RTC_CHECK(bitrate_allocator_);

    // Make sure temporal layers instances have been created.
    if (codec_out_.codecType == VideoCodecType::kVideoCodecVP8) {
      Vp8TemporalLayersFactory factory;
      const VideoEncoder::Settings settings(VideoEncoder::Capabilities(false),
                                            1, 1000);
      frame_buffer_controller_ =
          factory.Create(codec_out_, settings, &fec_controller_override_);
    }
  }

  VideoStream DefaultStream(
      int width = kDefaultWidth,
      int height = kDefaultHeight,
      absl::optional<ScalabilityMode> scalability_mode = absl::nullopt) {
    VideoStream stream;
    stream.width = width;
    stream.height = height;
    stream.max_framerate = kDefaultFrameRate;
    stream.min_bitrate_bps = kDefaultMinBitrateBps;
    stream.target_bitrate_bps = kDefaultTargetBitrateBps;
    stream.max_bitrate_bps = kDefaultMaxBitrateBps;
    stream.max_qp = kDefaultMaxQp;
    stream.num_temporal_layers = 1;
    stream.active = true;
    stream.scalability_mode = scalability_mode;
    return stream;
  }

  VideoStream DefaultScreenshareStream() {
    VideoStream stream = DefaultStream();
    stream.min_bitrate_bps = 30000;
    stream.target_bitrate_bps = kScreenshareCodecTargetBitrateBps;
    stream.max_bitrate_bps = 1000000;
    stream.max_framerate = kScreenshareDefaultFramerate;
    stream.num_temporal_layers = 2;
    stream.active = true;
    return stream;
  }

  const test::ExplicitKeyValueConfig field_trials_{""};
  MockFecControllerOverride fec_controller_override_;

  // Input settings.
  VideoEncoderConfig config_;
  std::vector<VideoStream> streams_;

  // Output.
  VideoCodec codec_out_;
  std::unique_ptr<VideoBitrateAllocator> bitrate_allocator_;
  std::unique_ptr<Vp8FrameBufferController> frame_buffer_controller_;
};

TEST_F(VideoCodecInitializerTest, SingleStreamVp8Screenshare) {
  SetUpFor(VideoCodecType::kVideoCodecVP8, 1, absl::nullopt, 1, true);
  streams_.push_back(DefaultStream());
  InitializeCodec();

  VideoBitrateAllocation bitrate_allocation =
      bitrate_allocator_->Allocate(VideoBitrateAllocationParameters(
          kDefaultTargetBitrateBps, kDefaultFrameRate));
  EXPECT_EQ(1u, codec_out_.numberOfSimulcastStreams);
  EXPECT_EQ(1u, codec_out_.VP8()->numberOfTemporalLayers);
  EXPECT_EQ(kDefaultTargetBitrateBps, bitrate_allocation.get_sum_bps());
}

TEST_F(VideoCodecInitializerTest, SingleStreamVp8ScreenshareInactive) {
  SetUpFor(VideoCodecType::kVideoCodecVP8, 1, absl::nullopt, 1, true);
  VideoStream inactive_stream = DefaultStream();
  inactive_stream.active = false;
  streams_.push_back(inactive_stream);
  InitializeCodec();

  VideoBitrateAllocation bitrate_allocation =
      bitrate_allocator_->Allocate(VideoBitrateAllocationParameters(
          kDefaultTargetBitrateBps, kDefaultFrameRate));
  EXPECT_EQ(1u, codec_out_.numberOfSimulcastStreams);
  EXPECT_EQ(1u, codec_out_.VP8()->numberOfTemporalLayers);
  EXPECT_EQ(0U, bitrate_allocation.get_sum_bps());
}

TEST_F(VideoCodecInitializerTest, TemporalLayeredVp8ScreenshareConference) {
  SetUpFor(VideoCodecType::kVideoCodecVP8, 1, absl::nullopt, 2, true);
  streams_.push_back(DefaultScreenshareStream());
  InitializeCodec();
  bitrate_allocator_->SetLegacyConferenceMode(true);

  EXPECT_EQ(1u, codec_out_.numberOfSimulcastStreams);
  EXPECT_EQ(2u, codec_out_.VP8()->numberOfTemporalLayers);
  VideoBitrateAllocation bitrate_allocation =
      bitrate_allocator_->Allocate(VideoBitrateAllocationParameters(
          kScreenshareCodecTargetBitrateBps, kScreenshareDefaultFramerate));
  EXPECT_EQ(kScreenshareCodecTargetBitrateBps,
            bitrate_allocation.get_sum_bps());
  EXPECT_EQ(kScreenshareConferenceTl0BitrateBps,
            bitrate_allocation.GetBitrate(0, 0));
}

TEST_F(VideoCodecInitializerTest, TemporalLayeredVp8Screenshare) {
  SetUpFor(VideoCodecType::kVideoCodecVP8, 1, absl::nullopt, 2, true);
  streams_.push_back(DefaultScreenshareStream());
  InitializeCodec();

  EXPECT_EQ(1u, codec_out_.numberOfSimulcastStreams);
  EXPECT_EQ(2u, codec_out_.VP8()->numberOfTemporalLayers);
  VideoBitrateAllocation bitrate_allocation =
      bitrate_allocator_->Allocate(VideoBitrateAllocationParameters(
          kScreenshareCodecTargetBitrateBps, kScreenshareDefaultFramerate));
  EXPECT_EQ(kScreenshareCodecTargetBitrateBps,
            bitrate_allocation.get_sum_bps());
  EXPECT_EQ(kScreenshareTl0BitrateBps, bitrate_allocation.GetBitrate(0, 0));
}

TEST_F(VideoCodecInitializerTest, SimulcastVp8Screenshare) {
  SetUpFor(VideoCodecType::kVideoCodecVP8, 2, absl::nullopt, 1, true);
  streams_.push_back(DefaultScreenshareStream());
  VideoStream video_stream = DefaultStream();
  video_stream.max_framerate = kScreenshareDefaultFramerate;
  streams_.push_back(video_stream);
  InitializeCodec();

  EXPECT_EQ(2u, codec_out_.numberOfSimulcastStreams);
  EXPECT_EQ(1u, codec_out_.VP8()->numberOfTemporalLayers);
  const uint32_t max_bitrate_bps =
      streams_[0].target_bitrate_bps + streams_[1].max_bitrate_bps;
  VideoBitrateAllocation bitrate_allocation =
      bitrate_allocator_->Allocate(VideoBitrateAllocationParameters(
          max_bitrate_bps, kScreenshareDefaultFramerate));
  EXPECT_EQ(max_bitrate_bps, bitrate_allocation.get_sum_bps());
  EXPECT_EQ(static_cast<uint32_t>(streams_[0].target_bitrate_bps),
            bitrate_allocation.GetSpatialLayerSum(0));
  EXPECT_EQ(static_cast<uint32_t>(streams_[1].max_bitrate_bps),
            bitrate_allocation.GetSpatialLayerSum(1));
}

// Tests that when a video stream is inactive, then the bitrate allocation will
// be 0 for that stream.
TEST_F(VideoCodecInitializerTest, SimulcastVp8ScreenshareInactive) {
  SetUpFor(VideoCodecType::kVideoCodecVP8, 2, absl::nullopt, 1, true);
  streams_.push_back(DefaultScreenshareStream());
  VideoStream inactive_video_stream = DefaultStream();
  inactive_video_stream.active = false;
  inactive_video_stream.max_framerate = kScreenshareDefaultFramerate;
  streams_.push_back(inactive_video_stream);
  InitializeCodec();

  EXPECT_EQ(2u, codec_out_.numberOfSimulcastStreams);
  EXPECT_EQ(1u, codec_out_.VP8()->numberOfTemporalLayers);
  const uint32_t target_bitrate =
      streams_[0].target_bitrate_bps + streams_[1].target_bitrate_bps;
  VideoBitrateAllocation bitrate_allocation =
      bitrate_allocator_->Allocate(VideoBitrateAllocationParameters(
          target_bitrate, kScreenshareDefaultFramerate));
  EXPECT_EQ(static_cast<uint32_t>(streams_[0].max_bitrate_bps),
            bitrate_allocation.get_sum_bps());
  EXPECT_EQ(static_cast<uint32_t>(streams_[0].max_bitrate_bps),
            bitrate_allocation.GetSpatialLayerSum(0));
  EXPECT_EQ(0U, bitrate_allocation.GetSpatialLayerSum(1));
}

TEST_F(VideoCodecInitializerTest, HighFpsSimulcastVp8Screenshare) {
  // Two simulcast streams, the lower one using legacy settings (two temporal
  // streams, 5fps), the higher one using 3 temporal streams and 30fps.
  SetUpFor(VideoCodecType::kVideoCodecVP8, 2, absl::nullopt, 3, true);
  streams_.push_back(DefaultScreenshareStream());
  VideoStream video_stream = DefaultStream();
  video_stream.num_temporal_layers = 3;
  streams_.push_back(video_stream);
  InitializeCodec();

  EXPECT_EQ(2u, codec_out_.numberOfSimulcastStreams);
  EXPECT_EQ(3u, codec_out_.VP8()->numberOfTemporalLayers);
  const uint32_t max_bitrate_bps =
      streams_[0].target_bitrate_bps + streams_[1].max_bitrate_bps;
  VideoBitrateAllocation bitrate_allocation = bitrate_allocator_->Allocate(
      VideoBitrateAllocationParameters(max_bitrate_bps, kDefaultFrameRate));
  EXPECT_EQ(max_bitrate_bps, bitrate_allocation.get_sum_bps());
  EXPECT_EQ(static_cast<uint32_t>(streams_[0].target_bitrate_bps),
            bitrate_allocation.GetSpatialLayerSum(0));
  EXPECT_EQ(static_cast<uint32_t>(streams_[1].max_bitrate_bps),
            bitrate_allocation.GetSpatialLayerSum(1));
  EXPECT_EQ(kHighScreenshareTl0Bps, bitrate_allocation.GetBitrate(1, 0));
  EXPECT_EQ(kHighScreenshareTl1Bps - kHighScreenshareTl0Bps,
            bitrate_allocation.GetBitrate(1, 1));
}

TEST_F(VideoCodecInitializerTest, Vp9SvcDefaultLayering) {
  SetUpFor(VideoCodecType::kVideoCodecVP9, absl::nullopt, 3, 3, false);
  VideoStream stream = DefaultStream();
  stream.num_temporal_layers = 3;
  streams_.push_back(stream);

  InitializeCodec();
  EXPECT_EQ(codec_out_.VP9()->numberOfSpatialLayers, 3u);
  EXPECT_EQ(codec_out_.VP9()->numberOfTemporalLayers, 3u);
}

TEST_F(VideoCodecInitializerTest, Vp9SvcAdjustedLayering) {
  SetUpFor(VideoCodecType::kVideoCodecVP9, absl::nullopt, 3, 3, false);
  VideoStream stream = DefaultStream();
  stream.num_temporal_layers = 3;
  // Set resolution which is only enough to produce 2 spatial layers.
  stream.width = kMinVp9SpatialLayerLongSideLength * 2;
  stream.height = kMinVp9SpatialLayerShortSideLength * 2;

  streams_.push_back(stream);

  InitializeCodec();
  EXPECT_EQ(codec_out_.VP9()->numberOfSpatialLayers, 2u);
}

TEST_F(VideoCodecInitializerTest,
       Vp9SingleSpatialLayerMaxBitrateIsEqualToCodecMaxBitrate) {
  SetUpFor(VideoCodecType::kVideoCodecVP9, absl::nullopt, 1, 3, false);
  VideoStream stream = DefaultStream();
  stream.num_temporal_layers = 3;
  streams_.push_back(stream);

  InitializeCodec();
  EXPECT_EQ(codec_out_.spatialLayers[0].maxBitrate,
            kDefaultMaxBitrateBps / 1000);
}

TEST_F(VideoCodecInitializerTest,
       Vp9SingleSpatialLayerMaxBitrateIsEqualToCodecMaxBitrateWithL1T1) {
  SetUpFor(VideoCodecType::kVideoCodecVP9, 1, 1, 1, false);
  VideoStream stream = DefaultStream();
  stream.num_temporal_layers = 1;
  stream.scalability_mode = ScalabilityMode::kL1T1;
  streams_.push_back(stream);

  InitializeCodec();
  EXPECT_EQ(1u, codec_out_.VP9()->numberOfSpatialLayers);
  EXPECT_EQ(codec_out_.spatialLayers[0].minBitrate,
            kDefaultMinBitrateBps / 1000);
  EXPECT_EQ(codec_out_.spatialLayers[0].maxBitrate,
            kDefaultMaxBitrateBps / 1000);
}

TEST_F(VideoCodecInitializerTest,
       Vp9SingleSpatialLayerTargetBitrateIsEqualToCodecMaxBitrate) {
  SetUpFor(VideoCodecType::kVideoCodecVP9, absl::nullopt, 1, 1, true);
  VideoStream stream = DefaultStream();
  stream.num_temporal_layers = 1;
  streams_.push_back(stream);

  InitializeCodec();
  EXPECT_EQ(codec_out_.spatialLayers[0].targetBitrate,
            kDefaultMaxBitrateBps / 1000);
}

TEST_F(VideoCodecInitializerTest,
       Vp9KeepBitrateLimitsIfNumberOfSpatialLayersIsReducedToOne) {
  // Request 3 spatial layers for 320x180 input. Actual number of layers will be
  // reduced to 1 due to low input resolution but SVC bitrate limits should be
  // applied.
  SetUpFor(VideoCodecType::kVideoCodecVP9, absl::nullopt, 3, 3, false);
  VideoStream stream = DefaultStream();
  stream.width = 320;
  stream.height = 180;
  stream.num_temporal_layers = 3;
  streams_.push_back(stream);

  InitializeCodec();
  EXPECT_LT(codec_out_.spatialLayers[0].maxBitrate,
            kDefaultMaxBitrateBps / 1000);
}

TEST_F(VideoCodecInitializerTest,
       Vp9KeepBitrateLimitsIfNumberOfSpatialLayersIsReducedToOneWithL3T1) {
  // Request 3 spatial layers for 320x180 input. Actual number of layers will be
  // reduced to 1 due to low input resolution but SVC bitrate limits should be
  // applied.
  SetUpFor(VideoCodecType::kVideoCodecVP9, 1, 3, 1, false);
  VideoStream stream = DefaultStream();
  stream.width = 320;
  stream.height = 180;
  stream.num_temporal_layers = 1;
  stream.scalability_mode = ScalabilityMode::kL3T1;
  streams_.push_back(stream);

  InitializeCodec();
  EXPECT_EQ(1u, codec_out_.VP9()->numberOfSpatialLayers);
  EXPECT_LT(codec_out_.spatialLayers[0].minBitrate,
            kDefaultMinBitrateBps / 1000);
  EXPECT_LT(codec_out_.spatialLayers[0].maxBitrate,
            kDefaultMaxBitrateBps / 1000);
}

TEST_F(VideoCodecInitializerTest, Vp9DeactivateLayers) {
  SetUpFor(VideoCodecType::kVideoCodecVP9, absl::nullopt, 3, 1, false);
  VideoStream stream = DefaultStream();
  streams_.push_back(stream);

  config_.simulcast_layers.resize(3);

  // Activate all layers.
  config_.simulcast_layers[0].active = true;
  config_.simulcast_layers[1].active = true;
  config_.simulcast_layers[2].active = true;
  InitializeCodec();
  EXPECT_EQ(codec_out_.VP9()->numberOfSpatialLayers, 3);
  EXPECT_TRUE(codec_out_.spatialLayers[0].active);
  EXPECT_TRUE(codec_out_.spatialLayers[1].active);
  EXPECT_TRUE(codec_out_.spatialLayers[2].active);

  // Deactivate top layer.
  config_.simulcast_layers[0].active = true;
  config_.simulcast_layers[1].active = true;
  config_.simulcast_layers[2].active = false;
  InitializeCodec();
  EXPECT_EQ(codec_out_.VP9()->numberOfSpatialLayers, 3);
  EXPECT_TRUE(codec_out_.spatialLayers[0].active);
  EXPECT_TRUE(codec_out_.spatialLayers[1].active);
  EXPECT_FALSE(codec_out_.spatialLayers[2].active);

  // Deactivate middle layer.
  config_.simulcast_layers[0].active = true;
  config_.simulcast_layers[1].active = false;
  config_.simulcast_layers[2].active = true;
  InitializeCodec();
  EXPECT_EQ(codec_out_.VP9()->numberOfSpatialLayers, 3);
  EXPECT_TRUE(codec_out_.spatialLayers[0].active);
  EXPECT_FALSE(codec_out_.spatialLayers[1].active);
  EXPECT_TRUE(codec_out_.spatialLayers[2].active);

  // Deactivate first layer.
  config_.simulcast_layers[0].active = false;
  config_.simulcast_layers[1].active = true;
  config_.simulcast_layers[2].active = true;
  InitializeCodec();
  EXPECT_EQ(codec_out_.VP9()->numberOfSpatialLayers, 2);
  EXPECT_TRUE(codec_out_.spatialLayers[0].active);
  EXPECT_TRUE(codec_out_.spatialLayers[1].active);

  // HD singlecast.
  config_.simulcast_layers[0].active = false;
  config_.simulcast_layers[1].active = false;
  config_.simulcast_layers[2].active = true;
  InitializeCodec();
  EXPECT_EQ(codec_out_.VP9()->numberOfSpatialLayers, 1);
  EXPECT_TRUE(codec_out_.spatialLayers[0].active);

  // VGA singlecast.
  config_.simulcast_layers[0].active = false;
  config_.simulcast_layers[1].active = true;
  config_.simulcast_layers[2].active = false;
  InitializeCodec();
  EXPECT_EQ(codec_out_.VP9()->numberOfSpatialLayers, 2);
  EXPECT_TRUE(codec_out_.spatialLayers[0].active);
  EXPECT_FALSE(codec_out_.spatialLayers[1].active);

  // QVGA singlecast.
  config_.simulcast_layers[0].active = true;
  config_.simulcast_layers[1].active = false;
  config_.simulcast_layers[2].active = false;
  InitializeCodec();
  EXPECT_EQ(codec_out_.VP9()->numberOfSpatialLayers, 3);
  EXPECT_TRUE(codec_out_.spatialLayers[0].active);
  EXPECT_FALSE(codec_out_.spatialLayers[1].active);
  EXPECT_FALSE(codec_out_.spatialLayers[2].active);
}

TEST_F(VideoCodecInitializerTest, Vp9SvcResolutionAlignment) {
  SetUpFor(VideoCodecType::kVideoCodecVP9, absl::nullopt, 3, 3, false);
  VideoStream stream = DefaultStream();
  stream.width = 1281;
  stream.height = 721;
  stream.num_temporal_layers = 3;
  streams_.push_back(stream);

  InitializeCodec();
  EXPECT_EQ(codec_out_.width, 1280);
  EXPECT_EQ(codec_out_.height, 720);
  EXPECT_EQ(codec_out_.numberOfSimulcastStreams, 1);
  EXPECT_EQ(codec_out_.simulcastStream[0].width, 1280);
  EXPECT_EQ(codec_out_.simulcastStream[0].height, 720);
}

TEST_F(VideoCodecInitializerTest, Vp9SimulcastResolutions) {
  // 3 x L1T3
  SetUpFor(VideoCodecType::kVideoCodecVP9, 3, 1, 3, false);
  // Scalability mode has to be set on all layers to avoid legacy SVC paths.
  streams_ = {DefaultStream(320, 180, ScalabilityMode::kL1T3),
              DefaultStream(640, 360, ScalabilityMode::kL1T3),
              DefaultStream(1280, 720, ScalabilityMode::kL1T3)};

  InitializeCodec();
  // This is expected to be the largest layer.
  EXPECT_EQ(codec_out_.width, 1280);
  EXPECT_EQ(codec_out_.height, 720);
  // `simulcastStream` is expected to be the same as the input (same order).
  EXPECT_EQ(codec_out_.numberOfSimulcastStreams, 3);
  EXPECT_EQ(codec_out_.simulcastStream[0].width, 320);
  EXPECT_EQ(codec_out_.simulcastStream[0].height, 180);
  EXPECT_EQ(codec_out_.simulcastStream[1].width, 640);
  EXPECT_EQ(codec_out_.simulcastStream[1].height, 360);
  EXPECT_EQ(codec_out_.simulcastStream[2].width, 1280);
  EXPECT_EQ(codec_out_.simulcastStream[2].height, 720);
}

TEST_F(VideoCodecInitializerTest, Av1SingleSpatialLayerBitratesAreConsistent) {
  VideoEncoderConfig config;
  config.codec_type = VideoCodecType::kVideoCodecAV1;
  std::vector<VideoStream> streams = {DefaultStream()};
  streams[0].scalability_mode = ScalabilityMode::kL1T2;

  VideoCodec codec =
      VideoCodecInitializer::SetupCodec(field_trials_, config, streams);

  EXPECT_GE(codec.spatialLayers[0].targetBitrate,
            codec.spatialLayers[0].minBitrate);
  EXPECT_LE(codec.spatialLayers[0].targetBitrate,
            codec.spatialLayers[0].maxBitrate);
}

TEST_F(VideoCodecInitializerTest, Av1TwoSpatialLayersBitratesAreConsistent) {
  VideoEncoderConfig config;
  config.codec_type = VideoCodecType::kVideoCodecAV1;
  std::vector<VideoStream> streams = {DefaultStream()};
  streams[0].scalability_mode = ScalabilityMode::kL2T2;

  VideoCodec codec =
      VideoCodecInitializer::SetupCodec(field_trials_, config, streams);

  EXPECT_GE(codec.spatialLayers[0].targetBitrate,
            codec.spatialLayers[0].minBitrate);
  EXPECT_LE(codec.spatialLayers[0].targetBitrate,
            codec.spatialLayers[0].maxBitrate);

  EXPECT_GE(codec.spatialLayers[1].targetBitrate,
            codec.spatialLayers[1].minBitrate);
  EXPECT_LE(codec.spatialLayers[1].targetBitrate,
            codec.spatialLayers[1].maxBitrate);
}

TEST_F(VideoCodecInitializerTest, Av1TwoSpatialLayersActiveByDefault) {
  VideoEncoderConfig config;
  config.codec_type = VideoCodecType::kVideoCodecAV1;
  std::vector<VideoStream> streams = {DefaultStream()};
  streams[0].scalability_mode = ScalabilityMode::kL2T2;
  config.spatial_layers = {};

  VideoCodec codec =
      VideoCodecInitializer::SetupCodec(field_trials_, config, streams);

  EXPECT_TRUE(codec.spatialLayers[0].active);
  EXPECT_TRUE(codec.spatialLayers[1].active);
}

TEST_F(VideoCodecInitializerTest, Av1TwoSpatialLayersOneDeactivated) {
  VideoEncoderConfig config;
  config.codec_type = VideoCodecType::kVideoCodecAV1;
  std::vector<VideoStream> streams = {DefaultStream()};
  streams[0].scalability_mode = ScalabilityMode::kL2T2;
  config.spatial_layers.resize(2);
  config.spatial_layers[0].active = true;
  config.spatial_layers[1].active = false;

  VideoCodec codec =
      VideoCodecInitializer::SetupCodec(field_trials_, config, streams);

  EXPECT_TRUE(codec.spatialLayers[0].active);
  EXPECT_FALSE(codec.spatialLayers[1].active);
}

TEST_F(VideoCodecInitializerTest, Vp9SingleSpatialLayerBitratesAreConsistent) {
  VideoEncoderConfig config;
  config.simulcast_layers.resize(3);
  config.simulcast_layers[0].active = true;
  config.simulcast_layers[1].active = false;
  config.simulcast_layers[2].active = false;

  config.codec_type = VideoCodecType::kVideoCodecVP9;
  std::vector<VideoStream> streams = {DefaultStream()};
  streams[0].scalability_mode = ScalabilityMode::kL1T2;

  VideoCodec codec =
      VideoCodecInitializer::SetupCodec(field_trials_, config, streams);

  EXPECT_EQ(1u, codec.VP9()->numberOfSpatialLayers);
  // Target is consistent with min and max (min <= target <= max).
  EXPECT_GE(codec.spatialLayers[0].targetBitrate,
            codec.spatialLayers[0].minBitrate);
  EXPECT_LE(codec.spatialLayers[0].targetBitrate,
            codec.spatialLayers[0].maxBitrate);
  // In the single spatial layer case, the spatial layer bitrates are copied
  // from the codec's bitrate which is the sum if VideoStream bitrates. In this
  // case we only have a single VideoStream using default values.
  EXPECT_EQ(codec.spatialLayers[0].minBitrate, kDefaultMinBitrateBps / 1000);
  EXPECT_EQ(codec.spatialLayers[0].targetBitrate, kDefaultMaxBitrateBps / 1000);
  EXPECT_EQ(codec.spatialLayers[0].maxBitrate, kDefaultMaxBitrateBps / 1000);
}

TEST_F(VideoCodecInitializerTest, Vp9TwoSpatialLayersBitratesAreConsistent) {
  VideoEncoderConfig config;
  config.simulcast_layers.resize(3);
  config.simulcast_layers[0].active = true;
  config.simulcast_layers[1].active = false;
  config.simulcast_layers[2].active = false;

  config.codec_type = VideoCodecType::kVideoCodecVP9;
  std::vector<VideoStream> streams = {DefaultStream()};
  streams[0].scalability_mode = ScalabilityMode::kL2T2;

  VideoCodec codec =
      VideoCodecInitializer::SetupCodec(field_trials_, config, streams);

  EXPECT_EQ(2u, codec.VP9()->numberOfSpatialLayers);
  EXPECT_GE(codec.spatialLayers[0].targetBitrate,
            codec.spatialLayers[0].minBitrate);
  EXPECT_LE(codec.spatialLayers[0].targetBitrate,
            codec.spatialLayers[0].maxBitrate);
  EXPECT_LT(codec.spatialLayers[0].minBitrate, kDefaultMinBitrateBps / 1000);

  EXPECT_GE(codec.spatialLayers[1].targetBitrate,
            codec.spatialLayers[1].minBitrate);
  EXPECT_LE(codec.spatialLayers[1].targetBitrate,
            codec.spatialLayers[1].maxBitrate);
  EXPECT_GT(codec.spatialLayers[1].minBitrate,
            codec.spatialLayers[0].maxBitrate);
}

TEST_F(VideoCodecInitializerTest, UpdatesVp9SpecificFieldsWithScalabilityMode) {
  VideoEncoderConfig config;
  config.codec_type = VideoCodecType::kVideoCodecVP9;
  std::vector<VideoStream> streams = {DefaultStream()};
  streams[0].scalability_mode = ScalabilityMode::kL2T3_KEY;

  VideoCodec codec =
      VideoCodecInitializer::SetupCodec(field_trials_, config, streams);

  EXPECT_EQ(codec.VP9()->numberOfSpatialLayers, 2u);
  EXPECT_EQ(codec.VP9()->numberOfTemporalLayers, 3u);
  EXPECT_EQ(codec.VP9()->interLayerPred, InterLayerPredMode::kOnKeyPic);

  streams[0].scalability_mode = ScalabilityMode::kS3T1;
  codec = VideoCodecInitializer::SetupCodec(field_trials_, config, streams);

  EXPECT_EQ(codec.VP9()->numberOfSpatialLayers, 3u);
  EXPECT_EQ(codec.VP9()->numberOfTemporalLayers, 1u);
  EXPECT_EQ(codec.VP9()->interLayerPred, InterLayerPredMode::kOff);
}

}  // namespace webrtc
