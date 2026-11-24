/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_BUFFERHOST_H
#define MOZILLA_GFX_BUFFERHOST_H

#include <stdint.h>  // for uint64_t
#include <stdio.h>   // for FILE

#include "gfxRect.h"             // for gfxRect
#include "mozilla/Assertions.h"  // for MOZ_ASSERT, etc
#include "mozilla/RefPtr.h"      // for RefPtr, RefCounted, etc
// #include "mozilla/gfx/MatrixFwd.h"  // for Matrix4x4
#include "mozilla/gfx/Polygon.h"  // for Polygon
#include "mozilla/gfx/Rect.h"     // for Rect
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/layers/CompositorTypes.h"  // for TextureInfo, etc
// #include "mozilla/layers/LayersTypes.h"      // for LayerRenderState, etc
#include "Units.h"  // for CSSToScreenScale
#include "mozilla/layers/LayersMessages.h"
#include "mozilla/layers/TextureHost.h"  // for TextureHost
#include "nsCOMPtr.h"                    // for already_AddRefed
#include "nscore.h"                      // for nsACString

namespace mozilla {

namespace layers {

class WebRenderImageHost;

struct ImageCompositeNotificationInfo {
  GeckoChildID mImageBridgeChildID;
  ImageCompositeNotification mNotification;
};

struct AsyncCompositableRef {
  AsyncCompositableRef() : mChildID(kInvalidGeckoChildID) {}
  AsyncCompositableRef(GeckoChildID aChildID, const CompositableHandle& aHandle)
      : mChildID(aChildID), mHandle(aHandle) {}
  explicit operator bool() const { return !!mHandle; }
  GeckoChildID mChildID;
  CompositableHandle mHandle;
};

/**
 * The compositor-side counterpart to CompositableClient. Responsible for
 * updating textures and data about textures from IPC and how textures are
 * composited (tiling, double buffering, etc.).
 *
 * Update (for images/canvases) and UpdateThebes (for Thebes) are called during
 * the layers transaction to update the Compositbale's textures from the
 * content side. The actual update (and any syncronous upload) is done by the
 * TextureHost, but it is coordinated by the CompositableHost.
 *
 * Composite is called by the owning layer when it is composited.
 * CompositableHost will use its TextureHost(s) and call Compositor::DrawQuad to
 * do the actual rendering.
 */
class CompositableHost {
 protected:
  virtual ~CompositableHost();

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CompositableHost)
  explicit CompositableHost(const TextureInfo& aTextureInfo);

  static already_AddRefed<CompositableHost> Create(
      const TextureInfo& aTextureInfo);

  virtual WebRenderImageHost* AsWebRenderImageHost() { return nullptr; }

  virtual void Dump(std::stringstream& aStream, const char* aPrefix = "",
                    bool aDumpHtml = false) {}
  static void DumpTextureHost(std::stringstream& aStream,
                              TextureHost* aTexture);

  struct TimedTexture {
    CompositableTextureHostRef mTexture;
    TimeStamp mTimeStamp;
    gfx::IntRect mPictureRect;
    int32_t mFrameID;
    int32_t mProducerID;
  };
  virtual void UseTextureHost(const nsTArray<TimedTexture>& aTextures);
  virtual void RemoveTextureHost(TextureHost* aTexture);

  virtual void ClearImages(ClearImagesType aType) {}

  const AsyncCompositableRef& GetAsyncRef() const { return mAsyncRef; }
  void SetAsyncRef(const AsyncCompositableRef& aRef) { mAsyncRef = aRef; }

  /// Called when shutting down the layer tree.
  /// This is a good place to clear all potential gpu resources before the
  /// widget is is destroyed.
  virtual void CleanupResources() {}

  virtual void OnReleased() {}

  virtual uint32_t GetDroppedFrames() { return 0; }

  const TextureInfo mTextureInfo;

 protected:
  AsyncCompositableRef mAsyncRef;
};

}  // namespace layers
}  // namespace mozilla

#endif
