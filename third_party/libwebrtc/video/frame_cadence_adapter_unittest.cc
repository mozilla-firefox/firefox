/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/frame_cadence_adapter.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "api/field_trials.h"
#include "api/field_trials_view.h"
#include "api/make_ref_counted.h"
#include "api/metronome/test/fake_metronome.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/nv12_buffer.h"
#include "api/video/video_frame.h"
#include "api/video_track_source_constraints.h"
#include "rtc_base/event.h"
#include "rtc_base/logging.h"
#include "rtc_base/rate_statistics.h"
#include "rtc_base/task_queue_for_test.h"
#include "rtc_base/thread.h"
#include "rtc_base/time_utils.h"
#include "system_wrappers/include/clock.h"
#include "system_wrappers/include/metrics.h"
#include "system_wrappers/include/ntp_time.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/time_controller/simulated_time_controller.h"

namespace webrtc {
namespace {

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::InSequence;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Pair;
using ::testing::Values;

VideoFrame CreateFrame() {
  return VideoFrame::Builder()
      .set_video_frame_buffer(
          make_ref_counted<NV12Buffer>(/*width=*/16, /*height=*/16))
      .build();
}

VideoFrame CreateFrameWithTimestamps(
    GlobalSimulatedTimeController* time_controller) {
  return VideoFrame::Builder()
      .set_video_frame_buffer(
          make_ref_counted<NV12Buffer>(/*width=*/16, /*height=*/16))
      .set_ntp_time_ms(time_controller->GetClock()->CurrentNtpInMilliseconds())
      .set_timestamp_us(time_controller->GetClock()->CurrentTime().us())
      .build();
}

std::unique_ptr<FrameCadenceAdapterInterface> CreateAdapter(
    const FieldTrialsView& field_trials,
    Clock* clock) {
  return FrameCadenceAdapterInterface::Create(
      clock, TaskQueueBase::Current(), /*metronome=*/nullptr,
      /*worker_queue=*/nullptr, field_trials);
}

class MockCallback : public FrameCadenceAdapterInterface::Callback {
 public:
  MOCK_METHOD(void, OnFrame, (Timestamp, bool, const VideoFrame&), (override));
  MOCK_METHOD(void, OnDiscardedFrame, (), (override));
  MOCK_METHOD(void, RequestRefreshFrame, (), (override));
};

TEST(FrameCadenceAdapterTest, CountsOutstandingFramesToProcess) {
  FieldTrials no_field_trials = CreateTestFieldTrials();
  GlobalSimulatedTimeController time_controller(Timestamp::Millis(1));
  MockCallback callback;
  auto adapter = CreateAdapter(no_field_trials, time_controller.GetClock());
  adapter->Initialize(&callback);
  EXPECT_CALL(callback, OnFrame(_, true, _)).Times(1);
  EXPECT_CALL(callback, OnFrame(_, false, _)).Times(1);
  auto frame = CreateFrame();
  adapter->OnFrame(frame);
  adapter->OnFrame(frame);
  time_controller.AdvanceTime(TimeDelta::Zero());
  EXPECT_CALL(callback, OnFrame(_, false, _)).Times(1);
  adapter->OnFrame(frame);
  time_controller.AdvanceTime(TimeDelta::Zero());
}

TEST(FrameCadenceAdapterTest, FrameRateFollowsRateStatisticsByDefault) {
  FieldTrials no_field_trials = CreateTestFieldTrials();
  GlobalSimulatedTimeController time_controller(Timestamp::Zero());
  auto adapter = CreateAdapter(no_field_trials, time_controller.GetClock());
  MockCallback callback;
  adapter->Initialize(&callback);

  // Create an "oracle" rate statistics which should be followed on a sequence
  // of frames.
  RateStatistics rate(
      FrameCadenceAdapterInterface::kFrameRateAveragingWindowSizeMs, 1000);

  for (int frame = 0; frame != 10; ++frame) {
    time_controller.AdvanceTime(TimeDelta::Millis(10));
    std::optional<int64_t> expected_fps =
        rate.Rate(time_controller.GetClock()->TimeInMilliseconds());
    rate.Update(1, time_controller.GetClock()->TimeInMilliseconds());
    // FrameCadanceAdapter::OnFrame post the frame to another sequence.
    adapter->OnFrame(CreateFrameWithTimestamps(&time_controller));
    time_controller.AdvanceTime(TimeDelta::Millis(0));
    EXPECT_EQ(expected_fps, adapter->GetInputFrameRateFps())
        << " failed for frame " << frame;
  }
}

TEST(FrameCadenceAdapterTest, FrameRateFollowsMaxFpsWhenZeroHertzActivated) {
  GlobalSimulatedTimeController time_controller(Timestamp::Zero());
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = CreateAdapter(no_field_trials, time_controller.GetClock());
  MockCallback callback;
  adapter->Initialize(&callback);
  adapter->SetZeroHertzModeEnabled(
      FrameCadenceAdapterInterface::ZeroHertzModeParams{});
  adapter->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = 1});
  for (int frame = 0; frame != 10; ++frame) {
    time_controller.AdvanceTime(TimeDelta::Millis(10));
    // FrameCadanceAdapter::OnFrame post the frame to another sequence.
    adapter->OnFrame(CreateFrameWithTimestamps(&time_controller));
    time_controller.AdvanceTime(TimeDelta::Millis(0));
    EXPECT_EQ(adapter->GetInputFrameRateFps(), 1u);
  }
}

TEST(FrameCadenceAdapterTest, ZeroHertzAdapterSupportsMaxFpsChange) {
  GlobalSimulatedTimeController time_controller(Timestamp::Zero());
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = CreateAdapter(no_field_trials, time_controller.GetClock());
  MockCallback callback;
  adapter->Initialize(&callback);
  adapter->SetZeroHertzModeEnabled(
      FrameCadenceAdapterInterface::ZeroHertzModeParams{});
  adapter->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = 1});
  time_controller.AdvanceTime(TimeDelta::Zero());
  EXPECT_EQ(adapter->GetInputFrameRateFps(), 1u);
  adapter->OnFrame(CreateFrame());
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
  adapter->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = 2});
  time_controller.AdvanceTime(TimeDelta::Zero());
  EXPECT_EQ(adapter->GetInputFrameRateFps(), 2u);
  adapter->OnFrame(CreateFrame());
  // Ensure that the max_fps has been changed from 1 to 2 fps even if it was
  // changed while zero hertz was already active.
  EXPECT_CALL(callback, OnFrame);
  time_controller.AdvanceTime(TimeDelta::Millis(500));
}

