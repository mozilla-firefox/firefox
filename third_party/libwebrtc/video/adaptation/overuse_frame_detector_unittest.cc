/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/adaptation/overuse_frame_detector.h"

#include <memory>

#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_adaptation_reason.h"
#include "modules/video_coding/utility/quality_scaler.h"
#include "rtc_base/event.h"
#include "rtc_base/fake_clock.h"
#include "rtc_base/random.h"
#include "rtc_base/task_queue_for_test.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

using ::testing::_;
using ::testing::InvokeWithoutArgs;

namespace {
const int kWidth = 640;
const int kHeight = 480;
// Corresponds to load of 15%
const int kFrameIntervalUs = 33 * rtc::kNumMicrosecsPerMillisec;
const int kProcessTimeUs = 5 * rtc::kNumMicrosecsPerMillisec;
}  // namespace

class MockCpuOveruseObserver : public OveruseFrameDetectorObserverInterface {
 public:
  MockCpuOveruseObserver() {}
  virtual ~MockCpuOveruseObserver() {}

  MOCK_METHOD(void, AdaptUp, (), (override));
  MOCK_METHOD(void, AdaptDown, (), (override));
};

class CpuOveruseObserverImpl : public OveruseFrameDetectorObserverInterface {
 public:
  CpuOveruseObserverImpl() : overuse_(0), normaluse_(0) {}
  virtual ~CpuOveruseObserverImpl() {}

  void AdaptDown() override { ++overuse_; }
  void AdaptUp() override { ++normaluse_; }

  int overuse_;
  int normaluse_;
};

class OveruseFrameDetectorUnderTest : public OveruseFrameDetector {
 public:
  explicit OveruseFrameDetectorUnderTest(
      const Environment& env,
      CpuOveruseMetricsObserver* metrics_observer)
      : OveruseFrameDetector(env, metrics_observer) {}
  ~OveruseFrameDetectorUnderTest() {}

  using OveruseFrameDetector::CheckForOveruse;
  using OveruseFrameDetector::SetOptions;
};

class OveruseFrameDetectorTest : public ::testing::Test,
                                 public CpuOveruseMetricsObserver {
 protected:
  void SetUp() override {
    observer_ = &mock_observer_;
    options_.min_process_count = 0;
    overuse_detector_ = std::make_unique<OveruseFrameDetectorUnderTest>(
        CreateEnvironment(), this);
    // Unfortunately, we can't call SetOptions here, since that would break
    // single-threading requirements in the RunOnTqNormalUsage test.
  }

  void OnEncodedFrameTimeMeasured(int encode_time_ms,
                                  int encode_usage_percent) override {
    encode_usage_percent_ = encode_usage_percent;
  }

  int InitialUsage() {
    return ((options_.low_encode_usage_threshold_percent +
             options_.high_encode_usage_threshold_percent) /
            2.0f) +
           0.5;
  }

  virtual void InsertAndSendFramesWithInterval(int num_frames,
                                               int interval_us,
                                               int width,
                                               int height,
                                               int delay_us) {
    VideoFrame frame =
        VideoFrame::Builder()
            .set_video_frame_buffer(I420Buffer::Create(width, height))
            .set_rotation(webrtc::kVideoRotation_0)
            .set_timestamp_us(0)
            .build();
    uint32_t timestamp = 0;
    while (num_frames-- > 0) {
      frame.set_rtp_timestamp(timestamp);
      int64_t capture_time_us = rtc::TimeMicros();
      overuse_detector_->FrameCaptured(frame, capture_time_us);
      clock_.AdvanceTime(TimeDelta::Micros(delay_us));
      overuse_detector_->FrameSent(timestamp, rtc::TimeMicros(),
                                   capture_time_us, delay_us);
      clock_.AdvanceTime(TimeDelta::Micros(interval_us - delay_us));
      timestamp += interval_us * 90 / 1000;
    }
  }

  virtual void InsertAndSendSimulcastFramesWithInterval(
      int num_frames,
      int interval_us,
      int width,
      int height,
      // One element per layer
      rtc::ArrayView<const int> delays_us) {
    VideoFrame frame =
        VideoFrame::Builder()
            .set_video_frame_buffer(I420Buffer::Create(width, height))
            .set_rotation(webrtc::kVideoRotation_0)
            .set_timestamp_us(0)
            .build();
    uint32_t timestamp = 0;
    while (num_frames-- > 0) {
      frame.set_rtp_timestamp(timestamp);
      int64_t capture_time_us = rtc::TimeMicros();
      overuse_detector_->FrameCaptured(frame, capture_time_us);
      int max_delay_us = 0;
      for (int delay_us : delays_us) {
        if (delay_us > max_delay_us) {
          clock_.AdvanceTime(TimeDelta::Micros(delay_us - max_delay_us));
          max_delay_us = delay_us;
        }

        overuse_detector_->FrameSent(timestamp, rtc::TimeMicros(),
                                     capture_time_us, delay_us);
      }
      overuse_detector_->CheckForOveruse(observer_);
      clock_.AdvanceTime(TimeDelta::Micros(interval_us - max_delay_us));
      timestamp += interval_us * 90 / 1000;
    }
  }

  virtual void InsertAndSendFramesWithRandomInterval(int num_frames,
                                                     int min_interval_us,
                                                     int max_interval_us,
                                                     int width,
                                                     int height,
                                                     int delay_us) {
    webrtc::Random random(17);

    VideoFrame frame =
        VideoFrame::Builder()
            .set_video_frame_buffer(I420Buffer::Create(width, height))
            .set_rotation(webrtc::kVideoRotation_0)
            .set_timestamp_us(0)
            .build();
    uint32_t timestamp = 0;
    while (num_frames-- > 0) {
      frame.set_rtp_timestamp(timestamp);
      int interval_us = random.Rand(min_interval_us, max_interval_us);
      int64_t capture_time_us = rtc::TimeMicros();
      overuse_detector_->FrameCaptured(frame, capture_time_us);
      clock_.AdvanceTime(TimeDelta::Micros(delay_us));
      overuse_detector_->FrameSent(timestamp, rtc::TimeMicros(),
                                   capture_time_us,
                                   absl::optional<int>(delay_us));

      overuse_detector_->CheckForOveruse(observer_);
      // Avoid turning clock backwards.
      if (interval_us > delay_us)
        clock_.AdvanceTime(TimeDelta::Micros(interval_us - delay_us));

      timestamp += interval_us * 90 / 1000;
    }
  }

  virtual void ForceUpdate(int width, int height) {
    // Insert one frame, wait a second and then put in another to force update
    // the usage. From the tests where these are used, adding another sample
    // doesn't affect the expected outcome (this is mainly to check initial
    // values and whether the overuse detector has been reset or not).
    InsertAndSendFramesWithInterval(2, rtc::kNumMicrosecsPerSec, width, height,
                                    kFrameIntervalUs);
  }
  void TriggerOveruse(int num_times) {
    const int kDelayUs = 32 * rtc::kNumMicrosecsPerMillisec;
    for (int i = 0; i < num_times; ++i) {
      InsertAndSendFramesWithInterval(1000, kFrameIntervalUs, kWidth, kHeight,
                                      kDelayUs);
      overuse_detector_->CheckForOveruse(observer_);
    }
  }

  void TriggerUnderuse() {
    const int kDelayUs1 = 5000;
    const int kDelayUs2 = 6000;
    InsertAndSendFramesWithInterval(1300, kFrameIntervalUs, kWidth, kHeight,
                                    kDelayUs1);
    InsertAndSendFramesWithInterval(1, kFrameIntervalUs, kWidth, kHeight,
                                    kDelayUs2);
    overuse_detector_->CheckForOveruse(observer_);
  }

  int UsagePercent() { return encode_usage_percent_; }

  int64_t OveruseProcessingTimeLimitForFramerate(int fps) const {
    int64_t frame_interval = rtc::kNumMicrosecsPerSec / fps;
    int64_t max_processing_time_us =
        (frame_interval * options_.high_encode_usage_threshold_percent) / 100;
    return max_processing_time_us;
  }

  int64_t UnderuseProcessingTimeLimitForFramerate(int fps) const {
    int64_t frame_interval = rtc::kNumMicrosecsPerSec / fps;
    int64_t max_processing_time_us =
        (frame_interval * options_.low_encode_usage_threshold_percent) / 100;
    return max_processing_time_us;
  }

  CpuOveruseOptions options_;
  rtc::ScopedFakeClock clock_;
  MockCpuOveruseObserver mock_observer_;
  OveruseFrameDetectorObserverInterface* observer_;
  std::unique_ptr<OveruseFrameDetectorUnderTest> overuse_detector_;
  int encode_usage_percent_ = -1;
};

