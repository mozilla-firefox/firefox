/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteImageHolder.h"

#include "GPUVideoImage.h"
#include "mozilla/PRemoteDecoderChild.h"
#include "mozilla/RemoteDecodeUtils.h"
#include "mozilla/RemoteMediaManagerChild.h"
#include "mozilla/RemoteMediaManagerParent.h"
#include "mozilla/gfx/SourceSurfaceRawData.h"
#include "mozilla/gfx/Swizzle.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/VideoBridgeUtils.h"

namespace mozilla {

using namespace gfx;
using namespace layers;

RemoteImageHolder::RemoteImageHolder() = default;
RemoteImageHolder::RemoteImageHolder(layers::SurfaceDescriptor&& aSD)
    : mSD(Some(std::move(aSD))) {}
RemoteImageHolder::RemoteImageHolder(
    layers::IGPUVideoSurfaceManager* aManager,
    layers::VideoBridgeSource aSource, const gfx::IntSize& aSize,
    const gfx::ColorDepth& aColorDepth, const layers::SurfaceDescriptor& aSD,
    gfx::YUVColorSpace aYUVColorSpace, gfx::ColorSpace2 aColorPrimaries,
    gfx::TransferFunction aTransferFunction, gfx::ColorRange aColorRange)
    : mSource(aSource),
      mSize(aSize),
      mColorDepth(aColorDepth),
      mSD(Some(aSD)),
      mManager(aManager),
      mYUVColorSpace(aYUVColorSpace),
      mColorPrimaries(aColorPrimaries),
      mTransferFunction(aTransferFunction),
      mColorRange(aColorRange) {}

RemoteImageHolder::RemoteImageHolder(RemoteImageHolder&& aOther)
    : mSource(aOther.mSource),
      mSize(aOther.mSize),
      mColorDepth(aOther.mColorDepth),
      mSD(std::move(aOther.mSD)),
      mManager(aOther.mManager),
      mYUVColorSpace(aOther.mYUVColorSpace),
      mColorPrimaries(aOther.mColorPrimaries),
      mTransferFunction(aOther.mTransferFunction),
      mColorRange(aOther.mColorRange) {
  aOther.mSD = Nothing();
}

already_AddRefed<Image> RemoteImageHolder::DeserializeImage(
    layers::BufferRecycleBin* aBufferRecycleBin) {
  MOZ_ASSERT(mSD && mSD->type() == SurfaceDescriptor::TSurfaceDescriptorBuffer);
  if (!aBufferRecycleBin) {
    return nullptr;
  }

  const SurfaceDescriptorBuffer& sdBuffer = mSD->get_SurfaceDescriptorBuffer();
  const MemoryOrShmem& memOrShmem = sdBuffer.data();
  if (memOrShmem.type() != MemoryOrShmem::TShmem) {
    MOZ_ASSERT_UNREACHABLE("Unexpected MemoryOrShmem type");
    return nullptr;
  }

  // Note that the shmem will be recycled by the parent automatically.
  uint8_t* buffer = memOrShmem.get_Shmem().get<uint8_t>();
  if (!buffer) {
    return nullptr;
  }

  size_t bufferSize = memOrShmem.get_Shmem().Size<uint8_t>();

  if (sdBuffer.desc().type() == BufferDescriptor::TYCbCrDescriptor) {
    const YCbCrDescriptor& descriptor = sdBuffer.desc().get_YCbCrDescriptor();

    Maybe<size_t> descriptorSize = ImageDataSerializer::ComputeYCbCrBufferSize(
        descriptor.display(), descriptor.ySize(), descriptor.yStride(),
        descriptor.cbCrSize(), descriptor.cbCrStride(), descriptor.yOffset(),
        descriptor.cbOffset(), descriptor.crOffset(), descriptor.colorDepth(),
        descriptor.chromaSubsampling());
    if (NS_WARN_IF(descriptorSize.isNothing() ||
                   descriptorSize.value() > bufferSize)) {
      // Skip assertion during gtests.
      if (!PR_GetEnv("MOZ_RUN_GTEST")) {
        MOZ_ASSERT_UNREACHABLE("Buffer too small to fit descriptor!");
      }
      return nullptr;
    }

    if (!IntRect(IntPoint(), descriptor.ySize())
             .Contains(descriptor.display())) {
      if (!PR_GetEnv("MOZ_RUN_GTEST")) {
        MOZ_ASSERT_UNREACHABLE(
            "YCbCr display rect exceeds Y plane dimensions!");
      }
      return nullptr;
    }

    auto croppedCbCr = ImageDataSerializer::GetCroppedCbCrSize(descriptor);
    if (croppedCbCr.width > descriptor.cbCrSize().width ||
        croppedCbCr.height > descriptor.cbCrSize().height) {
      if (!PR_GetEnv("MOZ_RUN_GTEST")) {
        MOZ_ASSERT_UNREACHABLE(
            "YCbCr chroma dimensions exceed CbCr plane size!");
      }
      return nullptr;
    }

    PlanarYCbCrData pData;
    pData.mYStride = descriptor.yStride();
    pData.mCbCrStride = descriptor.cbCrStride();
    // default mYSkip, mCbSkip, mCrSkip because not held in YCbCrDescriptor
    pData.mYSkip = pData.mCbSkip = pData.mCrSkip = 0;
    pData.mPictureRect = descriptor.display();
    pData.mStereoMode = descriptor.stereoMode();
    pData.mColorDepth = descriptor.colorDepth();
    pData.mYUVColorSpace = descriptor.yUVColorSpace();
    pData.mColorRange = descriptor.colorRange();
    pData.mChromaSubsampling = descriptor.chromaSubsampling();
    pData.mYChannel = ImageDataSerializer::GetYChannel(buffer, descriptor);
    pData.mCbChannel = ImageDataSerializer::GetCbChannel(buffer, descriptor);
    pData.mCrChannel = ImageDataSerializer::GetCrChannel(buffer, descriptor);

    // images coming from AOMDecoder are RecyclingPlanarYCbCrImages.
    RefPtr<RecyclingPlanarYCbCrImage> image =
        new RecyclingPlanarYCbCrImage(aBufferRecycleBin);
    if (NS_WARN_IF(NS_FAILED(image->CopyData(pData)))) {
      return nullptr;
    }

    return image.forget();
  }

  if (sdBuffer.desc().type() == BufferDescriptor::TRGBDescriptor) {
    const RGBDescriptor& descriptor = sdBuffer.desc().get_RGBDescriptor();

    Maybe<size_t> descriptorSize = ImageDataSerializer::ComputeRGBBufferSize(
        descriptor.size(), descriptor.format());
    if (NS_WARN_IF(descriptorSize.isNothing() ||
                   descriptorSize.value() > bufferSize)) {
      MOZ_ASSERT_UNREACHABLE("Buffer too small to fit descriptor!");
      return nullptr;
    }

    auto stride = ImageDataSerializer::ComputeRGBStride(
        descriptor.format(), descriptor.size().width);
    if (stride.isNothing()) {
      MOZ_ASSERT_UNREACHABLE("Invalid RBG stride!");
      return nullptr;
    }

    auto surface = MakeRefPtr<SourceSurfaceAlignedRawData>();
    if (NS_WARN_IF(!surface->Init(descriptor.size(), descriptor.format(),
                                  /* aClearMem */ false, /* aClearValue */ 0,
                                  stride.value()))) {
      return nullptr;
    }

    DataSourceSurface::ScopedMap map(surface, DataSourceSurface::WRITE);
    if (NS_WARN_IF(!map.IsMapped())) {
      return nullptr;
    }

    if (NS_WARN_IF(!SwizzleData(buffer, stride.value(), descriptor.format(),
                                map.GetData(), map.GetStride(),
                                descriptor.format(), descriptor.size()))) {
      return nullptr;
    }

    return MakeAndAddRef<SourceSurfaceImage>(descriptor.size(), surface);
  }

  MOZ_ASSERT_UNREACHABLE("Unexpected buffer descriptor type!");
  return nullptr;
}

already_AddRefed<layers::Image> RemoteImageHolder::TransferToImage(
    layers::BufferRecycleBin* aBufferRecycleBin) {
  if (IsEmpty()) {
    return nullptr;
  }
  RefPtr<Image> image;
  if (mSD->type() == SurfaceDescriptor::TSurfaceDescriptorBuffer) {
    image = DeserializeImage(aBufferRecycleBin);
  } else if (mManager) {
    image = mManager->TransferToImage(*mSD, mSize, mColorDepth, mYUVColorSpace,
                                      mColorPrimaries, mTransferFunction,
                                      mColorRange);
  }
  mSD = Nothing();
  mManager = nullptr;

  return image.forget();
}

RemoteImageHolder::~RemoteImageHolder() {
  // GPU Images are held by the RemoteMediaManagerParent, we didn't get to use
  // this image holder (the decoder could have been flushed). We don't need to
  // worry about Shmem based image as the Shmem will be automatically re-used
  // once the decoder is used again.
  if (!IsEmpty() && mManager &&
      mSD->type() != SurfaceDescriptor::TSurfaceDescriptorBuffer) {
    SurfaceDescriptorRemoteDecoder remoteSD =
        static_cast<const SurfaceDescriptorGPUVideo&>(*mSD);
    mManager->DeallocateSurfaceDescriptor(remoteSD);
  }
}

}  // namespace mozilla

