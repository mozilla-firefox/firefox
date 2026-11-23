/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef VIDEO_TEST_MOCK_VIDEO_STREAM_ENCODER_H_
#define VIDEO_TEST_MOCK_VIDEO_STREAM_ENCODER_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "api/adaptation/resource.h"
#include "api/fec_controller_override.h"
#include "api/rtp_parameters.h"
#include "api/rtp_sender_interface.h"
#include "api/scoped_refptr.h"
#include "api/units/data_rate.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_type.h"
#include "api/video/video_source_interface.h"
#include "test/gmock.h"
#include "video/config/video_encoder_config.h"
#include "video/video_stream_encoder_interface.h"

namespace webrtc {

class MockVideoStreamEncoder : public VideoStreamEncoderInterface {
 public:
  MOCK_METHOD(void,
              AddAdaptationResource,
              (scoped_refptr<Resource>),
              (override));
  MOCK_METHOD(void,
              SetSource,
              (VideoSourceInterface<VideoFrame>*, const DegradationPreference&),
              (override));
  MOCK_METHOD(void, SetSink, (EncoderSink*, bool), (override));
  MOCK_METHOD(void, SetStartBitrate, (int), (override));
  MOCK_METHOD(void,
              SendKeyFrame,
              (const std::vector<VideoFrameType>&),
              (override));
  MOCK_METHOD(void,
              OnBitrateUpdated,
              (DataRate, DataRate, uint8_t, int64_t, double),
              (override));
  MOCK_METHOD(void,
              SetFecControllerOverride,
              (FecControllerOverride*),
              (override));
  MOCK_METHOD(void, Stop, (), (override));

  MOCK_METHOD(void,
              MockedConfigureEncoder,
              (const VideoEncoderConfig&, size_t));
  MOCK_METHOD(void,
              MockedConfigureEncoder,
              (const VideoEncoderConfig&, size_t, SetParametersCallback));
  // gtest generates implicit copy which is not allowed on VideoEncoderConfig,
  // so we can't mock ConfigureEncoder directly.
  void ConfigureEncoder(VideoEncoderConfig config,
                        size_t max_data_payload_length) override {
    MockedConfigureEncoder(config, max_data_payload_length);
  }
  void ConfigureEncoder(VideoEncoderConfig config,
                        size_t max_data_payload_length,
                        SetParametersCallback) override {
    MockedConfigureEncoder(config, max_data_payload_length);
  }
};

}  // namespace webrtc

#endif  // VIDEO_TEST_MOCK_VIDEO_STREAM_ENCODER_H_
