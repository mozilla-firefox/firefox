/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "api/video/i010_buffer.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>

#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_rotation.h"
#include "rtc_base/checks.h"
#include "rtc_base/memory/aligned_malloc.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/libyuv/include/libyuv/convert_from.h"
#include "third_party/libyuv/include/libyuv/rotate.h"
#include "third_party/libyuv/include/libyuv/scale.h"

// Aligning pointer to 64 bytes for improved performance, e.g. use SIMD.
static const int kBufferAlignment = 64;
static const int kBytesPerPixel = 2;

namespace webrtc {

namespace {

int I010DataSize(int width,
                 int height,
                 int stride_y,
                 int stride_u,
                 int stride_v) {
  CheckValidDimensions(width, height, stride_y, stride_u, stride_v);
  int64_t h = height, y = stride_y, u = stride_u, v = stride_v;
  return checked_cast<int>(kBytesPerPixel * (y * h + (u + v) * ((h + 1) / 2)));
}

}  // namespace

I010Buffer::I010Buffer(int width,
                       int height,
                       int stride_y,
                       int stride_u,
                       int stride_v)
    : width_(width),
      height_(height),
      stride_y_(stride_y),
      stride_u_(stride_u),
      stride_v_(stride_v),
      data_(static_cast<uint16_t*>(AlignedMalloc(
          I010DataSize(width, height, stride_y, stride_u, stride_v),
          kBufferAlignment))) {
  RTC_DCHECK_GE(stride_u, (width + 1) / 2);
  RTC_DCHECK_GE(stride_v, (width + 1) / 2);
}

I010Buffer::~I010Buffer() {}

// static
scoped_refptr<I010Buffer> I010Buffer::Create(int width, int height) {
  return make_ref_counted<I010Buffer>(width, height, width, (width + 1) / 2,
                                      (width + 1) / 2);
}

// static
scoped_refptr<I010Buffer> I010Buffer::Copy(const I010BufferInterface& source) {
  const int width = source.width();
  const int height = source.height();
  scoped_refptr<I010Buffer> buffer = Create(width, height);
  int res = libyuv::I010Copy(
      source.DataY(), source.StrideY(), source.DataU(), source.StrideU(),
      source.DataV(), source.StrideV(), buffer->MutableDataY(),
      buffer->StrideY(), buffer->MutableDataU(), buffer->StrideU(),
      buffer->MutableDataV(), buffer->StrideV(), width, height);
  RTC_CHECK_EQ(res, 0);

  return buffer;
}

// static
scoped_refptr<I010Buffer> I010Buffer::Copy(const I420BufferInterface& source) {
  const int width = source.width();
  const int height = source.height();
  scoped_refptr<I010Buffer> buffer = Create(width, height);
  int res = libyuv::I420ToI010(
      source.DataY(), source.StrideY(), source.DataU(), source.StrideU(),
      source.DataV(), source.StrideV(), buffer->MutableDataY(),
      buffer->StrideY(), buffer->MutableDataU(), buffer->StrideU(),
      buffer->MutableDataV(), buffer->StrideV(), width, height);
  RTC_CHECK_EQ(res, 0);

  return buffer;
}

// static
scoped_refptr<I010Buffer> I010Buffer::Rotate(const I010BufferInterface& src,
                                             VideoRotation rotation) {
  if (rotation == kVideoRotation_0)
    return Copy(src);

  RTC_CHECK(src.DataY());
  RTC_CHECK(src.DataU());
  RTC_CHECK(src.DataV());
  int rotated_width = src.width();
  int rotated_height = src.height();
  if (rotation == kVideoRotation_90 || rotation == kVideoRotation_270) {
    std::swap(rotated_width, rotated_height);
  }

  scoped_refptr<I010Buffer> buffer = Create(rotated_width, rotated_height);

  int res = libyuv::I010Rotate(
      src.DataY(), src.StrideY(), src.DataU(), src.StrideU(), src.DataV(),
      src.StrideV(), buffer->MutableDataY(), buffer->StrideY(),
      buffer->MutableDataU(), buffer->StrideU(), buffer->MutableDataV(),
      buffer->StrideV(), src.width(), src.height(),
      static_cast<libyuv::RotationMode>(rotation));
  RTC_CHECK_EQ(res, 0);

  return buffer;
}

scoped_refptr<I420BufferInterface> I010Buffer::ToI420() {
  scoped_refptr<I420Buffer> i420_buffer = I420Buffer::Create(width(), height());
  int res = libyuv::I010ToI420(
      DataY(), StrideY(), DataU(), StrideU(), DataV(), StrideV(),
      i420_buffer->MutableDataY(), i420_buffer->StrideY(),
      i420_buffer->MutableDataU(), i420_buffer->StrideU(),
      i420_buffer->MutableDataV(), i420_buffer->StrideV(), width(), height());
  if (res != 0) {
    return nullptr;
  }

  return i420_buffer;
}

void I010Buffer::InitializeData() {
  std::ranges::fill(
      std::span(data_.get(),
                I010DataSize(width_, height_, stride_y_, stride_u_, stride_v_) /
                    kBytesPerPixel),
      0);
}

int I010Buffer::width() const {
  return width_;
}

int I010Buffer::height() const {
  return height_;
}

const uint16_t* I010Buffer::DataY() const {
  return data_.get();
}
const uint16_t* I010Buffer::DataU() const {
  return data_.get() + stride_y_ * height_;
}
const uint16_t* I010Buffer::DataV() const {
  return data_.get() + stride_y_ * height_ + stride_u_ * ((height_ + 1) / 2);
}

int I010Buffer::StrideY() const {
  return stride_y_;
}
int I010Buffer::StrideU() const {
  return stride_u_;
}
int I010Buffer::StrideV() const {
  return stride_v_;
}

uint16_t* I010Buffer::MutableDataY() {
  return const_cast<uint16_t*>(DataY());
}
uint16_t* I010Buffer::MutableDataU() {
  return const_cast<uint16_t*>(DataU());
}
uint16_t* I010Buffer::MutableDataV() {
  return const_cast<uint16_t*>(DataV());
}

void I010Buffer::CropAndScaleFrom(const I010BufferInterface& src,
                                  int offset_x,
                                  int offset_y,
                                  int crop_width,
                                  int crop_height) {
  RTC_CHECK_LE(crop_width, src.width());
  RTC_CHECK_LE(crop_height, src.height());
  RTC_CHECK_LE(crop_width + offset_x, src.width());
  RTC_CHECK_LE(crop_height + offset_y, src.height());
  RTC_CHECK_GE(offset_x, 0);
  RTC_CHECK_GE(offset_y, 0);

  // Make sure offset is even so that u/v plane becomes aligned.
  const int uv_offset_x = offset_x / 2;
  const int uv_offset_y = offset_y / 2;
  offset_x = uv_offset_x * 2;
  offset_y = uv_offset_y * 2;

  const uint16_t* y_plane = src.DataY() + src.StrideY() * offset_y + offset_x;
  const uint16_t* u_plane =
      src.DataU() + src.StrideU() * uv_offset_y + uv_offset_x;
  const uint16_t* v_plane =
      src.DataV() + src.StrideV() * uv_offset_y + uv_offset_x;
  int res = libyuv::I420Scale_16(
      y_plane, src.StrideY(), u_plane, src.StrideU(), v_plane, src.StrideV(),
      crop_width, crop_height, MutableDataY(), StrideY(), MutableDataU(),
      StrideU(), MutableDataV(), StrideV(), width(), height(),
      libyuv::kFilterBox);

  RTC_CHECK_EQ(res, 0);
}

void I010Buffer::ScaleFrom(const I010BufferInterface& src) {
  CropAndScaleFrom(src, 0, 0, src.width(), src.height());
}

}  // namespace webrtc
