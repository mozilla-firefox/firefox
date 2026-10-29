/*
 * Copyright 2024 The WebRTC project authors. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file in the root of the source
 * tree. An additional intellectual property rights grant can be found
 * in the file PATENTS.  All contributing project authors may
 * be found in the AUTHORS file in the root of the source tree.
 */

#include "video/corruption_detection/halton_frame_sampler.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/nv12_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/safe_minmax.h"
#include "video/corruption_detection/halton_sequence.h"
#include "video/corruption_detection/video_frame_sampler.h"

namespace webrtc {
namespace {

const double kCutoff = 0.2;
const int kLowerBoundKernelSize = 3;
constexpr int kMaxFramesBetweenSamples = 33;

// Corresponds to 1 second for RTP timestamps (which are 90kHz).
constexpr uint32_t kMaxDurationBetweenSamples = 90'000;

// The second *time* is always later than the first. If the second *timestamp*
// is smaller than the first, we interpret that as if one wraparound has
// occurred.
uint32_t EnoughTimeHasPassed(uint32_t from, uint32_t to) {
  return (to - from) >= kMaxDurationBetweenSamples;
}

}  // namespace

HaltonFrameSampler::HaltonFrameSampler()
    : coordinate_sampler_prng_(HaltonSequence(2)) {}

bool HaltonFrameSampler::ShouldSampleFrame(bool is_key_frame,
                                           uint32_t rtp_timestamp) {
  if (rtp_timestamp_last_frame_sampled_.has_value()) {
    RTC_CHECK_NE(*rtp_timestamp_last_frame_sampled_, rtp_timestamp);
  }
  if (is_key_frame || frames_until_next_sample_ <= 0 ||
      !rtp_timestamp_last_frame_sampled_.has_value() ||
      EnoughTimeHasPassed(*rtp_timestamp_last_frame_sampled_, rtp_timestamp)) {
    frames_until_next_sample_ =
        (kMaxFramesBetweenSamples - 1) - (frames_sampled_ % 8);
    ++frames_sampled_;
    rtp_timestamp_last_frame_sampled_ = rtp_timestamp;
    return true;
  }
  --frames_until_next_sample_;
  return false;
}

std::vector<HaltonFrameSampler::Coordinates>
HaltonFrameSampler::GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
    bool is_key_frame,
    uint32_t rtp_timestamp,
    int num_samples) {
  if (num_samples < 1) {
    return {};
  }
  if (ShouldSampleFrame(is_key_frame, rtp_timestamp)) {
    return GetSampleCoordinatesForFrame(num_samples);
  }
  return {};
}

std::vector<HaltonFrameSampler::Coordinates>
HaltonFrameSampler::GetSampleCoordinatesForFrame(int num_samples) {
  RTC_CHECK_GE(num_samples, 1);
  std::vector<Coordinates> coordinates;
  coordinates.reserve(num_samples);
  for (int i = 0; i < num_samples; ++i) {
    coordinates.push_back(GetNextSampleCoordinates());
  }
  return coordinates;
}

HaltonFrameSampler::Coordinates HaltonFrameSampler::GetNextSampleCoordinates() {
  std::vector<double> point = coordinate_sampler_prng_.GetNext();
  return {.row = point[0], .column = point[1]};
}

void HaltonFrameSampler::Restart() {
  coordinate_sampler_prng_.Reset();
}

int HaltonFrameSampler::GetCurrentIndex() const {
  return coordinate_sampler_prng_.GetCurrentIndex();
}

void HaltonFrameSampler::SetCurrentIndex(int index) {
  coordinate_sampler_prng_.SetCurrentIndex(index);
}

