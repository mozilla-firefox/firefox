/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebRenderTextureHost.h"

#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/layers/TextureSourceProvider.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/webrender/WebRenderAPI.h"

#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/layers/TextureHostOGL.h"
#endif

#ifdef XP_WIN
#  include "mozilla/layers/TextureD3D11.h"
#endif

namespace mozilla::layers {

class ScheduleHandleRenderTextureOps : public wr::NotificationHandler {
 public:
  explicit ScheduleHandleRenderTextureOps() = default;

  virtual void Notify(wr::Checkpoint aCheckpoint) override {
    if (aCheckpoint == wr::Checkpoint::FrameTexturesUpdated) {
      MOZ_RELEASE_ASSERT(wr::RenderThread::IsInRenderThread());
      wr::RenderThread::Get()->HandleRenderTextureOps();
    } else {
      MOZ_ASSERT(aCheckpoint == wr::Checkpoint::TransactionDropped);
    }
  }

 protected:
};

WebRenderTextureHost::WebRenderTextureHost(
    TextureFlags aFlags, TextureHost* aTexture,
    const wr::ExternalImageId& aExternalImageId)
    : TextureHost(TextureHostType::Unknown, aFlags),
      mWrappedTextureHost(aTexture) {
  MOZ_ASSERT(mWrappedTextureHost);
  // The wrapped textureHost will be used in WebRender, and the WebRender could
  // run at another thread. It's hard to control the life-time when gecko
  // receives PTextureParent destroy message. It's possible that textureHost is
  // still used by WebRender. So, we only accept the textureHost without
  // DEALLOCATE_CLIENT flag here. If the buffer deallocation is controlled by
  // parent, we could do something to make sure the wrapped textureHost is not
  // used by WebRender and then release it.
  MOZ_ASSERT(!(aFlags & TextureFlags::DEALLOCATE_CLIENT));
  MOZ_COUNT_CTOR(WebRenderTextureHost);

  mExternalImageId = Some(aExternalImageId);
}

WebRenderTextureHost::~WebRenderTextureHost() {
  MOZ_COUNT_DTOR(WebRenderTextureHost);
}

wr::ExternalImageId WebRenderTextureHost::GetExternalImageKey() {
  if (IsValid()) {
    mWrappedTextureHost->EnsureRenderTexture(mExternalImageId);
  }
  MOZ_ASSERT(mWrappedTextureHost->mExternalImageId.isSome());
  return mWrappedTextureHost->mExternalImageId.ref();
}

bool WebRenderTextureHost::IsValid() { return mWrappedTextureHost->IsValid(); }

void WebRenderTextureHost::UnbindTextureSource() {
  if (mWrappedTextureHost->AsBufferTextureHost()) {
    mWrappedTextureHost->UnbindTextureSource();
  }
  // Handle read unlock
  TextureHost::UnbindTextureSource();
}

already_AddRefed<gfx::DataSourceSurface> WebRenderTextureHost::GetAsSurface(
    gfx::DataSourceSurface* aSurface) {
  return mWrappedTextureHost->GetAsSurface(aSurface);
}

gfx::ColorDepth WebRenderTextureHost::GetColorDepth() const {
  return mWrappedTextureHost->GetColorDepth();
}

gfx::YUVColorSpace WebRenderTextureHost::GetYUVColorSpace() const {
  return mWrappedTextureHost->GetYUVColorSpace();
}

gfx::ColorRange WebRenderTextureHost::GetColorRange() const {
  return mWrappedTextureHost->GetColorRange();
}

gfx::TransferFunction WebRenderTextureHost::GetTransferFunction() const {
  return mWrappedTextureHost->GetTransferFunction();
}

gfx::IntSize WebRenderTextureHost::GetSize() const {
  return mWrappedTextureHost->GetSize();
}

gfx::SurfaceFormat WebRenderTextureHost::GetFormat() const {
  return mWrappedTextureHost->GetFormat();
}

bool WebRenderTextureHost::NeedsYFlip() const {
  return mWrappedTextureHost->NeedsYFlip();
}

void WebRenderTextureHost::MaybeDestroyRenderTexture() {
  // WebRenderTextureHost does not create RenderTexture, then
  // WebRenderTextureHost does not need to destroy RenderTexture.
  mExternalImageId = Nothing();
}

void WebRenderTextureHost::NotifyNotUsed() {
#ifdef MOZ_WIDGET_ANDROID
  // When SurfaceTextureHost is wrapped by RemoteTextureHostWrapper,
  // NotifyNotUsed() is handled by SurfaceTextureHost.
  if (IsWrappingSurfaceTextureHost() &&
      !mWrappedTextureHost->AsRemoteTextureHostWrapper()) {
    wr::RenderThread::Get()->NotifyNotUsed(GetExternalImageKey());
  }
#endif
  mWrappedTextureHost->NotifyNotUsed();
  TextureHost::NotifyNotUsed();
}

void WebRenderTextureHost::MaybeNotifyForUse(wr::TransactionBuilder& aTxn) {
#if defined(MOZ_WIDGET_ANDROID)
  if (IsWrappingSurfaceTextureHost() &&
      !mWrappedTextureHost->AsRemoteTextureHostWrapper()) {
    wr::RenderThread::Get()->NotifyForUse(GetExternalImageKey());
    aTxn.Notify(wr::Checkpoint::FrameTexturesUpdated,
                std::make_unique<ScheduleHandleRenderTextureOps>());
  }
#endif
}

bool WebRenderTextureHost::IsWrappingSurfaceTextureHost() {
  return mWrappedTextureHost->IsWrappingSurfaceTextureHost();
}

void WebRenderTextureHost::PrepareForUse() {
  // When SurfaceTextureHost is wrapped by RemoteTextureHostWrapper,
  // PrepareForUse() is handled by SurfaceTextureHost.
  if ((IsWrappingSurfaceTextureHost() &&
       !mWrappedTextureHost->AsRemoteTextureHostWrapper()) ||
      mWrappedTextureHost->AsBufferTextureHost()) {
    // Call PrepareForUse on render thread.
    // See RenderAndroidSurfaceTextureHostOGL::PrepareForUse.
    wr::RenderThread::Get()->PrepareForUse(GetExternalImageKey());
  }
}

gfx::SurfaceFormat WebRenderTextureHost::GetReadFormat() const {
  return mWrappedTextureHost->GetReadFormat();
}

bool WebRenderTextureHost::NeedsDeferredDeletion() const {
  return mWrappedTextureHost->NeedsDeferredDeletion();
}

uint32_t WebRenderTextureHost::NumSubTextures() {
  return mWrappedTextureHost->NumSubTextures();
}

void WebRenderTextureHost::PushResourceUpdates(
    wr::TransactionBuilder& aResources, ResourceUpdateOp aOp,
    const Range<wr::ImageKey>& aImageKeys, const wr::ExternalImageId& aExtID) {
  MOZ_ASSERT(GetExternalImageKey() == aExtID);

  mWrappedTextureHost->PushResourceUpdates(aResources, aOp, aImageKeys, aExtID);
}

void WebRenderTextureHost::PushDisplayItems(
    wr::DisplayListBuilder& aBuilder, const wr::LayoutRect& aBounds,
    const wr::LayoutRect& aClip, wr::ImageRendering aFilter,
    const Range<wr::ImageKey>& aImageKeys, PushDisplayItemFlagSet aFlags) {
  MOZ_ASSERT(aImageKeys.length() > 0);

  mWrappedTextureHost->PushDisplayItems(aBuilder, aBounds, aClip, aFilter,
                                        aImageKeys, aFlags);
}

bool WebRenderTextureHost::SupportsExternalCompositing(
    WebRenderBackend aBackend) {
  return mWrappedTextureHost->SupportsExternalCompositing(aBackend);
}

void WebRenderTextureHost::SetReadFence(Fence* aReadFence) {
  return mWrappedTextureHost->SetReadFence(aReadFence);
}

AndroidHardwareBuffer* WebRenderTextureHost::GetAndroidHardwareBuffer() const {
  return mWrappedTextureHost->GetAndroidHardwareBuffer();
}

TextureHostType WebRenderTextureHost::GetTextureHostType() {
  return mWrappedTextureHost->GetTextureHostType();
}

}  // namespace mozilla::layers
