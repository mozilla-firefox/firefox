/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <iterator>
#include <memory>

#include "ImageContainer.h"
#include "ImageConversion.h"
#include "SourceSurfaceRawData.h"
#include "YUVBufferGenerator.h"
#include "gtest/gtest.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/MemoryChecking.h"
#include "mozilla/RefPtr.h"
#include "mozilla/dom/ImageBitmapBinding.h"
#include "mozilla/dom/ImageUtils.h"

using mozilla::CheckedInt;
using mozilla::ConvertToI420;
using mozilla::ConvertToNV12;
using mozilla::ConvertToRGBA;
using mozilla::MakeAndAddRef;
using mozilla::MakeRefPtr;
using mozilla::Maybe;
using mozilla::Nothing;
using mozilla::Some;
using mozilla::dom::ImageBitmapFormat;
using mozilla::gfx::ChromaSize;
using mozilla::gfx::ChromaSubsampling;
using mozilla::gfx::ColorRange;
using mozilla::gfx::DataSourceSurface;
using mozilla::gfx::IntPoint;
using mozilla::gfx::IntRect;
using mozilla::gfx::IntSize;
using mozilla::gfx::SourceSurfaceAlignedRawData;
using mozilla::gfx::SurfaceFormat;
using mozilla::gfx::YUVColorSpace;
using mozilla::layers::Image;
using mozilla::layers::PlanarYCbCrImage;
using mozilla::layers::SourceSurfaceImage;

namespace {

// A Y/Cb/Cr sample triple used to paint and verify a planar image.
struct YCbCrValue {
  uint8_t mY;
  uint8_t mCb;
  uint8_t mCr;
};

// An R/G/B sample triple used to paint a surface, with the value for the
// fourth byte, alpha or padding.
struct RGBValue {
  uint8_t mR;
  uint8_t mG;
  uint8_t mB;
  uint8_t mA = 0xFF;
};

constexpr RGBValue kRGBRed{0xFF, 0x00, 0x00};
constexpr RGBValue kRGBGreen{0x00, 0xFF, 0x00};
constexpr RGBValue kRGBBlue{0x00, 0x00, 0xFF};
constexpr RGBValue kRGBWhite{0xFF, 0xFF, 0xFF};

// Red in BT.601 limited range, which every red RGB fixture converts to.
constexpr YCbCrValue kYCbCrRed{0x52, 0x5A, 0xEF};

// How many luma samples one chroma sample spans on each axis.
IntSize ChromaDivisor(ChromaSubsampling aSubsampling) {
  switch (aSubsampling) {
    case ChromaSubsampling::FULL:
      return IntSize(1, 1);
    case ChromaSubsampling::HALF_WIDTH:
      return IntSize(2, 1);
    case ChromaSubsampling::HALF_WIDTH_AND_HEIGHT:
      return IntSize(2, 2);
  }
  MOZ_CRASH("bad ChromaSubsampling");
}

// PlanarYCbCrImage in any of the planar and semi-planar layouts the conversion
// reads, backed by planes it owns. The coded buffer is painted aBorder and the
// picture rect aContent; Fill() repaints a region afterwards. A region must
// start on a chroma sample and end on one or at the coded buffer's edge.
class TestPlanarYCbCrImage final : public PlanarYCbCrImage {
 public:
  TestPlanarYCbCrImage(const IntSize& aSize, const YCbCrValue& aColor,
                       ImageBitmapFormat aFormat = ImageBitmapFormat::YUV420P)
      : TestPlanarYCbCrImage(aSize, IntRect(IntPoint(), aSize), aColor, aColor,
                             aFormat) {}

  TestPlanarYCbCrImage(const IntSize& aCodedSize, const IntRect& aPictureRect,
                       const YCbCrValue& aBorder, const YCbCrValue& aContent,
                       ImageBitmapFormat aFormat = ImageBitmapFormat::YUV420P)
      : mFormat(aFormat), mCodedSize(aCodedSize) {
    MOZ_ASSERT(!aPictureRect.IsEmpty());
    MOZ_ASSERT(IntRect(IntPoint(), aCodedSize).Contains(aPictureRect));

    switch (mFormat) {
      case ImageBitmapFormat::YUV420P:
      case ImageBitmapFormat::YUV420SP_NV12:
      case ImageBitmapFormat::YUV420SP_NV21:
        mData.mChromaSubsampling = ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;
        break;
      case ImageBitmapFormat::YUV422P:
        mData.mChromaSubsampling = ChromaSubsampling::HALF_WIDTH;
        break;
      case ImageBitmapFormat::YUV444P:
        mData.mChromaSubsampling = ChromaSubsampling::FULL;
        break;
      default:
        MOZ_CRASH("Unsupported ImageBitmapFormat!");
    }

    const IntSize chromaSize = ChromaSize(aCodedSize, mData.mChromaSubsampling);
    const CheckedInt<size_t> ySize =
        CheckedInt<size_t>(aCodedSize.width) * aCodedSize.height;
    const CheckedInt<size_t> cSize =
        CheckedInt<size_t>(chromaSize.width) * chromaSize.height;
    MOZ_ASSERT((ySize + cSize * 2).isValid(), "plane sizes are not valid");
    mY.SetLength(ySize.value());
    mCb.SetLength(IsInterleaved() ? cSize.value() * 2 : cSize.value());
    mCr.SetLength(IsInterleaved() ? 0 : cSize.value());

    mSize = aPictureRect.Size();
    mBufferSize = (ySize + cSize * 2).value();
    mData.mPictureRect = aPictureRect;
    mData.mYChannel = mY.Elements();
    mData.mYStride = aCodedSize.width;
    if (IsInterleaved()) {
      // NV12 interleaves Cb then Cr, NV21 the other way round.
      const bool nv12 = mFormat == ImageBitmapFormat::YUV420SP_NV12;
      mData.mCbChannel = mCb.Elements() + (nv12 ? 0 : 1);
      mData.mCrChannel = mCb.Elements() + (nv12 ? 1 : 0);
      mData.mCbCrStride = 2 * chromaSize.width;
      mData.mCbSkip = 1;
      mData.mCrSkip = 1;
    } else {
      mData.mCbChannel = mCb.Elements();
      mData.mCrChannel = mCr.Elements();
      mData.mCbCrStride = chromaSize.width;
    }

    Fill(IntRect(IntPoint(), aCodedSize), aBorder);
    Fill(aPictureRect, aContent);
  }

  // Paints aRect, in luma samples of the coded buffer, with aColor.
  void Fill(const IntRect& aRect, const YCbCrValue& aColor) {
    const IntSize divisor = ChromaDivisor(mData.mChromaSubsampling);
    MOZ_ASSERT(aRect.x % divisor.width == 0 && aRect.y % divisor.height == 0);
    MOZ_ASSERT(aRect.XMost() % divisor.width == 0 ||
               aRect.XMost() == mCodedSize.width);
    MOZ_ASSERT(aRect.YMost() % divisor.height == 0 ||
               aRect.YMost() == mCodedSize.height);

    for (int32_t row = aRect.y; row < aRect.YMost(); ++row) {
      memset(mData.mYChannel + size_t(row) * mData.mYStride + aRect.x,
             aColor.mY, aRect.width);
    }
    // The chroma samples covering the rect, rounding up at the coded edge.
    const int32_t left = aRect.x / divisor.width;
    const int32_t top = aRect.y / divisor.height;
    const int32_t right = (aRect.XMost() + divisor.width - 1) / divisor.width;
    const int32_t bottom =
        (aRect.YMost() + divisor.height - 1) / divisor.height;
    for (int32_t row = top; row < bottom; ++row) {
      const size_t offset = size_t(row) * mData.mCbCrStride;
      for (int32_t col = left; col < right; ++col) {
        mData.mCbChannel[offset + size_t(col) * (mData.mCbSkip + 1)] =
            aColor.mCb;
        mData.mCrChannel[offset + size_t(col) * (mData.mCrSkip + 1)] =
            aColor.mCr;
      }
    }
  }

