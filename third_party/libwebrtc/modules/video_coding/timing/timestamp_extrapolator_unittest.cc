/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/timing/timestamp_extrapolator.h"

#include <stdint.h>

#include <limits>

#include "absl/types/optional.h"
#include "api/units/frequency.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "system_wrappers/include/clock.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

using ::testing::Eq;
using ::testing::Optional;

namespace {

constexpr Frequency kRtpHz = Frequency::KiloHertz(90);
constexpr Frequency k25Fps = Frequency::Hertz(25);
constexpr TimeDelta k25FpsDelay = 1 / k25Fps;

}  // namespace

TEST(TimestampExtrapolatorTest, ExtrapolationOccursAfter2Packets) {
  SimulatedClock clock(Timestamp::Millis(1337));
  TimestampExtrapolator ts_extrapolator(clock.CurrentTime());

  // No packets so no timestamp.
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(90000), Eq(absl::nullopt));

  uint32_t rtp = 90000;
  clock.AdvanceTime(k25FpsDelay);
  // First result is a bit confusing since it is based off the "start" time,
  // which is arbitrary.
  ts_extrapolator.Update(clock.CurrentTime(), rtp);
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(rtp),
              Optional(clock.CurrentTime()));

  rtp += kRtpHz / k25Fps;
  clock.AdvanceTime(k25FpsDelay);
  ts_extrapolator.Update(clock.CurrentTime(), rtp);
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(rtp),
              Optional(clock.CurrentTime()));
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(rtp + 90000),
              Optional(clock.CurrentTime() + TimeDelta::Seconds(1)));
}

TEST(TimestampExtrapolatorTest, ResetsAfter10SecondPause) {
  SimulatedClock clock(Timestamp::Millis(1337));
  TimestampExtrapolator ts_extrapolator(clock.CurrentTime());

  uint32_t rtp = 90000;
  ts_extrapolator.Update(clock.CurrentTime(), rtp);
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(rtp),
              Optional(clock.CurrentTime()));

  rtp += kRtpHz / k25Fps;
  clock.AdvanceTime(k25FpsDelay);
  ts_extrapolator.Update(clock.CurrentTime(), rtp);
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(rtp),
              Optional(clock.CurrentTime()));

  rtp += 10 * kRtpHz.hertz();
  clock.AdvanceTime(TimeDelta::Seconds(10) + TimeDelta::Micros(1));
  ts_extrapolator.Update(clock.CurrentTime(), rtp);
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(rtp),
              Optional(clock.CurrentTime()));
}

TEST(TimestampExtrapolatorTest, TimestampExtrapolatesMultipleRtpWrapArounds) {
  SimulatedClock clock(Timestamp::Millis(1337));
  TimestampExtrapolator ts_extrapolator(clock.CurrentTime());

  uint32_t rtp = std::numeric_limits<uint32_t>::max();
  ts_extrapolator.Update(clock.CurrentTime(), rtp);
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(rtp),
              Optional(clock.CurrentTime()));

  // One overflow. Static cast to avoid undefined behaviour with +=.
  rtp += static_cast<uint32_t>(kRtpHz / k25Fps);
  clock.AdvanceTime(k25FpsDelay);
  ts_extrapolator.Update(clock.CurrentTime(), rtp);
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(rtp),
              Optional(clock.CurrentTime()));

  // Assert that extrapolation works across the boundary as expected.
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(rtp + 90000),
              Optional(clock.CurrentTime() + TimeDelta::Seconds(1)));
  // This is not quite 1s since the math always rounds up.
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(rtp - 90000),
              Optional(clock.CurrentTime() - TimeDelta::Millis(999)));

  // In order to avoid a wrap arounds reset, add a packet every 10s until we
  // overflow twice.
  constexpr TimeDelta kRtpOverflowDelay =
      std::numeric_limits<uint32_t>::max() / kRtpHz;
  const Timestamp overflow_time = clock.CurrentTime() + kRtpOverflowDelay * 2;

  while (clock.CurrentTime() < overflow_time) {
    clock.AdvanceTime(TimeDelta::Seconds(10));
    // Static-cast before += to avoid undefined behaviour of overflow.
    rtp += static_cast<uint32_t>(kRtpHz * TimeDelta::Seconds(10));
    ts_extrapolator.Update(clock.CurrentTime(), rtp);
    EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(rtp),
                Optional(clock.CurrentTime()));
  }
}

TEST(TimestampExtrapolatorTest, NegativeRtpTimestampWrapAround) {
  SimulatedClock clock(Timestamp::Millis(1337));
  TimestampExtrapolator ts_extrapolator(clock.CurrentTime());
  uint32_t rtp = 0;
  ts_extrapolator.Update(clock.CurrentTime(), rtp);
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(rtp),
              Optional(clock.CurrentTime()));
  // Go backwards! Static cast to avoid undefined behaviour with -=.
  rtp -= static_cast<uint32_t>(kRtpHz.hertz());
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(rtp),
              Optional(clock.CurrentTime() - TimeDelta::Seconds(1)));
}

TEST(TimestampExtrapolatorTest, NegativeRtpTimestampWrapAroundSecondScenario) {
  SimulatedClock clock(Timestamp::Millis(1337));
  TimestampExtrapolator ts_extrapolator(clock.CurrentTime());
  uint32_t rtp = 0;
  ts_extrapolator.Update(clock.CurrentTime(), rtp);
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(rtp),
              Optional(clock.CurrentTime()));
  // Go backwards! Static cast to avoid undefined behaviour with -=.
  rtp -= static_cast<uint32_t>(kRtpHz * TimeDelta::Seconds(10));
  ts_extrapolator.Update(clock.CurrentTime(), rtp);
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(rtp), absl::nullopt);
}

