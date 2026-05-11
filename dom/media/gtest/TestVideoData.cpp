/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageContainer.h"
#include "MediaData.h"
#include "MediaInfo.h"
#include "MediaResult.h"
#include "gtest/gtest.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/ResultVariant.h"
#include "mozilla/gfx/Types.h"
#include "nsIWidget.h"
#include "nsTArray.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layers;

using media::TimeUnit;

static void BuildI420Buffer(uint32_t aWidth, uint32_t aHeight,
                            nsTArray<uint8_t>& aStorage,
                            VideoData::YCbCrBuffer& aBuffer) {
  uint32_t yLen = aWidth * aHeight;
  uint32_t uvW = (aWidth + 1) / 2;
  uint32_t uvH = (aHeight + 1) / 2;
  uint32_t uvLen = uvW * uvH;
  aStorage.SetLength(yLen + 2 * uvLen);
  memset(aStorage.Elements(), 0x10, yLen);
  memset(aStorage.Elements() + yLen, 0x80, 2 * uvLen);

  aBuffer.mPlanes[0] = {aStorage.Elements(), aWidth, aHeight, aWidth, 0};
  aBuffer.mPlanes[1] = {aStorage.Elements() + yLen, uvW, uvH, uvW, 0};
  aBuffer.mPlanes[2] = {aStorage.Elements() + yLen + uvLen, uvW, uvH, uvW, 0};
  aBuffer.mYUVColorSpace = YUVColorSpace::BT2020;
  aBuffer.mChromaSubsampling = ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;
}

// Runs SetVideoDataToImage with the given HDR transfer function and full
// HDRMetadata (Smpte2086 + ContentLightLevel), then asserts all attributes.
static void TestHDRTransferFunctionPropagation(TransferFunction aTF) {
  const uint32_t w = 64, h = 48;

  nsTArray<uint8_t> storage;
  VideoData::YCbCrBuffer buf{};
  BuildI420Buffer(w, h, storage, buf);

  VideoInfo info;
  info.mDisplay = IntSize(w, h);
  info.mTransferFunction = Some(aTF);

  gfx::Smpte2086Metadata smpte2086;
  smpte2086.displayPrimaryRed = {0.708f, 0.292f};
  smpte2086.displayPrimaryGreen = {0.170f, 0.797f};
  smpte2086.displayPrimaryBlue = {0.131f, 0.046f};
  smpte2086.whitePoint = {0.3127f, 0.3290f};
  smpte2086.maxLuminance = 1000.0f;
  smpte2086.minLuminance = 0.001f;

  gfx::HDRMetadata hdrMetadata;
  hdrMetadata.mSmpte2086 = Some(smpte2086);
  hdrMetadata.mContentLightLevel = Some(gfx::ContentLightLevel{1000, 400});
  info.mHDRMetadata = Some(hdrMetadata);

  RefPtr<PlanarYCbCrImage> image =
      new RecyclingPlanarYCbCrImage(new BufferRecycleBin());

  MediaResult rv = VideoData::SetVideoDataToImage(image, info, buf,
                                                  IntRect(0, 0, w, h), true);
  ASSERT_TRUE(NS_SUCCEEDED(rv));

  const PlanarYCbCrData* data = image->GetData();
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(data->mTransferFunction, aTF);

  ASSERT_TRUE(data->mHDRMetadata.isSome());
  ASSERT_TRUE(data->mHDRMetadata->mSmpte2086.isSome());
  EXPECT_FLOAT_EQ(data->mHDRMetadata->mSmpte2086->maxLuminance, 1000.0f);
  EXPECT_FLOAT_EQ(data->mHDRMetadata->mSmpte2086->minLuminance, 0.001f);
  EXPECT_FLOAT_EQ(data->mHDRMetadata->mSmpte2086->displayPrimaryRed.x, 0.708f);
  EXPECT_FLOAT_EQ(data->mHDRMetadata->mSmpte2086->whitePoint.y, 0.3290f);
  ASSERT_TRUE(data->mHDRMetadata->mContentLightLevel.isSome());
  EXPECT_EQ(data->mHDRMetadata->mContentLightLevel->maxContentLightLevel, 1000);
  EXPECT_EQ(data->mHDRMetadata->mContentLightLevel->maxFrameAverageLightLevel,
            400);
}

TEST(VideoData, SetVideoDataToImagePropagatesPQTransferFunction)
{
  TestHDRTransferFunctionPropagation(TransferFunction::PQ);
}

TEST(VideoData, SetVideoDataToImagePropagatesHLGTransferFunction)
{
  TestHDRTransferFunctionPropagation(TransferFunction::HLG);
}

