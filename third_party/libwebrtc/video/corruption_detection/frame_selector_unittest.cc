/*
 * Copyright 2025 The WebRTC project authors. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file in the root of the source
 * tree. An additional intellectual property rights grant can be found
 * in the file PATENTS.  All contributing project authors may
 * be found in the AUTHORS file in the root of the source tree.
 */

#include "video/corruption_detection/frame_selector.h"

#include <cstdint>
#include <optional>

#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_frame_type.h"
#include "api/video/video_rotation.h"
#include "api/video_codecs/scalability_mode.h"
#include "system_wrappers/include/clock.h"
#include "test/create_test_environment.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

using ::testing::Test;

constexpr TimeDelta kLowOverheadLowerBound = TimeDelta::Seconds(1);
constexpr TimeDelta kLowOverheadUpperBound = TimeDelta::Seconds(2);
constexpr TimeDelta kHighOverheadLowerBound = TimeDelta::Seconds(3);
constexpr TimeDelta kHighOverheadUpperBound = TimeDelta::Seconds(4);

constexpr FrameSelector::Timespan kLowOverheadSpan = {
    .lower_bound = kLowOverheadLowerBound,
    .upper_bound = kLowOverheadUpperBound};
constexpr FrameSelector::Timespan kHighOverheadSpan = {
    .lower_bound = kHighOverheadLowerBound,
    .upper_bound = kHighOverheadUpperBound};

class FakeNativeBuffer : public VideoFrameBuffer {
 public:
  Type type() const override { return Type::kNative; }
  int width() const override { return 10; }
  int height() const override { return 10; }
  scoped_refptr<I420BufferInterface> ToI420() override {
    return I420Buffer::Create(10, 10);
  }
};

VideoFrame CreateLowOverheadFrame(Timestamp timestamp) {
  return VideoFrame::Builder()
      .set_video_frame_buffer(I420Buffer::Create(10, 10))
      .set_rotation(kVideoRotation_0)
      .set_timestamp_ms(timestamp.ms())
      .build();
}

VideoFrame CreateHighOverheadFrame(Timestamp timestamp) {
  return VideoFrame::Builder()
      .set_video_frame_buffer(make_ref_counted<FakeNativeBuffer>())
      .set_rotation(kVideoRotation_0)
      .set_timestamp_ms(timestamp.ms())
      .build();
}

EncodedImage CreateEncodedImage(VideoFrameType frame_type,
                                Timestamp capture_time,
                                int spatial_index = 0,
                                int simulcast_index = 0,
                                uint32_t rtp_timestamp = 0) {
  EncodedImage encoded_image;
  encoded_image.set_frame_type(frame_type);
  encoded_image.SetSpatialIndex(spatial_index);
  encoded_image.SetSimulcastIndex(simulcast_index);
  encoded_image.capture_time_ms_ = capture_time.ms();
  encoded_image.SetRtpTimestamp(rtp_timestamp == 0 ? capture_time.ms() * 90
                                                   : rtp_timestamp);
  return encoded_image;
}

TEST(FrameSelectorTest, AlwaysSelectsKeyFrames) {
  FrameSelector selector(CreateTestEnvironment(), ScalabilityMode::kL1T1,
                         kLowOverheadSpan, kHighOverheadSpan);
  // Even before any threshold, keyframes should be selected.
  EXPECT_TRUE(selector.ShouldInstrumentFrame(
      CreateLowOverheadFrame(Timestamp::Millis(100)),
      CreateEncodedImage(VideoFrameType::kVideoFrameKey,
                         Timestamp::Millis(100))));
}

TEST(FrameSelectorTest, SelectsBasedOnLowOverheadSpan) {
  FrameSelector selector(CreateTestEnvironment(), ScalabilityMode::kL1T1,
                         kLowOverheadSpan, kHighOverheadSpan);
  // First frame selected (to init).
  EXPECT_TRUE(selector.ShouldInstrumentFrame(
      CreateLowOverheadFrame(Timestamp::Millis(1000)),
      CreateEncodedImage(VideoFrameType::kVideoFrameKey,
                         Timestamp::Millis(1000))));

  // Next frame: should be at least 1s later.
  EXPECT_FALSE(selector.ShouldInstrumentFrame(
      CreateLowOverheadFrame(Timestamp::Millis(1500)),
      CreateEncodedImage(VideoFrameType::kVideoFrameDelta,
                         Timestamp::Millis(1500))));

  // Next frame: > 2s later (upper bound), must be selected.
  EXPECT_TRUE(selector.ShouldInstrumentFrame(
      CreateLowOverheadFrame(Timestamp::Millis(3500)),
      CreateEncodedImage(VideoFrameType::kVideoFrameDelta,
                         Timestamp::Millis(3500))));
}

TEST(FrameSelectorTest, SelectsBasedOnHighOverheadSpan) {
  FrameSelector selector(CreateTestEnvironment(), ScalabilityMode::kL1T1,
                         kLowOverheadSpan, kHighOverheadSpan);
  // First frame selected (to init).
  EXPECT_TRUE(selector.ShouldInstrumentFrame(
      CreateHighOverheadFrame(Timestamp::Millis(1000)),
      CreateEncodedImage(VideoFrameType::kVideoFrameKey,
                         Timestamp::Millis(1000))));

  // Next frame: should be at least 3s later.
  EXPECT_FALSE(selector.ShouldInstrumentFrame(
      CreateHighOverheadFrame(Timestamp::Millis(3500)),
      CreateEncodedImage(VideoFrameType::kVideoFrameDelta,
                         Timestamp::Millis(3500))));

  // Next frame: > 4s later (upper bound), must be selected.
  EXPECT_TRUE(selector.ShouldInstrumentFrame(
      CreateHighOverheadFrame(Timestamp::Millis(5500)),
      CreateEncodedImage(VideoFrameType::kVideoFrameDelta,
                         Timestamp::Millis(5500))));
}