TEST(TimestampExtrapolatorTest, Slow90KHzClock) {
  // This simulates a slow camera, which produces frames at 24Hz instead of
  // 25Hz. The extrapolator should be able to resolve this with enough data.
  SimulatedClock clock(Timestamp::Millis(1337));
  TimestampExtrapolator ts_extrapolator(clock.CurrentTime());

  constexpr TimeDelta k24FpsDelay = 1 / Frequency::Hertz(24);
  uint32_t rtp = 90000;
  ts_extrapolator.Update(clock.CurrentTime(), rtp);

  // Slow camera will increment RTP at 25 FPS rate even though its producing at
  // 24 FPS. After 25 frames the extrapolator should settle at this rate.
  for (int i = 0; i < 25; ++i) {
    rtp += kRtpHz / k25Fps;
    clock.AdvanceTime(k24FpsDelay);
    ts_extrapolator.Update(clock.CurrentTime(), rtp);
  }

  // The camera would normally produce 25 frames in 90K ticks, but is slow
  // so takes 1s + k24FpsDelay for 90K ticks.
  constexpr Frequency kSlowRtpHz = 90000 / (25 * k24FpsDelay);
  // The extrapolator will be predicting that time at millisecond precision.
  auto ts = ts_extrapolator.ExtrapolateLocalTime(rtp + kSlowRtpHz.hertz());
  ASSERT_TRUE(ts.has_value());
  EXPECT_EQ(ts->ms(), clock.TimeInMilliseconds() + 1000);
}

TEST(TimestampExtrapolatorTest, Fast90KHzClock) {
  // This simulates a fast camera, which produces frames at 26Hz instead of
  // 25Hz. The extrapolator should be able to resolve this with enough data.
  SimulatedClock clock(Timestamp::Millis(1337));
  TimestampExtrapolator ts_extrapolator(clock.CurrentTime());

  constexpr TimeDelta k26FpsDelay = 1 / Frequency::Hertz(26);
  uint32_t rtp = 90000;
  ts_extrapolator.Update(clock.CurrentTime(), rtp);

  // Fast camera will increment RTP at 25 FPS rate even though its producing at
  // 26 FPS. After 25 frames the extrapolator should settle at this rate.
  for (int i = 0; i < 25; ++i) {
    rtp += kRtpHz / k25Fps;
    clock.AdvanceTime(k26FpsDelay);
    ts_extrapolator.Update(clock.CurrentTime(), rtp);
  }

  // The camera would normally produce 25 frames in 90K ticks, but is slow
  // so takes 1s + k24FpsDelay for 90K ticks.
  constexpr Frequency kSlowRtpHz = 90000 / (25 * k26FpsDelay);
  // The extrapolator will be predicting that time at millisecond precision.
  auto ts = ts_extrapolator.ExtrapolateLocalTime(rtp + kSlowRtpHz.hertz());
  ASSERT_TRUE(ts.has_value());
  EXPECT_EQ(ts->ms(), clock.TimeInMilliseconds() + 1000);
}

TEST(TimestampExtrapolatorTest, TimestampJump) {
  // This simulates a jump in RTP timestamp, which could occur if a camera was
  // swapped for example.
  SimulatedClock clock(Timestamp::Millis(1337));
  TimestampExtrapolator ts_extrapolator(clock.CurrentTime());

  uint32_t rtp = 90000;
  clock.AdvanceTime(k25FpsDelay);
  ts_extrapolator.Update(clock.CurrentTime(), rtp);
  rtp += kRtpHz / k25Fps;
  clock.AdvanceTime(k25FpsDelay);
  ts_extrapolator.Update(clock.CurrentTime(), rtp);
  rtp += kRtpHz / k25Fps;
  clock.AdvanceTime(k25FpsDelay);
  ts_extrapolator.Update(clock.CurrentTime(), rtp);
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(rtp),
              Optional(clock.CurrentTime()));
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(rtp + 90000),
              Optional(clock.CurrentTime() + TimeDelta::Seconds(1)));

  // Jump RTP.
  uint32_t new_rtp = 1337 * 90000;
  clock.AdvanceTime(k25FpsDelay);
  ts_extrapolator.Update(clock.CurrentTime(), new_rtp);
  new_rtp += kRtpHz / k25Fps;
  clock.AdvanceTime(k25FpsDelay);
  ts_extrapolator.Update(clock.CurrentTime(), new_rtp);
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(new_rtp),
              Optional(clock.CurrentTime()));
}

TEST(TimestampExtrapolatorTest, GapInReceivedFrames) {
  SimulatedClock clock(
      Timestamp::Seconds(std::numeric_limits<uint32_t>::max() / 90000 - 31));
  TimestampExtrapolator ts_extrapolator(clock.CurrentTime());

  uint32_t rtp = std::numeric_limits<uint32_t>::max();
  clock.AdvanceTime(k25FpsDelay);
  ts_extrapolator.Update(clock.CurrentTime(), rtp);

  rtp += 30 * 90000;
  clock.AdvanceTime(TimeDelta::Seconds(30));
  ts_extrapolator.Update(clock.CurrentTime(), rtp);
  EXPECT_THAT(ts_extrapolator.ExtrapolateLocalTime(rtp),
              Optional(clock.CurrentTime()));
}

}  // namespace webrtc
