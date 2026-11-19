/*
 *  Copyright (c) 2025 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VIDEO_TIMING_SIMULATOR_DECODABILITY_SIMULATOR_H_
#define VIDEO_TIMING_SIMULATOR_DECODABILITY_SIMULATOR_H_

#include <cstdint>
#include <set>
#include <span>
#include <vector>

#include "absl/algorithm/container.h"
#include "api/numerics/samples_stats_counter.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "logging/rtc_event_log/rtc_event_log_parser.h"
#include "rtc_base/checks.h"
#include "video/timing/simulator/frame_base.h"
#include "video/timing/simulator/results_base.h"
#include "video/timing/simulator/stream_base.h"

namespace webrtc::video_timing_simulator {

// The `DecodabilitySimulator` takes an `ParsedRtcEventLog` and produces a
// sequence of metadata about decodable frames that were contained in the log.
class DecodabilitySimulator {
 public:
  struct Config {
    // If non-empty, will only simulate video streams whose main SSRCs is
    // contained in the set.
    std::set<uint32_t> ssrc_filter = {};
  };

  // Metadata about a single decodable frame.
  struct Frame : public FrameBase<Frame> {
    // -- Values --
    // Frame information.
    int num_packets = -1;              // Required.
    DataSize size = DataSize::Zero();  // Required.

    // RTP header information.
    int64_t unwrapped_rtp_timestamp = -1;  // Required.

    // Frame timestamps.
    Timestamp assembled_timestamp = Timestamp::PlusInfinity();  // Required.
    Timestamp decodable_timestamp = Timestamp::PlusInfinity();

    // -- Populated values --
    // One-way delay relative some baseline.
    TimeDelta frame_delay_variation = TimeDelta::PlusInfinity();

    // -- Value accessors --
    Timestamp ArrivalTimestampInternal() const { return decodable_timestamp; }

    // -- Per-frame metrics --
    // Time spent waiting for reference frames to arrive.
    TimeDelta UndecodableDuration() const {
      RTC_DCHECK(assembled_timestamp.IsFinite());
      return decodable_timestamp - assembled_timestamp;
    }
  };

  // All frames in one stream.
  struct Stream : public StreamBase<Stream, Frame> {
    Timestamp creation_timestamp = Timestamp::PlusInfinity();
    uint32_t ssrc = 0;
    std::vector<Frame> frames;

    // -- Per-stream metrics --

    // Total number of decodable frames.
    int NumDecodableFrames() const {
      return CountFiniteTimestamps(&Frame::decodable_timestamp);
    }

    // Samples of undecodable durations in ms.
    SamplesStatsCounter UndecodableDurationMs() const {
      return BuildSamplesMs(&Frame::UndecodableDuration);
    }
  };

  // All streams.
  struct Results : public ResultsBase<Results> {
    std::vector<Stream> streams;
  };

  explicit DecodabilitySimulator(Config config);
  ~DecodabilitySimulator();

  DecodabilitySimulator(const DecodabilitySimulator&) = delete;
  DecodabilitySimulator& operator=(const DecodabilitySimulator&) = delete;

  Results Simulate(const ParsedRtcEventLog& parsed_log) const;

 private:
  const Config config_;
};

// -- Comparators and sorting --
inline bool DecodableOrder(const DecodabilitySimulator::Frame& a,
                           const DecodabilitySimulator::Frame& b) {
  return a.decodable_timestamp < b.decodable_timestamp;
}
inline void SortByDecodableOrder(
    std::span<DecodabilitySimulator::Frame> frames) {
  absl::c_stable_sort(frames, DecodableOrder);
}

// -- Inter-frame metrics --
// Difference in decodable time between two frames.
inline TimeDelta InterDecodableTime(const DecodabilitySimulator::Frame& cur,
                                    const DecodabilitySimulator::Frame& prev) {
  if (!cur.decodable_timestamp.IsFinite() &&
      !prev.decodable_timestamp.IsFinite()) {
    return TimeDelta::PlusInfinity();
  }
  return cur.decodable_timestamp - prev.decodable_timestamp;
}

}  // namespace webrtc::video_timing_simulator

#endif  // VIDEO_TIMING_SIMULATOR_DECODABILITY_SIMULATOR_H_
