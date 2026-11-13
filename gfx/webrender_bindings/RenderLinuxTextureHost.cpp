/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderLinuxTextureHost.h"

#include "GLContextEGL.h"
#include "ScopedGLHelpers.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/widget/DMABufSurface.h"
#include "mozilla/widget/SHMBufSurface.h"

namespace mozilla::wr {

// Derived from
RenderLinuxTextureHost::RenderLinuxTextureHost(BufferSurface* aSurface)
    : mSurface(aSurface) {
  if (!aSurface->GetAsDMABufSurface()) {
    NS_WARNING("Direct import is used for DMABufSurfaces only!");
    mSurface = nullptr;
  }
  MOZ_COUNT_CTOR_INHERITED(RenderLinuxTextureHost, RenderTextureHost);
}

RenderLinuxTextureHost::RenderLinuxTextureHost(
    uint8_t* aBuffer, const layers::BufferDescriptor& aDescriptor) {
  MOZ_COUNT_CTOR_INHERITED(RenderLinuxTextureHost, RenderTextureHost);
  mSurface = SHMBufSurface::Create(aBuffer, aDescriptor);
}

RenderLinuxTextureHost::~RenderLinuxTextureHost() {
  MOZ_COUNT_DTOR_INHERITED(RenderLinuxTextureHost, RenderTextureHost);
  DeleteTextureHandle();
}

void RenderLinuxTextureHost::PrepareForUse() {
  // We don't use texture recycling for dmabuf, but we may
  // implement it for WebGL for instance.
  if (mSurface->GetAsSHMBufSurface()) {
    mSurface->MarkTextureDirty();
  }
}

wr::WrExternalImage RenderLinuxTextureHost::Lock(uint8_t aChannelIndex,
                                                 gl::GLContext* aGL) {
  const gfx::IntSize size(mSurface->GetWidth(aChannelIndex),
                          mSurface->GetHeight(aChannelIndex));
  // Wayland native compositor may not use textures so pass zero
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
    if (!mSurface->CreateTextures(mGL)) {
      return InvalidToWrExternalImage();
    }
    ActivateBindAndTexParameteri(mGL, LOCAL_GL_TEXTURE0, LOCAL_GL_TEXTURE_2D,
                                 mSurface->GetTexture(aChannelIndex));
  }

  auto texture = mSurface->GetTexture(aChannelIndex);
  if (auto* dmabuf = mSurface->GetAsDMABufSurface()) {
    dmabuf->MaybeSemaphoreWait(texture);
  }

  return NativeTextureToWrExternalImage(texture, 0.0, 0.0,
                                        static_cast<float>(size.width),
                                        static_cast<float>(size.height));
}

gfx::IntSize RenderLinuxTextureHost::GetSize(uint8_t aChannelIndex) const {
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

void RenderLinuxTextureHost::Unlock() {}

void RenderLinuxTextureHost::DeleteTextureHandle() {
  mSurface->ReleaseTextures();
}

void RenderLinuxTextureHost::ClearCachedResources() {
  DeleteTextureHandle();
  mGL = nullptr;
}

bool RenderLinuxTextureHost::MapPlane(RenderCompositor* aCompositor,
                                      uint8_t aChannelIndex,
                                      PlaneInfo& aPlaneInfo) {
  // SHM surfaces are directly mapped
  if (auto* shm = mSurface->GetAsSHMBufSurface()) {
    aPlaneInfo.mData =
        shm->MapPlane(aChannelIndex, aPlaneInfo.mStride, aPlaneInfo.mSize);
    return !!aPlaneInfo.mData;
  }

  if (mSurface->GetAsDMABufSurfaceYUV()) {
    // DMABufSurfaceYUV mapping is not supported yet. Do we need it?
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

void RenderLinuxTextureHost::UnmapPlanes() {
  if (mReadback) {
    mReadback->Unmap();
    mReadback = nullptr;
  }
}

}  // namespace mozilla::wr