  nsresult CopyData(const Data& aData) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  size_t SizeOfExcludingThis(mozilla::MallocSizeOf) const { return 0; }

 private:
  bool IsInterleaved() const {
    return mFormat == ImageBitmapFormat::YUV420SP_NV12 ||
           mFormat == ImageBitmapFormat::YUV420SP_NV21;
  }

  const ImageBitmapFormat mFormat;
  const IntSize mCodedSize;
  nsTArray<uint8_t> mY;
  // The chroma planes, or for NV12 and NV21 the interleaved plane in mCb.
  nsTArray<uint8_t> mCb;
  nsTArray<uint8_t> mCr;
};

}  // namespace

// YUVBufferGenerator rejects dimensions above PlanarYCbCrImage::MAX_DIMENSION,
// which is below kMaxConvertImageDimension, so the bounds tests build their
// source directly.
static already_AddRefed<Image> GenerateI420(int32_t aWidth, int32_t aHeight) {
  const IntSize size(aWidth, aHeight);
  const YCbCrValue black{0x10, 0x80, 0x80};
  return MakeAndAddRef<TestPlanarYCbCrImage>(size, black);
}

static already_AddRefed<SourceSurfaceImage> CreateSolidSurfaceImage(
    const IntSize& aSize, SurfaceFormat aFormat, const RGBValue& aColor) {
  uint8_t pixel[4] = {};

  switch (aFormat) {
    case SurfaceFormat::R8G8B8A8:
    case SurfaceFormat::R8G8B8X8:
      pixel[0] = aColor.mR;
      pixel[1] = aColor.mG;
      pixel[2] = aColor.mB;
      pixel[3] = aColor.mA;
      break;
    case SurfaceFormat::B8G8R8A8:
    case SurfaceFormat::B8G8R8X8:
      pixel[0] = aColor.mB;
      pixel[1] = aColor.mG;
      pixel[2] = aColor.mR;
      pixel[3] = aColor.mA;
      break;
    case SurfaceFormat::R5G6B5_UINT16: {
      const uint16_t rgb565 =
          ((aColor.mR >> 3) << 11) | ((aColor.mG >> 2) << 5) | (aColor.mB >> 3);
      pixel[0] = rgb565 & 0xFF;
      pixel[1] = rgb565 >> 8;
      break;
    }
    default:
      MOZ_ASSERT_UNREACHABLE("Unsupported format!");
      return nullptr;
  }

  auto surface = MakeRefPtr<SourceSurfaceAlignedRawData>();
  if (NS_WARN_IF(!surface->Init(aSize, aFormat, /* aClearMem */ false, 0, 0))) {
    return nullptr;
  }

  DataSourceSurface::ScopedMap map(surface, DataSourceSurface::WRITE);
  if (NS_WARN_IF(!map.IsMapped())) {
    return nullptr;
  }

  const uint32_t bpp = BytesPerPixel(aFormat);
  MOZ_ASSERT(bpp <= sizeof(pixel));

  uint8_t* rowPtr = map.GetData();
  for (int32_t row = 0; row < aSize.height; ++row) {
    for (int32_t col = 0; col < aSize.width; ++col) {
      for (uint32_t i = 0; i < bpp; ++i) {
        rowPtr[col * bpp + i] = pixel[i];
      }
    }
    rowPtr += map.GetStride();
  }

  return MakeAndAddRef<SourceSurfaceImage>(aSize, surface);
}

static already_AddRefed<SourceSurfaceImage> CreateSurfaceImage(
    const IntSize& aSurfaceSize, const IntSize& aImageSize,
    SurfaceFormat aFormat = SurfaceFormat::R8G8B8A8) {
  auto surface = MakeRefPtr<SourceSurfaceAlignedRawData>();
  if (NS_WARN_IF(!surface->Init(aSurfaceSize, aFormat,
                                /* aClearMem */ true, 0, 0))) {
    return nullptr;
  }
  return MakeAndAddRef<SourceSurfaceImage>(aImageSize, surface);
}

TEST(MediaImageConversion, ConvertToRGBASourceSurfaceExtent)
{
  // RGBA destination sized for the whole image, zero-initialized.
  constexpr IntSize imageSize(2, 2);
  constexpr int destStride = imageSize.width * 4;
  uint8_t dest[imageSize.width * imageSize.height * 4] = {};

  // Source surface matches the image size: the conversion succeeds.
  RefPtr<SourceSurfaceImage> matched = CreateSurfaceImage(imageSize, imageSize);
  ASSERT_TRUE(!!matched);
  EXPECT_TRUE(NS_SUCCEEDED(
      ConvertToRGBA(matched, SurfaceFormat::R8G8B8A8, dest, destStride)));

  // Source surface smaller than the image size: the conversion is refused.
  RefPtr<SourceSurfaceImage> undersized =
      CreateSurfaceImage(IntSize(2, 1), imageSize);
  ASSERT_TRUE(!!undersized);
  EXPECT_TRUE(NS_FAILED(
      ConvertToRGBA(undersized, SurfaceFormat::R8G8B8A8, dest, destStride)));
}

// As CreateRedSurfaceImage2x2 above, parameterised by size. Restricted to BGR*
// because ConvertToNV12 only accepts B8G8R8A8/B8G8R8X8 surfaces.
static already_AddRefed<SourceSurfaceImage> CreateRedSurfaceImage(
    const IntSize& aSize, SurfaceFormat aFormat) {
  uint8_t redPixel[4] = {};

  switch (aFormat) {
    case SurfaceFormat::B8G8R8A8:
    case SurfaceFormat::B8G8R8X8:
      redPixel[2] = 0xFF;
      redPixel[3] = 0xFF;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("ConvertToNV12 only accepts BGR* surfaces!");
      return nullptr;
  }

  auto surface = MakeRefPtr<SourceSurfaceAlignedRawData>();
  if (NS_WARN_IF(!surface->Init(aSize, aFormat, /* aClearMem */ false, 0, 0))) {
    return nullptr;
  }

  {
    DataSourceSurface::ScopedMap map(surface, DataSourceSurface::WRITE);
    if (NS_WARN_IF(!map.IsMapped())) {
      return nullptr;
    }

    const uint32_t bpp = BytesPerPixel(aFormat);
    MOZ_ASSERT(bpp <= sizeof(redPixel));

    uint8_t* rowPtr = map.GetData();
    for (int32_t row = 0; row < aSize.height; ++row) {
      for (int32_t col = 0; col < aSize.width; ++col) {
        for (uint32_t i = 0; i < bpp; ++i) {
          rowPtr[col * bpp + i] = redPixel[i];
        }
      }
      rowPtr += map.GetStride();
    }
  }

  return MakeAndAddRef<SourceSurfaceImage>(aSize, surface);
}

