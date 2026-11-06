/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/video/video_frame.h"

#include <math.h>
#include <string.h>

#include "api/video/i010_buffer.h"
#include "api/video/i210_buffer.h"
#include "api/video/i410_buffer.h"
#include "api/video/i420_buffer.h"
#include "api/video/i422_buffer.h"
#include "api/video/i444_buffer.h"
#include "api/video/nv12_buffer.h"
#include "rtc_base/time_utils.h"
#include "test/fake_texture_frame.h"
#include "test/frame_utils.h"
#include "test/gtest.h"

namespace webrtc {

namespace {

struct SubSampling {
  int x;
  int y;
};

SubSampling SubSamplingForType(VideoFrameBuffer::Type type) {
  switch (type) {
    case VideoFrameBuffer::Type::kI420:
      return {.x = 2, .y = 2};
    case VideoFrameBuffer::Type::kI420A:
      return {.x = 2, .y = 2};
    case VideoFrameBuffer::Type::kI422:
      return {.x = 2, .y = 1};
    case VideoFrameBuffer::Type::kI444:
      return {.x = 1, .y = 1};
    case VideoFrameBuffer::Type::kI010:
      return {.x = 2, .y = 2};
    case VideoFrameBuffer::Type::kI210:
      return {.x = 2, .y = 1};
    case VideoFrameBuffer::Type::kI410:
      return {.x = 1, .y = 1};
    default:
      return {};
  }
}

// Helper function to create a buffer and fill it with a gradient for
// PlanarYuvBuffer based buffers.
template <class T>
rtc::scoped_refptr<T> CreateGradient(int width, int height) {
  rtc::scoped_refptr<T> buffer(T::Create(width, height));
  // Initialize with gradient, Y = 128(x/w + y/h), U = 256 x/w, V = 256 y/h
  for (int x = 0; x < width; x++) {
    for (int y = 0; y < height; y++) {
      buffer->MutableDataY()[x + y * width] =
          128 * (x * height + y * width) / (width * height);
    }
  }
  int chroma_width = buffer->ChromaWidth();
  int chroma_height = buffer->ChromaHeight();
  for (int x = 0; x < chroma_width; x++) {
    for (int y = 0; y < chroma_height; y++) {
      buffer->MutableDataU()[x + y * chroma_width] =
          255 * x / (chroma_width - 1);
      buffer->MutableDataV()[x + y * chroma_width] =
          255 * y / (chroma_height - 1);
    }
  }
  return buffer;
}

// Helper function to create a buffer and fill it with a gradient.
rtc::scoped_refptr<NV12BufferInterface> CreateNV12Gradient(int width,
                                                           int height) {
  rtc::scoped_refptr<NV12Buffer> buffer(NV12Buffer::Create(width, height));
  // Initialize with gradient, Y = 128(x/w + y/h), U = 256 x/w, V = 256 y/h
  for (int x = 0; x < width; x++) {
    for (int y = 0; y < height; y++) {
      buffer->MutableDataY()[x + y * width] =
          128 * (x * height + y * width) / (width * height);
    }
  }
  int chroma_width = buffer->ChromaWidth();
  int chroma_height = buffer->ChromaHeight();
  for (int x = 0; x < chroma_width; x++) {
    for (int y = 0; y < chroma_height; y++) {
      buffer->MutableDataUV()[x * 2 + y * buffer->StrideUV()] =
          255 * x / (chroma_width - 1);
      buffer->MutableDataUV()[x * 2 + 1 + y * buffer->StrideUV()] =
          255 * y / (chroma_height - 1);
    }
  }
  return buffer;
}

// The offsets and sizes describe the rectangle extracted from the
// original (gradient) frame, in relative coordinates where the
// original frame correspond to the unit square, 0.0 <= x, y < 1.0.
template <class T>
void CheckCrop(const T& frame,
               double offset_x,
               double offset_y,
               double rel_width,
               double rel_height) {
  int width = frame.width();
  int height = frame.height();

  SubSampling plane_divider = SubSamplingForType(frame.type());

  // Check that pixel values in the corners match the gradient used
  // for initialization.
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      // Pixel coordinates of the corner.
      int x = i * (width - 1);
      int y = j * (height - 1);
      // Relative coordinates, range 0.0 - 1.0 correspond to the
      // size of the uncropped input frame.
      double orig_x = offset_x + i * rel_width;
      double orig_y = offset_y + j * rel_height;

      EXPECT_NEAR(frame.DataY()[x + y * frame.StrideY()] / 256.0,
                  (orig_x + orig_y) / 2, 0.02);
      EXPECT_NEAR(frame.DataU()[x / plane_divider.x +
                                (y / plane_divider.y) * frame.StrideU()] /
                      256.0,
                  orig_x, 0.02);
      EXPECT_NEAR(frame.DataV()[x / plane_divider.x +
                                (y / plane_divider.y) * frame.StrideV()] /
                      256.0,
                  orig_y, 0.02);
    }
  }
}