// UsagePercent() > high_encode_usage_threshold_percent => overuse.
// UsagePercent() < low_encode_usage_threshold_percent => underuse.
TEST_F(OveruseFrameDetectorTest, TriggerOveruse) {
  // usage > high => overuse
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(1);
  TriggerOveruse(options_.high_threshold_consecutive_count);
}

TEST_F(OveruseFrameDetectorTest, OveruseAndRecover) {
  // usage > high => overuse
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(1);
  TriggerOveruse(options_.high_threshold_consecutive_count);
  // usage < low => underuse
  EXPECT_CALL(mock_observer_, AdaptUp()).Times(::testing::AtLeast(1));
  TriggerUnderuse();
}

TEST_F(OveruseFrameDetectorTest, DoubleOveruseAndRecover) {
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(2);
  TriggerOveruse(options_.high_threshold_consecutive_count);
  TriggerOveruse(options_.high_threshold_consecutive_count);
  EXPECT_CALL(mock_observer_, AdaptUp()).Times(::testing::AtLeast(1));
  TriggerUnderuse();
}

TEST_F(OveruseFrameDetectorTest, TriggerUnderuseWithMinProcessCount) {
  const int kProcessIntervalUs = 5 * rtc::kNumMicrosecsPerSec;
  options_.min_process_count = 1;
  CpuOveruseObserverImpl overuse_observer;
  observer_ = nullptr;
  overuse_detector_->SetOptions(options_);
  InsertAndSendFramesWithInterval(1200, kFrameIntervalUs, kWidth, kHeight,
                                  kProcessTimeUs);
  overuse_detector_->CheckForOveruse(&overuse_observer);
  EXPECT_EQ(0, overuse_observer.normaluse_);
  clock_.AdvanceTime(TimeDelta::Micros(kProcessIntervalUs));
  overuse_detector_->CheckForOveruse(&overuse_observer);
  EXPECT_EQ(1, overuse_observer.normaluse_);
}

TEST_F(OveruseFrameDetectorTest, ConstantOveruseGivesNoNormalUsage) {
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptUp()).Times(0);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(64);
  for (size_t i = 0; i < 64; ++i) {
    TriggerOveruse(options_.high_threshold_consecutive_count);
  }
}

TEST_F(OveruseFrameDetectorTest, ConsecutiveCountTriggersOveruse) {
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(1);
  options_.high_threshold_consecutive_count = 2;
  overuse_detector_->SetOptions(options_);
  TriggerOveruse(2);
}

