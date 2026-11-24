/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_RemoteTextureHostWrapper_H
#define MOZILLA_GFX_RemoteTextureHostWrapper_H

#include "mozilla/Monitor.h"
#include "mozilla/layers/RemoteTextureMap.h"
#include "mozilla/layers/TextureHost.h"

namespace mozilla::layers {

// This class wraps TextureHost of remote texture.
// mRemoteTexture holds TextureHost of mTextureId.
class RemoteTextureHostWrapper : public TextureHost {
 public:
  static RefPtr<TextureHost> Create(const RemoteTextureId aTextureId,
                                    const RemoteTextureOwnerId aOwnerId,
                                    const GeckoChildID aForChildID,
                                    const gfx::IntSize aSize,
                                    const TextureFlags aFlags);

  void DeallocateDeviceData() override {}

  gfx::SurfaceFormat GetFormat() const override;

  already_AddRefed<gfx::DataSourceSurface> GetAsSurface(
      gfx::DataSourceSurface* aSurface) override {
    return nullptr;
  }

  gfx::YUVColorSpace GetYUVColorSpace() const override;
  gfx::ColorDepth GetColorDepth() const override;
  gfx::ColorRange GetColorRange() const override;
  gfx::TransferFunction GetTransferFunction() const override;

  gfx::IntSize GetSize() const override;

  bool IsValid() override;

#ifdef MOZ_LAYERS_HAVE_LOG
  const char* Name() override { return "RemoteTextureHostWrapper"; }
#endif

  void CreateRenderTexture(
      const wr::ExternalImageId& aExternalImageId) override;

  uint32_t NumSubTextures() override;

  void PushResourceUpdates(wr::TransactionBuilder& aResources,
                           ResourceUpdateOp aOp,
                           const Range<wr::ImageKey>& aImageKeys,
                           const wr::ExternalImageId& aExtID) override;

  void PushDisplayItems(wr::DisplayListBuilder& aBuilder,
                        const wr::LayoutRect& aBounds,
                        const wr::LayoutRect& aClip, wr::ImageRendering aFilter,
                        const Range<wr::ImageKey>& aImageKeys,
                        PushDisplayItemFlagSet aFlags) override;

  bool SupportsExternalCompositing(WebRenderBackend aBackend) override;

  void UnbindTextureSource() override;

  void NotifyNotUsed() override;

  void SetReadFence(Fence* aReadFence) override;

  RemoteTextureHostWrapper* AsRemoteTextureHostWrapper() override {
    return this;
  }

  TextureHostType GetTextureHostType() override;

  bool IsWrappingSurfaceTextureHost() override;

  bool NeedsDeferredDeletion() const override;

  AndroidHardwareBuffer* GetAndroidHardwareBuffer() const override;

  bool IsReadyForRendering();

  void ApplyTextureFlagsToRemoteTexture();

  void EnableWaitForRemoteTextureOwner(bool aEnable) {
    mWaitForRemoteTextureOwner = true;
  }

  RemoteTextureInfo GetRemoteTextureInfo() const {
    return RemoteTextureInfo(mTextureId, mOwnerId, mForChildID,
                             mWaitForRemoteTextureOwner);
  }

  const RemoteTextureId mTextureId;
  const RemoteTextureOwnerId mOwnerId;
  const GeckoChildID mForChildID;
  const gfx::IntSize mSize;

 protected:
  RemoteTextureHostWrapper(const RemoteTextureId aTextureId,
                           const RemoteTextureOwnerId aOwnerId,
                           const GeckoChildID aForChildID,
                           const gfx::IntSize aSize, const TextureFlags aFlags);
  virtual ~RemoteTextureHostWrapper();
  void MaybeCreateRenderTexture();

  // Called only by RemoteTextureMap
  TextureHost* GetRemoteTextureHost(const MonitorAutoLock& aProofOfLock);
  // Called only by RemoteTextureMap
  void SetRemoteTextureHost(const MonitorAutoLock& aProofOfLock,
                            TextureHost* aTextureHost);
  void ClearRemoteTextureHost(const MonitorAutoLock& aProofOfLock);

  // Updated by RemoteTextureMap
  //
  // Hold compositable ref of remote texture's TextureHost that is used for
  // building WebRender display list. It is TextureHost of mTextureId.
  CompositableTextureHostRef mRemoteTexture;

  bool mRenderTextureCreated = false;

  bool mWaitForRemoteTextureOwner = false;

  friend class RemoteTextureMap;
};

}  // namespace mozilla::layers

#endif  // MOZILLA_GFX_RemoteTextureHostWrapper_H