TEST(FrameCadenceAdapterTest,
     FrameRateFollowsRateStatisticsAfterZeroHertzDeactivated) {
  GlobalSimulatedTimeController time_controller(Timestamp::Zero());
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = CreateAdapter(no_field_trials, time_controller.GetClock());
  MockCallback callback;
  adapter->Initialize(&callback);
  adapter->SetZeroHertzModeEnabled(
      FrameCadenceAdapterInterface::ZeroHertzModeParams{});
  adapter->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = 1});
  RateStatistics rate(
      FrameCadenceAdapterInterface::kFrameRateAveragingWindowSizeMs, 1000);
  constexpr int MAX = 10;
  for (int frame = 0; frame != MAX; ++frame) {
    time_controller.AdvanceTime(TimeDelta::Millis(10));
    rate.Update(1, time_controller.GetClock()->TimeInMilliseconds());
    adapter->OnFrame(CreateFrameWithTimestamps(&time_controller));
    time_controller.AdvanceTime(TimeDelta::Millis(0));
  }
  // Turn off zero hertz on the next-last frame; after the last frame we
  // should see a value that tracks the rate oracle.
  adapter->SetZeroHertzModeEnabled(std::nullopt);
  // Last frame.
  time_controller.AdvanceTime(TimeDelta::Millis(10));
  adapter->OnFrame(CreateFrameWithTimestamps(&time_controller));
  time_controller.AdvanceTime(TimeDelta::Millis(0));

  EXPECT_EQ(rate.Rate(time_controller.GetClock()->TimeInMilliseconds()),
            adapter->GetInputFrameRateFps());
}

TEST(FrameCadenceAdapterTest, ForwardsFramesDelayed) {
  MockCallback callback;
  GlobalSimulatedTimeController time_controller(Timestamp::Zero());
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = CreateAdapter(no_field_trials, time_controller.GetClock());
  adapter->Initialize(&callback);
  adapter->SetZeroHertzModeEnabled(
      FrameCadenceAdapterInterface::ZeroHertzModeParams{});
  adapter->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = 1});
  constexpr int kNumFrames = 3;
  NtpTime original_ntp_time = time_controller.GetClock()->CurrentNtpTime();
  auto frame = CreateFrameWithTimestamps(&time_controller);
  int64_t original_timestamp_us = frame.timestamp_us();
  for (int index = 0; index != kNumFrames; ++index) {
    EXPECT_CALL(callback, OnFrame).Times(0);
    adapter->OnFrame(frame);
    EXPECT_CALL(callback, OnFrame)
        .WillOnce([&](Timestamp post_time, bool, const VideoFrame& frame) {
          EXPECT_EQ(post_time, time_controller.GetClock()->CurrentTime());
          EXPECT_EQ(frame.timestamp_us(),
                    original_timestamp_us + index * kNumMicrosecsPerSec);
          EXPECT_EQ(frame.ntp_time_ms(),
                    original_ntp_time.ToMs() + index * kNumMillisecsPerSec);
        });
    time_controller.AdvanceTime(TimeDelta::Seconds(1));
    frame = CreateFrameWithTimestamps(&time_controller);
  }
}

TEST(FrameCadenceAdapterTest, DelayedProcessingUnderSlightContention) {
  GlobalSimulatedTimeController time_controller(Timestamp::Zero());
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = CreateAdapter(no_field_trials, time_controller.GetClock());
  MockCallback callback;
  adapter->Initialize(&callback);
  adapter->SetZeroHertzModeEnabled(
      FrameCadenceAdapterInterface::ZeroHertzModeParams{});
  adapter->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = 1});

  // Expect frame delivery at 1 sec despite target sequence not running
  // callbacks for the time skipped.
  constexpr TimeDelta time_skipped = TimeDelta::Millis(999);
  EXPECT_CALL(callback, OnFrame).WillOnce([&] {
    EXPECT_EQ(time_controller.GetClock()->CurrentTime(),
              Timestamp::Zero() + TimeDelta::Seconds(1));
  });
  adapter->OnFrame(CreateFrame());
  time_controller.SkipForwardBy(time_skipped);
  time_controller.AdvanceTime(TimeDelta::Seconds(1) - time_skipped);
}

TEST(FrameCadenceAdapterTest, DelayedProcessingUnderHeavyContention) {
  GlobalSimulatedTimeController time_controller(Timestamp::Zero());
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = CreateAdapter(no_field_trials, time_controller.GetClock());
  MockCallback callback;
  adapter->Initialize(&callback);
  adapter->SetZeroHertzModeEnabled(
      FrameCadenceAdapterInterface::ZeroHertzModeParams{});
  adapter->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = 1});

  // Expect frame delivery at origin + `time_skipped` when the target sequence
  // is not running callbacks for the initial 1+ sec.
  constexpr TimeDelta time_skipped =
      TimeDelta::Seconds(1) + TimeDelta::Micros(1);
  EXPECT_CALL(callback, OnFrame).WillOnce([&] {
    EXPECT_EQ(time_controller.GetClock()->CurrentTime(),
              Timestamp::Zero() + time_skipped);
  });
  adapter->OnFrame(CreateFrame());
  time_controller.SkipForwardBy(time_skipped);
  time_controller.AdvanceTime(TimeDelta::Zero());
}

TEST(FrameCadenceAdapterTest, RepeatsFramesDelayed) {
  // Logic in the frame cadence adapter avoids modifying frame NTP and render
  // timestamps if these timestamps looks unset, which is the case when the
  // clock is initialized running from 0. For this reason we choose the
  // `time_controller` initialization constant to something arbitrary which is
  // not 0.
  MockCallback callback;
  GlobalSimulatedTimeController time_controller(Timestamp::Millis(47892223));
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = CreateAdapter(no_field_trials, time_controller.GetClock());
  adapter->Initialize(&callback);
  adapter->SetZeroHertzModeEnabled(
      FrameCadenceAdapterInterface::ZeroHertzModeParams{});
  adapter->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = 1});
  NtpTime original_ntp_time = time_controller.GetClock()->CurrentNtpTime();

  // Send one frame, expect 2 subsequent repeats.
  auto frame = CreateFrameWithTimestamps(&time_controller);
  int64_t original_timestamp_us = frame.timestamp_us();
  adapter->OnFrame(frame);

  EXPECT_CALL(callback, OnFrame)
      .WillOnce([&](Timestamp post_time, bool, const VideoFrame& frame) {
        EXPECT_EQ(post_time, time_controller.GetClock()->CurrentTime());
        EXPECT_EQ(frame.timestamp_us(), original_timestamp_us);
        EXPECT_EQ(frame.ntp_time_ms(), original_ntp_time.ToMs());
      });
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
  Mock::VerifyAndClearExpectations(&callback);

  EXPECT_CALL(callback, OnFrame)
      .WillOnce([&](Timestamp post_time, bool, const VideoFrame& frame) {
        EXPECT_EQ(post_time, time_controller.GetClock()->CurrentTime());
        EXPECT_EQ(frame.timestamp_us(),
                  original_timestamp_us + kNumMicrosecsPerSec);
        EXPECT_EQ(frame.ntp_time_ms(),
                  original_ntp_time.ToMs() + kNumMillisecsPerSec);
      });
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
  Mock::VerifyAndClearExpectations(&callback);

  EXPECT_CALL(callback, OnFrame)
      .WillOnce([&](Timestamp post_time, bool, const VideoFrame& frame) {
        EXPECT_EQ(post_time, time_controller.GetClock()->CurrentTime());
        EXPECT_EQ(frame.timestamp_us(),
                  original_timestamp_us + 2 * kNumMicrosecsPerSec);
        EXPECT_EQ(frame.ntp_time_ms(),
                  original_ntp_time.ToMs() + 2 * kNumMillisecsPerSec);
      });
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
}