TEST_F(OveruseFrameDetectorTest, IncorrectConsecutiveCountTriggersNoOveruse) {
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(0);
  options_.high_threshold_consecutive_count = 2;
  overuse_detector_->SetOptions(options_);
  TriggerOveruse(1);
}

TEST_F(OveruseFrameDetectorTest, ProcessingUsage) {
  overuse_detector_->SetOptions(options_);
  InsertAndSendFramesWithInterval(1000, kFrameIntervalUs, kWidth, kHeight,
                                  kProcessTimeUs);
  EXPECT_EQ(kProcessTimeUs * 100 / kFrameIntervalUs, UsagePercent());
}

TEST_F(OveruseFrameDetectorTest, ResetAfterResolutionChange) {
  overuse_detector_->SetOptions(options_);
  ForceUpdate(kWidth, kHeight);
  EXPECT_EQ(InitialUsage(), UsagePercent());
  InsertAndSendFramesWithInterval(1000, kFrameIntervalUs, kWidth, kHeight,
                                  kProcessTimeUs);
  EXPECT_NE(InitialUsage(), UsagePercent());
  // Verify reset (with new width/height).
  ForceUpdate(kWidth, kHeight + 1);
  EXPECT_EQ(InitialUsage(), UsagePercent());
}

TEST_F(OveruseFrameDetectorTest, ResetAfterFrameTimeout) {
  overuse_detector_->SetOptions(options_);
  ForceUpdate(kWidth, kHeight);
  EXPECT_EQ(InitialUsage(), UsagePercent());
  InsertAndSendFramesWithInterval(1000, kFrameIntervalUs, kWidth, kHeight,
                                  kProcessTimeUs);
  EXPECT_NE(InitialUsage(), UsagePercent());
  InsertAndSendFramesWithInterval(
      2, options_.frame_timeout_interval_ms * rtc::kNumMicrosecsPerMillisec,
      kWidth, kHeight, kProcessTimeUs);
  EXPECT_NE(InitialUsage(), UsagePercent());
  // Verify reset.
  InsertAndSendFramesWithInterval(
      2,
      (options_.frame_timeout_interval_ms + 1) * rtc::kNumMicrosecsPerMillisec,
      kWidth, kHeight, kProcessTimeUs);
  ForceUpdate(kWidth, kHeight);
  EXPECT_EQ(InitialUsage(), UsagePercent());
}

TEST_F(OveruseFrameDetectorTest, MinFrameSamplesBeforeUpdating) {
  options_.min_frame_samples = 40;
  overuse_detector_->SetOptions(options_);
  InsertAndSendFramesWithInterval(40, kFrameIntervalUs, kWidth, kHeight,
                                  kProcessTimeUs);
  EXPECT_EQ(InitialUsage(), UsagePercent());
  // Pass time far enough to digest all previous samples.
  clock_.AdvanceTime(TimeDelta::Seconds(1));
  InsertAndSendFramesWithInterval(1, kFrameIntervalUs, kWidth, kHeight,
                                  kProcessTimeUs);
  // The last sample has not been processed here.
  EXPECT_EQ(InitialUsage(), UsagePercent());

  // Pass time far enough to digest all previous samples, 41 in total.
  clock_.AdvanceTime(TimeDelta::Seconds(1));
  InsertAndSendFramesWithInterval(1, kFrameIntervalUs, kWidth, kHeight,
                                  kProcessTimeUs);
  EXPECT_NE(InitialUsage(), UsagePercent());
}

TEST_F(OveruseFrameDetectorTest, InitialProcessingUsage) {
  overuse_detector_->SetOptions(options_);
  ForceUpdate(kWidth, kHeight);
  EXPECT_EQ(InitialUsage(), UsagePercent());
}

TEST_F(OveruseFrameDetectorTest, MeasuresMultipleConcurrentSamples) {
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(::testing::AtLeast(1));
  static const int kIntervalUs = 33 * rtc::kNumMicrosecsPerMillisec;
  static const size_t kNumFramesEncodingDelay = 3;
  VideoFrame frame =
      VideoFrame::Builder()
          .set_video_frame_buffer(I420Buffer::Create(kWidth, kHeight))
          .set_rotation(webrtc::kVideoRotation_0)
          .set_timestamp_us(0)
          .build();
  for (size_t i = 0; i < 1000; ++i) {
    // Unique timestamps.
    frame.set_rtp_timestamp(static_cast<uint32_t>(i));
    int64_t capture_time_us = rtc::TimeMicros();
    overuse_detector_->FrameCaptured(frame, capture_time_us);
    clock_.AdvanceTime(TimeDelta::Micros(kIntervalUs));
    if (i > kNumFramesEncodingDelay) {
      overuse_detector_->FrameSent(
          static_cast<uint32_t>(i - kNumFramesEncodingDelay), rtc::TimeMicros(),
          capture_time_us, kIntervalUs);
    }
    overuse_detector_->CheckForOveruse(observer_);
  }
}

TEST_F(OveruseFrameDetectorTest, UpdatesExistingSamples) {
  // >85% encoding time should trigger overuse.
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(::testing::AtLeast(1));
  static const int kIntervalUs = 33 * rtc::kNumMicrosecsPerMillisec;
  static const int kDelayUs = 30 * rtc::kNumMicrosecsPerMillisec;
  VideoFrame frame =
      VideoFrame::Builder()
          .set_video_frame_buffer(I420Buffer::Create(kWidth, kHeight))
          .set_rotation(webrtc::kVideoRotation_0)
          .set_timestamp_us(0)
          .build();
  uint32_t timestamp = 0;
  for (size_t i = 0; i < 1000; ++i) {
    frame.set_rtp_timestamp(timestamp);
    int64_t capture_time_us = rtc::TimeMicros();
    overuse_detector_->FrameCaptured(frame, capture_time_us);
    // Encode and send first parts almost instantly.
    clock_.AdvanceTime(TimeDelta::Millis(1));
    overuse_detector_->FrameSent(timestamp, rtc::TimeMicros(), capture_time_us,
                                 rtc::kNumMicrosecsPerMillisec);
    // Encode heavier part, resulting in >85% usage total.
    clock_.AdvanceTime(TimeDelta::Micros(kDelayUs) - TimeDelta::Millis(1));
    overuse_detector_->FrameSent(timestamp, rtc::TimeMicros(), capture_time_us,
                                 kDelayUs);
    clock_.AdvanceTime(TimeDelta::Micros(kIntervalUs - kDelayUs));
    timestamp += kIntervalUs * 90 / 1000;
    overuse_detector_->CheckForOveruse(observer_);
  }
}

