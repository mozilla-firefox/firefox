/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "common_video/libyuv/include/webrtc_libyuv.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "api/scoped_refptr.h"
#include "api/video/i010_buffer.h"
#include "api/video/i210_buffer.h"
#include "api/video/i410_buffer.h"
#include "api/video/i420_buffer.h"
#include "api/video/i422_buffer.h"
#include "api/video/i444_buffer.h"
#include "api/video/nv12_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_rotation.h"
#include "test/frame_utils.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/testsupport/file_utils.h"
#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/libyuv/include/libyuv/rotate.h"

namespace webrtc {

namespace {
void Calc16ByteAlignedStride(int width, int* stride_y, int* stride_uv) {
  *stride_y = 16 * ((width + 15) / 16);
  *stride_uv = 16 * ((width + 31) / 32);
}

int PrintPlane(const uint8_t* buf,
               int width,
               int height,
               int stride,
               FILE* file) {
  for (int i = 0; i < height; i++, buf += stride) {
    if (fwrite(buf, 1, width, file) != static_cast<unsigned int>(width))
      return -1;
  }
  return 0;
}

int PrintVideoFrame(const I420BufferInterface& frame, FILE* file) {
  int width = frame.width();
  int height = frame.height();
  int chroma_width = frame.ChromaWidth();
  int chroma_height = frame.ChromaHeight();

  if (PrintPlane(frame.DataY(), width, height, frame.StrideY(), file) < 0) {
    return -1;
  }
  if (PrintPlane(frame.DataU(), chroma_width, chroma_height, frame.StrideU(),
                 file) < 0) {
    return -1;
  }
  if (PrintPlane(frame.DataV(), chroma_width, chroma_height, frame.StrideV(),
                 file) < 0) {
    return -1;
  }
  return 0;
}

}  // Anonymous namespace

class TestLibYuv : public ::testing::Test {
 protected:
  TestLibYuv();
  void SetUp() override;
  void TearDown() override;

