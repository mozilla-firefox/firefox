/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/pc/e2e/analyzer/video/default_video_quality_analyzer.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "api/array_view.h"
#include "api/numerics/samples_stats_counter.h"
#include "api/test/metrics/metric.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/video_frame.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/strings/string_builder.h"
#include "system_wrappers/include/clock.h"
#include "test/pc/e2e/analyzer/video/default_video_quality_analyzer_frame_in_flight.h"
#include "test/pc/e2e/analyzer/video/default_video_quality_analyzer_frames_comparator.h"
#include "test/pc/e2e/analyzer/video/default_video_quality_analyzer_internal_shared_objects.h"
#include "test/pc/e2e/analyzer/video/default_video_quality_analyzer_shared_objects.h"
#include "test/pc/e2e/analyzer/video/default_video_quality_analyzer_stream_state.h"
#include "test/pc/e2e/metric_metadata_keys.h"

namespace webrtc {
namespace {

using ::webrtc::test::ImprovementDirection;
using ::webrtc::test::Unit;
using ::webrtc::webrtc_pc_e2e::MetricMetadataKey;

constexpr int kBitsInByte = 8;
constexpr absl::string_view kSkipRenderedFrameReasonProcessed = "processed";
constexpr absl::string_view kSkipRenderedFrameReasonRendered = "rendered";
constexpr absl::string_view kSkipRenderedFrameReasonDropped =
    "considered dropped";

void LogFrameCounters(const std::string& name, const FrameCounters& counters) {
  RTC_LOG(LS_INFO) << "[" << name
                   << "] Captured         : " << counters.captured;
  RTC_LOG(LS_INFO) << "[" << name
                   << "] Pre encoded      : " << counters.pre_encoded;
  RTC_LOG(LS_INFO) << "[" << name
                   << "] Encoded          : " << counters.encoded;
  RTC_LOG(LS_INFO) << "[" << name
                   << "] Received         : " << counters.received;
  RTC_LOG(LS_INFO) << "[" << name
                   << "] Decoded          : " << counters.decoded;
  RTC_LOG(LS_INFO) << "[" << name
                   << "] Rendered         : " << counters.rendered;
  RTC_LOG(LS_INFO) << "[" << name
                   << "] Dropped          : " << counters.dropped;
  RTC_LOG(LS_INFO) << "[" << name
                   << "] Failed to decode : " << counters.failed_to_decode;
}

void LogStreamInternalStats(const std::string& name,
                            const StreamStats& stats,
                            Timestamp start_time) {
  for (const auto& entry : stats.dropped_by_phase) {
    RTC_LOG(LS_INFO) << "[" << name << "] Dropped at " << ToString(entry.first)
                     << ": " << entry.second;
  }
  Timestamp first_encoded_frame_time = Timestamp::PlusInfinity();
  for (const StreamCodecInfo& encoder : stats.encoders) {
    RTC_DCHECK(encoder.switched_on_at.IsFinite());
    RTC_DCHECK(encoder.switched_from_at.IsFinite());
    if (first_encoded_frame_time.IsInfinite()) {
      first_encoded_frame_time = encoder.switched_on_at;
    }
    RTC_LOG(LS_INFO)
        << "[" << name << "] Used encoder: \"" << encoder.codec_name
        << "\" used from (frame_id=" << encoder.first_frame_id
        << "; from_stream_start="
        << (encoder.switched_on_at - stats.stream_started_time).ms()
        << "ms, from_call_start=" << (encoder.switched_on_at - start_time).ms()
        << "ms) until (frame_id=" << encoder.last_frame_id
        << "; from_stream_start="
        << (encoder.switched_from_at - stats.stream_started_time).ms()
        << "ms, from_call_start="
        << (encoder.switched_from_at - start_time).ms() << "ms)";
  }
  for (const StreamCodecInfo& decoder : stats.decoders) {
    RTC_DCHECK(decoder.switched_on_at.IsFinite());
    RTC_DCHECK(decoder.switched_from_at.IsFinite());
    RTC_LOG(LS_INFO)
        << "[" << name << "] Used decoder: \"" << decoder.codec_name
        << "\" used from (frame_id=" << decoder.first_frame_id
        << "; from_stream_start="
        << (decoder.switched_on_at - stats.stream_started_time).ms()
        << "ms, from_call_start=" << (decoder.switched_on_at - start_time).ms()
        << "ms) until (frame_id=" << decoder.last_frame_id
        << "; from_stream_start="
        << (decoder.switched_from_at - stats.stream_started_time).ms()
        << "ms, from_call_start="
        << (decoder.switched_from_at - start_time).ms() << "ms)";
  }
}

template <typename T>
absl::optional<T> MaybeGetValue(const std::map<size_t, T>& map, size_t key) {
  auto it = map.find(key);
  if (it == map.end()) {
    return absl::nullopt;
  }
  return it->second;
}

SamplesStatsCounter::StatsSample StatsSample(double value,
                                             Timestamp sampling_time) {
  return SamplesStatsCounter::StatsSample{value, sampling_time};
}

}  // namespace

DefaultVideoQualityAnalyzer::DefaultVideoQualityAnalyzer(
    webrtc::Clock* clock,
    test::MetricsLogger* metrics_logger,
    DefaultVideoQualityAnalyzerOptions options)
    : options_(options),
      clock_(clock),
      metrics_logger_(metrics_logger),
      frames_storage_(options.max_frames_storage_duration, clock_),
      frames_comparator_(clock, cpu_measurer_, options) {
  RTC_CHECK(metrics_logger_);
}

DefaultVideoQualityAnalyzer::~DefaultVideoQualityAnalyzer() {
  Stop();
}

void DefaultVideoQualityAnalyzer::Start(
    std::string test_case_name,
    rtc::ArrayView<const std::string> peer_names,
    int max_threads_count) {
  test_label_ = std::move(test_case_name);
  frames_comparator_.Start(max_threads_count);
  {
    MutexLock lock(&mutex_);
    peers_ = std::make_unique<NamesCollection>(peer_names);
    RTC_CHECK(start_time_.IsMinusInfinity());

    RTC_CHECK_EQ(state_, State::kNew)
        << "DefaultVideoQualityAnalyzer is already started";
    state_ = State::kActive;
    start_time_ = Now();
  }
}

uint16_t DefaultVideoQualityAnalyzer::OnFrameCaptured(
    absl::string_view peer_name,
    const std::string& stream_label,
    const webrtc::VideoFrame& frame) {
  // `next_frame_id` is atomic, so we needn't lock here.
  Timestamp captured_time = Now();
  Timestamp start_time = Timestamp::MinusInfinity();
  size_t peer_index = -1;
  size_t peers_count = -1;
  size_t stream_index;
  uint16_t frame_id = VideoFrame::kNotSetId;
  {
    MutexLock lock(&mutex_);
    frame_id = GetNextFrameId();
    RTC_CHECK_EQ(state_, State::kActive)
        << "DefaultVideoQualityAnalyzer has to be started before use";
    // Create a local copy of `start_time_`, peer's index and total peers count
    // to access it without holding a `mutex_` during access to
    // `frames_comparator_`.
    start_time = start_time_;
    peer_index = peers_->index(peer_name);
    peers_count = peers_->size();
    stream_index = streams_.AddIfAbsent(stream_label);
  }
  // Ensure stats for this stream exists.
  frames_comparator_.EnsureStatsForStream(stream_index, peer_index, peers_count,
                                          captured_time, start_time);
  {
    MutexLock lock(&mutex_);
    stream_to_sender_[stream_index] = peer_index;
    frame_counters_.captured++;
    for (size_t i : peers_->GetAllIndexes()) {
      if (i != peer_index || options_.enable_receive_own_stream) {
        InternalStatsKey key(stream_index, peer_index, i);
        stream_frame_counters_[key].captured++;
      }
    }

    std::set<size_t> frame_receivers_indexes = peers_->GetPresentIndexes();
    if (!options_.enable_receive_own_stream) {
      frame_receivers_indexes.erase(peer_index);
    }

    auto state_it = stream_states_.find(stream_index);
    if (state_it == stream_states_.end()) {
      stream_states_.emplace(stream_index,
                             StreamState(peer_index, frame_receivers_indexes,
                                         captured_time, clock_));
    }
    StreamState* state = &stream_states_.at(stream_index);
    state->PushBack(frame_id);
    absl::optional<TimeDelta> time_between_captured_frames = absl::nullopt;
    if (state->last_captured_frame_time().has_value()) {
      time_between_captured_frames =
          captured_time - *state->last_captured_frame_time();
    }
    state->SetLastCapturedFrameTime(captured_time);
    // Update frames in flight info.
    auto it = captured_frames_in_flight_.find(frame_id);
    if (it != captured_frames_in_flight_.end()) {
      // If we overflow uint16_t and hit previous frame id and this frame is
      // still in flight, it means that this stream wasn't rendered for long
      // time and we need to process existing frame as dropped.
      for (size_t i : peers_->GetPresentIndexes()) {
        if (i == peer_index && !options_.enable_receive_own_stream) {
          continue;
        }

        uint16_t oldest_frame_id = state->PopFront(i);
        RTC_DCHECK_EQ(frame_id, oldest_frame_id);

        if (state->GetPausableState(i)->IsPaused()) {
          continue;
        }

        frame_counters_.dropped++;
        InternalStatsKey key(stream_index, peer_index, i);
        stream_frame_counters_.at(key).dropped++;

        analyzer_stats_.frames_in_flight_left_count.AddSample(
            StatsSample(captured_frames_in_flight_.size(), Now()));
        frames_comparator_.AddComparison(
            InternalStatsKey(stream_index, peer_index, i),
            /*captured=*/absl::nullopt,
            /*rendered=*/absl::nullopt, FrameComparisonType::kDroppedFrame,
            it->second.GetStatsForPeer(i));
      }

      frames_storage_.Remove(it->second.id());
      captured_frames_in_flight_.erase(it);
    }
    captured_frames_in_flight_.emplace(
        frame_id, FrameInFlight(stream_index, frame_id, captured_time,
                                time_between_captured_frames,
                                std::move(frame_receivers_indexes)));
    // Store local copy of the frame with frame_id set.
    VideoFrame local_frame(frame);
    local_frame.set_id(frame_id);
    frames_storage_.Add(std::move(local_frame), captured_time);

    // Update history stream<->frame mapping
    for (auto it = stream_to_frame_id_history_.begin();
         it != stream_to_frame_id_history_.end(); ++it) {
      it->second.erase(frame_id);
    }
    stream_to_frame_id_history_[stream_index].insert(frame_id);
    stream_to_frame_id_full_history_[stream_index].push_back(frame_id);

    if (options_.report_infra_metrics) {
      analyzer_stats_.on_frame_captured_processing_time_ms.AddSample(
          (Now() - captured_time).ms<double>());
    }
  }
  return frame_id;
}

void DefaultVideoQualityAnalyzer::OnFramePreEncode(
    absl::string_view peer_name,
    const webrtc::VideoFrame& frame) {
  Timestamp processing_started = Now();
  MutexLock lock(&mutex_);
  RTC_CHECK_EQ(state_, State::kActive)
      << "DefaultVideoQualityAnalyzer has to be started before use";

  auto it = captured_frames_in_flight_.find(frame.id());
  if (it == captured_frames_in_flight_.end()) {
    // If the frame is not found, it is possible that it has been encoded twice
    // and that it was received by all the participants the first time.
    RTC_LOG(LS_WARNING) << "Frame id=" << frame.id() << " not found.";
    return;
  }
  FrameInFlight& frame_in_flight = it->second;
  frame_counters_.pre_encoded++;
  size_t peer_index = peers_->index(peer_name);
  for (size_t i : peers_->GetAllIndexes()) {
    if (i != peer_index || options_.enable_receive_own_stream) {
      InternalStatsKey key(frame_in_flight.stream(), peer_index, i);
      stream_frame_counters_.at(key).pre_encoded++;
    }
  }
  frame_in_flight.SetPreEncodeTime(Now());

  if (options_.report_infra_metrics) {
    analyzer_stats_.on_frame_pre_encode_processing_time_ms.AddSample(
        (Now() - processing_started).ms<double>());
  }
}

void DefaultVideoQualityAnalyzer::OnFrameEncoded(
    absl::string_view peer_name,
    uint16_t frame_id,
    const webrtc::EncodedImage& encoded_image,
    const EncoderStats& stats,
    bool discarded) {
  if (discarded)
    return;

  Timestamp processing_started = Now();
  MutexLock lock(&mutex_);
  RTC_CHECK_EQ(state_, State::kActive)
      << "DefaultVideoQualityAnalyzer has to be started before use";

  auto it = captured_frames_in_flight_.find(frame_id);
  if (it == captured_frames_in_flight_.end()) {
    RTC_LOG(LS_WARNING)
        << "The encoding of video frame with id [" << frame_id << "] for peer ["
        << peer_name << "] finished after all receivers rendered this frame or "
        << "were removed. It can be OK for simulcast/SVC if higher quality "
        << "stream is not required or the last receiver was unregistered "
        << "between encoding of different layers, but it may indicate an ERROR "
        << "for singlecast or if it happens often.";
    return;
  }
  FrameInFlight& frame_in_flight = it->second;
  // For SVC we can receive multiple encoded images for one frame, so to cover
  // all cases we have to pick the last encode time.
  if (!frame_in_flight.HasEncodedTime()) {
    // Increase counters only when we meet this frame first time.
    frame_counters_.encoded++;
    size_t peer_index = peers_->index(peer_name);
    for (size_t i : peers_->GetAllIndexes()) {
      if (i != peer_index || options_.enable_receive_own_stream) {
        InternalStatsKey key(frame_in_flight.stream(), peer_index, i);
        stream_frame_counters_.at(key).encoded++;
      }
    }
  }
  Timestamp now = Now();
  StreamState& state = stream_states_.at(frame_in_flight.stream());
  absl::optional<TimeDelta> time_between_encoded_frames = absl::nullopt;
  if (state.last_encoded_frame_time().has_value()) {
    time_between_encoded_frames = now - *state.last_encoded_frame_time();
  }
  state.SetLastEncodedFrameTime(now);

  StreamCodecInfo used_encoder;
  used_encoder.codec_name = stats.encoder_name;
  used_encoder.first_frame_id = frame_id;
  used_encoder.last_frame_id = frame_id;
  used_encoder.switched_on_at = now;
  used_encoder.switched_from_at = now;
  // We could either have simulcast layers or spatial layers.
  // TODO(https://crbug.com/webrtc/14891): If we want to support a mix of
  // simulcast and SVC we'll also need to consider the case where we have both
  // simulcast and spatial indices.
  size_t stream_index = encoded_image.SpatialIndex().value_or(
      encoded_image.SimulcastIndex().value_or(0));
  frame_in_flight.OnFrameEncoded(
      now, time_between_encoded_frames, encoded_image._frameType,
      DataSize::Bytes(encoded_image.size()), stats.target_encode_bitrate,
      stream_index, stats.qp, used_encoder);

  if (options_.report_infra_metrics) {
    analyzer_stats_.on_frame_encoded_processing_time_ms.AddSample(
        (Now() - processing_started).ms<double>());
  }
}

void DefaultVideoQualityAnalyzer::OnFrameDropped(
    absl::string_view peer_name,
    webrtc::EncodedImageCallback::DropReason reason) {
  // Here we do nothing, because we will see this drop on renderer side.
}

void DefaultVideoQualityAnalyzer::OnFramePreDecode(
    absl::string_view peer_name,
    uint16_t frame_id,
    const webrtc::EncodedImage& input_image) {
  Timestamp processing_started = Now();
  MutexLock lock(&mutex_);
  RTC_CHECK_EQ(state_, State::kActive)
      << "DefaultVideoQualityAnalyzer has to be started before use";

  size_t peer_index = peers_->index(peer_name);

  if (frame_id == VideoFrame::kNotSetId) {
    frame_counters_.received++;
    unknown_sender_frame_counters_[std::string(peer_name)].received++;
    return;
  }

  auto it = captured_frames_in_flight_.find(frame_id);
  if (it == captured_frames_in_flight_.end() ||
      it->second.HasReceivedTime(peer_index)) {
    // It means this frame was predecoded before, so we can skip it. It may
    // happen when we have multiple simulcast streams in one track and received
    // the same picture from two different streams because SFU can't reliably
    // correlate two simulcast streams and started relaying the second stream
    // from the same frame it has relayed right before for the first stream.
    return;
  }

  frame_counters_.received++;
  InternalStatsKey key(it->second.stream(),
                       stream_to_sender_.at(it->second.stream()), peer_index);
  stream_frame_counters_.at(key).received++;
  // Determine the time of the last received packet of this video frame.
  RTC_DCHECK(!input_image.PacketInfos().empty());
  Timestamp last_receive_time =
      std::max_element(input_image.PacketInfos().cbegin(),
                       input_image.PacketInfos().cend(),
                       [](const RtpPacketInfo& a, const RtpPacketInfo& b) {
                         return a.receive_time() < b.receive_time();
                       })
          ->receive_time();
  it->second.OnFramePreDecode(peer_index,
                              /*received_time=*/last_receive_time,
                              /*decode_start_time=*/Now(),
                              input_image._frameType,
                              DataSize::Bytes(input_image.size()));

  if (options_.report_infra_metrics) {
    analyzer_stats_.on_frame_pre_decode_processing_time_ms.AddSample(
        (Now() - processing_started).ms<double>());
  }
}

void DefaultVideoQualityAnalyzer::OnFrameDecoded(
    absl::string_view peer_name,
    const webrtc::VideoFrame& frame,
    const DecoderStats& stats) {
  Timestamp processing_started = Now();
  MutexLock lock(&mutex_);
  RTC_CHECK_EQ(state_, State::kActive)
      << "DefaultVideoQualityAnalyzer has to be started before use";

  size_t peer_index = peers_->index(peer_name);

  if (frame.id() == VideoFrame::kNotSetId) {
    frame_counters_.decoded++;
    unknown_sender_frame_counters_[std::string(peer_name)].decoded++;
    return;
  }

  auto it = captured_frames_in_flight_.find(frame.id());
  if (it == captured_frames_in_flight_.end() ||
      it->second.HasDecodeEndTime(peer_index)) {
    // It means this frame was decoded before, so we can skip it. It may happen
    // when we have multiple simulcast streams in one track and received
    // the same frame from two different streams because SFU can't reliably
    // correlate two simulcast streams and started relaying the second stream
    // from the same frame it has relayed right before for the first stream.
    return;
  }
  frame_counters_.decoded++;
  InternalStatsKey key(it->second.stream(),
                       stream_to_sender_.at(it->second.stream()), peer_index);
  stream_frame_counters_.at(key).decoded++;
  Timestamp now = Now();
  StreamCodecInfo used_decoder;
  used_decoder.codec_name = stats.decoder_name;
  used_decoder.first_frame_id = frame.id();
  used_decoder.last_frame_id = frame.id();
  used_decoder.switched_on_at = now;
  used_decoder.switched_from_at = now;
  it->second.OnFrameDecoded(peer_index, now, frame.width(), frame.height(),
                            used_decoder);

  if (options_.report_infra_metrics) {
    analyzer_stats_.on_frame_decoded_processing_time_ms.AddSample(
        (Now() - processing_started).ms<double>());
  }
}

void DefaultVideoQualityAnalyzer::OnFrameRendered(
    absl::string_view peer_name,
    const webrtc::VideoFrame& frame) {
  Timestamp processing_started = Now();
  MutexLock lock(&mutex_);
  RTC_CHECK_EQ(state_, State::kActive)
      << "DefaultVideoQualityAnalyzer has to be started before use";

  size_t peer_index = peers_->index(peer_name);

  if (frame.id() == VideoFrame::kNotSetId) {
    frame_counters_.rendered++;
    unknown_sender_frame_counters_[std::string(peer_name)].rendered++;
    return;
  }

  auto frame_it = captured_frames_in_flight_.find(frame.id());
  if (frame_it == captured_frames_in_flight_.end() ||
      frame_it->second.HasRenderedTime(peer_index) ||
      frame_it->second.IsDropped(peer_index)) {
    // It means this frame was rendered or dropped before, so we can skip it.
    // It may happen when we have multiple simulcast streams in one track and
    // received the same frame from two different streams because SFU can't
    // reliably correlate two simulcast streams and started relaying the second
    // stream from the same frame it has relayed right before for the first
    // stream.
    absl::string_view reason = kSkipRenderedFrameReasonProcessed;
    if (frame_it != captured_frames_in_flight_.end()) {
      if (frame_it->second.HasRenderedTime(peer_index)) {
        reason = kSkipRenderedFrameReasonRendered;
      } else if (frame_it->second.IsDropped(peer_index)) {
        reason = kSkipRenderedFrameReasonDropped;
      }
    }
    RTC_LOG(LS_WARNING)
        << "Peer " << peer_name
        << "; Received frame out of order: received frame with id "
        << frame.id() << " which was " << reason << " before";
    return;
  }

  // Find corresponding captured frame.
  FrameInFlight* frame_in_flight = &frame_it->second;
  absl::optional<VideoFrame> captured_frame = frames_storage_.Get(frame.id());

  const size_t stream_index = frame_in_flight->stream();
  StreamState* state = &stream_states_.at(stream_index);
  const InternalStatsKey stats_key(stream_index, state->sender(), peer_index);

  // Update frames counters.
  frame_counters_.rendered++;
  stream_frame_counters_.at(stats_key).rendered++;

  // Update current frame stats.
  frame_in_flight->OnFrameRendered(peer_index, Now());

  // After we received frame here we need to check if there are any dropped
  // frames between this one and last one, that was rendered for this video
  // stream.
  int dropped_count = ProcessNotSeenFramesBeforeRendered(peer_index, frame.id(),
                                                         stats_key, *state);
  RTC_DCHECK(!state->IsEmpty(peer_index));
  state->PopFront(peer_index);

  if (state->last_rendered_frame_time(peer_index).has_value()) {
    TimeDelta time_between_rendered_frames =
        state->GetPausableState(peer_index)
            ->GetActiveDurationFrom(
                *state->last_rendered_frame_time(peer_index));
    if (state->GetPausableState(peer_index)->IsPaused()) {
      // If stream is currently paused for this receiver, but we still received
      // frame, we have to add time from last pause up to Now() to the time
      // between rendered frames.
      time_between_rendered_frames +=
          Now() - state->GetPausableState(peer_index)->GetLastEventTime();
    }
    frame_in_flight->SetTimeBetweenRenderedFrames(peer_index,
                                                  time_between_rendered_frames);
    frame_in_flight->SetPrevFrameRenderedTime(
        peer_index, *state->last_rendered_frame_time(peer_index));
  }
  state->SetLastRenderedFrameTime(peer_index,
                                  frame_in_flight->rendered_time(peer_index));
  analyzer_stats_.frames_in_flight_left_count.AddSample(
      StatsSample(captured_frames_in_flight_.size(), Now()));
  frames_comparator_.AddComparison(
      stats_key, dropped_count, captured_frame, /*rendered=*/frame,
      FrameComparisonType::kRegular,
      frame_in_flight->GetStatsForPeer(peer_index));

  if (frame_it->second.HaveAllPeersReceived()) {
    frames_storage_.Remove(frame_it->second.id());
    captured_frames_in_flight_.erase(frame_it);
  }

  if (options_.report_infra_metrics) {
    analyzer_stats_.on_frame_rendered_processing_time_ms.AddSample(
        (Now() - processing_started).ms<double>());
  }
}

void DefaultVideoQualityAnalyzer::OnEncoderError(
    absl::string_view peer_name,
    const webrtc::VideoFrame& frame,
    int32_t error_code) {
  RTC_LOG(LS_ERROR) << "Encoder error for frame.id=" << frame.id()
                    << ", code=" << error_code;
}

void DefaultVideoQualityAnalyzer::OnDecoderError(absl::string_view peer_name,
                                                 uint16_t frame_id,
                                                 int32_t error_code,
                                                 const DecoderStats& stats) {
  RTC_LOG(LS_ERROR) << "Decoder error for frame_id=" << frame_id
                    << ", code=" << error_code;

  Timestamp processing_started = Now();
  MutexLock lock(&mutex_);
  RTC_CHECK_EQ(state_, State::kActive)
      << "DefaultVideoQualityAnalyzer has to be started before use";

  size_t peer_index = peers_->index(peer_name);

  if (frame_id == VideoFrame::kNotSetId) {
    frame_counters_.failed_to_decode++;
    unknown_sender_frame_counters_[std::string(peer_name)].failed_to_decode++;
    return;
  }

  auto it = captured_frames_in_flight_.find(frame_id);
  if (it == captured_frames_in_flight_.end() ||
      it->second.HasDecodeEndTime(peer_index)) {
    // It means this frame was decoded before, so we can skip it. It may happen
    // when we have multiple simulcast streams in one track and received
    // the same frame from two different streams because SFU can't reliably
    // correlate two simulcast streams and started relaying the second stream
    // from the same frame it has relayed right before for the first stream.
    return;
  }
  frame_counters_.failed_to_decode++;
  InternalStatsKey key(it->second.stream(),
                       stream_to_sender_.at(it->second.stream()), peer_index);
  stream_frame_counters_.at(key).failed_to_decode++;
  Timestamp now = Now();
  StreamCodecInfo used_decoder;
  used_decoder.codec_name = stats.decoder_name;
  used_decoder.first_frame_id = frame_id;
  used_decoder.last_frame_id = frame_id;
  used_decoder.switched_on_at = now;
  used_decoder.switched_from_at = now;
  it->second.OnDecoderError(peer_index, used_decoder);

  if (options_.report_infra_metrics) {
    analyzer_stats_.on_decoder_error_processing_time_ms.AddSample(
        (Now() - processing_started).ms<double>());
  }
}

void DefaultVideoQualityAnalyzer::RegisterParticipantInCall(
    absl::string_view peer_name) {
  MutexLock lock(&mutex_);
  RTC_CHECK(!peers_->HasName(peer_name));
  size_t new_peer_index = peers_->AddIfAbsent(peer_name);

  // Ensure stats for receiving (for frames from other peers to this one)
  // streams exists. Since in flight frames will be sent to the new peer
  // as well. Sending stats (from this peer to others) will be added by
  // DefaultVideoQualityAnalyzer::OnFrameCaptured.
  std::vector<std::pair<InternalStatsKey, Timestamp>> stream_started_time;
  for (auto [stream_index, sender_peer_index] : stream_to_sender_) {
    InternalStatsKey key(stream_index, sender_peer_index, new_peer_index);

    // To initiate `FrameCounters` for the stream we should pick frame
    // counters with the same stream index and the same sender's peer index
    // and any receiver's peer index and copy from its sender side
    // counters.
    FrameCounters counters;
    for (size_t i : peers_->GetPresentIndexes()) {
      InternalStatsKey prototype_key(stream_index, sender_peer_index, i);
      auto it = stream_frame_counters_.find(prototype_key);
      if (it != stream_frame_counters_.end()) {
        counters.captured = it->second.captured;
        counters.pre_encoded = it->second.pre_encoded;
        counters.encoded = it->second.encoded;
        break;
      }
    }
    // It may happen if we had only one peer before this method was invoked,
    // then `counters` will be empty. In such case empty `counters` are ok.
    stream_frame_counters_.insert({key, std::move(counters)});

    stream_started_time.push_back(
        {key, stream_states_.at(stream_index).stream_started_time()});
  }
  frames_comparator_.RegisterParticipantInCall(stream_started_time,
                                               start_time_);
  // Ensure, that frames states are handled correctly
  // (e.g. dropped frames tracking).
  for (auto& [stream_index, stream_state] : stream_states_) {
    stream_state.AddPeer(new_peer_index);
  }
  // Register new peer for every frame in flight.
  // It is guaranteed, that no garbage FrameInFlight objects will stay in
  // memory because of adding new peer. Even if the new peer won't receive the
  // frame, the frame will be removed by OnFrameRendered after next frame comes
  // for the new peer. It is important because FrameInFlight is a large object.
  for (auto& [frame_id, frame_in_flight] : captured_frames_in_flight_) {
    frame_in_flight.AddExpectedReceiver(new_peer_index);
  }
}

void DefaultVideoQualityAnalyzer::UnregisterParticipantInCall(
    absl::string_view peer_name) {
  MutexLock lock(&mutex_);
  RTC_CHECK(peers_->HasName(peer_name));
  absl::optional<size_t> peer_index = peers_->RemoveIfPresent(peer_name);
  RTC_CHECK(peer_index.has_value());

  for (auto& [stream_index, stream_state] : stream_states_) {
    if (!options_.enable_receive_own_stream &&
        peer_index == stream_state.sender()) {
      continue;
    }

    AddExistingFramesInFlightForStreamToComparator(stream_index, stream_state,
                                                   *peer_index);

    stream_state.RemovePeer(*peer_index);
  }

  // Remove peer from every frame in flight. If we removed that last expected
  // receiver for the frame, then we should removed this frame if it was
  // already encoded. If frame wasn't encoded, it still will be used by sender
  // side pipeline, so we can't delete it yet.
  for (auto it = captured_frames_in_flight_.begin();
       it != captured_frames_in_flight_.end();) {
    FrameInFlight& frame_in_flight = it->second;
    frame_in_flight.RemoveExpectedReceiver(*peer_index);
    // If frame was fully sent and all receivers received it, then erase it.
    // It may happen that when we remove FrameInFlight only some Simulcast/SVC
    // layers were encoded and frame has encoded time, but more layers might be
    // encoded after removal. In such case it's safe to still remove a frame,
    // because OnFrameEncoded method will correctly handle the case when there
    // is no FrameInFlight for the received encoded image.
    if (frame_in_flight.HasEncodedTime() &&
        frame_in_flight.HaveAllPeersReceived()) {
      frames_storage_.Remove(frame_in_flight.id());
      it = captured_frames_in_flight_.erase(it);
    } else {
      it++;
    }
  }
}

void DefaultVideoQualityAnalyzer::OnPauseAllStreamsFrom(
    absl::string_view sender_peer_name,
    absl::string_view receiver_peer_name) {
  MutexLock lock(&mutex_);
  if (peers_->HasName(sender_peer_name) &&
      peers_->HasName(receiver_peer_name)) {
    size_t sender_peer_index = peers_->index(sender_peer_name);
    size_t receiver_peer_index = peers_->index(receiver_peer_name);
    for (auto& [unused, stream_state] : stream_states_) {
      if (stream_state.sender() == sender_peer_index) {
        stream_state.GetPausableState(receiver_peer_index)->Pause();
      }
    }
  }
}

void DefaultVideoQualityAnalyzer::OnResumeAllStreamsFrom(
    absl::string_view sender_peer_name,
    absl::string_view receiver_peer_name) {
  MutexLock lock(&mutex_);
  if (peers_->HasName(sender_peer_name) &&
      peers_->HasName(receiver_peer_name)) {
    size_t sender_peer_index = peers_->index(sender_peer_name);
    size_t receiver_peer_index = peers_->index(receiver_peer_name);
    for (auto& [unused, stream_state] : stream_states_) {
      if (stream_state.sender() == sender_peer_index) {
        stream_state.GetPausableState(receiver_peer_index)->Resume();
      }
    }
  }
}

void DefaultVideoQualityAnalyzer::Stop() {
  std::map<InternalStatsKey, Timestamp> last_rendered_frame_times;
  {
    MutexLock lock(&mutex_);
    if (state_ == State::kStopped) {
      return;
    }
    RTC_CHECK_EQ(state_, State::kActive)
        << "DefaultVideoQualityAnalyzer has to be started before use";

    state_ = State::kStopped;

    // Add the amount of frames in flight to the analyzer stats before all left
    // frames in flight will be sent to the `frames_compartor_`.
    analyzer_stats_.frames_in_flight_left_count.AddSample(
        StatsSample(captured_frames_in_flight_.size(), Now()));

    for (auto& state_entry : stream_states_) {
      const size_t stream_index = state_entry.first;
      StreamState& stream_state = state_entry.second;

      // Populate `last_rendered_frame_times` map for all peers that were met in
      // call, not only for the currently presented ones.
      for (size_t peer_index : peers_->GetAllIndexes()) {
        if (peer_index == stream_state.sender() &&
            !options_.enable_receive_own_stream) {
          continue;
        }

        InternalStatsKey stats_key(stream_index, stream_state.sender(),
                                   peer_index);

        // If there are no freezes in the call we have to report
        // time_between_freezes_ms as call duration and in such case
        // `stream_last_freeze_end_time` for this stream will be `start_time_`.
        // If there is freeze, then we need add time from last rendered frame
        // to last freeze end as time between freezes.
        if (stream_state.last_rendered_frame_time(peer_index)) {
          last_rendered_frame_times.emplace(
              stats_key,
              stream_state.last_rendered_frame_time(peer_index).value());
        }
      }

      // Push left frame in flight for analysis for the peers that are still in
      // the call.
      for (size_t peer_index : peers_->GetPresentIndexes()) {
        if (peer_index == stream_state.sender() &&
            !options_.enable_receive_own_stream) {
          continue;
        }

        AddExistingFramesInFlightForStreamToComparator(
            stream_index, stream_state, peer_index);
      }
    }
  }
  frames_comparator_.Stop(last_rendered_frame_times);

  // Perform final Metrics update. On this place analyzer is stopped and no one
  // holds any locks.
  {
    MutexLock lock(&mutex_);
    FramesComparatorStats frames_comparator_stats =
        frames_comparator_.frames_comparator_stats();
    analyzer_stats_.comparisons_queue_size =
        std::move(frames_comparator_stats.comparisons_queue_size);
    analyzer_stats_.comparisons_done = frames_comparator_stats.comparisons_done;
    analyzer_stats_.cpu_overloaded_comparisons_done =
        frames_comparator_stats.cpu_overloaded_comparisons_done;
    analyzer_stats_.memory_overloaded_comparisons_done =
        frames_comparator_stats.memory_overloaded_comparisons_done;
  }
  ReportResults();
}

std::string DefaultVideoQualityAnalyzer::GetStreamLabel(uint16_t frame_id) {
  MutexLock lock(&mutex_);
  return GetStreamLabelInternal(frame_id);
}

std::string DefaultVideoQualityAnalyzer::GetSenderPeerName(
    uint16_t frame_id) const {
  MutexLock lock(&mutex_);
  std::string stream_label = GetStreamLabelInternal(frame_id);
  size_t stream_index = streams_.index(stream_label);
  size_t sender_peer_index = stream_states_.at(stream_index).sender();
  return peers_->name(sender_peer_index);
}

std::set<StatsKey> DefaultVideoQualityAnalyzer::GetKnownVideoStreams() const {
  MutexLock lock(&mutex_);
  std::set<StatsKey> out;
  for (auto& item : frames_comparator_.stream_stats()) {
    RTC_LOG(LS_INFO) << item.first.ToString() << " ==> "
                     << ToStatsKey(item.first).ToString();
    out.insert(ToStatsKey(item.first));
  }
  return out;
}

VideoStreamsInfo DefaultVideoQualityAnalyzer::GetKnownStreams() const {
  MutexLock lock(&mutex_);
  std::map<std::string, std::string> stream_to_sender;
  std::map<std::string, std::set<std::string>> sender_to_streams;
  std::map<std::string, std::set<std::string>> stream_to_receivers;

  for (auto& item : frames_comparator_.stream_stats()) {
    const std::string& stream_label = streams_.name(item.first.stream);
    const std::string& sender = peers_->name(item.first.sender);
    const std::string& receiver = peers_->name(item.first.receiver);
    RTC_LOG(LS_INFO) << item.first.ToString() << " ==> "
                     << "stream=" << stream_label << "; sender=" << sender
                     << "; receiver=" << receiver;
    stream_to_sender.emplace(stream_label, sender);
    auto streams_it = sender_to_streams.find(sender);
    if (streams_it != sender_to_streams.end()) {
      streams_it->second.emplace(stream_label);
    } else {
      sender_to_streams.emplace(sender, std::set<std::string>{stream_label});
    }
    auto receivers_it = stream_to_receivers.find(stream_label);
    if (receivers_it != stream_to_receivers.end()) {
      receivers_it->second.emplace(receiver);
    } else {
      stream_to_receivers.emplace(stream_label,
                                  std::set<std::string>{receiver});
    }
  }

  return VideoStreamsInfo(std::move(stream_to_sender),
                          std::move(sender_to_streams),
                          std::move(stream_to_receivers));
}

FrameCounters DefaultVideoQualityAnalyzer::GetGlobalCounters() const {
  MutexLock lock(&mutex_);
  return frame_counters_;
}

std::map<std::string, FrameCounters>
DefaultVideoQualityAnalyzer::GetUnknownSenderFrameCounters() const {
  MutexLock lock(&mutex_);
  return unknown_sender_frame_counters_;
}

std::map<StatsKey, FrameCounters>
DefaultVideoQualityAnalyzer::GetPerStreamCounters() const {
  MutexLock lock(&mutex_);
  std::map<StatsKey, FrameCounters> out;
  for (auto& item : stream_frame_counters_) {
    out.emplace(ToStatsKey(item.first), item.second);
  }
  return out;
}

std::map<StatsKey, StreamStats> DefaultVideoQualityAnalyzer::GetStats() const {
  MutexLock lock1(&mutex_);
  std::map<StatsKey, StreamStats> out;
  for (auto& item : frames_comparator_.stream_stats()) {
    out.emplace(ToStatsKey(item.first), item.second);
  }
  return out;
}

AnalyzerStats DefaultVideoQualityAnalyzer::GetAnalyzerStats() const {
  MutexLock lock(&mutex_);
  return analyzer_stats_;
}

uint16_t DefaultVideoQualityAnalyzer::GetNextFrameId() {
  uint16_t frame_id = next_frame_id_++;
  if (next_frame_id_ == VideoFrame::kNotSetId) {
    next_frame_id_ = 1;
  }
  return frame_id;
}

void DefaultVideoQualityAnalyzer::
    AddExistingFramesInFlightForStreamToComparator(size_t stream_index,
                                                   StreamState& stream_state,
                                                   size_t peer_index) {
  InternalStatsKey stats_key(stream_index, stream_state.sender(), peer_index);

  // Add frames in flight for this stream into frames comparator.
  // Frames in flight were not rendered, so they won't affect stream's
  // last rendered frame time.
  while (!stream_state.IsEmpty(peer_index) &&
         !stream_state.GetPausableState(peer_index)->IsPaused()) {
    uint16_t frame_id = stream_state.PopFront(peer_index);
    auto it = captured_frames_in_flight_.find(frame_id);
    RTC_DCHECK(it != captured_frames_in_flight_.end());
    FrameInFlight& frame = it->second;

    frames_comparator_.AddComparison(stats_key, /*captured=*/absl::nullopt,
                                     /*rendered=*/absl::nullopt,
                                     FrameComparisonType::kFrameInFlight,
                                     frame.GetStatsForPeer(peer_index));
  }
}

int DefaultVideoQualityAnalyzer::ProcessNotSeenFramesBeforeRendered(
    size_t peer_index,
    uint16_t rendered_frame_id,
    const InternalStatsKey& stats_key,
    StreamState& state) {
  int dropped_count = 0;
  while (!state.IsEmpty(peer_index) &&
         state.Front(peer_index) != rendered_frame_id) {
    uint16_t next_frame_id = state.PopFront(peer_index);
    auto next_frame_it = captured_frames_in_flight_.find(next_frame_id);
    RTC_DCHECK(next_frame_it != captured_frames_in_flight_.end());
    FrameInFlight& next_frame = next_frame_it->second;

    // Depending if the receiver was subscribed to this stream or not at the
    // time when frame was captured, the frame should be considered as dropped
    // or superfluous (see below for explanation). Superfluous frames must be
    // excluded from stats calculations.
    //
    // We should consider next cases:
    // Legend:
    //    + - frame captured on the stream
    //    p - stream is paused
    //    r - stream is resumed
    //
    //    last                                                   currently
    //  rendered                                                  rendered
    //    frame                                                    frame
    //      |---------------------- dropped -------------------------|
    // (1) -[]---+---+---+---+---+---+---+---+---+---+---+---+---+---[]-> time
    //      |                                                        |
    //      |                                                        |
    //      |-- dropped ---┐       ┌- dropped -┐       ┌- dropped ---|
    // (2) -[]---+---+---+-|-+---+-|-+---+---+-|-+---+-|-+---+---+---[]-> time
    //      |              p       r           p       r             |
    //      |                                                        |
    //      |-- dropped ---┐       ┌------------ dropped ------------|
    // (3) -[]---+---+---+-|-+---+-|-+---+---+---+---+---+-|-+---+---[]-> time
    //                     p       r                       p
    //
    // Cases explanation:
    //   (1) Regular media flow, frame is received after freeze.
    //   (2) Stream was paused and received multiple times. Frame is received
    //       after freeze from last resume.
    //   (3) Stream was paused and received multiple times. Frame is received
    //       after stream was paused because frame was already in the network.
    //
    // Based on that if stream wasn't paused when `next_frame_id` was captured,
    // then `next_frame_id` should be considered as dropped. If stream was NOT
    // resumed after `next_frame_id` was captured but we still received a
    // `rendered_frame_id` on this stream, then `next_frame_id` also should
    // be considered as dropped. In other cases `next_frame_id` should be
    // considered as superfluous, because receiver wasn't expected to receive
    // `next_frame_id` at all.

    bool is_dropped = false;
    bool is_paused = state.GetPausableState(peer_index)
                         ->WasPausedAt(next_frame.captured_time());
    if (!is_paused) {
      is_dropped = true;
    } else {
      bool was_resumed_after =
          state.GetPausableState(peer_index)
              ->WasResumedAfter(next_frame.captured_time());
      if (!was_resumed_after) {
        is_dropped = true;
      }
    }

    if (is_dropped) {
      dropped_count++;
      // Frame with id `dropped_frame_id` was dropped. We need:
      // 1. Update global and stream frame counters
      // 2. Extract corresponding frame from `captured_frames_in_flight_`
      // 3. Send extracted frame to comparison with dropped=true
      // 4. Cleanup dropped frame
      frame_counters_.dropped++;
      stream_frame_counters_.at(stats_key).dropped++;

      next_frame.MarkDropped(peer_index);

      analyzer_stats_.frames_in_flight_left_count.AddSample(
          StatsSample(captured_frames_in_flight_.size(), Now()));
      frames_comparator_.AddComparison(stats_key, /*captured=*/absl::nullopt,
                                       /*rendered=*/absl::nullopt,
                                       FrameComparisonType::kDroppedFrame,
                                       next_frame.GetStatsForPeer(peer_index));
    } else {
      next_frame.MarkSuperfluous(peer_index);
    }

    if (next_frame_it->second.HaveAllPeersReceived()) {
      frames_storage_.Remove(next_frame_it->second.id());
      captured_frames_in_flight_.erase(next_frame_it);
    }
  }
  return dropped_count;
}

void DefaultVideoQualityAnalyzer::ReportResults() {
  MutexLock lock(&mutex_);
  for (auto& item : frames_comparator_.stream_stats()) {
    ReportResults(item.first, item.second,
                  stream_frame_counters_.at(item.first));
  }
  // TODO(bugs.webrtc.org/14757): Remove kExperimentalTestNameMetadataKey.
  metrics_logger_->LogSingleValueMetric(
      "cpu_usage_%", test_label_, GetCpuUsagePercent(), Unit::kUnitless,
      ImprovementDirection::kSmallerIsBetter,
      {{MetricMetadataKey::kExperimentalTestNameMetadataKey, test_label_}});
  LogFrameCounters("Global", frame_counters_);
  if (!unknown_sender_frame_counters_.empty()) {
    RTC_LOG(LS_INFO) << "Received frame counters with unknown frame id:";
    for (const auto& [peer_name, frame_counters] :
         unknown_sender_frame_counters_) {
      LogFrameCounters(peer_name, frame_counters);
    }
  }
  RTC_LOG(LS_INFO) << "Received frame counters per stream:";
  for (const auto& [stats_key, stream_stats] :
       frames_comparator_.stream_stats()) {
    LogFrameCounters(ToStatsKey(stats_key).ToString(),
                     stream_frame_counters_.at(stats_key));
    LogStreamInternalStats(ToStatsKey(stats_key).ToString(), stream_stats,
                           start_time_);
  }
  if (!analyzer_stats_.comparisons_queue_size.IsEmpty()) {
    RTC_LOG(LS_INFO) << "comparisons_queue_size min="
                     << analyzer_stats_.comparisons_queue_size.GetMin()
                     << "; max="
                     << analyzer_stats_.comparisons_queue_size.GetMax()
                     << "; 99%="
                     << analyzer_stats_.comparisons_queue_size.GetPercentile(
                            0.99);
  }
  RTC_LOG(LS_INFO) << "comparisons_done=" << analyzer_stats_.comparisons_done;
  RTC_LOG(LS_INFO) << "cpu_overloaded_comparisons_done="
                   << analyzer_stats_.cpu_overloaded_comparisons_done;
  RTC_LOG(LS_INFO) << "memory_overloaded_comparisons_done="
                   << analyzer_stats_.memory_overloaded_comparisons_done;
  if (options_.report_infra_metrics) {
    metrics_logger_->LogMetric("comparisons_queue_size", test_label_,
                               analyzer_stats_.comparisons_queue_size,
                               Unit::kCount,
                               ImprovementDirection::kSmallerIsBetter);
    metrics_logger_->LogMetric("frames_in_flight_left_count", test_label_,
                               analyzer_stats_.frames_in_flight_left_count,
                               Unit::kCount,
                               ImprovementDirection::kSmallerIsBetter);
    metrics_logger_->LogSingleValueMetric(
        "comparisons_done", test_label_, analyzer_stats_.comparisons_done,
        Unit::kCount, ImprovementDirection::kNeitherIsBetter);
    metrics_logger_->LogSingleValueMetric(
        "cpu_overloaded_comparisons_done", test_label_,
        analyzer_stats_.cpu_overloaded_comparisons_done, Unit::kCount,
        ImprovementDirection::kNeitherIsBetter);
    metrics_logger_->LogSingleValueMetric(
        "memory_overloaded_comparisons_done", test_label_,
        analyzer_stats_.memory_overloaded_comparisons_done, Unit::kCount,
        ImprovementDirection::kNeitherIsBetter);
    metrics_logger_->LogSingleValueMetric(
        "test_duration", test_label_, (Now() - start_time_).ms(),
        Unit::kMilliseconds, ImprovementDirection::kNeitherIsBetter);

    metrics_logger_->LogMetric(
        "on_frame_captured_processing_time_ms", test_label_,
        analyzer_stats_.on_frame_captured_processing_time_ms,
        Unit::kMilliseconds, ImprovementDirection::kSmallerIsBetter);
    metrics_logger_->LogMetric(
        "on_frame_pre_encode_processing_time_ms", test_label_,
        analyzer_stats_.on_frame_pre_encode_processing_time_ms,
        Unit::kMilliseconds, ImprovementDirection::kSmallerIsBetter);
    metrics_logger_->LogMetric(
        "on_frame_encoded_processing_time_ms", test_label_,
        analyzer_stats_.on_frame_encoded_processing_time_ms,
        Unit::kMilliseconds, ImprovementDirection::kSmallerIsBetter);
    metrics_logger_->LogMetric(
        "on_frame_pre_decode_processing_time_ms", test_label_,
        analyzer_stats_.on_frame_pre_decode_processing_time_ms,
        Unit::kMilliseconds, ImprovementDirection::kSmallerIsBetter);
    metrics_logger_->LogMetric(
        "on_frame_decoded_processing_time_ms", test_label_,
        analyzer_stats_.on_frame_decoded_processing_time_ms,
        Unit::kMilliseconds, ImprovementDirection::kSmallerIsBetter);
    metrics_logger_->LogMetric(
        "on_frame_rendered_processing_time_ms", test_label_,
        analyzer_stats_.on_frame_rendered_processing_time_ms,
        Unit::kMilliseconds, ImprovementDirection::kSmallerIsBetter);
    metrics_logger_->LogMetric(
        "on_decoder_error_processing_time_ms", test_label_,
        analyzer_stats_.on_decoder_error_processing_time_ms,
        Unit::kMilliseconds, ImprovementDirection::kSmallerIsBetter);
  }
}

void DefaultVideoQualityAnalyzer::ReportResults(
    const InternalStatsKey& key,
    const StreamStats& stats,
    const FrameCounters& frame_counters) {
  TimeDelta test_duration = Now() - start_time_;
  std::string test_case_name = GetTestCaseName(ToMetricName(key));
  // TODO(bugs.webrtc.org/14757): Remove kExperimentalTestNameMetadataKey.
  std::map<std::string, std::string> metric_metadata{
      {MetricMetadataKey::kPeerMetadataKey, peers_->name(key.sender)},
      {MetricMetadataKey::kVideoStreamMetadataKey, streams_.name(key.stream)},
      {MetricMetadataKey::kSenderMetadataKey, peers_->name(key.sender)},
      {MetricMetadataKey::kReceiverMetadataKey, peers_->name(key.receiver)},
      {MetricMetadataKey::kExperimentalTestNameMetadataKey, test_label_}};

  metrics_logger_->LogMetric(
      "psnr_dB", test_case_name, stats.psnr, Unit::kUnitless,
      ImprovementDirection::kBiggerIsBetter, metric_metadata);
  metrics_logger_->LogMetric(
      "ssim", test_case_name, stats.ssim, Unit::kUnitless,
      ImprovementDirection::kBiggerIsBetter, metric_metadata);
  metrics_logger_->LogMetric("transport_time", test_case_name,
                             stats.transport_time_ms, Unit::kMilliseconds,
                             ImprovementDirection::kSmallerIsBetter,
                             metric_metadata);
  metrics_logger_->LogMetric(
      "total_delay_incl_transport", test_case_name,
      stats.total_delay_incl_transport_ms, Unit::kMilliseconds,
      ImprovementDirection::kSmallerIsBetter, metric_metadata);
  metrics_logger_->LogMetric(
      "time_between_rendered_frames", test_case_name,
      stats.time_between_rendered_frames_ms, Unit::kMilliseconds,
      ImprovementDirection::kSmallerIsBetter, metric_metadata);
  metrics_logger_->LogSingleValueMetric(
      "harmonic_framerate", test_case_name, stats.harmonic_framerate_fps,
      Unit::kHertz, ImprovementDirection::kBiggerIsBetter, metric_metadata);
  metrics_logger_->LogSingleValueMetric(
      "encode_frame_rate", test_case_name,
      stats.encode_frame_rate.IsEmpty()
          ? 0
          : stats.encode_frame_rate.GetEventsPerSecond(),
      Unit::kHertz, ImprovementDirection::kBiggerIsBetter, metric_metadata);
  metrics_logger_->LogMetric(
      "encode_time", test_case_name, stats.encode_time_ms, Unit::kMilliseconds,
      ImprovementDirection::kSmallerIsBetter, metric_metadata);
  metrics_logger_->LogMetric("time_between_freezes", test_case_name,
                             stats.time_between_freezes_ms, Unit::kMilliseconds,
                             ImprovementDirection::kBiggerIsBetter,
                             metric_metadata);
  metrics_logger_->LogMetric("freeze_time_ms", test_case_name,
                             stats.freeze_time_ms, Unit::kMilliseconds,
                             ImprovementDirection::kSmallerIsBetter,
                             metric_metadata);
  metrics_logger_->LogMetric(
      "pixels_per_frame", test_case_name, stats.resolution_of_decoded_frame,
      Unit::kCount, ImprovementDirection::kBiggerIsBetter, metric_metadata);
  metrics_logger_->LogSingleValueMetric(
      "min_psnr_dB", test_case_name,
      stats.psnr.IsEmpty() ? 0 : stats.psnr.GetMin(), Unit::kUnitless,
      ImprovementDirection::kBiggerIsBetter, metric_metadata);
  metrics_logger_->LogMetric(
      "decode_time", test_case_name, stats.decode_time_ms, Unit::kMilliseconds,
      ImprovementDirection::kSmallerIsBetter, metric_metadata);
  metrics_logger_->LogMetric(
      "receive_to_render_time", test_case_name, stats.receive_to_render_time_ms,
      Unit::kMilliseconds, ImprovementDirection::kSmallerIsBetter,
      metric_metadata);
  metrics_logger_->LogSingleValueMetric(
      "dropped_frames", test_case_name, frame_counters.dropped, Unit::kCount,
      ImprovementDirection::kSmallerIsBetter, metric_metadata);
  metrics_logger_->LogSingleValueMetric(
      "frames_in_flight", test_case_name,
      frame_counters.captured - frame_counters.rendered -
          frame_counters.dropped,
      Unit::kCount, ImprovementDirection::kSmallerIsBetter, metric_metadata);
  metrics_logger_->LogSingleValueMetric(
      "rendered_frames", test_case_name, frame_counters.rendered, Unit::kCount,
      ImprovementDirection::kBiggerIsBetter, metric_metadata);
  metrics_logger_->LogMetric(
      "max_skipped", test_case_name, stats.skipped_between_rendered,
      Unit::kCount, ImprovementDirection::kSmallerIsBetter, metric_metadata);
  metrics_logger_->LogMetric(
      "target_encode_bitrate", test_case_name,
      stats.target_encode_bitrate / 1000, Unit::kKilobitsPerSecond,
      ImprovementDirection::kNeitherIsBetter, metric_metadata);
  for (const auto& [spatial_layer, qp] : stats.spatial_layers_qp) {
    std::map<std::string, std::string> qp_metadata = metric_metadata;
    qp_metadata[MetricMetadataKey::kSpatialLayerMetadataKey] =
        std::to_string(spatial_layer);
    metrics_logger_->LogMetric("qp_sl" + std::to_string(spatial_layer),
                               test_case_name, qp, Unit::kUnitless,
                               ImprovementDirection::kSmallerIsBetter,
                               std::move(qp_metadata));
  }
  metrics_logger_->LogSingleValueMetric(
      "actual_encode_bitrate", test_case_name,
      static_cast<double>(stats.total_encoded_images_payload) /
          test_duration.seconds<double>() * kBitsInByte / 1000,
      Unit::kKilobitsPerSecond, ImprovementDirection::kNeitherIsBetter,
      metric_metadata);

  if (options_.report_detailed_frame_stats) {
    metrics_logger_->LogSingleValueMetric(
        "capture_frame_rate", test_case_name,
        stats.capture_frame_rate.IsEmpty()
            ? 0
            : stats.capture_frame_rate.GetEventsPerSecond(),
        Unit::kHertz, ImprovementDirection::kBiggerIsBetter, metric_metadata);
    metrics_logger_->LogSingleValueMetric(
        "num_encoded_frames", test_case_name, frame_counters.encoded,
        Unit::kCount, ImprovementDirection::kBiggerIsBetter, metric_metadata);
    metrics_logger_->LogSingleValueMetric(
        "num_decoded_frames", test_case_name, frame_counters.decoded,
        Unit::kCount, ImprovementDirection::kBiggerIsBetter, metric_metadata);
    metrics_logger_->LogSingleValueMetric(
        "num_send_key_frames", test_case_name, stats.num_send_key_frames,
        Unit::kCount, ImprovementDirection::kBiggerIsBetter, metric_metadata);
    metrics_logger_->LogSingleValueMetric(
        "num_recv_key_frames", test_case_name, stats.num_recv_key_frames,
        Unit::kCount, ImprovementDirection::kBiggerIsBetter, metric_metadata);

    metrics_logger_->LogMetric("recv_key_frame_size_bytes", test_case_name,
                               stats.recv_key_frame_size_bytes, Unit::kCount,
                               ImprovementDirection::kBiggerIsBetter,
                               metric_metadata);
    metrics_logger_->LogMetric("recv_delta_frame_size_bytes", test_case_name,
                               stats.recv_delta_frame_size_bytes, Unit::kCount,
                               ImprovementDirection::kBiggerIsBetter,
                               metric_metadata);
  }
}

std::string DefaultVideoQualityAnalyzer::GetTestCaseName(
    const std::string& stream_label) const {
  return test_label_ + "/" + stream_label;
}

Timestamp DefaultVideoQualityAnalyzer::Now() {
  return clock_->CurrentTime();
}

StatsKey DefaultVideoQualityAnalyzer::ToStatsKey(
    const InternalStatsKey& key) const {
  return StatsKey(streams_.name(key.stream), peers_->name(key.receiver));
}

std::string DefaultVideoQualityAnalyzer::ToMetricName(
    const InternalStatsKey& key) const {
  const std::string& stream_label = streams_.name(key.stream);
  if (peers_->GetKnownSize() <= 2 && key.sender != key.receiver) {
    // TODO(titovartem): remove this special case.
    return stream_label;
  }
  rtc::StringBuilder out;
  out << stream_label << "_" << peers_->name(key.sender) << "_"
      << peers_->name(key.receiver);
  return out.str();
}

std::string DefaultVideoQualityAnalyzer::GetStreamLabelInternal(
    uint16_t frame_id) const {
  auto it = captured_frames_in_flight_.find(frame_id);
  if (it != captured_frames_in_flight_.end()) {
    return streams_.name(it->second.stream());
  }
  for (auto hist_it = stream_to_frame_id_history_.begin();
       hist_it != stream_to_frame_id_history_.end(); ++hist_it) {
    auto hist_set_it = hist_it->second.find(frame_id);
    if (hist_set_it != hist_it->second.end()) {
      return streams_.name(hist_it->first);
    }
  }
  RTC_CHECK(false) << "Unknown frame_id=" << frame_id;
}

double DefaultVideoQualityAnalyzer::GetCpuUsagePercent() const {
  return cpu_measurer_.GetCpuUsagePercent();
}

std::map<std::string, std::vector<uint16_t>>
DefaultVideoQualityAnalyzer::GetStreamFrames() const {
  MutexLock lock(&mutex_);
  std::map<std::string, std::vector<uint16_t>> out;
  for (auto entry_it : stream_to_frame_id_full_history_) {
    out.insert({streams_.name(entry_it.first), entry_it.second});
  }
  return out;
}

}  // namespace webrtc
