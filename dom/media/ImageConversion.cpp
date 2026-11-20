/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageConversion.h"

#include "ImageContainer.h"
#include "ImagePixelFormat.h"
#include "YCbCrUtils.h"
#include "libyuv/convert.h"
#include "libyuv/convert_from_argb.h"
#include "libyuv/scale_argb.h"
#include "mozilla/PodOperations.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ToString.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/Swizzle.h"
#include "nsPrintfCString.h"
#include "nsThreadUtils.h"
#include "skia/include/core/SkBitmap.h"
#include "skia/include/core/SkColorSpace.h"
#include "skia/include/core/SkImage.h"
#include "skia/include/core/SkImageInfo.h"

using mozilla::CheckedInt;
using mozilla::ImageFormat;
using mozilla::Maybe;
using mozilla::Nothing;
using mozilla::Some;
using mozilla::gfx::ColorRange;
using mozilla::gfx::DataSourceSurface;
using mozilla::gfx::IntSize;
using mozilla::gfx::SourceSurface;
using mozilla::gfx::SurfaceFormat;
using mozilla::gfx::YUVColorSpace;
using mozilla::layers::Image;
using mozilla::layers::PlanarYCbCrData;
using mozilla::layers::PlanarYCbCrImage;

static const PlanarYCbCrData* GetPlanarYCbCrData(Image* aImage) {
  switch (aImage->GetFormat()) {
    case ImageFormat::PLANAR_YCBCR:
      return aImage->AsPlanarYCbCrImage()->GetData();
    case ImageFormat::NV_IMAGE:
      return aImage->AsNVImage()->GetData();
    default:
      return nullptr;
  }
}

static nsresult MapRv(int aRv) {
  // Docs for libyuv::ConvertToI420 say:
  // Returns 0 for successful; -1 for invalid parameter. Non-zero for failure.
  switch (aRv) {
    case 0:
      return NS_OK;
    case -1:
      return NS_ERROR_INVALID_ARG;
    default:
      return NS_ERROR_FAILURE;
  }
}

// How many luma samples one chroma sample spans on each axis.
static IntSize ChromaOriginDivisor(
    mozilla::gfx::ChromaSubsampling aSubsampling) {
  switch (aSubsampling) {
    case mozilla::gfx::ChromaSubsampling::FULL:
      return IntSize(1, 1);
    case mozilla::gfx::ChromaSubsampling::HALF_WIDTH:
      return IntSize(2, 1);
    case mozilla::gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT:
      return IntSize(2, 2);
  }
  MOZ_CRASH("bad ChromaSubsampling");
}

// The plane pointers advanced to mPictureRect's origin.
struct PictureRegion {
  uint8_t* mY;
  uint8_t* mCb;
  uint8_t* mCr;
};

// The plane pointers address the coded buffer, so reaching the picture region
// means adding mPictureRect's origin. Nothing() if that cannot be done: an
// origin off a chroma sample is not expressible as a plane offset -- the
// sibling YUV->RGB path sidesteps this by passing pic_x/pic_y to the conversion
// routine instead -- and neither is an offset that overflows.
static Maybe<PictureRegion> PictureRegionOf(const PlanarYCbCrData& aData) {
  const IntSize divisor = ChromaOriginDivisor(aData.mChromaSubsampling);
  if (aData.mPictureRect.x % divisor.width != 0 ||
      aData.mPictureRect.y % divisor.height != 0) {
    return Nothing();
  }

  // Floor division, unlike ChromaSize's ceiling: a luma column belongs to the
  // chroma sample covering it.
  const int32_t chromaX = aData.mPictureRect.x / divisor.width;
  const int32_t chromaY = aData.mPictureRect.y / divisor.height;

  const CheckedInt<int32_t> yOffset =
      CheckedInt<int32_t>(aData.mPictureRect.y) * aData.mYStride +
      CheckedInt<int32_t>(aData.mPictureRect.x) * (aData.mYSkip + 1);
  const CheckedInt<int32_t> chromaRow =
      CheckedInt<int32_t>(chromaY) * aData.mCbCrStride;
  const CheckedInt<int32_t> cbOffset =
      chromaRow + CheckedInt<int32_t>(chromaX) * (aData.mCbSkip + 1);
  const CheckedInt<int32_t> crOffset =
      chromaRow + CheckedInt<int32_t>(chromaX) * (aData.mCrSkip + 1);
  if (!yOffset.isValid() || !cbOffset.isValid() || !crOffset.isValid()) {
    return Nothing();
  }

  return Some(PictureRegion{aData.mYChannel + yOffset.value(),
                            aData.mCbChannel + cbOffset.value(),
                            aData.mCrChannel + crOffset.value()});
}

static bool DataSurfaceCoversSize(DataSourceSurface* aSurface,
                                  const IntSize& aSize) {
  const IntSize surfaceSize = aSurface->GetSize();
  return surfaceSize.width >= aSize.width && surfaceSize.height >= aSize.height;
}

// libyuv's RGB-to-YUV matrices for one YUV color space and range. libyuv names
// them after the pixel read as a little-endian word, so kArgb* is for BGRA in
// memory and kAbgr* for RGBA in memory.
struct RGBToYUVMatrices {
  YUVColorSpace mColorSpace;
  ColorRange mColorRange;
  const libyuv::ArgbConstants* mBGRA;
  const libyuv::ArgbConstants* mRGBA;
};

