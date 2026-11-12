/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "common_video/include/video_frame_buffer.h"

#include <cstdint>
#include <functional>
#include <utility>

#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame_buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/ref_counted_object.h"
#include "third_party/libyuv/include/libyuv/convert.h"

namespace webrtc {

namespace {

// Template to implement a wrapped buffer for a I4??BufferInterface.
template <typename Base>
class WrappedYuvBuffer : public Base {
 public:
  WrappedYuvBuffer(int width,
                   int height,
                   const uint8_t* y_plane,
                   int y_stride,
                   const uint8_t* u_plane,
                   int u_stride,
                   const uint8_t* v_plane,
                   int v_stride,
                   std::function<void()> no_longer_used)
      : width_(width),
        height_(height),
        y_plane_(y_plane),
        u_plane_(u_plane),
        v_plane_(v_plane),
        y_stride_(y_stride),
        u_stride_(u_stride),
        v_stride_(v_stride),
        no_longer_used_cb_(std::move(no_longer_used)) {}

  ~WrappedYuvBuffer() override { no_longer_used_cb_(); }

  int width() const override { return width_; }

  int height() const override { return height_; }

  const uint8_t* DataY() const override { return y_plane_; }

  const uint8_t* DataU() const override { return u_plane_; }

  const uint8_t* DataV() const override { return v_plane_; }

  int StrideY() const override { return y_stride_; }

  int StrideU() const override { return u_stride_; }

  int StrideV() const override { return v_stride_; }

 private:
  friend class RefCountedObject<WrappedYuvBuffer>;

  const int width_;
  const int height_;
  const uint8_t* const y_plane_;
  const uint8_t* const u_plane_;
  const uint8_t* const v_plane_;
  const int y_stride_;
  const int u_stride_;
  const int v_stride_;
  std::function<void()> no_longer_used_cb_;
};

// Template to implement a wrapped buffer for a I4??BufferInterface.
template <typename BaseWithA>
class WrappedYuvaBuffer : public WrappedYuvBuffer<BaseWithA> {
 public:
  WrappedYuvaBuffer(int width,
                    int height,
                    const uint8_t* y_plane,
                    int y_stride,
                    const uint8_t* u_plane,
                    int u_stride,
                    const uint8_t* v_plane,
                    int v_stride,
                    const uint8_t* a_plane,
                    int a_stride,
                    std::function<void()> no_longer_used)
      : WrappedYuvBuffer<BaseWithA>(width,
                                    height,
                                    y_plane,
                                    y_stride,
                                    u_plane,
                                    u_stride,
                                    v_plane,
                                    v_stride,
                                    std::move(no_longer_used)),
        a_plane_(a_plane),
        a_stride_(a_stride) {}

  const uint8_t* DataA() const override { return a_plane_; }
  int StrideA() const override { return a_stride_; }

 private:
  const uint8_t* const a_plane_;
  const int a_stride_;
};

class I444BufferBase : public I444BufferInterface {
 public:
  scoped_refptr<I420BufferInterface> ToI420() final;
};

scoped_refptr<I420BufferInterface> I444BufferBase::ToI420() {
  scoped_refptr<I420Buffer> i420_buffer = I420Buffer::Create(width(), height());
  if (libyuv::I444ToI420(DataY(), StrideY(), DataU(), StrideU(), DataV(),
                         StrideV(), i420_buffer->MutableDataY(),
                         i420_buffer->StrideY(), i420_buffer->MutableDataU(),
                         i420_buffer->StrideU(), i420_buffer->MutableDataV(),
                         i420_buffer->StrideV(), width(), height()) != 0) {
    return nullptr;
  }
  return i420_buffer;
}

class I422BufferBase : public I422BufferInterface {
 public:
  scoped_refptr<I420BufferInterface> ToI420() final;
};

scoped_refptr<I420BufferInterface> I422BufferBase::ToI420() {
  scoped_refptr<I420Buffer> i420_buffer = I420Buffer::Create(width(), height());
  if (libyuv::I422ToI420(DataY(), StrideY(), DataU(), StrideU(), DataV(),
                         StrideV(), i420_buffer->MutableDataY(),
                         i420_buffer->StrideY(), i420_buffer->MutableDataU(),
                         i420_buffer->StrideU(), i420_buffer->MutableDataV(),
                         i420_buffer->StrideV(), width(), height()) != 0) {
    return nullptr;
  }
  return i420_buffer;
}

// Template to implement a wrapped buffer for a PlanarYuv16BBuffer.
template <typename Base>
class WrappedYuv16BBuffer : public Base {
 public:
  WrappedYuv16BBuffer(int width,
                      int height,
                      const uint16_t* y_plane,
                      int y_stride,
                      const uint16_t* u_plane,
                      int u_stride,
                      const uint16_t* v_plane,
                      int v_stride,
                      std::function<void()> no_longer_used)
      : width_(width),
        height_(height),
        y_plane_(y_plane),
        u_plane_(u_plane),
        v_plane_(v_plane),
        y_stride_(y_stride),
        u_stride_(u_stride),
        v_stride_(v_stride),
        no_longer_used_cb_(std::move(no_longer_used)) {}

