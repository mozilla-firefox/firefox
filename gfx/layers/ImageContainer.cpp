/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageContainer.h"

#include <string.h>  // for memcpy, memset

#include "GLImages.h"  // for SurfaceTextureImage
#include "GPUVideoImage.h"
#include "MediaInfo.h"   // VideoInfo::Rotation
#include "YCbCrUtils.h"  // for YCbCr conversions
#include "gfx2DGlue.h"
#include "gfxPlatform.h"  // for gfxPlatform
#include "gfxUtils.h"     // for gfxUtils
#include "libyuv.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/RefPtr.h"  // for already_AddRefed
#include "mozilla/StaticPrefs_layers.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Swizzle.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/ipc/CrossProcessMutex.h"  // for CrossProcessMutex, etc
#include "mozilla/layers/CompositorTypes.h"
#include "mozilla/layers/ImageBridgeChild.h"     // for ImageBridgeChild
#include "mozilla/layers/ImageClient.h"          // for ImageClient
#include "mozilla/layers/ImageDataSerializer.h"  // for SurfaceDescriptorBuffer
#include "mozilla/layers/LayersMessages.h"
#include "mozilla/layers/SharedPlanarYCbCrImage.h"
#include "mozilla/layers/SharedRGBImage.h"
#include "mozilla/layers/TextureClientRecycleAllocator.h"
#include "nsISupportsUtils.h"  // for NS_IF_ADDREF
#include "nsProxyRelease.h"

#ifdef XP_DARWIN
#  include "MacIOSurfaceImage.h"
#endif

#ifdef XP_WIN
#  include <d3d10_1.h>

#  include "gfxWindowsPlatform.h"
#  include "mozilla/gfx/DeviceManagerDx.h"
#  include "mozilla/layers/D3D11ShareHandleImage.h"
#  include "mozilla/layers/D3D11YCbCrImage.h"
#endif

