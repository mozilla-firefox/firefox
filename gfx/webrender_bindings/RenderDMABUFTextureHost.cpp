/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderDMABUFTextureHost.h"

#include "GLContextEGL.h"
#include "mozilla/gfx/Logging.h"
#include "ScopedGLHelpers.h"

namespace mozilla::wr {

RenderDMABUFTextureHost::RenderDMABUFTextureHost(DMABufSurface* aSurface)
    : mSurface(aSurface) {
  MOZ_COUNT_CTOR_INHERITED(RenderDMABUFTextureHost, RenderTextureHost);
}

RenderDMABUFTextureHost::~RenderDMABUFTextureHost() {
  MOZ_COUNT_DTOR_INHERITED(RenderDMABUFTextureHost, RenderTextureHost);
  DeleteTextureHandle();
}

wr::WrExternalImage RenderDMABUFTextureHost::Lock(uint8_t aChannelIndex,
                                                  gl::GLContext* aGL) {
  const gfx::IntSize size(mSurface->GetWidth(aChannelIndex),
                          mSurface->GetHeight(aChannelIndex));

  // Wayland native compositor doesn't use textures so pass zero
  // there. It saves GPU resources.
  if (!aGL) {
    return NativeTextureToWrExternalImage(0, 0.0, 0.0,
                                          static_cast<float>(size.width),
                                          static_cast<float>(size.height));
  }

  if (mGL.get() != aGL) {
    if (mGL) {
      // This should not happen. EGLImage is created only in
      // parent process.
      MOZ_ASSERT_UNREACHABLE("Unexpected GL context");
      return InvalidToWrExternalImage();
    }
    mGL = aGL;
  }

  if (!mGL || !mGL->MakeCurrent()) {
    return InvalidToWrExternalImage();
  }

  if (!mSurface->GetTexture(aChannelIndex)) {
    if (!mSurface->CreateTexture(mGL, aChannelIndex)) {
      return InvalidToWrExternalImage();
    }
    ActivateBindAndTexParameteri(mGL, LOCAL_GL_TEXTURE0, LOCAL_GL_TEXTURE_2D,
                                 mSurface->GetTexture(aChannelIndex));
  }

  if (auto texture = mSurface->GetTexture(aChannelIndex)) {
    mSurface->MaybeSemaphoreWait(texture);
  }

  return NativeTextureToWrExternalImage(
      mSurface->GetTexture(aChannelIndex), 0.0, 0.0,
      static_cast<float>(size.width), static_cast<float>(size.height));
}

gfx::IntSize RenderDMABUFTextureHost::GetSize(uint8_t aChannelIndex) const {
  MOZ_ASSERT(mSurface);
  MOZ_ASSERT((mSurface->GetTextureCount() == 0)
                 ? (aChannelIndex == mSurface->GetTextureCount())
                 : (aChannelIndex < mSurface->GetTextureCount()));

  if (!mSurface) {
    return gfx::IntSize();
  }
  return gfx::IntSize(mSurface->GetWidth(aChannelIndex),
                      mSurface->GetHeight(aChannelIndex));
}

void RenderDMABUFTextureHost::Unlock() {}

void RenderDMABUFTextureHost::DeleteTextureHandle() {
  mSurface->ReleaseTextures();
}

void RenderDMABUFTextureHost::ClearCachedResources() {
  DeleteTextureHandle();
  mGL = nullptr;
}

bool RenderDMABUFTextureHost::MapPlane(RenderCompositor* aCompositor,
                                       uint8_t aChannelIndex,
                                       PlaneInfo& aPlaneInfo) {
  if (mSurface->GetAsDMABufSurfaceYUV()) {
    // DMABufSurfaceYUV is not supported.
    return false;
  }

  const RefPtr<gfx::SourceSurface> surface = mSurface->GetAsSourceSurface();
  if (!surface) {
    return false;
  }

  const RefPtr<gfx::DataSourceSurface> dataSurface = surface->GetDataSurface();
  if (!dataSurface) {
    return false;
  }

  gfx::DataSourceSurface::MappedSurface map;
  if (!dataSurface->Map(gfx::DataSourceSurface::MapType::READ, &map)) {
    return false;
  }

  mReadback = dataSurface;
  aPlaneInfo.mSize = gfx::IntSize(mSurface->GetWidth(), mSurface->GetHeight());
  aPlaneInfo.mStride = map.mStride;
  aPlaneInfo.mData = map.mData;

  return true;
}

void RenderDMABUFTextureHost::UnmapPlanes() {
  if (mReadback) {
    mReadback->Unmap();
    mReadback = nullptr;
  }
}

}  // namespace mozilla::wr
