/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RendererOGL.h"

#include "base/task.h"
#include "GLContext.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/Fence.h"
#include "mozilla/layers/LayersTypes.h"
#include "mozilla/layers/ProfilerScreenshots.h"
#include "mozilla/webrender/RenderCompositor.h"
#include "mozilla/webrender/RenderTextureHost.h"
#include "mozilla/widget/CompositorWidget.h"

namespace mozilla {
namespace wr {

class RendererRecordedFrame final : public layers::RecordedFrame {
 public:
  RendererRecordedFrame(const TimeStamp& aTimeStamp, wr::Renderer* aRenderer,
                        const wr::RecordedFrameHandle aHandle,
                        const gfx::IntSize& aSize)
      : RecordedFrame(aTimeStamp),
        mRenderer(aRenderer),
        mSize(aSize),
        mHandle(aHandle) {}

  already_AddRefed<gfx::DataSourceSurface> GetSourceSurface() override {
    if (!mSurface) {
      mSurface = gfx::Factory::CreateDataSourceSurface(
          mSize, gfx::SurfaceFormat::B8G8R8A8, /* aZero = */ false);

      gfx::DataSourceSurface::ScopedMap map(mSurface,
                                            gfx::DataSourceSurface::WRITE);

      if (!wr_renderer_map_recorded_frame(mRenderer, mHandle, map.GetData(),
                                          map.GetStride() * mSize.height,
                                          map.GetStride())) {
        return nullptr;
      }
    }

    return do_AddRef(mSurface);
  }