/* static */ void IPC::ParamTraits<mozilla::RemoteImageHolder>::Write(
    MessageWriter* aWriter, mozilla::RemoteImageHolder&& aParam) {
  WriteParam(aWriter, aParam.mSource);
  WriteParam(aWriter, aParam.mSize);
  WriteParam(aWriter, aParam.mColorDepth);
  WriteParam(aWriter, aParam.mSD);
  WriteParam(aWriter, aParam.mYUVColorSpace);
  WriteParam(aWriter, aParam.mColorPrimaries);
  WriteParam(aWriter, aParam.mTransferFunction);
  WriteParam(aWriter, aParam.mColorRange);
  // Empty this holder.
  aParam.mSD = mozilla::Nothing();
  aParam.mManager = nullptr;
}

/* static */ bool IPC::ParamTraits<mozilla::RemoteImageHolder>::Read(
    MessageReader* aReader, mozilla::RemoteImageHolder* aResult) {
  if (!ReadParam(aReader, &aResult->mSource) ||
      !ReadParam(aReader, &aResult->mSize) ||
      !ReadParam(aReader, &aResult->mColorDepth) ||
      !ReadParam(aReader, &aResult->mSD) ||
      !ReadParam(aReader, &aResult->mYUVColorSpace) ||
      !ReadParam(aReader, &aResult->mColorPrimaries) ||
      !ReadParam(aReader, &aResult->mTransferFunction) ||
      !ReadParam(aReader, &aResult->mColorRange)) {
    return false;
  }

  if (aResult->IsEmpty()) {
    return true;
  }

  if (auto* actor = aReader->GetActor()) {
    if (XRE_IsContentProcess()) {
      aResult->mManager =
          ActorDynCast<mozilla::RemoteMediaManagerChild>(actor->Manager());
    } else {
      aResult->mManager =
          ActorDynCast<mozilla::RemoteMediaManagerParent>(actor->Manager());
    }
  }

  if (!aResult->mManager) {
    MOZ_ASSERT_UNREACHABLE("Unexpected or missing protocol manager!");
    return false;
  }
  return true;
}