static constexpr RGBToYUVMatrices kRGBToYUVMatrices[] = {
    {YUVColorSpace::BT601, ColorRange::LIMITED, &libyuv::kArgbI601Constants,
     &libyuv::kAbgrI601Constants},
    {YUVColorSpace::BT601, ColorRange::FULL, &libyuv::kArgbJPEGConstants,
     &libyuv::kAbgrJPEGConstants},
    {YUVColorSpace::BT709, ColorRange::LIMITED, &libyuv::kArgbH709Constants,
     &libyuv::kAbgrH709Constants},
    {YUVColorSpace::BT709, ColorRange::FULL, &libyuv::kArgbF709Constants,
     &libyuv::kAbgrF709Constants},
    {YUVColorSpace::BT2020, ColorRange::LIMITED, &libyuv::kArgbU2020Constants,
     &libyuv::kAbgrU2020Constants},
    {YUVColorSpace::BT2020, ColorRange::FULL, &libyuv::kArgbV2020Constants,
     &libyuv::kAbgrV2020Constants},
};

// The libyuv matrix converting a 32-bit RGB surface of aFormat to YUV in
// aYUVColorSpace and aColorRange, or nullptr if libyuv has none.
static const libyuv::ArgbConstants* RGBToYUVMatrix(SurfaceFormat aFormat,
                                                   YUVColorSpace aYUVColorSpace,
                                                   ColorRange aColorRange) {
  for (const RGBToYUVMatrices& matrices : kRGBToYUVMatrices) {
    if (matrices.mColorSpace != aYUVColorSpace ||
        matrices.mColorRange != aColorRange) {
      continue;
    }
    switch (aFormat) {
      case SurfaceFormat::B8G8R8A8:
      case SurfaceFormat::B8G8R8X8:
        return matrices.mBGRA;
      case SurfaceFormat::R8G8B8A8:
      case SurfaceFormat::R8G8B8X8:
        return matrices.mRGBA;
      default:
        return nullptr;
    }
  }
  return nullptr;
}

