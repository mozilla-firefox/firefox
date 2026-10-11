/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageWrapper.h"

#include "Orientation.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/image/Resolution.h"

namespace mozilla {

using dom::Document;
using gfx::IntSize;
using gfx::SamplingFilter;
using gfx::SourceSurface;
using layers::ImageContainer;

namespace image {

// Inherited methods from Image.

already_AddRefed<ProgressTracker> ImageWrapper::GetProgressTracker() {
  return mInnerImage->GetProgressTracker();
}

size_t ImageWrapper::SizeOfSourceWithComputedFallback(
    SizeOfState& aState) const {
  return mInnerImage->SizeOfSourceWithComputedFallback(aState);
}

void ImageWrapper::CollectSizeOfSurfaces(
    nsTArray<SurfaceMemoryCounter>& aCounters,
    MallocSizeOf aMallocSizeOf) const {
  mInnerImage->CollectSizeOfSurfaces(aCounters, aMallocSizeOf);
}

void ImageWrapper::IncrementAnimationConsumers() {
  MOZ_ASSERT(NS_IsMainThread(),
             "Main thread only to encourage serialization "
             "with DecrementAnimationConsumers");
  mInnerImage->IncrementAnimationConsumers();
}

void ImageWrapper::DecrementAnimationConsumers() {
  MOZ_ASSERT(NS_IsMainThread(),
             "Main thread only to encourage serialization "
             "with IncrementAnimationConsumers");
  mInnerImage->DecrementAnimationConsumers();
}

#ifdef DEBUG
uint32_t ImageWrapper::GetAnimationConsumers() {
  return mInnerImage->GetAnimationConsumers();
}
#endif

nsresult ImageWrapper::OnImageDataAvailable(nsIRequest* aRequest,
                                            nsIInputStream* aInStr,
                                            uint64_t aSourceOffset,
                                            uint32_t aCount) {
  return mInnerImage->OnImageDataAvailable(aRequest, aInStr, aSourceOffset,
                                           aCount);
}

nsresult ImageWrapper::OnImageDataComplete(nsIRequest* aRequest,
                                           nsresult aStatus, bool aLastPart) {
  return mInnerImage->OnImageDataComplete(aRequest, aStatus, aLastPart);
}

void ImageWrapper::OnSurfaceDiscarded(const SurfaceKey& aSurfaceKey) {
  return mInnerImage->OnSurfaceDiscarded(aSurfaceKey);
}

void ImageWrapper::SetInnerWindowID(uint64_t aInnerWindowId) {
  mInnerImage->SetInnerWindowID(aInnerWindowId);
}

uint64_t ImageWrapper::InnerWindowID() const {
  return mInnerImage->InnerWindowID();
}

bool ImageWrapper::HasError() { return mInnerImage->HasError(); }

void ImageWrapper::SetHasError() { mInnerImage->SetHasError(); }

nsIURI* ImageWrapper::GetURI() const { return mInnerImage->GetURI(); }

// Methods inherited from XPCOM interfaces.

NS_IMPL_ISUPPORTS(ImageWrapper, imgIContainer)

NS_IMETHODIMP
ImageWrapper::GetWidth(int32_t* aWidth) {
  return mInnerImage->GetWidth(aWidth);
}

NS_IMETHODIMP
ImageWrapper::GetHeight(int32_t* aHeight) {
  return mInnerImage->GetHeight(aHeight);
}

void ImageWrapper::MediaFeatureValuesChangedAllDocuments(
    const mozilla::MediaFeatureChange& aChange) {}

nsresult ImageWrapper::GetNativeSizes(nsTArray<IntSize>& aNativeSizes) {
  return mInnerImage->GetNativeSizes(aNativeSizes);
}

size_t ImageWrapper::GetNativeSizesLength() {
  return mInnerImage->GetNativeSizesLength();
}

NS_IMETHODIMP
ImageWrapper::GetIntrinsicSize(ImageIntrinsicSize* aIntrinsicSize) {
  return mInnerImage->GetIntrinsicSize(aIntrinsicSize);
}

NS_IMETHODIMP
ImageWrapper::GetIntrinsicSizeInAppUnits(nsSize* aSize) {
  return mInnerImage->GetIntrinsicSizeInAppUnits(aSize);
}

AspectRatio ImageWrapper::GetIntrinsicRatio() {
  return mInnerImage->GetIntrinsicRatio();
}

nsresult ImageWrapper::GetHotspotX(int32_t* aX) {
  return Image::GetHotspotX(aX);
}

nsresult ImageWrapper::GetHotspotY(int32_t* aY) {
  return Image::GetHotspotY(aY);
}

NS_IMETHODIMP_(Orientation)
ImageWrapper::GetOrientation() { return mInnerImage->GetOrientation(); }

NS_IMETHODIMP_(Resolution)
ImageWrapper::GetResolution() { return mInnerImage->GetResolution(); }

NS_IMETHODIMP
ImageWrapper::GetType(uint16_t* aType) { return mInnerImage->GetType(aType); }

NS_IMETHODIMP
ImageWrapper::GetProviderId(uint32_t* aId) {
  return mInnerImage->GetProviderId(aId);
}

NS_IMETHODIMP
ImageWrapper::GetAnimated(bool* aAnimated) {
  return mInnerImage->GetAnimated(aAnimated);
}

NS_IMETHODIMP_(already_AddRefed<SourceSurface>)
ImageWrapper::GetFrame(uint32_t aWhichFrame, uint32_t aFlags) {
  RefPtr<Image> inner = mInnerImage;
  return inner->GetFrame(aWhichFrame, aFlags);
}

NS_IMETHODIMP_(already_AddRefed<SourceSurface>)
ImageWrapper::GetFrameAtSize(const IntSize& aSize, uint32_t aWhichFrame,
                             uint32_t aFlags) {
  RefPtr<Image> inner = mInnerImage;
  return inner->GetFrameAtSize(aSize, aWhichFrame, aFlags);
}

NS_IMETHODIMP_(bool)
ImageWrapper::WillDrawOpaqueNow() { return mInnerImage->WillDrawOpaqueNow(); }

NS_IMETHODIMP_(bool)
ImageWrapper::IsImageContainerAvailable(WindowRenderer* aRenderer,
                                        uint32_t aFlags) {
  return mInnerImage->IsImageContainerAvailable(aRenderer, aFlags);
}

NS_IMETHODIMP_(ImgDrawResult)
ImageWrapper::GetImageProvider(WindowRenderer* aRenderer,
                               const gfx::IntSize& aSize,
                               const SVGImageContext& aSVGContext,
                               const Maybe<ImageIntRegion>& aRegion,
                               uint32_t aFlags,
                               WebRenderImageProvider** aProvider) {
  RefPtr<Image> inner = mInnerImage;
  return inner->GetImageProvider(aRenderer, aSize, aSVGContext, aRegion, aFlags,
                                 aProvider);
}

NS_IMETHODIMP_(ImgDrawResult)
ImageWrapper::Draw(gfxContext* aContext, const nsIntSize& aSize,
                   const ImageRegion& aRegion, uint32_t aWhichFrame,
                   SamplingFilter aSamplingFilter,
                   const SVGImageContext& aSVGContext, uint32_t aFlags,
                   float aOpacity) {
  RefPtr<Image> inner = mInnerImage;
  return inner->Draw(aContext, aSize, aRegion, aWhichFrame, aSamplingFilter,
                     aSVGContext, aFlags, aOpacity);
}

NS_IMETHODIMP
ImageWrapper::StartDecoding(uint32_t aFlags, uint32_t aWhichFrame) {
  RefPtr<Image> inner = mInnerImage;
  return inner->StartDecoding(aFlags, aWhichFrame);
}

bool ImageWrapper::StartDecodingWithResult(uint32_t aFlags,
                                           uint32_t aWhichFrame) {
  RefPtr<Image> inner = mInnerImage;
  return inner->StartDecodingWithResult(aFlags, aWhichFrame);
}

bool ImageWrapper::HasDecodedPixels() {
  return InnerImage()->HasDecodedPixels();
}

imgIContainer::DecodeResult ImageWrapper::RequestDecodeWithResult(
    uint32_t aFlags, uint32_t aWhichFrame) {
  RefPtr<Image> inner = mInnerImage;
  return inner->RequestDecodeWithResult(aFlags, aWhichFrame);
}

NS_IMETHODIMP
ImageWrapper::RequestDecodeForSize(const nsIntSize& aSize, uint32_t aFlags,
                                   uint32_t aWhichFrame) {
  RefPtr<Image> inner = mInnerImage;
  return inner->RequestDecodeForSize(aSize, aFlags, aWhichFrame);
}

NS_IMETHODIMP
ImageWrapper::LockImage() {
  MOZ_ASSERT(NS_IsMainThread(),
             "Main thread to encourage serialization with UnlockImage");
  return mInnerImage->LockImage();
}

NS_IMETHODIMP
ImageWrapper::UnlockImage() {
  MOZ_ASSERT(NS_IsMainThread(),
             "Main thread to encourage serialization with LockImage");
  return mInnerImage->UnlockImage();
}

NS_IMETHODIMP
ImageWrapper::RequestDiscard() {
  RefPtr<Image> inner = mInnerImage;
  return inner->RequestDiscard();
}

NS_IMETHODIMP_(void)
ImageWrapper::RequestRefresh(const TimeStamp& aTime) {
  RefPtr<Image> inner = mInnerImage;
  return inner->RequestRefresh(aTime);
}

NS_IMETHODIMP
ImageWrapper::GetAnimationMode(uint16_t* aAnimationMode) {
  return mInnerImage->GetAnimationMode(aAnimationMode);
}

NS_IMETHODIMP
ImageWrapper::SetAnimationMode(uint16_t aAnimationMode) {
  return mInnerImage->SetAnimationMode(aAnimationMode);
}

NS_IMETHODIMP
ImageWrapper::ResetAnimation() {
  RefPtr<Image> inner = mInnerImage;
  return inner->ResetAnimation();
}

NS_IMETHODIMP_(float)
ImageWrapper::GetFrameIndex(uint32_t aWhichFrame) {
  return mInnerImage->GetFrameIndex(aWhichFrame);
}

NS_IMETHODIMP_(int32_t)
ImageWrapper::GetFirstFrameDelay() { return mInnerImage->GetFirstFrameDelay(); }

NS_IMETHODIMP_(void)
ImageWrapper::SetAnimationStartTime(const TimeStamp& aTime) {
  mInnerImage->SetAnimationStartTime(aTime);
}

void ImageWrapper::PropagateUseCounters(Document* aReferencingDocument) {
  mInnerImage->PropagateUseCounters(aReferencingDocument);
}

nsIntSize ImageWrapper::OptimalImageSizeForDest(const gfxSize& aDest,
                                                uint32_t aWhichFrame,
                                                SamplingFilter aSamplingFilter,
                                                uint32_t aFlags) {
  return mInnerImage->OptimalImageSizeForDest(aDest, aWhichFrame,
                                              aSamplingFilter, aFlags);
}

NS_IMETHODIMP_(nsIntRect)
ImageWrapper::GetImageSpaceInvalidationRect(const nsIntRect& aRect) {
  return mInnerImage->GetImageSpaceInvalidationRect(aRect);
}

already_AddRefed<imgIContainer> ImageWrapper::Unwrap() {
  return mInnerImage->Unwrap();
}

}  // namespace image
}  // namespace mozilla
