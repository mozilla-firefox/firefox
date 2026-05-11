/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/NativeLayerRemoteMac.h"

#include <algorithm>
#include <utility>

#include "CFTypeRefPtr.h"
#include "gfxPlatform.h"
#include "gfxUtils.h"
#include "GLBlitHelper.h"
#ifdef XP_MACOSX
#  include "GLContextCGL.h"
#else
#  include "GLContextEAGL.h"
#endif
#include "GLContextProvider.h"
#include "MozFramebuffer.h"
#include "mozilla/gfx/Swizzle.h"
#include "mozilla/glean/GfxMetrics.h"
#include "mozilla/webrender/RenderMacIOSurfaceTextureHost.h"
#include "NativeLayerCA.h"
#include "ScopedGLHelpers.h"

namespace mozilla {
namespace layers {

using gfx::IntPoint;
using gfx::IntRect;
using gfx::IntRegion;
using gfx::IntSize;
using gfx::Matrix4x4;

NativeLayerRemoteMac::NativeLayerRemoteMac(
    const IntSize& aSize, bool aIsOpaque,
    SurfacePoolHandleCA* aSurfacePoolHandle)
    : mIsOpaque(aIsOpaque) {
  // We need a surface handler for this type of layer.
  mSurfaceHandler.emplace(aSize, aSurfacePoolHandle);
}

NativeLayerRemoteMac::NativeLayerRemoteMac(bool aIsOpaque)
    : mIsOpaque(aIsOpaque) {}

NativeLayerRemoteMac::NativeLayerRemoteMac(gfx::DeviceColor aColor)
    : mColor(Some(aColor)), mIsOpaque(aColor.a >= 1.0f) {}

NativeLayerRemoteMac::~NativeLayerRemoteMac() {
  if (mCommandQueue) {
    mCommandQueue->AppendCommand(mozilla::layers::CommandLayerDestroyed(
        reinterpret_cast<uint64_t>(this)));
  }
}

void NativeLayerRemoteMac::AttachExternalImage(
    wr::RenderTextureHost* aExternalImage) {
  MOZ_ASSERT(XRE_IsGPUProcess());
  MOZ_ASSERT(!mSurfaceHandler);

  wr::RenderMacIOSurfaceTextureHost* texture =
      aExternalImage->AsRenderMacIOSurfaceTextureHost();
  MOZ_ASSERT(texture);

  auto externalImage = texture->GetSurface()->GetIOSurfaceRef();
  mExternalImage = externalImage;

  auto texSize = texture->GetSize(0);
  mSize = texSize;

  auto displayRect = IntRect(IntPoint{}, mSize);
  bool changedDisplayRect = !mDisplayRect.IsEqualInterior(displayRect);
  mDisplayRect = displayRect;

  bool isDRM = aExternalImage->IsFromDRMSource();
  bool changedIsDRM = mIsDRM != isDRM;
  mIsDRM = isDRM;

  MacIOSurface* macIOSurface = texture->GetSurface();
  mIsHDR = macIOSurface->IsHDRSurface() && gfxPlatform::UseHDR();

  mDirtyLayerInfo |= changedDisplayRect;
  mSnapshotLayer.mMutatedFrontSurface = true;
  mSnapshotLayer.mMutatedSize |= changedDisplayRect;
  mSnapshotLayer.mMutatedDisplayRect |= changedDisplayRect;
  mSnapshotLayer.mMutatedIsDRM |= changedIsDRM;
  mDirtyChangedSurface = true;
}

GpuFence* NativeLayerRemoteMac::GetGpuFence() { return nullptr; }

IntSize NativeLayerRemoteMac::GetSize() {
  if (mSurfaceHandler) {
    return mSurfaceHandler->Size();
  }
  return mSize;
}

void NativeLayerRemoteMac::SetPosition(const IntPoint& aPosition) {
  if (mPosition != aPosition) {
    mDirtyLayerInfo = true;
    mSnapshotLayer.mMutatedPosition = true;
    mPosition = aPosition;
  }
}

IntPoint NativeLayerRemoteMac::GetPosition() { return mPosition; }

void NativeLayerRemoteMac::SetTransform(const Matrix4x4& aTransform) {
  MOZ_ASSERT(aTransform.IsRectilinear());

  if (mTransform != aTransform) {
    mDirtyLayerInfo = true;
    mSnapshotLayer.mMutatedTransform = true;
    mTransform = aTransform;
  }
}

void NativeLayerRemoteMac::SetSamplingFilter(
    gfx::SamplingFilter aSamplingFilter) {
  if (mSamplingFilter != aSamplingFilter) {
    mDirtyLayerInfo = true;
    mSnapshotLayer.mMutatedSamplingFilter = true;
    mSamplingFilter = aSamplingFilter;
  }
}

gfx::SamplingFilter NativeLayerRemoteMac::SamplingFilter() {
  return mSamplingFilter;
}

Matrix4x4 NativeLayerRemoteMac::GetTransform() { return mTransform; }

IntRect NativeLayerRemoteMac::GetRect() {
  IntSize size = mSize;
  if (mSurfaceHandler) {
    size = mSurfaceHandler->Size();
  }
  return IntRect(mPosition, size);
}

bool NativeLayerRemoteMac::IsOpaque() { return mIsOpaque; }

void NativeLayerRemoteMac::SetClipRect(const Maybe<gfx::IntRect>& aClipRect) {
  if (mClipRect != aClipRect) {
    mDirtyLayerInfo = true;
    mClipRect = aClipRect;
  }
}

Maybe<gfx::IntRect> NativeLayerRemoteMac::ClipRect() { return mClipRect; }

void NativeLayerRemoteMac::SetRoundedClipRect(
    const Maybe<gfx::RoundedRect>& aRoundedClipRect) {
  if (mRoundedClipRect != aRoundedClipRect) {
    mDirtyLayerInfo = true;
    mSnapshotLayer.mMutatedRoundedClipRect = true;
    mRoundedClipRect = aRoundedClipRect;
  }
}

Maybe<gfx::RoundedRect> NativeLayerRemoteMac::RoundedClipRect() {
  return mRoundedClipRect;
}

gfx::IntRect NativeLayerRemoteMac::CurrentSurfaceDisplayRect() {
  if (mSurfaceHandler) {
    return mSurfaceHandler->DisplayRect();
  }
  return mDisplayRect;
}

void NativeLayerRemoteMac::SetSurfaceIsFlipped(bool aIsFlipped) {
  if (SurfaceIsFlipped() != aIsFlipped) {
    mDirtyLayerInfo = true;
    mSnapshotLayer.mMutatedSurfaceIsFlipped = true;
    if (mSurfaceHandler) {
      mSurfaceHandler->SetSurfaceIsFlipped(aIsFlipped);
    } else {
      mSurfaceIsFlipped = aIsFlipped;
    }
  }
}

bool NativeLayerRemoteMac::SurfaceIsFlipped() {
  if (mSurfaceHandler) {
    return mSurfaceHandler->SurfaceIsFlipped();
  }
  return mSurfaceIsFlipped;
}

RefPtr<gfx::DrawTarget> NativeLayerRemoteMac::NextSurfaceAsDrawTarget(
    const IntRect& aDisplayRect, const IntRegion& aUpdateRegion,
    gfx::BackendType aBackendType) {
  MOZ_ASSERT(mSurfaceHandler);
  return mSurfaceHandler->NextSurfaceAsDrawTarget(aDisplayRect, aUpdateRegion,
                                                  aBackendType);
}

Maybe<GLuint> NativeLayerRemoteMac::NextSurfaceAsFramebuffer(
    const IntRect& aDisplayRect, const IntRegion& aUpdateRegion,
    bool aNeedsDepth) {
  MOZ_ASSERT(mSurfaceHandler);
  return mSurfaceHandler->NextSurfaceAsFramebuffer(aDisplayRect, aUpdateRegion,
                                                   aNeedsDepth);
}

Maybe<SurfaceWithInvalidRegion> NativeLayerRemoteMac::FrontSurface() {
  if (mSurfaceHandler) {
    return mSurfaceHandler->FrontSurface();
  }
  if (mExternalImage) {
    return Some(SurfaceWithInvalidRegion{mExternalImage, GetRect()});
  }
  return Nothing();
}

void NativeLayerRemoteMac::NotifySurfaceReady() {
  MOZ_ASSERT(mSurfaceHandler);

  // When we call the surface handler NotifySurfaceReady(), we get a
  // return value indicating if the display rect has changed. Set our
  // dirty flag (covering all non-surface changes) if that is true.
  bool changedDisplayRect = mSurfaceHandler->NotifySurfaceReady();
  mDirtyLayerInfo |= changedDisplayRect;
  mDirtyChangedSurface = true;
  mSnapshotLayer.mMutatedFrontSurface = true;
}

void NativeLayerRemoteMac::DiscardBackbuffers() {
  MOZ_ASSERT(mSurfaceHandler);
  mSurfaceHandler->DiscardBackbuffers();
}

void NativeLayerRemoteMac::FlushDirtyLayerInfoToCommandQueue() {
  auto ID = reinterpret_cast<uint64_t>(this);

  if (mDirtyChangedSurface) {
    IOSurfacePort surfacePort;
    auto surfaceWithInvalidRegion = FrontSurface();
    if (surfaceWithInvalidRegion) {
      surfacePort =
          IOSurfacePort::FromSurface(surfaceWithInvalidRegion->mSurface);
    }

    mCommandQueue->AppendCommand(mozilla::layers::CommandChangedSurface(
        ID, std::move(surfacePort), IsDRM(), IsHDR(), GetSize()));
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

void NativeLayerRemoteMac::UpdateSnapshotLayer() {
  CFTypeRefPtr<IOSurfaceRef> surface;
  if (auto frontSurface = FrontSurface()) {
    surface = frontSurface->mSurface;
  }

  IntRect rect = GetRect();
  IntRect displayRect = CurrentSurfaceDisplayRect();

  bool specializeVideo = false;
  bool isVideo = false;
  mSnapshotLayer.ApplyChanges(
      NativeLayerCAUpdateType::All, rect.Size(), mIsOpaque, rect.TopLeft(),
      mTransform, displayRect, mClipRect, mRoundedClipRect, mBackingScale,
      mSurfaceIsFlipped, mSamplingFilter, specializeVideo, surface, mColor,
      mIsDRM, isVideo);
}

CALayer* NativeLayerRemoteMac::CALayerForSnapshot() {
  return mSnapshotLayer.UnderlyingCALayer();
}

}  // namespace layers
}  // namespace mozilla
