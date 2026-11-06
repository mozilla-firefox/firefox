/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/frame_utils.h"

#include <stdio.h>
#include <string.h>

#include "api/video/i420_buffer.h"
#include "api/video/nv12_buffer.h"
#include "api/video/video_frame.h"

namespace webrtc {
namespace test {

bool EqualPlane(const uint8_t* data1,
                const uint8_t* data2,
                int stride1,
                int stride2,
                int width,
                int height) {
  for (int y = 0; y < height; ++y) {
    if (memcmp(data1, data2, width) != 0)
      return false;
    data1 += stride1;
    data2 += stride2;
  }
  return true;
}

bool FramesEqual(const webrtc::VideoFrame& f1, const webrtc::VideoFrame& f2) {
  if (f1.rtp_timestamp() != f2.rtp_timestamp() ||
      f1.ntp_time_ms() != f2.ntp_time_ms() ||
      f1.render_time_ms() != f2.render_time_ms()) {
    return false;
  }
  return FrameBufsEqual(f1.video_frame_buffer(), f2.video_frame_buffer());
}

bool FrameBufsEqual(const rtc::scoped_refptr<webrtc::VideoFrameBuffer>& f1,
                    const rtc::scoped_refptr<webrtc::VideoFrameBuffer>& f2) {
  if (f1 == f2) {
    return true;
  }
  // Exlude nullptr (except if both are nullptr, as above)
  if (!f1 || !f2) {
    return false;
  }

  if (f1->width() != f2->width() || f1->height() != f2->height() ||
      f1->type() != f2->type()) {
    return false;
  }

  rtc::scoped_refptr<webrtc::I420BufferInterface> f1_i420 = f1->ToI420();
  rtc::scoped_refptr<webrtc::I420BufferInterface> f2_i420 = f2->ToI420();
  return EqualPlane(f1_i420->DataY(), f2_i420->DataY(), f1_i420->StrideY(),
                    f2_i420->StrideY(), f1_i420->width(), f1_i420->height()) &&
         EqualPlane(f1_i420->DataU(), f2_i420->DataU(), f1_i420->StrideU(),
                    f2_i420->StrideU(), f1_i420->ChromaWidth(),
                    f1_i420->ChromaHeight()) &&
         EqualPlane(f1_i420->DataV(), f2_i420->DataV(), f1_i420->StrideV(),
                    f2_i420->StrideV(), f1_i420->ChromaWidth(),
                    f1_i420->ChromaHeight());
}

rtc::scoped_refptr<I420Buffer> ReadI420Buffer(int width, int height, FILE* f) {
  int half_width = (width + 1) / 2;
  rtc::scoped_refptr<I420Buffer> buffer(
      // Explicit stride, no padding between rows.
      I420Buffer::Create(width, height, width, half_width, half_width));
  size_t size_y = static_cast<size_t>(width) * height;
  size_t size_uv = static_cast<size_t>(half_width) * ((height + 1) / 2);

  if (fread(buffer->MutableDataY(), 1, size_y, f) < size_y)
    return nullptr;
  if (fread(buffer->MutableDataU(), 1, size_uv, f) < size_uv)
    return nullptr;
  if (fread(buffer->MutableDataV(), 1, size_uv, f) < size_uv)
    return nullptr;
  return buffer;
}

rtc::scoped_refptr<NV12Buffer> ReadNV12Buffer(int width, int height, FILE* f) {
  rtc::scoped_refptr<NV12Buffer> buffer(NV12Buffer::Create(width, height));
  size_t size_y = static_cast<size_t>(width) * height;
  size_t size_uv = static_cast<size_t>(width + width % 2) * ((height + 1) / 2);

  if (fread(buffer->MutableDataY(), 1, size_y, f) < size_y)
    return nullptr;
  if (fread(buffer->MutableDataUV(), 1, size_uv, f) < size_uv)
    return nullptr;
  return buffer;
}

}  // namespace test
}  // namespace webrtc
