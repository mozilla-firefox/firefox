/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/fake_vp8_encoder.h"

#include <memory>
#include <utility>

#include "api/test/create_simulcast_test_fixture.h"
#include "api/test/simulcast_test_fixture.h"
#include "api/test/video/function_video_decoder_factory.h"
#include "api/test/video/function_video_encoder_factory.h"
#include "modules/video_coding/utility/simulcast_test_fixture_impl.h"
#include "test/fake_vp8_decoder.h"

namespace webrtc {
namespace test {

namespace {

std::unique_ptr<SimulcastTestFixture> CreateSpecificSimulcastTestFixture() {
  std::unique_ptr<VideoEncoderFactory> encoder_factory =
      std::make_unique<FunctionVideoEncoderFactory>(
          [](const Environment& env, const SdpVideoFormat& format) {
            return std::make_unique<FakeVp8Encoder>(env);
          });
  std::unique_ptr<VideoDecoderFactory> decoder_factory =
      std::make_unique<FunctionVideoDecoderFactory>(
          []() { return std::make_unique<FakeVp8Decoder>(); });
  return CreateSimulcastTestFixture(std::move(encoder_factory),
                                    std::move(decoder_factory),
                                    SdpVideoFormat::VP8());
}
}  // namespace

TEST(TestFakeVp8Codec, TestKeyFrameRequestsOnAllStreams) {
  auto fixture = CreateSpecificSimulcastTestFixture();
  fixture->TestKeyFrameRequestsOnAllStreams();
}

TEST(TestFakeVp8Codec, TestPaddingAllStreams) {
  auto fixture = CreateSpecificSimulcastTestFixture();
  fixture->TestPaddingAllStreams();
}

TEST(TestFakeVp8Codec, TestPaddingTwoStreams) {
  auto fixture = CreateSpecificSimulcastTestFixture();
  fixture->TestPaddingTwoStreams();
}

TEST(TestFakeVp8Codec, TestPaddingTwoStreamsOneMaxedOut) {
  auto fixture = CreateSpecificSimulcastTestFixture();
  fixture->TestPaddingTwoStreamsOneMaxedOut();
}

TEST(TestFakeVp8Codec, TestPaddingOneStream) {
  auto fixture = CreateSpecificSimulcastTestFixture();
  fixture->TestPaddingOneStream();
}

TEST(TestFakeVp8Codec, TestPaddingOneStreamTwoMaxedOut) {
  auto fixture = CreateSpecificSimulcastTestFixture();
  fixture->TestPaddingOneStreamTwoMaxedOut();
}

TEST(TestFakeVp8Codec, TestSendAllStreams) {
  auto fixture = CreateSpecificSimulcastTestFixture();
  fixture->TestSendAllStreams();
}

TEST(TestFakeVp8Codec, TestDisablingStreams) {
  auto fixture = CreateSpecificSimulcastTestFixture();
  fixture->TestDisablingStreams();
}

TEST(TestFakeVp8Codec, TestSwitchingToOneStream) {
  auto fixture = CreateSpecificSimulcastTestFixture();
  fixture->TestSwitchingToOneStream();
}

TEST(TestFakeVp8Codec, TestSwitchingToOneOddStream) {
  auto fixture = CreateSpecificSimulcastTestFixture();
  fixture->TestSwitchingToOneOddStream();
}

TEST(TestFakeVp8Codec, TestSwitchingToOneSmallStream) {
  auto fixture = CreateSpecificSimulcastTestFixture();
  fixture->TestSwitchingToOneSmallStream();
}

TEST(TestFakeVp8Codec, TestSpatioTemporalLayers333PatternEncoder) {
  auto fixture = CreateSpecificSimulcastTestFixture();
  fixture->TestSpatioTemporalLayers333PatternEncoder();
}

TEST(TestFakeVp8Codec, TestDecodeWidthHeightSet) {
  auto fixture = CreateSpecificSimulcastTestFixture();
  fixture->TestDecodeWidthHeightSet();
}

TEST(TestFakeVp8Codec,
     TestEncoderInfoForDefaultTemporalLayerProfileHasFpsAllocation) {
  auto fixture = CreateSpecificSimulcastTestFixture();
  fixture->TestEncoderInfoForDefaultTemporalLayerProfileHasFpsAllocation();
}

}  // namespace test
}  // namespace webrtc