TEST(FrameCadenceAdapterTest, SetsRepeatMode) {
  MockCallback callback;
  GlobalSimulatedTimeController time_controller(Timestamp::Millis(47892223));
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = CreateAdapter(no_field_trials, time_controller.GetClock());
  adapter->Initialize(&callback);
  adapter->SetZeroHertzModeEnabled(
      FrameCadenceAdapterInterface::ZeroHertzModeParams{.num_simulcast_layers =
                                                            1});
  adapter->UpdateLayerStatus(0, /*enabled=*/true);
  adapter->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = 1});

  // Send one frame, expect repeat_mode to be kNone.
  auto frame = CreateFrameWithTimestamps(&time_controller);
  adapter->OnFrame(frame);
  EXPECT_CALL(callback, OnFrame)
      .WillOnce([&](Timestamp, bool, const VideoFrame& frame) {
        EXPECT_EQ(frame.repeat_mode(), VideoFrame::RepeatMode::kNone);
        EXPECT_FALSE(frame.is_repeat_frame());
      });
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
  Mock::VerifyAndClearExpectations(&callback);

  // Expect the unconverged repeated frame to have kQualityConvergence.
  EXPECT_CALL(callback, OnFrame)
      .WillOnce([&](Timestamp, bool, const VideoFrame& frame) {
        EXPECT_EQ(frame.repeat_mode(),
                  VideoFrame::RepeatMode::kQualityConvergence);
        EXPECT_TRUE(frame.is_repeat_frame());
      });
  // The already scheduled repeat fires as a quality-convergence repeat.
  // Marking quality as converged before it fires makes it schedule the
  // following repeat as an idle keep-alive.
  adapter->UpdateLayerQualityConvergence(0, /*converged=*/true);
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
  Mock::VerifyAndClearExpectations(&callback);

  // Expect the converged idle repeated frame to have kKeepAlive.
  EXPECT_CALL(callback, OnFrame)
      .WillOnce([&](Timestamp, bool, const VideoFrame& frame) {
        EXPECT_EQ(frame.repeat_mode(), VideoFrame::RepeatMode::kKeepAlive);
        EXPECT_TRUE(frame.is_repeat_frame());
      });
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
  Mock::VerifyAndClearExpectations(&callback);

  // Send a new frame, expect repeat_mode to be kNone again.
  auto new_frame = CreateFrameWithTimestamps(&time_controller);
  adapter->OnFrame(new_frame);
  EXPECT_CALL(callback, OnFrame)
      .WillOnce([&](Timestamp, bool, const VideoFrame& frame) {
        EXPECT_EQ(frame.repeat_mode(), VideoFrame::RepeatMode::kNone);
        EXPECT_FALSE(frame.is_repeat_frame());
      });
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
}

TEST(FrameCadenceAdapterTest,
     RepeatsFramesWithoutTimestampsWithUnsetTimestamps) {
  // Logic in the frame cadence adapter avoids modifying frame NTP and render
  // timestamps if these timestamps looks unset, which is the case when the
  // clock is initialized running from 0. In this test we deliberately don't set
  // it to zero, but select unset timestamps in the frames (via CreateFrame())
  // and verify that the timestamp modifying logic doesn't depend on the current
  // time.
  MockCallback callback;
  GlobalSimulatedTimeController time_controller(Timestamp::Millis(4711));
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = CreateAdapter(no_field_trials, time_controller.GetClock());
  adapter->Initialize(&callback);
  adapter->SetZeroHertzModeEnabled(
      FrameCadenceAdapterInterface::ZeroHertzModeParams{});
  adapter->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = 1});

  // Send one frame, expect a repeat.
  adapter->OnFrame(CreateFrame());
  EXPECT_CALL(callback, OnFrame)
      .WillOnce([&](Timestamp post_time, bool, const VideoFrame& frame) {
        EXPECT_EQ(post_time, time_controller.GetClock()->CurrentTime());
        EXPECT_EQ(frame.timestamp_us(), 0);
        EXPECT_EQ(frame.ntp_time_ms(), 0);
      });
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
  Mock::VerifyAndClearExpectations(&callback);
  EXPECT_CALL(callback, OnFrame)
      .WillOnce([&](Timestamp post_time, bool, const VideoFrame& frame) {
        EXPECT_EQ(post_time, time_controller.GetClock()->CurrentTime());
        EXPECT_EQ(frame.timestamp_us(), 0);
        EXPECT_EQ(frame.ntp_time_ms(), 0);
      });
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
}

TEST(FrameCadenceAdapterTest, StopsRepeatingFramesDelayed) {
  // At 1s, the initially scheduled frame appears.
  // At 2s, the repeated initial frame appears.
  // At 2.5s, we schedule another new frame.
  // At 3.5s, we receive this frame.
  MockCallback callback;
  GlobalSimulatedTimeController time_controller(Timestamp::Zero());
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = CreateAdapter(no_field_trials, time_controller.GetClock());
  adapter->Initialize(&callback);
  adapter->SetZeroHertzModeEnabled(
      FrameCadenceAdapterInterface::ZeroHertzModeParams{});
  adapter->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = 1});
  NtpTime original_ntp_time = time_controller.GetClock()->CurrentNtpTime();

  // Send one frame, expect 1 subsequent repeat.
  adapter->OnFrame(CreateFrameWithTimestamps(&time_controller));
  EXPECT_CALL(callback, OnFrame).Times(2);
  time_controller.AdvanceTime(TimeDelta::Seconds(2.5));
  Mock::VerifyAndClearExpectations(&callback);

  // Send the new frame at 2.5s, which should appear after 3.5s.
  adapter->OnFrame(CreateFrameWithTimestamps(&time_controller));
  EXPECT_CALL(callback, OnFrame)
      .WillOnce([&](Timestamp, bool, const VideoFrame& frame) {
        EXPECT_EQ(frame.timestamp_us(), 5 * kNumMicrosecsPerSec / 2);
        EXPECT_EQ(frame.ntp_time_ms(),
                  original_ntp_time.ToMs() + 5u * kNumMillisecsPerSec / 2);
      });
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
}

TEST(FrameCadenceAdapterTest, RequestsRefreshFrameOnKeyFrameRequestWhenNew) {
  MockCallback callback;
  GlobalSimulatedTimeController time_controller(Timestamp::Zero());
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = CreateAdapter(no_field_trials, time_controller.GetClock());
  adapter->Initialize(&callback);
  adapter->SetZeroHertzModeEnabled(
      FrameCadenceAdapterInterface::ZeroHertzModeParams{});
  constexpr int kMaxFps = 10;
  adapter->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = kMaxFps});
  EXPECT_CALL(callback, RequestRefreshFrame);
  time_controller.AdvanceTime(
      TimeDelta::Seconds(1) *
      FrameCadenceAdapterInterface::kOnDiscardedFrameRefreshFramePeriod /
      kMaxFps);
  adapter->ProcessKeyFrameRequest();
}