namespace mozilla::layers {

using namespace mozilla::gfx;
using namespace mozilla::ipc;

Atomic<int32_t> Image::sSerialCounter(0);

Atomic<uint32_t> ImageContainer::sGenerationCounter(0);

static Maybe<IntSize> CheckedPictureRectEnd(const IntRect& aPictureRect) {
  if (aPictureRect.IsEmpty() || aPictureRect.X() < 0 || aPictureRect.Y() < 0) {
    return Nothing();
  }

  CheckedInt32 right(aPictureRect.X());
  right += aPictureRect.Width();
  CheckedInt32 bottom(aPictureRect.Y());
  bottom += aPictureRect.Height();
  if (!right.isValid() || !bottom.isValid()) {
    return Nothing();
  }
  return Some(IntSize(right.value(), bottom.value()));
}

Maybe<IntSize> Image::GetAddressableSize() const {
  const IntSize size = GetSize();
  const Maybe<IntSize> pictureEnd = CheckedPictureRectEnd(GetPictureRect());
  if (!IsValid() || size.IsEmpty() || !pictureEnd ||
      pictureEnd->width > size.width || pictureEnd->height > size.height) {
    return Nothing();
  }
  return Some(size);
}

static void CopyPlane(uint8_t* aDst, const uint8_t* aSrc,
                      const gfx::IntSize& aSize, int32_t aStride, int32_t aSkip,
                      int32_t aBytesPerElement);

RefPtr<PlanarYCbCrImage> ImageFactory::CreatePlanarYCbCrImage(
    const gfx::IntSize& aScaleHint, BufferRecycleBin* aRecycleBin) {
  return MakeRefPtr<RecyclingPlanarYCbCrImage>(aRecycleBin);
}

BufferRecycleBin::BufferRecycleBin()
    : mLock("mozilla.layers.BufferRecycleBin.mLock")
      // This member is only valid when the bin is not empty and will be
      // properly initialized in RecycleBuffer, but initializing it here avoids
      // static analysis noise.
      ,
      mRecycledBufferSize(0) {}

void BufferRecycleBin::RecycleBuffer(UniquePtr<uint8_t[]> aBuffer,
                                     uint32_t aSize) {
  MutexAutoLock lock(mLock);

  if (!mRecycledBuffers.IsEmpty() && aSize != mRecycledBufferSize) {
    mRecycledBuffers.Clear();
  }
  mRecycledBufferSize = aSize;
  mRecycledBuffers.AppendElement(std::move(aBuffer));
}

UniquePtr<uint8_t[]> BufferRecycleBin::GetBuffer(uint32_t aSize) {
  MutexAutoLock lock(mLock);

  if (mRecycledBuffers.IsEmpty() || mRecycledBufferSize != aSize) {
    return MakeUniqueFallible<uint8_t[]>(aSize);
  }

  return mRecycledBuffers.PopLastElement();
}

void BufferRecycleBin::ClearRecycledBuffers() {
  MutexAutoLock lock(mLock);
  if (!mRecycledBuffers.IsEmpty()) {
    mRecycledBuffers.Clear();
  }
  mRecycledBufferSize = 0;
}

ImageContainerListener::ImageContainerListener(ImageContainer* aImageContainer)
    : mLock("mozilla.layers.ImageContainerListener.mLock"),
      mImageContainer(aImageContainer) {}

ImageContainerListener::~ImageContainerListener() = default;

void ImageContainerListener::NotifyComposite(
    const ImageCompositeNotification& aNotification) {
  MutexAutoLock lock(mLock);
  if (mImageContainer) {
    mImageContainer->NotifyComposite(aNotification);
  }
}

void ImageContainerListener::NotifyDropped(uint32_t aDropped) {
  MutexAutoLock lock(mLock);
  if (mImageContainer) {
    mImageContainer->NotifyDropped(aDropped);
  }
}

void ImageContainerListener::ClearImageContainer() {
  MutexAutoLock lock(mLock);
  mImageContainer = nullptr;
}

void ImageContainerListener::DropImageClient() {
  MutexAutoLock lock(mLock);
  if (mImageContainer) {
    mImageContainer->DropImageClient();
  }
}

already_AddRefed<ImageClient> ImageContainer::GetImageClient() {
  RecursiveMutexAutoLock mon(mRecursiveMutex);
  EnsureImageClient();
  RefPtr<ImageClient> imageClient = mImageClient;
  return imageClient.forget();
}

void ImageContainer::DropImageClient() {
  RecursiveMutexAutoLock mon(mRecursiveMutex);
  if (mImageClient) {
    mImageClient->ClearCachedResources();
    mImageClient = nullptr;
  }
}

void ImageContainer::EnsureImageClient() {
  // If we're not forcing a new ImageClient, then we can skip this if we don't
  // have an existing ImageClient, or if the existing one belongs to an IPC
  // actor that is still open.
  if (!mIsAsync) {
    return;
  }
  if (mImageClient &&
      mImageClient->GetForwarder()->GetLayersIPCActor()->IPCOpen()) {
    return;
  }

  RefPtr<ImageBridgeChild> imageBridge = ImageBridgeChild::GetSingleton();
  if (!imageBridge) {
    return;
  }

  mImageClient = imageBridge->CreateImageClient(CompositableType::IMAGE, this);
  if (mImageClient) {
    mAsyncContainerHandle = mImageClient->GetAsyncHandle();
  } else {
    // It's okay to drop the async container handle since the ImageBridgeChild
    // is going to die anyway.
    mAsyncContainerHandle = CompositableHandle();
  }
}

ImageContainer::ImageContainer(ImageUsageType aUsageType, Mode aFlag)
    : mUsageType(aUsageType),
      mIsAsync(aFlag == ASYNCHRONOUS),
      mRecursiveMutex("ImageContainer.mRecursiveMutex"),
      mGenerationCounter(++sGenerationCounter),
      mPaintCount(0),
      mDroppedImageCount(0),
      mImageFactory(new ImageFactory()),
      mRotation(VideoRotation::kDegree_0),
      mRecycleBin(MakeRefPtr<BufferRecycleBin>()),
      mCurrentProducerID(-1) {
  if (aFlag == ASYNCHRONOUS) {
    mNotifyCompositeListener = MakeRefPtr<ImageContainerListener>(this);
    EnsureImageClient();
  }
}

ImageContainer::~ImageContainer() {
  if (mNotifyCompositeListener) {
    mNotifyCompositeListener->ClearImageContainer();
  }
  if (mAsyncContainerHandle) {
    if (RefPtr<ImageBridgeChild> imageBridge =
            ImageBridgeChild::GetSingleton()) {
      imageBridge->ForgetImageContainer(mAsyncContainerHandle);
    }
  }
}

/* static */ nsresult Image::AllocateSurfaceDescriptorBufferRgb(
    const gfx::IntSize& aSize, gfx::SurfaceFormat aFormat, uint8_t*& aOutBuffer,
    SurfaceDescriptorBuffer& aSdBuffer, int32_t& aStride,
    const std::function<layers::MemoryOrShmem(uint32_t)>& aAllocate) {
  auto stride = ImageDataSerializer::ComputeRGBStride(aFormat, aSize.width);
  Maybe<uint32_t> length =
      ImageDataSerializer::ComputeRGBBufferSize(aSize, aFormat);

  if (stride.isNothing() || length.isNothing()) {
    return NS_ERROR_INVALID_ARG;
  }

  aStride = stride.value();

  aSdBuffer.desc() = RGBDescriptor(aSize, aFormat, gfx::ColorSpace2::SRGB,
                                   gfx::TransferFunction::SRGB);
  aSdBuffer.data() = aAllocate(length.value());

  const layers::MemoryOrShmem& memOrShmem = aSdBuffer.data();
  switch (memOrShmem.type()) {
    case layers::MemoryOrShmem::Tuintptr_t:
      aOutBuffer = reinterpret_cast<uint8_t*>(memOrShmem.get_uintptr_t());
      break;
    case layers::MemoryOrShmem::TShmem:
      aOutBuffer = memOrShmem.get_Shmem().get<uint8_t>();
      break;
    default:
      return NS_ERROR_OUT_OF_MEMORY;
  }

  MOZ_ASSERT(aOutBuffer);
  return NS_OK;
}

nsresult Image::BuildSurfaceDescriptorBuffer(
    SurfaceDescriptorBuffer& aSdBuffer, BuildSdbFlags aFlags,
    const std::function<MemoryOrShmem(uint32_t)>& aAllocate) {
  RefPtr<SourceSurface> surface = GetAsSourceSurface();
  if (NS_WARN_IF(!surface)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  RefPtr<DataSourceSurface> dataSurface = surface->GetDataSurface();
  if (NS_WARN_IF(!dataSurface)) {
    return NS_ERROR_FAILURE;
  }

  DataSourceSurface::ScopedMap map(dataSurface, DataSourceSurface::READ);
  if (NS_WARN_IF(!map.IsMapped())) {
    return NS_ERROR_FAILURE;
  }

  SurfaceFormat format = dataSurface->GetFormat();
  IntSize size = dataSurface->GetSize();
  uint8_t* output = nullptr;
  int32_t stride = 0;
  nsresult rv = AllocateSurfaceDescriptorBufferRgb(
      size, format, output, aSdBuffer, stride, aAllocate);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (NS_WARN_IF(!SwizzleData(map.GetData(), map.GetStride(), format, output,
                              stride, format, size))) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult Image::BuildSurfaceDescriptorGPUVideoOrBuffer(
    SurfaceDescriptor& aSd, BuildSdbFlags aFlags,
    const Maybe<VideoBridgeSource>& aDest,
    const std::function<MemoryOrShmem(uint32_t)>& aAllocate,
    const std::function<void(MemoryOrShmem&&)>& aFree) {
  if (auto* gpuImage = AsGPUVideoImage()) {
    if (auto maybeSd = gpuImage->GetDesc()) {
      if (!aDest ||
          (maybeSd->type() == SurfaceDescriptor::TSurfaceDescriptorGPUVideo &&
           maybeSd->get_SurfaceDescriptorGPUVideo()
                   .get_SurfaceDescriptorRemoteDecoder()
                   .source() == aDest)) {
        aSd = std::move(*maybeSd);
        return NS_OK;
      }
    }
  }

  SurfaceDescriptorBuffer sdb;
  nsresult rv = BuildSurfaceDescriptorBuffer(sdb, aFlags, aAllocate);
  if (NS_FAILED(rv)) {
    if (sdb.data().type() != MemoryOrShmem::Type::T__None) {
      aFree(std::move(sdb.data()));
    }
    return rv;
  }

  aSd = std::move(sdb);
  return NS_OK;
}

Maybe<SurfaceDescriptor> Image::GetDesc() { return GetDescFromTexClient(); }

Maybe<SurfaceDescriptor> Image::GetDescFromTexClient(
    TextureClient* const tcOverride) {
  RefPtr<TextureClient> tc = tcOverride;
  if (!tcOverride) {
    tc = GetTextureClient(nullptr);
  }
  if (!tc) {
    return {};
  }

  SurfaceDescriptor ret;
  if (!tc->ToSurfaceDescriptor(ret)) {
    return {};
  }
  return Some(ret);
}

already_AddRefed<ImageContainerListener>
ImageContainer::GetImageContainerListener() const {
  MOZ_ASSERT(InImageBridgeChildThread());
  return do_AddRef(mNotifyCompositeListener);
}

RefPtr<PlanarYCbCrImage> ImageContainer::CreatePlanarYCbCrImage() {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  EnsureImageClient();
  if (mImageClient && mImageClient->AsImageClientSingle()) {
    return MakeRefPtr<SharedPlanarYCbCrImage>(mImageClient);
  }
  if (mRecycleAllocator) {
    return MakeRefPtr<SharedPlanarYCbCrImage>(mRecycleAllocator);
  }
  return mImageFactory->CreatePlanarYCbCrImage(mScaleHint, mRecycleBin);
}

RefPtr<SharedRGBImage> ImageContainer::CreateSharedRGBImage() {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  EnsureImageClient();
  if (mImageClient && mImageClient->AsImageClientSingle()) {
    return MakeRefPtr<SharedRGBImage>(mImageClient);
  }
  if (mRecycleAllocator) {
    return MakeRefPtr<SharedRGBImage>(mRecycleAllocator);
  }
  return nullptr;
}

void ImageContainer::SetCurrentImageInternal(
    const nsTArray<NonOwningImage>& aImages) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);

  mGenerationCounter = ++sGenerationCounter;

  if (!aImages.IsEmpty()) {
    NS_ASSERTION(mCurrentImages.IsEmpty() ||
                     mCurrentImages[0].mProducerID != aImages[0].mProducerID ||
                     mCurrentImages[0].mFrameID <= aImages[0].mFrameID,
                 "frame IDs shouldn't go backwards");
    if (aImages[0].mProducerID != mCurrentProducerID) {
      mCurrentProducerID = aImages[0].mProducerID;
    }
    for (auto& img : mCurrentImages) {
      img.mImage->OnAbandonForwardToHost();
    }
  }

  nsTArray<OwningImage> newImages;

  for (uint32_t i = 0; i < aImages.Length(); ++i) {
    NS_ASSERTION(aImages[i].mImage, "image can't be null");
    NS_ASSERTION(!aImages[i].mTimeStamp.IsNull() || aImages.Length() == 1,
                 "Multiple images require timestamps");
    if (i > 0) {
      NS_ASSERTION(aImages[i].mTimeStamp >= aImages[i - 1].mTimeStamp,
                   "Timestamps must not decrease");
      NS_ASSERTION(aImages[i].mFrameID > aImages[i - 1].mFrameID,
                   "FrameIDs must increase");
      NS_ASSERTION(aImages[i].mProducerID == aImages[i - 1].mProducerID,
                   "ProducerIDs must be the same");
    }
    OwningImage* img = newImages.AppendElement();
    img->mImage = aImages[i].mImage;
    img->mTimeStamp = aImages[i].mTimeStamp;
    img->mProcessingDuration = aImages[i].mProcessingDuration;
    img->mMediaTime = aImages[i].mMediaTime;
    img->mWebrtcCaptureTime = aImages[i].mWebrtcCaptureTime;
    img->mWebrtcReceiveTime = aImages[i].mWebrtcReceiveTime;
    img->mRtpTimestamp = aImages[i].mRtpTimestamp;
    img->mFrameID = aImages[i].mFrameID;
    img->mProducerID = aImages[i].mProducerID;
    img->mRotation = aImages[i].mRotation;
    for (const auto& oldImg : mCurrentImages) {
      if (oldImg.mFrameID == img->mFrameID &&
          oldImg.mProducerID == img->mProducerID) {
        img->mComposited = oldImg.mComposited;
        break;
      }
    }
    img->mImage->OnPrepareForwardToHost();
  }

  mCurrentImages = std::move(newImages);
}

void ImageContainer::ClearImagesFromImageBridge() {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  SetCurrentImageInternal(nsTArray<NonOwningImage>());
}

void ImageContainer::SetCurrentImages(const nsTArray<NonOwningImage>& aImages) {
  AUTO_PROFILER_LABEL("ImageContainer::SetCurrentImages", GRAPHICS);
  MOZ_ASSERT(!aImages.IsEmpty());
  MOZ_ASSERT(mUsageType == ImageUsageType::Canvas ||
             mUsageType == ImageUsageType::OffscreenCanvas ||
             mUsageType == ImageUsageType::VideoFrameContainer);

  RecursiveMutexAutoLock lock(mRecursiveMutex);
  if (mIsAsync) {
    if (RefPtr<ImageBridgeChild> imageBridge =
            ImageBridgeChild::GetSingleton()) {
      imageBridge->UpdateImageClient(this);
    }
  }
  SetCurrentImageInternal(aImages);
}

void ImageContainer::ClearImagesInHost(ClearImagesType aType) {
  MOZ_ASSERT(mIsAsync);
  if (!mIsAsync) {
    return;
  }

  mRecursiveMutex.Lock();
  if (mImageClient) {
    RefPtr<ImageClient> imageClient = mImageClient;
    mRecursiveMutex.Unlock();

    // Let ImageClient clear Images(TextureClients). This doesn't return
    // until ImageBridge has called ImageClient::ClearImagesInHost.
    if (RefPtr<ImageBridgeChild> imageBridge =
            ImageBridgeChild::GetSingleton()) {
      imageBridge->ClearImagesInHost(imageClient, this, aType);
    }
    return;
  }

  SetCurrentImageInternal(nsTArray<NonOwningImage>());
  mRecursiveMutex.Unlock();
}

void ImageContainer::ClearCachedResources() {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  if (mImageClient && mImageClient->AsImageClientSingle()) {
    if (!mImageClient->HasTextureClientRecycler()) {
      return;
    }
    mImageClient->GetTextureClientRecycler()->ShrinkToMinimumSize();
    return;
  }
  return mRecycleBin->ClearRecycledBuffers();
}

void ImageContainer::SetCurrentImageInTransaction(Image* aImage) {
  AutoTArray<NonOwningImage, 1> images;
  images.AppendElement(NonOwningImage(aImage));
  SetCurrentImagesInTransaction(images);
}

void ImageContainer::SetCurrentImagesInTransaction(
    const nsTArray<NonOwningImage>& aImages) {
  MOZ_ASSERT(!mIsAsync);
  MOZ_ASSERT(mUsageType == ImageUsageType::WebRenderFallbackData);

  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  NS_ASSERTION(!HasImageClient(),
               "Should use async image transfer with ImageBridge.");

  SetCurrentImageInternal(aImages);
}

bool ImageContainer::IsAsync() const { return mIsAsync; }

CompositableHandle ImageContainer::GetAsyncContainerHandle() {
  NS_ASSERTION(IsAsync(),
               "Shared image ID is only relevant to async ImageContainers");
  RecursiveMutexAutoLock mon(mRecursiveMutex);
  NS_ASSERTION(mAsyncContainerHandle, "Should have a shared image ID");
  EnsureImageClient();
  return mAsyncContainerHandle;
}

bool ImageContainer::HasCurrentImage() {
  RecursiveMutexAutoLock lock(mRecursiveMutex);

  return !mCurrentImages.IsEmpty();
}

void ImageContainer::GetCurrentImages(nsTArray<OwningImage>* aImages,
                                      uint32_t* aGenerationCounter) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);

  aImages->Assign(mCurrentImages);
  if (aGenerationCounter) {
    *aGenerationCounter = mGenerationCounter;
  }
}