// Apply Gaussian filtering to the data.
double GetFilteredElement(const VideoFrameSampler& frame_sampler,
                          VideoFrameSampler::ChannelType channel,
                          int row,
                          int column,
                          double std_dev) {
  RTC_CHECK_GE(row, 0);
  RTC_CHECK_LT(row, frame_sampler.height(channel));
  RTC_CHECK_GE(column, 0);
  RTC_CHECK_LT(column, frame_sampler.width(channel));
  RTC_CHECK_GE(std_dev, 0.0);

  // `std_dev` being zero should ideally correspond to a very low QP value. In
  // this case even a noisy pixel should be able to be encoded and transmitted
  // correctly. Hence, the pixel value can be used as is.
  if (std_dev == 0.0) {
    return frame_sampler.GetSampleValue(channel, column, row);
  }

  int max_distance =
      std::ceil(std::sqrt(-2.0 * std::log(kCutoff) * std::pow(std_dev, 2.0))) -
      1;
  // In order to counteract unexpected distortions (such as noise), a lower
  // bound for blurring is introduced. This is done to reduce false positives
  // caused by these distortions.
  // False positives are decreased since for small `std_dev`s the quantization
  // is strong and would cut of many of the small continuous weights used for
  // robust comparision.
  max_distance = std::max(kLowerBoundKernelSize, max_distance);

  double element_sum = 0.0;
  double total_weight = 0.0;
  for (int r = std::max(row - max_distance, 0);
       r < std::min(row + max_distance + 1, frame_sampler.height(channel));
       ++r) {
    for (int c = std::max(column - max_distance, 0);
         c < std::min(column + max_distance + 1, frame_sampler.width(channel));
         ++c) {
      double weight =
          std::exp(-1.0 * (std::pow(row - r, 2) + std::pow(column - c, 2)) /
                   (2.0 * std::pow(std_dev, 2)));
      element_sum += frame_sampler.GetSampleValue(channel, c, r) * weight;
      total_weight += weight;
    }
  }

  // Take the rounding errors into consideration.
  return SafeClamp(element_sum / total_weight, 0.0, 255.0);
}

