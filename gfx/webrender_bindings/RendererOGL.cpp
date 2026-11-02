/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RendererOGL.h"

#include "GLContext.h"
#include "base/task.h"
#include "mozilla/Logging.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/Fence.h"
#include "mozilla/layers/LayersTypes.h"
#include "mozilla/layers/ProfilerScreenshots.h"
#include "mozilla/layers/SyncObject.h"
#include "mozilla/webrender/RenderCompositor.h"
#include "mozilla/webrender/RenderTextureHost.h"
#include "mozilla/widget/CompositorWidget.h"

#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/layers/AndroidHardwareBuffer.h"
#endif

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
                                                    uint8_t aChannelIndex,
                                                    bool aIsComposited) {
  RendererOGL* renderer = reinterpret_cast<RendererOGL*>(aObj);
  RenderTextureHost* texture = renderer->GetRenderTexture(aId);
  MOZ_ASSERT(texture);
  if (!texture) {
    gfxCriticalNoteOnce << "Failed to lock ExternalImage for extId:"
                        << AsUint64(aId);
    return InvalidToWrExternalImage();
  }

  renderer->GetCompositor()->MaybeWaitingForPendingReadFence(texture);

#if defined(MOZ_WAYLAND)
  // Wayland native compositor doesn't use textures for direct compositing.
  if (aIsComposited && texture->AsRenderDMABUFTextureHost() &&
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
      mLastPipelineInfo(new WebRenderPipelineInfo) {
  MOZ_ASSERT(mThread);
  MOZ_ASSERT(mCompositor);
  MOZ_ASSERT(mRenderer);
  MOZ_ASSERT(mBridge);
  MOZ_COUNT_CTOR(RendererOGL);
}

RendererOGL::~RendererOGL() {
  MOZ_COUNT_DTOR(RendererOGL);
#ifdef MOZ_WIDGET_ANDROID
  if (mPendingScreenPixelsRequest) {
    mPendingScreenPixelsRequest->mPromise->Reject(NS_ERROR_ABORT, __func__);
  }
#endif
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

  bool fullRender = false;
  bool needPostRenderCall = false;
  bool beginFrame = !mThread->IsHandlingDeviceReset();

  if (beginFrame && present) {
    if (!mCompositor->GetWidget()->PreRender(&widgetContext)) {
      // XXX This could cause oom in webrender since pending_texture_updates is
      // not handled. It needs to be addressed.
      return RenderedFrameId();
    }
    needPostRenderCall = true;

    // XXX set clear color if MOZ_WIDGET_ANDROID is defined.

    if (aReadbackBuffer.isSome() || layers::ProfilerScreenshots::IsEnabled()) {
      if (mCompositor->UseLayerCompositor()) {
        mCompositor->EnableAsyncScreenshot();
      }
    }

    if (!mCompositor->BeginFrame()) {
      beginFrame = false;
    }

    fullRender = mCompositor->RequestFullRender();
    // When we're rendering to an external target, we want to render everything.
    if (mCompositor->UseWindowPartialPresent() &&
        (aReadbackBuffer.isSome() ||
         layers::ProfilerScreenshots::IsEnabled())) {
      fullRender = true;
    }
  } else if (!mCompositor->MakeCurrent()) {
    // MakeCurrent is otherwise called by mCompositor->BeginFrame above.
    return RenderedFrameId();
  }

  if (!beginFrame) {
    CheckGraphicsResetStatus(gfx::DeviceResetDetectPlace::WR_BEGIN_FRAME,
                             /* aForce */ true);
    if (needPostRenderCall) {
      mCompositor->GetWidget()->PostRender(&widgetContext);
    }
    return RenderedFrameId();
  }

  wr_renderer_update(mRenderer);

  // Order the frame after producers that synchronize with the sync object,
  // once the frame's external images are all known.
  layers::SyncObjectHost* syncObject = mCompositor->GetSyncObject();
  if (syncObject && mThread->SyncObjectNeeded() &&
      !syncObject->Synchronize(/* aFallible */ true)) {
    mThread->HandleDeviceReset(gfx::DeviceResetDetectPlace::WR_SYNC_OBJRCT,
                               gfx::DeviceResetReason::UNKNOWN);
    if (present) {
      mCompositor->CancelFrame();
    }
    if (needPostRenderCall) {
      mCompositor->GetWidget()->PostRender(&widgetContext);
    }
    return RenderedFrameId();
  }

  if (fullRender) {
    wr_renderer_force_redraw(mRenderer);
  }

  LayoutDeviceIntSize size = mCompositor->GetBufferSize();
  auto bufferAge = mCompositor->GetBufferAge();

  nsTArray<DeviceIntRect> dirtyRects;
  bool didRasterize = false;
  bool rendered =
      wr_renderer_render(mRenderer, size.width, size.height, bufferAge,
                         aOutStats, &dirtyRects, &didRasterize);
  FlushPipelineInfo();

  // Track whether any tiles were rasterized for reftest support.
  // Use OR to accumulate - once rasterization is detected, keep it set
  // until explicitly cleared by CheckAndClearDidRasterize().
  mLastFrameDidRasterize = mLastFrameDidRasterize || didRasterize;
  if (!rendered) {
    if (present) {
      mCompositor->CancelFrame();
    }
    if (needPostRenderCall) {
      mCompositor->GetWidget()->PostRender(&widgetContext);
    }
    RenderThread::Get()->HandleWebRenderError(WebRenderError::RENDER);
    return RenderedFrameId();
  }

  RenderedFrameId frameId;

  if (present) {
    if (aReadbackBuffer.isSome()) {
      // Check graphics reset status before readback
      CheckGraphicsResetStatus(gfx::DeviceResetDetectPlace::WR_BEFORE_READBACK,
                               /* aForce */ true);
    }

    if (aReadbackBuffer.isSome() && !mThread->IsHandlingDeviceReset()) {
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

#ifdef MOZ_WIDGET_ANDROID
    MaybeCaptureScreenPixels();
#endif

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
    MOZ_ASSERT(needPostRenderCall);
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
  if (mCompositor->UseLayerCompositor()) {
    return mCompositor->EnableAsyncScreenshot();
  }
  if (mCompositor->SupportAsyncScreenshot()) {
    return true;
  }
  MOZ_ASSERT_UNREACHABLE("unexpected to be called");
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

RefPtr<layers::Fence> RendererOGL::GetAndResetReadFence() {
  return mCompositor->GetAndResetReadFence();
}

RenderedFrameId RendererOGL::GetLastCompletedFrameId() {
  return mCompositor->GetLastCompletedFrameId();
}

RenderedFrameId RendererOGL::UpdateFrameId() {
  return mCompositor->UpdateFrameId();
}

void RendererOGL::Pause() {
  mCompositor->Pause();

  // Only trim on the transition into the paused state. Repeated pause
  // notifications should not generate redundant backend work. Do not use
  // RenderCompositor::IsPaused() here: on GTK it describes surface state and
  // remains false for normal paused windows.
  if (mPausedForResourceTrimming) {
    return;
  }
  mPausedForResourceTrimming = true;

  const uint32_t trimMode =
      StaticPrefs::gfx_webrender_trim_paused_renderers_mode();
  if (trimMode > 0) {
    wr_renderer_trim_transient_resources(mRenderer, trimMode >= 2);
  }
}

bool RendererOGL::Resume() {
  const bool resumed = mCompositor->Resume();
  if (resumed) {
    mPausedForResourceTrimming = false;
  }
  return resumed;
}

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
      std::make_unique<layers::CompositionRecorder>(aRecordingStart);
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

#ifdef MOZ_WIDGET_ANDROID
RefPtr<RendererOGL::ScreenPixelsPromise> RendererOGL::RequestScreenPixels(
    gfx::IntRect aSourceRect,
    RefPtr<layers::AndroidHardwareBuffer> aHardwareBuffer) {
  if (!aHardwareBuffer) {
    return ScreenPixelsPromise::CreateAndReject(NS_ERROR_ILLEGAL_VALUE,
                                                __func__);
  }

  // If a new request is made we no longer care about the result of the previous
  // one, so just reject it if it exists.
  if (mPendingScreenPixelsRequest) {
    mPendingScreenPixelsRequest.extract().mPromise->Reject(NS_ERROR_ABORT,
                                                           __func__);
  }
  mPendingScreenPixelsRequest.emplace(ScreenPixelsRequest{
      .mSourceRect = aSourceRect,
      .mHardwareBuffer = std::move(aHardwareBuffer),
      .mPromise = new ScreenPixelsPromise::Private(__func__),
  });
  return mPendingScreenPixelsRequest->mPromise;
}

void RendererOGL::MaybeCaptureScreenPixels() {
  if (!mPendingScreenPixelsRequest || !EnsureAsyncScreenshot()) {
    return;
  }

  auto request = mPendingScreenPixelsRequest.extract();
  if (mCompositor->CaptureScreenPixels(request.mSourceRect,
                                       request.mHardwareBuffer)) {
    request.mPromise->Resolve(Ok{}, __func__);
  } else {
    request.mPromise->Reject(NS_ERROR_FAILURE, __func__);
  }
}
#endif

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

bool RendererOGL::CheckAndClearDidRasterize() {
  bool result = mLastFrameDidRasterize;
  mLastFrameDidRasterize = false;
  return result;
}

}  // namespace wr
}  // namespace mozilla
