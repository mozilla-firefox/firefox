/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VIDEO_RTP_STREAMS_SYNCHRONIZER2_H_
#define VIDEO_RTP_STREAMS_SYNCHRONIZER2_H_

#include <cstdint>
#include <memory>

#include "api/environment/environment.h"
#include "api/task_queue/task_queue_base.h"
#include "rtc_base/task_utils/repeating_task.h"
#include "rtc_base/thread_annotations.h"
#include "video/stream_synchronization.h"

namespace webrtc {

class Syncable;

namespace internal {

// RtpStreamsSynchronizer is responsible for synchronizing audio and video for
// a given audio receive stream and video receive stream.
class RtpStreamsSynchronizer {
 public:
  RtpStreamsSynchronizer(const Environment& env,
                         TaskQueueBase* network_queue,
                         Syncable* syncable_video);
  ~RtpStreamsSynchronizer();

  void ConfigureSync(Syncable* syncable_audio);

  // Gets the estimated playout NTP timestamp for the video frame with
  // `rtp_timestamp` and the sync offset between the current played out audio
  // frame and the video frame. Returns true on success, false otherwise.
  // The `estimated_freq_khz` is the frequency used in the RTP to NTP timestamp
  // conversion.
  bool GetStreamSyncOffsetInMs(uint32_t rtp_timestamp,
                               int64_t render_time_ms,
                               int64_t* video_playout_ntp_ms,
                               int64_t* stream_offset_ms,
                               double* estimated_freq_khz) const;

 private:
  void UpdateDelay();

  const Environment env_;
  TaskQueueBase* const network_queue_;
  Syncable* const syncable_video_;

  Syncable* syncable_audio_ RTC_GUARDED_BY(network_queue_) = nullptr;
  std::unique_ptr<StreamSynchronization> sync_ RTC_GUARDED_BY(network_queue_);
  StreamSynchronization::Measurements audio_measurement_
      RTC_GUARDED_BY(network_queue_);
  StreamSynchronization::Measurements video_measurement_
      RTC_GUARDED_BY(network_queue_);
  RepeatingTaskHandle repeating_task_ RTC_GUARDED_BY(network_queue_);
  int64_t last_stats_log_ms_ RTC_GUARDED_BY(network_queue_);
};

}  // namespace internal
}  // namespace webrtc

#endif  // VIDEO_RTP_STREAMS_SYNCHRONIZER2_H_