TEST_F(OveruseFrameDetectorTest, RunOnTqNormalUsage) {
  TaskQueueForTest queue("OveruseFrameDetectorTestQueue");

  queue.SendTask([&] {
    overuse_detector_->StartCheckForOveruse(queue.Get(), options_, observer_);
  });

  rtc::Event event;
  // Expect NormalUsage(). When called, stop the `overuse_detector_` and then
  // set `event` to end the test.
  EXPECT_CALL(mock_observer_, AdaptUp())
      .WillOnce(InvokeWithoutArgs([this, &event] {
        overuse_detector_->StopCheckForOveruse();
        event.Set();
      }));

  queue.PostTask([this] {
    const int kDelayUs1 = 5 * rtc::kNumMicrosecsPerMillisec;
    const int kDelayUs2 = 6 * rtc::kNumMicrosecsPerMillisec;
    InsertAndSendFramesWithInterval(1300, kFrameIntervalUs, kWidth, kHeight,
                                    kDelayUs1);
    InsertAndSendFramesWithInterval(1, kFrameIntervalUs, kWidth, kHeight,
                                    kDelayUs2);
  });

  EXPECT_TRUE(event.Wait(TimeDelta::Seconds(10)));
}

// TODO(crbug.com/webrtc/12846): investigate why the test fails on MAC bots.
#if !defined(WEBRTC_MAC)
TEST_F(OveruseFrameDetectorTest, MaxIntervalScalesWithFramerate) {
  const int kCapturerMaxFrameRate = 30;
  const int kEncodeMaxFrameRate = 20;  // Maximum fps the encoder can sustain.

  overuse_detector_->SetOptions(options_);
  // Trigger overuse.
  int64_t frame_interval_us = rtc::kNumMicrosecsPerSec / kCapturerMaxFrameRate;
  // Processing time just below over use limit given kEncodeMaxFrameRate.
  int64_t processing_time_us =
      (98 * OveruseProcessingTimeLimitForFramerate(kEncodeMaxFrameRate)) / 100;
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(1);
  for (int i = 0; i < options_.high_threshold_consecutive_count; ++i) {
    InsertAndSendFramesWithInterval(1200, frame_interval_us, kWidth, kHeight,
                                    processing_time_us);
    overuse_detector_->CheckForOveruse(observer_);
  }

  // Simulate frame rate reduction and normal usage.
  frame_interval_us = rtc::kNumMicrosecsPerSec / kEncodeMaxFrameRate;
  overuse_detector_->OnTargetFramerateUpdated(kEncodeMaxFrameRate);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(0);
  for (int i = 0; i < options_.high_threshold_consecutive_count; ++i) {
    InsertAndSendFramesWithInterval(1200, frame_interval_us, kWidth, kHeight,
                                    processing_time_us);
    overuse_detector_->CheckForOveruse(observer_);
  }

  // Reduce processing time to trigger underuse.
  processing_time_us =
      (98 * UnderuseProcessingTimeLimitForFramerate(kEncodeMaxFrameRate)) / 100;
  EXPECT_CALL(mock_observer_, AdaptUp()).Times(1);
  InsertAndSendFramesWithInterval(1200, frame_interval_us, kWidth, kHeight,
                                  processing_time_us);
  overuse_detector_->CheckForOveruse(observer_);
}
#endif

TEST_F(OveruseFrameDetectorTest, RespectsMinFramerate) {
  const int kMinFrameRate = 7;  // Minimum fps allowed by current detector impl.
  overuse_detector_->SetOptions(options_);
  overuse_detector_->OnTargetFramerateUpdated(kMinFrameRate);

  // Normal usage just at the limit.
  int64_t frame_interval_us = rtc::kNumMicrosecsPerSec / kMinFrameRate;
  // Processing time just below over use limit given kEncodeMaxFrameRate.
  int64_t processing_time_us =
      (98 * OveruseProcessingTimeLimitForFramerate(kMinFrameRate)) / 100;
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(0);
  for (int i = 0; i < options_.high_threshold_consecutive_count; ++i) {
    InsertAndSendFramesWithInterval(1200, frame_interval_us, kWidth, kHeight,
                                    processing_time_us);
    overuse_detector_->CheckForOveruse(observer_);
  }

  // Over the limit to overuse.
  processing_time_us =
      (102 * OveruseProcessingTimeLimitForFramerate(kMinFrameRate)) / 100;
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(1);
  for (int i = 0; i < options_.high_threshold_consecutive_count; ++i) {
    InsertAndSendFramesWithInterval(1200, frame_interval_us, kWidth, kHeight,
                                    processing_time_us);
    overuse_detector_->CheckForOveruse(observer_);
  }

  // Reduce input frame rate. Should still trigger overuse.
  overuse_detector_->OnTargetFramerateUpdated(kMinFrameRate - 1);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(1);
  for (int i = 0; i < options_.high_threshold_consecutive_count; ++i) {
    InsertAndSendFramesWithInterval(1200, frame_interval_us, kWidth, kHeight,
                                    processing_time_us);
    overuse_detector_->CheckForOveruse(observer_);
  }
}