gfx::IntSize ImageContainer::GetCurrentSize() {
  RecursiveMutexAutoLock lock(mRecursiveMutex);

  if (mCurrentImages.IsEmpty()) {
    return gfx::IntSize(0, 0);
  }

  return mCurrentImages[0].mImage->GetSize();
}

void ImageContainer::NotifyComposite(
    const ImageCompositeNotification& aNotification) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);

  // An image composition notification is sent the first time a particular
  // image is composited by an ImageHost. Thus, every time we receive such
  // a notification, a new image has been painted.
  ++mPaintCount;

  if (aNotification.producerID() == mCurrentProducerID) {
    for (auto& img : mCurrentImages) {
      if (img.mFrameID == aNotification.frameID()) {
        img.mComposited = true;
      }
    }
  }

  if (!aNotification.imageTimeStamp().IsNull()) {
    mPaintDelay = aNotification.firstCompositeTimeStamp() -
                  aNotification.imageTimeStamp();
  }
}

void ImageContainer::NotifyDropped(uint32_t aDropped) {
  mDroppedImageCount += aDropped;
}

void ImageContainer::EnsureRecycleAllocatorForRDD(
    KnowsCompositor* aKnowsCompositor) {
  MOZ_ASSERT(!mIsAsync);
  MOZ_ASSERT(XRE_IsRDDProcess());

  RecursiveMutexAutoLock lock(mRecursiveMutex);
  MOZ_ASSERT(!mImageClient);

  if (mRecycleAllocator &&
      aKnowsCompositor == mRecycleAllocator->GetKnowsCompositor()) {
    return;
  }

  if (!StaticPrefs::layers_recycle_allocator_rdd_AtStartup()) {
    return;
  }

  static const uint32_t MAX_POOLED_VIDEO_COUNT = 5;

  mRecycleAllocator =
      MakeRefPtr<layers::TextureClientRecycleAllocator>(aKnowsCompositor);
  mRecycleAllocator->SetMaxPoolSize(MAX_POOLED_VIDEO_COUNT);
}