TEST(FrameSelectorTest, IndependentKeyframesWithSimulcast) {
  FrameSelector selector(CreateTestEnvironment(), ScalabilityMode::kS2T2,
                         kLowOverheadSpan, kHighOverheadSpan);
  // Initial keyframe.
  EXPECT_TRUE(selector.ShouldInstrumentFrame(
      CreateLowOverheadFrame(Timestamp::Millis(1000)),
      CreateEncodedImage(VideoFrameType::kVideoFrameKey,
                         Timestamp::Millis(1000), 0, 0)));
  EXPECT_TRUE(selector.ShouldInstrumentFrame(
      CreateLowOverheadFrame(Timestamp::Millis(1000)),
      CreateEncodedImage(VideoFrameType::kVideoFrameKey,
                         Timestamp::Millis(1000), 1, 0)));

  // After 500ms (before low overhead lower bound), issue keyframe on S0.
  // This should instruments S0 but not S1.
  EXPECT_TRUE(selector.ShouldInstrumentFrame(
      CreateLowOverheadFrame(Timestamp::Millis(1500)),
      CreateEncodedImage(VideoFrameType::kVideoFrameKey,
                         Timestamp::Millis(1500), 0, 0)));
  EXPECT_FALSE(selector.ShouldInstrumentFrame(
      CreateLowOverheadFrame(Timestamp::Millis(1500)),
      CreateEncodedImage(VideoFrameType::kVideoFrameDelta,
                         Timestamp::Millis(1500), 1, 0)));
}

TEST(FrameSelectorTest, TreatsDeltaAsKeyframeWithInterLayerPrediction) {
  FrameSelector selector(CreateTestEnvironment(), ScalabilityMode::kL2T2,
                         kLowOverheadSpan, kHighOverheadSpan);
  // Initial keyframe.
  EXPECT_TRUE(selector.ShouldInstrumentFrame(
      CreateLowOverheadFrame(Timestamp::Millis(1000)),
      CreateEncodedImage(VideoFrameType::kVideoFrameKey,
                         Timestamp::Millis(1000), 0, 0)));
  EXPECT_TRUE(selector.ShouldInstrumentFrame(
      CreateLowOverheadFrame(Timestamp::Millis(1000)),
      CreateEncodedImage(VideoFrameType::kVideoFrameDelta,
                         Timestamp::Millis(1000), 1, 0)));

  // After 500ms (before low overhead lower bound), issue keyframe on S0.
  // The delta frame on S1 should be treated as part of the keyframes and be
  // instrumented.
  EXPECT_TRUE(selector.ShouldInstrumentFrame(
      CreateLowOverheadFrame(Timestamp::Millis(1500)),
      CreateEncodedImage(VideoFrameType::kVideoFrameKey,
                         Timestamp::Millis(1500), 0, 0)));
  EXPECT_TRUE(selector.ShouldInstrumentFrame(
      CreateLowOverheadFrame(Timestamp::Millis(1500)),
      CreateEncodedImage(VideoFrameType::kVideoFrameDelta,
                         Timestamp::Millis(1500), 1, 0)));
}

TEST(FrameSelector, SelectsAboutHalfInMiddleOfSpan) {
  // FrameSelector seeds its random number generator from the clock.
  SimulatedClock clock(Timestamp::Seconds(1));
  FrameSelector selector(CreateTestEnvironment({.time = &clock}),
                         ScalabilityMode::kL1T1, kLowOverheadSpan,
                         kHighOverheadSpan);
  Timestamp timestamp = Timestamp::Millis(1000);  // Arbitrary start timestamp;
  const TimeDelta p50_delta =
      (kLowOverheadSpan.lower_bound + kLowOverheadSpan.upper_bound) / 2;
  int num_selected = 0;
  int num_total = 0;
  // Enough frames for the check below to pass with practically any seed.
  for (int i = 0; i < 1000; ++i) {
    if (selector.ShouldInstrumentFrame(
            CreateLowOverheadFrame(timestamp),
            CreateEncodedImage(VideoFrameType::kVideoFrameDelta, timestamp))) {
      ++num_selected;
    } else {
      // Simulate a keyframe to force the state to record now as the last
      // instrumented time and issue a new cutoff time.
      selector.ShouldInstrumentFrame(
          CreateLowOverheadFrame(timestamp),
          CreateEncodedImage(VideoFrameType::kVideoFrameKey, timestamp));
    }
    ++num_total;

    timestamp += p50_delta;
  }
  EXPECT_NEAR(num_selected, num_total / 2.0, num_total / 10);
}

TEST(FrameSelectorTest, FallbackToRtpTimestamp) {
  FrameSelector selector(CreateTestEnvironment(), ScalabilityMode::kL1T1,
                         kLowOverheadSpan, kHighOverheadSpan);
  // Frame with 0 capture time but valid RTP.
  // 90kHz clock. 1 second = 90000.
  // Start at 0.
  EncodedImage first_frame = CreateEncodedImage(VideoFrameType::kVideoFrameKey,
                                                Timestamp::Zero(), 0, 0, 0);
  EXPECT_TRUE(selector.ShouldInstrumentFrame(
      CreateLowOverheadFrame(Timestamp::Zero()), first_frame));

  // Next frame 2.5s later -> 225000 RTP units.
  EncodedImage second_frame = CreateEncodedImage(
      VideoFrameType::kVideoFrameDelta, Timestamp::Zero(), 0, 0, 225000);

  EXPECT_TRUE(selector.ShouldInstrumentFrame(
      CreateLowOverheadFrame(Timestamp::Zero()), second_frame));
}

}  // namespace
}  // namespace webrtc