TEST_F(OveruseFrameDetectorTest, LimitsMaxFrameInterval) {
  const int kMaxFrameRate = 20;
  overuse_detector_->SetOptions(options_);
  overuse_detector_->OnTargetFramerateUpdated(kMaxFrameRate);
  int64_t frame_interval_us = rtc::kNumMicrosecsPerSec / kMaxFrameRate;
  // Maximum frame interval allowed is 35% above ideal.
  int64_t max_frame_interval_us = (135 * frame_interval_us) / 100;
  // Maximum processing time, without triggering overuse, allowed with the above
  // frame interval.
  int64_t max_processing_time_us =
      (max_frame_interval_us * options_.high_encode_usage_threshold_percent) /
      100;

  // Processing time just below overuse limit given kMaxFrameRate.
  int64_t processing_time_us = (98 * max_processing_time_us) / 100;
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(0);
  for (int i = 0; i < options_.high_threshold_consecutive_count; ++i) {
    InsertAndSendFramesWithInterval(1200, max_frame_interval_us, kWidth,
                                    kHeight, processing_time_us);
    overuse_detector_->CheckForOveruse(observer_);
  }

  // Go above limit, trigger overuse.
  processing_time_us = (102 * max_processing_time_us) / 100;
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(1);
  for (int i = 0; i < options_.high_threshold_consecutive_count; ++i) {
    InsertAndSendFramesWithInterval(1200, max_frame_interval_us, kWidth,
                                    kHeight, processing_time_us);
    overuse_detector_->CheckForOveruse(observer_);
  }

  // Increase frame interval, should still trigger overuse.
  max_frame_interval_us *= 2;
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(1);
  for (int i = 0; i < options_.high_threshold_consecutive_count; ++i) {
    InsertAndSendFramesWithInterval(1200, max_frame_interval_us, kWidth,
                                    kHeight, processing_time_us);
    overuse_detector_->CheckForOveruse(observer_);
  }
}

// Models screencast, with irregular arrival of frames which are heavy
// to encode.
TEST_F(OveruseFrameDetectorTest, NoOveruseForLargeRandomFrameInterval) {
  // TODO(bugs.webrtc.org/8504): When new estimator is relanded,
  // behavior is improved in this scenario, with only AdaptUp events,
  // and estimated load closer to the true average.

  // EXPECT_CALL(mock_observer_, AdaptDown()).Times(0);
  // EXPECT_CALL(mock_observer_, AdaptUp())
  //     .Times(::testing::AtLeast(1));
  overuse_detector_->SetOptions(options_);

  const int kNumFrames = 500;
  const int kEncodeTimeUs = 100 * rtc::kNumMicrosecsPerMillisec;

  const int kMinIntervalUs = 30 * rtc::kNumMicrosecsPerMillisec;
  const int kMaxIntervalUs = 1000 * rtc::kNumMicrosecsPerMillisec;

  const int kTargetFramerate = 5;

  overuse_detector_->OnTargetFramerateUpdated(kTargetFramerate);

  InsertAndSendFramesWithRandomInterval(kNumFrames, kMinIntervalUs,
                                        kMaxIntervalUs, kWidth, kHeight,
                                        kEncodeTimeUs);
  // Average usage 19%. Check that estimate is in the right ball park.
  // EXPECT_NEAR(UsagePercent(), 20, 10);
  EXPECT_NEAR(UsagePercent(), 20, 35);
}

// Models screencast, with irregular arrival of frames, often
// exceeding the timeout interval.
TEST_F(OveruseFrameDetectorTest, NoOveruseForRandomFrameIntervalWithReset) {
  // TODO(bugs.webrtc.org/8504): When new estimator is relanded,
  // behavior is improved in this scenario, and we get AdaptUp events.
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(0);
  // EXPECT_CALL(mock_observer_, AdaptUp())
  //     .Times(::testing::AtLeast(1));

  const int kNumFrames = 500;
  const int kEncodeTimeUs = 100 * rtc::kNumMicrosecsPerMillisec;

  const int kMinIntervalUs = 30 * rtc::kNumMicrosecsPerMillisec;
  const int kMaxIntervalUs = 3000 * rtc::kNumMicrosecsPerMillisec;

  const int kTargetFramerate = 5;

  overuse_detector_->OnTargetFramerateUpdated(kTargetFramerate);

  InsertAndSendFramesWithRandomInterval(kNumFrames, kMinIntervalUs,
                                        kMaxIntervalUs, kWidth, kHeight,
                                        kEncodeTimeUs);

  // Average usage 6.6%, but since the frame_timeout_interval_ms is
  // only 1500 ms, we often reset the estimate to the initial value.
  // Check that estimate is in the right ball park.
  EXPECT_GE(UsagePercent(), 1);
  EXPECT_LE(UsagePercent(), InitialUsage() + 5);
}

// Models simulcast, with multiple encoded frames for each input frame.
// Load estimate should be based on the maximum encode time per input frame.
TEST_F(OveruseFrameDetectorTest, NoOveruseForSimulcast) {
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(0);

  constexpr int kNumFrames = 500;
  constexpr int kEncodeTimesUs[] = {
      10 * rtc::kNumMicrosecsPerMillisec,
      8 * rtc::kNumMicrosecsPerMillisec,
      12 * rtc::kNumMicrosecsPerMillisec,
  };
  constexpr int kIntervalUs = 30 * rtc::kNumMicrosecsPerMillisec;

  InsertAndSendSimulcastFramesWithInterval(kNumFrames, kIntervalUs, kWidth,
                                           kHeight, kEncodeTimesUs);

  // Average usage 40%. 12 ms / 30 ms.
  EXPECT_GE(UsagePercent(), 35);
  EXPECT_LE(UsagePercent(), 45);
}