  ~WrappedYuv16BBuffer() override { no_longer_used_cb_(); }

  int width() const override { return width_; }

  int height() const override { return height_; }

  const uint16_t* DataY() const override { return y_plane_; }

  const uint16_t* DataU() const override { return u_plane_; }

  const uint16_t* DataV() const override { return v_plane_; }

  int StrideY() const override { return y_stride_; }

  int StrideU() const override { return u_stride_; }

  int StrideV() const override { return v_stride_; }

 private:
  friend class RefCountedObject<WrappedYuv16BBuffer>;

  const int width_;
  const int height_;
  const uint16_t* const y_plane_;
  const uint16_t* const u_plane_;
  const uint16_t* const v_plane_;
  const int y_stride_;
  const int u_stride_;
  const int v_stride_;
  std::function<void()> no_longer_used_cb_;
};

class I010BufferBase : public I010BufferInterface {
 public:
  scoped_refptr<I420BufferInterface> ToI420() final;
};

scoped_refptr<I420BufferInterface> I010BufferBase::ToI420() {
  scoped_refptr<I420Buffer> i420_buffer = I420Buffer::Create(width(), height());
  if (libyuv::I010ToI420(DataY(), StrideY(), DataU(), StrideU(), DataV(),
                         StrideV(), i420_buffer->MutableDataY(),
                         i420_buffer->StrideY(), i420_buffer->MutableDataU(),
                         i420_buffer->StrideU(), i420_buffer->MutableDataV(),
                         i420_buffer->StrideV(), width(), height()) != 0) {
    return nullptr;
  }
  return i420_buffer;
}

class I210BufferBase : public I210BufferInterface {
 public:
  scoped_refptr<I420BufferInterface> ToI420() final;
};

scoped_refptr<I420BufferInterface> I210BufferBase::ToI420() {
  scoped_refptr<I420Buffer> i420_buffer = I420Buffer::Create(width(), height());
  if (libyuv::I210ToI420(DataY(), StrideY(), DataU(), StrideU(), DataV(),
                         StrideV(), i420_buffer->MutableDataY(),
                         i420_buffer->StrideY(), i420_buffer->MutableDataU(),
                         i420_buffer->StrideU(), i420_buffer->MutableDataV(),
                         i420_buffer->StrideV(), width(), height()) != 0) {
    return nullptr;
  }
  return i420_buffer;
}

class I410BufferBase : public I410BufferInterface {
 public:
  scoped_refptr<I420BufferInterface> ToI420() final;
};

scoped_refptr<I420BufferInterface> I410BufferBase::ToI420() {
  scoped_refptr<I420Buffer> i420_buffer = I420Buffer::Create(width(), height());
  if (libyuv::I410ToI420(DataY(), StrideY(), DataU(), StrideU(), DataV(),
                         StrideV(), i420_buffer->MutableDataY(),
                         i420_buffer->StrideY(), i420_buffer->MutableDataU(),
                         i420_buffer->StrideU(), i420_buffer->MutableDataV(),
                         i420_buffer->StrideV(), width(), height()) != 0) {
    return nullptr;
  }
  return i420_buffer;
}

}  // namespace

scoped_refptr<I420BufferInterface> WrapI420Buffer(
    int width,
    int height,
    const uint8_t* y_plane,
    int y_stride,
    const uint8_t* u_plane,
    int u_stride,
    const uint8_t* v_plane,
    int v_stride,
    std::function<void()> no_longer_used) {
  return scoped_refptr<I420BufferInterface>(
      make_ref_counted<WrappedYuvBuffer<I420BufferInterface>>(
          width, height, y_plane, y_stride, u_plane, u_stride, v_plane,
          v_stride, std::move(no_longer_used)));
}

scoped_refptr<I420ABufferInterface> WrapI420ABuffer(
    int width,
    int height,
    const uint8_t* y_plane,
    int y_stride,
    const uint8_t* u_plane,
    int u_stride,
    const uint8_t* v_plane,
    int v_stride,
    const uint8_t* a_plane,
    int a_stride,
    std::function<void()> no_longer_used) {
  return scoped_refptr<I420ABufferInterface>(
      make_ref_counted<WrappedYuvaBuffer<I420ABufferInterface>>(
          width, height, y_plane, y_stride, u_plane, u_stride, v_plane,
          v_stride, a_plane, a_stride, std::move(no_longer_used)));
}

scoped_refptr<I422BufferInterface> WrapI422Buffer(
    int width,
    int height,
    const uint8_t* y_plane,
    int y_stride,
    const uint8_t* u_plane,
    int u_stride,
    const uint8_t* v_plane,
    int v_stride,
    std::function<void()> no_longer_used) {
  return scoped_refptr<I422BufferBase>(
      make_ref_counted<WrappedYuvBuffer<I422BufferBase>>(
          width, height, y_plane, y_stride, u_plane, u_stride, v_plane,
          v_stride, std::move(no_longer_used)));
}

scoped_refptr<I444BufferInterface> WrapI444Buffer(
    int width,
    int height,
    const uint8_t* y_plane,
    int y_stride,
    const uint8_t* u_plane,
    int u_stride,
    const uint8_t* v_plane,
    int v_stride,
    std::function<void()> no_longer_used) {
  return scoped_refptr<I444BufferInterface>(
      make_ref_counted<WrappedYuvBuffer<I444BufferBase>>(
          width, height, y_plane, y_stride, u_plane, u_stride, v_plane,
          v_stride, std::move(no_longer_used)));
}

scoped_refptr<PlanarYuvBuffer> WrapYuvBuffer(
    VideoFrameBuffer::Type type,
    int width,
    int height,
    const uint8_t* y_plane,
    int y_stride,
    const uint8_t* u_plane,
    int u_stride,
    const uint8_t* v_plane,
    int v_stride,
    std::function<void()> no_longer_used) {
  switch (type) {
    case VideoFrameBuffer::Type::kI420:
      return WrapI420Buffer(width, height, y_plane, y_stride, u_plane, u_stride,
                            v_plane, v_stride, std::move(no_longer_used));
    case VideoFrameBuffer::Type::kI422:
      return WrapI422Buffer(width, height, y_plane, y_stride, u_plane, u_stride,
                            v_plane, v_stride, std::move(no_longer_used));
    case VideoFrameBuffer::Type::kI444:
      return WrapI444Buffer(width, height, y_plane, y_stride, u_plane, u_stride,
                            v_plane, v_stride, std::move(no_longer_used));
    default:
      RTC_CHECK_NOTREACHED();
  }
}

scoped_refptr<I010BufferInterface> WrapI010Buffer(
    int width,
    int height,
    const uint16_t* y_plane,
    int y_stride,
    const uint16_t* u_plane,
    int u_stride,
    const uint16_t* v_plane,
    int v_stride,
    std::function<void()> no_longer_used) {
  return scoped_refptr<I010BufferInterface>(
      make_ref_counted<WrappedYuv16BBuffer<I010BufferBase>>(
          width, height, y_plane, y_stride, u_plane, u_stride, v_plane,
          v_stride, std::move(no_longer_used)));
}

scoped_refptr<I210BufferInterface> WrapI210Buffer(
    int width,
    int height,
    const uint16_t* y_plane,
    int y_stride,
    const uint16_t* u_plane,
    int u_stride,
    const uint16_t* v_plane,
    int v_stride,
    std::function<void()> no_longer_used) {
  return scoped_refptr<I210BufferInterface>(
      make_ref_counted<WrappedYuv16BBuffer<I210BufferBase>>(
          width, height, y_plane, y_stride, u_plane, u_stride, v_plane,
          v_stride, std::move(no_longer_used)));
}

scoped_refptr<I410BufferInterface> WrapI410Buffer(
    int width,
    int height,
    const uint16_t* y_plane,
    int y_stride,
    const uint16_t* u_plane,
    int u_stride,
    const uint16_t* v_plane,
    int v_stride,
    std::function<void()> no_longer_used) {
  return scoped_refptr<I410BufferInterface>(
      make_ref_counted<WrappedYuv16BBuffer<I410BufferBase>>(
          width, height, y_plane, y_stride, u_plane, u_stride, v_plane,
          v_stride, std::move(no_longer_used)));
}

}  // namespace webrtc