#ifdef XP_WIN
already_AddRefed<D3D11RecycleAllocator>
ImageContainer::GetD3D11RecycleAllocator(KnowsCompositor* aKnowsCompositor,
                                         gfx::SurfaceFormat aPreferredFormat) {
  MOZ_ASSERT(aKnowsCompositor);

  if (!aKnowsCompositor->SupportsD3D11()) {
    return nullptr;
  }

  RefPtr<ID3D11Device> device = gfx::DeviceManagerDx::Get()->GetImageDevice();
  if (!device) {
    return nullptr;
  }

  RecursiveMutexAutoLock lock(mRecursiveMutex);
  if (mD3D11RecycleAllocator && mD3D11RecycleAllocator->mDevice == device &&
      mD3D11RecycleAllocator->GetKnowsCompositor() == aKnowsCompositor) {
    return do_AddRef(mD3D11RecycleAllocator);
  }

  mD3D11RecycleAllocator = MakeRefPtr<D3D11RecycleAllocator>(
      aKnowsCompositor, device, aPreferredFormat);

  if (device != DeviceManagerDx::Get()->GetCompositorDevice()) {
    RefPtr<SyncObjectClient> syncObject =
        SyncObjectClient::CreateSyncObjectClient(
            aKnowsCompositor->GetTextureFactoryIdentifier().mSyncHandle,
            device);
    mD3D11RecycleAllocator->SetSyncObject(syncObject);
  }

  return do_AddRef(mD3D11RecycleAllocator);
}

already_AddRefed<D3D11YCbCrRecycleAllocator>
ImageContainer::GetD3D11YCbCrRecycleAllocator(
    KnowsCompositor* aKnowsCompositor) {
  if (!aKnowsCompositor->SupportsD3D11() ||
      !gfx::DeviceManagerDx::Get()->GetImageDevice()) {
    return nullptr;
  }

  RecursiveMutexAutoLock lock(mRecursiveMutex);
  if (mD3D11YCbCrRecycleAllocator &&
      aKnowsCompositor == mD3D11YCbCrRecycleAllocator->GetKnowsCompositor()) {
    return do_AddRef(mD3D11YCbCrRecycleAllocator);
  }

  mD3D11YCbCrRecycleAllocator =
      MakeRefPtr<D3D11YCbCrRecycleAllocator>(aKnowsCompositor);
  return do_AddRef(mD3D11YCbCrRecycleAllocator);
}
#endif

#ifdef XP_DARWIN
already_AddRefed<MacIOSurfaceRecycleAllocator>
ImageContainer::GetMacIOSurfaceRecycleAllocator() {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  if (!mMacIOSurfaceRecycleAllocator) {
    mMacIOSurfaceRecycleAllocator = MakeRefPtr<MacIOSurfaceRecycleAllocator>();
  }

  return do_AddRef(mMacIOSurfaceRecycleAllocator);
}
#endif

// -
// https://searchfox.org/firefox-main/source/dom/media/ipc/RemoteImageHolder.cpp#46

Maybe<PlanarYCbCrData::PlaneSizes> PlanarYCbCrData::GetPlaneSizes() const {
  const Maybe<IntSize> ySize = CheckedPictureRectEnd(mPictureRect);
  if (!ySize) {
    return Nothing();
  }
  return Some(PlaneSizes{*ySize, mCbCrStride > 0
                                     ? ChromaSize(*ySize, mChromaSubsampling)
                                     : IntSize(0, 0)});
}

Maybe<PlanarYCbCrData> PlanarYCbCrData::From(
    const SurfaceDescriptorBuffer& sdb) {
  if (sdb.desc().type() != BufferDescriptor::TYCbCrDescriptor) {
    return {};
  }
  const YCbCrDescriptor& yuvDesc = sdb.desc().get_YCbCrDescriptor();

  Maybe<Range<uint8_t>> buffer;
  const MemoryOrShmem& memOrShmem = sdb.data();
  switch (memOrShmem.type()) {
    case MemoryOrShmem::Tuintptr_t:
      gfxCriticalError() << "PlanarYCbCrData::From SurfaceDescriptorBuffer "
                            "w/uintptr_t unsupported.";
      break;
    case MemoryOrShmem::TShmem:
      buffer.emplace(memOrShmem.get_Shmem().Range<uint8_t>());
      break;
    default:
      MOZ_ASSERT(false, "Unknown MemoryOrShmem type");
      break;
  }
  if (!buffer) {
    return {};
  }

  PlanarYCbCrData yuvData;
  yuvData.mYStride = AssertedCast<int32_t>(yuvDesc.yStride());
  yuvData.mCbCrStride = AssertedCast<int32_t>(yuvDesc.cbCrStride());
  // default mYSkip, mCbSkip, mCrSkip because not held in YCbCrDescriptor
  yuvData.mYSkip = yuvData.mCbSkip = yuvData.mCrSkip = 0;
  yuvData.mPictureRect = yuvDesc.display();
  yuvData.mStereoMode = yuvDesc.stereoMode();
  yuvData.mColorDepth = yuvDesc.colorDepth();
  yuvData.mYUVColorSpace = yuvDesc.yUVColorSpace();
  yuvData.mColorRange = yuvDesc.colorRange();
  yuvData.mChromaSubsampling = yuvDesc.chromaSubsampling();

  const auto sizes = yuvData.GetPlaneSizes();
  if (!sizes) {
    gfxCriticalError() << "Invalid PlanarYCbCrData picture rectangle: "
                       << yuvData.mPictureRect;
    return {};
  }

  const auto GetPlanePtr = [&](const uint32_t beginOffset,
                               const gfx::IntSize size,
                               const int32_t stride) -> uint8_t* {
    if (size.width > stride) return nullptr;
    auto bytesNeeded = CheckedInt<uintptr_t>(stride) *
                       size.height;  // Don't accept `stride*(h-1)+w`.
    bytesNeeded += beginOffset;
    if (!bytesNeeded.isValid() || bytesNeeded.value() > buffer->length()) {
      gfxCriticalError()
          << "PlanarYCbCrData::From asked for out-of-bounds plane data.";
      return nullptr;
    }
    return (buffer->begin() + beginOffset).get();
  };
  yuvData.mYChannel =
      GetPlanePtr(yuvDesc.yOffset(), yuvDesc.ySize(), yuvData.mYStride);
  yuvData.mCbChannel =
      GetPlanePtr(yuvDesc.cbOffset(), yuvDesc.cbCrSize(), yuvData.mCbCrStride);
  yuvData.mCrChannel =
      GetPlanePtr(yuvDesc.crOffset(), yuvDesc.cbCrSize(), yuvData.mCbCrStride);

  if (yuvData.mYSkip || yuvData.mCbSkip || yuvData.mCrSkip ||
      yuvDesc.ySize().width < 0 || yuvDesc.ySize().height < 0 ||
      yuvDesc.cbCrSize().width < 0 || yuvDesc.cbCrSize().height < 0 ||
      yuvData.mYStride < 0 || yuvData.mCbCrStride < 0 || !yuvData.mYChannel ||
      !yuvData.mCbChannel || !yuvData.mCrChannel ||
      !(sizes->mY <= yuvDesc.ySize()) ||
      !(sizes->mCbCr <= yuvDesc.cbCrSize())) {
    gfxCriticalError() << "Unusual PlanarYCbCrData: " << yuvData.mYSkip << ","
                       << yuvData.mCbSkip << "," << yuvData.mCrSkip << ", "
                       << yuvDesc.ySize().width << "," << yuvDesc.ySize().height
                       << ", " << yuvDesc.cbCrSize().width << ","
                       << yuvDesc.cbCrSize().height << ", " << yuvData.mYStride
                       << "," << yuvData.mCbCrStride << ", "
                       << yuvData.mYChannel << "," << yuvData.mCbChannel << ","
                       << yuvData.mCrChannel << "," << sizes->mY.width << ","
                       << sizes->mY.height;
    return {};
  }

  return Some(yuvData);
}