// Tests using new cpu load estimator
class OveruseFrameDetectorTest2 : public OveruseFrameDetectorTest {
 protected:
  void SetUp() override {
    options_.filter_time_ms = 5 * rtc::kNumMillisecsPerSec;
    OveruseFrameDetectorTest::SetUp();
  }

  void InsertAndSendFramesWithInterval(int num_frames,
                                       int interval_us,
                                       int width,
                                       int height,
                                       int delay_us) override {
    VideoFrame frame =
        VideoFrame::Builder()
            .set_video_frame_buffer(I420Buffer::Create(width, height))
            .set_rotation(webrtc::kVideoRotation_0)
            .set_timestamp_us(0)
            .build();
    while (num_frames-- > 0) {
      int64_t capture_time_us = rtc::TimeMicros();
      overuse_detector_->FrameCaptured(frame, capture_time_us /* ignored */);
      overuse_detector_->FrameSent(0 /* ignored timestamp */,
                                   0 /* ignored send_time_us */,
                                   capture_time_us, delay_us);
      clock_.AdvanceTime(TimeDelta::Micros(interval_us));
    }
  }

  void InsertAndSendFramesWithRandomInterval(int num_frames,
                                             int min_interval_us,
                                             int max_interval_us,
                                             int width,
                                             int height,
                                             int delay_us) override {
    webrtc::Random random(17);

    VideoFrame frame =
        VideoFrame::Builder()
            .set_video_frame_buffer(I420Buffer::Create(width, height))
            .set_rotation(webrtc::kVideoRotation_0)
            .set_timestamp_us(0)
            .build();
    for (int i = 0; i < num_frames; i++) {
      int interval_us = random.Rand(min_interval_us, max_interval_us);
      int64_t capture_time_us = rtc::TimeMicros();
      overuse_detector_->FrameCaptured(frame, capture_time_us);
      overuse_detector_->FrameSent(0 /* ignored timestamp */,
                                   0 /* ignored send_time_us */,
                                   capture_time_us, delay_us);

      overuse_detector_->CheckForOveruse(observer_);
      clock_.AdvanceTime(TimeDelta::Micros(interval_us));
    }
  }

  void ForceUpdate(int width, int height) override {
    // This is mainly to check initial values and whether the overuse
    // detector has been reset or not.
    InsertAndSendFramesWithInterval(1, rtc::kNumMicrosecsPerSec, width, height,
                                    kFrameIntervalUs);
  }
};

// UsagePercent() > high_encode_usage_threshold_percent => overuse.
// UsagePercent() < low_encode_usage_threshold_percent => underuse.
TEST_F(OveruseFrameDetectorTest2, TriggerOveruse) {
  // usage > high => overuse
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(1);
  TriggerOveruse(options_.high_threshold_consecutive_count);
}

TEST_F(OveruseFrameDetectorTest2, OveruseAndRecover) {
  // usage > high => overuse
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(1);
  TriggerOveruse(options_.high_threshold_consecutive_count);
  // usage < low => underuse
  EXPECT_CALL(mock_observer_, AdaptUp()).Times(::testing::AtLeast(1));
  TriggerUnderuse();
}

TEST_F(OveruseFrameDetectorTest2, DoubleOveruseAndRecover) {
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(2);
  TriggerOveruse(options_.high_threshold_consecutive_count);
  TriggerOveruse(options_.high_threshold_consecutive_count);
  EXPECT_CALL(mock_observer_, AdaptUp()).Times(::testing::AtLeast(1));
  TriggerUnderuse();
}

TEST_F(OveruseFrameDetectorTest2, TriggerUnderuseWithMinProcessCount) {
  const int kProcessIntervalUs = 5 * rtc::kNumMicrosecsPerSec;
  options_.min_process_count = 1;
  CpuOveruseObserverImpl overuse_observer;
  observer_ = nullptr;
  overuse_detector_->SetOptions(options_);
  InsertAndSendFramesWithInterval(1200, kFrameIntervalUs, kWidth, kHeight,
                                  kProcessTimeUs);
  overuse_detector_->CheckForOveruse(&overuse_observer);
  EXPECT_EQ(0, overuse_observer.normaluse_);
  clock_.AdvanceTime(TimeDelta::Micros(kProcessIntervalUs));
  overuse_detector_->CheckForOveruse(&overuse_observer);
  EXPECT_EQ(1, overuse_observer.normaluse_);
}

TEST_F(OveruseFrameDetectorTest2, ConstantOveruseGivesNoNormalUsage) {
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptUp()).Times(0);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(64);
  for (size_t i = 0; i < 64; ++i) {
    TriggerOveruse(options_.high_threshold_consecutive_count);
  }
}

TEST_F(OveruseFrameDetectorTest2, ConsecutiveCountTriggersOveruse) {
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(1);
  options_.high_threshold_consecutive_count = 2;
  overuse_detector_->SetOptions(options_);
  TriggerOveruse(2);
}

TEST_F(OveruseFrameDetectorTest2, IncorrectConsecutiveCountTriggersNoOveruse) {
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(0);
  options_.high_threshold_consecutive_count = 2;
  overuse_detector_->SetOptions(options_);
  TriggerOveruse(1);
}

TEST_F(OveruseFrameDetectorTest2, ProcessingUsage) {
  overuse_detector_->SetOptions(options_);
  InsertAndSendFramesWithInterval(1000, kFrameIntervalUs, kWidth, kHeight,
                                  kProcessTimeUs);
  EXPECT_EQ(kProcessTimeUs * 100 / kFrameIntervalUs, UsagePercent());
}

