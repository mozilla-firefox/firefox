/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_RENDERTEXTUREHOST_H
#define MOZILLA_GFX_RENDERTEXTUREHOST_H

#include "GLConsts.h"
#include "GLTypes.h"
#include "mozilla/Atomics.h"
#include "mozilla/RefPtr.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/layers/OverlayInfo.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "mozilla/webrender/webrender_ffi.h"
#include "nsISupportsImpl.h"

namespace mozilla {

namespace gl {
class GLContext;
}
namespace layers {
class TextureSource;
class TextureSourceProvider;
}  // namespace layers

namespace wr {

class RenderEGLImageTextureHost;
class RenderAndroidHardwareBufferTextureHost;
class RenderAndroidImageReaderImageTextureHost;
class RenderAndroidSurfaceTextureHost;
class RenderCompositor;
class RenderDXGITextureHost;
class RenderDXGIYCbCrTextureHost;
class RenderDcompSurfaceTextureHost;
class RenderMacIOSurfaceTextureHost;
class RenderBufferTextureHost;
class RenderTextureHostSWGL;
class RenderTextureHostWrapper;
class RenderDMABUFTextureHost;
class RenderWaylandTextureHost;

void ActivateBindAndTexParameteri(gl::GLContext* aGL, GLenum aActiveTexture,
                                  GLenum aBindTarget, GLuint aBindTexture);

// RenderTextureHostUsageInfo holds information about how the RenderTextureHost
// is used. It is used by AsyncImagePipelineManager to determine how to render
// TextureHost.
class RenderTextureHostUsageInfo final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RenderTextureHostUsageInfo)

  RenderTextureHostUsageInfo() : mCreationTimeStamp(TimeStamp::Now()) {}

  bool VideoOverlayDisabled() { return mVideoOverlayDisabled; }
  void DisableVideoOverlay() { mVideoOverlayDisabled = true; }

  void OnVideoPresent(int aFrameId, uint32_t aDurationMs);
  void OnCompositorEndFrame(int aFrameId, uint32_t aDurationMs);

  const TimeStamp mCreationTimeStamp;

 protected:
  ~RenderTextureHostUsageInfo() = default;

  // RenderTextureHost prefers to disable video overlay.
  Atomic<bool> mVideoOverlayDisabled{false};

  int mVideoPresentFrameId = 0;
  int mSlowPresentCount = 0;
  int mSlowCommitCount = 0;
};

class RenderTextureHost {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RenderTextureHost)

 public:
  RenderTextureHost();

  virtual gfx::SurfaceFormat GetFormat() const {
    return gfx::SurfaceFormat::UNKNOWN;
  }

  virtual gfx::YUVRangedColorSpace GetYUVColorSpace() const {
    return gfx::YUVRangedColorSpace::Default;
  }

  virtual wr::WrExternalImage Lock(uint8_t aChannelIndex, gl::GLContext* aGL);

  virtual void Unlock() {}

  virtual wr::WrExternalImage LockSWGL(uint8_t aChannelIndex, void* aContext,
                                       RenderCompositor* aCompositor);

  virtual void UnlockSWGL() {}

  virtual bool LockSWGLCompositeSurface(void* aContext,
                                        wr::SWGLCompositeSurfaceInfo* aInfo) {
    return false;
  }

  virtual void UnlockSWGLCompositeSurface() {}

  virtual RefPtr<layers::TextureSource> CreateTextureSource(
      layers::TextureSourceProvider* aProvider);

  virtual void ClearCachedResources() {}

  // Called asynchronouly when corresponding TextureHost's mCompositableCount
  // becomes from 0 to 1. For now, it is used only for
  // SurfaceTextureHost/RenderAndroidSurfaceTextureHost.
  virtual void PrepareForUse() {}
  // Called asynchronouly when corresponding TextureHost's is actually going to
  // be used by WebRender. For now, it is used only for
  // SurfaceTextureHost/RenderAndroidSurfaceTextureHost.
  virtual void NotifyForUse() {}
  // Called asynchronouly when corresponding TextureHost's mCompositableCount
  // becomes 0. For now, it is used only for
  // SurfaceTextureHost/RenderAndroidSurfaceTextureHost.
  virtual void NotifyNotUsed() {}
  // Returns true when RenderTextureHost needs SyncObjectHost::Synchronize()
  // call, before its usage.
  virtual bool SyncObjectNeeded() { return false; }
  // Returns true when this texture was generated from a DRM-protected source.
  bool IsFromDRMSource() { return mIsFromDRMSource; }
  void SetIsFromDRMSource(bool aIsFromDRMSource) {
    mIsFromDRMSource = aIsFromDRMSource;
  }

  virtual size_t Bytes() = 0;

  virtual RenderDXGITextureHost* AsRenderDXGITextureHost() { return nullptr; }
  virtual RenderDXGIYCbCrTextureHost* AsRenderDXGIYCbCrTextureHost() {
    return nullptr;
  }

  virtual RenderMacIOSurfaceTextureHost* AsRenderMacIOSurfaceTextureHost() {
    return nullptr;
  }

  virtual RenderEGLImageTextureHost* AsRenderEGLImageTextureHost() {
    return nullptr;
  }

  virtual RenderAndroidHardwareBufferTextureHost*
  AsRenderAndroidHardwareBufferTextureHost() {
    return nullptr;
  }

  virtual RenderAndroidImageReaderImageTextureHost*
  AsRenderAndroidImageReaderImageTextureHost() {
    return nullptr;
  }

  virtual RenderAndroidSurfaceTextureHost* AsRenderAndroidSurfaceTextureHost() {
    return nullptr;
  }

  virtual RenderTextureHostSWGL* AsRenderTextureHostSWGL() { return nullptr; }

  virtual RenderDcompSurfaceTextureHost* AsRenderDcompSurfaceTextureHost() {
    return nullptr;
  }

  virtual RenderDMABUFTextureHost* AsRenderDMABUFTextureHost() {
    return nullptr;
  }

  virtual RenderWaylandTextureHost* AsRenderWaylandTextureHost() {
    return nullptr;
  }

  virtual void Destroy();

  virtual void SetIsSoftwareDecodedVideo() {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
  }
  virtual bool IsSoftwareDecodedVideo() {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return false;
  }

  // Get RenderTextureHostUsageInfo of the RenderTextureHost.
  // If mRenderTextureHostUsageInfo and aUsageInfo are different, merge them to
  // one RenderTextureHostUsageInfo.
  virtual RefPtr<RenderTextureHostUsageInfo> GetOrMergeUsageInfo(
      const MutexAutoLock& aProofOfMapLock,
      RefPtr<RenderTextureHostUsageInfo> aUsageInfo);

  virtual RefPtr<RenderTextureHostUsageInfo> GetTextureHostUsageInfo(
      const MutexAutoLock& aProofOfMapLock);

  virtual void SetReadFenceFd(UniqueFileHandle&& aFenceFd) {}

  void SetDestroyedCallback(std::function<void()>&& aDestroyedCallback) {
    MOZ_ASSERT(!mDestroyedCallback);
    mDestroyedCallback = std::move(aDestroyedCallback);
  }

 protected:
  virtual ~RenderTextureHost();

  bool mIsFromDRMSource;

  // protected by RenderThread::mRenderTextureMapLock
  RefPtr<RenderTextureHostUsageInfo> mRenderTextureHostUsageInfo;
  std::function<void()> mDestroyedCallback;

  friend class RenderTextureHostWrapper;
};

}  // namespace wr
}  // namespace mozilla

#endif  // MOZILLA_GFX_RENDERTEXTUREHOST_H