Maybe<PlanarYCbCrData> PlanarYCbCrData::From(
    const VideoData::YCbCrBuffer& yuvDesc) {
  constexpr int YPlane = 0;
  constexpr int CbPlane = 1;
  constexpr int CrPlane = 2;

  PlanarYCbCrData yuvData;
  yuvData.mYStride = yuvDesc.mPlanes[YPlane].mStride;
  yuvData.mCbCrStride = yuvDesc.mPlanes[CbPlane].mStride;
  yuvData.mYSkip = yuvDesc.mPlanes[YPlane].mSkip;
  yuvData.mCbSkip = yuvDesc.mPlanes[CbPlane].mSkip;
  yuvData.mCrSkip = yuvDesc.mPlanes[CrPlane].mSkip;
  yuvData.mPictureRect = gfx::IntRect(0, 0, yuvDesc.mPlanes[YPlane].mWidth,
                                      yuvDesc.mPlanes[YPlane].mHeight);
  yuvData.mColorDepth = yuvDesc.mColorDepth;
  yuvData.mYUVColorSpace = yuvDesc.mYUVColorSpace;
  yuvData.mColorRange = yuvDesc.mColorRange;
  yuvData.mChromaSubsampling = yuvDesc.mChromaSubsampling;
  yuvData.mYChannel = yuvDesc.mPlanes[YPlane].mData;
  yuvData.mCbChannel = yuvDesc.mPlanes[CbPlane].mData;
  yuvData.mCrChannel = yuvDesc.mPlanes[CrPlane].mData;

  if (yuvData.GetPlaneSizes().isNothing() || yuvData.mYSkip ||
      yuvData.mCbSkip || yuvData.mCrSkip || yuvData.mYStride < 0 ||
      yuvData.mCbCrStride < 0 || !yuvData.mYChannel || !yuvData.mCbChannel ||
      !yuvData.mCrChannel) {
    gfxCriticalError() << "Unusual PlanarYCbCrData: " << yuvData.mYSkip << ","
                       << yuvData.mCbSkip << "," << yuvData.mCrSkip << ","
                       << yuvData.mYStride << "," << yuvData.mCbCrStride << ", "
                       << yuvData.mYChannel << "," << yuvData.mCbChannel << ","
                       << yuvData.mCrChannel;
    return {};
  }

  return Some(yuvData);
}

// -

std::ostream& operator<<(std::ostream& aOut, const PlanarYCbCrData& aData) {
  aOut << "PlanarYCbCrData: {"
       << "mYStride=" << aData.mYStride << ", mCbCrStride=" << aData.mCbCrStride
       << ", mYSkip=" << aData.mYSkip << ", mCbSkip=" << aData.mCbSkip
       << ", mCrSkip=" << aData.mCrSkip
       << ", mPictureRect=" << aData.mPictureRect
       << ", mColorDepth=" << aData.mColorDepth
       << ", mYUVColorSpace=" << aData.mYUVColorSpace
       << ", mColorPrimaries=" << aData.mColorPrimaries
       << ", mTransferFunction=" << aData.mTransferFunction
       << ", mColorRange=" << aData.mColorRange
       << ", mChromaSubsampling=" << aData.mChromaSubsampling;
  if (const auto sizes = aData.GetPlaneSizes()) {
    aOut << ", YDataSize=" << sizes->mY << ", CbCrDataSize=" << sizes->mCbCr;
  } else {
    aOut << ", invalid plane sizes";
  }
  return aOut << "}";
}

PlanarYCbCrImage::PlanarYCbCrImage()
    : Image(nullptr, ImageFormat::PLANAR_YCBCR),
      mOffscreenFormat(SurfaceFormat::UNKNOWN),
      mBufferSize(0) {}

