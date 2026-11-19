/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/svc/simulcast_to_svc_converter.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "api/video/encoded_image.h"
#include "api/video/video_bitrate_allocation.h"
#include "api/video/video_codec_type.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/video_codec.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/svc/create_scalability_structure.h"
#include "modules/video_coding/svc/scalable_video_controller.h"
#include "test/gtest.h"

namespace webrtc {

TEST(SimulcastToSvc, ConvertsConfig) {
  VideoCodec codec;
  codec.codecType = kVideoCodecVP9;
  codec.SetScalabilityMode(ScalabilityMode::kL1T3);
  codec.width = 1280;
  codec.height = 720;
  codec.minBitrate = 10;
  codec.maxBitrate = 2500;
  codec.numberOfSimulcastStreams = 3;
  codec.VP9()->numberOfSpatialLayers = 1;
  codec.VP9()->interLayerPred = InterLayerPredMode::kOff;
  codec.simulcastStream[0] = {.width = 320,
                              .height = 180,
                              .maxFramerate = 30,
                              .numberOfTemporalLayers = 3,
                              .maxBitrate = 100,
                              .targetBitrate = 70,
                              .minBitrate = 50,
                              .qpMax = 150,
                              .active = true};
  codec.simulcastStream[1] = {.width = 640,
                              .height = 360,
                              .maxFramerate = 30,
                              .numberOfTemporalLayers = 3,
                              .maxBitrate = 250,
                              .targetBitrate = 150,
                              .minBitrate = 100,
                              .qpMax = 150,
                              .active = true};
  codec.simulcastStream[2] = {.width = 12800,
                              .height = 720,
                              .maxFramerate = 30,
                              .numberOfTemporalLayers = 3,
                              .maxBitrate = 1500,
                              .targetBitrate = 1200,
                              .minBitrate = 800,
                              .qpMax = 150,
                              .active = true};
  VideoCodec result = codec;

  SimulcastToSvcConverter converter(codec);
  result = converter.GetConfig();

  EXPECT_EQ(result.numberOfSimulcastStreams, 1);
  EXPECT_EQ(result.spatialLayers[0], codec.simulcastStream[0]);
  EXPECT_EQ(result.spatialLayers[1], codec.simulcastStream[1]);
  EXPECT_EQ(result.spatialLayers[2], codec.simulcastStream[2]);
  EXPECT_EQ(result.VP9()->numberOfTemporalLayers, 3);
  EXPECT_EQ(result.VP9()->numberOfSpatialLayers, 3);
  EXPECT_EQ(result.VP9()->interLayerPred, InterLayerPredMode::kOff);
}

TEST(SimulcastToSvc, ConvertsEncodedImage) {
  VideoCodec codec;
  codec.codecType = kVideoCodecVP9;
  codec.SetScalabilityMode(ScalabilityMode::kL1T3);
  codec.width = 1280;
  codec.height = 720;
  codec.minBitrate = 10;
  codec.maxBitrate = 2500;
  codec.numberOfSimulcastStreams = 3;
  codec.VP9()->numberOfSpatialLayers = 1;
  codec.VP9()->interLayerPred = InterLayerPredMode::kOff;
  codec.simulcastStream[0] = {.width = 320,
                              .height = 180,
                              .maxFramerate = 30,
                              .numberOfTemporalLayers = 3,
                              .maxBitrate = 100,
                              .targetBitrate = 70,
                              .minBitrate = 50,
                              .qpMax = 150,
                              .active = true};
  codec.simulcastStream[1] = {.width = 640,
                              .height = 360,
                              .maxFramerate = 30,
                              .numberOfTemporalLayers = 3,
                              .maxBitrate = 250,
                              .targetBitrate = 150,
                              .minBitrate = 100,
                              .qpMax = 150,
                              .active = true};
  codec.simulcastStream[2] = {.width = 1280,
                              .height = 720,
                              .maxFramerate = 30,
                              .numberOfTemporalLayers = 3,
                              .maxBitrate = 1500,
                              .targetBitrate = 1200,
                              .minBitrate = 800,
                              .qpMax = 150,
                              .active = true};

  SimulcastToSvcConverter converter(codec);

  EncodedImage image;
  image.SetRtpTimestamp(123);
  image.SetSpatialIndex(1);
  image.SetTemporalIndex(0);
  image._encodedWidth = 640;
  image._encodedHeight = 360;

  CodecSpecificInfo codec_specific;
  codec_specific.codecType = kVideoCodecVP9;
  codec_specific.end_of_picture = false;
  codec_specific.codecSpecific.VP9.num_spatial_layers = 3;
  codec_specific.codecSpecific.VP9.first_active_layer = 0;
  codec_specific.scalability_mode = ScalabilityMode::kS3T3;

  converter.EncodeStarted(/*force_keyframe =*/true);
  converter.ConvertFrame(image, codec_specific);

  EXPECT_EQ(image.SpatialIndex(), std::nullopt);
  EXPECT_EQ(image.SimulcastIndex(), 1);
  EXPECT_EQ(image.TemporalIndex(), 0);

  EXPECT_EQ(codec_specific.end_of_picture, true);
  EXPECT_EQ(codec_specific.scalability_mode, ScalabilityMode::kL1T3);
}

// Checks that ScalableVideoController, which actualle is used  by the encoder
// in the forced S-mode behaves as SimulcastToSvcConverter assumes.
TEST(SimulcastToSvc, PredictsInternalStateCorrectlyOnFrameDrops) {
  VideoCodec codec;
  codec.codecType = kVideoCodecVP9;
  codec.SetScalabilityMode(ScalabilityMode::kL1T3);
  codec.width = 1280;
  codec.height = 720;
  codec.minBitrate = 10;
  codec.maxBitrate = 2500;
  codec.numberOfSimulcastStreams = 3;
  codec.VP9()->numberOfSpatialLayers = 1;
  codec.VP9()->interLayerPred = InterLayerPredMode::kOff;

  codec.simulcastStream[0] = {.width = 320,
                              .height = 180,
                              .maxFramerate = 30,
                              .numberOfTemporalLayers = 3,
                              .maxBitrate = 100,
                              .targetBitrate = 70,
                              .minBitrate = 50,
                              .qpMax = 150,
                              .active = true};
  codec.simulcastStream[1] = {.width = 640,
                              .height = 360,
                              .maxFramerate = 30,
                              .numberOfTemporalLayers = 3,
                              .maxBitrate = 250,
                              .targetBitrate = 150,
                              .minBitrate = 100,
                              .qpMax = 150,
                              .active = true};
  codec.simulcastStream[2] = {.width = 1280,
                              .height = 720,
                              .maxFramerate = 30,
                              .numberOfTemporalLayers = 3,
                              .maxBitrate = 1500,
                              .targetBitrate = 1200,
                              .minBitrate = 800,
                              .qpMax = 150,
                              .active = true};

  std::unique_ptr<ScalableVideoController> svc_controller =
      CreateScalabilityStructure(ScalabilityMode::kS3T3);

  VideoBitrateAllocation dummy_bitrates;
  for (int sid = 0; sid < 3; ++sid) {
    for (int tid = 0; tid < 3; ++tid) {
      dummy_bitrates.SetBitrate(sid, tid, 10000);
    }
  }
  svc_controller->OnRatesUpdated(dummy_bitrates);

  SimulcastToSvcConverter converter(codec);

  EncodedImage image;

  // Simulate complex dropping pattern.
  const int kDropInterval[3] = {11, 7, 5};
  const int kKeyFrameInterval = 13;
  for (int i = 0; i < 100; ++i) {
    bool force_restart = ((i + 1) % kKeyFrameInterval == 0) || (i == 0);
    std::vector<ScalableVideoController::LayerFrameConfig> layer_config =
        svc_controller->NextFrameConfig(force_restart);
    converter.EncodeStarted(force_restart);
    for (const ScalableVideoController::LayerFrameConfig& config :
         layer_config) {
      int sid = config.SpatialId();
      if ((i + 1) % kDropInterval[sid] == 0) {
        continue;
      }
      image.SetRtpTimestamp(123 * i);
      image.SetSpatialIndex(sid);
      image.SetTemporalIndex(0);
      image._encodedWidth = 1280 / (1 << sid);
      image._encodedHeight = 720 / (1 << sid);
      image.SetSpatialIndex(sid);
      image.SetTemporalIndex(config.TemporalId());

      CodecSpecificInfo codec_specific;
      codec_specific.codecType = kVideoCodecVP9;
      codec_specific.end_of_picture = false;
      codec_specific.codecSpecific.VP9.num_spatial_layers = 3;
      codec_specific.codecSpecific.VP9.first_active_layer = 0;
      codec_specific.codecSpecific.VP9.temporal_idx = config.TemporalId();
      codec_specific.generic_frame_info = svc_controller->OnEncodeDone(config);

      codec_specific.scalability_mode = ScalabilityMode::kS3T3;

      EXPECT_TRUE(converter.ConvertFrame(image, codec_specific));

      EXPECT_EQ(image.SpatialIndex(), std::nullopt);
      EXPECT_EQ(image.SimulcastIndex(), sid);
      EXPECT_EQ(image.TemporalIndex(), config.TemporalId());

      EXPECT_EQ(codec_specific.scalability_mode, ScalabilityMode::kL1T3);
    }
  }
}

TEST(SimulcastToSvc, SupportsOnlyContinuousActiveStreams) {
  VideoCodec codec;
  codec.codecType = kVideoCodecVP9;
  codec.SetScalabilityMode(ScalabilityMode::kL1T3);
  codec.width = 1280;
  codec.height = 720;
  codec.minBitrate = 10;
  codec.maxBitrate = 2500;
  codec.numberOfSimulcastStreams = 3;
  codec.VP9()->numberOfSpatialLayers = 1;
  codec.VP9()->interLayerPred = InterLayerPredMode::kOff;

  codec.simulcastStream[0] = {.width = 320,
                              .height = 180,
                              .maxFramerate = 30,
                              .numberOfTemporalLayers = 3,
                              .maxBitrate = 100,
                              .targetBitrate = 70,
                              .minBitrate = 50,
                              .qpMax = 150,
                              .active = true};
  codec.simulcastStream[1] = {.width = 640,
                              .height = 360,
                              .maxFramerate = 30,
                              .numberOfTemporalLayers = 3,
                              .maxBitrate = 250,
                              .targetBitrate = 150,
                              .minBitrate = 100,
                              .qpMax = 150,
                              .active = true};
  codec.simulcastStream[2] = {.width = 1280,
                              .height = 720,
                              .maxFramerate = 30,
                              .numberOfTemporalLayers = 3,
                              .maxBitrate = 1500,
                              .targetBitrate = 1200,
                              .minBitrate = 800,
                              .qpMax = 150,
                              .active = true};
  EXPECT_TRUE(SimulcastToSvcConverter::IsConfigSupported(codec));

  codec.simulcastStream[0].active = false;
  codec.simulcastStream[1].active = true;
  codec.simulcastStream[2].active = true;
  EXPECT_TRUE(SimulcastToSvcConverter::IsConfigSupported(codec));

  codec.simulcastStream[0].active = true;
  codec.simulcastStream[1].active = true;
  codec.simulcastStream[2].active = false;
  EXPECT_TRUE(SimulcastToSvcConverter::IsConfigSupported(codec));

  codec.simulcastStream[0].active = true;
  codec.simulcastStream[1].active = false;
  codec.simulcastStream[2].active = true;
  EXPECT_FALSE(SimulcastToSvcConverter::IsConfigSupported(codec));
}

TEST(SimulcastToSvc, SupportsOnlySameTemporalStructure) {
  VideoCodec codec;
  codec.codecType = kVideoCodecVP9;
  codec.width = 1280;
  codec.height = 720;
  codec.minBitrate = 10;
  codec.maxBitrate = 2500;
  codec.numberOfSimulcastStreams = 3;
  codec.VP9()->numberOfSpatialLayers = 1;
  codec.VP9()->interLayerPred = InterLayerPredMode::kOff;

  codec.simulcastStream[0] = {.width = 320,
                              .height = 180,
                              .maxFramerate = 30,
                              .numberOfTemporalLayers = 3,
                              .maxBitrate = 100,
                              .targetBitrate = 70,
                              .minBitrate = 50,
                              .qpMax = 150,
                              .active = true};
  codec.simulcastStream[1] = {.width = 640,
                              .height = 360,
                              .maxFramerate = 30,
                              .numberOfTemporalLayers = 3,
                              .maxBitrate = 250,
                              .targetBitrate = 150,
                              .minBitrate = 100,
                              .qpMax = 150,
                              .active = true};
  codec.simulcastStream[2] = {.width = 1280,
                              .height = 720,
                              .maxFramerate = 30,
                              .numberOfTemporalLayers = 3,
                              .maxBitrate = 1500,
                              .targetBitrate = 1200,
                              .minBitrate = 800,
                              .qpMax = 150,
                              .active = true};
  EXPECT_TRUE(SimulcastToSvcConverter::IsConfigSupported(codec));

  codec.simulcastStream[0].numberOfTemporalLayers = 1;
  EXPECT_FALSE(SimulcastToSvcConverter::IsConfigSupported(codec));
}

TEST(SimulcastToSvc, SupportsOnly421Scaling) {
  VideoCodec codec;
  codec.codecType = kVideoCodecVP9;
  codec.width = 1280;
  codec.height = 720;
  codec.minBitrate = 10;
  codec.maxBitrate = 2500;
  codec.numberOfSimulcastStreams = 3;
  codec.VP9()->numberOfSpatialLayers = 1;
  codec.VP9()->interLayerPred = InterLayerPredMode::kOff;

  codec.simulcastStream[0] = {.width = 320,
                              .height = 180,
                              .maxFramerate = 30,
                              .numberOfTemporalLayers = 3,
                              .maxBitrate = 100,
                              .targetBitrate = 70,
                              .minBitrate = 50,
                              .qpMax = 150,
                              .active = true};
  codec.simulcastStream[1] = {.width = 640,
                              .height = 360,
                              .maxFramerate = 30,
                              .numberOfTemporalLayers = 3,
                              .maxBitrate = 250,
                              .targetBitrate = 150,
                              .minBitrate = 100,
                              .qpMax = 150,
                              .active = true};
  codec.simulcastStream[2] = {.width = 1280,
                              .height = 720,
                              .maxFramerate = 30,
                              .numberOfTemporalLayers = 3,
                              .maxBitrate = 1500,
                              .targetBitrate = 1200,
                              .minBitrate = 800,
                              .qpMax = 150,
                              .active = true};
  EXPECT_TRUE(SimulcastToSvcConverter::IsConfigSupported(codec));

  codec.simulcastStream[0].width = 160;
  codec.simulcastStream[0].height = 90;
  EXPECT_FALSE(SimulcastToSvcConverter::IsConfigSupported(codec));
}

}  // namespace webrtc