// Distinct from every byte the source images can produce.
static constexpr uint8_t kNV12TrailingPattern = 0xA5;
static constexpr size_t kNV12TrailingBytes = 64;

// Strides of the frame width, and a length covering the luma plane plus the
// interleaved chroma plane, followed by a trailing region of a known pattern so
// the test can tell whether anything was stored outside the declared length.
class NV12DestBuffer {
 public:
  explicit NV12DestBuffer(const IntSize& aSize)
      : mYStride(aSize.width),
        mUVStride(aSize.width),
        mYLength(static_cast<size_t>(aSize.width) * aSize.height),
        mUVLength(static_cast<size_t>(aSize.width) *
                  (aSize.height / 2 + aSize.height % 2)),
        mBufferLength(mYLength + mUVLength) {
    mBuffer.SetLength(mBufferLength + kNV12TrailingBytes);
    memset(mBuffer.Elements(), 0, mBufferLength);
    memset(mBuffer.Elements() + mBufferLength, kNV12TrailingPattern,
           kNV12TrailingBytes);
  }

  ~NV12DestBuffer() { MarkTrailingAccessible(); }

  uint8_t* DestY() { return mBuffer.Elements(); }
  uint8_t* DestUV() { return mBuffer.Elements() + mYLength; }

  // Under ASan or MSan this reports at the point of the store; elsewhere it is
  // a no-op, so DescribeModifiedTrailingBytes stays the portable check.
  void MarkTrailingInaccessible() {
    MOZ_MAKE_MEM_NOACCESS(mBuffer.Elements() + mBufferLength,
                          kNV12TrailingBytes);
    mTrailingMarked = true;
  }

  void MarkTrailingAccessible() {
    if (mTrailingMarked) {
      MOZ_MAKE_MEM_DEFINED(mBuffer.Elements() + mBufferLength,
                           kNV12TrailingBytes);
      mTrailingMarked = false;
    }
  }

  static const char* DetectorDescription() {
#ifdef MOZ_HAVE_MEM_CHECKS
    return "trailing pattern + sanitizer marking";
#else
    return "trailing pattern only (no sanitizer in this build)";
#endif
  }

  nsCString DescribeModifiedTrailingBytes(size_t* aCount) const {
    nsCString description;
    *aCount = 0;
    for (size_t i = 0; i < kNV12TrailingBytes; ++i) {
      const uint8_t actual = mBuffer[mBufferLength + i];
      if (actual != kNV12TrailingPattern) {
        ++*aCount;
        description.AppendPrintf(" mBufferLength+%zu=0x%02X", i, actual);
      }
    }
    return description;
  }

  const int32_t mYStride;
  const int32_t mUVStride;
  const size_t mYLength;
  const size_t mUVLength;
  const size_t mBufferLength;

 private:
  nsTArray<uint8_t> mBuffer;
  bool mTrailingMarked = false;
};

// ConvertToNV12 must decline a uvStride that cannot hold an interleaved chroma
// row, and must convert when it can.
TEST(MediaImageConversion, ConvertToNV12RejectsShortDestStride)
{
  enum class Source { I420, BGRA };
  enum class Expect { Converts, Refuses };

  struct Case {
    const char* mName;
    Source mSource;
    IntSize mImageSize;
    IntSize mDestSize;
    Expect mExpected;
  };

  static const Case kCases[] = {
      {"I420 65x64 -> 65x64 (odd width)",
       Source::I420,
       {65, 64},
       {65, 64},
       Expect::Refuses},
      {"I420 65x65 -> 65x65 (odd width and height)",
       Source::I420,
       {65, 65},
       {65, 65},
       Expect::Refuses},
      {"I420 641x480 -> 641x480 (odd width above the 128px HW floor)",
       Source::I420,
       {641, 480},
       {641, 480},
       Expect::Refuses},
      {"BGRA 65x64 -> 65x64 (odd width)",
       Source::BGRA,
       {65, 64},
       {65, 64},
       Expect::Refuses},
      {"BGRA 641x480 -> 641x480 (odd width above the 128px HW floor)",
       Source::BGRA,
       {641, 480},
       {641, 480},
       Expect::Refuses},

      // Odd width with a rescale, so the I420Scale / ARGBScale prologues run.
      {"I420 130x128 -> 65x64 (odd width, rescaled)",
       Source::I420,
       {130, 128},
       {65, 64},
       Expect::Refuses},
      {"BGRA 130x128 -> 65x64 (odd width, rescaled)",
       Source::BGRA,
       {130, 128},
       {65, 64},
       Expect::Refuses},

      // Even widths: these must convert.
      {"I420 64x64 -> 64x64 (even control)",
       Source::I420,
       {64, 64},
       {64, 64},
       Expect::Converts},
      {"I420 64x65 -> 64x65 (odd height only, even width)",
       Source::I420,
       {64, 65},
       {64, 65},
       Expect::Converts},
      {"I420 128x128 -> 64x64 (even control, rescaled)",
       Source::I420,
       {128, 128},
       {64, 64},
       Expect::Converts},
      {"BGRA 64x64 -> 64x64 (even control)",
       Source::BGRA,
       {64, 64},
       {64, 64},
       Expect::Converts},
      {"BGRA 128x128 -> 64x64 (even control, rescaled)",
       Source::BGRA,
       {128, 128},
       {64, 64},
       Expect::Converts},
  };

  GTEST_LOG_(INFO) << "excess detector in this build: "
                   << NV12DestBuffer::DetectorDescription();

  for (const Case& testCase : kCases) {
    RefPtr<mozilla::layers::Image> image;
    YUVBufferGenerator generator;
    switch (testCase.mSource) {
      case Source::I420:
        generator.Init(testCase.mImageSize);
        image = generator.GenerateI420Image();
        break;
      case Source::BGRA:
        image =
            CreateRedSurfaceImage(testCase.mImageSize, SurfaceFormat::B8G8R8A8);
        break;
    }
    ASSERT_TRUE(!!image)
    << testCase.mName << ": failed to build source image";

    NV12DestBuffer dest(testCase.mDestSize);

    // If this stops holding, the odd cases would convert and pass vacuously.
    const int32_t chromaRowBytes = 2 * ((testCase.mDestSize.width + 1) / 2);
    const int32_t expectedExcess = testCase.mDestSize.width % 2 ? 1 : 0;
    ASSERT_EQ(chromaRowBytes - dest.mUVStride, expectedExcess)
        << testCase.mName << ": premise broken. An NV12 chroma row is "
        << chromaRowBytes << " bytes against a stride of " << dest.mUVStride
        << ", i.e. an excess of " << (chromaRowBytes - dest.mUVStride)
        << " rather than the expected " << expectedExcess
        << ". Either the NV12 row length or this test's model of it changed; "
        << "correct the model before trusting any result below.";

    // Must be intact going in, or the check afterwards proves nothing.
    size_t before = 0;
    dest.DescribeModifiedTrailingBytes(&before);
    ASSERT_EQ(before, size_t(0))
        << testCase.mName
        << ": trailing region was already disturbed before the conversion";

    dest.MarkTrailingInaccessible();

    const nsresult rv =
        ConvertToNV12(image, dest.DestY(), dest.mYStride, dest.DestUV(),
                      dest.mUVStride, testCase.mDestSize);

    dest.MarkTrailingAccessible();

    if (testCase.mExpected == Expect::Refuses) {
      EXPECT_EQ(rv, NS_ERROR_INVALID_ARG)
          << testCase.mName << ": a uvStride of " << dest.mUVStride
          << " cannot hold a " << chromaRowBytes
          << "-byte NV12 chroma row, so ConvertToNV12 must refuse it rather "
             "than convert";
    } else {
      EXPECT_TRUE(NS_SUCCEEDED(rv))
          << testCase.mName << ": ConvertToNV12 unexpectedly failed";
    }

    size_t modified = 0;
    const nsCString detail = dest.DescribeModifiedTrailingBytes(&modified);
    EXPECT_EQ(modified, size_t(0))
        << testCase.mName << ": ConvertToNV12 wrote " << modified
        << " byte(s) outside the declared length of a " << dest.mBufferLength
        << "-byte NV12 buffer (yLength=" << dest.mYLength
        << ", uvLength=" << dest.mUVLength << ", yStride=" << dest.mYStride
        << ", uvStride=" << dest.mUVStride
        << "); all trailing bytes should still be 0xA5, modified:"
        << detail.get();
  }
}

