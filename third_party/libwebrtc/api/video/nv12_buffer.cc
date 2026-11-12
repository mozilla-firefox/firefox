/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/video/nv12_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame_buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/memory/aligned_malloc.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/libyuv/include/libyuv/convert_from.h"
#include "third_party/libyuv/include/libyuv/scale.h"

namespace webrtc {

namespace {

const int kBufferAlignment = 64;

int NV12DataSize(int width, int height, int stride_y, int stride_uv) {
  CheckValidDimensions(width, height, stride_y, stride_uv, stride_uv);
  int64_t h = height, y = stride_y, uv = stride_uv;
  return checked_cast<int>(y * h + uv * ((h + 1) / 2));
}

}  // namespace

NV12Buffer::NV12Buffer(int width, int height)
    : NV12Buffer(width, height, width, width + width % 2) {}

NV12Buffer::NV12Buffer(int width, int height, int stride_y, int stride_uv)
    : width_(width),
      height_(height),
      stride_y_(stride_y),
      stride_uv_(stride_uv),
      data_(static_cast<uint8_t*>(
          AlignedMalloc(NV12DataSize(width, height, stride_y, stride_uv),
                        kBufferAlignment))) {
  RTC_DCHECK_GE(stride_uv, width + width % 2);
}

NV12Buffer::~NV12Buffer() = default;

// static
scoped_refptr<NV12Buffer> NV12Buffer::Create(int width, int height) {
  return make_ref_counted<NV12Buffer>(width, height);
}

// static
scoped_refptr<NV12Buffer> NV12Buffer::Create(int width,
                                             int height,
                                             int stride_y,
                                             int stride_uv) {
  return make_ref_counted<NV12Buffer>(width, height, stride_y, stride_uv);
}

// static
scoped_refptr<NV12Buffer> NV12Buffer::Copy(
    const I420BufferInterface& i420_buffer) {
  scoped_refptr<NV12Buffer> buffer =
      NV12Buffer::Create(i420_buffer.width(), i420_buffer.height());
  int res = libyuv::I420ToNV12(
      i420_buffer.DataY(), i420_buffer.StrideY(), i420_buffer.DataU(),
      i420_buffer.StrideU(), i420_buffer.DataV(), i420_buffer.StrideV(),
      buffer->MutableDataY(), buffer->StrideY(), buffer->MutableDataUV(),
      buffer->StrideUV(), buffer->width(), buffer->height());
  RTC_CHECK_EQ(res, 0);
  return buffer;
}

scoped_refptr<I420BufferInterface> NV12Buffer::ToI420() {
  scoped_refptr<I420Buffer> i420_buffer = I420Buffer::Create(width(), height());
  if (libyuv::NV12ToI420(DataY(), StrideY(), DataUV(), StrideUV(),
                         i420_buffer->MutableDataY(), i420_buffer->StrideY(),
                         i420_buffer->MutableDataU(), i420_buffer->StrideU(),
                         i420_buffer->MutableDataV(), i420_buffer->StrideV(),
                         width(), height()) != 0) {
    return nullptr;
  }
  return i420_buffer;
}

int NV12Buffer::width() const {
  return width_;
}
int NV12Buffer::height() const {
  return height_;
}

int NV12Buffer::StrideY() const {
  return stride_y_;
}
int NV12Buffer::StrideUV() const {
  return stride_uv_;
}

const uint8_t* NV12Buffer::DataY() const {
  return data_.get();
}

const uint8_t* NV12Buffer::DataUV() const {
  return data_.get() + UVOffset();
}

uint8_t* NV12Buffer::MutableDataY() {
  return data_.get();
}

uint8_t* NV12Buffer::MutableDataUV() {
  return data_.get() + UVOffset();
}

size_t NV12Buffer::UVOffset() const {
  return stride_y_ * height_;
}

void NV12Buffer::InitializeData() {
  memset(data_.get(), 0, NV12DataSize(width_, height_, stride_y_, stride_uv_));
}

void NV12Buffer::CropAndScaleFrom(const NV12BufferInterface& src,
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

  const uint8_t* y_plane = src.DataY() + src.StrideY() * offset_y + offset_x;
  const uint8_t* uv_plane =
      src.DataUV() + src.StrideUV() * uv_offset_y + uv_offset_x * 2;

  int res = libyuv::NV12Scale(y_plane, src.StrideY(), uv_plane, src.StrideUV(),
                              crop_width, crop_height, MutableDataY(),
                              StrideY(), MutableDataUV(), StrideUV(), width(),
                              height(), libyuv::kFilterBox);

  RTC_CHECK_EQ(res, 0);
}

}  // namespace webrtc
