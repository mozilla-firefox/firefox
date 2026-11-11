/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_RENDERLINUXTEXTUREHOST_H
#define MOZILLA_GFX_RENDERLINUXTEXTUREHOST_H

#include "RenderTextureHost.h"
#include "RenderTextureHostSWGL.h"
#include "mozilla/layers/TextureHostOGL.h"
#include "mozilla/widget/BufferSurface.h"

namespace mozilla {

namespace layers {
class SurfaceDescriptorDMABuf;
}

namespace wr {

class RenderLinuxTextureHost final : public RenderTextureHostSWGL {
 public:
  explicit RenderLinuxTextureHost(BufferSurface* aSurface);
  RenderLinuxTextureHost(uint8_t* aBuffer,
                         const layers::BufferDescriptor& aDescriptor);

  RenderLinuxTextureHost* AsRenderLinuxTextureHost() override { return this; }

  gfx::IntSize GetSize(uint8_t aChannelIndex) const;
  wr::WrExternalImage Lock(uint8_t aChannelIndex, gl::GLContext* aGL) override;
  void Unlock() override;
  void ClearCachedResources() override;

  size_t Bytes() override {
    return mSurface->GetWidth() * mSurface->GetHeight() *
           BytesPerPixel(mSurface->GetFormat());
  }
  RefPtr<BufferSurface> GetSurface() { return mSurface; }

  // RenderTextureHostSWGL
  gfx::SurfaceFormat GetFormat() const override {
    return mSurface->GetFormat();
  }
  gfx::ColorDepth GetColorDepth() const override {
    return mSurface->GetColorDepth();
  }
  size_t GetPlaneCount() const override { return mSurface->GetTextureCount(); };
  bool MapPlane(RenderCompositor* aCompositor, uint8_t aChannelIndex,
                PlaneInfo& aPlaneInfo) override;
  void UnmapPlanes() override;

  void PrepareForUse() override;

  bool SupportsDirectComposition() {
    return mSurface->SupportsDirectComposition();
  }

 private:
  virtual ~RenderLinuxTextureHost();
  void DeleteTextureHandle();

  RefPtr<BufferSurface> mSurface;
  RefPtr<gl::GLContext> mGL;
  RefPtr<gfx::DataSourceSurface> mReadback;
};

}  // namespace wr
}  // namespace mozilla

#endif  // MOZILLA_GFX_RENDERLINUXTEXTUREHOST_H