template <class T>
void CheckRotate(int width,
                 int height,
                 webrtc::VideoRotation rotation,
                 const T& rotated) {
  int rotated_width = width;
  int rotated_height = height;

  if (rotation == kVideoRotation_90 || rotation == kVideoRotation_270) {
    std::swap(rotated_width, rotated_height);
  }
  EXPECT_EQ(rotated_width, rotated.width());
  EXPECT_EQ(rotated_height, rotated.height());

  // Clock-wise order (with 0,0 at top-left)
  const struct {
    int x;
    int y;
  } corners[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  // Corresponding corner colors of the frame produced by CreateGradient.
  const struct {
    int y;
    int u;
    int v;
  } colors[] = {{0, 0, 0}, {127, 255, 0}, {255, 255, 255}, {127, 0, 255}};
  int corner_offset = static_cast<int>(rotation) / 90;

  SubSampling plane_divider = SubSamplingForType(rotated.type());

  for (int i = 0; i < 4; i++) {
    int j = (i + corner_offset) % 4;
    int x = corners[j].x * (rotated_width - 1);
    int y = corners[j].y * (rotated_height - 1);
    EXPECT_EQ(colors[i].y, rotated.DataY()[x + y * rotated.StrideY()]);
    if (rotated.type() == VideoFrameBuffer::Type::kI422 ||
        rotated.type() == VideoFrameBuffer::Type::kI210) {
      EXPECT_NEAR(colors[i].u,
                  rotated.DataU()[(x / plane_divider.x) +
                                  (y / plane_divider.y) * rotated.StrideU()],
                  1);
      EXPECT_NEAR(colors[i].v,
                  rotated.DataV()[(x / plane_divider.x) +
                                  (y / plane_divider.y) * rotated.StrideV()],
                  1);
    } else {
      EXPECT_EQ(colors[i].u,
                rotated.DataU()[(x / plane_divider.x) +
                                (y / plane_divider.y) * rotated.StrideU()]);
      EXPECT_EQ(colors[i].v,
                rotated.DataV()[(x / plane_divider.x) +
                                (y / plane_divider.y) * rotated.StrideV()]);
    }
  }
}

}  // namespace

TEST(TestVideoFrame, WidthHeightValues) {
  VideoFrame frame =
      VideoFrame::Builder()
          .set_video_frame_buffer(I420Buffer::Create(10, 10, 10, 14, 90))
          .set_rotation(webrtc::kVideoRotation_0)
          .set_timestamp_ms(789)
          .build();
  const int valid_value = 10;
  EXPECT_EQ(valid_value, frame.width());
  EXPECT_EQ(valid_value, frame.height());
  frame.set_rtp_timestamp(123u);
  EXPECT_EQ(123u, frame.rtp_timestamp());
  frame.set_ntp_time_ms(456);
  EXPECT_EQ(456, frame.ntp_time_ms());
  EXPECT_EQ(789, frame.render_time_ms());
}

