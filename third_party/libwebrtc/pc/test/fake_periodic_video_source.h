/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_TEST_FAKE_PERIODIC_VIDEO_SOURCE_H_
#define PC_TEST_FAKE_PERIODIC_VIDEO_SOURCE_H_

#include <memory>

#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_adapter.h"
#include "api/video/video_broadcaster.h"
#include "api/video/video_frame.h"
#include "api/video/video_rotation.h"
#include "api/video/video_sink_interface.h"
#include "api/video/video_source_interface.h"
#include "media/base/fake_frame_source.h"
#include "rtc_base/checks.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/task_queue_for_test.h"
#include "rtc_base/task_utils/repeating_task.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/time_utils.h"

namespace webrtc {

class FakePeriodicVideoSource final : public VideoSourceInterface<VideoFrame> {
 public:
  static constexpr int kDefaultWidth = 640;
  static constexpr int kDefaultHeight = 480;

  struct Config {
    int width = kDefaultWidth;
    int height = kDefaultHeight;
    TimeDelta frame_interval = TimeDelta::Millis(33);
    VideoRotation rotation = kVideoRotation_0;
    Timestamp timestamp_offset = Timestamp::Zero();
  };

  FakePeriodicVideoSource() : FakePeriodicVideoSource(Config()) {}
  explicit FakePeriodicVideoSource(Config config)
      : frame_source_(config.width,
                      config.height,
                      config.frame_interval,
                      config.timestamp_offset),
        task_queue_(std::make_unique<TaskQueueForTest>(
            "FakePeriodicVideoTrackSource")) {
    frame_source_.SetRotation(config.rotation);

    TimeDelta frame_interval = config.frame_interval;
    repeating_task_handle_ =
        RepeatingTaskHandle::Start(task_queue_->Get(), [this, frame_interval] {
          VideoFrame frame = broadcaster_.wants().rotation_applied
                                 ? frame_source_.GetFrameRotationApplied()
                                 : frame_source_.GetFrame();
          int cropped_width = 0;
          int cropped_height = 0;
          int out_width = 0;
          int out_height = 0;
          if (video_adapter_.AdaptFrameResolution(
                  frame.width(), frame.height(),
                  frame.timestamp_us() * kNumNanosecsPerMicrosec,
                  &cropped_width, &cropped_height, &out_width, &out_height)) {
            if (out_height != frame.height() || out_width != frame.width()) {
              scoped_refptr<I420Buffer> scaled_buffer =
                  I420Buffer::Create(out_width, out_height);
              scaled_buffer->ScaleFrom(*frame.video_frame_buffer()->ToI420());
              frame = VideoFrame::Builder()
                          .set_video_frame_buffer(scaled_buffer)
                          .set_rotation(frame.rotation())
                          .set_timestamp_us(frame.timestamp_us())
                          .set_id(frame.id())
                          .build();
            }
            broadcaster_.OnFrame(frame);
          } else {
            broadcaster_.OnDiscardedFrame();
          }
          return frame_interval;
        });
  }

  VideoSinkWants wants() const {
    MutexLock lock(&mutex_);
    return wants_;
  }

  void RemoveSink(VideoSinkInterface<VideoFrame>* sink) override {
    RTC_DCHECK(thread_checker_.IsCurrent());
    broadcaster_.RemoveSink(sink);
    video_adapter_.OnSinkWants(broadcaster_.wants());
  }

  void AddOrUpdateSink(VideoSinkInterface<VideoFrame>* sink,
                       const VideoSinkWants& wants) override {
    RTC_DCHECK(thread_checker_.IsCurrent());
    {
      MutexLock lock(&mutex_);
      wants_ = wants;
    }
    broadcaster_.AddOrUpdateSink(sink, wants);
    video_adapter_.OnSinkWants(broadcaster_.wants());
  }

  void Stop() {
    RTC_DCHECK(task_queue_);
    task_queue_->SendTask([&]() { repeating_task_handle_.Stop(); });
    task_queue_.reset();
  }

 private:
  SequenceChecker thread_checker_{SequenceChecker::kDetached};

  VideoBroadcaster broadcaster_;
  FakeFrameSource frame_source_;
  VideoAdapter video_adapter_;
  mutable Mutex mutex_;
  VideoSinkWants wants_ RTC_GUARDED_BY(&mutex_);

  std::unique_ptr<TaskQueueForTest> task_queue_;
  RepeatingTaskHandle repeating_task_handle_;
};

}  // namespace webrtc

#endif  // PC_TEST_FAKE_PERIODIC_VIDEO_SOURCE_H_
