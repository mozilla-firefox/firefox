/*
 * Copyright 2025 The WebRTC project authors. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file in the root of the source
 * tree. An additional intellectual property rights grant can be found
 * in the file PATENTS.  All contributing project authors may
 * be found in the AUTHORS file in the root of the source tree.
 */

#include "video/corruption_detection/video_frame_sampler.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "api/scoped_refptr.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "rtc_base/checks.h"

namespace webrtc {

class I420FrameSampler : public VideoFrameSampler {
 public:
  explicit I420FrameSampler(scoped_refptr<const I420BufferInterface> buffer)
      : buffer_(std::move(buffer)) {}

  uint8_t GetSampleValue(ChannelType channel, int col, int row) const override {
    RTC_DCHECK_GE(col, 0);
    RTC_DCHECK_GE(row, 0);
    switch (channel) {
      case ChannelType::Y:
        RTC_DCHECK_LT(col, width(ChannelType::Y));
        RTC_DCHECK_LT(row, height(ChannelType::Y));
        return buffer_->DataY()[row * buffer_->StrideY() + col];
      case ChannelType::U:
        RTC_DCHECK_LT(col, width(ChannelType::U));
        RTC_DCHECK_LT(row, height(ChannelType::U));
        return buffer_->DataU()[row * buffer_->StrideU() + col];
      case ChannelType::V:
        RTC_DCHECK_LT(col, width(ChannelType::V));
        RTC_DCHECK_LT(row, height(ChannelType::V));
        return buffer_->DataV()[row * buffer_->StrideV() + col];
    }
  }

  int width(ChannelType channel) const override {
    switch (channel) {
      case ChannelType::Y:
        return buffer_->width();
      case ChannelType::U:
      case ChannelType::V:
        return buffer_->ChromaWidth();
    }
  }

  int height(ChannelType channel) const override {
    switch (channel) {
      case ChannelType::Y:
        return buffer_->height();
      case ChannelType::U:
      case ChannelType::V:
        return buffer_->ChromaHeight();
    }
  }

 private:
  const scoped_refptr<const I420BufferInterface> buffer_;
};

class NV12FrameSampler : public VideoFrameSampler {
 public:
  explicit NV12FrameSampler(scoped_refptr<const NV12BufferInterface> buffer)
      : buffer_(std::move(buffer)) {}

  uint8_t GetSampleValue(ChannelType channel, int col, int row) const override {
    RTC_DCHECK_GE(col, 0);
    RTC_DCHECK_GE(row, 0);
    switch (channel) {
      case ChannelType::Y:
        RTC_DCHECK_LT(col, width(ChannelType::Y));
        RTC_DCHECK_LT(row, height(ChannelType::Y));
        return buffer_->DataY()[row * buffer_->StrideY() + col];
      case ChannelType::U:
        RTC_DCHECK_LT(col, width(ChannelType::U));
        RTC_DCHECK_LT(row, height(ChannelType::U));
        return buffer_->DataUV()[row * buffer_->StrideUV() + (col * 2)];
      case ChannelType::V:
        RTC_DCHECK_LT(col, width(ChannelType::V));
        RTC_DCHECK_LT(row, height(ChannelType::V));
        return buffer_->DataUV()[row * buffer_->StrideUV() + (col * 2) + 1];
    }
  }

  int width(ChannelType channel) const override {
    switch (channel) {
      case ChannelType::Y:
        return buffer_->width();
      case ChannelType::U:
      case ChannelType::V:
        return buffer_->ChromaWidth();
    }
  }

  int height(ChannelType channel) const override {
    switch (channel) {
      case ChannelType::Y:
        return buffer_->height();
      case ChannelType::U:
      case ChannelType::V:
        return buffer_->ChromaHeight();
    }
  }

 private:
  const scoped_refptr<const NV12BufferInterface> buffer_;
};

std::unique_ptr<VideoFrameSampler> VideoFrameSampler::Create(
    const VideoFrame& frame) {
  if (frame.video_frame_buffer() == nullptr) {
    return nullptr;
  }
  switch (frame.video_frame_buffer()->type()) {
    case VideoFrameBuffer::Type::kNV12: {
      scoped_refptr<const NV12BufferInterface> buffer(
          frame.video_frame_buffer()->GetNV12());
      if (buffer == nullptr) {
        return nullptr;
      }
      return std::make_unique<NV12FrameSampler>(std::move(buffer));
    }
    case VideoFrameBuffer::Type::kI420:
    case VideoFrameBuffer::Type::kI420A: {
      // Native I420 and I420A are used directly (Alpha channel ignored).
      scoped_refptr<const I420BufferInterface> buffer(
          frame.video_frame_buffer()->GetI420());
      if (buffer == nullptr) {
        return nullptr;
      }
      return std::make_unique<I420FrameSampler>(std::move(buffer));
    }
    default: {
      // Conversion and copy to I420 from some other format.
      scoped_refptr<const I420BufferInterface> buffer =
          frame.video_frame_buffer()->ToI420ForInspection();
      if (buffer == nullptr) {
        return nullptr;
      }
      return std::make_unique<I420FrameSampler>(std::move(buffer));
    }
  }
}

}  // namespace webrtc
