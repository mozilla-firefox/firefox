/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ImageUtils.h"

#include "ImageContainer.h"
#include "Intervals.h"
#include "mozilla/dom/ImageBitmapBinding.h"

using namespace mozilla::layers;
using namespace mozilla::gfx;

namespace mozilla::dom {

static Maybe<ImageBitmapFormat> GetImageBitmapFormatFromSurfaceFromat(
    SurfaceFormat aSurfaceFormat) {
  switch (aSurfaceFormat) {
    case SurfaceFormat::B8G8R8A8:
    case SurfaceFormat::B8G8R8X8:
      return Some(ImageBitmapFormat::BGRA32);
    case SurfaceFormat::R8G8B8A8:
    case SurfaceFormat::R8G8B8X8:
      return Some(ImageBitmapFormat::RGBA32);
    case SurfaceFormat::R8G8B8:
      return Some(ImageBitmapFormat::RGB24);
    case SurfaceFormat::B8G8R8:
      return Some(ImageBitmapFormat::BGR24);
    case SurfaceFormat::HSV:
      return Some(ImageBitmapFormat::HSV);
    case SurfaceFormat::Lab:
      return Some(ImageBitmapFormat::Lab);
    case SurfaceFormat::Depth:
      return Some(ImageBitmapFormat::DEPTH);
    case SurfaceFormat::A8:
      return Some(ImageBitmapFormat::GRAY8);
    case SurfaceFormat::R5G6B5_UINT16:
    case SurfaceFormat::R10G10B10A2_UINT32:
    case SurfaceFormat::R10G10B10X2_UINT32:
    case SurfaceFormat::R16G16B16A16F:
    case SurfaceFormat::YUV420:
    case SurfaceFormat::NV12:
    case SurfaceFormat::P010:
    case SurfaceFormat::P016:
    case SurfaceFormat::UNKNOWN:
    default:
      return Nothing();
  }
}

static Maybe<ImageBitmapFormat> GetImageBitmapFormatFromPlanarYCbCrData(
    layers::PlanarYCbCrData const* aData) {
  MOZ_ASSERT(aData);

  const auto sizes = aData->GetPlaneSizes();
  if (!sizes) {
    return Nothing();
  }
  const auto& ySize = sizes->mY;
  const auto& cbcrSize = sizes->mCbCr;
  media::Interval<uintptr_t> YInterval(
      uintptr_t(aData->mYChannel),
      uintptr_t(aData->mYChannel) + ySize.height * aData->mYStride),
      CbInterval(
          uintptr_t(aData -> mCbChannel),
          uintptr_t(aData->mCbChannel) + cbcrSize.height * aData->mCbCrStride),
      CrInterval(
          uintptr_t(aData->mCrChannel),
          uintptr_t(aData->mCrChannel) + cbcrSize.height * aData->mCbCrStride);
  if (aData->mYSkip == 0 && aData->mCbSkip == 0 &&
      aData->mCrSkip == 0) {  // Possibly three planes.
    if (!YInterval.Intersects(CbInterval) &&
        !CbInterval.Intersects(CrInterval)) {  // Three planes.
      switch (aData->mChromaSubsampling) {
        case ChromaSubsampling::FULL:
          return Some(ImageBitmapFormat::YUV444P);
        case ChromaSubsampling::HALF_WIDTH:
          return Some(ImageBitmapFormat::YUV422P);
        case ChromaSubsampling::HALF_WIDTH_AND_HEIGHT:
          return Some(ImageBitmapFormat::YUV420P);
        default:
          break;
      }
    }
  } else if (aData->mYSkip == 0 && aData->mCbSkip == 1 && aData->mCrSkip == 1 &&
             aData->mChromaSubsampling ==
                 ChromaSubsampling::HALF_WIDTH_AND_HEIGHT) {  // Possibly two
                                                              // planes.
    if (!YInterval.Intersects(CbInterval) &&
        aData->mCbChannel == aData->mCrChannel - 1) {  // Two planes.
      return Some(ImageBitmapFormat::YUV420SP_NV12);   // Y-Cb-Cr
    } else if (!YInterval.Intersects(CrInterval) &&
               aData->mCrChannel == aData->mCbChannel - 1) {  // Two planes.
      return Some(ImageBitmapFormat::YUV420SP_NV21);          // Y-Cr-Cb
    }
  }

  return Nothing();
}

// ImageUtils::Impl implements the _generic_ algorithm which always readback
// data in RGBA format into CPU memory.
// Since layers::CairoImage is just a warpper to a DataSourceSurface, the
// implementation of CairoSurfaceImpl is nothing different to the generic
// version.
class ImageUtils::Impl {
 public:
  explicit Impl(layers::Image* aImage) : mImage(aImage), mSurface(nullptr) {}