TEST_F(OveruseFrameDetectorTest2, ResetAfterResolutionChange) {
  overuse_detector_->SetOptions(options_);
  ForceUpdate(kWidth, kHeight);
  EXPECT_EQ(InitialUsage(), UsagePercent());
  InsertAndSendFramesWithInterval(1000, kFrameIntervalUs, kWidth, kHeight,
                                  kProcessTimeUs);
  EXPECT_NE(InitialUsage(), UsagePercent());
  // Verify reset (with new width/height).
  ForceUpdate(kWidth, kHeight + 1);
  EXPECT_EQ(InitialUsage(), UsagePercent());
}

TEST_F(OveruseFrameDetectorTest2, ResetAfterFrameTimeout) {
  overuse_detector_->SetOptions(options_);
  ForceUpdate(kWidth, kHeight);
  EXPECT_EQ(InitialUsage(), UsagePercent());
  InsertAndSendFramesWithInterval(1000, kFrameIntervalUs, kWidth, kHeight,
                                  kProcessTimeUs);
  EXPECT_NE(InitialUsage(), UsagePercent());
  InsertAndSendFramesWithInterval(
      2, options_.frame_timeout_interval_ms * rtc::kNumMicrosecsPerMillisec,
      kWidth, kHeight, kProcessTimeUs);
  EXPECT_NE(InitialUsage(), UsagePercent());
  // Verify reset.
  InsertAndSendFramesWithInterval(
      2,
      (options_.frame_timeout_interval_ms + 1) * rtc::kNumMicrosecsPerMillisec,
      kWidth, kHeight, kProcessTimeUs);
  ForceUpdate(kWidth, kHeight);
  EXPECT_EQ(InitialUsage(), UsagePercent());
}

TEST_F(OveruseFrameDetectorTest2, ConvergesSlowly) {
  overuse_detector_->SetOptions(options_);
  InsertAndSendFramesWithInterval(1, kFrameIntervalUs, kWidth, kHeight,
                                  kProcessTimeUs);
  // No update for the first sample.
  EXPECT_EQ(InitialUsage(), UsagePercent());

  // Total time approximately 40 * 33ms = 1.3s, significantly less
  // than the 5s time constant.
  InsertAndSendFramesWithInterval(40, kFrameIntervalUs, kWidth, kHeight,
                                  kProcessTimeUs);

  // Should have started to approach correct load of 15%, but not very far.
  EXPECT_LT(UsagePercent(), InitialUsage());
  EXPECT_GT(UsagePercent(), (InitialUsage() * 3 + 8) / 4);

  // Run for roughly 10s more, should now be closer.
  InsertAndSendFramesWithInterval(300, kFrameIntervalUs, kWidth, kHeight,
                                  kProcessTimeUs);
  EXPECT_NEAR(UsagePercent(), 20, 5);
}

TEST_F(OveruseFrameDetectorTest2, InitialProcessingUsage) {
  overuse_detector_->SetOptions(options_);
  ForceUpdate(kWidth, kHeight);
  EXPECT_EQ(InitialUsage(), UsagePercent());
}

TEST_F(OveruseFrameDetectorTest2, MeasuresMultipleConcurrentSamples) {
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(::testing::AtLeast(1));
  static const int kIntervalUs = 33 * rtc::kNumMicrosecsPerMillisec;
  static const size_t kNumFramesEncodingDelay = 3;
  VideoFrame frame =
      VideoFrame::Builder()
          .set_video_frame_buffer(I420Buffer::Create(kWidth, kHeight))
          .set_rotation(webrtc::kVideoRotation_0)
          .set_timestamp_us(0)
          .build();
  for (size_t i = 0; i < 1000; ++i) {
    // Unique timestamps.
    frame.set_rtp_timestamp(static_cast<uint32_t>(i));
    int64_t capture_time_us = rtc::TimeMicros();
    overuse_detector_->FrameCaptured(frame, capture_time_us);
    clock_.AdvanceTime(TimeDelta::Micros(kIntervalUs));
    if (i > kNumFramesEncodingDelay) {
      overuse_detector_->FrameSent(
          static_cast<uint32_t>(i - kNumFramesEncodingDelay), rtc::TimeMicros(),
          capture_time_us, kIntervalUs);
    }
    overuse_detector_->CheckForOveruse(observer_);
  }
}

TEST_F(OveruseFrameDetectorTest2, UpdatesExistingSamples) {
  // >85% encoding time should trigger overuse.
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(::testing::AtLeast(1));
  static const int kIntervalUs = 33 * rtc::kNumMicrosecsPerMillisec;
  static const int kDelayUs = 30 * rtc::kNumMicrosecsPerMillisec;
  VideoFrame frame =
      VideoFrame::Builder()
          .set_video_frame_buffer(I420Buffer::Create(kWidth, kHeight))
          .set_rotation(webrtc::kVideoRotation_0)
          .set_timestamp_us(0)
          .build();
  uint32_t timestamp = 0;
  for (size_t i = 0; i < 1000; ++i) {
    frame.set_rtp_timestamp(timestamp);
    int64_t capture_time_us = rtc::TimeMicros();
    overuse_detector_->FrameCaptured(frame, capture_time_us);
    // Encode and send first parts almost instantly.
    clock_.AdvanceTime(TimeDelta::Millis(1));
    overuse_detector_->FrameSent(timestamp, rtc::TimeMicros(), capture_time_us,
                                 rtc::kNumMicrosecsPerMillisec);
    // Encode heavier part, resulting in >85% usage total.
    clock_.AdvanceTime(TimeDelta::Micros(kDelayUs) - TimeDelta::Millis(1));
    overuse_detector_->FrameSent(timestamp, rtc::TimeMicros(), capture_time_us,
                                 kDelayUs);
    clock_.AdvanceTime(TimeDelta::Micros(kIntervalUs - kDelayUs));
    timestamp += kIntervalUs * 90 / 1000;
    overuse_detector_->CheckForOveruse(observer_);
  }
}