TEST(MediaImageConversion, ConvertToI420)
{
  uint8_t y[20] = {};
  uint8_t u[20] = {};
  uint8_t v[20] = {};

  auto checkBuf = [&](const uint8_t* aY, const uint8_t* aU, const uint8_t* aV) {
    for (size_t i = 0; i < sizeof(y); ++i) {
      EXPECT_EQ(y[i], aY[i]);
    }
    for (size_t i = 0; i < sizeof(u); ++i) {
      EXPECT_EQ(u[i], aU[i]);
    }
    for (size_t i = 0; i < sizeof(v); ++i) {
      EXPECT_EQ(v[i], aV[i]);
    }
    memset(y, 0, sizeof(y));
    memset(u, 0, sizeof(u));
    memset(v, 0, sizeof(v));
  };

  static constexpr uint8_t yRed1x1[20] = {0x52};
  static constexpr uint8_t yRed2x2[20] = {0x52, 0x52, 0x52, 0x52};
  static constexpr uint8_t yRed4x4[20] = {0x52, 0x52, 0x52, 0x52, 0x52, 0x52,
                                          0x52, 0x52, 0x52, 0x52, 0x52, 0x52,
                                          0x52, 0x52, 0x52, 0x52};

  static constexpr uint8_t uRed1x1[20] = {0x5A};
  static constexpr uint8_t uRed2x2[20] = {0x5A, 0x5A, 0x5A, 0x5A};

  static constexpr uint8_t vRed1x1[20] = {0xEF};
  static constexpr uint8_t vRed2x2[20] = {0xEF, 0xEF, 0xEF, 0xEF};

  auto checkImage = [&](mozilla::layers::Image* aImage,
                        const Maybe<ImageBitmapFormat>& aFormat) {
    ASSERT_TRUE(!!aImage);

    mozilla::dom::ImageUtils utils(aImage);
    Maybe<ImageBitmapFormat> format = utils.GetFormat();
    ASSERT_EQ(format.isSome(), aFormat.isSome());
    if (format.isSome()) {
      ASSERT_EQ(format.value(), aFormat.value());
    }

    EXPECT_TRUE(
        NS_SUCCEEDED(ConvertToI420(aImage, y, 2, u, 1, v, 1, IntSize(2, 2))));
    checkBuf(yRed2x2, uRed1x1, vRed1x1);

    EXPECT_TRUE(
        NS_SUCCEEDED(ConvertToI420(aImage, y, 1, u, 1, v, 1, IntSize(1, 1))));
    checkBuf(yRed1x1, uRed1x1, vRed1x1);

    EXPECT_TRUE(
        NS_SUCCEEDED(ConvertToI420(aImage, y, 4, u, 2, v, 2, IntSize(4, 4))));
    checkBuf(yRed4x4, uRed2x2, vRed2x2);
  };

  RefPtr<SourceSurfaceImage> imgRgba =
      CreateSolidSurfaceImage(IntSize(2, 2), SurfaceFormat::R8G8B8A8, kRGBRed);
  checkImage(imgRgba, Some(ImageBitmapFormat::RGBA32));

  RefPtr<SourceSurfaceImage> imgBgra =
      CreateSolidSurfaceImage(IntSize(2, 2), SurfaceFormat::B8G8R8A8, kRGBRed);
  checkImage(imgBgra, Some(ImageBitmapFormat::BGRA32));

  RefPtr<SourceSurfaceImage> imgRgb565 = CreateSolidSurfaceImage(
      IntSize(2, 2), SurfaceFormat::R5G6B5_UINT16, kRGBRed);
  checkImage(imgRgb565, Nothing());

  auto imgYuv420p = MakeRefPtr<TestPlanarYCbCrImage>(
      IntSize(2, 2), kYCbCrRed, ImageBitmapFormat::YUV420P);
  checkImage(imgYuv420p, Some(ImageBitmapFormat::YUV420P));

  auto imgYuv422p = MakeRefPtr<TestPlanarYCbCrImage>(
      IntSize(2, 2), kYCbCrRed, ImageBitmapFormat::YUV422P);
  checkImage(imgYuv422p, Some(ImageBitmapFormat::YUV422P));

  auto imgYuv444p = MakeRefPtr<TestPlanarYCbCrImage>(
      IntSize(2, 2), kYCbCrRed, ImageBitmapFormat::YUV444P);
  checkImage(imgYuv444p, Some(ImageBitmapFormat::YUV444P));

  auto imgYuvNv12 = MakeRefPtr<TestPlanarYCbCrImage>(
      IntSize(2, 2), kYCbCrRed, ImageBitmapFormat::YUV420SP_NV12);
  checkImage(imgYuvNv12, Some(ImageBitmapFormat::YUV420SP_NV12));

  auto imgYuvNv21 = MakeRefPtr<TestPlanarYCbCrImage>(
      IntSize(2, 2), kYCbCrRed, ImageBitmapFormat::YUV420SP_NV21);
  checkImage(imgYuvNv21, Some(ImageBitmapFormat::YUV420SP_NV21));
}

// The smallest of the pair of source dimensions used by the bounds tests.
static constexpr int32_t kSmallDimension = 16;
// At or above kMaxConvertImageDimension the source is rejected before scaling.
static constexpr int32_t kOverLimitDimension =
    mozilla::kMaxConvertImageDimension;
// The largest even in-range dimension (odd dimensions hit an unrelated
// chroma-subsampling limit in the conversion, so use even).
static constexpr int32_t kInRangeDimension =
    mozilla::kMaxConvertImageDimension - 2;

