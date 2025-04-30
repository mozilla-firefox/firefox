/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_RENDERDMABUFTEXTUREHOST_H
#define MOZILLA_GFX_RENDERDMABUFTEXTUREHOST_H

#include "mozilla/layers/TextureHostOGL.h"
#include "RenderTextureHost.h"
#include "RenderTextureHostSWGL.h"
#include "mozilla/widget/DMABufSurface.h"

namespace mozilla {

namespace layers {
class SurfaceDescriptorDMABuf;
}

namespace wr {

class RenderDMABUFTextureHost final : public RenderTextureHostSWGL {
 public:
  explicit RenderDMABUFTextureHost(DMABufSurface* aSurface);

  RenderDMABUFTextureHost* AsRenderDMABUFTextureHost() override { return this; }
  gfx::IntSize GetSize(uint8_t aChannelIndex) const;
  wr::WrExternalImage Lock(uint8_t aChannelIndex, gl::GLContext* aGL) override;
  void Unlock() override;
  void ClearCachedResources() override;

  size_t Bytes() override {
    return mSurface->GetWidth() * mSurface->GetHeight() *
           BytesPerPixel(mSurface->GetFormat());
  }
  RefPtr<DMABufSurface> GetSurface() { return mSurface; }

  // RenderTextureHostSWGL
  gfx::SurfaceFormat GetFormat() const override {
    return mSurface->GetFormat();
  }
  gfx::ColorDepth GetColorDepth() const override {
    return gfx::ColorDepth::COLOR_8;
  }
  size_t GetPlaneCount() const override { return mSurface->GetTextureCount(); };
  bool MapPlane(RenderCompositor* aCompositor, uint8_t aChannelIndex,
                PlaneInfo& aPlaneInfo) override;
  void UnmapPlanes() override;

 private:
  virtual ~RenderDMABUFTextureHost();
  void DeleteTextureHandle();

  RefPtr<DMABufSurface> mSurface;
  RefPtr<gl::GLContext> mGL;
  RefPtr<gfx::DataSourceSurface> mReadback;
};

}  // namespace wr
}  // namespace mozilla

#endif  // MOZILLA_GFX_RENDERDMABUFTEXTUREHOST_H