nsresult PlanarYCbCrImage::BuildSurfaceDescriptorBuffer(
    SurfaceDescriptorBuffer& aSdBuffer, BuildSdbFlags aFlags,
    const std::function<MemoryOrShmem(uint32_t)>& aAllocate) {
  const PlanarYCbCrData* pdata = GetData();
  MOZ_ASSERT(pdata, "must have PlanarYCbCrData");
  MOZ_ASSERT(pdata->mYSkip == 0 && pdata->mCbSkip == 0 && pdata->mCrSkip == 0,
             "YCbCrDescriptor doesn't hold skip values");

  const auto sizes = pdata->GetPlaneSizes();
  if (!sizes) {
    return NS_ERROR_ILLEGAL_VALUE;
  }

  if (aFlags & BuildSdbFlags::RgbOnly) {
    gfx::IntSize size(mSize);
    auto format = gfx::ImageFormatToSurfaceFormat(GetOffscreenFormat());
    gfx::GetYCbCrToRGBDestFormatAndSize(mData, format, size);

    uint8_t* buffer = nullptr;
    int32_t stride = 0;
    nsresult rv = AllocateSurfaceDescriptorBufferRgb(
        size, format, buffer, aSdBuffer, stride, aAllocate);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    // If we can copy directly from the surface, let's do that to avoid the YUV
    // to RGB conversion.
    RefPtr<gfx::DataSourceSurface> sourceSurface = mSourceSurface.Get();
    if (sourceSurface && sourceSurface->GetSize() == size) {
      DataSourceSurface::ScopedMap map(sourceSurface, DataSourceSurface::READ);
      if (map.IsMapped() && SwizzleData(map.GetData(), map.GetStride(),
                                        sourceSurface->GetFormat(), buffer,
                                        stride, format, size)) {
        return NS_OK;
      }
    }

    rv = gfx::ConvertYCbCrToRGB(mData, format, size, buffer, stride);
    MOZ_ASSERT(NS_SUCCEEDED(rv), "Failed to convert YUV into RGB data");
    return rv;
  }

  const auto& ySize = sizes->mY;
  const auto& cbcrSize = sizes->mCbCr;
  uint32_t yOffset;
  uint32_t cbOffset;
  uint32_t crOffset;
  ImageDataSerializer::ComputeYCbCrOffsets(pdata->mYStride, ySize.height,
                                           pdata->mCbCrStride, cbcrSize.height,
                                           yOffset, cbOffset, crOffset);

  Maybe<uint32_t> bufferSize = ImageDataSerializer::ComputeYCbCrBufferSize(
      pdata->mPictureRect, ySize, pdata->mYStride, cbcrSize, pdata->mCbCrStride,
      yOffset, cbOffset, crOffset, pdata->mColorDepth,
      pdata->mChromaSubsampling);
  if (bufferSize.isNothing()) {
    return NS_ERROR_FAILURE;
  }

  aSdBuffer.data() = aAllocate(bufferSize.value());

  uint8_t* buffer = nullptr;
  const MemoryOrShmem& memOrShmem = aSdBuffer.data();
  switch (memOrShmem.type()) {
    case MemoryOrShmem::Tuintptr_t:
      buffer = reinterpret_cast<uint8_t*>(memOrShmem.get_uintptr_t());
      break;
    case MemoryOrShmem::TShmem:
      buffer = memOrShmem.get_Shmem().get<uint8_t>();
      break;
    default:
      buffer = nullptr;
      break;
  }
  if (!buffer) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  aSdBuffer.desc() = YCbCrDescriptor(
      pdata->mPictureRect, ySize, pdata->mYStride, cbcrSize, pdata->mCbCrStride,
      yOffset, cbOffset, crOffset, pdata->mStereoMode, pdata->mColorDepth,
      pdata->mYUVColorSpace, pdata->mColorRange, pdata->mTransferFunction,
      pdata->mChromaSubsampling, pdata->mHDRMetadata);

  const int32_t bytesPerSample =
      pdata->mColorDepth == gfx::ColorDepth::COLOR_8 ? 1 : 2;

  CopyPlane(buffer + yOffset, pdata->mYChannel, ySize, pdata->mYStride,
            pdata->mYSkip, bytesPerSample);
  CopyPlane(buffer + cbOffset, pdata->mCbChannel, cbcrSize, pdata->mCbCrStride,
            pdata->mCbSkip, bytesPerSample);
  CopyPlane(buffer + crOffset, pdata->mCrChannel, cbcrSize, pdata->mCbCrStride,
            pdata->mCrSkip, bytesPerSample);
  return NS_OK;
}

RecyclingPlanarYCbCrImage::~RecyclingPlanarYCbCrImage() {
  if (mBuffer) {
    mRecycleBin->RecycleBuffer(std::move(mBuffer), mBufferSize);
  }
}

size_t RecyclingPlanarYCbCrImage::SizeOfExcludingThis(
    MallocSizeOf aMallocSizeOf) const {
  // Ignoring:
  // - mData - just wraps mBuffer
  // - Surfaces should be reported under gfx-surfaces-*:
  //   - mSourceSurface
  // - Base class:
  //   - mImplData is not used
  // Not owned:
  // - mRecycleBin
  size_t size = aMallocSizeOf(mBuffer.get());

  // Could add in the future:
  // - mBackendData (from base class)

  return size;
}

UniquePtr<uint8_t[]> RecyclingPlanarYCbCrImage::AllocateBuffer(uint32_t aSize) {
  return mRecycleBin->GetBuffer(aSize);
}

static void CopyPlane(uint8_t* aDst, const uint8_t* aSrc,
                      const gfx::IntSize& aSize, int32_t aStride, int32_t aSkip,
                      int32_t aBytesPerElement) {
  int32_t height = aSize.height;
  int32_t width = aSize.width;
  const int32_t rowBytes = width * aBytesPerElement;

  // The interleaved (aSkip != 0) path steps over skipped elements between
  // pixels, so a row reaches further than the packed width. Computed with
  // 64-bit arithmetic.
  const int64_t srcRowSpan =
      (static_cast<int64_t>(width) + static_cast<int64_t>(width - 1) * aSkip) *
      aBytesPerElement;
  MOZ_RELEASE_ASSERT(srcRowSpan <= aStride);

  if (height == 0) {
    return;
  }

  if (!aSkip) {
    // Fast path: planar input.
    memcpy(aDst, aSrc, size_t(aStride) * (height - 1) + rowBytes);
  } else {
    for (int y = 0; y < height; ++y) {
      const uint8_t* src = aSrc + size_t(y) * aStride;
      uint8_t* dst = aDst + size_t(y) * aStride;
      // Slow path
      for (int x = 0; x < width; ++x) {
        for (int b = 0; b < aBytesPerElement; ++b) {
          *dst++ = *src++;
        }
        src += aSkip * aBytesPerElement;
      }
    }
  }

  if (rowBytes < aStride) {
    // Trailing stride bytes are not pixel data; zero them so
    // VideoFrame.copyTo() does not expose stale buffer contents to JS.
    for (int y = 0; y < height; ++y) {
      memset(aDst + size_t(y) * aStride + rowBytes, 0, aStride - rowBytes);
    }
  }
}

nsresult RecyclingPlanarYCbCrImage::CopyData(const Data& aData) {
  const auto sizes = aData.GetPlaneSizes();
  if (!sizes) {
    return NS_ERROR_INVALID_ARG;
  }

  // update buffer size
  // Use uint32_t throughout to match AllocateBuffer's param and mBufferSize
  const auto& ySize = sizes->mY;
  const auto& cbcrSize = sizes->mCbCr;
  const auto checkedSize =
      CheckedInt<uint32_t>(aData.mCbCrStride) * cbcrSize.height * 2 +
      CheckedInt<uint32_t>(aData.mYStride) * ySize.height *
          (aData.mAlpha ? 2 : 1);

  if (!checkedSize.isValid()) return NS_ERROR_INVALID_ARG;

  const auto size = checkedSize.value();

  // get new buffer
  mBuffer = AllocateBuffer(size);
  if (!mBuffer) return NS_ERROR_OUT_OF_MEMORY;

  // update buffer size
  mBufferSize = size;

  mData = aData;  // mAlpha will be set if aData has it
  mData.mYChannel = mBuffer.get();
  mData.mCbChannel = mData.mYChannel + mData.mYStride * ySize.height;
  mData.mCrChannel = mData.mCbChannel + mData.mCbCrStride * cbcrSize.height;
  mData.mYSkip = mData.mCbSkip = mData.mCrSkip = 0;

  const int32_t bytesPerSample =
      aData.mColorDepth == gfx::ColorDepth::COLOR_8 ? 1 : 2;

  CopyPlane(mData.mYChannel, aData.mYChannel, ySize, aData.mYStride,
            aData.mYSkip, bytesPerSample);
  CopyPlane(mData.mCbChannel, aData.mCbChannel, cbcrSize, aData.mCbCrStride,
            aData.mCbSkip, bytesPerSample);
  CopyPlane(mData.mCrChannel, aData.mCrChannel, cbcrSize, aData.mCbCrStride,
            aData.mCrSkip, bytesPerSample);
  if (aData.mAlpha) {
    MOZ_ASSERT(mData.mAlpha);
    mData.mAlpha->mChannel =
        mData.mCrChannel + mData.mCbCrStride * cbcrSize.height;
    CopyPlane(mData.mAlpha->mChannel, aData.mAlpha->mChannel, ySize,
              aData.mYStride, aData.mYSkip,
              aData.mAlpha->mDepth == gfx::ColorDepth::COLOR_8 ? 1 : 2);
  }

  mSize = aData.mPictureRect.Size();
  mOrigin = aData.mPictureRect.TopLeft();
  return NS_OK;
}