TEST(FrameCadenceAdapterTest, IgnoresKeyFrameRequestShortlyAfterFrame) {
  MockCallback callback;
  GlobalSimulatedTimeController time_controller(Timestamp::Zero());
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = CreateAdapter(no_field_trials, time_controller.GetClock());
  adapter->Initialize(&callback);
  adapter->SetZeroHertzModeEnabled(
      FrameCadenceAdapterInterface::ZeroHertzModeParams{});
  adapter->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = 10});
  adapter->OnFrame(CreateFrame());
  time_controller.AdvanceTime(TimeDelta::Zero());
  EXPECT_CALL(callback, RequestRefreshFrame).Times(0);
  adapter->ProcessKeyFrameRequest();
}

TEST(FrameCadenceAdapterTest, RequestsRefreshFramesUntilArrival) {
  MockCallback callback;
  GlobalSimulatedTimeController time_controller(Timestamp::Zero());
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = CreateAdapter(no_field_trials, time_controller.GetClock());
  adapter->Initialize(&callback);
  adapter->SetZeroHertzModeEnabled(
      FrameCadenceAdapterInterface::ZeroHertzModeParams{});
  constexpr int kMaxFps = 10;
  adapter->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = kMaxFps});

  // We should see max_fps + 1 -
  // FrameCadenceAdapterInterface::kOnDiscardedFrameRefreshFramePeriod refresh
  // frame requests during the one second we wait until we send a single frame,
  // after which refresh frame requests should cease (we should see no such
  // requests during a second).
  EXPECT_CALL(callback, RequestRefreshFrame)
      .Times(kMaxFps + 1 -
             FrameCadenceAdapterInterface::kOnDiscardedFrameRefreshFramePeriod);
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
  Mock::VerifyAndClearExpectations(&callback);
  adapter->OnFrame(CreateFrame());
  EXPECT_CALL(callback, RequestRefreshFrame).Times(0);
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
}

TEST(FrameCadenceAdapterTest, RequestsRefreshAfterFrameDrop) {
  MockCallback callback;
  GlobalSimulatedTimeController time_controller(Timestamp::Zero());
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = CreateAdapter(no_field_trials, time_controller.GetClock());
  adapter->Initialize(&callback);
  adapter->SetZeroHertzModeEnabled(
      FrameCadenceAdapterInterface::ZeroHertzModeParams{});
  constexpr int kMaxFps = 10;
  adapter->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = kMaxFps});

  EXPECT_CALL(callback, RequestRefreshFrame).Times(0);

  // Send a frame through to cancel the initial delayed timer waiting for first
  // frame entry.
  adapter->OnFrame(CreateFrame());
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
  Mock::VerifyAndClearExpectations(&callback);

  // Send a dropped frame indication without any following frames received.
  // After FrameCadenceAdapterInterface::kOnDiscardedFrameRefreshFramePeriod
  // frame periods, we should receive a first refresh request.
  adapter->OnDiscardedFrame();
  EXPECT_CALL(callback, RequestRefreshFrame);
  time_controller.AdvanceTime(
      TimeDelta::Seconds(1) *
      FrameCadenceAdapterInterface::kOnDiscardedFrameRefreshFramePeriod /
      kMaxFps);
  Mock::VerifyAndClearExpectations(&callback);

  // We will now receive a refresh frame request for every frame period.
  EXPECT_CALL(callback, RequestRefreshFrame).Times(kMaxFps);
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
  Mock::VerifyAndClearExpectations(&callback);

  // After a frame is passed the requests will cease.
  EXPECT_CALL(callback, RequestRefreshFrame).Times(0);
  adapter->OnFrame(CreateFrame());
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
}

TEST(FrameCadenceAdapterTest, OmitsRefreshAfterFrameDropWithTimelyFrameEntry) {
  MockCallback callback;
  GlobalSimulatedTimeController time_controller(Timestamp::Zero());
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = CreateAdapter(no_field_trials, time_controller.GetClock());
  adapter->Initialize(&callback);
  adapter->SetZeroHertzModeEnabled(
      FrameCadenceAdapterInterface::ZeroHertzModeParams{});
  constexpr int kMaxFps = 10;
  adapter->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = kMaxFps});

  // Send a frame through to cancel the initial delayed timer waiting for first
  // frame entry.
  EXPECT_CALL(callback, RequestRefreshFrame).Times(0);
  adapter->OnFrame(CreateFrame());
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
  Mock::VerifyAndClearExpectations(&callback);

  // Send a frame drop indication. No refresh frames should be requested
  // until FrameCadenceAdapterInterface::kOnDiscardedFrameRefreshFramePeriod
  // intervals pass. Stop short of this.
  EXPECT_CALL(callback, RequestRefreshFrame).Times(0);
  adapter->OnDiscardedFrame();
  time_controller.AdvanceTime(
      TimeDelta::Seconds(1) *
          FrameCadenceAdapterInterface::kOnDiscardedFrameRefreshFramePeriod /
          kMaxFps -
      TimeDelta::Micros(1));
  Mock::VerifyAndClearExpectations(&callback);

  // Send a frame. The timer to request the refresh frame should be cancelled by
  // the reception, so no refreshes should be requested.
  EXPECT_CALL(callback, RequestRefreshFrame).Times(0);
  adapter->OnFrame(CreateFrame());
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
  Mock::VerifyAndClearExpectations(&callback);
}

TEST(FrameCadenceAdapterTest, AcceptsUnconfiguredLayerFeedback) {
  // This is a regression test for bugs.webrtc.org/14417.
  MockCallback callback;
  GlobalSimulatedTimeController time_controller(Timestamp::Zero());
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = CreateAdapter(no_field_trials, time_controller.GetClock());
  adapter->Initialize(&callback);
  adapter->SetZeroHertzModeEnabled(
      FrameCadenceAdapterInterface::ZeroHertzModeParams{.num_simulcast_layers =
                                                            1});
  constexpr int kMaxFps = 10;
  adapter->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = kMaxFps});
  time_controller.AdvanceTime(TimeDelta::Zero());

  adapter->UpdateLayerQualityConvergence(2, false);
  adapter->UpdateLayerStatus(2, false);
}