namespace mozilla {

already_AddRefed<SourceSurface> GetSourceSurface(Image* aImage) {
  if (!aImage->AsGLImage() || NS_IsMainThread()) {
    return aImage->GetAsSourceSurface();
  }

  // GLImage::GetAsSourceSurface() only supports main thread
  RefPtr<SourceSurface> surf;
  NS_DispatchAndSpinEventLoopUntilComplete(
      "ImageToI420::GLImage::GetSourceSurface"_ns,
      mozilla::GetMainThreadSerialEventTarget(),
      NS_NewRunnableFunction(
          "ImageToI420::GLImage::GetSourceSurface",
          [&aImage, &surf]() { surf = aImage->GetAsSourceSurface(); }));

  return surf.forget();
}

// Where a 4:2:0 conversion writes: the three planes of I420 or, for NV12, the
// luma plane and the interleaved chroma plane as mU.
struct YUV420Planes {
  uint8_t* mY = nullptr;
  int mYStride = 0;
  uint8_t* mU = nullptr;
  int mUStride = 0;
  uint8_t* mV = nullptr;
  int mVStride = 0;
  enum class PixelFormat { I420, NV12 } mFormat = PixelFormat::I420;
};

// The conversions below write whichever layout aDest has. libyuv converts
// RGB565 to I420 only, so that source is refused for NV12 beforehand.
static nsresult I420ToPlanes(const uint8_t* aY, int aYStride, const uint8_t* aU,
                             int aUStride, const uint8_t* aV, int aVStride,
                             const YUV420Planes& aDest, const IntSize& aSize) {
  switch (aDest.mFormat) {
    case YUV420Planes::PixelFormat::NV12:
      return MapRv(libyuv::I420ToNV12(
          aY, aYStride, aU, aUStride, aV, aVStride, aDest.mY, aDest.mYStride,
          aDest.mU, aDest.mUStride, aSize.width, aSize.height));
    case YUV420Planes::PixelFormat::I420:
      return MapRv(libyuv::I420Copy(aY, aYStride, aU, aUStride, aV, aVStride,
                                    aDest.mY, aDest.mYStride, aDest.mU,
                                    aDest.mUStride, aDest.mV, aDest.mVStride,
                                    aSize.width, aSize.height));
  }
  MOZ_ASSERT_UNREACHABLE("unexpected format");
  return NS_ERROR_NOT_IMPLEMENTED;
}

static nsresult I422ToPlanes(const uint8_t* aY, int aYStride, const uint8_t* aU,
                             int aUStride, const uint8_t* aV, int aVStride,
                             const YUV420Planes& aDest, const IntSize& aSize) {
  switch (aDest.mFormat) {
    case YUV420Planes::PixelFormat::NV12:
      // libyuv only has the NV21 variant; swapping the chroma planes yields
      // NV12.
      return MapRv(libyuv::I422ToNV21(
          aY, aYStride, aV, aVStride, aU, aUStride, aDest.mY, aDest.mYStride,
          aDest.mU, aDest.mUStride, aSize.width, aSize.height));
    case YUV420Planes::PixelFormat::I420:
      return MapRv(libyuv::I422ToI420(aY, aYStride, aU, aUStride, aV, aVStride,
                                      aDest.mY, aDest.mYStride, aDest.mU,
                                      aDest.mUStride, aDest.mV, aDest.mVStride,
                                      aSize.width, aSize.height));
  }
  MOZ_ASSERT_UNREACHABLE("unexpected format");
  return NS_ERROR_NOT_IMPLEMENTED;
}

static nsresult I444ToPlanes(const uint8_t* aY, int aYStride, const uint8_t* aU,
                             int aUStride, const uint8_t* aV, int aVStride,
                             const YUV420Planes& aDest, const IntSize& aSize) {
  switch (aDest.mFormat) {
    case YUV420Planes::PixelFormat::NV12:
      return MapRv(libyuv::I444ToNV12(
          aY, aYStride, aU, aUStride, aV, aVStride, aDest.mY, aDest.mYStride,
          aDest.mU, aDest.mUStride, aSize.width, aSize.height));
    case YUV420Planes::PixelFormat::I420:
      return MapRv(libyuv::I444ToI420(aY, aYStride, aU, aUStride, aV, aVStride,
                                      aDest.mY, aDest.mYStride, aDest.mU,
                                      aDest.mUStride, aDest.mV, aDest.mVStride,
                                      aSize.width, aSize.height));
  }
  MOZ_ASSERT_UNREACHABLE("unexpected format");
  return NS_ERROR_NOT_IMPLEMENTED;
}

static nsresult NV12ToPlanes(const uint8_t* aY, int aYStride,
                             const uint8_t* aUV, int aUVStride,
                             const YUV420Planes& aDest, const IntSize& aSize) {
  switch (aDest.mFormat) {
    case YUV420Planes::PixelFormat::NV12:
      return MapRv(libyuv::NV12Copy(aY, aYStride, aUV, aUVStride, aDest.mY,
                                    aDest.mYStride, aDest.mU, aDest.mUStride,
                                    aSize.width, aSize.height));
    case YUV420Planes::PixelFormat::I420:
      return MapRv(libyuv::NV12ToI420(
          aY, aYStride, aUV, aUVStride, aDest.mY, aDest.mYStride, aDest.mU,
          aDest.mUStride, aDest.mV, aDest.mVStride, aSize.width, aSize.height));
  }
  MOZ_ASSERT_UNREACHABLE("unexpected format");
  return NS_ERROR_NOT_IMPLEMENTED;
}

static nsresult NV21ToPlanes(const uint8_t* aY, int aYStride,
                             const uint8_t* aVU, int aVUStride,
                             const YUV420Planes& aDest, const IntSize& aSize) {
  switch (aDest.mFormat) {
    case YUV420Planes::PixelFormat::NV12:
      return MapRv(libyuv::NV21ToNV12(aY, aYStride, aVU, aVUStride, aDest.mY,
                                      aDest.mYStride, aDest.mU, aDest.mUStride,
                                      aSize.width, aSize.height));
    case YUV420Planes::PixelFormat::I420:
      return MapRv(libyuv::NV21ToI420(
          aY, aYStride, aVU, aVUStride, aDest.mY, aDest.mYStride, aDest.mU,
          aDest.mUStride, aDest.mV, aDest.mVStride, aSize.width, aSize.height));
  }
  MOZ_ASSERT_UNREACHABLE("unexpected format");
  return NS_ERROR_NOT_IMPLEMENTED;
}

static nsresult RGB32ToPlanes(const uint8_t* aRGB, int aStride,
                              const libyuv::ArgbConstants* aMatrix,
                              const YUV420Planes& aDest, const IntSize& aSize) {
  switch (aDest.mFormat) {
    case YUV420Planes::PixelFormat::NV12:
      return MapRv(libyuv::ARGBToNV12Matrix(
          aRGB, aStride, aDest.mY, aDest.mYStride, aDest.mU, aDest.mUStride,
          aMatrix, aSize.width, aSize.height));
    case YUV420Planes::PixelFormat::I420:
      return MapRv(libyuv::ARGBToI420Matrix(
          aRGB, aStride, aDest.mY, aDest.mYStride, aDest.mU, aDest.mUStride,
          aDest.mV, aDest.mVStride, aMatrix, aSize.width, aSize.height));
  }
  MOZ_ASSERT_UNREACHABLE("unexpected format");
  return NS_ERROR_NOT_IMPLEMENTED;
}

// Scales between two buffers of aDest's layout.
static nsresult ScalePlanes(const YUV420Planes& aSrc, const IntSize& aSrcSize,
                            const YUV420Planes& aDest,
                            const IntSize& aDestSize) {
  switch (aDest.mFormat) {
    case YUV420Planes::PixelFormat::NV12:
      return MapRv(libyuv::NV12Scale(
          aSrc.mY, aSrc.mYStride, aSrc.mU, aSrc.mUStride, aSrcSize.width,
          aSrcSize.height, aDest.mY, aDest.mYStride, aDest.mU, aDest.mUStride,
          aDestSize.width, aDestSize.height, libyuv::FilterMode::kFilterBox));
    case YUV420Planes::PixelFormat::I420:
      return MapRv(libyuv::I420Scale(
          aSrc.mY, aSrc.mYStride, aSrc.mU, aSrc.mUStride, aSrc.mV,
          aSrc.mVStride, aSrcSize.width, aSrcSize.height, aDest.mY,
          aDest.mYStride, aDest.mU, aDest.mUStride, aDest.mV, aDest.mVStride,
          aDestSize.width, aDestSize.height, libyuv::FilterMode::kFilterBox));
  }
  MOZ_ASSERT_UNREACHABLE("unexpected format");
  return NS_ERROR_NOT_IMPLEMENTED;
}

// Converts aImage to aDestSize and writes it in aDest's layout.
static nsresult ConvertToYUV420(Image* aImage, const YUV420Planes& aDest,
                                const IntSize& aDestSize,
                                YUVColorSpace aDestYUVColorSpace,
                                ColorRange aDestColorRange) {
  if (!aImage->IsValid()) {
    return NS_ERROR_INVALID_ARG;
  }

  const IntSize imageSize = aImage->GetSize();
  if (!IsImageDimensionSupportedForConversion(imageSize)) {
    return NS_ERROR_INVALID_ARG;
  }

  // One output row must fit in its stride: aDestSize.width luma bytes, and
  // ceil(width / 2) chroma samples per plane, both planes at once for NV12.
  const CheckedInt<int32_t> chromaWidth =
      (CheckedInt<int32_t>(aDestSize.width) + 1) / 2;
  const CheckedInt<int32_t> chromaRowBytes =
      aDest.mFormat == YUV420Planes::PixelFormat::NV12 ? chromaWidth * 2
                                                       : chromaWidth;
  if (!chromaRowBytes.isValid() || aDest.mYStride < aDestSize.width ||
      aDest.mUStride < chromaRowBytes.value() ||
      (aDest.mFormat == YUV420Planes::PixelFormat::I420 &&
       aDest.mVStride < chromaRowBytes.value())) {
    NS_WARNING("ConvertToYUV420: destination strides too small");
    return NS_ERROR_INVALID_ARG;
  }

  auto srcPixelCount = CheckedInt<int32_t>(imageSize.width) * imageSize.height;
  auto dstPixelCount = CheckedInt<int32_t>(aDestSize.width) * aDestSize.height;
  if (!srcPixelCount.isValid() || !dstPixelCount.isValid()) {
    MOZ_ASSERT_UNREACHABLE("Bad input or output sizes");
    return NS_ERROR_INVALID_ARG;
  }

  // If we are downscaling, we prefer an early scale. If we are upscaling, we
  // prefer a late scale. This minimizes the number of pixel manipulations.
  // Depending on the input format, we may be forced to do a late scale after
  // conversion, because we don't support scaling the input format.
  const bool needsScale = imageSize != aDestSize;
  bool earlyScale = srcPixelCount.value() > dstPixelCount.value();

  Maybe<DataSourceSurface::ScopedMap> surfaceMap;
  SurfaceFormat surfaceFormat = SurfaceFormat::UNKNOWN;
  const libyuv::ArgbConstants* rgbToYuvMatrix = nullptr;

  const PlanarYCbCrData* data = GetPlanarYCbCrData(aImage);

  // Declared here because the later data paths below read them too.
  uint8_t* srcY = nullptr;
  uint8_t* srcCb = nullptr;
  uint8_t* srcCr = nullptr;

  Maybe<ImagePixelFormat> format;
  if (data) {
    format = ImageToPixelFormat(aImage);
    if (format.isNothing()) {
      NS_WARNING("ConvertToYUV420: unnamed YUV layout");
      return NS_ERROR_NOT_IMPLEMENTED;
    }

    Maybe<PictureRegion> picture = PictureRegionOf(*data);
    if (picture.isNothing()) {
      NS_WARNING("ConvertToYUV420: cannot address the picture region");
      return NS_ERROR_INVALID_ARG;
    }
    srcY = picture->mY;
    srcCb = picture->mCb;
    srcCr = picture->mCr;

    switch (format.value()) {
      case ImagePixelFormat::I420:
      case ImagePixelFormat::I420A:
        if (!needsScale) {
          return I420ToPlanes(srcY, data->mYStride, srcCb, data->mCbCrStride,
                              srcCr, data->mCbCrStride, aDest, aDestSize);
        }
        // Planar to planar scales directly into the output buffer.
        if (aDest.mFormat == YUV420Planes::PixelFormat::I420) {
          const YUV420Planes source{srcY,  data->mYStride,
                                    srcCb, data->mCbCrStride,
                                    srcCr, data->mCbCrStride};
          return ScalePlanes(source, imageSize, aDest, aDestSize);
        }
        break;
      case ImagePixelFormat::I422:
      case ImagePixelFormat::I422A:
        if (!needsScale) {
          return I422ToPlanes(srcY, data->mYStride, srcCb, data->mCbCrStride,
                              srcCr, data->mCbCrStride, aDest, aDestSize);
        }
        break;
      case ImagePixelFormat::I444:
      case ImagePixelFormat::I444A:
        if (!needsScale) {
          return I444ToPlanes(srcY, data->mYStride, srcCb, data->mCbCrStride,
                              srcCr, data->mCbCrStride, aDest, aDestSize);
        }
        break;
      case ImagePixelFormat::NV12:
        if (!needsScale) {
          return NV12ToPlanes(srcY, data->mYStride, srcCb, data->mCbCrStride,
                              aDest, aDestSize);
        }
        break;
      case ImagePixelFormat::NV21:
        if (!needsScale) {
          return NV21ToPlanes(srcY, data->mYStride, srcCr, data->mCbCrStride,
                              aDest, aDestSize);
        }
        earlyScale = false;
        break;
      default:
        NS_WARNING("ConvertToYUV420: only 8-bit YUV sources are supported");
        return NS_ERROR_NOT_IMPLEMENTED;
    }
  } else {
    RefPtr<SourceSurface> surface = GetSourceSurface(aImage);
    if (!surface) {
      return NS_ERROR_FAILURE;
    }

    RefPtr<DataSourceSurface> dataSurface = surface->GetDataSurface();
    if (!dataSurface) {
      return NS_ERROR_FAILURE;
    }

    if (!DataSurfaceCoversSize(dataSurface, imageSize)) {
      NS_WARNING("ConvertToYUV420: Source surface smaller than image size");
      return NS_ERROR_INVALID_ARG;
    }

    surfaceMap.emplace(dataSurface, DataSourceSurface::READ);
    if (!surfaceMap->IsMapped()) {
      return NS_ERROR_FAILURE;
    }

    surfaceFormat = dataSurface->GetFormat();
    switch (surfaceFormat) {
      case SurfaceFormat::B8G8R8A8:
      case SurfaceFormat::B8G8R8X8:
      case SurfaceFormat::R8G8B8A8:
      case SurfaceFormat::R8G8B8X8:
        rgbToYuvMatrix =
            RGBToYUVMatrix(surfaceFormat, aDestYUVColorSpace, aDestColorRange);
        if (!rgbToYuvMatrix) {
          NS_WARNING(
              nsPrintfCString(
                  "ConvertToYUV420: no RGB-to-YUV matrix for %s %s from %s",
                  ToString(aDestYUVColorSpace).c_str(),
                  ToString(aDestColorRange).c_str(),
                  ToString(surfaceFormat).c_str())
                  .get());
          return NS_ERROR_NOT_IMPLEMENTED;
        }
        if (!needsScale) {
          return RGB32ToPlanes(surfaceMap->GetData(), surfaceMap->GetStride(),
                               rgbToYuvMatrix, aDest, aDestSize);
        }
        break;
      case SurfaceFormat::R5G6B5_UINT16:
        if (aDest.mFormat == YUV420Planes::PixelFormat::NV12) {
          NS_WARNING("ConvertToYUV420: no RGB565 to NV12 conversion");
          return NS_ERROR_NOT_IMPLEMENTED;
        }
        // libyuv converts RGB565 with BT.601 limited range only.
        if (aDestYUVColorSpace != YUVColorSpace::BT601 ||
            aDestColorRange != ColorRange::LIMITED) {
          NS_WARNING(
              nsPrintfCString(
                  "ConvertToYUV420: no RGB-to-YUV matrix for %s %s from %s",
                  ToString(aDestYUVColorSpace).c_str(),
                  ToString(aDestColorRange).c_str(),
                  ToString(surfaceFormat).c_str())
                  .get());
          return NS_ERROR_NOT_IMPLEMENTED;
        }
        if (!needsScale) {
          return MapRv(libyuv::RGB565ToI420(
              static_cast<uint8_t*>(surfaceMap->GetData()),
              surfaceMap->GetStride(), aDest.mY, aDest.mYStride, aDest.mU,
              aDest.mUStride, aDest.mV, aDest.mVStride, aDestSize.width,
              aDestSize.height));
        }
        earlyScale = false;
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("Surface format conversion not implemented");
        return NS_ERROR_NOT_IMPLEMENTED;
    }
  }

  MOZ_DIAGNOSTIC_ASSERT(needsScale);

  // We have to scale, and we are unable to scale directly, so we need a
  // temporary buffer to hold the scaled result in the input format, or the
  // unscaled result in the output layout.
  IntSize tempBufSize;
  IntSize tempBufCbCrSize;
  YUV420Planes::PixelFormat tempFormat = YUV420Planes::PixelFormat::I420;
  if (earlyScale) {
    // Early scaling means we are scaling from the input buffer to a temporary
    // buffer of the same format.
    tempBufSize = aDestSize;
    if (data) {
      tempBufCbCrSize = gfx::ChromaSize(tempBufSize, data->mChromaSubsampling);
      if (format.value() == ImagePixelFormat::NV12 ||
          format.value() == ImagePixelFormat::NV21) {
        tempFormat = YUV420Planes::PixelFormat::NV12;
      }
    }
  } else {
    // Late scaling means we are scaling from a temporary buffer in the output
    // layout to the destination.
    tempBufSize = imageSize;
    tempBufCbCrSize = gfx::ChromaSize(
        tempBufSize, gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT);
    tempFormat = aDest.mFormat;
  }

  MOZ_ASSERT(!tempBufSize.IsEmpty());

  // Make sure we can allocate the temporary buffer.
  gfx::AlignedArray<uint8_t> tempBuf;
  YUV420Planes tempPlanes;
  int32_t tempRgbStride = 0;
  if (!tempBufCbCrSize.IsEmpty()) {
    // Our temporary buffer is represented as a YUV format. Interleaved chroma
    // spans both chroma planes' worth of bytes.
    auto tempBufYLen =
        CheckedInt<size_t>(tempBufSize.width) * tempBufSize.height;
    auto tempBufCbCrLen =
        CheckedInt<size_t>(tempBufCbCrSize.width) * tempBufCbCrSize.height;
    auto tempBufLen = tempBufYLen + 2 * tempBufCbCrLen;
    if (!tempBufLen.isValid()) {
      MOZ_ASSERT_UNREACHABLE("Bad buffer size!");
      return NS_ERROR_FAILURE;
    }

    tempBuf.Realloc(tempBufLen.value());
    if (!tempBuf) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    tempPlanes.mY = tempBuf;
    tempPlanes.mYStride = tempBufSize.width;
    tempPlanes.mU = tempPlanes.mY + tempBufYLen.value();
    tempPlanes.mFormat = tempFormat;
    if (tempFormat == YUV420Planes::PixelFormat::NV12) {
      tempPlanes.mUStride = 2 * tempBufCbCrSize.width;
    } else {
      tempPlanes.mUStride = tempBufCbCrSize.width;
      tempPlanes.mV = tempPlanes.mU + tempBufCbCrLen.value();
      tempPlanes.mVStride = tempBufCbCrSize.width;
    }
  } else {
    // The temporary buffer is represented as a RGBA/BGRA format.
    auto tempStride = CheckedInt<int32_t>(tempBufSize.width) * 4;
    auto tempBufLen = tempStride * tempBufSize.height;
    if (!tempStride.isValid() || !tempBufLen.isValid()) {
      MOZ_ASSERT_UNREACHABLE("Bad buffer size!");
      return NS_ERROR_FAILURE;
    }

    tempBuf.Realloc(tempBufLen.value());
    if (!tempBuf) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    tempRgbStride = tempStride.value();
  }

  nsresult rv;
  if (!earlyScale) {
    // First convert whatever the input format is into the temp buffer.
    if (data) {
      switch (format.value()) {
        case ImagePixelFormat::I420:
        case ImagePixelFormat::I420A:
          rv = I420ToPlanes(srcY, data->mYStride, srcCb, data->mCbCrStride,
                            srcCr, data->mCbCrStride, tempPlanes, tempBufSize);
          break;
        case ImagePixelFormat::I422:
        case ImagePixelFormat::I422A:
          rv = I422ToPlanes(srcY, data->mYStride, srcCb, data->mCbCrStride,
                            srcCr, data->mCbCrStride, tempPlanes, tempBufSize);
          break;
        case ImagePixelFormat::I444:
        case ImagePixelFormat::I444A:
          rv = I444ToPlanes(srcY, data->mYStride, srcCb, data->mCbCrStride,
                            srcCr, data->mCbCrStride, tempPlanes, tempBufSize);
          break;
        case ImagePixelFormat::NV12:
          rv = NV12ToPlanes(srcY, data->mYStride, srcCb, data->mCbCrStride,
                            tempPlanes, tempBufSize);
          break;
        case ImagePixelFormat::NV21:
          rv = NV21ToPlanes(srcY, data->mYStride, srcCr, data->mCbCrStride,
                            tempPlanes, tempBufSize);
          break;
        default:
          MOZ_ASSERT_UNREACHABLE("YUV format conversion not implemented");
          return NS_ERROR_UNEXPECTED;
      }
    } else {
      switch (surfaceFormat) {
        case SurfaceFormat::B8G8R8A8:
        case SurfaceFormat::B8G8R8X8:
        case SurfaceFormat::R8G8B8A8:
        case SurfaceFormat::R8G8B8X8:
          rv = RGB32ToPlanes(surfaceMap->GetData(), surfaceMap->GetStride(),
                             rgbToYuvMatrix, tempPlanes, tempBufSize);
          break;
        case SurfaceFormat::R5G6B5_UINT16:
          rv = MapRv(libyuv::RGB565ToI420(
              static_cast<uint8_t*>(surfaceMap->GetData()),
              surfaceMap->GetStride(), tempPlanes.mY, tempPlanes.mYStride,
              tempPlanes.mU, tempPlanes.mUStride, tempPlanes.mV,
              tempPlanes.mVStride, tempBufSize.width, tempBufSize.height));
          break;
        default:
          MOZ_ASSERT_UNREACHABLE("Surface format conversion not implemented");
          return NS_ERROR_NOT_IMPLEMENTED;
      }
    }

    if (NS_FAILED(rv)) {
      return rv;
    }

    // Now do the scale in the output layout to the output buffer.
    return ScalePlanes(tempPlanes, tempBufSize, aDest, aDestSize);
  }

  if (data) {
    // First scale in the input format to the desired size into temp buffer, and
    // then convert that into the final result.
    switch (format.value()) {
      case ImagePixelFormat::I420:
      case ImagePixelFormat::I420A:
        rv = MapRv(libyuv::I420Scale(
            srcY, data->mYStride, srcCb, data->mCbCrStride, srcCr,
            data->mCbCrStride, imageSize.width, imageSize.height, tempPlanes.mY,
            tempPlanes.mYStride, tempPlanes.mU, tempPlanes.mUStride,
            tempPlanes.mV, tempPlanes.mVStride, tempBufSize.width,
            tempBufSize.height, libyuv::FilterMode::kFilterBox));
        if (NS_FAILED(rv)) {
          return rv;
        }
        return I420ToPlanes(tempPlanes.mY, tempPlanes.mYStride, tempPlanes.mU,
                            tempPlanes.mUStride, tempPlanes.mV,
                            tempPlanes.mVStride, aDest, aDestSize);
      case ImagePixelFormat::I422:
      case ImagePixelFormat::I422A:
        rv = MapRv(libyuv::I422Scale(
            srcY, data->mYStride, srcCb, data->mCbCrStride, srcCr,
            data->mCbCrStride, imageSize.width, imageSize.height, tempPlanes.mY,
            tempPlanes.mYStride, tempPlanes.mU, tempPlanes.mUStride,
            tempPlanes.mV, tempPlanes.mVStride, tempBufSize.width,
            tempBufSize.height, libyuv::FilterMode::kFilterBox));
        if (NS_FAILED(rv)) {
          return rv;
        }
        return I422ToPlanes(tempPlanes.mY, tempPlanes.mYStride, tempPlanes.mU,
                            tempPlanes.mUStride, tempPlanes.mV,
                            tempPlanes.mVStride, aDest, aDestSize);
      case ImagePixelFormat::I444:
      case ImagePixelFormat::I444A:
        rv = MapRv(libyuv::I444Scale(
            srcY, data->mYStride, srcCb, data->mCbCrStride, srcCr,
            data->mCbCrStride, imageSize.width, imageSize.height, tempPlanes.mY,
            tempPlanes.mYStride, tempPlanes.mU, tempPlanes.mUStride,
            tempPlanes.mV, tempPlanes.mVStride, tempBufSize.width,
            tempBufSize.height, libyuv::FilterMode::kFilterBox));
        if (NS_FAILED(rv)) {
          return rv;
        }
        return I444ToPlanes(tempPlanes.mY, tempPlanes.mYStride, tempPlanes.mU,
                            tempPlanes.mUStride, tempPlanes.mV,
                            tempPlanes.mVStride, aDest, aDestSize);
      case ImagePixelFormat::NV12:
        rv = MapRv(libyuv::NV12Scale(
            srcY, data->mYStride, srcCb, data->mCbCrStride, imageSize.width,
            imageSize.height, tempPlanes.mY, tempPlanes.mYStride, tempPlanes.mU,
            tempPlanes.mUStride, tempBufSize.width, tempBufSize.height,
            libyuv::FilterMode::kFilterBox));
        if (NS_FAILED(rv)) {
          return rv;
        }
        return NV12ToPlanes(tempPlanes.mY, tempPlanes.mYStride, tempPlanes.mU,
                            tempPlanes.mUStride, aDest, aDestSize);
      default:
        MOZ_ASSERT_UNREACHABLE("YUV format conversion not implemented");
        return NS_ERROR_NOT_IMPLEMENTED;
    }
  }

  MOZ_DIAGNOSTIC_ASSERT(surfaceFormat == SurfaceFormat::B8G8R8X8 ||
                        surfaceFormat == SurfaceFormat::B8G8R8A8 ||
                        surfaceFormat == SurfaceFormat::R8G8B8X8 ||
                        surfaceFormat == SurfaceFormat::R8G8B8A8);

  // We can use the same scaling method for either BGRA or RGBA since the
  // channel orders don't matter to the scaling algorithm.
  rv = MapRv(libyuv::ARGBScale(
      surfaceMap->GetData(), surfaceMap->GetStride(), imageSize.width,
      imageSize.height, tempBuf, tempRgbStride, tempBufSize.width,
      tempBufSize.height, libyuv::FilterMode::kFilterBox));
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Now convert the scale result.
  return RGB32ToPlanes(tempBuf, tempRgbStride, rgbToYuvMatrix, aDest,
                       aDestSize);
}

nsresult ConvertToI420(Image* aImage, uint8_t* aDestY, int aDestStrideY,
                       uint8_t* aDestU, int aDestStrideU, uint8_t* aDestV,
                       int aDestStrideV, const IntSize& aDestSize,
                       YUVColorSpace aDestYUVColorSpace,
                       ColorRange aDestColorRange) {
  const YUV420Planes dest{aDestY,       aDestStrideY, aDestU,
                          aDestStrideU, aDestV,       aDestStrideV};
  return ConvertToYUV420(aImage, dest, aDestSize, aDestYUVColorSpace,
                         aDestColorRange);
}

nsresult ConvertToNV12(layers::Image* aImage, uint8_t* aDestY, int aDestStrideY,
                       uint8_t* aDestUV, int aDestStrideUV,
                       gfx::IntSize aDestSize, YUVColorSpace aDestYUVColorSpace,
                       ColorRange aDestColorRange) {
  const YUV420Planes dest{aDestY,
                          aDestStrideY,
                          aDestUV,
                          aDestStrideUV,
                          nullptr,
                          0,
                          YUV420Planes::PixelFormat::NV12};
  return ConvertToYUV420(aImage, dest, aDestSize, aDestYUVColorSpace,
                         aDestColorRange);
}

static bool IsRGBX(const SurfaceFormat& aFormat) {
  return aFormat == SurfaceFormat::B8G8R8A8 ||
         aFormat == SurfaceFormat::B8G8R8X8 ||
         aFormat == SurfaceFormat::R8G8B8A8 ||
         aFormat == SurfaceFormat::R8G8B8X8 ||
         aFormat == SurfaceFormat::X8R8G8B8 ||
         aFormat == SurfaceFormat::A8R8G8B8;
}

static bool HasAlpha(const SurfaceFormat& aFormat) {
  return aFormat == SurfaceFormat::B8G8R8A8 ||
         aFormat == SurfaceFormat::R8G8B8A8 ||
         aFormat == SurfaceFormat::A8R8G8B8;
}

static nsresult SwapRGBA(DataSourceSurface* aSurface,
                         const SurfaceFormat& aDestFormat) {
  if (!aSurface || !IsRGBX(aSurface->GetFormat()) || !IsRGBX(aDestFormat)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (aSurface->GetFormat() == aDestFormat) {
    return NS_OK;
  }

  DataSourceSurface::ScopedMap map(aSurface, DataSourceSurface::READ_WRITE);
  if (!map.IsMapped()) {
    return NS_ERROR_FAILURE;
  }

  gfx::SwizzleData(map.GetData(), map.GetStride(), aSurface->GetFormat(),
                   map.GetData(), map.GetStride(), aDestFormat,
                   aSurface->GetSize());

  return NS_OK;
}

nsresult ConvertToRGBA(Image* aImage, const SurfaceFormat& aDestFormat,
                       uint8_t* aDestBuffer, int aDestStride) {
  if (!aImage || !aImage->IsValid() || aImage->GetSize().IsEmpty() ||
      !aDestBuffer || !IsRGBX(aDestFormat) || aDestStride <= 0) {
    return NS_ERROR_INVALID_ARG;
  }

  // Read YUV image to the given buffer in required RGBA format.
  if (const PlanarYCbCrData* data = GetPlanarYCbCrData(aImage)) {
    SurfaceFormat convertedFormat = aDestFormat;
    gfx::PremultFunc premultOp = nullptr;
    if (data->mAlpha && HasAlpha(aDestFormat)) {
      if (aDestFormat == SurfaceFormat::A8R8G8B8) {
        convertedFormat = SurfaceFormat::B8G8R8A8;
      }
      if (data->mAlpha->mPremultiplied) {
        premultOp = libyuv::ARGBUnattenuate;
      }
    } else {
      if (aDestFormat == SurfaceFormat::X8R8G8B8 ||
          aDestFormat == SurfaceFormat::A8R8G8B8) {
        convertedFormat = SurfaceFormat::B8G8R8X8;
      }
    }

    nsresult result =
        ConvertYCbCrToRGB32(*data, convertedFormat, aDestBuffer,
                            AssertedCast<int32_t>(aDestStride), premultOp);
    if (NS_FAILED(result)) {
      return result;
    }

    if (convertedFormat == aDestFormat) {
      return NS_OK;
    }

    // Since format of the converted data returned from ConvertYCbCrToRGB or
    // ConvertYCbCrAToARGB is BRG* or RBG*, we need swap the RGBA channels to
    // the required format if needed.

    RefPtr<DataSourceSurface> surf =
        gfx::Factory::CreateWrappingDataSourceSurface(
            aDestBuffer, aDestStride, aImage->GetSize(), convertedFormat);

    if (!surf) {
      return NS_ERROR_FAILURE;
    }

    return SwapRGBA(surf.get(), aDestFormat);
  }

  // Read RGBA image to the given buffer in required RGBA format.

  RefPtr<SourceSurface> surf = GetSourceSurface(aImage);
  if (!surf) {
    return NS_ERROR_FAILURE;
  }

  if (!IsRGBX(surf->GetFormat())) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  RefPtr<DataSourceSurface> src = surf->GetDataSurface();
  if (!src) {
    return NS_ERROR_FAILURE;
  }

  DataSourceSurface::ScopedMap srcMap(src, DataSourceSurface::READ);
  if (!srcMap.IsMapped()) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<DataSourceSurface> dest =
      gfx::Factory::CreateWrappingDataSourceSurface(
          aDestBuffer, aDestStride, aImage->GetSize(), aDestFormat);

  if (!dest) {
    return NS_ERROR_FAILURE;
  }

  DataSourceSurface::ScopedMap destMap(dest, gfx::DataSourceSurface::WRITE);
  if (!destMap.IsMapped()) {
    return NS_ERROR_FAILURE;
  }

  // SwizzleData reads the source over the destination's extent, so the source
  // surface must be at least as large as the destination.
  if (src->GetSize().width < dest->GetSize().width ||
      src->GetSize().height < dest->GetSize().height) {
    return NS_ERROR_FAILURE;
  }

  gfx::SwizzleData(srcMap.GetData(), srcMap.GetStride(), src->GetFormat(),
                   destMap.GetData(), destMap.GetStride(), dest->GetFormat(),
                   dest->GetSize());

  return NS_OK;
}

static SkColorType ToSkColorType(const SurfaceFormat& aFormat) {
  switch (aFormat) {
    case SurfaceFormat::B8G8R8A8:
    case SurfaceFormat::B8G8R8X8:
      return kBGRA_8888_SkColorType;
    case SurfaceFormat::R8G8B8A8:
    case SurfaceFormat::R8G8B8X8:
      return kRGBA_8888_SkColorType;
    default:
      break;
  }
  return kUnknown_SkColorType;
}

nsresult ConvertSRGBBufferToDisplayP3(uint8_t* aSrcBuffer,
                                      const SurfaceFormat& aSrcFormat,
                                      uint8_t* aDestBuffer, int aWidth,
                                      int aHeight) {
  if (!aSrcBuffer || !aDestBuffer || aWidth <= 0 || aHeight <= 0 ||
      !IsRGBX(aSrcFormat)) {
    return NS_ERROR_INVALID_ARG;
  }

  SkColorType srcColorType = ToSkColorType(aSrcFormat);
  if (srcColorType == kUnknown_SkColorType) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  // TODO: Provide source's color space info to customize SkColorSpace.
  auto srcColorSpace = SkColorSpace::MakeSRGB();
  SkImageInfo srcInfo = SkImageInfo::Make(aWidth, aHeight, srcColorType,
                                          kUnpremul_SkAlphaType, srcColorSpace);

  constexpr size_t bytesPerPixel = 4;
  CheckedInt<size_t> rowBytes(bytesPerPixel);
  rowBytes *= aWidth;
  if (!rowBytes.isValid()) {
    return NS_ERROR_DOM_MEDIA_OVERFLOW_ERR;
  }

  SkBitmap srcBitmap;
  if (!srcBitmap.installPixels(srcInfo, aSrcBuffer, rowBytes.value())) {
    return NS_ERROR_FAILURE;
  }

  // TODO: Provide destination's color space info to customize SkColorSpace.
  auto destColorSpace =
      SkColorSpace::MakeRGB(SkNamedTransferFn::kSRGB, SkNamedGamut::kDisplayP3);

  SkBitmap destBitmap;
  if (!destBitmap.tryAllocPixels(srcInfo.makeColorSpace(destColorSpace))) {
    return NS_ERROR_FAILURE;
  }

  if (!srcBitmap.readPixels(destBitmap.pixmap())) {
    return NS_ERROR_FAILURE;
  }

  CheckedInt<size_t> size(rowBytes.value());
  size *= aHeight;
  if (!size.isValid()) {
    return NS_ERROR_DOM_MEDIA_OVERFLOW_ERR;
  }

  PodCopy(aDestBuffer, reinterpret_cast<uint8_t*>(destBitmap.getPixels()),
          size.value());
  return NS_OK;
}

}  // namespace mozilla