// When VideoInfo has no TransferFunction, the BT709 default is preserved.
TEST(VideoData, SetVideoDataToImagePreservesBT709Default)
{
  const uint32_t w = 64, h = 48;

  nsTArray<uint8_t> storage;
  VideoData::YCbCrBuffer buf{};
  BuildI420Buffer(w, h, storage, buf);

  VideoInfo info;
  info.mDisplay = IntSize(w, h);
  // mTransferFunction = Nothing() — default

  RefPtr<PlanarYCbCrImage> image =
      new RecyclingPlanarYCbCrImage(new BufferRecycleBin());

  MediaResult rv = VideoData::SetVideoDataToImage(image, info, buf,
                                                  IntRect(0, 0, w, h), true);
  ASSERT_TRUE(NS_SUCCEEDED(rv));

  const PlanarYCbCrData* data = image->GetData();
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(data->mTransferFunction, TransferFunction::BT709);
  EXPECT_EQ(data->mHDRMetadata, Nothing());
}

TEST(VideoData, StrideOrSizeMismatch)
{
  constexpr int width = 32;
  constexpr int height = 64;
  constexpr int stride = width * 10;
  VideoInfo info(width, height);
  uint8_t buffer[stride * height] = {};
  VideoData::YCbCrBuffer b;
  VideoData::YCbCrBuffer::Plane alpha_plane;

  b.mPlanes[0].mStride = alpha_plane.mStride = stride;
  b.mPlanes[0].mHeight = alpha_plane.mHeight = height;
  b.mPlanes[0].mWidth = alpha_plane.mWidth = width;
  b.mPlanes[0].mSkip = alpha_plane.mSkip = 0;
  b.mPlanes[0].mData = alpha_plane.mData = buffer;

  b.mChromaSubsampling = gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;
  b.mPlanes[1].mStride = b.mPlanes[2].mStride = stride / 4;
  b.mPlanes[1].mSkip = b.mPlanes[2].mSkip = 0;
  b.mPlanes[1].mHeight = b.mPlanes[2].mHeight = (b.mPlanes[0].mHeight + 1) / 2;
  b.mPlanes[1].mWidth = b.mPlanes[2].mWidth = (b.mPlanes[0].mWidth + 1) / 2;
  // Use tail end of buffer.
  b.mPlanes[1].mData = b.mPlanes[2].mData =
      buffer + stride * height - b.mPlanes[1].mStride * b.mPlanes[1].mHeight;

  b.mYUVColorSpace = gfx::YUVColorSpace::BT601;
  b.mColorRange = gfx::ColorRange::FULL;

  RefPtr imageContainer = new ImageContainer(
      ImageUsageType::VideoFrameContainer, ImageContainer::ASYNCHRONOUS);
  auto CreateVideoData = [&]() {
    Result res = VideoData::CreateAndCopyData(
        info, imageContainer, /*aOffset*/ 0, TimeUnit::Zero(),
        TimeUnit::FromSeconds(1), b, alpha_plane, /*aKeyFrame*/ true,
        TimeUnit::Zero(), info.ImageRect());
    return res.unwrapOr(nullptr);
  };
  // Trigger gfx::GPUProcessManager::EnsureImageBridgeChild()
  // for ImageContainer::CreateSharedRGBImage(),
  // from VideoData::CreateAndCopyData().
  // (ImageContainer::EnsureRecycleAllocatorForRDD() requires
  // XRE_IsRDDProcess().)
  RefPtr widget = nsIWidget::CreateHeadlessWidget();
  widget->CreateCompositor();
  // Check that VideoData can be created successfully from a simple
  // combination of planes.
  RefPtr v = CreateVideoData();
  EXPECT_NE(v.get(), nullptr);

  // Check that non-so-simple combinations do not cause crashes.
  // At the time of writing this test, CreateAndCopyData() returns failure,
  // but these combinations could be supported in the future, if desired.
  {
    // Differing alpha and luma plane strides
    AutoRestore r(alpha_plane.mStride);
    AutoRestore r2(alpha_plane.mData);
    alpha_plane.mStride = width;
    EXPECT_NE(alpha_plane.mStride, b.mPlanes[0].mStride);
    // tail end of buffer
    alpha_plane.mData = buffer + (stride - alpha_plane.mStride) * height;
    v = CreateVideoData();
    // Change to NE if this becomes supported.
    EXPECT_EQ(v.get(), nullptr);
  }
  {
    // Differing alpha and luma plane heights
    AutoRestore r(alpha_plane.mHeight);
    AutoRestore r2(alpha_plane.mData);
    alpha_plane.mHeight = height / 2;
    EXPECT_NE(alpha_plane.mHeight, b.mPlanes[0].mHeight);
    alpha_plane.mData =
        buffer + stride * height - alpha_plane.mStride * alpha_plane.mHeight;
    v = CreateVideoData();
    // Change to NE if this becomes supported.
    EXPECT_EQ(v.get(), nullptr);
  }
  {
    // Differing chroma plane strides
    AutoRestore r(b.mPlanes[2].mStride);
    AutoRestore r2(b.mPlanes[2].mData);
    b.mPlanes[2].mStride = b.mPlanes[2].mWidth;
    EXPECT_NE(b.mPlanes[1].mStride, b.mPlanes[2].mStride);
    b.mPlanes[2].mData =
        buffer + stride * height - b.mPlanes[2].mStride * b.mPlanes[2].mHeight;
    v = CreateVideoData();
    // Change to NE if this becomes supported.
    EXPECT_EQ(v.get(), nullptr);
  }
}