  virtual ~Impl() = default;

  virtual Maybe<ImageBitmapFormat> GetFormat() const {
    if (DataSourceSurface* surface = Surface()) {
      return GetImageBitmapFormatFromSurfaceFromat(surface->GetFormat());
    }
    return Nothing();
  }

  virtual uint32_t GetBufferLength() const {
    if (DataSourceSurface* surface = Surface()) {
      DataSourceSurface::ScopedMap map(surface, DataSourceSurface::READ);
      const uint32_t stride = map.GetStride();
      const IntSize size = surface->GetSize();
      return (uint32_t)(size.height * stride);
    }
    return 0;
  }

 protected:
  Impl() = default;

  DataSourceSurface* Surface() const {
    if (mSurface) {
      return mSurface.get();
    }

    RefPtr<SourceSurface> surface = mImage->GetAsSourceSurface();
    if (NS_WARN_IF(!surface)) {
      return nullptr;
    }

    mSurface = surface->GetDataSurface();
    MOZ_ASSERT(mSurface);
    return mSurface.get();
  }

  RefPtr<layers::Image> mImage;
  mutable RefPtr<DataSourceSurface> mSurface;
};

// YUVImpl is optimized for the layers::PlanarYCbCrImage and layers::NVImage.
// This implementation does not readback data in RGBA format but keep it in YUV
// format family.
class YUVImpl final : public ImageUtils::Impl {
 public:
  explicit YUVImpl(layers::Image* aImage) : Impl(aImage) {
    MOZ_ASSERT(aImage->GetFormat() == ImageFormat::PLANAR_YCBCR ||
               aImage->GetFormat() == ImageFormat::NV_IMAGE);
  }

  Maybe<ImageBitmapFormat> GetFormat() const override {
    return GetImageBitmapFormatFromPlanarYCbCrData(GetPlanarYCbCrData());
  }

  uint32_t GetBufferLength() const override {
    if (mImage->GetFormat() == ImageFormat::PLANAR_YCBCR) {
      return mImage->AsPlanarYCbCrImage()->GetDataSize();
    }
    return mImage->AsNVImage()->GetBufferSize();
  }

 private:
  const PlanarYCbCrData* GetPlanarYCbCrData() const {
    if (mImage->GetFormat() == ImageFormat::PLANAR_YCBCR) {
      return mImage->AsPlanarYCbCrImage()->GetData();
    }
    return mImage->AsNVImage()->GetData();
  }
};

// TODO: optimize for other platforms.
// For Windows: implement D3D9RGB32TextureImpl and D3D11ShareHandleTextureImpl.
// Others: SharedBGRImpl, MACIOSrufaceImpl, GLImageImpl, SurfaceTextureImpl
//         EGLImageImpl and OverlayImegImpl.

ImageUtils::ImageUtils(layers::Image* aImage) : mImpl(nullptr) {
  MOZ_ASSERT(aImage, "Create ImageUtils with nullptr.");
  switch (aImage->GetFormat()) {
    case mozilla::ImageFormat::PLANAR_YCBCR:
    case mozilla::ImageFormat::NV_IMAGE:
      mImpl = new YUVImpl(aImage);
      break;
    case mozilla::ImageFormat::MOZ2D_SURFACE:
    default:
      mImpl = new Impl(aImage);
  }
}

ImageUtils::~ImageUtils() {
  if (mImpl) {
    delete mImpl;
    mImpl = nullptr;
  }
}

Maybe<ImageBitmapFormat> ImageUtils::GetFormat() const {
  MOZ_ASSERT(mImpl);
  return mImpl->GetFormat();
}

uint32_t ImageUtils::GetBufferLength() const {
  MOZ_ASSERT(mImpl);
  return mImpl->GetBufferLength();
}

}  // namespace mozilla::dom