TEST(TestVideoFrame, ShallowCopy) {
  uint32_t timestamp = 1;
  int64_t ntp_time_ms = 2;
  int64_t timestamp_us = 3;
  int stride_y = 15;
  int stride_u = 10;
  int stride_v = 10;
  int width = 15;
  int height = 15;

  const int kSizeY = 400;
  const int kSizeU = 100;
  const int kSizeV = 100;
  const VideoRotation kRotation = kVideoRotation_270;
  uint8_t buffer_y[kSizeY];
  uint8_t buffer_u[kSizeU];
  uint8_t buffer_v[kSizeV];
  memset(buffer_y, 16, kSizeY);
  memset(buffer_u, 8, kSizeU);
  memset(buffer_v, 4, kSizeV);

  VideoFrame frame1 = VideoFrame::Builder()
                          .set_video_frame_buffer(I420Buffer::Copy(
                              width, height, buffer_y, stride_y, buffer_u,
                              stride_u, buffer_v, stride_v))
                          .set_rotation(kRotation)
                          .set_timestamp_us(0)
                          .build();
  frame1.set_rtp_timestamp(timestamp);
  frame1.set_ntp_time_ms(ntp_time_ms);
  frame1.set_timestamp_us(timestamp_us);
  VideoFrame frame2(frame1);

  EXPECT_EQ(frame1.video_frame_buffer(), frame2.video_frame_buffer());
  const webrtc::I420BufferInterface* yuv1 =
      frame1.video_frame_buffer()->GetI420();
  const webrtc::I420BufferInterface* yuv2 =
      frame2.video_frame_buffer()->GetI420();
  EXPECT_EQ(yuv1->DataY(), yuv2->DataY());
  EXPECT_EQ(yuv1->DataU(), yuv2->DataU());
  EXPECT_EQ(yuv1->DataV(), yuv2->DataV());

  EXPECT_EQ(frame2.rtp_timestamp(), frame1.rtp_timestamp());
  EXPECT_EQ(frame2.ntp_time_ms(), frame1.ntp_time_ms());
  EXPECT_EQ(frame2.timestamp_us(), frame1.timestamp_us());
  EXPECT_EQ(frame2.rotation(), frame1.rotation());

  frame2.set_rtp_timestamp(timestamp + 1);
  frame2.set_ntp_time_ms(ntp_time_ms + 1);
  frame2.set_timestamp_us(timestamp_us + 1);
  frame2.set_rotation(kVideoRotation_90);

  EXPECT_NE(frame2.rtp_timestamp(), frame1.rtp_timestamp());
  EXPECT_NE(frame2.ntp_time_ms(), frame1.ntp_time_ms());
  EXPECT_NE(frame2.timestamp_us(), frame1.timestamp_us());
  EXPECT_NE(frame2.rotation(), frame1.rotation());
}

TEST(TestVideoFrame, TextureInitialValues) {
  VideoFrame frame = test::FakeNativeBuffer::CreateFrame(
      640, 480, 100, 10, webrtc::kVideoRotation_0);
  EXPECT_EQ(640, frame.width());
  EXPECT_EQ(480, frame.height());
  EXPECT_EQ(100u, frame.rtp_timestamp());
  EXPECT_EQ(10, frame.render_time_ms());
  ASSERT_TRUE(frame.video_frame_buffer() != nullptr);
  EXPECT_TRUE(frame.video_frame_buffer()->type() ==
              VideoFrameBuffer::Type::kNative);

  frame.set_rtp_timestamp(200);
  EXPECT_EQ(200u, frame.rtp_timestamp());
  frame.set_timestamp_us(20);
  EXPECT_EQ(20, frame.timestamp_us());
}

template <typename T>
class TestPlanarYuvBuffer : public ::testing::Test {};
TYPED_TEST_SUITE_P(TestPlanarYuvBuffer);

