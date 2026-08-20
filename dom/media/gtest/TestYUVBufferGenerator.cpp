/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageContainer.h"
#include "YUVBufferGenerator.h"
#include "gtest/gtest.h"
#include "mozilla/RefPtr.h"

using mozilla::gfx::IntSize;
using mozilla::layers::PlanarYCbCrImage;

// Init() sizes its source buffer from the luma plane length, but the image
// creators lay out two chroma planes of ceil(width / 2) x ceil(height / 2).
// Those two sizes agree only for even dimensions, so at odd dimensions the
// second plane does not fit in what Init() reserved. The shortfall is
// observable because the affected bytes do not hold the value Init() filled
// with.
TEST(YUVBufferGenerator, I420ChromaPlanesAreFullyInitialised)
{
  static const IntSize kSizes[] = {
      {65, 64}, {64, 65}, {65, 65}, {64, 64}, {66, 66}};

  for (const IntSize& size : kSizes) {
    YUVBufferGenerator generator;
    generator.Init(size);

    RefPtr<mozilla::layers::Image> image = generator.GenerateI420Image();
    ASSERT_TRUE(!!image)
    << size.width << "x" << size.height << ": failed to generate an image";

    PlanarYCbCrImage* planar = image->AsPlanarYCbCrImage();
    ASSERT_TRUE(!!planar)
    << size.width << "x" << size.height << ": image is not planar YCbCr";

    const PlanarYCbCrImage::Data* data = planar->GetData();
    ASSERT_TRUE(!!data)
    << size.width << "x" << size.height << ": no data";

    const IntSize chromaSize = data->CbCrDataSize();
    ASSERT_GT(chromaSize.width, 0);
    ASSERT_GT(chromaSize.height, 0);

    // Init() memsets everything it reserved, so every byte within that region
    // holds one repeated value. Cb is the first chroma plane and always fits,
    // so its first byte is the fill value either way. Reading it rather than
    // hardcoding keeps this test independent of Init()'s choice of byte, and
    // comparing both planes against it means a non-uniform fill fails on Cb.
    const uint8_t expectedFill = data->mCbChannel[0];

    const uint8_t* planes[] = {data->mCbChannel, data->mCrChannel};
    const char* names[] = {"Cb", "Cr"};

    for (size_t p = 0; p < 2; ++p) {
      size_t mismatches = 0;
      int32_t firstBadRow = -1;
      int32_t firstBadCol = -1;
      uint8_t firstBadValue = 0;

      for (int32_t row = 0; row < chromaSize.height; ++row) {
        const uint8_t* rowPtr = planes[p] + row * data->mCbCrStride;
        for (int32_t col = 0; col < chromaSize.width; ++col) {
          const uint8_t actual = rowPtr[col];
          if (actual != expectedFill) {
            if (firstBadRow < 0) {
              firstBadRow = row;
              firstBadCol = col;
              firstBadValue = actual;
            }
            ++mismatches;
          }
        }
      }

      EXPECT_EQ(mismatches, size_t(0))
          << size.width << "x" << size.height << ": " << mismatches << " of "
          << (chromaSize.width * chromaSize.height) << " bytes in the "
          << names[p] << " plane do not match the fill value 0x" << std::hex
          << int(expectedFill) << std::dec << "; first at row " << firstBadRow
          << " col " << firstBadCol << " = 0x" << std::hex << int(firstBadValue)
          << std::dec
          << ". Either the source buffer is short of "
             "2 * ceil(width / 2) * ceil(height / 2), or Init() no longer "
             "fills "
             "the chroma region with a single repeated byte.";
    }
  }
}
