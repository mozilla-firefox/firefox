/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/NativeLayerRemoteWayland.h"

#include <utility>

#include "mozilla/gfx/Logging.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/layers/SurfacePoolWayland.h"
#include "mozilla/webrender/RenderLinuxTextureHost.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/widget/DMABufSurface.h"
#include "mozilla/widget/SHMBufSurface.h"
#include "mozilla/widget/WaylandBuffer.h"

namespace mozilla {
namespace layers {

using gfx::IntPoint;
using gfx::IntRect;
using gfx::IntRegion;
using gfx::IntSize;
using gfx::Matrix4x4;

NativeLayerRemoteWayland::NativeLayerRemoteWayland(
    const IntSize& aSize, bool aIsOpaque,
    SurfacePoolHandleWayland* aSurfacePoolHandle,
    RefPtr<widget::DRMFormat> aDRMFormat)
    : mDRMFormat(std::move(aDRMFormat)), mIsOpaque(aIsOpaque) {
  // We need a surface handler for this type of layer.
  mSurfaceHandler.emplace(aSize, aSurfacePoolHandle);
}

NativeLayerRemoteWayland::NativeLayerRemoteWayland(bool aIsOpaque)
    : mIsOpaque(aIsOpaque) {}

NativeLayerRemoteWayland::NativeLayerRemoteWayland(gfx::DeviceColor aColor)
    : mColor(Some(aColor)), mIsOpaque(aColor.a >= 1.0f) {}

NativeLayerRemoteWayland::~NativeLayerRemoteWayland() {
  if (mSurfaceHandler) {
    mSurfaceHandler->DiscardBackbuffersLocked(nullptr, /* aForce */ true);
  }
  if (mCommandQueue) {
    mCommandQueue->AppendCommand(mozilla::layers::CommandLayerDestroyed(
        reinterpret_cast<uint64_t>(this)));
  }
}

void NativeLayerRemoteWayland::AttachExternalImage(
    wr::RenderTextureHost* aExternalImage) {
  MOZ_ASSERT(XRE_IsGPUProcess());
  MOZ_ASSERT(!mSurfaceHandler);

  RefPtr<BufferSurface> surface;
  if (auto* linuxHost = aExternalImage->AsRenderLinuxTextureHost()) {
    surface = linuxHost->GetSurface();
  }
  if (!surface) {
    gfxCriticalNoteOnce << "ExternalImage is not RenderLinuxTextureHost";
    return;
  }

  auto lastSurface = reinterpret_cast<uintptr_t>(surface.get());
  if (mLastExternalSurface == lastSurface) {
    return;
  }
  mLastExternalSurface = lastSurface;

  // The parent process can only present DMABuf surfaces.
  if (auto* shmSurface = surface->GetAsSHMBufSurface()) {
    surface = shmSurface->UploadToDMABufSurface(
        wr::RenderThread::Get()->SingletonGL());
    if (!surface) {
      gfxCriticalNoteOnce << "SHM->DMABuf conversion failed.";
      mExternalImage = nullptr;
      mDirtyChangedSurface = true;
      return;
    }
    surface->DisableRecycle();
  }

  IntSize size = surface->GetSize(0);
  auto displayRect = IntRect(IntPoint{}, size);
  mDirtyLayerInfo |= !mDisplayRect.IsEqualEdges(displayRect);
  mDisplayRect = displayRect;
  mSize = size;
  mIsDRM = aExternalImage->IsFromDRMSource();
  mIsHDR = surface->IsHDRSurface();
  mExternalImage = widget::WaylandBuffer::Create(surface);
  mDirtyChangedSurface = true;
}

RefPtr<GpuFence> NativeLayerRemoteWayland::GetGpuFence() { return nullptr; }

IntSize NativeLayerRemoteWayland::GetSize() {
  if (mSurfaceHandler) {
    return mSurfaceHandler->Size();
  }
  return mSize;
}

void NativeLayerRemoteWayland::SetPosition(const IntPoint& aPosition) {
  if (mPosition != aPosition) {
    mDirtyLayerInfo = true;
    mPosition = aPosition;
  }
}

IntPoint NativeLayerRemoteWayland::GetPosition() { return mPosition; }

void NativeLayerRemoteWayland::SetTransform(const Matrix4x4& aTransform) {
  MOZ_ASSERT(aTransform.IsRectilinear());

  if (mTransform != aTransform) {
    mDirtyLayerInfo = true;
    mTransform = aTransform;
  }
}

Matrix4x4 NativeLayerRemoteWayland::GetTransform() { return mTransform; }

void NativeLayerRemoteWayland::SetSamplingFilter(
    gfx::SamplingFilter aSamplingFilter) {
  if (mSamplingFilter != aSamplingFilter) {
    mDirtyLayerInfo = true;
    mSamplingFilter = aSamplingFilter;
  }
}

gfx::SamplingFilter NativeLayerRemoteWayland::SamplingFilter() {
  return mSamplingFilter;
}

IntRect NativeLayerRemoteWayland::GetRect() {
  return IntRect(mPosition, GetSize());
}

bool NativeLayerRemoteWayland::IsOpaque() { return mIsOpaque; }

void NativeLayerRemoteWayland::SetClipRect(
    const Maybe<gfx::IntRect>& aClipRect) {
  if (mClipRect != aClipRect) {
    mDirtyLayerInfo = true;
    mClipRect = aClipRect;
  }
}

Maybe<gfx::IntRect> NativeLayerRemoteWayland::ClipRect() { return mClipRect; }

void NativeLayerRemoteWayland::SetRoundedClipRect(
    const Maybe<gfx::RoundedRect>& aRoundedClipRect) {
  if (mRoundedClipRect != aRoundedClipRect) {
    mDirtyLayerInfo = true;
    mRoundedClipRect = aRoundedClipRect;
  }
}

Maybe<gfx::RoundedRect> NativeLayerRemoteWayland::RoundedClipRect() {
  return mRoundedClipRect;
}

gfx::IntRect NativeLayerRemoteWayland::CurrentSurfaceDisplayRect() {
  return mDisplayRect;
}

void NativeLayerRemoteWayland::SetSurfaceIsFlipped(bool aIsFlipped) {
  if (SurfaceIsFlipped() != aIsFlipped) {
    mDirtyLayerInfo = true;
    if (mSurfaceHandler) {
      mSurfaceHandler->SetSurfaceIsFlipped(aIsFlipped);
    } else {
      mSurfaceIsFlipped = aIsFlipped;
    }
  }
}

bool NativeLayerRemoteWayland::SurfaceIsFlipped() {
  if (mSurfaceHandler) {
    return mSurfaceHandler->SurfaceIsFlipped();
  }
  return mSurfaceIsFlipped;
}

RefPtr<gfx::DrawTarget> NativeLayerRemoteWayland::NextSurfaceAsDrawTarget(
    const IntRect& aDisplayRect, const IntRegion& aUpdateRegion,
    gfx::BackendType aBackendType) {
  MOZ_ASSERT(mSurfaceHandler);
  return mSurfaceHandler->NextSurfaceAsDrawTarget(
      nullptr, aDisplayRect, aUpdateRegion, aBackendType, mDRMFormat);
}

Maybe<GLuint> NativeLayerRemoteWayland::NextSurfaceAsFramebuffer(
    const IntRect& aDisplayRect, const IntRegion& aUpdateRegion,
    bool aNeedsDepth) {
  MOZ_ASSERT(mSurfaceHandler);
  return mSurfaceHandler->NextSurfaceAsFramebuffer(
      nullptr, aDisplayRect, aUpdateRegion, aNeedsDepth, mDRMFormat);
}

RefPtr<widget::WaylandBuffer> NativeLayerRemoteWayland::FrontBuffer() {
  if (mSurfaceHandler) {
    return mSurfaceHandler->FrontBuffer();
  }
  return mExternalImage;
}

void NativeLayerRemoteWayland::NotifySurfaceReady() {
  MOZ_ASSERT(mSurfaceHandler);
  mSurfaceHandler->NotifySurfaceReady(nullptr);

  IntRect displayRect = mSurfaceHandler->DisplayRect();
  mDirtyLayerInfo |= !mDisplayRect.IsEqualEdges(displayRect);
  mDisplayRect = displayRect;
  mDirtyChangedSurface = true;
}

void NativeLayerRemoteWayland::DiscardBackbuffers() {
  MOZ_ASSERT(mSurfaceHandler);
  mSurfaceHandler->DiscardBackbuffersLocked(nullptr, /* aForce */ false);
}

void NativeLayerRemoteWayland::FlushDirtyLayerInfoToCommandQueue() {
  auto ID = reinterpret_cast<uint64_t>(this);

  if (mDirtyChangedSurface) {
    Maybe<SurfaceDescriptorDMABuf> desc;
    RefPtr<widget::WaylandBuffer> buffer = FrontBuffer();
    if (DMABufSurface* surface =
            buffer ? buffer->GetDMABufSurface() : nullptr) {
      SurfaceDescriptor surfaceDesc;
      if (surface->Serialize(surfaceDesc)) {
        // Hold a global reference while the descriptor is in flight, so the
        // buffer isn't recycled before the parent process imports it. The
        // parent releases this reference once it has imported the surface.
        if (surface->GetGlobalRefCountFd()) {
          surface->GlobalRefAdd();
        }
        desc = Some(surfaceDesc.get_SurfaceDescriptorDMABuf());
      } else {
        gfxCriticalNoteOnce << "Failed to serialize DMABufSurface.";
      }
    }

    mCommandQueue->AppendCommand(mozilla::layers::CommandChangedSurface(
        ID, mozilla::layers::SurfaceTransferWayland(std::move(desc)), IsDRM(),
        IsHDR(), GetSize()));
    mDirtyChangedSurface = false;
  }

  if (mDirtyLayerInfo) {
    mCommandQueue->AppendCommand(mozilla::layers::CommandLayerInfo(
        ID, GetPosition(), CurrentSurfaceDisplayRect(), ClipRect(),
        RoundedClipRect(), GetTransform(), SamplingFilter(),
        SurfaceIsFlipped()));
    mDirtyLayerInfo = false;
  }
}

}  // namespace layers
}  // namespace mozilla