template <class T>
rtc::scoped_refptr<T> CreateAndFillBuffer() {
  auto buf = T::Create(20, 10);
  memset(buf->MutableDataY(), 1, 200);

  if (buf->type() == VideoFrameBuffer::Type::kI444 ||
      buf->type() == VideoFrameBuffer::Type::kI410) {
    memset(buf->MutableDataU(), 2, 200);
    memset(buf->MutableDataV(), 3, 200);
  } else if (buf->type() == VideoFrameBuffer::Type::kI422 ||
             buf->type() == VideoFrameBuffer::Type::kI210) {
    memset(buf->MutableDataU(), 2, 100);
    memset(buf->MutableDataV(), 3, 100);
  } else {
    memset(buf->MutableDataU(), 2, 50);
    memset(buf->MutableDataV(), 3, 50);
  }

  return buf;
}

TYPED_TEST_P(TestPlanarYuvBuffer, Copy) {
  rtc::scoped_refptr<TypeParam> buf1 = CreateAndFillBuffer<TypeParam>();
  rtc::scoped_refptr<TypeParam> buf2 = TypeParam::Copy(*buf1);
  EXPECT_TRUE(test::FrameBufsEqual(buf1, buf2));
}

TYPED_TEST_P(TestPlanarYuvBuffer, CropXCenter) {
  rtc::scoped_refptr<TypeParam> buf = CreateGradient<TypeParam>(200, 100);

  // Pure center cropping, no scaling.
  rtc::scoped_refptr<TypeParam> scaled_buffer = TypeParam::Create(100, 100);
  scaled_buffer->CropAndScaleFrom(*buf, 50, 0, 100, 100);
  CheckCrop<TypeParam>(*scaled_buffer, 0.25, 0.0, 0.5, 1.0);
}

TYPED_TEST_P(TestPlanarYuvBuffer, CropXNotCenter) {
  rtc::scoped_refptr<TypeParam> buf = CreateGradient<TypeParam>(200, 100);

  // Non-center cropping, no scaling.
  rtc::scoped_refptr<TypeParam> scaled_buffer = TypeParam::Create(100, 100);
  scaled_buffer->CropAndScaleFrom(*buf, 25, 0, 100, 100);
  CheckCrop<TypeParam>(*scaled_buffer, 0.125, 0.0, 0.5, 1.0);
}

TYPED_TEST_P(TestPlanarYuvBuffer, CropYCenter) {
  rtc::scoped_refptr<TypeParam> buf = CreateGradient<TypeParam>(100, 200);

  // Pure center cropping, no scaling.
  rtc::scoped_refptr<TypeParam> scaled_buffer = TypeParam::Create(100, 100);
  scaled_buffer->CropAndScaleFrom(*buf, 0, 50, 100, 100);
  CheckCrop<TypeParam>(*scaled_buffer, 0.0, 0.25, 1.0, 0.5);
}

TYPED_TEST_P(TestPlanarYuvBuffer, CropYNotCenter) {
  rtc::scoped_refptr<TypeParam> buf = CreateGradient<TypeParam>(100, 200);

  // Pure center cropping, no scaling.
  rtc::scoped_refptr<TypeParam> scaled_buffer = TypeParam::Create(100, 100);
  scaled_buffer->CropAndScaleFrom(*buf, 0, 25, 100, 100);
  CheckCrop<TypeParam>(*scaled_buffer, 0.0, 0.125, 1.0, 0.5);
}

TYPED_TEST_P(TestPlanarYuvBuffer, CropAndScale16x9) {
  const int buffer_width = 640;
  const int buffer_height = 480;
  const int crop_width = 320;
  const int crop_height = 180;
  rtc::scoped_refptr<TypeParam> buf = CreateGradient<TypeParam>(640, 480);

  // Pure center cropping, no scaling.
  const int out_width =
      std::min(buffer_width, crop_width * buffer_height / crop_height);
  const int out_height =
      std::min(buffer_height, crop_height * buffer_width / crop_width);
  rtc::scoped_refptr<TypeParam> scaled_buffer =
      TypeParam::Create(out_width, out_height);
  scaled_buffer->CropAndScaleFrom(*buf, (buffer_width - out_width) / 2,
                                  (buffer_height - out_height) / 2, out_width,
                                  out_height);
  CheckCrop<TypeParam>(*scaled_buffer, 0.0, 0.125, 1.0, 0.75);
}

