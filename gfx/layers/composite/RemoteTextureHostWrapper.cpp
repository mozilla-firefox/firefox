/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteTextureHostWrapper.h"

#include "mozilla/StaticPrefs_webgl.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/AsyncImagePipelineManager.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/RemoteTextureMap.h"
#include "mozilla/layers/WebRenderTextureHost.h"
#include "mozilla/webrender/RenderTextureHostWrapper.h"
#include "mozilla/webrender/RenderThread.h"

namespace mozilla::layers {

/* static */
RefPtr<TextureHost> RemoteTextureHostWrapper::Create(
    const RemoteTextureId aTextureId, const RemoteTextureOwnerId aOwnerId,
    const GeckoChildID aForChildID, const gfx::IntSize aSize,
    const TextureFlags aFlags) {
  RefPtr<TextureHost> textureHost =
      new RemoteTextureHostWrapper(aTextureId, aOwnerId, aForChildID, aSize,
                                   aFlags | TextureFlags::REMOTE_TEXTURE);
  auto externalImageId = AsyncImagePipelineManager::GetNextExternalImageId();
  textureHost = new WebRenderTextureHost(aFlags, textureHost, externalImageId);
  return textureHost;
}

RemoteTextureHostWrapper::RemoteTextureHostWrapper(
    const RemoteTextureId aTextureId, const RemoteTextureOwnerId aOwnerId,
    const GeckoChildID aForChildID, const gfx::IntSize aSize,
    const TextureFlags aFlags)
    : TextureHost(TextureHostType::Unknown, aFlags),
      mTextureId(aTextureId),
      mOwnerId(aOwnerId),
      mForChildID(aForChildID),
      mSize(aSize) {
  MOZ_COUNT_CTOR(RemoteTextureHostWrapper);
}

RemoteTextureHostWrapper::~RemoteTextureHostWrapper() {
  MOZ_COUNT_DTOR(RemoteTextureHostWrapper);
}

bool RemoteTextureHostWrapper::IsValid() { return !!mRemoteTexture; }

gfx::YUVColorSpace RemoteTextureHostWrapper::GetYUVColorSpace() const {
  MOZ_ASSERT(mRemoteTexture, "TextureHost isn't valid yet");
  if (!mRemoteTexture) {
    return TextureHost::GetYUVColorSpace();
  }
  return mRemoteTexture->GetYUVColorSpace();
}

gfx::ColorDepth RemoteTextureHostWrapper::GetColorDepth() const {
  MOZ_ASSERT(mRemoteTexture, "TextureHost isn't valid yet");
  if (!mRemoteTexture) {
    return TextureHost::GetColorDepth();
  }
  return mRemoteTexture->GetColorDepth();
}

gfx::ColorRange RemoteTextureHostWrapper::GetColorRange() const {
  MOZ_ASSERT(mRemoteTexture, "TextureHost isn't valid yet");
  if (!mRemoteTexture) {
    return TextureHost::GetColorRange();
  }
  return mRemoteTexture->GetColorRange();
}

gfx::TransferFunction RemoteTextureHostWrapper::GetTransferFunction() const {
  MOZ_ASSERT(mRemoteTexture, "TextureHost isn't valid yet");
  if (!mRemoteTexture) {
    return TextureHost::GetTransferFunction();
  }
  return mRemoteTexture->GetTransferFunction();
}

gfx::IntSize RemoteTextureHostWrapper::GetSize() const {
  MOZ_ASSERT(mRemoteTexture, "TextureHost isn't valid yet");
  if (!mRemoteTexture) {
    return gfx::IntSize();
  }
  return mRemoteTexture->GetSize();
}

gfx::SurfaceFormat RemoteTextureHostWrapper::GetFormat() const {
  MOZ_ASSERT(mRemoteTexture, "TextureHost isn't valid yet");
  if (!mRemoteTexture) {
    return gfx::SurfaceFormat::UNKNOWN;
  }
  return mRemoteTexture->GetFormat();
}

void RemoteTextureHostWrapper::CreateRenderTexture(
    const wr::ExternalImageId& aExternalImageId) {
  MOZ_ASSERT(mExternalImageId.isSome());

  MaybeCreateRenderTexture();
}

void RemoteTextureHostWrapper::MaybeCreateRenderTexture() {
  if (!mRemoteTexture) {
    return;
  }
  MOZ_ASSERT(mRemoteTexture->mExternalImageId.isSome());

  // mRemoteTexture is also used for WebRender rendering.
  auto wrappedId = mRemoteTexture->mExternalImageId.ref();
  RefPtr texture = MakeRefPtr<wr::RenderTextureHostWrapper>(wrappedId);
  wr::RenderThread::Get()->RegisterExternalImage(mExternalImageId.ref(),
                                                 texture.forget());
  mRenderTextureCreated = true;
}

uint32_t RemoteTextureHostWrapper::NumSubTextures() {
  if (!mRemoteTexture) {
    return 0;
  }
  return mRemoteTexture->NumSubTextures();
}

void RemoteTextureHostWrapper::PushResourceUpdates(
    wr::TransactionBuilder& aResources, ResourceUpdateOp aOp,
    const Range<wr::ImageKey>& aImageKeys, const wr::ExternalImageId& aExtID) {
  MOZ_ASSERT(mRemoteTexture, "TextureHost isn't valid yet");
  if (!mRemoteTexture) {
    return;
  }
  mRemoteTexture->PushResourceUpdates(aResources, aOp, aImageKeys, aExtID);
}

void RemoteTextureHostWrapper::PushDisplayItems(
    wr::DisplayListBuilder& aBuilder, const wr::LayoutRect& aBounds,
    const wr::LayoutRect& aClip, wr::ImageRendering aFilter,
    const Range<wr::ImageKey>& aImageKeys, PushDisplayItemFlagSet aFlags) {
  MOZ_ASSERT(mRemoteTexture, "TextureHost isn't valid yet");
  MOZ_ASSERT(aImageKeys.length() > 0);
  if (!mRemoteTexture) {
    return;
  }
  mRemoteTexture->PushDisplayItems(aBuilder, aBounds, aClip, aFilter,
                                   aImageKeys, aFlags);
}

bool RemoteTextureHostWrapper::SupportsExternalCompositing(
    WebRenderBackend aBackend) {
  if (!mRemoteTexture) {
    return false;
  }
  return mRemoteTexture->SupportsExternalCompositing(aBackend);
}

void RemoteTextureHostWrapper::UnbindTextureSource() {}

void RemoteTextureHostWrapper::NotifyNotUsed() {
  if (mRemoteTexture) {
    // Release mRemoteTexture.
    RemoteTextureMap::Get()->ReleaseRemoteTextureHost(this);
  }
  MOZ_ASSERT(!mRemoteTexture);

  RemoteTextureMap::Get()->UnregisterRemoteTextureHostWrapper(
      mTextureId, mOwnerId, mForChildID);
}

void RemoteTextureHostWrapper::SetReadFence(Fence* aReadFence) {
  if (mRemoteTexture) {
    mRemoteTexture->SetReadFence(aReadFence);
  }
}

TextureHostType RemoteTextureHostWrapper::GetTextureHostType() {
  if (!mRemoteTexture) {
    return TextureHostType::Unknown;
  }
  return mRemoteTexture->GetTextureHostType();
}

bool RemoteTextureHostWrapper::IsReadyForRendering() {
  return !!mRemoteTexture;
}

void RemoteTextureHostWrapper::ApplyTextureFlagsToRemoteTexture() {
  if (!mRemoteTexture) {
    return;
  }
  mRemoteTexture->SetFlags(mFlags | TextureFlags::DEALLOCATE_CLIENT);
}

TextureHost* RemoteTextureHostWrapper::GetRemoteTextureHost(
    const MonitorAutoLock& aProofOfLock) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  return mRemoteTexture;
}

void RemoteTextureHostWrapper::SetRemoteTextureHost(
    const MonitorAutoLock& aProofOfLock, TextureHost* aTextureHost) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  mRemoteTexture = aTextureHost;

  if (mExternalImageId.isSome() && !mRenderTextureCreated) {
    MaybeCreateRenderTexture();
  }
}

void RemoteTextureHostWrapper::ClearRemoteTextureHost(
    const MonitorAutoLock& aProofOfLoc) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  mRemoteTexture = nullptr;
}

bool RemoteTextureHostWrapper::IsWrappingSurfaceTextureHost() {
  if (!mRemoteTexture) {
    return false;
  }
  return mRemoteTexture->IsWrappingSurfaceTextureHost();
}

bool RemoteTextureHostWrapper::NeedsDeferredDeletion() const {
  if (!mRemoteTexture) {
    return true;
  }
  return mRemoteTexture->NeedsDeferredDeletion();
}

AndroidHardwareBuffer* RemoteTextureHostWrapper::GetAndroidHardwareBuffer()
    const {
  if (!mRemoteTexture) {
    return nullptr;
  }
  return mRemoteTexture->GetAndroidHardwareBuffer();
}

}  // namespace mozilla::layers
