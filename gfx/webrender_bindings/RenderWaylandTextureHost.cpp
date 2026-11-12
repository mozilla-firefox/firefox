/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderWaylandTextureHost.h"

#include "GLContext.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/widget/SHMBufSurface.h"

using namespace mozilla::widget;

namespace mozilla {
namespace wr {

RenderWaylandTextureHost::RenderWaylandTextureHost(
    uint8_t* aBuffer, const layers::BufferDescriptor& aDescriptor) {
  MOZ_COUNT_CTOR_INHERITED(RenderWaylandTextureHost, RenderTextureHost);
  if (aDescriptor.type() == layers::BufferDescriptor::TYCbCrDescriptor) {
    mSurface = SHMBufSurfaceYUV::Create(aBuffer, aDescriptor);
  } else {
    mSurface = SHMBufSurfaceRGBA::Create(aBuffer, aDescriptor);
  }
  mDescriptor = aDescriptor;
}

RenderWaylandTextureHost::~RenderWaylandTextureHost() {
  MOZ_COUNT_DTOR_INHERITED(RenderWaylandTextureHost, RenderTextureHost);
}

void RenderWaylandTextureHost::PrepareForUse() { mSurface->MarkTextureDirty(); }

wr::WrExternalImage RenderWaylandTextureHost::Lock(uint8_t aChannelIndex,
                                                   gl::GLContext* aGL) {
  const gfx::IntSize size(mSurface->GetWidth(aChannelIndex),
                          mSurface->GetHeight(aChannelIndex));

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

  // CreateTextures() refreshes recycled SHM content and sets texture filters.
  if (!mSurface->CreateTextures(mGL)) {
    return InvalidToWrExternalImage();
  }

  return NativeTextureToWrExternalImage(
      mSurface->GetTexture(aChannelIndex), 0.0, 0.0,
      static_cast<float>(size.width), static_cast<float>(size.height));
}

void RenderWaylandTextureHost::DeleteTextureHandle() {
  mSurface->ReleaseTextures();
}

void RenderWaylandTextureHost::ClearCachedResources() {
  DeleteTextureHandle();
  mGL = nullptr;
}

bool RenderWaylandTextureHost::MapPlane(RenderCompositor* aCompositor,
                                        uint8_t aChannelIndex,
                                        PlaneInfo& aPlaneInfo) {
  uint8_t* buffer = reinterpret_cast<uint8_t*>(mSurface->GetImageData());
  switch (mDescriptor.type()) {
    case layers::BufferDescriptor::TYCbCrDescriptor: {
      const layers::YCbCrDescriptor& desc = mDescriptor.get_YCbCrDescriptor();
      switch (aChannelIndex) {
        case 0:
          aPlaneInfo.mData =
              layers::ImageDataSerializer::GetYChannel(buffer, desc);
          aPlaneInfo.mStride = desc.yStride();
          aPlaneInfo.mSize = desc.display().Size();
          break;
        case 1:
          aPlaneInfo.mData =
              layers::ImageDataSerializer::GetCbChannel(buffer, desc);
          aPlaneInfo.mStride = desc.cbCrStride();
          aPlaneInfo.mSize =
              layers::ImageDataSerializer::GetCroppedCbCrSize(desc);
          break;
        case 2:
          aPlaneInfo.mData =
              layers::ImageDataSerializer::GetCrChannel(buffer, desc);
          aPlaneInfo.mStride = desc.cbCrStride();
          aPlaneInfo.mSize =
              layers::ImageDataSerializer::GetCroppedCbCrSize(desc);
          break;
      }
      break;
    }
    default: {
      const layers::RGBDescriptor& desc = mDescriptor.get_RGBDescriptor();
      aPlaneInfo.mData = buffer;
      auto stride = layers::ImageDataSerializer::GetRGBStride(desc);
      if (stride.isNothing()) {
        gfxCriticalNote << "Invalid stride";
        return false;
      }
      aPlaneInfo.mStride = stride.value();
      aPlaneInfo.mSize = desc.size();
      break;
    }
  }
  return true;
}

void RenderWaylandTextureHost::UnmapPlanes() {}

}  // namespace wr
}  // namespace mozilla