TEST(FrameCadenceAdapterTest, IgnoresDropInducedCallbacksPostDestruction) {
  auto callback = std::make_unique<MockCallback>();
  GlobalSimulatedTimeController time_controller(Timestamp::Zero());
  auto queue = time_controller.GetTaskQueueFactory()->CreateTaskQueue(
      "queue", TaskQueueFactory::Priority::kNormal);
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = FrameCadenceAdapterInterface::Create(
      time_controller.GetClock(), queue.get(), /*metronome=*/nullptr,
      /*worker_queue=*/nullptr, no_field_trials);
  queue->PostTask([&adapter, &callback] {
    adapter->Initialize(callback.get());
    adapter->SetZeroHertzModeEnabled(
        FrameCadenceAdapterInterface::ZeroHertzModeParams{});
  });
  time_controller.AdvanceTime(TimeDelta::Zero());
  constexpr int kMaxFps = 10;
  adapter->OnConstraintsChanged(
      VideoTrackSourceConstraints{.min_fps = 0, .max_fps = kMaxFps});
  adapter->OnDiscardedFrame();
  time_controller.AdvanceTime(TimeDelta::Zero());
  callback = nullptr;
  queue->PostTask([adapter = std::move(adapter)]() mutable {});
  time_controller.AdvanceTime(3 * TimeDelta::Seconds(1) / kMaxFps);
}

TEST(FrameCadenceAdapterTest, EncodeFramesAreAlignedWithMetronomeTick) {
  GlobalSimulatedTimeController time_controller(Timestamp::Zero());
  // Here the metronome interval is 33ms, because the metronome is not
  // infrequent then the encode tasks are aligned with the tick period.
  static constexpr TimeDelta kTickPeriod = TimeDelta::Millis(33);
  auto queue = time_controller.GetTaskQueueFactory()->CreateTaskQueue(
      "queue", TaskQueueFactory::Priority::kNormal);
  auto worker_queue = time_controller.GetTaskQueueFactory()->CreateTaskQueue(
      "work_queue", TaskQueueFactory::Priority::kNormal);
  test::FakeMetronome metronome(kTickPeriod);
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = FrameCadenceAdapterInterface::Create(
      time_controller.GetClock(), queue.get(), &metronome, worker_queue.get(),
      no_field_trials);
  MockCallback callback;
  adapter->Initialize(&callback);
  auto frame = CreateFrame();

  // `callback->OnFrame()` would not be called if only 32ms went by after
  // `adapter->OnFrame()`.
  EXPECT_CALL(callback, OnFrame(_, false, _)).Times(0);
  adapter->OnFrame(frame);
  time_controller.AdvanceTime(TimeDelta::Millis(32));
  Mock::VerifyAndClearExpectations(&callback);

  // `callback->OnFrame()` should be called if 33ms went by after
  // `adapter->OnFrame()`.
  EXPECT_CALL(callback, OnFrame(_, false, _)).Times(1);
  time_controller.AdvanceTime(TimeDelta::Millis(1));
  Mock::VerifyAndClearExpectations(&callback);

  // `callback->OnFrame()` would not be called if only 32ms went by after
  // `adapter->OnFrame()`.
  EXPECT_CALL(callback, OnFrame(_, false, _)).Times(0);
  // Send two frame before next tick.
  adapter->OnFrame(frame);
  adapter->OnFrame(frame);
  time_controller.AdvanceTime(TimeDelta::Millis(32));
  Mock::VerifyAndClearExpectations(&callback);

  // `callback->OnFrame()` should be called if 33ms went by after
  // `adapter->OnFrame()`.
  EXPECT_CALL(callback, OnFrame(_, false, _)).Times(2);
  time_controller.AdvanceTime(TimeDelta::Millis(1));
  Mock::VerifyAndClearExpectations(&callback);

  // Change the metronome tick period to 67ms (15Hz).
  metronome.SetTickPeriod(TimeDelta::Millis(67));
  // Expect the encode would happen immediately.
  EXPECT_CALL(callback, OnFrame(_, false, _)).Times(1);
  adapter->OnFrame(frame);
  time_controller.AdvanceTime(TimeDelta::Zero());
  Mock::VerifyAndClearExpectations(&callback);

  // Change the metronome tick period to 16ms (60Hz).
  metronome.SetTickPeriod(TimeDelta::Millis(16));
  // Expect the encode would not happen if only 15ms went by after
  // `adapter->OnFrame()`.
  EXPECT_CALL(callback, OnFrame(_, false, _)).Times(0);
  adapter->OnFrame(frame);
  time_controller.AdvanceTime(TimeDelta::Millis(15));
  Mock::VerifyAndClearExpectations(&callback);
  // `callback->OnFrame()` should be called if 16ms went by after
  // `adapter->OnFrame()`.
  EXPECT_CALL(callback, OnFrame(_, false, _)).Times(1);
  time_controller.AdvanceTime(TimeDelta::Millis(1));
  Mock::VerifyAndClearExpectations(&callback);

  Event finalized;
  queue->PostTask([&] {
    adapter = nullptr;
    finalized.Set();
  });
  finalized.Wait(Event::kForever);
}

TEST(FrameCadenceAdapterTest, ShutdownUnderMetronome) {
  // Regression test for crbug.com/356423094.
  // The current thread takes the role of worker queue.
  GlobalSimulatedTimeController time_controller(Timestamp::Zero());
  static constexpr TimeDelta kTickPeriod = TimeDelta::Millis(100);
  auto queue = time_controller.GetTaskQueueFactory()->CreateTaskQueue(
      "queue", TaskQueueFactory::Priority::kNormal);
  test::FakeMetronome metronome(kTickPeriod);
  FieldTrials no_field_trials = CreateTestFieldTrials();
  auto adapter = FrameCadenceAdapterInterface::Create(
      time_controller.GetClock(), queue.get(), &metronome,
      TaskQueueBase::Current(), no_field_trials);
  MockCallback callback;
  EXPECT_CALL(callback, OnFrame).Times(0);
  adapter->Initialize(&callback);

  // Pass a frame, this is expected to trigger an encode call in the future.
  adapter->OnFrame(CreateFrame());

  // Then post destruction of the adapter and destroy the encode queue from the
  // worker (i.e. current).
  SendTask(queue.get(), [&] { adapter = nullptr; });
  queue = nullptr;

  // Now that we advance time, there should be no encoding happening.
  time_controller.AdvanceTime(TimeDelta::Millis(100));
}

class FrameCadenceAdapterSimulcastLayersParamTest
    : public ::testing::TestWithParam<int> {
 public:
  static constexpr int kMaxFpsHz = 8;
  static constexpr TimeDelta kMinFrameDelay =
      TimeDelta::Millis(1000 / kMaxFpsHz);
  static constexpr TimeDelta kIdleFrameDelay =
      FrameCadenceAdapterInterface::kZeroHertzIdleRepeatRatePeriod;

  FrameCadenceAdapterSimulcastLayersParamTest() {
    adapter_->Initialize(&callback_);
    adapter_->OnConstraintsChanged(
        VideoTrackSourceConstraints{.min_fps = 0, .max_fps = kMaxFpsHz});
    time_controller_.AdvanceTime(TimeDelta::Zero());
    adapter_->SetZeroHertzModeEnabled(
        FrameCadenceAdapterInterface::ZeroHertzModeParams{});
    const size_t num_spatial_layers = GetParam();
    adapter_->SetZeroHertzModeEnabled(
        FrameCadenceAdapterInterface::ZeroHertzModeParams{num_spatial_layers});
  }

  int NumSpatialLayers() const { return GetParam(); }

 protected:
  FieldTrials no_field_trials_ = CreateTestFieldTrials();
  MockCallback callback_;
  GlobalSimulatedTimeController time_controller_{Timestamp::Zero()};
  const std::unique_ptr<FrameCadenceAdapterInterface> adapter_{
      CreateAdapter(no_field_trials_, time_controller_.GetClock())};
};