TEST(MediaImageConversion, ConvertToI420SourceSizeBounds)
{
  uint8_t y[4] = {};
  uint8_t u[1] = {};
  uint8_t v[1] = {};
  const IntSize dst(2, 2);

  RefPtr<Image> tall = GenerateI420(kSmallDimension, kOverLimitDimension);
  EXPECT_EQ(ConvertToI420(tall, y, 2, u, 1, v, 1, dst), NS_ERROR_INVALID_ARG);

  RefPtr<Image> wide = GenerateI420(kOverLimitDimension, kSmallDimension);
  EXPECT_EQ(ConvertToI420(wide, y, 2, u, 1, v, 1, dst), NS_ERROR_INVALID_ARG);

  RefPtr<Image> maxTall = GenerateI420(kSmallDimension, kInRangeDimension);
  EXPECT_TRUE(NS_SUCCEEDED(ConvertToI420(maxTall, y, 2, u, 1, v, 1, dst)));

  RefPtr<Image> maxWide = GenerateI420(kInRangeDimension, kSmallDimension);
  EXPECT_TRUE(NS_SUCCEEDED(ConvertToI420(maxWide, y, 2, u, 1, v, 1, dst)));
}

// ConvertToI420 must reject destination strides that are too small for one
// output row, like ConvertToNV12 has done since bug 2050150: a luma stride
// smaller than the width, or a chroma stride smaller than ceil(width / 2),
// would make libyuv write a full row and then advance by the stride, so the
// rows overlap and can write past a plane buffer sized stride * height.
TEST(MediaImageConversion, ConvertToI420DestinationStrideBounds)
{
  uint8_t y[16] = {};
  uint8_t u[8] = {};
  uint8_t v[8] = {};

  RefPtr<Image> image = GenerateI420(4, 4);
  ASSERT_TRUE(!!image);

  // Strides that fit one output row are accepted.
  const IntSize dst(4, 4);
  EXPECT_TRUE(NS_SUCCEEDED(ConvertToI420(image, y, 4, u, 2, v, 2, dst)));

  // A luma stride smaller than the destination width is rejected.
  EXPECT_EQ(NS_ERROR_INVALID_ARG, ConvertToI420(image, y, 3, u, 2, v, 2, dst));

  // Chroma strides smaller than ceil(width / 2) are rejected.
  EXPECT_EQ(NS_ERROR_INVALID_ARG, ConvertToI420(image, y, 4, u, 1, v, 2, dst));
  EXPECT_EQ(NS_ERROR_INVALID_ARG, ConvertToI420(image, y, 4, u, 2, v, 1, dst));

  // For an odd width the chroma row is ceil(width / 2) bytes, so a stride of
  // width / 2 is rejected -- the same trap bug 2050150 caught for NV12.
  const IntSize oddDst(3, 2);
  EXPECT_EQ(NS_ERROR_INVALID_ARG,
            ConvertToI420(image, y, 3, u, 1, v, 2, oddDst));
  EXPECT_EQ(NS_ERROR_INVALID_ARG,
            ConvertToI420(image, y, 3, u, 2, v, 1, oddDst));
}

TEST(MediaImageConversion, ConvertToNV12SourceSizeBounds)
{
  uint8_t y[4] = {};
  uint8_t uv[2] = {};
  const IntSize dst(2, 2);

  RefPtr<Image> tall = GenerateI420(kSmallDimension, kOverLimitDimension);
  EXPECT_EQ(ConvertToNV12(tall, y, 2, uv, 2, dst), NS_ERROR_INVALID_ARG);

  RefPtr<Image> wide = GenerateI420(kOverLimitDimension, kSmallDimension);
  EXPECT_EQ(ConvertToNV12(wide, y, 2, uv, 2, dst), NS_ERROR_INVALID_ARG);

  RefPtr<Image> maxTall = GenerateI420(kSmallDimension, kInRangeDimension);
  EXPECT_TRUE(NS_SUCCEEDED(ConvertToNV12(maxTall, y, 2, uv, 2, dst)));

  RefPtr<Image> maxWide = GenerateI420(kInRangeDimension, kSmallDimension);
  EXPECT_TRUE(NS_SUCCEEDED(ConvertToNV12(maxWide, y, 2, uv, 2, dst)));
}

// The surface returned by GetAsSourceSurface() must cover the image's
// reported GetSize(); conversion rejects an image whose surface is smaller
// than that size.
TEST(MediaImageConversion, UndersizedSourceSurface)
{
  const IntSize reportedSize(64, 64);
  const size_t planeSize =
      static_cast<size_t>(reportedSize.width) * reportedSize.height;

  // Destination buffers, reused across both cases.
  auto destY = std::make_unique<uint8_t[]>(planeSize);
  auto destUV = std::make_unique<uint8_t[]>(planeSize);
  auto destU = std::make_unique<uint8_t[]>(planeSize);
  auto destV = std::make_unique<uint8_t[]>(planeSize);

  {
    // Undersized surface: it does not cover the reported image size, so every
    // conversion must be rejected regardless of the requested destination
    // size. The rejection is driven by the surface size, not the destination.
    const IntSize undersizedSurface(60, 60);
    RefPtr<SourceSurfaceImage> undersizedImage = CreateSurfaceImage(
        undersizedSurface, reportedSize, SurfaceFormat::B8G8R8A8);
    ASSERT_TRUE(!!undersizedImage);
    ASSERT_EQ(undersizedImage->GetSize(), reportedSize);

    // Destination equal to the reported size.
    EXPECT_EQ(NS_ERROR_INVALID_ARG,
              ConvertToNV12(undersizedImage, destY.get(), reportedSize.width,
                            destUV.get(), reportedSize.width, reportedSize));
    EXPECT_EQ(NS_ERROR_INVALID_ARG,
              ConvertToI420(undersizedImage, destY.get(), reportedSize.width,
                            destU.get(), reportedSize.width, destV.get(),
                            reportedSize.width, reportedSize));

    // Destination smaller than the reported size (scaling path).
    const IntSize scaledSize(32, 32);
    EXPECT_EQ(NS_ERROR_INVALID_ARG,
              ConvertToNV12(undersizedImage, destY.get(), scaledSize.width,
                            destUV.get(), scaledSize.width, scaledSize));
    EXPECT_EQ(NS_ERROR_INVALID_ARG,
              ConvertToI420(undersizedImage, destY.get(), scaledSize.width,
                            destU.get(), scaledSize.width, destV.get(),
                            scaledSize.width, scaledSize));
  }

  {
    // Covering surface: it exactly covers the reported image size, so the
    // conversion must be accepted.
    RefPtr<SourceSurfaceImage> exactImage =
        CreateSurfaceImage(reportedSize, reportedSize, SurfaceFormat::B8G8R8A8);
    ASSERT_TRUE(!!exactImage);
    ASSERT_EQ(exactImage->GetSize(), reportedSize);

    EXPECT_EQ(NS_OK,
              ConvertToNV12(exactImage, destY.get(), reportedSize.width,
                            destUV.get(), reportedSize.width, reportedSize));
    EXPECT_EQ(NS_OK, ConvertToI420(exactImage, destY.get(), reportedSize.width,
                                   destU.get(), reportedSize.width, destV.get(),
                                   reportedSize.width, reportedSize));
  }
}