REGISTER_TYPED_TEST_SUITE_P(TestPlanarYuvBuffer,
                            Copy,
                            CropXCenter,
                            CropXNotCenter,
                            CropYCenter,
                            CropYNotCenter,
                            CropAndScale16x9);

using TestTypesAll = ::testing::Types<I420Buffer,
                                      I010Buffer,
                                      I444Buffer,
                                      I422Buffer,
                                      I210Buffer,
                                      I410Buffer>;
INSTANTIATE_TYPED_TEST_SUITE_P(All, TestPlanarYuvBuffer, TestTypesAll);

template <class T>
class TestPlanarYuvBufferScale : public ::testing::Test {};
TYPED_TEST_SUITE_P(TestPlanarYuvBufferScale);

TYPED_TEST_P(TestPlanarYuvBufferScale, Scale) {
  rtc::scoped_refptr<TypeParam> buf = CreateGradient<TypeParam>(200, 100);

  // Pure scaling, no cropping.
  rtc::scoped_refptr<TypeParam> scaled_buffer = TypeParam::Create(150, 75);
  scaled_buffer->ScaleFrom(*buf);
  CheckCrop<TypeParam>(*scaled_buffer, 0.0, 0.0, 1.0, 1.0);
}

REGISTER_TYPED_TEST_SUITE_P(TestPlanarYuvBufferScale, Scale);

using TestTypesScale =
    ::testing::Types<I420Buffer, I010Buffer, I210Buffer, I410Buffer>;
INSTANTIATE_TYPED_TEST_SUITE_P(All, TestPlanarYuvBufferScale, TestTypesScale);

template <class T>
class TestPlanarYuvBufferRotate : public ::testing::Test {
 public:
  std::vector<webrtc::VideoRotation> RotationParams = {
      kVideoRotation_0, kVideoRotation_90, kVideoRotation_180,
      kVideoRotation_270};
};

TYPED_TEST_SUITE_P(TestPlanarYuvBufferRotate);

TYPED_TEST_P(TestPlanarYuvBufferRotate, Rotates) {
  for (const webrtc::VideoRotation& rotation : this->RotationParams) {
    rtc::scoped_refptr<TypeParam> buffer = CreateGradient<TypeParam>(640, 480);
    rtc::scoped_refptr<TypeParam> rotated_buffer =
        TypeParam::Rotate(*buffer, rotation);
    CheckRotate(640, 480, rotation, *rotated_buffer);
  }
}

REGISTER_TYPED_TEST_SUITE_P(TestPlanarYuvBufferRotate, Rotates);

using TestTypesRotate = ::testing::
    Types<I420Buffer, I010Buffer, I444Buffer, I422Buffer, I210Buffer>;
INSTANTIATE_TYPED_TEST_SUITE_P(Rotate,
                               TestPlanarYuvBufferRotate,
                               TestTypesRotate);

