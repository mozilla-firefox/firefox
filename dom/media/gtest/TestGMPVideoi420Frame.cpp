/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <climits>

#include "GMPVideoi420FrameImpl.h"
#include "gtest/gtest.h"
#include "mozilla/gmp/GMPTypes.h"

using namespace mozilla::gmp;

struct PlaneData {
  int32_t offset;
  int32_t size;
  int32_t stride;
};

struct FrameWithBuffer {
  GMPVideoi420FrameData frameData;
  size_t bufferSize;
};

static size_t CalculateBufferSize(const PlaneData& y, const PlaneData& u,
                                  const PlaneData& v) {
  int32_t max_end = 0;

  if (y.offset >= 0 && y.size > 0) {
    max_end = std::max(max_end, y.offset + y.size);
  }

  if (u.offset >= 0 && u.size > 0) {
    max_end = std::max(max_end, u.offset + u.size);
  }

  if (v.offset >= 0 && v.size > 0) {
    max_end = std::max(max_end, v.offset + v.size);
  }

  return static_cast<size_t>(std::max(max_end, 0));
}

static FrameWithBuffer MakeFrameData(int32_t width, int32_t height,
                                     const PlaneData& y, const PlaneData& u,
                                     const PlaneData& v,
                                     size_t bufferSizeOverride = 0) {
  FrameWithBuffer result;
  result.frameData.mYPlane().mOffset() = y.offset;
  result.frameData.mYPlane().mSize() = y.size;
  result.frameData.mYPlane().mStride() = y.stride;
  result.frameData.mUPlane().mOffset() = u.offset;
  result.frameData.mUPlane().mSize() = u.size;
  result.frameData.mUPlane().mStride() = u.stride;
  result.frameData.mVPlane().mOffset() = v.offset;
  result.frameData.mVPlane().mSize() = v.size;
  result.frameData.mVPlane().mStride() = v.stride;
  result.frameData.mWidth() = width;
  result.frameData.mHeight() = height;
  result.frameData.mTimestamp() = 0;
  result.frameData.mUpdatedTimestamp() = mozilla::Nothing();
  result.frameData.mDuration() = 0;
  result.bufferSize = bufferSizeOverride > 0 ? bufferSizeOverride
                                             : CalculateBufferSize(y, u, v);
  return result;
}

static FrameWithBuffer MakeValidI420Frame(int32_t width, int32_t height) {
  int32_t y_stride = width;
  int32_t y_size = y_stride * height;

  int32_t half_width = (width + 1) / 2;
  int32_t half_height = (height + 1) / 2;
  int32_t uv_stride = half_width;
  int32_t uv_size = uv_stride * half_height;

  PlaneData y = {0, y_size, y_stride};
  PlaneData u = {y_size, uv_size, uv_stride};
  PlaneData v = {y_size + uv_size, uv_size, uv_stride};

  return MakeFrameData(width, height, y, u, v);
}

TEST(GMPVideoi420FrameTest, RejectsInvalidYPlaneEnd)
{
  PlaneData y = {1, INT32_MAX, 2};
  PlaneData u = {-1, 1, 1};
  PlaneData v = {0, 1, 1};
  auto [frameData, bufferSize] = MakeFrameData(2, 2, y, u, v);

  EXPECT_FALSE(GMPVideoi420FrameImpl::CheckFrameData(frameData, bufferSize));
}

TEST(GMPVideoi420FrameTest, RejectsInvalidYPlaneEndLarge)
{
  PlaneData y = {1, INT32_MAX, 2};
  PlaneData u = {-65536, 1, 1};
  PlaneData v = {0, 1, 1};
  auto [frameData, bufferSize] = MakeFrameData(2, 2, y, u, v);

  EXPECT_FALSE(GMPVideoi420FrameImpl::CheckFrameData(frameData, bufferSize));
}

TEST(GMPVideoi420FrameTest, RejectsInvalidUPlaneEnd)
{
  PlaneData y = {0, 4, 2};
  PlaneData u = {4, INT32_MAX, 1};
  PlaneData v = {5, 1, 1};
  auto [frameData, bufferSize] = MakeFrameData(2, 2, y, u, v);

  EXPECT_FALSE(GMPVideoi420FrameImpl::CheckFrameData(frameData, bufferSize));
}

TEST(GMPVideoi420FrameTest, CheckValidFrameData)
{
  auto [frameData, bufferSize] = MakeValidI420Frame(4, 4);

  EXPECT_TRUE(GMPVideoi420FrameImpl::CheckFrameData(frameData, bufferSize));
}

TEST(GMPVideoi420FrameTest, CheckUPlaneOverlappingYPlane)
{
  auto [frameData, bufferSize] = MakeValidI420Frame(4, 4);
  frameData.mUPlane().mOffset() = 12;

  EXPECT_FALSE(GMPVideoi420FrameImpl::CheckFrameData(frameData, bufferSize));
}

TEST(GMPVideoi420FrameTest, CheckVPlaneOverlappingUPlane)
{
  auto [frameData, bufferSize] = MakeValidI420Frame(4, 4);
  frameData.mVPlane().mOffset() = 18;

  EXPECT_FALSE(GMPVideoi420FrameImpl::CheckFrameData(frameData, bufferSize));
}

TEST(GMPVideoi420FrameTest, RejectsInvalidStrideHeight)
{
  PlaneData y = {0, 7, 1};
  PlaneData u = {7, 1, INT32_MAX};
  PlaneData v = {8, 1, 0x40000000};
  auto [frameData, bufferSize] = MakeFrameData(1, 7, y, u, v);

  EXPECT_FALSE(GMPVideoi420FrameImpl::CheckFrameData(frameData, bufferSize));
}