  FILE* source_file_;
  std::unique_ptr<VideoFrame> orig_frame_;
  const int width_;
  const int height_;
  const int size_y_;
  const int size_uv_;
  const size_t frame_length_;
};

TestLibYuv::TestLibYuv()
    : source_file_(nullptr),
      orig_frame_(),
      width_(352),
      height_(288),
      size_y_(width_ * height_),
      size_uv_(((width_ + 1) / 2) * ((height_ + 1) / 2)),
      frame_length_(CalcBufferSize(VideoType::kI420, 352, 288)) {}

void TestLibYuv::SetUp() {
  const std::string input_file_name = test::ResourcePath("foreman_cif", "yuv");
  source_file_ = fopen(input_file_name.c_str(), "rb");
  ASSERT_TRUE(source_file_ != nullptr)
      << "Cannot read file: " << input_file_name << "\n";

  scoped_refptr<I420BufferInterface> buffer(
      test::ReadI420Buffer(width_, height_, source_file_));

  orig_frame_ = std::make_unique<VideoFrame>(VideoFrame::Builder()
                                                 .set_video_frame_buffer(buffer)
                                                 .set_rotation(kVideoRotation_0)
                                                 .set_timestamp_us(0)
                                                 .build());
}

void TestLibYuv::TearDown() {
  if (source_file_ != nullptr) {
    ASSERT_EQ(0, fclose(source_file_));
  }
  source_file_ = nullptr;
}

TEST_F(TestLibYuv, ConvertTest) {
  // Reading YUV frame - testing on the first frame of the foreman sequence
  int j = 0;
  std::string output_file_name =
      test::OutputPath() + "LibYuvTest_conversion.yuv";
  FILE* output_file = fopen(output_file_name.c_str(), "wb");
  ASSERT_TRUE(output_file != nullptr);

  double psnr = 0.0;

  scoped_refptr<I420Buffer> res_i420_buffer =
      I420Buffer::Create(width_, height_);

  printf("\nConvert #%d I420 <-> I420 \n", j);
  std::unique_ptr<uint8_t[]> out_i420_buffer(new uint8_t[frame_length_]);
  EXPECT_EQ(0, ConvertFromI420(*orig_frame_, VideoType::kI420, 0,
                               out_i420_buffer.get()));
  int y_size = width_ * height_;
  int u_size = res_i420_buffer->ChromaWidth() * res_i420_buffer->ChromaHeight();
  int ret = libyuv::I420Copy(
      out_i420_buffer.get(), width_, out_i420_buffer.get() + y_size,
      width_ >> 1, out_i420_buffer.get() + y_size + u_size, width_ >> 1,
      res_i420_buffer->MutableDataY(), res_i420_buffer->StrideY(),
      res_i420_buffer->MutableDataU(), res_i420_buffer->StrideU(),
      res_i420_buffer->MutableDataV(), res_i420_buffer->StrideV(), width_,
      height_);
  EXPECT_EQ(0, ret);

  if (PrintVideoFrame(*res_i420_buffer, output_file) < 0) {
    return;
  }
  psnr =
      I420PSNR(*orig_frame_->video_frame_buffer()->GetI420(), *res_i420_buffer);
  EXPECT_EQ(48.0, psnr);
  j++;

  printf("\nConvert #%d I420 <-> RGB24\n", j);
  std::unique_ptr<uint8_t[]> res_rgb_buffer2(new uint8_t[width_ * height_ * 3]);
  // Align the stride values for the output frame.
  int stride_y = 0;
  int stride_uv = 0;
  Calc16ByteAlignedStride(width_, &stride_y, &stride_uv);
  res_i420_buffer =
      I420Buffer::Create(width_, height_, stride_y, stride_uv, stride_uv);
  EXPECT_EQ(0, ConvertFromI420(*orig_frame_, VideoType::kRGB24, 0,
                               res_rgb_buffer2.get()));

  ret = ConvertToI420(
      res_rgb_buffer2.get(), 0, res_i420_buffer->MutableDataY(),
      res_i420_buffer->StrideY(), res_i420_buffer->MutableDataU(),
      res_i420_buffer->StrideU(), res_i420_buffer->MutableDataV(),
      res_i420_buffer->StrideV(), width_, height_, 0, res_i420_buffer->width(),
      res_i420_buffer->height(), static_cast<uint32_t>(libyuv::kRotate0),
      ConvertVideoType(VideoType::kRGB24));

  EXPECT_EQ(0, ret);
  if (PrintVideoFrame(*res_i420_buffer, output_file) < 0) {
    return;
  }
  psnr =
      I420PSNR(*orig_frame_->video_frame_buffer()->GetI420(), *res_i420_buffer);

  // Optimization Speed- quality trade-off => 45 dB only (platform dependant).
  EXPECT_GT(ceil(psnr), 44);
  j++;

  printf("\nConvert #%d I420 <-> UYVY\n", j);
  std::unique_ptr<uint8_t[]> out_uyvy_buffer(new uint8_t[width_ * height_ * 2]);
  EXPECT_EQ(0, ConvertFromI420(*orig_frame_, VideoType::kUYVY, 0,
                               out_uyvy_buffer.get()));

  ret = ConvertToI420(
      out_uyvy_buffer.get(), 0, res_i420_buffer->MutableDataY(),
      res_i420_buffer->StrideY(), res_i420_buffer->MutableDataU(),
      res_i420_buffer->StrideU(), res_i420_buffer->MutableDataV(),
      res_i420_buffer->StrideV(), width_, height_, 0, res_i420_buffer->width(),
      res_i420_buffer->height(), static_cast<uint32_t>(libyuv::kRotate0),
      ConvertVideoType(VideoType::kUYVY));

  EXPECT_EQ(0, ret);
  psnr =
      I420PSNR(*orig_frame_->video_frame_buffer()->GetI420(), *res_i420_buffer);
  EXPECT_EQ(48.0, psnr);
  if (PrintVideoFrame(*res_i420_buffer, output_file) < 0) {
    return;
  }
  j++;

  printf("\nConvert #%d I420 <-> YUY2\n", j);
  std::unique_ptr<uint8_t[]> out_yuy2_buffer(new uint8_t[width_ * height_ * 2]);
  EXPECT_EQ(0, ConvertFromI420(*orig_frame_, VideoType::kYUY2, 0,
                               out_yuy2_buffer.get()));

  ret = ConvertToI420(
      out_yuy2_buffer.get(), 0, res_i420_buffer->MutableDataY(),
      res_i420_buffer->StrideY(), res_i420_buffer->MutableDataU(),
      res_i420_buffer->StrideU(), res_i420_buffer->MutableDataV(),
      res_i420_buffer->StrideV(), width_, height_, 0, res_i420_buffer->width(),
      res_i420_buffer->height(), static_cast<uint32_t>(libyuv::kRotate0),
      ConvertVideoType(VideoType::kYUY2));

  EXPECT_EQ(0, ret);

  if (PrintVideoFrame(*res_i420_buffer, output_file) < 0) {
    return;
  }

  psnr =
      I420PSNR(*orig_frame_->video_frame_buffer()->GetI420(), *res_i420_buffer);
  EXPECT_EQ(48.0, psnr);

  printf("\nConvert #%d I420 <-> RGB565\n", j);
  std::unique_ptr<uint8_t[]> out_rgb565_buffer(
      new uint8_t[width_ * height_ * 2]);
  EXPECT_EQ(0, ConvertFromI420(*orig_frame_, VideoType::kRGB565, 0,
                               out_rgb565_buffer.get()));

  ret = ConvertToI420(
      out_rgb565_buffer.get(), 0, res_i420_buffer->MutableDataY(),
      res_i420_buffer->StrideY(), res_i420_buffer->MutableDataU(),
      res_i420_buffer->StrideU(), res_i420_buffer->MutableDataV(),
      res_i420_buffer->StrideV(), width_, height_, 0, res_i420_buffer->width(),
      res_i420_buffer->height(), static_cast<uint32_t>(libyuv::kRotate0),
      ConvertVideoType(VideoType::kRGB565));

  EXPECT_EQ(0, ret);
  if (PrintVideoFrame(*res_i420_buffer, output_file) < 0) {
    return;
  }
  j++;

  psnr =
      I420PSNR(*orig_frame_->video_frame_buffer()->GetI420(), *res_i420_buffer);
  // TODO(leozwang) Investigate the right psnr should be set for I420ToRGB565,
  // Another example is I420ToRGB24, the psnr is 44
  // TODO(mikhal): Add psnr for RGB565, 1555, 4444, convert to ARGB.
  EXPECT_GT(ceil(psnr), 40);

  printf("\nConvert #%d I420 <-> ARGB8888\n", j);
  std::unique_ptr<uint8_t[]> out_argb8888_buffer(
      new uint8_t[width_ * height_ * 4]);
  EXPECT_EQ(0, ConvertFromI420(*orig_frame_, VideoType::kARGB, 0,
                               out_argb8888_buffer.get()));

  ret = ConvertToI420(
      out_argb8888_buffer.get(), 0, res_i420_buffer->MutableDataY(),
      res_i420_buffer->StrideY(), res_i420_buffer->MutableDataU(),
      res_i420_buffer->StrideU(), res_i420_buffer->MutableDataV(),
      res_i420_buffer->StrideV(), width_, height_, 0, res_i420_buffer->width(),
      res_i420_buffer->height(), static_cast<uint32_t>(libyuv::kRotate0),
      ConvertVideoType(VideoType::kARGB));

  EXPECT_EQ(0, ret);

  if (PrintVideoFrame(*res_i420_buffer, output_file) < 0) {
    return;
  }

  psnr =
      I420PSNR(*orig_frame_->video_frame_buffer()->GetI420(), *res_i420_buffer);
  // TODO(leozwang) Investigate the right psnr should be set for
  // I420ToARGB8888,
  EXPECT_GT(ceil(psnr), 42);

  ASSERT_EQ(0, fclose(output_file));
}

TEST_F(TestLibYuv, ConvertAlignedFrame) {
  // Reading YUV frame - testing on the first frame of the foreman sequence
  std::string output_file_name =
      test::OutputPath() + "LibYuvTest_conversion.yuv";
  FILE* output_file = fopen(output_file_name.c_str(), "wb");
  ASSERT_TRUE(output_file != nullptr);

  double psnr = 0.0;

  int stride_y = 0;
  int stride_uv = 0;
  Calc16ByteAlignedStride(width_, &stride_y, &stride_uv);

  scoped_refptr<I420Buffer> res_i420_buffer =
      I420Buffer::Create(width_, height_, stride_y, stride_uv, stride_uv);
  std::unique_ptr<uint8_t[]> out_i420_buffer(new uint8_t[frame_length_]);
  EXPECT_EQ(0, ConvertFromI420(*orig_frame_, VideoType::kI420, 0,
                               out_i420_buffer.get()));
  int y_size = width_ * height_;
  int u_size = res_i420_buffer->ChromaWidth() * res_i420_buffer->ChromaHeight();
  int ret = libyuv::I420Copy(
      out_i420_buffer.get(), width_, out_i420_buffer.get() + y_size,
      width_ >> 1, out_i420_buffer.get() + y_size + u_size, width_ >> 1,
      res_i420_buffer->MutableDataY(), res_i420_buffer->StrideY(),
      res_i420_buffer->MutableDataU(), res_i420_buffer->StrideU(),
      res_i420_buffer->MutableDataV(), res_i420_buffer->StrideV(), width_,
      height_);

  EXPECT_EQ(0, ret);

  if (PrintVideoFrame(*res_i420_buffer, output_file) < 0) {
    return;
  }
  psnr =
      I420PSNR(*orig_frame_->video_frame_buffer()->GetI420(), *res_i420_buffer);
  EXPECT_EQ(48.0, psnr);
}

static uint8_t Average(int a, int b, int c, int d) {
  return (a + b + c + d + 2) / 4;
}

TEST_F(TestLibYuv, NV12Scale2x2to2x2) {
  const std::vector<uint8_t> src_y = {0, 1, 2, 3};
  const std::vector<uint8_t> src_uv = {0, 1};
  std::vector<uint8_t> dst_y(4);
  std::vector<uint8_t> dst_uv(2);

  uint8_t* tmp_buffer = nullptr;

  NV12Scale(tmp_buffer, src_y.data(), 2, src_uv.data(), 2, 2, 2, dst_y.data(),
            2, dst_uv.data(), 2, 2, 2);

  EXPECT_THAT(dst_y, ::testing::ContainerEq(src_y));
  EXPECT_THAT(dst_uv, ::testing::ContainerEq(src_uv));
}

TEST_F(TestLibYuv, NV12Scale4x4to2x2) {
  const uint8_t src_y[] = {0, 1, 2,  3,  4,  5,  6,  7,
                           8, 9, 10, 11, 12, 13, 14, 15};
  const uint8_t src_uv[] = {0, 1, 2, 3, 4, 5, 6, 7};
  std::vector<uint8_t> dst_y(4);
  std::vector<uint8_t> dst_uv(2);

  std::vector<uint8_t> tmp_buffer;
  const int src_chroma_width = (4 + 1) / 2;
  const int src_chroma_height = (4 + 1) / 2;
  const int dst_chroma_width = (2 + 1) / 2;
  const int dst_chroma_height = (2 + 1) / 2;
  tmp_buffer.resize(src_chroma_width * src_chroma_height * 2 +
                    dst_chroma_width * dst_chroma_height * 2);
  tmp_buffer.shrink_to_fit();

  NV12Scale(tmp_buffer.data(), src_y, 4, src_uv, 4, 4, 4, dst_y.data(), 2,
            dst_uv.data(), 2, 2, 2);

  EXPECT_THAT(dst_y, ::testing::ElementsAre(
                         Average(0, 1, 4, 5), Average(2, 3, 6, 7),
                         Average(8, 9, 12, 13), Average(10, 11, 14, 15)));
  EXPECT_THAT(dst_uv,
              ::testing::ElementsAre(Average(0, 2, 4, 6), Average(1, 3, 5, 7)));
}

TEST(I420WeightedPSNRTest, SmokeTest) {
  uint8_t ref_y[] = {0, 0, 0, 0};
  uint8_t ref_uv[] = {0};
  scoped_refptr<I420Buffer> ref_buffer =
      I420Buffer::Copy(/*width=*/2, /*height=*/2, ref_y, /*stride_y=*/2, ref_uv,
                       /*stride_u=*/1, ref_uv, /*stride_v=*/1);

  uint8_t test_y[] = {1, 1, 1, 1};
  uint8_t test_uv[] = {2};
  scoped_refptr<I420Buffer> test_buffer = I420Buffer::Copy(
      /*width=*/2, /*height=*/2, test_y, /*stride_y=*/2, test_uv,
      /*stride_u=*/1, test_uv, /*stride_v=*/1);

  auto psnr = [](double mse) { return 10.0 * log10(255.0 * 255.0 / mse); };
  EXPECT_NEAR(I420WeightedPSNR(*ref_buffer, *test_buffer),
              (6.0 * psnr(1.0) + psnr(4.0) + psnr(4.0)) / 8.0,
              /*abs_error=*/0.001);
}

#if GTEST_HAS_DEATH_TEST && !defined(WEBRTC_ANDROID)
// Check that we catch int overflow if invalid dimensions get passed to
// `I420Buffer::Create()`.
TEST_F(TestLibYuv, I420DimensionsTooLarge) {
  // Dimensions large enough to cause overflow.
  constexpr int kWidth = 0xFFFF;
  constexpr int kHeight = 0xAAB0;
  // Sanity check for this test. This calculation, which is part of what
  // `I420Buffer::Create()` will do, should cause an `int` overflow.
  static_assert(
      (int64_t{kWidth} * int64_t{kHeight}) > std::numeric_limits<int>::max(),
      "");

  EXPECT_DEATH(I010Buffer::Create(kWidth, kHeight),
               "IsValueInRangeForNumericType");
  EXPECT_DEATH(I210Buffer::Create(kWidth, kHeight),
               "IsValueInRangeForNumericType");

  int stride_uv = (kWidth + 1) / 2;
  EXPECT_DEATH(I410Buffer::Create(kWidth, kHeight, /*stride_y=*/kWidth,
                                  stride_uv, stride_uv),
               "IsValueInRangeForNumericType");
  EXPECT_DEATH(I420Buffer::Create(kWidth, kHeight, /*stride_y=*/kWidth,
                                  stride_uv, stride_uv),
               "IsValueInRangeForNumericType");
  EXPECT_DEATH(I422Buffer::Create(kWidth, kHeight, /*stride_y=*/kWidth,
                                  stride_uv, stride_uv),
               "IsValueInRangeForNumericType");
  EXPECT_DEATH(I444Buffer::Create(kWidth, kHeight, /*stride_y=*/kWidth,
                                  stride_uv, stride_uv),
               "IsValueInRangeForNumericType");
  EXPECT_DEATH(
      NV12Buffer::Create(kWidth, kHeight, /*stride_y=*/kWidth, stride_uv),
      "IsValueInRangeForNumericType");
}

template <typename T>
void TestInvalidDimensions5Params() {
  EXPECT_DEATH(T::Create(-11, 1, /*stride_y=*/1,
                         /*stride_u=*/1,
                         /*stride_v=*/1),
               "> 0");
  EXPECT_DEATH(T::Create(1, -11, /*stride_y=*/1,
                         /*stride_u=*/1,
                         /*stride_v=*/1),
               "> 0");
  EXPECT_DEATH(T::Create(1, 1, /*stride_y=*/-12,
                         /*stride_u=*/1,
                         /*stride_v=*/1),
               ">= width");
  EXPECT_DEATH(T::Create(1, 1, /*stride_y=*/1,
                         /*stride_u=*/-12,
                         /*stride_v=*/1),
               "> 0");
  EXPECT_DEATH(T::Create(1, 1, /*stride_y=*/1,
                         /*stride_u=*/1,
                         /*stride_v=*/-12),
               "> 0");
}

template <typename T>
void TestInvalidDimensions4Params() {
  EXPECT_DEATH(T::Create(-11, 1, /*stride_y=*/1,
                         /*stride_uv=*/1),
               "> 0");
  EXPECT_DEATH(T::Create(1, -11, /*stride_y=*/1,
                         /*stride_uv=*/1),
               "> 0");
  EXPECT_DEATH(T::Create(1, 1, /*stride_y=*/-12,
                         /*stride_uv=*/1),
               ">= width");
  EXPECT_DEATH(T::Create(1, 1, /*stride_y=*/1,
                         /*stride_uv=*/-12),
               "> 0");
}

template <typename T>
void TestInvalidDimensions2Param() {
  EXPECT_DEATH(T::Create(-11, 1), "> 0");
  EXPECT_DEATH(T::Create(1, -11), "> 0");
}

TEST_F(TestLibYuv, I420InvalidDimensions) {
  // Only width and height provided to `Create()`.
  TestInvalidDimensions2Param<I010Buffer>();
  TestInvalidDimensions2Param<I210Buffer>();
  // `Create() is provided with width, height, y, u, v.
  TestInvalidDimensions5Params<I410Buffer>();
  TestInvalidDimensions5Params<I420Buffer>();
  TestInvalidDimensions5Params<I422Buffer>();
  TestInvalidDimensions5Params<I444Buffer>();
  // `Create() is provided with width, height, y, u_and_v.
  TestInvalidDimensions4Params<NV12Buffer>();
}

#endif  // GTEST_HAS_DEATH_TEST && !defined(WEBRTC_ANDROID)

}  // namespace webrtc