// ConvertToI420()/ConvertToNV12() must convert the picture region defined by
// PlanarYCbCrData::mPictureRect, not the region anchored at the coded-buffer
// origin (0,0). The sibling YUV->RGB path (ConvertYCbCrToRGB) honors
// mPictureRect.TopLeft(); these tests guard that the I420/NV12 paths do the
// same. Each test builds an I420 source whose coded buffer is larger than the
// picture, fills the whole buffer with a "border" value and the picture
// sub-region with a distinct "content" value, then asserts the converted
// output carries the content value.
TEST(MediaImageConversion, ConvertToI420HonorsPictureRectOrigin)
{
  // Odd coded extents on purpose: the picture rect has to be even for chroma
  // alignment, but the buffer around it does not.
  const IntSize coded(65, 63);
  const IntRect picture(16, 8, 32, 32);
  const YCbCrValue border{0x10, 0x20, 0x30};
  const YCbCrValue content{0x80, 0xA0, 0xC0};
  auto image =
      MakeRefPtr<TestPlanarYCbCrImage>(coded, picture, border, content);

  const int32_t chromaW = picture.width / 2;
  const int32_t chromaH = picture.height / 2;
  nsTArray<uint8_t> destY;
  nsTArray<uint8_t> destU;
  nsTArray<uint8_t> destV;
  destY.SetLength(size_t(picture.width) * picture.height);
  destU.SetLength(size_t(chromaW) * chromaH);
  destV.SetLength(size_t(chromaW) * chromaH);

  ASSERT_TRUE(NS_SUCCEEDED(
      ConvertToI420(image, destY.Elements(), picture.width, destU.Elements(),
                    chromaW, destV.Elements(), chromaW, picture.Size())));

  for (const uint8_t& v : destY) {
    EXPECT_EQ(v, content.mY);
  }
  for (const uint8_t& v : destU) {
    EXPECT_EQ(v, content.mCb);
  }
  for (const uint8_t& v : destV) {
    EXPECT_EQ(v, content.mCr);
  }
}

TEST(MediaImageConversion, ConvertToNV12HonorsPictureRectOrigin)
{
  // Odd coded extents on purpose: the picture rect has to be even for chroma
  // alignment, but the buffer around it does not.
  const IntSize coded(65, 63);
  const IntRect picture(16, 8, 32, 32);
  const YCbCrValue border{0x10, 0x20, 0x30};
  const YCbCrValue content{0x80, 0xA0, 0xC0};
  auto image =
      MakeRefPtr<TestPlanarYCbCrImage>(coded, picture, border, content);

  nsTArray<uint8_t> destY;
  nsTArray<uint8_t> destUV;
  destY.SetLength(size_t(picture.width) * picture.height);
  destUV.SetLength(size_t(picture.width) * (picture.height / 2));

  ASSERT_TRUE(NS_SUCCEEDED(ConvertToNV12(image, destY.Elements(), picture.width,
                                         destUV.Elements(), picture.width,
                                         picture.Size())));

  for (const uint8_t& v : destY) {
    EXPECT_EQ(v, content.mY);
  }
  // NV12 interleaves U/V: even bytes are U, odd bytes are V.
  for (size_t i = 0; i < destUV.Length(); ++i) {
    EXPECT_EQ(destUV[i], (i % 2 == 0) ? content.mCb : content.mCr);
  }
}

namespace {

// Solid colors and, per RGB-to-YUV matrix, the samples libyuv's fixed-point
// coefficients produce for them.
struct RGBSample {
  const char* mName;
  RGBValue mRGB;
};

constexpr RGBSample kRGBSamples[] = {
    {"red", kRGBRed},
    {"green", kRGBGreen},
    {"blue", kRGBBlue},
    {"white", kRGBWhite},
};

struct RGBToYUVExpectation {
  YUVColorSpace mColorSpace;
  ColorRange mColorRange;
  // In kRGBSamples order.
  YCbCrValue mYUV[std::size(kRGBSamples)];
};

constexpr RGBToYUVExpectation kRGBToYUVExpectations[] = {
    {YUVColorSpace::BT601,
     ColorRange::LIMITED,
     {{0x52, 0x5A, 0xEF},
      {0x90, 0x36, 0x22},
      {0x29, 0xEF, 0x6E},
      {0xEB, 0x80, 0x80}}},
    {YUVColorSpace::BT601,
     ColorRange::FULL,
     {{0x4D, 0x55, 0xFF},
      {0x95, 0x2B, 0x15},
      {0x1D, 0xFF, 0x6B},
      {0xFF, 0x80, 0x80}}},
    {YUVColorSpace::BT709,
     ColorRange::LIMITED,
     {{0x3F, 0x66, 0xEF},
      {0xAC, 0x2A, 0x1A},
      {0x20, 0xEF, 0x76},
      {0xEB, 0x80, 0x80}}},
    {YUVColorSpace::BT709,
     ColorRange::FULL,
     {{0x36, 0x63, 0xFF},
      {0xB6, 0x1D, 0x0C},
      {0x13, 0xFF, 0x74},
      {0xFF, 0x80, 0x80}}},
    {YUVColorSpace::BT2020,
     ColorRange::LIMITED,
     {{0x4B, 0x61, 0xEF},
      {0xA3, 0x2F, 0x19},
      {0x1D, 0xEF, 0x77},
      {0xEB, 0x80, 0x80}}},
    {YUVColorSpace::BT2020,
     ColorRange::FULL,
     {{0x43, 0x5C, 0xFF},
      {0xAD, 0x24, 0x0A},
      {0x0F, 0xFF, 0x76},
      {0xFF, 0x80, 0x80}}},
};

}  // namespace

// Converts aImage to an I420 image of aDestSize with the given matrix and
// checks that every sample matches aExpected.
static void CheckI420Conversion(Image* aImage, const IntSize& aDestSize,
                                YUVColorSpace aColorSpace,
                                ColorRange aColorRange,
                                const YCbCrValue& aExpected) {
  const IntSize chroma =
      ChromaSize(aDestSize, ChromaSubsampling::HALF_WIDTH_AND_HEIGHT);
  nsTArray<uint8_t> y;
  nsTArray<uint8_t> u;
  nsTArray<uint8_t> v;
  y.SetLength(size_t(aDestSize.width) * aDestSize.height);
  u.SetLength(size_t(chroma.width) * chroma.height);
  v.SetLength(u.Length());

  ASSERT_EQ(ConvertToI420(aImage, y.Elements(), aDestSize.width, u.Elements(),
                          chroma.width, v.Elements(), chroma.width, aDestSize,
                          aColorSpace, aColorRange),
            NS_OK);
  for (uint8_t sample : y) {
    EXPECT_EQ(sample, aExpected.mY);
  }
  for (uint8_t sample : u) {
    EXPECT_EQ(sample, aExpected.mCb);
  }
  for (uint8_t sample : v) {
    EXPECT_EQ(sample, aExpected.mCr);
  }
}

static void CheckNV12Conversion(Image* aImage, const IntSize& aDestSize,
                                YUVColorSpace aColorSpace,
                                ColorRange aColorRange,
                                const YCbCrValue& aExpected) {
  const IntSize chroma =
      ChromaSize(aDestSize, ChromaSubsampling::HALF_WIDTH_AND_HEIGHT);
  nsTArray<uint8_t> y;
  nsTArray<uint8_t> uv;
  y.SetLength(size_t(aDestSize.width) * aDestSize.height);
  uv.SetLength(size_t(2 * chroma.width) * chroma.height);

  ASSERT_EQ(
      ConvertToNV12(aImage, y.Elements(), aDestSize.width, uv.Elements(),
                    2 * chroma.width, aDestSize, aColorSpace, aColorRange),
      NS_OK);
  for (uint8_t sample : y) {
    EXPECT_EQ(sample, aExpected.mY);
  }
  for (size_t i = 0; i < uv.Length(); ++i) {
    EXPECT_EQ(uv[i], (i % 2 == 0) ? aExpected.mCb : aExpected.mCr);
  }
}

