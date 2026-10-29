/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/frame_helpers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>

#include "absl/container/inlined_vector.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/encoded_frame.h"
#include "api/video/encoded_image.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

namespace webrtc {

namespace {
constexpr TimeDelta kMaxVideoDelay = TimeDelta::Millis(10000);
}  // namespace

bool FrameHasBadRenderTiming(Timestamp render_time, Timestamp now) {
  // Zero render time means render immediately.
  if (render_time.IsZero()) {
    return false;
  }
  if (render_time < Timestamp::Zero()) {
    return true;
  }
  TimeDelta frame_delay = render_time - now;
  if (frame_delay.Abs() > kMaxVideoDelay) {
    RTC_LOG(LS_WARNING) << "Frame has bad render timing because it is out of "
                           "the delay bounds (frame_delay_ms="
                        << frame_delay.ms()
                        << ", kMaxVideoDelay_ms=" << kMaxVideoDelay.ms() << ")";
    return true;
  }
  return false;
}

bool TargetVideoDelayIsTooLarge(TimeDelta target_video_delay) {
  if (target_video_delay > kMaxVideoDelay) {
    RTC_LOG(LS_WARNING)
        << "Target video delay is too large. (target_video_delay_ms="
        << target_video_delay.ms()
        << ", kMaxVideoDelay_ms=" << kMaxVideoDelay.ms() << ")";
    return true;
  }
  return false;
}

std::unique_ptr<EncodedFrame> CombineAndDeleteFrames(
    absl::InlinedVector<std::unique_ptr<EncodedFrame>, 4> frames) {
  RTC_DCHECK(!frames.empty());

  if (frames.size() == 1) {
    return std::move(frames[0]);
  }

  // Check if any of the spatial layers explicitly define the spatial id.
  // If they do the spatial ids must be strictly increasing.
  bool any_spatial_index_defined = false;
  for (const auto& frame : frames) {
    if (frame->SpatialIndex().has_value()) {
      any_spatial_index_defined = true;
      break;
    }
  }

  if (any_spatial_index_defined) {
    for (size_t i = 1; i < frames.size(); ++i) {
      if (frames[i]->SpatialIndex().value_or(0) <=
          frames[i - 1]->SpatialIndex().value_or(0)) {
        RTC_LOG(LS_ERROR)
            << "Failed to combine frames - invalid spatial index order.";
        return nullptr;
      }
    }
  }

  size_t total_length = 0;
  for (const auto& frame : frames) {
    total_length += frame->size();
  }
  const EncodedFrame& last_frame = *frames.back();
  std::unique_ptr<EncodedFrame> first_frame = std::move(frames[0]);
  auto encoded_image_buffer = EncodedImageBuffer::Create(total_length);
  std::span<uint8_t> buffer(*encoded_image_buffer);
  if (any_spatial_index_defined) {
    const int first_spatial_index = first_frame->SpatialIndex().value_or(0);
    // Spatial index of combined frame is set equal to spatial index of its top
    // spatial layer.
    first_frame->SetSpatialIndex(last_frame.SpatialIndex().value_or(0));
    first_frame->SetSpatialLayerFrameSize(first_spatial_index,
                                          first_frame->size());
  }
  std::ranges::copy(*first_frame, buffer.begin());
  buffer = buffer.subspan(first_frame->size());

  // Each spatial layer (at the same rtp_timestamp) sends corruption data.
  // Reconstructed (combined) frame will be of resolution of the highest spatial
  // layer and that's why the corruption data for the highest layer should be
  // used to calculate the metric on the combined frame for the best outcome.
  //
  // TODO: bugs.webrtc.org/358039777 - Fix for LxTy scalability, currently only
  // works for LxTy_KEY and L1Ty.
  first_frame->SetFrameInstrumentationData(
      last_frame.CodecSpecific()->frame_instrumentation_data);

  first_frame->video_timing_mutable()->network2_timestamp_ms =
      last_frame.video_timing().network2_timestamp_ms;
  first_frame->video_timing_mutable()->receive_finish_ms =
      last_frame.video_timing().receive_finish_ms;

  // Append all remaining frames to the first one.
  for (size_t i = 1; i < frames.size(); ++i) {
    // Let |next_frame| fall out of scope so it is deleted after copying.
    std::unique_ptr<EncodedFrame> next_frame = std::move(frames[i]);
    if (any_spatial_index_defined) {
      first_frame->SetSpatialLayerFrameSize(
          next_frame->SpatialIndex().value_or(0), next_frame->size());
    }
    std::ranges::copy(*next_frame, buffer.begin());
    buffer = buffer.subspan(next_frame->size());
  }
  first_frame->SetEncodedData(encoded_image_buffer);
  return first_frame;
}

}  // namespace webrtc
