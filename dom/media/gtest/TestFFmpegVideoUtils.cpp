/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>

#include "FFmpegUtils.h"
#include "FFmpegVideoUtils.h"
#include "MediaData.h"
#include "gtest/gtest.h"
#include "libavutil/pixfmt.h"
#include "mozilla/UniquePtr.h"

using namespace mozilla;

namespace {

// FFmpeg's decoder pool (update_frame_pool in libavcodec/get_buffer.c)
// allocates each plane for the height avcodec_align_dimensions2
// (libavcodec/utils.c) rounds up to 32 rows for planar YUV, plus
// kPlaneTrailingPadding bytes.
constexpr int kFrameWidth = 1920;
constexpr int kFrameHeight = 1080;
constexpr int kAlignedHeight = FFALIGN(kFrameHeight, 32);

// Strides and rows of a kFrameWidth x kFrameHeight frame in one pixel format,
// and the buffer sizes allocated for it.
struct PlaneLayout {
  int mFormat;
  int mYStride;
  int mYRows;
  int mCbCrStride;
  int mCbCrRows;

  constexpr size_t YBytes() const {
    return static_cast<size_t>(mYStride) * mYRows;
  }
  constexpr size_t CbCrBytes() const {
    return static_cast<size_t>(mCbCrStride) * mCbCrRows;
  }
  constexpr size_t YBufferSize() const {
    return YBytes() + kPlaneTrailingPadding;
  }
  constexpr size_t CbCrBufferSize() const {
    return CbCrBytes() + kPlaneTrailingPadding;
  }
  constexpr size_t SharedBufferSize() const {
    return YBytes() + 2 * CbCrBytes();
  }
};

// 10-bit samples take two bytes; 4:2:0 halves both chroma dimensions and
// 4:2:2 only the width.
constexpr PlaneLayout kYUV420P10 = {AV_PIX_FMT_YUV420P10LE, kFrameWidth * 2,
                                    kAlignedHeight, (kFrameWidth / 2) * 2,
                                    kAlignedHeight / 2};
constexpr PlaneLayout kYUV422P10 = {AV_PIX_FMT_YUV422P10LE, kFrameWidth * 2,
                                    kAlignedHeight, (kFrameWidth / 2) * 2,
                                    kAlignedHeight};
constexpr PlaneLayout kYUV444P = {AV_PIX_FMT_YUV444P, kFrameWidth,
                                  kAlignedHeight, kFrameWidth, kAlignedHeight};

// The shared-memory image GetVideoBuffer decodes into: 8-bit planes whose
// widths AllocateTextureClientForImage rounds up with avcodec_align_dimensions
// (STRIDE_ALIGN << log2_chroma_w for chroma), so a 960-pixel chroma row gets a
// 1024-byte stride; see the Eg1 comment there. Each plane also gets
// GetBuffer2StrideAlign() trailing rows in place of the pool's byte padding.
constexpr int kSharedCbCrStride = FFALIGN(kFrameWidth / 2, 2 * kStrideAlign);
constexpr PlaneLayout kYUV420PShared = {
    AV_PIX_FMT_YUV420P, kFrameWidth,
    kAlignedHeight + GetBuffer2StrideAlign(kFrameWidth), kSharedCbCrStride,
    kAlignedHeight / 2 + GetBuffer2StrideAlign(kSharedCbCrStride)};

// One AVBufferRef per plane, as FFmpeg's pool allocates, or a single
// AVBufferRef spanning all planes, as GetVideoBuffer does.
enum class Backing { PerPlane, Shared };

// Plane storage plus the AVFrame and AVBufferRef structs describing it.
class TestFrame {
 public:
  explicit TestFrame(const PlaneLayout& aLayout,
                     Backing aBacking = Backing::PerPlane)
      : TestFrame(aLayout, aLayout.mFormat, aBacking) {}

  // aFormat may differ from aLayout's format to tag the buffers as a frame of
  // another format.
  TestFrame(const PlaneLayout& aLayout, int aFormat,
            Backing aBacking = Backing::PerPlane) {
    if (aBacking == Backing::PerPlane) {
      const size_t sizes[3] = {aLayout.YBufferSize(), aLayout.CbCrBufferSize(),
                               aLayout.CbCrBufferSize()};
      for (size_t i = 0; i < 3; ++i) {
        mStorage[i] = std::make_unique<uint8_t[]>(sizes[i]);
        mRefs[i].data = mStorage[i].get();
        mRefs[i].size = sizes[i];
        mFrame.buf[i] = &mRefs[i];
        mFrame.data[i] = mRefs[i].data;
      }
    } else {
      mStorage[0] = std::make_unique<uint8_t[]>(aLayout.SharedBufferSize());
      mRefs[0].data = mStorage[0].get();
      mRefs[0].size = aLayout.SharedBufferSize();
      mFrame.buf[0] = &mRefs[0];
      mFrame.data[0] = mRefs[0].data;
      mFrame.data[1] = mFrame.data[0] + aLayout.YBytes();
      mFrame.data[2] = mFrame.data[1] + aLayout.CbCrBytes();
    }
    mFrame.linesize[0] = aLayout.mYStride;
    mFrame.linesize[1] = mFrame.linesize[2] = aLayout.mCbCrStride;
    mFrame.format = aFormat;
    mFrame.width = kFrameWidth;
    mFrame.height = kFrameHeight;
  }

