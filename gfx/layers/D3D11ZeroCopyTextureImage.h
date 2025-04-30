/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_D311_TEXTURE_IMF_SAMPLE_IMAGE_H
#define GFX_D311_TEXTURE_IMF_SAMPLE_IMAGE_H

#include "ImageContainer.h"
#include "mozilla/RefPtr.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/layers/TextureClient.h"
#include "mozilla/layers/TextureD3D11.h"
#include "mozilla/ThreadSafeWeakPtr.h"

struct ID3D11Texture2D;
struct IMFSample;

namespace mozilla {

class D3D11TextureWrapper;

namespace gl {
class GLBlitHelper;
}
namespace layers {

class FenceD3D11;

class ZeroCopyUsageInfo final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ZeroCopyUsageInfo)

  ZeroCopyUsageInfo() = default;

  bool SupportsZeroCopyNV12Texture() { return mSupportsZeroCopyNV12Texture; }
  void DisableZeroCopyNV12Texture() { mSupportsZeroCopyNV12Texture = false; }

 protected:
  ~ZeroCopyUsageInfo() = default;

  Atomic<bool> mSupportsZeroCopyNV12Texture{true};
};

// A shared ID3D11Texture2D created by the compositor device.
// Expected to be used in GPU process.
class D3D11ZeroCopyTextureImage : public Image {
 public:
  D3D11ZeroCopyTextureImage(ID3D11Texture2D* aTexture, uint32_t aArrayIndex,
                            const gfx::IntSize& aSize,
                            const gfx::IntRect& aRect,
                            gfx::ColorSpace2 aColorSpace,
                            gfx::ColorRange aColorRange,
                            gfx::ColorDepth aColorDepth);
  virtual ~D3D11ZeroCopyTextureImage();

  void AllocateTextureClient(KnowsCompositor* aKnowsCompositor,
                             RefPtr<ZeroCopyUsageInfo> aUsageInfo,
                             const RefPtr<FenceD3D11> aWriteFence);

  gfx::IntSize GetSize() const override;
  already_AddRefed<gfx::SourceSurface> GetAsSourceSurface() override;
  nsresult BuildSurfaceDescriptorBuffer(
      SurfaceDescriptorBuffer& aSdBuffer, BuildSdbFlags aFlags,
      const std::function<MemoryOrShmem(uint32_t)>& aAllocate) override;
  TextureClient* GetTextureClient(KnowsCompositor* aKnowsCompositor) override;
  gfx::IntRect GetPictureRect() const override { return mPictureRect; }

  ID3D11Texture2D* GetTexture() const;

  gfx::ColorRange GetColorRange() const { return mColorRange; }

  gfx::ColorDepth GetColorDepth() const override { return mColorDepth; }

 protected:
  friend class gl::GLBlitHelper;
  D3D11TextureData* GetData() const {
    if (!mTextureClient) {
      return nullptr;
    }
    return mTextureClient->GetInternalData()->AsD3D11TextureData();
  }

  RefPtr<ID3D11Texture2D> mTexture;
  RefPtr<TextureClient> mTextureClient;

 public:
  const uint32_t mArrayIndex;
  const gfx::IntSize mSize;
  const gfx::IntRect mPictureRect;
  const gfx::ColorSpace2 mColorSpace;
  const gfx::ColorRange mColorRange;
  const gfx::ColorDepth mColorDepth;
};

class IMFSampleWrapper : public SupportsThreadSafeWeakPtr<IMFSampleWrapper> {
 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(IMFSampleWrapper)

  static RefPtr<IMFSampleWrapper> Create(IMFSample* aVideoSample);
  virtual ~IMFSampleWrapper();
  void ClearVideoSample();

 protected:
  explicit IMFSampleWrapper(IMFSample* aVideoSample);

  RefPtr<IMFSample> mVideoSample;
};

// Image class that wraps ID3D11Texture2D of IMFSample
// Expected to be used in GPU process.
class D3D11TextureIMFSampleImage final : public D3D11ZeroCopyTextureImage {
 public:
  D3D11TextureIMFSampleImage(IMFSample* aVideoSample, ID3D11Texture2D* aTexture,
                             uint32_t aArrayIndex, const gfx::IntSize& aSize,
                             const gfx::IntRect& aRect,
                             gfx::ColorSpace2 aColorSpace,
                             gfx::ColorRange aColorRange,
                             gfx::ColorDepth aColorDepth);
  virtual ~D3D11TextureIMFSampleImage() = default;

  RefPtr<IMFSampleWrapper> GetIMFSampleWrapper();

 private:
  RefPtr<IMFSampleWrapper> mVideoSample;
};

// Image class that wraps ID3D11Texture2D of AVFrame from ffvpx hw decoder
// Expected to be used in GPU process.
class D3D11TextureAVFrameImage final : public D3D11ZeroCopyTextureImage {
 public:
  D3D11TextureAVFrameImage(D3D11TextureWrapper* aWrapper,
                           const gfx::IntSize& aSize, const gfx::IntRect& aRect,
                           gfx::ColorSpace2 aColorSpace,
                           gfx::ColorRange aColorRange,
                           gfx::ColorDepth aColorDepth);
  virtual ~D3D11TextureAVFrameImage() = default;

 private:
  UniquePtr<D3D11TextureWrapper> mWrapper;
};

}  // namespace layers
}  // namespace mozilla

#endif  // GFX_D311_TEXTURE_IMF_SAMPLE_IMAGE_H