gfxImageFormat PlanarYCbCrImage::GetOffscreenFormat() const {
  return mOffscreenFormat == SurfaceFormat::UNKNOWN ? gfxVars::OffscreenFormat()
                                                    : mOffscreenFormat;
}

Maybe<IntSize> PlanarYCbCrImage::GetAddressableSize() const {
  const auto sizes = IsValid() ? mData.GetPlaneSizes() : Nothing();
  return sizes ? Some(sizes->mY) : Nothing();
}

nsresult PlanarYCbCrImage::AdoptData(const Data& aData) {
  if (aData.GetPlaneSizes().isNothing()) {
    return NS_ERROR_INVALID_ARG;
  }
  mData = aData;
  mSize = aData.mPictureRect.Size();
  mOrigin = aData.mPictureRect.TopLeft();
  return NS_OK;
}

already_AddRefed<gfx::SourceSurface> PlanarYCbCrImage::GetAsSourceSurface() {
  if (RefPtr<gfx::DataSourceSurface> cached = mSourceSurface.Get()) {
    return cached.forget();
  }

  gfx::IntSize size(mSize);
  gfx::SurfaceFormat format =
      gfx::ImageFormatToSurfaceFormat(GetOffscreenFormat());
  gfx::GetYCbCrToRGBDestFormatAndSize(mData, format, size);
  if (mSize.width > PlanarYCbCrImage::MAX_DIMENSION ||
      mSize.height > PlanarYCbCrImage::MAX_DIMENSION) {
    NS_ERROR("Illegal image dest width or height");
    return nullptr;
  }

  RefPtr<gfx::DataSourceSurface> surface =
      gfx::Factory::CreateDataSourceSurface(size, format);
  if (NS_WARN_IF(!surface)) {
    return nullptr;
  }

  DataSourceSurface::ScopedMap mapping(surface, DataSourceSurface::WRITE);
  if (NS_WARN_IF(!mapping.IsMapped())) {
    return nullptr;
  }

  if (NS_WARN_IF(NS_FAILED(gfx::ConvertYCbCrToRGB(
          mData, format, size, mapping.GetData(), mapping.GetStride())))) {
    MOZ_ASSERT_UNREACHABLE("Failed to convert YUV into RGB data");
    return nullptr;
  }

  mSourceSurface.Set(surface);

  return surface.forget();
}

PlanarYCbCrImage::~PlanarYCbCrImage() = default;

NVImage::NVImage() : Image(nullptr, ImageFormat::NV_IMAGE), mBufferSize(0) {}

NVImage::~NVImage() = default;

IntSize NVImage::GetSize() const { return mSize; }

IntRect NVImage::GetPictureRect() const { return mData.mPictureRect; }

Maybe<IntSize> NVImage::GetAddressableSize() const {
  const auto sizes = IsValid() ? mData.GetPlaneSizes() : Nothing();
  return sizes ? Some(sizes->mY) : Nothing();
}

// Planar I420 conversion of an NV12/NV21 image: mBuffer owns the pixels, mData
// points into it.
struct I420Buffer {
  UniquePtr<uint8_t[]> mBuffer;
  PlanarYCbCrData mData;
};

// Convert |aData| (NV12 or NV21) to planar I420 in a freshly allocated buffer.
// Returns an empty buffer for invalid geometry or an overflowing buffer length.
static I420Buffer CreateI420Buffer(const PlanarYCbCrData& aData) {
  const auto sizes = aData.GetPlaneSizes();
  if (!sizes) {
    return {};
  }
  // NV12/NV21 store interleaved chroma with Cb and Cr in adjacent bytes.
  MOZ_ASSERT(aData.mCbSkip == 1 && aData.mCrSkip == 1,
             "CreateI420Buffer expects NV12 or NV21 (interleaved chroma)");
  MOZ_ASSERT((int)std::abs(aData.mCbChannel - aData.mCrChannel) == 1,
             "NV12/NV21 chroma planes must be adjacent (interleaved)");
  const auto& ySize = sizes->mY;
  const auto& cbcrSize = sizes->mCbCr;
  const CheckedInt32 bufferLength =
      CheckedInt32(ySize.height) * aData.mYStride +
      CheckedInt32(cbcrSize.height) * cbcrSize.width * 2;
  if (NS_WARN_IF(!bufferLength.isValid())) {
    return {};
  }
  I420Buffer result{std::make_unique<uint8_t[]>(bufferLength.value()), aData};

  PlanarYCbCrData& dst = result.mData;
  dst.mCbCrStride = cbcrSize.width;
  dst.mCbSkip = 0;
  dst.mCrSkip = 0;
  dst.mYChannel = result.mBuffer.get();
  dst.mCbChannel = dst.mYChannel + ySize.height * dst.mYStride;
  dst.mCrChannel = dst.mCbChannel + cbcrSize.height * dst.mCbCrStride;

  if (aData.mCbChannel < aData.mCrChannel) {  // NV12
    libyuv::NV12ToI420(aData.mYChannel, aData.mYStride, aData.mCbChannel,
                       aData.mCbCrStride, dst.mYChannel, dst.mYStride,
                       dst.mCbChannel, dst.mCbCrStride, dst.mCrChannel,
                       dst.mCbCrStride, ySize.width, ySize.height);
  } else {  // NV21
    libyuv::NV21ToI420(aData.mYChannel, aData.mYStride, aData.mCrChannel,
                       aData.mCbCrStride, dst.mYChannel, dst.mYStride,
                       dst.mCbChannel, dst.mCbCrStride, dst.mCrChannel,
                       dst.mCbCrStride, ySize.width, ySize.height);
  }
  return result;
}

already_AddRefed<SourceSurface> NVImage::GetAsSourceSurface() {
  if (RefPtr<gfx::DataSourceSurface> cached = mSourceSurface.Get()) {
    return cached.forget();
  }

  // Convert the current NV12 or NV21 data to YUV420P so that we can follow the
  // logics in PlanarYCbCrImage::GetAsSourceSurface().
  auto i420 = CreateI420Buffer(mData);
  if (!i420.mBuffer) {
    return nullptr;
  }

  // The logics in PlanarYCbCrImage::GetAsSourceSurface().
  gfx::IntSize size(mSize);
  gfx::SurfaceFormat format = gfx::ImageFormatToSurfaceFormat(
      gfxPlatform::GetPlatform()->GetOffscreenFormat());
  gfx::GetYCbCrToRGBDestFormatAndSize(i420.mData, format, size);
  if (mSize.width > PlanarYCbCrImage::MAX_DIMENSION ||
      mSize.height > PlanarYCbCrImage::MAX_DIMENSION) {
    NS_ERROR("Illegal image dest width or height");
    return nullptr;
  }

  RefPtr<gfx::DataSourceSurface> surface =
      gfx::Factory::CreateDataSourceSurface(size, format);
  if (NS_WARN_IF(!surface)) {
    return nullptr;
  }

  DataSourceSurface::ScopedMap mapping(surface, DataSourceSurface::WRITE);
  if (NS_WARN_IF(!mapping.IsMapped())) {
    return nullptr;
  }

  if (NS_WARN_IF(NS_FAILED(gfx::ConvertYCbCrToRGB(
          i420.mData, format, size, mapping.GetData(), mapping.GetStride())))) {
    return nullptr;
  }

  mSourceSurface.Set(surface);

  return surface.forget();
}