// The RGB paths must convert with the requested matrix whether the image is
// converted as is, scaled down as RGB first, or converted first and then
// scaled up as I420.
TEST(MediaImageConversion, ConvertToI420SelectsRGBToYUVMatrix)
{
  const IntSize twoByTwo(2, 2);
  const IntSize fourByFour(4, 4);

  for (const RGBToYUVExpectation& e : kRGBToYUVExpectations) {
    for (size_t i = 0; i < std::size(kRGBSamples); ++i) {
      for (SurfaceFormat format :
           {SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8X8,
            SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8X8}) {
        SCOPED_TRACE(::testing::Message()
                     << kRGBSamples[i].mName << " " << e.mColorSpace << " "
                     << e.mColorRange << " " << format);
        RefPtr<SourceSurfaceImage> smallImage =
            CreateSolidSurfaceImage(twoByTwo, format, kRGBSamples[i].mRGB);
        RefPtr<SourceSurfaceImage> largeImage =
            CreateSolidSurfaceImage(fourByFour, format, kRGBSamples[i].mRGB);
        ASSERT_NE(smallImage, nullptr);
        ASSERT_NE(largeImage, nullptr);

        CheckI420Conversion(smallImage, twoByTwo, e.mColorSpace, e.mColorRange,
                            e.mYUV[i]);
        CheckI420Conversion(largeImage, twoByTwo, e.mColorSpace, e.mColorRange,
                            e.mYUV[i]);
        CheckI420Conversion(smallImage, fourByFour, e.mColorSpace,
                            e.mColorRange, e.mYUV[i]);
      }
    }
  }
}

TEST(MediaImageConversion, ConvertToNV12SelectsRGBToYUVMatrix)
{
  const IntSize twoByTwo(2, 2);
  const IntSize fourByFour(4, 4);

  for (const RGBToYUVExpectation& e : kRGBToYUVExpectations) {
    for (size_t i = 0; i < std::size(kRGBSamples); ++i) {
      for (SurfaceFormat format :
           {SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8X8,
            SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8X8}) {
        SCOPED_TRACE(::testing::Message()
                     << kRGBSamples[i].mName << " " << e.mColorSpace << " "
                     << e.mColorRange << " " << format);
        RefPtr<SourceSurfaceImage> smallImage =
            CreateSolidSurfaceImage(twoByTwo, format, kRGBSamples[i].mRGB);
        RefPtr<SourceSurfaceImage> largeImage =
            CreateSolidSurfaceImage(fourByFour, format, kRGBSamples[i].mRGB);
        ASSERT_NE(smallImage, nullptr);
        ASSERT_NE(largeImage, nullptr);

        CheckNV12Conversion(smallImage, twoByTwo, e.mColorSpace, e.mColorRange,
                            e.mYUV[i]);
        CheckNV12Conversion(largeImage, twoByTwo, e.mColorSpace, e.mColorRange,
                            e.mYUV[i]);
        CheckNV12Conversion(smallImage, fourByFour, e.mColorSpace,
                            e.mColorRange, e.mYUV[i]);
      }
    }
  }
}

// Runs aCheck on a red 2x2 image in every source layout the conversion reads,
// telling it whether the image is RGB565.
template <typename CheckImage>
static void ForEachRedSourceImage(CheckImage&& aCheck) {
  for (SurfaceFormat format : {SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8X8,
                               SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8X8,
                               SurfaceFormat::R5G6B5_UINT16}) {
    SCOPED_TRACE(::testing::Message() << format);
    RefPtr<SourceSurfaceImage> image =
        CreateSolidSurfaceImage(IntSize(2, 2), format, kRGBRed);
    ASSERT_NE(image, nullptr);
    aCheck(image.get(), format == SurfaceFormat::R5G6B5_UINT16);
  }
  for (ImageBitmapFormat format :
       {ImageBitmapFormat::YUV420P, ImageBitmapFormat::YUV422P,
        ImageBitmapFormat::YUV444P, ImageBitmapFormat::YUV420SP_NV12,
        ImageBitmapFormat::YUV420SP_NV21}) {
    SCOPED_TRACE(::testing::Message() << static_cast<int>(format));
    auto image =
        MakeRefPtr<TestPlanarYCbCrImage>(IntSize(2, 2), kYCbCrRed, format);
    aCheck(image.get(), false);
  }
}

// Every source converts to I420, whether the image is converted as is, scaled
// down, including to an odd size, or scaled up.
TEST(MediaImageConversion, ConvertToI420FromEverySource)
{
  const IntSize sizes[] = {IntSize(2, 2), IntSize(1, 1), IntSize(4, 4)};
  ForEachRedSourceImage([&](Image* aImage, bool) {
    for (const IntSize& size : sizes) {
      SCOPED_TRACE(::testing::Message() << size.width << "x" << size.height);
      CheckI420Conversion(aImage, size, YUVColorSpace::BT601,
                          ColorRange::LIMITED, kYCbCrRed);
    }
  });
}

// Every source but RGB565 converts to NV12 the same way; libyuv converts
// RGB565 to I420 only.
TEST(MediaImageConversion, ConvertToNV12FromEverySource)
{
  const IntSize sizes[] = {IntSize(2, 2), IntSize(1, 1), IntSize(4, 4)};
  ForEachRedSourceImage([&](Image* aImage, bool aIsRGB565) {
    if (aIsRGB565) {
      uint8_t y[4] = {};
      uint8_t uv[2] = {};
      EXPECT_EQ(ConvertToNV12(aImage, y, 2, uv, 2, IntSize(2, 2)),
                NS_ERROR_NOT_IMPLEMENTED);
      return;
    }
    for (const IntSize& size : sizes) {
      SCOPED_TRACE(::testing::Message() << size.width << "x" << size.height);
      CheckNV12Conversion(aImage, size, YUVColorSpace::BT601,
                          ColorRange::LIMITED, kYCbCrRed);
    }
  });
}

// One output row must fit in its stride: the width for luma, ceil(width / 2)
// samples per chroma plane, and both chroma planes at once for NV12.
TEST(MediaImageConversion, DestinationStrideBounds)
{
  auto image = MakeRefPtr<TestPlanarYCbCrImage>(IntSize(4, 4), kYCbCrRed);
  const IntSize even(4, 4);
  const IntSize odd(3, 3);
  uint8_t y[16] = {};
  uint8_t u[8] = {};
  uint8_t v[8] = {};
  uint8_t uv[16] = {};

  // Strides that hold exactly one row are accepted, for an odd width too.
  EXPECT_EQ(ConvertToI420(image, y, 4, u, 2, v, 2, even), NS_OK);
  EXPECT_EQ(ConvertToNV12(image, y, 4, uv, 4, even), NS_OK);
  EXPECT_EQ(ConvertToI420(image, y, 3, u, 2, v, 2, odd), NS_OK);
  EXPECT_EQ(ConvertToNV12(image, y, 3, uv, 4, odd), NS_OK);

  // A luma stride below the width is rejected.
  EXPECT_EQ(ConvertToI420(image, y, 3, u, 2, v, 2, even), NS_ERROR_INVALID_ARG);
  EXPECT_EQ(ConvertToNV12(image, y, 3, uv, 4, even), NS_ERROR_INVALID_ARG);

  // A chroma stride below ceil(width / 2) samples is rejected for either plane.
  EXPECT_EQ(ConvertToI420(image, y, 3, u, 1, v, 2, odd), NS_ERROR_INVALID_ARG);
  EXPECT_EQ(ConvertToI420(image, y, 3, u, 2, v, 1, odd), NS_ERROR_INVALID_ARG);
  EXPECT_EQ(ConvertToNV12(image, y, 3, uv, 3, odd), NS_ERROR_INVALID_ARG);
}

