/*
 *  Copyright (c) 2025 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VIDEO_TIMING_SIMULATOR_RENDERING_SIMULATOR_H_
#define VIDEO_TIMING_SIMULATOR_RENDERING_SIMULATOR_H_

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "absl/algorithm/container.h"
#include "api/numerics/samples_stats_counter.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/timing/video_jitter_timing_factory.h"
#include "logging/rtc_event_log/rtc_event_log_parser.h"
#include "rtc_base/checks.h"
#include "video/timing/simulator/frame_base.h"
#include "video/timing/simulator/results_base.h"
#include "video/timing/simulator/stream_base.h"

namespace webrtc::video_timing_simulator {

// The `RenderingSimulator` takes an `ParsedRtcEventLog` and produces a
// sequence of metadata about decoded and rendered frames that were contained in
// the log.
class RenderingSimulator {
 public:
  struct Config {
    std::string name = "";
    std::string field_trials_string = "";
    const VideoJitterTimingFactory* video_jitter_timing_factory = nullptr;

    // If non-empty, will only simulate video streams whose main SSRCs is
    // contained in the set.
    std::set<uint32_t> ssrc_filter = {};
  };

  // Metadata about a single rendered frame.
  struct Frame : public FrameBase<Frame> {
    // -- Values --
    // Frame information.
    int num_packets = -1;              // Required.
    DataSize size = DataSize::Zero();  // Required.

    // RTP header information.
    int payload_type = -1;
    uint32_t rtp_timestamp = 0;
    int64_t unwrapped_rtp_timestamp = -1;  // Required.

    // Dependency descriptor information.
    int64_t frame_id = -1;
    int spatial_id = -1;
    int temporal_id = -1;
    int num_references = -1;

    // Packet timestamps. Both are required.
    Timestamp first_packet_arrival_timestamp = Timestamp::PlusInfinity();
    Timestamp last_packet_arrival_timestamp = Timestamp::MinusInfinity();

    // Frame timestamps.
    Timestamp assembled_timestamp = Timestamp::PlusInfinity();  // Required.
    Timestamp render_timestamp = Timestamp::PlusInfinity();
    Timestamp decoded_timestamp = Timestamp::PlusInfinity();
    Timestamp rendered_timestamp = Timestamp::PlusInfinity();

    // Jitter buffer state at the time of this frame.
    int frames_dropped = 0;
    // TODO: b/423646186 - Add `current_delay_ms`.
    // The `jitter_buffer_*` metrics below are recorded by the production code,
    // and should be compatible with the `webrtc-stats` definitions. One major
    // difference is that they are _not_ cumulative.
    // https://www.w3.org/TR/webrtc-stats/#dom-rtcinboundrtpstreamstats-jitterbufferminimumdelay
    TimeDelta jitter_buffer_minimum_delay = TimeDelta::PlusInfinity();
    // https://www.w3.org/TR/webrtc-stats/#dom-rtcinboundrtpstreamstats-jitterbuffertargetdelay
    TimeDelta jitter_buffer_target_delay = TimeDelta::PlusInfinity();
    // https://www.w3.org/TR/webrtc-stats/#dom-rtcinboundrtpstreamstats-jitterbufferdelay
    TimeDelta jitter_buffer_delay = TimeDelta::PlusInfinity();

    // -- Populated values --
    // One-way delay relative some baseline.
    TimeDelta frame_delay_variation = TimeDelta::PlusInfinity();

    // -- Value accessors --
    Timestamp ArrivalTimestampInternal() const { return rendered_timestamp; }

    // -- Per-frame metrics --
    // Time spent being assembled (waiting for all packets to arrive).
    TimeDelta PacketBufferDuration() const {
      RTC_DCHECK(assembled_timestamp.IsFinite());
      RTC_DCHECK(first_packet_arrival_timestamp.IsFinite());
      return assembled_timestamp - first_packet_arrival_timestamp;
    }

    // Time spent waiting to be decoded, after assembly. (This includes a,
    // currently, zero decode duration.) Note that this is similar to
    // `jitter_buffer_delay`, except that the latter is 1) recorded by the
    // production code; and, 2) is anchored on `first_packet_arrival_timestamp`
    // rather than `assembled_timestamp`.
    TimeDelta FrameBufferDuration() const {
      RTC_DCHECK(assembled_timestamp.IsFinite());
      return decoded_timestamp - assembled_timestamp;
    }

    // Time spent waiting to be rendered, after decode.
    TimeDelta RenderBufferDuration() const {
      if (!decoded_timestamp.IsFinite()) {
        RTC_DCHECK(!rendered_timestamp.IsFinite());
        return TimeDelta::PlusInfinity();
      }
      if (!rendered_timestamp.IsFinite()) {
        return TimeDelta::PlusInfinity();
      }
      return rendered_timestamp - decoded_timestamp;
    }

    // Total duration in all three buffers: from first packet to rendered.
    TimeDelta TotalBufferDuration() const {
      TimeDelta total_duration = PacketBufferDuration() +
                                 FrameBufferDuration() + RenderBufferDuration();
      RTC_DCHECK_EQ(total_duration,
                    rendered_timestamp - first_packet_arrival_timestamp);
      return total_duration;
    }

    // Pre-buffer margin between render timestamp (target) and
    // assembled timestamp (actual arrival):
    //   * A frame that is assembled early w.r.t. the target (<=> arriving
    //     in time from the network) has a positive margin.
    //   * A frame that is assembled on time w.r.t. the target (<=> arriving
    //     slightly late from the network) has zero margin.
    //   * A frame that is assembled late w.r.t. the target (<=> arriving
    //     very late from the network) has negative margin.
    // Positive margins mean no video freezes, at the cost of receiver delay.
    // A jitter buffer needs to strike a balance between video freezes and
    // delay. In terms of margin, that means low positive margin values, a
    // couple of frames with zero margin, and very few frames with
    // negative margin.
    // TODO: b/423646186 - Change this to be `DecodabilityMargin`.
    //
    // Note: calculations are done at the ms-level, since `render_timestamp`
    // only has ms-level precision.
    TimeDelta AssembledMargin() const {
      if (!render_timestamp.IsFinite()) {
        return TimeDelta::PlusInfinity();
      }
      RTC_DCHECK(assembled_timestamp.IsFinite());
      // Subtract `kRenderDelay`, since that is what `VCMTiming` and
      // `VideoRenderFrames` also do.
      int64_t margin_ms =
          render_timestamp.ms() - kRenderDelay.ms() - assembled_timestamp.ms();
      return TimeDelta::Millis(margin_ms);
    }
    std::optional<bool> AssembledInTime() const {
      TimeDelta assembled_margin = AssembledMargin();
      if (!assembled_margin.IsFinite()) {
        return std::nullopt;
      }
      return assembled_margin >= TimeDelta::Zero();
    }
    std::optional<bool> AssembledLate() const {
      std::optional<bool> assembled_in_time = AssembledInTime();
      if (!assembled_in_time.has_value()) {
        return std::nullopt;
      }
      return !assembled_in_time.value();
    }

    // Split the assembled margin along zero: "excess margin" for positive
    // margins and "deficit margin" for negative margins. A jitter buffer would
    // generally want to minimize the number of frames with a deficit margin
    // (delayed frames/buffer underruns => video freezes), while also minimizing
    // the stream-level min/p10 of the excess margin (early frames spending a
    // long time in the buffer => high latency).
    // Frames with `render_timestamp` unset are excluded from both.
    std::optional<TimeDelta> AssembledMarginExcess() const {
      std::optional<bool> assembled_in_time = AssembledInTime();
      if (!assembled_in_time.has_value()) {
        return std::nullopt;
      }
      return *assembled_in_time ? std::optional<TimeDelta>(AssembledMargin())
                                : std::nullopt;
    }
    std::optional<TimeDelta> AssembledMarginDeficit() const {
      std::optional<bool> assembled_late = AssembledLate();
      if (!assembled_late.has_value()) {
        return std::nullopt;
      }
      return *assembled_late ? std::optional<TimeDelta>(AssembledMargin())
                             : std::nullopt;
    }

    // Post-buffer margin between render timestamp (target) and
    // rendered timestamp (actual render):
    //   * Frames should not be rendered early, if the timing works well.
    //   * A frame that is rendered on time w.r.t. the target has zero margin.
    //   * A frame that is rendered late w.r.t. the target has negative margin.
    // Frames with `render_timestamp` or `rendered_timestamp` unset are excluded
    // from all.
    //
    // Note: calculations are done at the ms-level, since `render_timestamp`
    // only has ms-level precision.
    TimeDelta RenderedMargin() const {
      if (!render_timestamp.IsFinite()) {
        RTC_DCHECK(!rendered_timestamp.IsFinite());
        return TimeDelta::PlusInfinity();
      }
      if (!rendered_timestamp.IsFinite()) {
        return TimeDelta::PlusInfinity();
      }
      // Subtract `kRenderDelay`, since that is what `VCMTiming` and
      // `VideoRenderFrames` also do.
      int64_t margin_ms =
          render_timestamp.ms() - kRenderDelay.ms() - rendered_timestamp.ms();
      return TimeDelta::Millis(margin_ms);
    }
    std::optional<bool> RenderedInTime() const {
      TimeDelta rendered_margin = RenderedMargin();
      if (!rendered_margin.IsFinite()) {
        return std::nullopt;
      }
      return rendered_margin >= TimeDelta::Zero();
    }
    std::optional<bool> RenderedLate() const {
      std::optional<bool> rendered_in_time = RenderedInTime();
      if (!rendered_in_time.has_value()) {
        return std::nullopt;
      }
      return !rendered_in_time.value();
    }

    // A well-functioning jitter buffer would have very few (or non) frames with
    // a render margin excess. Render margin deficits can happen though.
    // Frames with `render_timestamp` or `rendered_timestamp` unset are excluded
    // from both.
    std::optional<TimeDelta> RenderedMarginExcess() const {
      std::optional<bool> rendered_in_time = RenderedInTime();
      if (!rendered_in_time.has_value()) {
        return std::nullopt;
      }
      return *rendered_in_time ? std::optional<TimeDelta>(RenderedMargin())
                               : std::nullopt;
    }
    std::optional<TimeDelta> RenderedMarginDeficit() const {
      std::optional<bool> rendered_late = RenderedLate();
      if (!rendered_late.has_value()) {
        return std::nullopt;
      }
      return *rendered_late ? std::optional<TimeDelta>(RenderedMargin())
                            : std::nullopt;
    }
  };

  // All frames in one stream.
  struct Stream : public StreamBase<Stream, Frame> {
    Timestamp creation_timestamp = Timestamp::PlusInfinity();
    uint32_t ssrc = 0;
    std::vector<Frame> frames;

    // -- Per-stream metrics --

    // Total number of frames that were assembled in time or late.
    int NumAssembledInTimeFrames() const {
      return CountSetAndTrue(&Frame::AssembledInTime);
    }
    int NumAssembledLateFrames() const {
      return CountSetAndTrue(&Frame::AssembledLate);
    }

    // Total number of decoded frames.
    int NumDecodedFrames() const {
      return CountFiniteTimestamps(&Frame::decoded_timestamp);
    }

    // Total number of rendered frames.
    int NumRenderedFrames() const {
      return CountFiniteTimestamps(&Frame::rendered_timestamp);
    }

    // Total number of frames that were rendered in time or late.
    int NumRenderedInTimeFrames() const {
      return CountSetAndTrue(&Frame::RenderedInTime);
    }
    int NumRenderedLateFrames() const {
      return CountSetAndTrue(&Frame::RenderedLate);
    }

    // Total number of dropped frames in the decoder.
    int NumDecoderDroppedFrames() const {
      return SumNonNegativeIntField(&Frame::frames_dropped);
    }

    // Samples of per-frame delay variation metrics. Not `const` because they
    // sort `frames`. Defined in `.cc` for circular include order reasons.
    SamplesStatsCounter InterRenderTimeMs();
    SamplesStatsCounter InterDecodedTimeMs();
    SamplesStatsCounter InterRenderedTimeMs();

    // Samples of webrtc-stats values in ms.
    SamplesStatsCounter JitterBufferMinimumDelayMs() const {
      return BuildSamplesMs(&Frame::jitter_buffer_minimum_delay);
    }
    SamplesStatsCounter JitterBufferDelayMs() const {
      return BuildSamplesMs(&Frame::jitter_buffer_delay);
    }

    // Samples of buffer durations in ms.
    SamplesStatsCounter PacketBufferDurationMs() const {
      return BuildSamplesMs(&Frame::PacketBufferDuration);
    }
    SamplesStatsCounter FrameBufferDurationMs() const {
      return BuildSamplesMs(&Frame::FrameBufferDuration);
    }
    SamplesStatsCounter RenderBufferDurationMs() const {
      return BuildSamplesMs(&Frame::RenderBufferDuration);
    }
    SamplesStatsCounter TotalBufferDurationMs() const {
      return BuildSamplesMs(&Frame::TotalBufferDuration);
    }

    // Samples of assembled margin in ms.
    SamplesStatsCounter AssembledMarginMs() const {
      return BuildSamplesMs(&Frame::AssembledMargin);
    }
    SamplesStatsCounter AssembledMarginExcessMs() const {
      return BuildSamplesMs(&Frame::AssembledMarginExcess);
    }
    SamplesStatsCounter AssembledMarginDeficitMs() const {
      return BuildSamplesMs(&Frame::AssembledMarginDeficit);
    }

    // Samples of render margin in ms.
    SamplesStatsCounter RenderedMarginMs() const {
      return BuildSamplesMs(&Frame::RenderedMargin);
    }
    SamplesStatsCounter RenderedMarginExcessMs() const {
      return BuildSamplesMs(&Frame::RenderedMarginExcess);
    }
    SamplesStatsCounter RenderedMarginDeficitMs() const {
      return BuildSamplesMs(&Frame::RenderedMarginDeficit);
    }
  };

  // All streams.
  struct Results : public ResultsBase<Results> {
    std::string config_name;
    std::vector<Stream> streams;
  };

  // Static configuration.

  // The "render delay" that is passed through the timing component and
  // render buffer. It is added and subtracted through the pipeline, so it is
  // important to have it set.
  static constexpr TimeDelta kRenderDelay = TimeDelta::Millis(10);

  explicit RenderingSimulator(Config config);
  ~RenderingSimulator();

  RenderingSimulator(const RenderingSimulator&) = delete;
  RenderingSimulator& operator=(const RenderingSimulator&) = delete;

  Results Simulate(const ParsedRtcEventLog& parsed_log) const;

 private:
  const Config config_;
};

// -- Comparators and sorting --
inline bool RenderOrder(const RenderingSimulator::Frame& a,
                        const RenderingSimulator::Frame& b) {
  return a.render_timestamp < b.render_timestamp;
}
inline void SortByRenderOrder(std::span<RenderingSimulator::Frame> frames) {
  absl::c_stable_sort(frames, RenderOrder);
}

inline bool DecodedOrder(const RenderingSimulator::Frame& a,
                         const RenderingSimulator::Frame& b) {
  return a.decoded_timestamp < b.decoded_timestamp;
}
inline void SortByDecodedOrder(std::span<RenderingSimulator::Frame> frames) {
  absl::c_stable_sort(frames, DecodedOrder);
}

inline bool RenderedOrder(const RenderingSimulator::Frame& a,
                          const RenderingSimulator::Frame& b) {
  return a.rendered_timestamp < b.rendered_timestamp;
}
inline void SortByRenderedOrder(std::span<RenderingSimulator::Frame> frames) {
  absl::c_stable_sort(frames, RenderedOrder);
}

// -- Inter-frame metrics --
// Difference in render time (target) between two frames.
inline TimeDelta InterRenderTime(const RenderingSimulator::Frame& cur,
                                 const RenderingSimulator::Frame& prev) {
  if (!cur.render_timestamp.IsFinite() && !prev.render_timestamp.IsFinite()) {
    return TimeDelta::PlusInfinity();
  }
  return cur.render_timestamp - prev.render_timestamp;
}

// Difference in decoded time (actual) between two frames.
inline TimeDelta InterDecodedTime(const RenderingSimulator::Frame& cur,
                                  const RenderingSimulator::Frame& prev) {
  if (!cur.decoded_timestamp.IsFinite() && !prev.decoded_timestamp.IsFinite()) {
    return TimeDelta::PlusInfinity();
  }
  return cur.decoded_timestamp - prev.decoded_timestamp;
}

// Difference in rendered time (actual) between two frames.
inline TimeDelta InterRenderedTime(const RenderingSimulator::Frame& cur,
                                   const RenderingSimulator::Frame& prev) {
  if (!cur.rendered_timestamp.IsFinite() &&
      !prev.rendered_timestamp.IsFinite()) {
    return TimeDelta::PlusInfinity();
  }
  return cur.rendered_timestamp - prev.rendered_timestamp;
}

}  // namespace webrtc::video_timing_simulator

#endif  // VIDEO_TIMING_SIMULATOR_RENDERING_SIMULATOR_H_
