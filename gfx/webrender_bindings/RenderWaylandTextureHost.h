/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_RENDERWAYLANDTEXTUREHOST_H
#define MOZILLA_GFX_RENDERWAYLANDTEXTUREHOST_H

#include "RenderTextureHostSWGL.h"
#include "mozilla/layers/TextureHostOGL.h"
#include "mozilla/widget/BufferSurface.h"

class BufferSurface;

namespace mozilla {
namespace wr {

/**
 * RenderWaylandTextureHost manages external SHM textures used by WebRender on
 * Wayland, used for decoded video frames.
 */
class RenderWaylandTextureHost final : public RenderTextureHostSWGL {
 public:
  RenderWaylandTextureHost(uint8_t* aBuffer,
                           const layers::BufferDescriptor& aDescriptor);

  RenderWaylandTextureHost* AsRenderWaylandTextureHost() override {
    return this;
  }

  wr::WrExternalImage Lock(uint8_t aChannelIndex, gl::GLContext* aGL) override;
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

 private:
  ~RenderWaylandTextureHost();
  void DeleteTextureHandle();

  layers::BufferDescriptor mDescriptor;
  RefPtr<BufferSurface> mSurface;
  RefPtr<gl::GLContext> mGL;
};

}  // namespace wr
}  // namespace mozilla

#endif  // MOZILLA_GFX_RENDERWAYLANDTEXTUREHOST_H
