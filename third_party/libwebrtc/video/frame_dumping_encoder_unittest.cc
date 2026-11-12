/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/frame_dumping_encoder.h"

#include <memory>
#include <utility>

#include "api/environment/environment.h"
#include "api/test/mock_video_encoder.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

using ::testing::ElementsAre;

TEST(FrameDumpingEncoderTest, ForwardsSetComplexity) {
  Environment env = CreateTestEnvironment(
      {.field_trials = "WebRTC-EncoderDataDumpDirectory/tmp/"});
  auto mock_encoder = std::make_unique<MockVideoEncoder>();
  MockVideoEncoder* mock_encoder_ptr = mock_encoder.get();
  std::unique_ptr<VideoEncoder> wrapper =
      MaybeCreateFrameDumpingEncoderWrapper(env, std::move(mock_encoder));

  EXPECT_CALL(*mock_encoder_ptr,
              SetComplexity(ElementsAre(VideoCodecComplexity::kComplexityLow,
                                        VideoCodecComplexity::kComplexityHigh),
                            VideoEncoder::ComplexityControlMode::kVariable));
  const VideoCodecComplexity kComplexities[] = {
      VideoCodecComplexity::kComplexityLow,
      VideoCodecComplexity::kComplexityHigh};
  wrapper->SetComplexity(kComplexities,
                         VideoEncoder::ComplexityControlMode::kVariable);
}

}  // namespace
}  // namespace webrtc