 private:
  wr::Renderer* mRenderer;
  RefPtr<gfx::DataSourceSurface> mSurface;
  gfx::IntSize mSize;
  wr::RecordedFrameHandle mHandle;
};

wr::WrExternalImage wr_renderer_lock_external_image(void* aObj,
                                                    wr::ExternalImageId aId,
                                                    uint8_t aChannelIndex) {
  RendererOGL* renderer = reinterpret_cast<RendererOGL*>(aObj);
  RenderTextureHost* texture = renderer->GetRenderTexture(aId);
  MOZ_ASSERT(texture);
  if (!texture) {
    gfxCriticalNoteOnce << "Failed to lock ExternalImage for extId:"
                        << AsUint64(aId);
    return InvalidToWrExternalImage();
  }

#if defined(MOZ_WAYLAND)
  // Wayland native compositor doesn't use textures so pass null GL context.
  if (texture->AsRenderDMABUFTextureHost() &&
      renderer->GetCompositor()->CompositorType() ==
          layers::WebRenderCompositor::WAYLAND) {
    return texture->Lock(aChannelIndex, nullptr);
  }
#endif

  if (auto* gl = renderer->gl()) {
    return texture->Lock(aChannelIndex, gl);
  } else if (auto* swgl = renderer->swgl()) {
    return texture->LockSWGL(aChannelIndex, swgl, renderer->GetCompositor());
  } else {
    gfxCriticalNoteOnce
        << "No GL or SWGL context available to lock ExternalImage for extId:"
        << AsUint64(aId);
    return InvalidToWrExternalImage();
  }
}

void wr_renderer_unlock_external_image(void* aObj, wr::ExternalImageId aId,
                                       uint8_t aChannelIndex) {
  RendererOGL* renderer = reinterpret_cast<RendererOGL*>(aObj);
  RenderTextureHost* texture = renderer->GetRenderTexture(aId);
  MOZ_ASSERT(texture);
  if (!texture) {
    return;
  }
  if (renderer->gl()) {
    texture->Unlock();
  } else if (renderer->swgl()) {
    texture->UnlockSWGL();
  }
}

RendererOGL::RendererOGL(RefPtr<RenderThread>&& aThread,
                         UniquePtr<RenderCompositor> aCompositor,
                         wr::WindowId aWindowId, wr::Renderer* aRenderer,
                         layers::CompositorBridgeParent* aBridge)
    : mThread(aThread),
      mCompositor(std::move(aCompositor)),
      mRenderer(aRenderer),
      mBridge(aBridge),
      mWindowId(aWindowId),
      mDisableNativeCompositor(false),
      mLastPipelineInfo(new WebRenderPipelineInfo) {
  MOZ_ASSERT(mThread);
  MOZ_ASSERT(mCompositor);
  MOZ_ASSERT(mRenderer);
  MOZ_ASSERT(mBridge);
  MOZ_COUNT_CTOR(RendererOGL);
}

RendererOGL::~RendererOGL() {
  MOZ_COUNT_DTOR(RendererOGL);
  if (!mCompositor->MakeCurrent()) {
    gfxCriticalNote
        << "Failed to make render context current during destroying.";
    // Leak resources!
  } else {
    wr_renderer_delete(mRenderer);
  }
}

wr::WrExternalImageHandler RendererOGL::GetExternalImageHandler() {
  return wr::WrExternalImageHandler{
      this,
  };
}

void RendererOGL::SetFramePublishId(FramePublishId aPublishId) {
  wr_renderer_set_target_frame_publish_id(mRenderer, aPublishId);
}

void RendererOGL::Update() {
  mCompositor->Update();
  if (mCompositor->MakeCurrent()) {
    wr_renderer_update(mRenderer);
    FlushPipelineInfo();
  }
}

static void DoWebRenderDisableNativeCompositor(
    layers::CompositorBridgeParent* aBridge) {
  aBridge->NotifyWebRenderDisableNativeCompositor();
}

RenderedFrameId RendererOGL::UpdateAndRender(
    const Maybe<gfx::IntSize>& aReadbackSize,
    const Maybe<wr::ImageFormat>& aReadbackFormat,
    const Maybe<Range<uint8_t>>& aReadbackBuffer, bool* aNeedsYFlip,
    const wr::FrameReadyParams& aFrameParams, RendererStats* aOutStats) {
  mozilla::widget::WidgetRenderingContext widgetContext;

#if defined(XP_MACOSX)
  widgetContext.mGL = mCompositor->gl();
#endif

  // If present is false, WebRender needs to render some offscreen content
  // but we don't want to touch the window, so we avoid most interactions
  // with mCompositor.
  bool present = aFrameParams.present;

  LayoutDeviceIntSize size(0, 0);
  auto bufferAge = 0;
  bool fullRender = false;

  bool beginFrame = !mThread->IsHandlingDeviceReset();

  if (beginFrame && present) {
    if (!mCompositor->GetWidget()->PreRender(&widgetContext)) {
      // XXX This could cause oom in webrender since pending_texture_updates is
      // not handled. It needs to be addressed.
      return RenderedFrameId();
    }
    // XXX set clear color if MOZ_WIDGET_ANDROID is defined.

    if (!mCompositor->BeginFrame()) {
      beginFrame = false;
    }

    size = mCompositor->GetBufferSize();
    bufferAge = mCompositor->GetBufferAge();

    fullRender = mCompositor->RequestFullRender();
    // When we're rendering to an external target, we want to render everything.
    if (mCompositor->UsePartialPresent() &&
        (aReadbackBuffer.isSome() ||
         layers::ProfilerScreenshots::IsEnabled())) {
      fullRender = true;
    }
  }

  if (!beginFrame) {
    CheckGraphicsResetStatus(gfx::DeviceResetDetectPlace::WR_BEGIN_FRAME,
                             /* aForce */ true);
    return RenderedFrameId();
  }

  wr_renderer_update(mRenderer);

  if (fullRender) {
    wr_renderer_force_redraw(mRenderer);
  }

  nsTArray<DeviceIntRect> dirtyRects;
  bool rendered = wr_renderer_render(mRenderer, size.width, size.height,
                                     bufferAge, aOutStats, &dirtyRects);
  FlushPipelineInfo();
  if (!rendered) {
    if (present) {
      mCompositor->CancelFrame();
      mCompositor->GetWidget()->PostRender(&widgetContext);
    }
    RenderThread::Get()->HandleWebRenderError(WebRenderError::RENDER);
    return RenderedFrameId();
  }

  RenderedFrameId frameId;

  if (present) {
    if (aReadbackBuffer.isSome()) {
      MOZ_ASSERT(aReadbackSize.isSome());
      MOZ_ASSERT(aReadbackFormat.isSome());
      if (!mCompositor->MaybeReadback(aReadbackSize.ref(),
                                      aReadbackFormat.ref(),
                                      aReadbackBuffer.ref(), aNeedsYFlip)) {
        wr_renderer_readback(mRenderer, aReadbackSize.ref().width,
                             aReadbackSize.ref().height, aReadbackFormat.ref(),
                             &aReadbackBuffer.ref()[0],
                             aReadbackBuffer.ref().length());
        if (aNeedsYFlip != nullptr) {
          *aNeedsYFlip = !mCompositor->SurfaceOriginIsTopLeft();
        }
      }
    }

    if (size.Width() != 0 && size.Height() != 0) {
      if (!mCompositor->MaybeGrabScreenshot(size.ToUnknownSize())) {
        mScreenshotGrabber.MaybeGrabScreenshot(this, size.ToUnknownSize());
      }
    }

    // Frame recording must happen before EndFrame, as we must ensure we read
    // the contents of the back buffer before any calls to SwapBuffers which
    // might invalidate it.
    MaybeRecordFrame(mLastPipelineInfo);
    frameId = mCompositor->EndFrame(dirtyRects);
    mCompositor->GetWidget()->PostRender(&widgetContext);
  }

#if defined(ENABLE_FRAME_LATENCY_LOG)
  if (mFrameStartTime) {
    uint32_t latencyMs =
        round((TimeStamp::Now() - mFrameStartTime).ToMilliseconds());
    printf_stderr("generate frame latencyMs latencyMs %d\n", latencyMs);
  }
  // Clear frame start time
  mFrameStartTime = TimeStamp();
#endif

  if (present) {
    if (!mCompositor->MaybeProcessScreenshotQueue()) {
      mScreenshotGrabber.MaybeProcessQueue(this);
    }
  }

  // TODO: Flush pending actions such as texture deletions/unlocks and
  //       textureHosts recycling.

  return frameId;
}

bool RendererOGL::EnsureAsyncScreenshot() {
  if (mCompositor->SupportAsyncScreenshot()) {
    return true;
  }
  if (!mDisableNativeCompositor) {
    layers::CompositorThread()->Dispatch(
        NewRunnableFunction("DoWebRenderDisableNativeCompositorRunnable",
                            &DoWebRenderDisableNativeCompositor, mBridge));

    mDisableNativeCompositor = true;
    gfxCriticalNote << "Disable native compositor for async screenshot";
  }
  return false;
}

void RendererOGL::CheckGraphicsResetStatus(gfx::DeviceResetDetectPlace aPlace,
                                           bool aForce) {
  if (mCompositor) {
    auto reason = mCompositor->IsContextLost(aForce);
    if (reason != gfx::DeviceResetReason::OK) {
      RenderThread::Get()->HandleDeviceReset(aPlace, reason);
    }
  }
}

void RendererOGL::WaitForGPU() {
  if (!mCompositor->WaitForGPU()) {
    CheckGraphicsResetStatus(gfx::DeviceResetDetectPlace::WR_WAIT_FOR_GPU,
                             /* aForce */ true);
  }
}

RefPtr<layers::Fence> RendererOGL::GetAndResetReleaseFence() {
  return mCompositor->GetAndResetReleaseFence();
}

RenderedFrameId RendererOGL::GetLastCompletedFrameId() {
  return mCompositor->GetLastCompletedFrameId();
}

RenderedFrameId RendererOGL::UpdateFrameId() {
  return mCompositor->UpdateFrameId();
}

void RendererOGL::Pause() { mCompositor->Pause(); }

bool RendererOGL::Resume() { return mCompositor->Resume(); }

bool RendererOGL::IsPaused() { return mCompositor->IsPaused(); }

layers::SyncObjectHost* RendererOGL::GetSyncObject() const {
  return mCompositor->GetSyncObject();
}

gl::GLContext* RendererOGL::gl() const { return mCompositor->gl(); }

void* RendererOGL::swgl() const { return mCompositor->swgl(); }

void RendererOGL::SetFrameStartTime(const TimeStamp& aTime) {
  if (mFrameStartTime) {
    // frame start time is already set. This could happen when multiple
    // generate frame requests are merged by webrender.
    return;
  }
  mFrameStartTime = aTime;
}

void RendererOGL::BeginRecording(const TimeStamp& aRecordingStart,
                                 wr::PipelineId aRootPipelineId) {
  MOZ_ASSERT(!mCompositionRecorder);

  mRootPipelineId = aRootPipelineId;
  mCompositionRecorder =
      MakeUnique<layers::CompositionRecorder>(aRecordingStart);
  mCompositor->MaybeRequestAllowFrameRecording(true);
}

void RendererOGL::MaybeRecordFrame(const WebRenderPipelineInfo* aPipelineInfo) {
  if (!mCompositionRecorder || !EnsureAsyncScreenshot()) {
    return;
  }

  if (!mRenderer || !aPipelineInfo || !DidPaintContent(aPipelineInfo)) {
    return;
  }

  if (mCompositor->MaybeRecordFrame(*mCompositionRecorder)) {
    return;
  }

  wr::RecordedFrameHandle handle{0};
  gfx::IntSize size(0, 0);

  if (wr_renderer_record_frame(mRenderer, wr::ImageFormat::BGRA8, &handle,
                               &size.width, &size.height)) {
    RefPtr<layers::RecordedFrame> frame =
        new RendererRecordedFrame(TimeStamp::Now(), mRenderer, handle, size);

    mCompositionRecorder->RecordFrame(frame);
  }
}

bool RendererOGL::DidPaintContent(const WebRenderPipelineInfo* aFrameEpochs) {
  const wr::WrPipelineInfo& info = aFrameEpochs->Raw();
  bool didPaintContent = false;

  // Check if a non-root pipeline has updated to a new epoch.
  // We treat all non-root pipelines as "content" pipelines, even if they're
  // not fed by content paints, such as videos (see bug 1665512).
  for (const auto& epoch : info.epochs) {
    const wr::PipelineId pipelineId = epoch.pipeline_id;

    if (pipelineId == mRootPipelineId) {
      continue;
    }

    const auto it = mContentPipelineEpochs.find(AsUint64(pipelineId));
    if (it == mContentPipelineEpochs.end() || it->second != epoch.epoch) {
      // This pipeline has updated since last render or has newly rendered.
      didPaintContent = true;
      mContentPipelineEpochs[AsUint64(pipelineId)] = epoch.epoch;
    }
  }

  for (const auto& removedPipeline : info.removed_pipelines) {
    const wr::PipelineId pipelineId = removedPipeline.pipeline_id;
    if (pipelineId == mRootPipelineId) {
      continue;
    }
    mContentPipelineEpochs.erase(AsUint64(pipelineId));
  }

  return didPaintContent;
}

Maybe<layers::FrameRecording> RendererOGL::EndRecording() {
  if (!mCompositionRecorder) {
    MOZ_DIAGNOSTIC_ASSERT(
        false, "Attempted to get frames from a window that was not recording.");
    return Nothing();
  }

  auto maybeRecording = mCompositionRecorder->GetRecording();

  wr_renderer_release_composition_recorder_structures(mRenderer);

  mCompositor->MaybeRequestAllowFrameRecording(false);
  mCompositionRecorder = nullptr;

  return maybeRecording;
}

void RendererOGL::FlushPipelineInfo() {
  RefPtr<WebRenderPipelineInfo> info = new WebRenderPipelineInfo;
  wr_renderer_flush_pipeline_info(mRenderer, &info->Raw());
  mLastPipelineInfo = info;
}

RenderTextureHost* RendererOGL::GetRenderTexture(
    wr::ExternalImageId aExternalImageId) {
  return mThread->GetRenderTexture(aExternalImageId);
}

void RendererOGL::AccumulateMemoryReport(MemoryReport* aReport) {
  wr_renderer_accumulate_memory_report(GetRenderer(), aReport, swgl());

  LayoutDeviceIntSize size = mCompositor->GetBufferSize();

  // Assume BGRA8 for the format since it's not exposed anywhere,
  // and all compositor backends should be using that.
  uintptr_t swapChainSize = size.width * size.height *
                            BytesPerPixel(gfx::SurfaceFormat::B8G8R8A8) *
                            (mCompositor->UseTripleBuffering() ? 3 : 2);
  aReport->swap_chain += swapChainSize;
}

void RendererOGL::SetProfilerUI(const nsACString& aUI) {
  wr_renderer_set_profiler_ui(GetRenderer(), (const uint8_t*)aUI.BeginReading(),
                              aUI.Length());
}

}  // namespace wr
}  // namespace mozilla