TEST_P(FrameCadenceAdapterSimulcastLayersParamTest,
       LayerReconfigurationResetsConvergenceInfo) {
  // Assumes layer reconfiguration has just happened.
  // Verify the state is unconverged.
  adapter_->OnFrame(CreateFrame());
  EXPECT_CALL(callback_, OnFrame).Times(kMaxFpsHz);
  time_controller_.AdvanceTime(kMaxFpsHz * kMinFrameDelay);
}

TEST_P(FrameCadenceAdapterSimulcastLayersParamTest,
       IgnoresKeyFrameRequestWhileShortRepeating) {
  // Plot:
  // 1. 0 * kMinFrameDelay: Start unconverged. Frame -> adapter.
  // 2. 1 * kMinFrameDelay: Frame -> callback.
  // 3. 2 * kMinFrameDelay: 1st short repeat.
  // Since we're unconverged we assume the process continues.
  adapter_->OnFrame(CreateFrame());
  time_controller_.AdvanceTime(2 * kMinFrameDelay);
  EXPECT_CALL(callback_, RequestRefreshFrame).Times(0);
  adapter_->ProcessKeyFrameRequest();

  // Expect short repeating as ususal.
  EXPECT_CALL(callback_, OnFrame).Times(8);
  time_controller_.AdvanceTime(8 * kMinFrameDelay);
}

TEST_P(FrameCadenceAdapterSimulcastLayersParamTest,
       IgnoresKeyFrameRequestJustBeforeIdleRepeating) {
  // (Only for > 0 spatial layers as we assume not converged with 0 layers)
  if (NumSpatialLayers() == 0)
    return;

  // Plot:
  // 1. 0 * kMinFrameDelay: Start converged. Frame -> adapter.
  // 2. 1 * kMinFrameDelay: Frame -> callback. New repeat scheduled at
  //    (kMaxFpsHz + 1) * kMinFrameDelay.
  // 3. kMaxFpsHz * kMinFrameDelay: Process keyframe.
  // 4. (kMaxFpsHz + N) * kMinFrameDelay (1 <= N <= kMaxFpsHz): Short repeats
  //    due to not converged.
  for (int i = 0; i != NumSpatialLayers(); i++) {
    adapter_->UpdateLayerStatus(i, /*enabled=*/true);
    adapter_->UpdateLayerQualityConvergence(i, /*converged=*/true);
  }
  adapter_->OnFrame(CreateFrame());
  time_controller_.AdvanceTime(kIdleFrameDelay);

  // We process the key frame request kMinFrameDelay before the first idle
  // repeat should happen. The resulting repeats should happen spaced by
  // kMinFrameDelay before we get new convergence info.
  EXPECT_CALL(callback_, RequestRefreshFrame).Times(0);
  adapter_->ProcessKeyFrameRequest();
  EXPECT_CALL(callback_, OnFrame).Times(kMaxFpsHz);
  time_controller_.AdvanceTime(kMaxFpsHz * kMinFrameDelay);
}

TEST_P(FrameCadenceAdapterSimulcastLayersParamTest,
       IgnoresKeyFrameRequestShortRepeatsBeforeIdleRepeat) {
  // (Only for > 0 spatial layers as we assume not converged with 0 layers)
  if (NumSpatialLayers() == 0)
    return;
  // Plot:
  // 1. 0 * kMinFrameDelay: Start converged. Frame -> adapter.
  // 2. 1 * kMinFrameDelay: Frame -> callback. New repeat scheduled at
  //    (kMaxFpsHz + 1) * kMinFrameDelay.
  // 3. 2 * kMinFrameDelay: Process keyframe.
  // 4. (2 + N) * kMinFrameDelay (1 <= N <= kMaxFpsHz): Short repeats due to not
  //    converged.
  for (int i = 0; i != NumSpatialLayers(); i++) {
    adapter_->UpdateLayerStatus(i, /*enabled=*/true);
    adapter_->UpdateLayerQualityConvergence(i, /*converged=*/true);
  }
  adapter_->OnFrame(CreateFrame());
  time_controller_.AdvanceTime(2 * kMinFrameDelay);

  // We process the key frame request (kMaxFpsHz - 1) * kMinFrameDelay before
  // the first idle repeat should happen. The resulting repeats should happen
  // spaced kMinFrameDelay before we get new convergence info.
  EXPECT_CALL(callback_, RequestRefreshFrame).Times(0);
  adapter_->ProcessKeyFrameRequest();
  EXPECT_CALL(callback_, OnFrame).Times(kMaxFpsHz);
  time_controller_.AdvanceTime(kMaxFpsHz * kMinFrameDelay);
}

INSTANTIATE_TEST_SUITE_P(,
                         FrameCadenceAdapterSimulcastLayersParamTest,
                         Values(0, 1, 2));

class ZeroHertzLayerQualityConvergenceTest : public ::testing::Test {
 public:
  static constexpr TimeDelta kMinFrameDelay = TimeDelta::Millis(100);
  static constexpr TimeDelta kIdleFrameDelay =
      FrameCadenceAdapterInterface::kZeroHertzIdleRepeatRatePeriod;
  // Restricts non-idle repeat rate to 5 fps (default is 10 fps);
  static constexpr int kRestrictedMaxFps = 5;

  ZeroHertzLayerQualityConvergenceTest() {
    adapter_->Initialize(&callback_);
    adapter_->SetZeroHertzModeEnabled(
        FrameCadenceAdapterInterface::ZeroHertzModeParams{
            /*num_simulcast_layers=*/2});
    adapter_->OnConstraintsChanged(VideoTrackSourceConstraints{
        .min_fps = 0, .max_fps = TimeDelta::Seconds(1) / kMinFrameDelay});
    time_controller_.AdvanceTime(TimeDelta::Zero());
  }

  void PassFrame() { adapter_->OnFrame(CreateFrame()); }

  void ExpectFrameEntriesAtDelaysFromNow(
      std::initializer_list<TimeDelta> list) {
    Timestamp origin = time_controller_.GetClock()->CurrentTime();
    for (auto delay : list) {
      EXPECT_CALL(callback_, OnFrame(origin + delay, false, _));
      time_controller_.AdvanceTime(origin + delay -
                                   time_controller_.GetClock()->CurrentTime());
    }
  }

  void ScheduleDelayed(TimeDelta delay, absl::AnyInvocable<void() &&> task) {
    TaskQueueBase::Current()->PostDelayedTask(std::move(task), delay);
  }

 protected:
  FieldTrials no_field_trials_ = CreateTestFieldTrials();
  MockCallback callback_;
  GlobalSimulatedTimeController time_controller_{Timestamp::Zero()};
  std::unique_ptr<FrameCadenceAdapterInterface> adapter_{
      CreateAdapter(no_field_trials_, time_controller_.GetClock())};
};

