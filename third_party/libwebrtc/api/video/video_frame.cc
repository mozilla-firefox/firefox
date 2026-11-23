/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/video/video_frame.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

#include "api/rtp_packet_infos.h"
#include "api/scoped_refptr.h"
#include "api/units/timestamp.h"
#include "api/video/color_space.h"
#include "api/video/video_content_type.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_rotation.h"
#include "rtc_base/checks.h"
#include "rtc_base/time_utils.h"

namespace webrtc {

void VideoFrame::UpdateRect::Union(const UpdateRect& other) {
  if (other.IsEmpty())
    return;
  if (IsEmpty()) {
    *this = other;
    return;
  }
  int right = std::max(offset_x + width, other.offset_x + other.width);
  int bottom = std::max(offset_y + height, other.offset_y + other.height);
  offset_x = std::min(offset_x, other.offset_x);
  offset_y = std::min(offset_y, other.offset_y);
  width = right - offset_x;
  height = bottom - offset_y;
  RTC_DCHECK_GT(width, 0);
  RTC_DCHECK_GT(height, 0);
}

void VideoFrame::UpdateRect::Intersect(const UpdateRect& other) {
  if (other.IsEmpty() || IsEmpty()) {
    MakeEmptyUpdate();
    return;
  }

  int right = std::min(offset_x + width, other.offset_x + other.width);
  int bottom = std::min(offset_y + height, other.offset_y + other.height);
  offset_x = std::max(offset_x, other.offset_x);
  offset_y = std::max(offset_y, other.offset_y);
  width = right - offset_x;
  height = bottom - offset_y;
  if (width <= 0 || height <= 0) {
    MakeEmptyUpdate();
  }
}

void VideoFrame::UpdateRect::MakeEmptyUpdate() {
  width = height = offset_x = offset_y = 0;
}

bool VideoFrame::UpdateRect::IsEmpty() const {
  return width == 0 && height == 0;
}

VideoFrame::UpdateRect VideoFrame::UpdateRect::ScaleWithFrame(
    int frame_width,
    int frame_height,
    int crop_x,
    int crop_y,
    int crop_width,
    int crop_height,
    int scaled_width,
    int scaled_height) const {
  RTC_DCHECK_GT(frame_width, 0);
  RTC_DCHECK_GT(frame_height, 0);

  RTC_DCHECK_GT(crop_width, 0);
  RTC_DCHECK_GT(crop_height, 0);

  RTC_DCHECK_LE(crop_width + crop_x, frame_width);
  RTC_DCHECK_LE(crop_height + crop_y, frame_height);

  RTC_DCHECK_GT(scaled_width, 0);
  RTC_DCHECK_GT(scaled_height, 0);

  // Check if update rect is out of the cropped area.
  if (offset_x + width < crop_x || offset_x > crop_x + crop_width ||
      offset_y + height < crop_y || offset_y > crop_y + crop_width) {
    return {.offset_x = 0, .offset_y = 0, .width = 0, .height = 0};
  }

  int x = offset_x - crop_x;
  int w = width;
  if (x < 0) {
    w += x;
    x = 0;
  }
  int y = offset_y - crop_y;
  int h = height;
  if (y < 0) {
    h += y;
    y = 0;
  }

  // Lower corner is rounded down.
  x = x * scaled_width / crop_width;
  y = y * scaled_height / crop_height;
  // Upper corner is rounded up.
  w = (w * scaled_width + crop_width - 1) / crop_width;
  h = (h * scaled_height + crop_height - 1) / crop_height;

  // Round to full 2x2 blocks due to possible subsampling in the pixel data.
  if (x % 2) {
    --x;
    ++w;
  }
  if (y % 2) {
    --y;
    ++h;
  }
  if (w % 2) {
    ++w;
  }
  if (h % 2) {
    ++h;
  }

  // Expand the update rect by 2 pixels in each direction to include any
  // possible scaling artifacts.
  if (scaled_width != crop_width || scaled_height != crop_height) {
    if (x > 0) {
      x -= 2;
      w += 2;
    }
    if (y > 0) {
      y -= 2;
      h += 2;
    }
    w += 2;
    h += 2;
  }

  // Ensure update rect is inside frame dimensions.
  if (x + w > scaled_width) {
    w = scaled_width - x;
  }
  if (y + h > scaled_height) {
    h = scaled_height - y;
  }
  RTC_DCHECK_GE(w, 0);
  RTC_DCHECK_GE(h, 0);
  if (w == 0 || h == 0) {
    w = 0;
    h = 0;
    x = 0;
    y = 0;
  }

  return {.offset_x = x, .offset_y = y, .width = w, .height = h};
}

VideoFrame::Builder::Builder() = default;

VideoFrame::Builder::~Builder() = default;

VideoFrame VideoFrame::Builder::build() {
  RTC_CHECK(video_frame_buffer_ != nullptr);
  return VideoFrame(id_, video_frame_buffer_, timestamp_us_,
                    presentation_timestamp_, reference_time_, timestamp_rtp_,
                    ntp_time_ms_, rotation_, color_space_, render_parameters_,
                    update_rect_, packet_infos_, repeat_mode_, content_type_);
}

VideoFrame::Builder& VideoFrame::Builder::set_video_frame_buffer(
    const scoped_refptr<VideoFrameBuffer>& buffer) {
  video_frame_buffer_ = buffer;
  return *this;
}

VideoFrame::Builder& VideoFrame::Builder::set_timestamp_ms(
    int64_t timestamp_ms) {
  timestamp_us_ = timestamp_ms * kNumMicrosecsPerMillisec;
  return *this;
}

VideoFrame::Builder& VideoFrame::Builder::set_timestamp_us(
    int64_t timestamp_us) {
  timestamp_us_ = timestamp_us;
  return *this;
}

VideoFrame::Builder& VideoFrame::Builder::set_presentation_timestamp(
    const std::optional<Timestamp>& presentation_timestamp) {
  presentation_timestamp_ = presentation_timestamp;
  return *this;
}

VideoFrame::Builder& VideoFrame::Builder::set_reference_time(
    const std::optional<Timestamp>& reference_time) {
  reference_time_ = reference_time;
  return *this;
}

VideoFrame::Builder& VideoFrame::Builder::set_rtp_timestamp(
    uint32_t rtp_timestamp) {
  timestamp_rtp_ = rtp_timestamp;
  return *this;
}

VideoFrame::Builder& VideoFrame::Builder::set_timestamp_rtp(
    uint32_t timestamp_rtp) {
  timestamp_rtp_ = timestamp_rtp;
  return *this;
}

VideoFrame::Builder& VideoFrame::Builder::set_ntp_time_ms(int64_t ntp_time_ms) {
  ntp_time_ms_ = ntp_time_ms;
  return *this;
}

VideoFrame::Builder& VideoFrame::Builder::set_rotation(VideoRotation rotation) {
  rotation_ = rotation;
  return *this;
}

VideoFrame::Builder& VideoFrame::Builder::set_color_space(
    const std::optional<ColorSpace>& color_space) {
  color_space_ = color_space;
  return *this;
}

VideoFrame::Builder& VideoFrame::Builder::set_color_space(
    const ColorSpace* color_space) {
  color_space_ = color_space ? std::make_optional(*color_space) : std::nullopt;
  return *this;
}

VideoFrame::Builder& VideoFrame::Builder::set_id(uint16_t id) {
  id_ = id;
  return *this;
}

VideoFrame::Builder& VideoFrame::Builder::set_update_rect(
    const std::optional<VideoFrame::UpdateRect>& update_rect) {
  update_rect_ = update_rect;
  return *this;
}

VideoFrame::Builder& VideoFrame::Builder::set_packet_infos(
    RtpPacketInfos packet_infos) {
  packet_infos_ = std::move(packet_infos);
  return *this;
}

VideoFrame::Builder& VideoFrame::Builder::set_repeat_mode(
    RepeatMode repeat_mode) {
  repeat_mode_ = repeat_mode;
  return *this;
}

VideoFrame::Builder& VideoFrame::Builder::set_is_repeat_frame(
    bool is_repeat_frame) {
  repeat_mode_ =
      is_repeat_frame ? RepeatMode::kQualityConvergence : RepeatMode::kNone;
  return *this;
}

VideoFrame::Builder& VideoFrame::Builder::set_content_type(
    VideoContentType content_type) {
  content_type_ = content_type;
  return *this;
}

VideoFrame::VideoFrame(const scoped_refptr<VideoFrameBuffer>& buffer,
                       VideoRotation rotation,
                       int64_t timestamp_us)
    : video_frame_buffer_(buffer),
      timestamp_rtp_(0),
      ntp_time_ms_(0),
      timestamp_us_(timestamp_us),
      rotation_(rotation),
      repeat_mode_(RepeatMode::kNone) {}

VideoFrame::VideoFrame(const scoped_refptr<VideoFrameBuffer>& buffer,
                       uint32_t timestamp_rtp,
                       int64_t render_time_ms,
                       VideoRotation rotation)
    : video_frame_buffer_(buffer),
      timestamp_rtp_(timestamp_rtp),
      ntp_time_ms_(0),
      timestamp_us_(render_time_ms * kNumMicrosecsPerMillisec),
      rotation_(rotation),
      repeat_mode_(RepeatMode::kNone) {
  RTC_DCHECK(buffer);
}

VideoFrame::~VideoFrame() = default;

VideoFrame::VideoFrame(const VideoFrame&) = default;
VideoFrame::VideoFrame(VideoFrame&&) = default;
VideoFrame& VideoFrame::operator=(const VideoFrame&) = default;
VideoFrame& VideoFrame::operator=(VideoFrame&&) = default;

int VideoFrame::width() const {
  return video_frame_buffer_ ? video_frame_buffer_->width() : 0;
}

int VideoFrame::height() const {
  return video_frame_buffer_ ? video_frame_buffer_->height() : 0;
}

uint32_t VideoFrame::size() const {
  return width() * height();
}

scoped_refptr<VideoFrameBuffer> VideoFrame::video_frame_buffer() const {
  return video_frame_buffer_;
}

void VideoFrame::set_video_frame_buffer(
    const scoped_refptr<VideoFrameBuffer>& buffer) {
  RTC_CHECK(buffer);
  video_frame_buffer_ = buffer;
}

int64_t VideoFrame::render_time_ms() const {
  return timestamp_us() / kNumMicrosecsPerMillisec;
}

}  // namespace webrtc
