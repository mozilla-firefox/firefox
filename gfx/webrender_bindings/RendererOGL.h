/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_RENDEREROGL_H
#define MOZILLA_LAYERS_RENDEREROGL_H

#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/layers/CompositorTypes.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "mozilla/webrender/webrender_ffi.h"
#include "mozilla/webrender/RendererScreenshotGrabber.h"

namespace mozilla {

namespace gfx {
class DrawTarget;
}

namespace gl {
class GLContext;
}

namespace layers {
class CompositorBridgeParent;
class Fence;
class SyncObjectHost;
}  // namespace layers

namespace widget {
class CompositorWidget;
}

namespace wr {

class RenderCompositor;
class RenderTextureHost;

/// Owns the WebRender renderer and GL context.
///
/// There is one renderer per window, all owned by the render thread.
/// This class is a similar abstraction to CompositorOGL except that it is used
/// on the render thread instead of the compositor thread.
class RendererOGL {
  friend wr::WrExternalImage LockExternalImage(void* aObj,
                                               wr::ExternalImageId aId,
                                               uint8_t aChannelIndex,
                                               wr::ImageRendering);
  friend void UnlockExternalImage(void* aObj, wr::ExternalImageId aId,
                                  uint8_t aChannelIndex);

 public:
  wr::WrExternalImageHandler GetExternalImageHandler();

  void SetFramePublishId(FramePublishId aPublishId);

  /// This can be called on the render thread only.
  void Update();

  /// This can be called on the render thread only.
  RenderedFrameId UpdateAndRender(const Maybe<gfx::IntSize>& aReadbackSize,
                                  const Maybe<wr::ImageFormat>& aReadbackFormat,
                                  const Maybe<Range<uint8_t>>& aReadbackBuffer,
                                  bool* aNeedsYFlip,
                                  const wr::FrameReadyParams& aFrameParams,
                                  RendererStats* aOutStats);

  /// This can be called on the render thread only.
  void WaitForGPU();

  /// This can be called on the render thread only.
  RefPtr<layers::Fence> GetAndResetReleaseFence();

  /// This can be called on the render thread only.
  RenderedFrameId GetLastCompletedFrameId();

  /// This can be called on the render thread only.
  RenderedFrameId UpdateFrameId();

  /// This can be called on the render thread only.
  void SetProfilerEnabled(bool aEnabled);

  /// This can be called on the render thread only.
  void SetFrameStartTime(const TimeStamp& aTime);

  /// These can be called on the render thread only.
  void BeginRecording(const TimeStamp& aRecordingStart,
                      wr::PipelineId aPipelineId);
  void MaybeRecordFrame(const WebRenderPipelineInfo* aPipelineInfo);

  Maybe<layers::FrameRecording> EndRecording();

  /// This can be called on the render thread only.
  ~RendererOGL();

  /// This can be called on the render thread only.
  RendererOGL(RefPtr<RenderThread>&& aThread,
              UniquePtr<RenderCompositor> aCompositor, wr::WindowId aWindowId,
              wr::Renderer* aRenderer, layers::CompositorBridgeParent* aBridge);

  /// This can be called on the render thread only.
  void Pause();

  /// This can be called on the render thread only.
  bool Resume();

  /// This can be called on the render thread only.
  bool IsPaused();

  /// This can be called on the render thread only.
  void CheckGraphicsResetStatus(gfx::DeviceResetDetectPlace aPlace,
                                bool aForce);

  layers::SyncObjectHost* GetSyncObject() const;

  layers::CompositorBridgeParent* GetCompositorBridge() { return mBridge; }

  void FlushPipelineInfo();

  RefPtr<const WebRenderPipelineInfo> GetLastPipelineInfo() const {
    return mLastPipelineInfo;
  }

  RenderTextureHost* GetRenderTexture(wr::ExternalImageId aExternalImageId);

  RenderCompositor* GetCompositor() { return mCompositor.get(); }

  void AccumulateMemoryReport(MemoryReport* aReport);

  void SetProfilerUI(const nsACString& aUI);

  wr::Renderer* GetRenderer() { return mRenderer; }

  gl::GLContext* gl() const;

  void* swgl() const;

  bool EnsureAsyncScreenshot();

 protected:
  /**
   * Determine if any content pipelines updated, and update
   * mContentPipelineEpochs.
   */
  bool DidPaintContent(const wr::WebRenderPipelineInfo* aFrameEpochs);

  RefPtr<RenderThread> mThread;
  UniquePtr<RenderCompositor> mCompositor;
  UniquePtr<layers::CompositionRecorder> mCompositionRecorder;  // can be null
  wr::Renderer* mRenderer;
  layers::CompositorBridgeParent* mBridge;
  wr::WindowId mWindowId;
  TimeStamp mFrameStartTime;

  bool mDisableNativeCompositor;

  RendererScreenshotGrabber mScreenshotGrabber;

  // The id of the root WebRender pipeline.
  //
  // All other pipelines are considered content.
  wr::PipelineId mRootPipelineId;

  // A mapping of wr::PipelineId to the epochs when last they updated.
  //
  // We need to use uint64_t here since wr::PipelineId is not default
  // constructable.
  std::unordered_map<uint64_t, wr::Epoch> mContentPipelineEpochs;

  RefPtr<WebRenderPipelineInfo> mLastPipelineInfo;
};

}  // namespace wr
}  // namespace mozilla

#endif