TEST(TestNV12Buffer, CropAndScale) {
  const int kSourceWidth = 640;
  const int kSourceHeight = 480;
  const int kScaledWidth = 320;
  const int kScaledHeight = 240;
  const int kCropLeft = 40;
  const int kCropTop = 30;
  const int kCropRight = 0;
  const int kCropBottom = 30;

  rtc::scoped_refptr<VideoFrameBuffer> buf =
      CreateNV12Gradient(kSourceWidth, kSourceHeight);

  rtc::scoped_refptr<VideoFrameBuffer> scaled_buffer = buf->CropAndScale(
      kCropLeft, kCropTop, kSourceWidth - kCropLeft - kCropRight,
      kSourceHeight - kCropTop - kCropBottom, kScaledWidth, kScaledHeight);

  // Parameters to CheckCrop indicate what part of the source frame is in the
  // scaled frame.
  const float kOffsetX = (kCropLeft + 0.0) / kSourceWidth;
  const float kOffsetY = (kCropTop + 0.0) / kSourceHeight;
  const float kRelativeWidth =
      (kSourceWidth - kCropLeft - kCropRight + 0.0) / kSourceWidth;
  const float kRelativeHeight =
      (kSourceHeight - kCropTop - kCropBottom + 0.0) / kSourceHeight;
  CheckCrop(*scaled_buffer->ToI420(), kOffsetX, kOffsetY, kRelativeWidth,
            kRelativeHeight);
}

TEST(TestUpdateRect, CanCompare) {
  VideoFrame::UpdateRect a = {0, 0, 100, 200};
  VideoFrame::UpdateRect b = {0, 0, 100, 200};
  VideoFrame::UpdateRect c = {1, 0, 100, 200};
  VideoFrame::UpdateRect d = {0, 1, 100, 200};
  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a == c);
  EXPECT_FALSE(a == d);
}

TEST(TestUpdateRect, ComputesIsEmpty) {
  VideoFrame::UpdateRect a = {0, 0, 0, 0};
  VideoFrame::UpdateRect b = {0, 0, 100, 200};
  VideoFrame::UpdateRect c = {1, 100, 0, 0};
  VideoFrame::UpdateRect d = {1, 100, 100, 200};
  EXPECT_TRUE(a.IsEmpty());
  EXPECT_FALSE(b.IsEmpty());
  EXPECT_TRUE(c.IsEmpty());
  EXPECT_FALSE(d.IsEmpty());
}

TEST(TestUpdateRectUnion, NonIntersecting) {
  VideoFrame::UpdateRect a = {0, 0, 10, 20};
  VideoFrame::UpdateRect b = {100, 200, 10, 20};
  a.Union(b);
  EXPECT_EQ(a, VideoFrame::UpdateRect({0, 0, 110, 220}));
}

TEST(TestUpdateRectUnion, Intersecting) {
  VideoFrame::UpdateRect a = {0, 0, 10, 10};
  VideoFrame::UpdateRect b = {5, 5, 30, 20};
  a.Union(b);
  EXPECT_EQ(a, VideoFrame::UpdateRect({0, 0, 35, 25}));
}

TEST(TestUpdateRectUnion, OneInsideAnother) {
  VideoFrame::UpdateRect a = {0, 0, 100, 100};
  VideoFrame::UpdateRect b = {5, 5, 30, 20};
  a.Union(b);
  EXPECT_EQ(a, VideoFrame::UpdateRect({0, 0, 100, 100}));
}

TEST(TestUpdateRectIntersect, NonIntersecting) {
  VideoFrame::UpdateRect a = {0, 0, 10, 20};
  VideoFrame::UpdateRect b = {100, 200, 10, 20};
  a.Intersect(b);
  EXPECT_EQ(a, VideoFrame::UpdateRect({0, 0, 0, 0}));
}

TEST(TestUpdateRectIntersect, Intersecting) {
  VideoFrame::UpdateRect a = {0, 0, 10, 10};
  VideoFrame::UpdateRect b = {5, 5, 30, 20};
  a.Intersect(b);
  EXPECT_EQ(a, VideoFrame::UpdateRect({5, 5, 5, 5}));
}

TEST(TestUpdateRectIntersect, OneInsideAnother) {
  VideoFrame::UpdateRect a = {0, 0, 100, 100};
  VideoFrame::UpdateRect b = {5, 5, 30, 20};
  a.Intersect(b);
  EXPECT_EQ(a, VideoFrame::UpdateRect({5, 5, 30, 20}));
}

