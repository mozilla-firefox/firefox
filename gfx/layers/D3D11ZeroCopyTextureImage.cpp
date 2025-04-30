/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <d3d11.h>
#include <memory>
#include <mfobjects.h>

#include "D3D11ZeroCopyTextureImage.h"
#include "D3D11TextureWrapper.h"
#include "WMF.h"
#include "mozilla/gfx/SourceSurfaceRawData.h"
#include "mozilla/layers/FenceD3D11.h"
#include "mozilla/layers/KnowsCompositor.h"
#include "mozilla/layers/TextureForwarder.h"

namespace mozilla {
namespace layers {

using namespace gfx;

/* static */
RefPtr<IMFSampleWrapper> IMFSampleWrapper::Create(IMFSample* aVideoSample) {
  RefPtr<IMFSampleWrapper> wrapper = new IMFSampleWrapper(aVideoSample);
  return wrapper;
}

IMFSampleWrapper::IMFSampleWrapper(IMFSample* aVideoSample)
    : mVideoSample(aVideoSample) {}

IMFSampleWrapper::~IMFSampleWrapper() {}

void IMFSampleWrapper::ClearVideoSample() { mVideoSample = nullptr; }

D3D11ZeroCopyTextureImage::D3D11ZeroCopyTextureImage(
    ID3D11Texture2D* aTexture, uint32_t aArrayIndex, const gfx::IntSize& aSize,
    const gfx::IntRect& aRect, gfx::ColorSpace2 aColorSpace,
    gfx::ColorRange aColorRange, gfx::ColorDepth aColorDepth)
    : Image(nullptr, ImageFormat::D3D11_TEXTURE_ZERO_COPY),
      mTexture(aTexture),
      mArrayIndex(aArrayIndex),
      mSize(aSize),
      mPictureRect(aRect),
      mColorSpace(aColorSpace),
      mColorRange(aColorRange),
      mColorDepth(aColorDepth) {
  MOZ_ASSERT(XRE_IsGPUProcess());
}

D3D11ZeroCopyTextureImage::~D3D11ZeroCopyTextureImage() {
  // XXX add shutdown check for fence?
}

void D3D11ZeroCopyTextureImage::AllocateTextureClient(
    KnowsCompositor* aKnowsCompositor, RefPtr<ZeroCopyUsageInfo> aUsageInfo,
    const RefPtr<FenceD3D11> aWriteFence) {
  if (aWriteFence) {
    aWriteFence->IncrementAndSignal();
  }
  mTextureClient = D3D11TextureData::CreateTextureClient(
      mTexture, mArrayIndex, mSize, gfx::SurfaceFormat::NV12, mColorSpace,
      mColorRange, aKnowsCompositor, aUsageInfo, aWriteFence);
  MOZ_ASSERT(mTextureClient);
}

gfx::IntSize D3D11ZeroCopyTextureImage::GetSize() const { return mSize; }

TextureClient* D3D11ZeroCopyTextureImage::GetTextureClient(
    KnowsCompositor* aKnowsCompositor) {
  return mTextureClient;
}

already_AddRefed<gfx::SourceSurface>
D3D11ZeroCopyTextureImage::GetAsSourceSurface() {
  RefPtr<ID3D11Texture2D> src = GetTexture();
  if (!src) {
    gfxWarning() << "Cannot readback from shared texture because no texture is "
                    "available.";
    return nullptr;
  }

  RefPtr<gfx::SourceSurface> sourceSurface =
      gfx::Factory::CreateBGRA8DataSourceSurfaceForD3D11Texture(
          src, mArrayIndex, mColorSpace, mColorRange);

  // There is a case that mSize and size of mTexture are different. In this
  // case, size of sourceSurface is different from mSize.
  if (sourceSurface && sourceSurface->GetSize() != mSize) {
    MOZ_RELEASE_ASSERT(sourceSurface->GetType() == SurfaceType::DATA_ALIGNED);
    RefPtr<gfx::SourceSurfaceAlignedRawData> rawData =
        static_cast<gfx::SourceSurfaceAlignedRawData*>(sourceSurface.get());
    auto data = rawData->GetData();
    auto stride = rawData->Stride();
    auto size = rawData->GetSize();
    auto format = rawData->GetFormat();
    sourceSurface = gfx::Factory::CreateWrappingDataSourceSurface(
        data, stride, Min(size, mSize), format,
        [](void* aClosure) {
          RefPtr<SourceSurfaceAlignedRawData> surface =
              dont_AddRef(static_cast<SourceSurfaceAlignedRawData*>(aClosure));
        },
        rawData.forget().take());
  }
  return sourceSurface.forget();
}

nsresult D3D11ZeroCopyTextureImage::BuildSurfaceDescriptorBuffer(
    SurfaceDescriptorBuffer& aSdBuffer, BuildSdbFlags aFlags,
    const std::function<MemoryOrShmem(uint32_t)>& aAllocate) {
  RefPtr<ID3D11Texture2D> src = GetTexture();
  if (!src) {
    gfxWarning() << "Cannot readback from shared texture because no texture is "
                    "available.";
    return NS_ERROR_FAILURE;
  }

  return gfx::Factory::CreateSdbForD3D11Texture(src, mSize, aSdBuffer,
                                                aAllocate);
}

ID3D11Texture2D* D3D11ZeroCopyTextureImage::GetTexture() const {
  return mTexture;
}

D3D11TextureIMFSampleImage::D3D11TextureIMFSampleImage(
    IMFSample* aVideoSample, ID3D11Texture2D* aTexture, uint32_t aArrayIndex,
    const gfx::IntSize& aSize, const gfx::IntRect& aRect,
    gfx::ColorSpace2 aColorSpace, gfx::ColorRange aColorRange,
    gfx::ColorDepth aColorDepth)
    : D3D11ZeroCopyTextureImage(aTexture, aArrayIndex, aSize, aRect,
                                aColorSpace, aColorRange, aColorDepth),
      mVideoSample(IMFSampleWrapper::Create(aVideoSample)) {
  MOZ_ASSERT(XRE_IsGPUProcess());
}

RefPtr<IMFSampleWrapper> D3D11TextureIMFSampleImage::GetIMFSampleWrapper() {
  return mVideoSample;
}

D3D11TextureAVFrameImage::D3D11TextureAVFrameImage(
    D3D11TextureWrapper* aWrapper, const gfx::IntSize& aSize,
    const gfx::IntRect& aRect, gfx::ColorSpace2 aColorSpace,
    gfx::ColorRange aColorRange, gfx::ColorDepth aColorDepth)
    : D3D11ZeroCopyTextureImage(aWrapper->GetTexture(), aWrapper->GetArrayIdx(),
                                aSize, aRect, aColorSpace, aColorRange,
                                aColorDepth),
      mWrapper(aWrapper) {
  MOZ_ASSERT(XRE_IsGPUProcess());
}

}  // namespace layers
}  // namespace mozilla