TEST_F(OveruseFrameDetectorTest2, RunOnTqNormalUsage) {
  TaskQueueForTest queue("OveruseFrameDetectorTestQueue");

  queue.SendTask([&] {
    overuse_detector_->StartCheckForOveruse(queue.Get(), options_, observer_);
  });

  rtc::Event event;
  // Expect NormalUsage(). When called, stop the `overuse_detector_` and then
  // set `event` to end the test.
  EXPECT_CALL(mock_observer_, AdaptUp())
      .WillOnce(InvokeWithoutArgs([this, &event] {
        overuse_detector_->StopCheckForOveruse();
        event.Set();
      }));

  queue.PostTask([this] {
    const int kDelayUs1 = 5 * rtc::kNumMicrosecsPerMillisec;
    const int kDelayUs2 = 6 * rtc::kNumMicrosecsPerMillisec;
    InsertAndSendFramesWithInterval(1300, kFrameIntervalUs, kWidth, kHeight,
                                    kDelayUs1);
    InsertAndSendFramesWithInterval(1, kFrameIntervalUs, kWidth, kHeight,
                                    kDelayUs2);
  });

  EXPECT_TRUE(event.Wait(TimeDelta::Seconds(10)));
}

// Models screencast, with irregular arrival of frames which are heavy
// to encode.
TEST_F(OveruseFrameDetectorTest2, NoOveruseForLargeRandomFrameInterval) {
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(0);
  EXPECT_CALL(mock_observer_, AdaptUp()).Times(::testing::AtLeast(1));

  const int kNumFrames = 500;
  const int kEncodeTimeUs = 100 * rtc::kNumMicrosecsPerMillisec;

  const int kMinIntervalUs = 30 * rtc::kNumMicrosecsPerMillisec;
  const int kMaxIntervalUs = 1000 * rtc::kNumMicrosecsPerMillisec;

  InsertAndSendFramesWithRandomInterval(kNumFrames, kMinIntervalUs,
                                        kMaxIntervalUs, kWidth, kHeight,
                                        kEncodeTimeUs);
  // Average usage 19%. Check that estimate is in the right ball park.
  EXPECT_NEAR(UsagePercent(), 20, 10);
}

// Models screencast, with irregular arrival of frames, often
// exceeding the timeout interval.
TEST_F(OveruseFrameDetectorTest2, NoOveruseForRandomFrameIntervalWithReset) {
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(0);
  EXPECT_CALL(mock_observer_, AdaptUp()).Times(::testing::AtLeast(1));

  const int kNumFrames = 500;
  const int kEncodeTimeUs = 100 * rtc::kNumMicrosecsPerMillisec;

  const int kMinIntervalUs = 30 * rtc::kNumMicrosecsPerMillisec;
  const int kMaxIntervalUs = 3000 * rtc::kNumMicrosecsPerMillisec;

  InsertAndSendFramesWithRandomInterval(kNumFrames, kMinIntervalUs,
                                        kMaxIntervalUs, kWidth, kHeight,
                                        kEncodeTimeUs);

  // Average usage 6.6%, but since the frame_timeout_interval_ms is
  // only 1500 ms, we often reset the estimate to the initial value.
  // Check that estimate is in the right ball park.
  EXPECT_GE(UsagePercent(), 1);
  EXPECT_LE(UsagePercent(), InitialUsage() + 5);
}

TEST_F(OveruseFrameDetectorTest2, ToleratesOutOfOrderFrames) {
  overuse_detector_->SetOptions(options_);
  // Represents a cpu utilization close to 100%. First input frame results in
  // three encoded frames, and the last of those isn't finished until after the
  // first encoded frame corresponding to the next input frame.
  const int kEncodeTimeUs = 30 * rtc::kNumMicrosecsPerMillisec;
  const int kCaptureTimesMs[] = {33, 33, 66, 33};

  for (int capture_time_ms : kCaptureTimesMs) {
    overuse_detector_->FrameSent(
        0, 0, capture_time_ms * rtc::kNumMicrosecsPerMillisec, kEncodeTimeUs);
  }
  EXPECT_GE(UsagePercent(), InitialUsage());
}

// Models simulcast, with multiple encoded frames for each input frame.
// Load estimate should be based on the maximum encode time per input frame.
TEST_F(OveruseFrameDetectorTest2, NoOveruseForSimulcast) {
  overuse_detector_->SetOptions(options_);
  EXPECT_CALL(mock_observer_, AdaptDown()).Times(0);

  constexpr int kNumFrames = 500;
  constexpr int kEncodeTimesUs[] = {
      10 * rtc::kNumMicrosecsPerMillisec,
      8 * rtc::kNumMicrosecsPerMillisec,
      12 * rtc::kNumMicrosecsPerMillisec,
  };
  constexpr int kIntervalUs = 30 * rtc::kNumMicrosecsPerMillisec;

  InsertAndSendSimulcastFramesWithInterval(kNumFrames, kIntervalUs, kWidth,
                                           kHeight, kEncodeTimesUs);

  // Average usage 40%. 12 ms / 30 ms.
  EXPECT_GE(UsagePercent(), 35);
  EXPECT_LE(UsagePercent(), 45);
}

}  // namespace webrtc
