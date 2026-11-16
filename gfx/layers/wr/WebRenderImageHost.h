/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_WEBRENDERIMAGEHOST_H
#define MOZILLA_GFX_WEBRENDERIMAGEHOST_H

#include <deque>
#include <unordered_map>

#include "CompositableHost.h"               // for CompositableHost
#include "mozilla/layers/ImageComposite.h"  // for ImageComposite
#include "mozilla/webrender/RenderTextureHost.h"

namespace mozilla {
namespace layers {

class AsyncImagePipelineManager;
class TextureWrapperD3D11Allocator;
class WebRenderBridgeParent;
class WebRenderBridgeParentRef;

/**
 * ImageHost. Works with ImageClientSingle and ImageClientBuffered
 */
class WebRenderImageHost : public CompositableHost, public ImageComposite {
 public:
  explicit WebRenderImageHost(const TextureInfo& aTextureInfo);
  virtual ~WebRenderImageHost();

  void UseTextureHost(const nsTArray<TimedTexture>& aTextures) override;
  void RemoveTextureHost(TextureHost* aTexture) override;

  void ClearImages(ClearImagesType aType) override;

  void Dump(std::stringstream& aStream, const char* aPrefix = "",
            bool aDumpHtml = false) override;

  void CleanupResources() override;

  void OnReleased() override;

  uint32_t GetDroppedFrames() override { return GetDroppedFramesAndReset(); }

  WebRenderImageHost* AsWebRenderImageHost() override { return this; }

  void PushPendingRemoteTexture(const RemoteTextureId aTextureId,
                                const RemoteTextureOwnerId aOwnerId,
                                const base::ProcessId aForPid,
                                const gfx::IntSize aSize,
                                const TextureFlags aFlags);
  void UseRemoteTexture(bool aCalledInCallback);

  // If a remote texture ready check is pending, wait for the texture to be
  // ready and use it. Returns true if it did.
  bool WaitForPendingRemoteTexture();

  TextureHost* GetAsTextureHostForComposite(
      AsyncImagePipelineManager* aAsyncImageManager);

  void SetWrBridge(const wr::PipelineId& aPipelineId,
                   WebRenderBridgeParent* aWrBridge);

  void ClearWrBridge(const wr::PipelineId& aPipelineId,
                     WebRenderBridgeParent* aWrBridge);

  TextureHost* GetCurrentTextureHost() { return mCurrentTextureHost; }

  void SetRenderTextureHostUsageInfo(
      RefPtr<wr::RenderTextureHostUsageInfo> aUsageInfo) {
    mRenderTextureHostUsageInfo = aUsageInfo;
  }
  RefPtr<wr::RenderTextureHostUsageInfo> GetRenderTextureHostUsageInfo() const {
    return mRenderTextureHostUsageInfo;
  }

 protected:
  // ImageComposite
  TimeStamp GetCompositionTime() const override;
  CompositionOpportunityId GetCompositionOpportunityId() const override;
  void AppendImageCompositeNotification(
      const ImageCompositeNotificationInfo& aInfo) const override;

  void SetCurrentTextureHost(TextureHost* aTexture);

  // Drops the pending remote textures and any ready check waiting for them.
  void ClearPendingRemoteTextures();

  std::unordered_map<uint64_t, RefPtr<WebRenderBridgeParentRef>> mWrBridges;

  AsyncImagePipelineManager* mCurrentAsyncImageManager;

  CompositableTextureHostRef mCurrentTextureHost;

  std::deque<CompositableTextureHostRef> mPendingRemoteTextureWrappers;
  bool mWaitingReadyCallback = false;
  // Identifies the pending ready check, so that stale callbacks are ignored.
  uint64_t mReadyCallbackGeneration = 0;
  RemoteTextureOwnerId mReadyCallbackOwnerId;
  base::ProcessId mReadyCallbackForPid = 0;
  bool mWaitForRemoteTextureOwner = true;

  RefPtr<wr::RenderTextureHostUsageInfo> mRenderTextureHostUsageInfo;

#if XP_WIN
  RefPtr<TextureWrapperD3D11Allocator> mTextureAllocator;
#endif
};

}  // namespace layers
}  // namespace mozilla

#endif  // MOZILLA_GFX_WEBRENDERIMAGEHOST_H
