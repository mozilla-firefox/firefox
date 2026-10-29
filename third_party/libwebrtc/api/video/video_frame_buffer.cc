/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/video/video_frame_buffer.h"

#include <cstddef>
#include <span>
#include <string>

#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/i422_buffer.h"
#include "api/video/i444_buffer.h"
#include "api/video/nv12_buffer.h"
#include "rtc_base/checks.h"

namespace webrtc {

scoped_refptr<VideoFrameBuffer> VideoFrameBuffer::CropAndScale(
    int offset_x,
    int offset_y,
    int crop_width,
    int crop_height,
    int scaled_width,
    int scaled_height) {
  scoped_refptr<I420BufferInterface> i420_buffer = this->ToI420();
  if (!i420_buffer) {
    return nullptr;
  }
  scoped_refptr<I420Buffer> result =
      I420Buffer::CreateOrNull(scaled_width, scaled_height);
  if (!result) {
    return nullptr;
  }
  result->CropAndScaleFrom(*i420_buffer, offset_x, offset_y, crop_width,
                           crop_height);
  return result;
}

void VideoFrameBuffer::PrepareMappedBufferAsync(
    size_t width,
    size_t height,
    scoped_refptr<PreparedFrameHandler> handler,
    size_t frame_identifier) {
  // Default implementation can't do any preparations,
  // so it just invokes the callback immediately.
  handler->OnFramePrepared(frame_identifier);
}

const I420BufferInterface* VideoFrameBuffer::GetI420() const {
  // Overridden by subclasses that can return an I420 buffer without any
  // conversion, in particular, I420BufferInterface.
  return nullptr;
}

const I420ABufferInterface* VideoFrameBuffer::GetI420A() const {
  RTC_CHECK(type() == Type::kI420A);
  return static_cast<const I420ABufferInterface*>(this);
}

const I444BufferInterface* VideoFrameBuffer::GetI444() const {
  RTC_CHECK(type() == Type::kI444);
  return static_cast<const I444BufferInterface*>(this);
}

const I422BufferInterface* VideoFrameBuffer::GetI422() const {
  RTC_CHECK(type() == Type::kI422);
  return static_cast<const I422BufferInterface*>(this);
}

const I010BufferInterface* VideoFrameBuffer::GetI010() const {
  RTC_CHECK(type() == Type::kI010);
  return static_cast<const I010BufferInterface*>(this);
}

const I210BufferInterface* VideoFrameBuffer::GetI210() const {
  RTC_CHECK(type() == Type::kI210);
  return static_cast<const I210BufferInterface*>(this);
}

const I410BufferInterface* VideoFrameBuffer::GetI410() const {
  RTC_CHECK(type() == Type::kI410);
  return static_cast<const I410BufferInterface*>(this);
}

const NV12BufferInterface* VideoFrameBuffer::GetNV12() const {
  RTC_CHECK(type() == Type::kNV12);
  return static_cast<const NV12BufferInterface*>(this);
}

scoped_refptr<I420BufferInterface> VideoFrameBuffer::ToI420ForInspection() {
  return ToI420();
}

scoped_refptr<VideoFrameBuffer> VideoFrameBuffer::GetMappedFrameBuffer(
    std::span<Type> /* types */) {
  RTC_CHECK(type() == Type::kNative);
  return nullptr;
}

std::string VideoFrameBuffer::storage_representation() const {
  return "?";
}

VideoFrameBuffer::Type I420BufferInterface::type() const {
  return Type::kI420;
}

const char* VideoFrameBufferTypeToString(VideoFrameBuffer::Type type) {
  switch (type) {
    case VideoFrameBuffer::Type::kNative:
      return "kNative";
    case VideoFrameBuffer::Type::kI420:
      return "kI420";
    case VideoFrameBuffer::Type::kI420A:
      return "kI420A";
    case VideoFrameBuffer::Type::kI444:
      return "kI444";
    case VideoFrameBuffer::Type::kI422:
      return "kI422";
    case VideoFrameBuffer::Type::kI010:
      return "kI010";
    case VideoFrameBuffer::Type::kI210:
      return "kI210";
    case VideoFrameBuffer::Type::kI410:
      return "kI410";
    case VideoFrameBuffer::Type::kNV12:
      return "kNV12";
    default:
      RTC_DCHECK_NOTREACHED();
  }
}

int I420BufferInterface::ChromaWidth() const {
  return (width() + 1) / 2;
}

int I420BufferInterface::ChromaHeight() const {
  return (height() + 1) / 2;
}

scoped_refptr<I420BufferInterface> I420BufferInterface::ToI420() {
  return scoped_refptr<I420BufferInterface>(this);
}

const I420BufferInterface* I420BufferInterface::GetI420() const {
  return this;
}

VideoFrameBuffer::Type I420ABufferInterface::type() const {
  return Type::kI420A;
}

VideoFrameBuffer::Type I444BufferInterface::type() const {
  return Type::kI444;
}

int I444BufferInterface::ChromaWidth() const {
  return width();
}

int I444BufferInterface::ChromaHeight() const {
  return height();
}

scoped_refptr<VideoFrameBuffer> I444BufferInterface::CropAndScale(
    int offset_x,
    int offset_y,
    int crop_width,
    int crop_height,
    int scaled_width,
    int scaled_height) {
  scoped_refptr<I444Buffer> result =
      I444Buffer::Create(scaled_width, scaled_height);
  result->CropAndScaleFrom(*this, offset_x, offset_y, crop_width, crop_height);
  return result;
}

VideoFrameBuffer::Type I422BufferInterface::type() const {
  return Type::kI422;
}

int I422BufferInterface::ChromaWidth() const {
  return (width() + 1) / 2;
}

int I422BufferInterface::ChromaHeight() const {
  return height();
}

scoped_refptr<VideoFrameBuffer> I422BufferInterface::CropAndScale(
    int offset_x,
    int offset_y,
    int crop_width,
    int crop_height,
    int scaled_width,
    int scaled_height) {
  scoped_refptr<I422Buffer> result =
      I422Buffer::Create(scaled_width, scaled_height);
  result->CropAndScaleFrom(*this, offset_x, offset_y, crop_width, crop_height);
  return result;
}

VideoFrameBuffer::Type I010BufferInterface::type() const {
  return Type::kI010;
}

int I010BufferInterface::ChromaWidth() const {
  return (width() + 1) / 2;
}

int I010BufferInterface::ChromaHeight() const {
  return (height() + 1) / 2;
}

VideoFrameBuffer::Type I210BufferInterface::type() const {
  return Type::kI210;
}

int I210BufferInterface::ChromaWidth() const {
  return (width() + 1) / 2;
}

int I210BufferInterface::ChromaHeight() const {
  return height();
}

VideoFrameBuffer::Type I410BufferInterface::type() const {
  return Type::kI410;
}

int I410BufferInterface::ChromaWidth() const {
  return width();
}

int I410BufferInterface::ChromaHeight() const {
  return height();
}

VideoFrameBuffer::Type NV12BufferInterface::type() const {
  return Type::kNV12;
}

int NV12BufferInterface::ChromaWidth() const {
  return (width() + 1) / 2;
}

int NV12BufferInterface::ChromaHeight() const {
  return (height() + 1) / 2;
}

scoped_refptr<VideoFrameBuffer> NV12BufferInterface::CropAndScale(
    int offset_x,
    int offset_y,
    int crop_width,
    int crop_height,
    int scaled_width,
    int scaled_height) {
  scoped_refptr<NV12Buffer> result =
      NV12Buffer::Create(scaled_width, scaled_height);
  result->CropAndScaleFrom(*this, offset_x, offset_y, crop_width, crop_height);
  return result;
}

void CheckValidDimensions(int width,
                          int height,
                          int stride_y,
                          int stride_u,
                          int stride_v) {
  RTC_CHECK_GT(width, 0);
  RTC_CHECK_GT(height, 0);
  RTC_CHECK_GE(stride_y, width);
  RTC_CHECK_GT(stride_u, 0);
  RTC_CHECK_GT(stride_v, 0);
}

}  // namespace webrtc
