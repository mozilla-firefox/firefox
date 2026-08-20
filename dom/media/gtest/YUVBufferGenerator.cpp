/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "YUVBufferGenerator.h"

#include "VideoUtils.h"
#include "gtest/gtest.h"
#include "mozilla/CheckedInt.h"

using namespace mozilla::layers;
using namespace mozilla;

void YUVBufferGenerator::Init(const mozilla::gfx::IntSize& aSize) {
  mImageSize = aSize;

  // Size the chroma region the way the image creators below lay it out, using
  // the same helper, so the two cannot disagree. Deriving it from yPlaneLen
  // instead is short whenever a dimension is odd.
  const gfx::IntSize chromaSize =
      gfx::ChromaSize(aSize, gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT);
  const CheckedInt<size_t> yPlaneLen =
      CheckedInt<size_t>(aSize.width) * aSize.height;
  const CheckedInt<size_t> cbcrPlaneLen =
      CheckedInt<size_t>(chromaSize.width) * chromaSize.height * 2;
  const CheckedInt<size_t> frameLen = yPlaneLen + cbcrPlaneLen;
  // The sum carries the parts' validity, so one check covers all three.
  EXPECT_TRUE(frameLen.isValid());

  // Generate source buffer.
  mSourceBuffer.SetLength(frameLen.value());

  // Fill Y plane.
  memset(mSourceBuffer.Elements(), 0x10, yPlaneLen.value());

  // Fill Cb/Cr planes.
  memset(mSourceBuffer.Elements() + yPlaneLen.value(), 0x80,
         cbcrPlaneLen.value());
}

mozilla::gfx::IntSize YUVBufferGenerator::GetSize() const { return mImageSize; }

already_AddRefed<Image> YUVBufferGenerator::GenerateI420Image() {
  return do_AddRef(CreateI420Image());
}

already_AddRefed<Image> YUVBufferGenerator::GenerateNV12Image() {
  return do_AddRef(CreateNV12Image());
}

already_AddRefed<Image> YUVBufferGenerator::GenerateNV21Image() {
  return do_AddRef(CreateNV21Image());
}

Image* YUVBufferGenerator::CreateI420Image() {
  PlanarYCbCrImage* image =
      new RecyclingPlanarYCbCrImage(new BufferRecycleBin());
  PlanarYCbCrData data;
  data.mPictureRect = gfx::IntRect(0, 0, mImageSize.width, mImageSize.height);

  const gfx::IntSize chromaSize = gfx::ChromaSize(
      mImageSize, gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT);
  const CheckedInt<uint32_t> checkedYPlaneSize =
      CheckedInt<uint32_t>(mImageSize.width) * mImageSize.height;
  const CheckedInt<uint32_t> checkedUVPlaneSize =
      CheckedInt<uint32_t>(chromaSize.width) * chromaSize.height;
  EXPECT_TRUE((checkedYPlaneSize + checkedUVPlaneSize * 2).isValid());
  const uint32_t yPlaneSize = checkedYPlaneSize.value();
  const uint32_t uvPlaneSize = checkedUVPlaneSize.value();

  // Y plane.
  uint8_t* y = mSourceBuffer.Elements();
  data.mYChannel = y;
  data.mYStride = mImageSize.width;
  data.mYSkip = 0;

  // Cr plane (aka V).
  uint8_t* cr = y + yPlaneSize + uvPlaneSize;
  data.mCrChannel = cr;
  data.mCrSkip = 0;

  // Cb plane (aka U).
  uint8_t* cb = y + yPlaneSize;
  data.mCbChannel = cb;
  data.mCbSkip = 0;

  // CrCb plane vectors.
  data.mCbCrStride = chromaSize.width;
  data.mChromaSubsampling = gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;

  data.mYUVColorSpace = DefaultColorSpace(mImageSize);

  image->CopyData(data);
  return image;
}

Image* YUVBufferGenerator::CreateNV12Image() {
  NVImage* image = new NVImage();
  PlanarYCbCrData data;
  data.mPictureRect = gfx::IntRect(0, 0, mImageSize.width, mImageSize.height);

  const uint32_t yPlaneSize = mImageSize.width * mImageSize.height;

  // Y plane.
  uint8_t* y = mSourceBuffer.Elements();
  data.mYChannel = y;
  data.mYStride = mImageSize.width;
  data.mYSkip = 0;

  // Cb plane (aka U).
  uint8_t* cb = y + yPlaneSize;
  data.mCbChannel = cb;
  data.mCbSkip = 1;

  // Cr plane (aka V).
  uint8_t* cr = y + yPlaneSize + 1;
  data.mCrChannel = cr;
  data.mCrSkip = 1;

  // 4:2:0.
  data.mCbCrStride = mImageSize.width;
  data.mChromaSubsampling = gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;

  image->SetData(data);
  return image;
}

Image* YUVBufferGenerator::CreateNV21Image() {
  NVImage* image = new NVImage();
  PlanarYCbCrData data;
  data.mPictureRect = gfx::IntRect(0, 0, mImageSize.width, mImageSize.height);

  const uint32_t yPlaneSize = mImageSize.width * mImageSize.height;

  // Y plane.
  uint8_t* y = mSourceBuffer.Elements();
  data.mYChannel = y;
  data.mYStride = mImageSize.width;
  data.mYSkip = 0;

  // Cb plane (aka U).
  uint8_t* cb = y + yPlaneSize + 1;
  data.mCbChannel = cb;
  data.mCbSkip = 1;

  // Cr plane (aka V).
  uint8_t* cr = y + yPlaneSize;
  data.mCrChannel = cr;
  data.mCrSkip = 1;

  // 4:2:0.
  data.mCbCrStride = mImageSize.width;
  data.mChromaSubsampling = gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;

  data.mYUVColorSpace = DefaultColorSpace(mImageSize);

  image->SetData(data);
  return image;
}
