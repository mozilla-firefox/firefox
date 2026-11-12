/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderTextureHostWrapper.h"

#include "mozilla/gfx/Logging.h"
#include "mozilla/layers/RemoteTextureMap.h"
#include "mozilla/webrender/RenderThread.h"

namespace mozilla {
namespace wr {

RenderTextureHostWrapper::RenderTextureHostWrapper(
    ExternalImageId aExternalImageId)
    : mExternalImageId(aExternalImageId) {
  MOZ_COUNT_CTOR_INHERITED(RenderTextureHostWrapper, RenderTextureHost);
  EnsureTextureHost();
}

RenderTextureHostWrapper::~RenderTextureHostWrapper() {
  MOZ_COUNT_DTOR_INHERITED(RenderTextureHostWrapper, RenderTextureHost);
}

void RenderTextureHostWrapper::EnsureTextureHost() const {
  if (mTextureHost) {
    return;
  }

  mTextureHost = RenderThread::Get()->GetRenderTexture(mExternalImageId);
  if (!mTextureHost) {
    gfxCriticalNoteOnce << "Failed to get RenderTextureHost for extId:"
                        << AsUint64(mExternalImageId);
  }
}

wr::WrExternalImage RenderTextureHostWrapper::Lock(uint8_t aChannelIndex,
                                                   gl::GLContext* aGL) {
  if (!mTextureHost) {
    return InvalidToWrExternalImage();
  }

  return mTextureHost->Lock(aChannelIndex, aGL);
}

void RenderTextureHostWrapper::Unlock() {
  if (mTextureHost) {
    mTextureHost->Unlock();
  }
}

wr::WrExternalImage RenderTextureHostWrapper::LockSWGL(
    uint8_t aChannelIndex, void* aContext, RenderCompositor* aCompositor) {
  if (!mTextureHost) {
    return InvalidToWrExternalImage();
  }

  return mTextureHost->LockSWGL(aChannelIndex, aContext, aCompositor);
}

void RenderTextureHostWrapper::UnlockSWGL() {
  if (mTextureHost) {
    mTextureHost->UnlockSWGL();
  }
}

bool RenderTextureHostWrapper::LockSWGLCompositeSurface(
    void* aContext, wr::SWGLCompositeSurfaceInfo* aInfo) {
  if (!mTextureHost) {
    return false;
  }

  return mTextureHost->LockSWGLCompositeSurface(aContext, aInfo);
}

void RenderTextureHostWrapper::UnlockSWGLCompositeSurface() {
  if (mTextureHost) {
    mTextureHost->UnlockSWGLCompositeSurface();
  }
}

void RenderTextureHostWrapper::ClearCachedResources() {
  if (mTextureHost) {
    mTextureHost->ClearCachedResources();
  }
}

void RenderTextureHostWrapper::PrepareForUse() {
  if (!mTextureHost) {
    return;
  }
  mTextureHost->PrepareForUse();
}

void RenderTextureHostWrapper::NotifyForUse() {
  if (!mTextureHost) {
    return;
  }
  mTextureHost->NotifyForUse();
}

void RenderTextureHostWrapper::NotifyNotUsed() {
  if (!mTextureHost) {
    return;
  }
  mTextureHost->NotifyNotUsed();
}

bool RenderTextureHostWrapper::SyncObjectNeeded() { return false; }

RefPtr<layers::TextureSource> RenderTextureHostWrapper::CreateTextureSource(
    layers::TextureSourceProvider* aProvider) {
  if (!mTextureHost) {
    return nullptr;
  }
  return mTextureHost->CreateTextureSource(aProvider);
}

RenderMacIOSurfaceTextureHost*
RenderTextureHostWrapper::AsRenderMacIOSurfaceTextureHost() {
  if (!mTextureHost) {
    return nullptr;
  }
  return mTextureHost->AsRenderMacIOSurfaceTextureHost();
}

RenderDXGITextureHost* RenderTextureHostWrapper::AsRenderDXGITextureHost() {
  if (!mTextureHost) {
    return nullptr;
  }
  return mTextureHost->AsRenderDXGITextureHost();
}

RenderDXGIYCbCrTextureHost*
RenderTextureHostWrapper::AsRenderDXGIYCbCrTextureHost() {
  if (!mTextureHost) {
    return nullptr;
  }
  return mTextureHost->AsRenderDXGIYCbCrTextureHost();
}

RenderDcompSurfaceTextureHost*
RenderTextureHostWrapper::AsRenderDcompSurfaceTextureHost() {
  if (!mTextureHost) {
    return nullptr;
  }
  return mTextureHost->AsRenderDcompSurfaceTextureHost();
}

RenderTextureHostSWGL* RenderTextureHostWrapper::AsRenderTextureHostSWGL() {
  if (!mTextureHost) {
    return nullptr;
  }
  return mTextureHost->AsRenderTextureHostSWGL();
}

RenderDMABUFTextureHost* RenderTextureHostWrapper::AsRenderDMABUFTextureHost() {
  if (!mTextureHost) {
    return nullptr;
  }
  return mTextureHost->AsRenderDMABUFTextureHost();
}

RenderWaylandTextureHost*
RenderTextureHostWrapper::AsRenderWaylandTextureHost() {
  if (!mTextureHost) {
    return nullptr;
  }
  return mTextureHost->AsRenderWaylandTextureHost();
}

RenderAndroidHardwareBufferTextureHost*
RenderTextureHostWrapper::AsRenderAndroidHardwareBufferTextureHost() {
  if (!mTextureHost) {
    return nullptr;
  }
  return mTextureHost->AsRenderAndroidHardwareBufferTextureHost();
}

RenderAndroidImageReaderImageTextureHost*
RenderTextureHostWrapper::AsRenderAndroidImageReaderImageTextureHost() {
  if (!mTextureHost) {
    return nullptr;
  }
  return mTextureHost->AsRenderAndroidImageReaderImageTextureHost();
}

RenderAndroidSurfaceTextureHost*
RenderTextureHostWrapper::AsRenderAndroidSurfaceTextureHost() {
  if (!mTextureHost) {
    return nullptr;
  }
  return mTextureHost->AsRenderAndroidSurfaceTextureHost();
}

RenderEGLImageTextureHost*
RenderTextureHostWrapper::AsRenderEGLImageTextureHost() {
  if (!mTextureHost) {
    return nullptr;
  }
  return mTextureHost->AsRenderEGLImageTextureHost();
}

RenderTextureHostSWGL* RenderTextureHostWrapper::EnsureRenderTextureHostSWGL()
    const {
  if (!mTextureHost) {
    return nullptr;
  }
  return mTextureHost->AsRenderTextureHostSWGL();
}

void RenderTextureHostWrapper::SetReadFenceFd(UniqueFileHandle&& aFenceFd) {
  if (!mTextureHost) {
    return;
  }
  mTextureHost->SetReadFenceFd(std::move(aFenceFd));
}

void RenderTextureHostWrapper::SetIsSoftwareDecodedVideo() {
  if (!mTextureHost) {
    return;
  }
  return mTextureHost->SetIsSoftwareDecodedVideo();
}

bool RenderTextureHostWrapper::IsSoftwareDecodedVideo() {
  if (!mTextureHost) {
    return false;
  }
  return mTextureHost->IsSoftwareDecodedVideo();
}

RefPtr<RenderTextureHostUsageInfo>
RenderTextureHostWrapper::GetOrMergeUsageInfo(
    const MutexAutoLock& aProofOfMapLock,
    RefPtr<RenderTextureHostUsageInfo> aUsageInfo) {
  if (!mTextureHost) {
    return nullptr;
  }
  return mTextureHost->GetOrMergeUsageInfo(aProofOfMapLock, aUsageInfo);
}

RefPtr<RenderTextureHostUsageInfo>
RenderTextureHostWrapper::GetTextureHostUsageInfo(
    const MutexAutoLock& aProofOfMapLock) {
  if (!mTextureHost) {
    return nullptr;
  }
  return mTextureHost->GetTextureHostUsageInfo(aProofOfMapLock);
}

size_t RenderTextureHostWrapper::GetPlaneCount() const {
  if (RenderTextureHostSWGL* swglHost = EnsureRenderTextureHostSWGL()) {
    return swglHost->GetPlaneCount();
  }
  return 0;
}

gfx::SurfaceFormat RenderTextureHostWrapper::GetFormat() const {
  if (RenderTextureHostSWGL* swglHost = EnsureRenderTextureHostSWGL()) {
    return swglHost->GetFormat();
  }
  return gfx::SurfaceFormat::UNKNOWN;
}

gfx::ColorDepth RenderTextureHostWrapper::GetColorDepth() const {
  if (RenderTextureHostSWGL* swglHost = EnsureRenderTextureHostSWGL()) {
    return swglHost->GetColorDepth();
  }
  return gfx::ColorDepth::COLOR_8;
}

gfx::YUVRangedColorSpace RenderTextureHostWrapper::GetYUVColorSpace() const {
  if (RenderTextureHostSWGL* swglHost = EnsureRenderTextureHostSWGL()) {
    return swglHost->GetYUVColorSpace();
  }
  return gfx::YUVRangedColorSpace::Default;
}

gfx::TransferFunction RenderTextureHostWrapper::GetTransferFunction() const {
  if (RenderTextureHostSWGL* swglHost = EnsureRenderTextureHostSWGL()) {
    return swglHost->GetTransferFunction();
  }
  return gfx::TransferFunction::BT709;
}

bool RenderTextureHostWrapper::MapPlane(RenderCompositor* aCompositor,
                                        uint8_t aChannelIndex,
                                        PlaneInfo& aPlaneInfo) {
  if (RenderTextureHostSWGL* swglHost = EnsureRenderTextureHostSWGL()) {
    return swglHost->MapPlane(aCompositor, aChannelIndex, aPlaneInfo);
  }
  return false;
}

void RenderTextureHostWrapper::UnmapPlanes() {
  if (RenderTextureHostSWGL* swglHost = EnsureRenderTextureHostSWGL()) {
    swglHost->UnmapPlanes();
  }
}

}  // namespace wr
}  // namespace mozilla