TEST_F(ZeroHertzLayerQualityConvergenceTest, InitialStateUnconverged) {
  // As the layer count is just configured, assume we start out as unconverged.
  PassFrame();
  ExpectFrameEntriesAtDelaysFromNow({
      1 * kMinFrameDelay,  // Original frame emitted
      2 * kMinFrameDelay,  // Short repeats.
      3 * kMinFrameDelay,  // ...
  });
}

TEST_F(ZeroHertzLayerQualityConvergenceTest, UnconvergedAfterLayersEnabled) {
  // With newly enabled layers we assume quality is unconverged.
  adapter_->UpdateLayerStatus(0, /*enabled=*/true);
  adapter_->UpdateLayerStatus(1, /*enabled=*/true);
  PassFrame();
  ExpectFrameEntriesAtDelaysFromNow({
      kMinFrameDelay,      // Original frame emitted
      2 * kMinFrameDelay,  // Unconverged repeats.
      3 * kMinFrameDelay,  // ...
  });
}

TEST_F(ZeroHertzLayerQualityConvergenceTest,
       RepeatsPassedFramesUntilConvergence) {
  ScheduleDelayed(TimeDelta::Zero(), [&] {
    adapter_->UpdateLayerStatus(0, /*enabled=*/true);
    adapter_->UpdateLayerStatus(1, /*enabled=*/true);
    PassFrame();
  });
  ScheduleDelayed(2.5 * kMinFrameDelay, [&] {
    adapter_->UpdateLayerQualityConvergence(/*spatial_index=*/1, true);
  });
  ScheduleDelayed(3.5 * kMinFrameDelay, [&] {
    adapter_->UpdateLayerQualityConvergence(/*spatial_index=*/0, true);
  });
  ScheduleDelayed(8 * kMinFrameDelay, [&] { PassFrame(); });
  ScheduleDelayed(9.5 * kMinFrameDelay, [&] {
    adapter_->UpdateLayerQualityConvergence(/*spatial_index=*/0, true);
  });
  ScheduleDelayed(10.5 * kMinFrameDelay, [&] {
    adapter_->UpdateLayerQualityConvergence(/*spatial_index=*/1, true);
  });
  ExpectFrameEntriesAtDelaysFromNow({
      kMinFrameDelay,      // Original frame emitted
      2 * kMinFrameDelay,  // Repeat from kMinFrameDelay.

      // 2.5 * kMinFrameDelay: Converged in layer 1, layer 0 still unconverged.
      3 * kMinFrameDelay,  // Repeat from 2 * kMinFrameDelay.

      // 3.5 * kMinFrameDelay: Converged in layer 0 as well.
      4 * kMinFrameDelay,  // Repeat from 3 * kMinFrameDelay. An idle repeat is
                           // scheduled for kIdleFrameDelay + 3 *
                           // kMinFrameDelay.

      // A new frame is passed at 8 * kMinFrameDelay.
      9 * kMinFrameDelay,  // Original frame emitted

      // 9.5 * kMinFrameDelay: Converged in layer 0, layer 1 still unconverged.
      10 * kMinFrameDelay,  // Repeat from 9 * kMinFrameDelay.
      // 10.5 * kMinFrameDelay: Converged in layer 0 as well.
      11 * kMinFrameDelay,                        // Idle repeats from 1000.
      11 * kMinFrameDelay + kIdleFrameDelay,      // ...
      11 * kMinFrameDelay + 2 * kIdleFrameDelay,  // ...
                                                  // ...
  });
}

TEST_F(ZeroHertzLayerQualityConvergenceTest,
       UnconvergedRepeatRateAdaptsDownWhenRestricted) {
  PassFrame();
  ScheduleDelayed(1.5 * kMinFrameDelay, [&] {
    adapter_->UpdateVideoSourceRestrictions(kRestrictedMaxFps);
  });
  ExpectFrameEntriesAtDelaysFromNow({
      1 * kMinFrameDelay,  // Original frame emitted at non-restricted rate.

      // 1.5 * kMinFrameDelay: restricts max fps to 5 fps which should result
      // in a new non-idle repeat delay of 2 * kMinFrameDelay.
      2 * kMinFrameDelay,  // Unconverged repeat at non-restricted rate.
      4 * kMinFrameDelay,  // Unconverged repeats at restricted rate. This
                           // happens 2 * kMinFrameDelay after the last frame.
      6 * kMinFrameDelay,  // ...
  });
}

TEST_F(ZeroHertzLayerQualityConvergenceTest,
       UnconvergedRepeatRateAdaptsUpWhenGoingFromRestrictedToUnrestricted) {
  PassFrame();
  ScheduleDelayed(1.5 * kMinFrameDelay, [&] {
    adapter_->UpdateVideoSourceRestrictions(kRestrictedMaxFps);
  });
  ScheduleDelayed(5.5 * kMinFrameDelay, [&] {
    adapter_->UpdateVideoSourceRestrictions(std::nullopt);
  });
  ExpectFrameEntriesAtDelaysFromNow({
      1 * kMinFrameDelay,  // Original frame emitted at non-restricted rate.

      // 1.5 * kMinFrameDelay: restricts max fps to 5 fps which should result
      // in a new non-idle repeat delay of 2 * kMinFrameDelay.
      2 * kMinFrameDelay,  // Unconverged repeat at non-restricted rate.
      4 * kMinFrameDelay,  // Unconverged repeat at restricted rate.

      // 5.5 * kMinFrameDelay: removes frame-rate restriction and we should
      // then go back to 10 fps as unconverged repeat rate.
      6 * kMinFrameDelay,  // Last unconverged repeat at restricted rate.
      7 * kMinFrameDelay,  // Back to unconverged repeat at non-restricted rate.
      8 * kMinFrameDelay,  // We are now unrestricted.
      9 * kMinFrameDelay,  // ...
  });
}