TEST(TestUpdateRectScale, NoScale) {
  const int width = 640;
  const int height = 480;
  VideoFrame::UpdateRect a = {100, 50, 100, 200};
  VideoFrame::UpdateRect scaled =
      a.ScaleWithFrame(width, height, 0, 0, width, height, width, height);
  EXPECT_EQ(scaled, VideoFrame::UpdateRect({100, 50, 100, 200}));
}

TEST(TestUpdateRectScale, CropOnly) {
  const int width = 640;
  const int height = 480;
  VideoFrame::UpdateRect a = {100, 50, 100, 200};
  VideoFrame::UpdateRect scaled = a.ScaleWithFrame(
      width, height, 10, 10, width - 20, height - 20, width - 20, height - 20);
  EXPECT_EQ(scaled, VideoFrame::UpdateRect({90, 40, 100, 200}));
}

TEST(TestUpdateRectScale, CropOnlyToOddOffset) {
  const int width = 640;
  const int height = 480;
  VideoFrame::UpdateRect a = {100, 50, 100, 200};
  VideoFrame::UpdateRect scaled = a.ScaleWithFrame(
      width, height, 5, 5, width - 10, height - 10, width - 10, height - 10);
  EXPECT_EQ(scaled, VideoFrame::UpdateRect({94, 44, 102, 202}));
}

TEST(TestUpdateRectScale, ScaleByHalf) {
  const int width = 640;
  const int height = 480;
  VideoFrame::UpdateRect a = {100, 60, 100, 200};
  VideoFrame::UpdateRect scaled = a.ScaleWithFrame(
      width, height, 0, 0, width, height, width / 2, height / 2);
  // Scaled by half and +2 pixels in all directions.
  EXPECT_EQ(scaled, VideoFrame::UpdateRect({48, 28, 54, 104}));
}

TEST(TestUpdateRectScale, CropToUnchangedRegionBelowUpdateRect) {
  const int width = 640;
  const int height = 480;
  VideoFrame::UpdateRect a = {100, 60, 100, 200};
  VideoFrame::UpdateRect scaled = a.ScaleWithFrame(
      width, height, (width - 10) / 2, (height - 10) / 2, 10, 10, 10, 10);
  // Update is out of the cropped frame.
  EXPECT_EQ(scaled, VideoFrame::UpdateRect({0, 0, 0, 0}));
}

TEST(TestUpdateRectScale, CropToUnchangedRegionAboveUpdateRect) {
  const int width = 640;
  const int height = 480;
  VideoFrame::UpdateRect a = {600, 400, 10, 10};
  VideoFrame::UpdateRect scaled = a.ScaleWithFrame(
      width, height, (width - 10) / 2, (height - 10) / 2, 10, 10, 10, 10);
  // Update is out of the cropped frame.
  EXPECT_EQ(scaled, VideoFrame::UpdateRect({0, 0, 0, 0}));
}

TEST(TestUpdateRectScale, CropInsideUpdate) {
  const int width = 640;
  const int height = 480;
  VideoFrame::UpdateRect a = {300, 200, 100, 100};
  VideoFrame::UpdateRect scaled = a.ScaleWithFrame(
      width, height, (width - 10) / 2, (height - 10) / 2, 10, 10, 10, 10);
  // Cropped frame is inside the update rect.
  EXPECT_EQ(scaled, VideoFrame::UpdateRect({0, 0, 10, 10}));
}

TEST(TestUpdateRectScale, CropAndScaleByHalf) {
  const int width = 640;
  const int height = 480;
  VideoFrame::UpdateRect a = {100, 60, 100, 200};
  VideoFrame::UpdateRect scaled =
      a.ScaleWithFrame(width, height, 10, 10, width - 20, height - 20,
                       (width - 20) / 2, (height - 20) / 2);
  // Scaled by half and +3 pixels in all directions, because of odd offset after
  // crop and scale.
  EXPECT_EQ(scaled, VideoFrame::UpdateRect({42, 22, 56, 106}));
}

}  // namespace webrtc