  TestFrame(const TestFrame&) = delete;
  TestFrame& operator=(const TestFrame&) = delete;

  AVFrame* Frame() { return &mFrame; }

  // The plane geometry CreateImage derives from the frame.
  VideoData::YCbCrBuffer Buffer() const {
    VideoData::YCbCrBuffer b;
    for (size_t i = 0; i < 3; ++i) {
      b.mPlanes[i].mData = mFrame.data[i];
      b.mPlanes[i].mStride = mFrame.linesize[i];
      b.mPlanes[i].mSkip = 0;
    }
    b.mPlanes[0].mWidth = mFrame.width;
    b.mPlanes[0].mHeight = mFrame.height;
    SetChromaPlaneGeometryFromAVFormat(b, mFrame.format, mFrame.width,
                                       mFrame.height);
    return b;
  }

 private:
  UniquePtr<uint8_t[]> mStorage[3];
  AVBufferRef mRefs[3]{};
  AVFrame mFrame{};
};

// Bytes from aBufferStart to the end of aPlane's rows.
size_t PlaneEnd(const uint8_t* aBufferStart,
                const VideoData::YCbCrBuffer::Plane& aPlane) {
  return static_cast<size_t>(aPlane.mData - aBufferStart) +
         static_cast<size_t>(aPlane.mHeight) * aPlane.mStride;
}

}  // namespace

TEST(FFmpegVideoUtils, AcceptsMatchingPlaneGeometry)
{
  for (const PlaneLayout& layout : {kYUV420P10, kYUV422P10, kYUV444P}) {
    TestFrame frame(layout);
    EXPECT_TRUE(FramePlanesFitBackingBuffers(frame.Frame(), frame.Buffer()))
        << "format " << layout.mFormat;
  }
}

TEST(FFmpegVideoUtils, RejectsMismatchedChromaGeometry)
{
  for (int format : {AV_PIX_FMT_YUV422P10LE, AV_PIX_FMT_YUV444P}) {
    TestFrame frame(kYUV420P10, format);
    EXPECT_FALSE(FramePlanesFitBackingBuffers(frame.Frame(), frame.Buffer()))
        << "format " << format;
  }
}

TEST(FFmpegVideoUtils, Rejects422TaggedYUV420P10Frame)
{
  TestFrame frame(kYUV420P10, AV_PIX_FMT_YUV422P10LE);
  const VideoData::YCbCrBuffer b = frame.Buffer();

  EXPECT_EQ(FrameBufferSize(frame.Frame(), 1), 1044559u);
  EXPECT_EQ(static_cast<size_t>(b.mPlanes[1].mHeight) * b.mPlanes[1].mStride,
            2073600u);
  EXPECT_FALSE(FramePlanesFitBackingBuffers(frame.Frame(), b));
}

TEST(FFmpegVideoUtils, LumaBufferSizeBounds)
{
  TestFrame frame(kYUV420P10);
  const VideoData::YCbCrBuffer b = frame.Buffer();
  const size_t required = PlaneEnd(frame.Frame()->data[0], b.mPlanes[0]);
  frame.Frame()->buf[0]->size = required;
  EXPECT_TRUE(FramePlanesFitBackingBuffers(frame.Frame(), b));
  frame.Frame()->buf[0]->size = required - 1;
  EXPECT_FALSE(FramePlanesFitBackingBuffers(frame.Frame(), b));
}

TEST(FFmpegVideoUtils, SharedBufferPlaneBounds)
{
  TestFrame frame(kYUV420PShared, Backing::Shared);
  const VideoData::YCbCrBuffer b = frame.Buffer();
  const size_t required = PlaneEnd(frame.Frame()->data[0], b.mPlanes[2]);
  frame.Frame()->buf[0]->size = required;
  EXPECT_TRUE(FramePlanesFitBackingBuffers(frame.Frame(), b));
  frame.Frame()->buf[0]->size = required - 1;
  EXPECT_FALSE(FramePlanesFitBackingBuffers(frame.Frame(), b));
}

TEST(FFmpegVideoUtils, RejectsPlaneInSeparateStorage)
{
  TestFrame frame(kYUV420P10);
  auto separateCbStorage =
      std::make_unique<uint8_t[]>(kYUV420P10.CbCrBufferSize());
  frame.Frame()->data[1] = separateCbStorage.get();
  EXPECT_FALSE(FramePlanesFitBackingBuffers(frame.Frame(), frame.Buffer()));
}

TEST(FFmpegVideoUtils, RejectsFrameWithoutReferenceCountedBuffers)
{
  TestFrame frame(kYUV420P10);
  for (auto& buf : frame.Frame()->buf) {
    buf = nullptr;
  }
  EXPECT_FALSE(FramePlanesFitBackingBuffers(frame.Frame(), frame.Buffer()));
}

TEST(FFmpegVideoUtils, RejectsOversizedPlane)
{
  TestFrame frame(kYUV420P10);
  VideoData::YCbCrBuffer b = frame.Buffer();
  b.mPlanes[0].mHeight = b.mPlanes[0].mStride = UINT32_MAX;
  EXPECT_FALSE(FramePlanesFitBackingBuffers(frame.Frame(), b));
}