// The fourth byte, alpha or padding, must not affect the conversion: the
// expected samples were computed for opaque colors.
TEST(MediaImageConversion, RGBToYUVIgnoresFourthByte)
{
  const IntSize size(2, 2);

  for (const RGBToYUVExpectation& e : kRGBToYUVExpectations) {
    for (size_t i = 0; i < std::size(kRGBSamples); ++i) {
      RGBValue transparent = kRGBSamples[i].mRGB;
      transparent.mA = 0x00;
      for (SurfaceFormat format :
           {SurfaceFormat::B8G8R8A8, SurfaceFormat::B8G8R8X8,
            SurfaceFormat::R8G8B8A8, SurfaceFormat::R8G8B8X8}) {
        SCOPED_TRACE(::testing::Message()
                     << kRGBSamples[i].mName << " " << e.mColorSpace << " "
                     << e.mColorRange << " " << format);
        RefPtr<SourceSurfaceImage> image =
            CreateSolidSurfaceImage(size, format, transparent);
        ASSERT_NE(image, nullptr);

        CheckI420Conversion(image, size, e.mColorSpace, e.mColorRange,
                            e.mYUV[i]);
        CheckNV12Conversion(image, size, e.mColorSpace, e.mColorRange,
                            e.mYUV[i]);
      }
    }
  }
}

// A YUV source is repacked as it is; the matrix only applies to RGB sources.
TEST(MediaImageConversion, YUVSourceIgnoresRGBToYUVMatrix)
{
  const IntSize size(2, 2);
  auto image = MakeRefPtr<TestPlanarYCbCrImage>(size, kYCbCrRed);

  CheckI420Conversion(image, size, YUVColorSpace::BT709, ColorRange::FULL,
                      kYCbCrRed);
  CheckNV12Conversion(image, size, YUVColorSpace::BT709, ColorRange::FULL,
                      kYCbCrRed);
}

TEST(MediaImageConversion, UnsupportedRGBToYUVMatrix)
{
  const IntSize size(2, 2);
  uint8_t y[4] = {};
  uint8_t u[1] = {};
  uint8_t v[1] = {};
  uint8_t uv[2] = {};

  // Identity is not an RGB-to-YUV matrix.
  RefPtr<SourceSurfaceImage> bgra =
      CreateSolidSurfaceImage(size, SurfaceFormat::B8G8R8A8, kRGBRed);
  ASSERT_NE(bgra, nullptr);
  EXPECT_EQ(ConvertToI420(bgra, y, 2, u, 1, v, 1, size, YUVColorSpace::Identity,
                          ColorRange::LIMITED),
            NS_ERROR_NOT_IMPLEMENTED);
  EXPECT_EQ(ConvertToNV12(bgra, y, 2, uv, 2, size, YUVColorSpace::Identity,
                          ColorRange::LIMITED),
            NS_ERROR_NOT_IMPLEMENTED);

  // libyuv converts RGB565 with BT.601 limited range only, and to I420 only.
  RefPtr<SourceSurfaceImage> rgb565 =
      CreateSolidSurfaceImage(size, SurfaceFormat::R5G6B5_UINT16, kRGBRed);
  ASSERT_NE(rgb565, nullptr);
  EXPECT_EQ(ConvertToI420(rgb565, y, 2, u, 1, v, 1, size, YUVColorSpace::BT709,
                          ColorRange::LIMITED),
            NS_ERROR_NOT_IMPLEMENTED);
  EXPECT_EQ(ConvertToI420(rgb565, y, 2, u, 1, v, 1, size, YUVColorSpace::BT601,
                          ColorRange::FULL),
            NS_ERROR_NOT_IMPLEMENTED);
  EXPECT_EQ(ConvertToI420(rgb565, y, 2, u, 1, v, 1, size, YUVColorSpace::BT601,
                          ColorRange::LIMITED),
            NS_OK);
  EXPECT_EQ(ConvertToNV12(rgb565, y, 2, uv, 2, size, YUVColorSpace::BT709,
                          ColorRange::LIMITED),
            NS_ERROR_NOT_IMPLEMENTED);
  EXPECT_EQ(ConvertToNV12(rgb565, y, 2, uv, 2, size, YUVColorSpace::BT601,
                          ColorRange::FULL),
            NS_ERROR_NOT_IMPLEMENTED);
  EXPECT_EQ(ConvertToNV12(rgb565, y, 2, uv, 2, size, YUVColorSpace::BT601,
                          ColorRange::LIMITED),
            NS_ERROR_NOT_IMPLEMENTED);
}

// Downscaling an NV12 source must average each chroma quadrant on its own: the
// interleaved chroma rows of the scaled intermediate are twice as wide as the
// planar ones.
TEST(MediaImageConversion, DownscaleNV12SourceKeepsChromaRows)
{
  // Mid-gray luma with a distinct chroma pair per quadrant: red, green, blue
  // and magenta at the corners of the chroma plane.
  const YCbCrValue quadrants[] = {
      {0x80, 0x40, 0xC0},  // red
      {0x80, 0x40, 0x40},  // green
      {0x80, 0xC0, 0x40},  // blue
      {0x80, 0xC0, 0xC0},  // magenta
  };
  auto image = MakeRefPtr<TestPlanarYCbCrImage>(
      IntSize(8, 8), quadrants[0], ImageBitmapFormat::YUV420SP_NV12);
  // Repaint each 4x4 quadrant, left to right then top to bottom, with its own
  // color, so the 2x2 chroma output holds one sample per quadrant.
  for (size_t i = 0; i < std::size(quadrants); ++i) {
    image->Fill(IntRect(int32_t(i % 2) * 4, int32_t(i / 2) * 4, 4, 4),
                quadrants[i]);
  }

  const IntSize dest(4, 4);
  uint8_t y[16] = {};
  uint8_t u[4] = {};
  uint8_t v[4] = {};
  uint8_t uv[8] = {};

  ASSERT_EQ(ConvertToI420(image, y, 4, u, 2, v, 2, dest), NS_OK);
  for (size_t i = 0; i < std::size(quadrants); ++i) {
    EXPECT_EQ(u[i], quadrants[i].mCb);
    EXPECT_EQ(v[i], quadrants[i].mCr);
  }

  ASSERT_EQ(ConvertToNV12(image, y, 4, uv, 4, dest), NS_OK);
  for (size_t i = 0; i < std::size(quadrants); ++i) {
    EXPECT_EQ(uv[2 * i], quadrants[i].mCb);
    EXPECT_EQ(uv[2 * i + 1], quadrants[i].mCr);
  }
}