nsresult NVImage::BuildSurfaceDescriptorBuffer(
    SurfaceDescriptorBuffer& aSdBuffer, BuildSdbFlags aFlags,
    const std::function<MemoryOrShmem(uint32_t)>& aAllocate) {
  // Convert the current NV12 or NV21 data to YUV420P so that we can follow the
  // logics in PlanarYCbCrImage::GetAsSourceSurface().
  RefPtr<gfx::DataSourceSurface> sourceSurface = mSourceSurface.Get();
  I420Buffer i420;
  if (!sourceSurface) {
    i420 = CreateI420Buffer(mData);
    if (!i420.mBuffer) {
      return NS_ERROR_INVALID_ARG;
    }
  }

  // The logics in PlanarYCbCrImage::GetAsSourceSurface(). mData is used here
  // rather than i420.mData because only the chroma subsampling and picture size
  // are read, which are identical, and i420.mData is left unset on the
  // cached-surface path.
  gfx::IntSize size(mSize);
  gfx::SurfaceFormat format = gfx::ImageFormatToSurfaceFormat(
      gfxPlatform::GetPlatform()->GetOffscreenFormat());
  gfx::GetYCbCrToRGBDestFormatAndSize(mData, format, size);
  if (mSize.width > PlanarYCbCrImage::MAX_DIMENSION ||
      mSize.height > PlanarYCbCrImage::MAX_DIMENSION) {
    NS_ERROR("Illegal image dest width or height");
    return NS_ERROR_FAILURE;
  }

  if (sourceSurface && sourceSurface->GetSize() != size) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  uint8_t* output = nullptr;
  int32_t stride = 0;
  nsresult rv = AllocateSurfaceDescriptorBufferRgb(
      size, format, output, aSdBuffer, stride, aAllocate);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (!sourceSurface) {
    rv = gfx::ConvertYCbCrToRGB(i420.mData, format, size, output, stride);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      MOZ_ASSERT_UNREACHABLE("Failed to convert YUV into RGB data");
      return rv;
    }
    return NS_OK;
  }

  DataSourceSurface::ScopedMap map(sourceSurface, DataSourceSurface::READ);
  if (NS_WARN_IF(!map.IsMapped())) {
    return NS_ERROR_FAILURE;
  }

  if (!SwizzleData(map.GetData(), map.GetStride(), sourceSurface->GetFormat(),
                   output, stride, format, size)) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

bool NVImage::IsValid() const { return !!mBufferSize; }

uint32_t NVImage::GetBufferSize() const { return mBufferSize; }

NVImage* NVImage::AsNVImage() { return this; };

nsresult NVImage::SetData(const Data& aData) {
  const auto sizes = aData.GetPlaneSizes();
  if (!sizes) {
    return NS_ERROR_INVALID_ARG;
  }

  MOZ_ASSERT(aData.mCbSkip == 1 && aData.mCrSkip == 1);
  MOZ_ASSERT((int)std::abs(aData.mCbChannel - aData.mCrChannel) == 1);

  // Calculate buffer size
  // Use uint32_t throughout to match AllocateBuffer's param and mBufferSize
  const auto& ySize = sizes->mY;
  const auto& cbcrSize = sizes->mCbCr;
  const auto checkedSize =
      CheckedInt<uint32_t>(ySize.height) * aData.mYStride +
      CheckedInt<uint32_t>(cbcrSize.height) * aData.mCbCrStride;

  if (!checkedSize.isValid()) {
    return NS_ERROR_INVALID_ARG;
  }

  const auto size = checkedSize.value();

  // Allocate a new buffer.
  mBuffer = AllocateBuffer(size);
  if (!mBuffer) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // Update mBufferSize.
  mBufferSize = size;

  // Update mData.
  mData = aData;
  mData.mYChannel = mBuffer.get();
  mData.mCbChannel = mData.mYChannel + (aData.mCbChannel - aData.mYChannel);
  mData.mCrChannel = mData.mYChannel + (aData.mCrChannel - aData.mYChannel);

  // Update mSize.
  mSize = aData.mPictureRect.Size();

  // Copy the input data into mBuffer.
  // This copies the y-channel and the interleaving CbCr-channel.
  memcpy(mData.mYChannel, aData.mYChannel, mBufferSize);

  return NS_OK;
}

const NVImage::Data* NVImage::GetData() const { return &mData; }

UniquePtr<uint8_t[]> NVImage::AllocateBuffer(uint32_t aSize) {
  return std::make_unique<uint8_t[]>(aSize);
}

SourceSurfaceImage::SourceSurfaceImage(const gfx::IntSize& aSize,
                                       gfx::SourceSurface* aSourceSurface)
    : Image(nullptr, ImageFormat::MOZ2D_SURFACE),
      mSize(aSize),
      mSourceSurface(aSourceSurface),
      mTextureFlags(TextureFlags::DEFAULT) {}

SourceSurfaceImage::SourceSurfaceImage(gfx::SourceSurface* aSourceSurface)
    : Image(nullptr, ImageFormat::MOZ2D_SURFACE),
      mSize(aSourceSurface->GetSize()),
      mSourceSurface(aSourceSurface),
      mTextureFlags(TextureFlags::DEFAULT) {}

SourceSurfaceImage::~SourceSurfaceImage() {
  NS_ReleaseOnMainThread("SourceSurfaceImage::mSourceSurface",
                         mSourceSurface.forget());
}

TextureClient* SourceSurfaceImage::GetTextureClient(
    KnowsCompositor* aKnowsCompositor) {
  if (!aKnowsCompositor) {
    return nullptr;
  }

  return mTextureClients.WithEntryHandle(
      aKnowsCompositor->GetSerial(), [&](auto&& entry) -> TextureClient* {
        if (entry) {
          return entry->get();
        }

        RefPtr<TextureClient> textureClient;
        RefPtr<SourceSurface> surface = GetAsSourceSurface();
        MOZ_ASSERT(surface);
        if (surface) {
          // gfx::BackendType::NONE means default to content backend
          textureClient = TextureClient::CreateFromSurface(
              aKnowsCompositor, surface, BackendSelector::Content,
              mTextureFlags, ALLOC_DEFAULT);
        }
        if (textureClient) {
          textureClient->SyncWithObject(aKnowsCompositor->GetSyncObject());
          return entry.Insert(std::move(textureClient)).get();
        }

        return nullptr;
      });
}

ImageContainer::ProducerID ImageContainer::AllocateProducerID() {
  // Callable on all threads.
  static Atomic<ImageContainer::ProducerID> sProducerID(0u);
  return ++sProducerID;
}

}  // namespace mozilla::layers