TEST_F(ZeroHertzLayerQualityConvergenceTest,
       UnconvergedRepeatRateMaintainsRestrictionOnReconfigureToHigherMaxFps) {
  PassFrame();
  ScheduleDelayed(1.5 * kMinFrameDelay, [&] {
    adapter_->UpdateVideoSourceRestrictions(kRestrictedMaxFps);
  });
  ScheduleDelayed(2.5 * kMinFrameDelay, [&] {
    adapter_->OnConstraintsChanged(VideoTrackSourceConstraints{
        .min_fps = 0, .max_fps = 2 * TimeDelta::Seconds(1) / kMinFrameDelay});
  });
  ScheduleDelayed(3 * kMinFrameDelay, [&] { PassFrame(); });
  ScheduleDelayed(8 * kMinFrameDelay, [&] {
    adapter_->OnConstraintsChanged(VideoTrackSourceConstraints{
        .min_fps = 0, .max_fps = 0.2 * TimeDelta::Seconds(1) / kMinFrameDelay});
  });
  ScheduleDelayed(9 * kMinFrameDelay, [&] { PassFrame(); });
  ExpectFrameEntriesAtDelaysFromNow({
      1 * kMinFrameDelay,  // Original frame emitted at non-restricted rate.

      // 1.5 * kMinFrameDelay: restricts max fps to 5 fps which should result
      // in a new non-idle repeat delay of 2 * kMinFrameDelay.
      2 * kMinFrameDelay,  // Unconverged repeat at non-restricted rate.

      // 2.5 * kMinFrameDelay: new constraint asks for max rate of 20 fps.
      // The 0Hz adapter is reconstructed for 20 fps but inherits the current
      // restriction for rate of non-converged frames of 5 fps.

      // A new frame is passed at 3 * kMinFrameDelay. The previous repeat
      // cadence was stopped by the change in constraints.
      3.5 * kMinFrameDelay,  // Original frame emitted at non-restricted 20 fps.
                             // The delay is 0.5 * kMinFrameDelay.
      5.5 * kMinFrameDelay,  // Unconverged repeat at restricted rate.
                             // The delay is 2 * kMinFrameDelay when restricted.
      7.5 * kMinFrameDelay,  // ...

      // 8 * kMinFrameDelay: new constraint asks for max rate of 2 fps.
      // The 0Hz adapter is reconstructed for 2 fps and will therefore not obey
      // the current restriction for rate of non-converged frames of 5 fps
      // since the new max rate is lower.

      // A new frame is passed at 9 * kMinFrameDelay. The previous repeat
      // cadence was stopped by the change in constraints.
      14 * kMinFrameDelay,  // Original frame emitted at non-restricted 2 fps.
                            // The delay is 5 * kMinFrameDelay.
      19 * kMinFrameDelay,  // Unconverged repeat at non-restricted rate.
      24 * kMinFrameDelay,  // ...
  });
}

class FrameCadenceAdapterMetricsTest : public ::testing::Test {
 public:
  FrameCadenceAdapterMetricsTest() : time_controller_(Timestamp::Millis(1)) {
    metrics::Reset();
  }
  void DepleteTaskQueues() { time_controller_.AdvanceTime(TimeDelta::Zero()); }

 protected:
  GlobalSimulatedTimeController time_controller_;
};

TEST(FrameCadenceAdapterRealTimeTest, TimestampsDoNotDrift) {
  // This regression test must be performed in realtime because of limitations
  // in GlobalSimulatedTimeController.
  //
  // We sleep for a long while in OnFrame when a repeat was scheduled which
  // should reflect in accordingly increased ntp_time_ms() and timestamp_us() in
  // the repeated frames.
  auto factory = CreateDefaultTaskQueueFactory();
  auto queue =
      factory->CreateTaskQueue("test", TaskQueueFactory::Priority::kNormal);
  MockCallback callback;
  Clock* clock = Clock::GetRealTimeClock();
  std::unique_ptr<FrameCadenceAdapterInterface> adapter;
  int frame_counter = 0;
  int64_t original_ntp_time_ms;
  int64_t original_timestamp_us;
  Event event;
  FieldTrials no_field_trials = CreateTestFieldTrials();
  queue->PostTask([&] {
    adapter = CreateAdapter(no_field_trials, clock);
    adapter->Initialize(&callback);
    adapter->SetZeroHertzModeEnabled(
        FrameCadenceAdapterInterface::ZeroHertzModeParams{});
    adapter->OnConstraintsChanged(
        VideoTrackSourceConstraints{.min_fps = 0, .max_fps = 30});
    auto frame = CreateFrame();
    original_ntp_time_ms = clock->CurrentNtpInMilliseconds();
    frame.set_ntp_time_ms(original_ntp_time_ms);
    original_timestamp_us = clock->CurrentTime().us();
    frame.set_timestamp_us(original_timestamp_us);
    constexpr int kSleepMs = kNumMillisecsPerSec / 2;
    EXPECT_CALL(callback, OnFrame)
        .WillRepeatedly(
            [&](Timestamp, bool, const VideoFrame& incoming_frame) {
              ++frame_counter;
              // Avoid the first OnFrame and sleep on the second.
              if (frame_counter == 2) {
                Thread::SleepMs(kSleepMs);
              } else if (frame_counter == 3) {
                EXPECT_GE(incoming_frame.ntp_time_ms(),
                          original_ntp_time_ms + kSleepMs);
                EXPECT_GE(incoming_frame.timestamp_us(),
                          original_timestamp_us + kSleepMs);
                event.Set();
              }
            });
    adapter->OnFrame(frame);
  });
  event.Wait(Event::kForever);
  Event finalized;
  queue->PostTask([&] {
    adapter = nullptr;
    finalized.Set();
  });
  finalized.Wait(Event::kForever);
}

TEST(FrameCadenceAdapterRealTimeTest, ScheduledRepeatAllowsForSlowEncode) {
  // This regression test must be performed in realtime because of limitations
  // in GlobalSimulatedTimeController.
  //
  // We sleep for a long while (but less than max fps) in the first repeated
  // OnFrame (frame 2). This should not lead to a belated second repeated
  // OnFrame (frame 3).
  auto factory = CreateDefaultTaskQueueFactory();
  auto queue =
      factory->CreateTaskQueue("test", TaskQueueFactory::Priority::kNormal);
  MockCallback callback;
  Clock* clock = Clock::GetRealTimeClock();
  std::unique_ptr<FrameCadenceAdapterInterface> adapter;
  int frame_counter = 0;
  Event event;
  std::optional<Timestamp> start_time;
  FieldTrials no_field_trials = CreateTestFieldTrials();
  queue->PostTask([&] {
    adapter = CreateAdapter(no_field_trials, clock);
    adapter->Initialize(&callback);
    adapter->SetZeroHertzModeEnabled(
        FrameCadenceAdapterInterface::ZeroHertzModeParams{});
    adapter->OnConstraintsChanged(
        VideoTrackSourceConstraints{.min_fps = 0, .max_fps = 2});
    auto frame = CreateFrame();
    constexpr int kSleepMs = 400;
    constexpr TimeDelta kAllowedBelate = TimeDelta::Millis(151);
    EXPECT_CALL(callback, OnFrame).WillRepeatedly([&, kAllowedBelate] {
      ++frame_counter;
      // Avoid the first OnFrame and sleep on the second.
      if (frame_counter == 2) {
        start_time = clock->CurrentTime();
        Thread::SleepMs(kSleepMs);
      } else if (frame_counter == 3) {
        TimeDelta diff =
            clock->CurrentTime() - (*start_time + TimeDelta::Millis(500));
        RTC_LOG(LS_ERROR) << "Difference in when frame should vs is appearing: "
                          << diff;
        EXPECT_LT(diff, kAllowedBelate);
        event.Set();
      }
    });
    adapter->OnFrame(frame);
  });
  event.Wait(Event::kForever);
  Event finalized;
  queue->PostTask([&] {
    adapter = nullptr;
    finalized.Set();
  });
  finalized.Wait(Event::kForever);
}
}  // namespace
}  // namespace webrtc