std::vector<FilteredSample> GetSampleValuesForFrame(
    const VideoFrame& frame,
    std::span<const HaltonFrameSampler::Coordinates> sample_coordinates,
    int scaled_width,
    int scaled_height,
    double std_dev_gaussian_blur) {
  // Validate input.
  if (frame.video_frame_buffer() == nullptr) {
    RTC_LOG(LS_WARNING) << "The framebuffer must not be nullptr";
    return {};
  }
  if (sample_coordinates.empty()) {
    RTC_LOG(LS_WARNING) << "There must be at least one coordinate provided";
    return {};
  }
  for (HaltonFrameSampler::Coordinates coordinate : sample_coordinates) {
    if (coordinate.column < 0.0 || coordinate.column >= 1.0 ||
        coordinate.row < 0.0 || coordinate.row >= 1.0) {
      RTC_LOG(LS_WARNING) << "The coordinates must be in [0,1): column="
                          << coordinate.column << ", row=" << coordinate.row
                          << ".\n";
      return {};
    }
  }
  if (scaled_width <= 0 || scaled_height <= 0) {
    RTC_LOG(LS_WARNING)
        << "The width and height to scale to must be positive: width="
        << scaled_width << ", height=" << scaled_height << ".\n";
    return {};
  }
  if (std_dev_gaussian_blur < 0.0) {
    RTC_LOG(LS_WARNING)
        << "The standard deviation for the Gaussian blur must not be negative: "
        << std_dev_gaussian_blur << ".\n";
    return {};
  }
  if (scaled_width > frame.width() || scaled_height > frame.height()) {
    RTC_LOG(LS_WARNING)
        << "Upscaling causes corruption. Therefore, only down-scaling is "
           "permissible.";
    return {};
  }

  VideoFrame scaled_frame = frame;
  std::unique_ptr<VideoFrameSampler> frame_sampler;
  if (scaled_width == frame.width() && scaled_height == frame.height()) {
    frame_sampler = VideoFrameSampler::Create(frame);
  } else {
    // Scale the frame to the desired resolution:
    // 1. Create a new buffer with the desired resolution.
    // 2. Scale the old buffer to the size of the new buffer.
    if (frame.video_frame_buffer()->type() == VideoFrameBuffer::Type::kNV12) {
      scoped_refptr<NV12Buffer> scaled_buffer =
          NV12Buffer::Create(scaled_width, scaled_height);
      // Set crop width/height to full width/height so this is only a scaling
      // operation, no cropping happening.
      scaled_buffer->CropAndScaleFrom(
          *frame.video_frame_buffer()->GetNV12(), /*offset_x=*/0,
          /*offset_y=*/0, /*crop_width=*/frame.width(),
          /*crop_height=*/frame.height());
      scaled_frame.set_video_frame_buffer(scaled_buffer);
    } else {
      scoped_refptr<I420BufferInterface> buffer =
          frame.video_frame_buffer()->ToI420ForInspection();
      if (buffer == nullptr) {
        RTC_LOG(LS_WARNING) << "Unable to convert frame to I420 format.";
        return {};
      }
      scoped_refptr<I420Buffer> scaled_buffer =
          I420Buffer::Create(scaled_width, scaled_height);
      scaled_buffer->ScaleFrom(*buffer);
      scaled_frame.set_video_frame_buffer(scaled_buffer);
    }
    frame_sampler = VideoFrameSampler::Create(scaled_frame);
  }
  if (frame_sampler == nullptr) {
    RTC_LOG(LS_WARNING) << "Unable to create frame sampler for buffer type "
                        << VideoFrameBufferTypeToString(
                               frame.video_frame_buffer()->type());
    return {};
  }

  // Treat the planes as if they would have the following 2-dimensional layout:
  // +------+---+
  // |      | U |
  // |  Y   +---+
  // |      | V |
  // +------+---+
  // where width:=(Y.width+U.width) and height:=Y.height.
  // When interpreting the 2D sample coordinates, we simply treat them
  // as if they were taken from the above layout. We then need to translate the
  // coordinates back to the corresponding plane's corresponding 2D coordinates.
  // Then we find the filtered value that corresponds to those coordinates.
  RTC_DCHECK_EQ(frame_sampler->width(VideoFrameSampler::ChannelType::U),
                frame_sampler->width(VideoFrameSampler::ChannelType::V))
      << "Chroma channels are expected to be equal in resolution.";
  int width_merged_planes =
      frame_sampler->width(VideoFrameSampler::ChannelType::Y) +
      frame_sampler->width(VideoFrameSampler::ChannelType::U);
  int height_merged_planes =
      frame_sampler->height(VideoFrameSampler::ChannelType::Y);
  // Fetch the sample value for all of the requested coordinates.
  std::vector<FilteredSample> filtered_samples;
  filtered_samples.reserve(sample_coordinates.size());
  for (HaltonFrameSampler::Coordinates coordinate : sample_coordinates) {
    // Scale the coordinates from [0,1) to [0,`width_merged_planes`) and
    // [0,`height_merged_planes`). Truncation is intentional.
    int column = coordinate.column * width_merged_planes;
    int row = coordinate.row * height_merged_planes;

    // Map to plane coordinates and fetch the value.
    double value_for_coordinate;
    if (column < frame_sampler->width(VideoFrameSampler::ChannelType::Y)) {
      // Y plane.
      value_for_coordinate =
          GetFilteredElement(*frame_sampler, VideoFrameSampler::ChannelType::Y,
                             row, column, std_dev_gaussian_blur);
      filtered_samples.push_back(
          {.value = value_for_coordinate, .plane = ImagePlane::kLuma});
    } else if (row < frame_sampler->height(VideoFrameSampler::ChannelType::U)) {
      // U plane.
      column -= frame_sampler->width(VideoFrameSampler::ChannelType::Y);
      value_for_coordinate =
          GetFilteredElement(*frame_sampler, VideoFrameSampler::ChannelType::U,
                             row, column, std_dev_gaussian_blur);
      filtered_samples.push_back(
          {.value = value_for_coordinate, .plane = ImagePlane::kChroma});
    } else {
      // V plane.
      column -= frame_sampler->width(VideoFrameSampler::ChannelType::Y);
      row -= frame_sampler->height(VideoFrameSampler::ChannelType::U);
      value_for_coordinate =
          GetFilteredElement(*frame_sampler, VideoFrameSampler::ChannelType::V,
                             row, column, std_dev_gaussian_blur);
      filtered_samples.push_back(
          {.value = value_for_coordinate, .plane = ImagePlane::kChroma});
    }
  }
  return filtered_samples;
}

[[deprecated]] std::vector<FilteredSample> GetSampleValuesForFrame(
    scoped_refptr<I420BufferInterface> i420_frame_buffer,
    std::span<const HaltonFrameSampler::Coordinates> sample_coordinates,
    int scaled_width,
    int scaled_height,
    double std_dev_gaussian_blur) {
  return GetSampleValuesForFrame(
      VideoFrame::Builder()
          .set_video_frame_buffer(std::move(i420_frame_buffer))
          .build(),
      sample_coordinates, scaled_width, scaled_height, std_dev_gaussian_blur);
}

}  // namespace webrtc
