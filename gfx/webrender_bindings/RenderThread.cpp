/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/task.h"
#include "GeckoProfiler.h"
#include "gfxPlatform.h"
#include "GLContext.h"
#include "RenderThread.h"
#include "nsThread.h"
#include "nsThreadUtils.h"
#include "transport/runnable_utils.h"
#include "mozilla/BackgroundHangMonitor.h"
#include "mozilla/layers/AsyncImagePipelineManager.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/gfx/GPUParent.h"
#include "mozilla/gfx/GPUProcessManager.h"
#include "mozilla/glean/GfxMetrics.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/CompositorManagerParent.h"
#include "mozilla/layers/Fence.h"
#include "mozilla/layers/WebRenderBridgeParent.h"
#include "mozilla/layers/SharedSurfacesParent.h"
#include "mozilla/layers/SurfacePool.h"
#include "mozilla/layers/SynchronousTask.h"
#include "mozilla/PerfStats.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/webrender/RendererOGL.h"
#include "mozilla/webrender/RenderTextureHost.h"
#include "mozilla/widget/CompositorWidget.h"
#include "OGLShaderProgram.h"

#ifdef XP_WIN
#  include "GLContextEGL.h"
#  include "GLLibraryEGL.h"
#  include "mozilla/widget/WinCompositorWindowThread.h"
#  include "mozilla/gfx/DeviceManagerDx.h"
#  include "mozilla/webrender/DCLayerTree.h"
// #  include "nsWindowsHelpers.h"
// #  include <d3d11.h>
#endif

#ifdef MOZ_WIDGET_ANDROID
#  include "GLLibraryEGL.h"
#  include "mozilla/webrender/RenderAndroidSurfaceTextureHost.h"
#endif

#ifdef MOZ_WIDGET_GTK
#  include "mozilla/WidgetUtilsGtk.h"
#  include "GLLibraryEGL.h"
#endif

using namespace mozilla;

static already_AddRefed<gl::GLContext> CreateGLContext(nsACString& aError);

MOZ_DEFINE_MALLOC_SIZE_OF(WebRenderRendererMallocSizeOf)

namespace mozilla::wr {

LazyLogModule gRenderThreadLog("RenderThread");
// Should be called only on RenderThread, since LazyLogModule is not thread safe
#define LOG(...) MOZ_LOG(gRenderThreadLog, LogLevel::Debug, (__VA_ARGS__))

static StaticRefPtr<RenderThread> sRenderThread;
static mozilla::BackgroundHangMonitor* sBackgroundHangMonitor;
#ifdef DEBUG
static bool sRenderThreadEverStarted = false;
#endif
size_t RenderThread::sRendererCount = 0;
size_t RenderThread::sActiveRendererCount = 0;

#if defined(MOZ_WIDGET_ANDROID) || defined(MOZ_WIDGET_GTK)
static bool USE_DEDICATED_GLYPH_RASTER_THREAD = true;
#else
static bool USE_DEDICATED_GLYPH_RASTER_THREAD = false;
#endif

RenderThread::RenderThread(RefPtr<nsIThread> aThread)
    : mThread(std::move(aThread)),
      mThreadPool(false),
      mThreadPoolLP(true),
      mChunkPool(wr_chunk_pool_new()),
      mGlyphRasterThread(USE_DEDICATED_GLYPH_RASTER_THREAD),
      mSingletonGLIsForHardwareWebRender(true),
      mBatteryInfo("RenderThread.mBatteryInfo"),
      mWindowInfos("RenderThread.mWindowInfos"),
      mRenderTextureMapLock("RenderThread.mRenderTextureMapLock"),
      mHasShutdown(false),
      mHandlingDeviceReset(false),
      mHandlingWebRenderError(false) {}

RenderThread::~RenderThread() {
  MOZ_ASSERT(mRenderTexturesDeferred.empty());
  wr_chunk_pool_delete(mChunkPool);
}

// static
RenderThread* RenderThread::Get() { return sRenderThread; }

// static
void RenderThread::Start(uint32_t aNamespace) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!sRenderThread);

#ifdef DEBUG
  // Check to ensure nobody will try to ever start us more than once during
  // the process' lifetime (in particular after ShutDown).
  MOZ_ASSERT(!sRenderThreadEverStarted);
  sRenderThreadEverStarted = true;
#endif

  // When the CanvasRenderer thread is disabled, WebGL may be handled on this
  // thread, requiring a bigger stack size. See: CanvasManagerParent::Init
  //
  // This is 4M, which is higher than the default 256K.
  // Increased with bug 1753349 to accommodate the `chromium/5359` branch of
  // ANGLE, which has large peak stack usage for some pathological shader
  // compilations.
  //
  // Previously increased to 512K to accommodate Mesa in bug 1753340.
  //
  // Previously increased to 320K to avoid a stack overflow in the
  // Intel Vulkan driver initialization in bug 1716120.
  //
  // Note: we only override it if it's limited already.
  uint32_t stackSize = nsIThreadManager::DEFAULT_STACK_SIZE;
  if (stackSize && !gfx::gfxVars::SupportsThreadsafeGL()) {
    stackSize = std::max(stackSize, 4096U << 10);
  }
#if !defined(__OPTIMIZE__)
  // swgl's draw_quad_spans will allocate ~1.5MB in no-opt builds
  // and the default thread stack size on macOS is 512KB
  stackSize = std::max(stackSize, 4 * 1024 * 1024U);
#endif

  RefPtr<nsIThread> thread;
  nsresult rv = NS_NewNamedThread(
      "Renderer", getter_AddRefs(thread),
      NS_NewRunnableFunction(
          "Renderer::BackgroundHanSetup",
          []() {
            sBackgroundHangMonitor = new mozilla::BackgroundHangMonitor(
                "Render",
                /* Timeout values are powers-of-two to enable us get better
                   data. 128ms is chosen for transient hangs because 8Hz should
                   be the minimally acceptable goal for Render
                   responsiveness (normal goal is 60Hz). */
                128,
                /* 2048ms is chosen for permanent hangs because it's longer than
                 * most Render hangs seen in the wild, but is short enough
                 * to not miss getting native hang stacks. */
                2048);
            nsCOMPtr<nsIThread> thread = NS_GetCurrentThread();
            nsThread* nsthread = static_cast<nsThread*>(thread.get());
            nsthread->SetUseHangMonitor(true);
            nsthread->SetPriority(nsISupportsPriority::PRIORITY_HIGH);
          }),
      {.stackSize = stackSize});

  if (NS_FAILED(rv)) {
    gfxCriticalNote << "Failed to create Renderer thread: "
                    << gfx::hexa((uint32_t)rv);
    return;
  }

  sRenderThread = new RenderThread(thread);
  CrashReporter::RegisterAnnotationUSize(
      CrashReporter::Annotation::GraphicsNumRenderers, &sRendererCount);
  CrashReporter::RegisterAnnotationUSize(
      CrashReporter::Annotation::GraphicsNumActiveRenderers,
      &sActiveRendererCount);
#ifdef XP_WIN
  widget::WinCompositorWindowThread::Start();
#endif
  layers::SharedSurfacesParent::Initialize();

  RefPtr<Runnable> runnable = WrapRunnable(
      RefPtr<RenderThread>(sRenderThread.get()), &RenderThread::InitDeviceTask);
  sRenderThread->PostRunnable(runnable.forget());
}

// static
void RenderThread::ShutDown() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(sRenderThread);

  {
    MutexAutoLock lock(sRenderThread->mRenderTextureMapLock);
    sRenderThread->mHasShutdown = true;
  }

  RefPtr<Runnable> runnable = WrapRunnable(
      RefPtr<RenderThread>(sRenderThread.get()), &RenderThread::ShutDownTask);
  sRenderThread->PostRunnable(runnable.forget());

  // This will empty the thread queue and thus run the above runnable while
  // spinning the MT event loop.
  nsCOMPtr<nsIThread> oldThread = sRenderThread->GetRenderThread();
  oldThread->Shutdown();

  layers::SharedSurfacesParent::Shutdown();

#ifdef XP_WIN
  if (widget::WinCompositorWindowThread::Get()) {
    widget::WinCompositorWindowThread::ShutDown();
  }
#endif

  // We null this out only after we finished shutdown to give everbody the
  // chance to check for sRenderThread->mHasShutdown. Hopefully everybody
  // checks this before using us!
  sRenderThread = nullptr;
}

extern void ClearAllBlobImageResources();

void RenderThread::ShutDownTask() {
  MOZ_ASSERT(IsInRenderThread());
  LOG("RenderThread::ShutDownTask()");

  {
    // Clear RenderTextureHosts
    MutexAutoLock lock(mRenderTextureMapLock);
    mRenderTexturesDeferred.clear();
    mRenderTextures.clear();
    mSyncObjectNeededRenderTextures.clear();
    mRenderTextureOps.clear();
  }

  // Let go of our handle to the (internally ref-counted) thread pool.
  mThreadPool.Release();
  mThreadPoolLP.Release();

  // Releasing on the render thread will allow us to avoid dispatching to remove
  // remaining textures from the texture map.
  layers::SharedSurfacesParent::ShutdownRenderThread();

#ifdef XP_WIN
  DCLayerTree::Shutdown();
#endif

  ClearAllBlobImageResources();
  ClearSingletonGL();
  ClearSharedSurfacePool();
}

// static
bool RenderThread::IsInRenderThread() {
  return sRenderThread && sRenderThread->mThread == NS_GetCurrentThread();
}

// static
already_AddRefed<nsIThread> RenderThread::GetRenderThread() {
  nsCOMPtr<nsIThread> thread;
  if (sRenderThread) {
    thread = sRenderThread->mThread;
  }
  return thread.forget();
}

void RenderThread::DoAccumulateMemoryReport(
    MemoryReport aReport,
    const RefPtr<MemoryReportPromise::Private>& aPromise) {
  MOZ_ASSERT(IsInRenderThread());

  for (auto& r : mRenderers) {
    r.second->AccumulateMemoryReport(&aReport);
  }

  // Note memory used by the shader cache, which is shared across all WR
  // instances.
  MOZ_ASSERT(aReport.shader_cache == 0);
  if (mProgramCache) {
    aReport.shader_cache = wr_program_cache_report_memory(
        mProgramCache->Raw(), &WebRenderRendererMallocSizeOf);
  }

  size_t renderTextureMemory = 0;
  {
    MutexAutoLock lock(mRenderTextureMapLock);
    for (const auto& entry : mRenderTextures) {
      renderTextureMemory += entry.second->Bytes();
    }
  }
  aReport.render_texture_hosts = renderTextureMemory;

  aPromise->Resolve(aReport, __func__);
}

// static
RefPtr<MemoryReportPromise> RenderThread::AccumulateMemoryReport(
    MemoryReport aInitial) {
  RefPtr<MemoryReportPromise::Private> p =
      new MemoryReportPromise::Private(__func__);
  MOZ_ASSERT(!IsInRenderThread());
  if (!Get()) {
    // This happens when the GPU process fails to start and we fall back to the
    // basic compositor in the parent process. We could assert against this if
    // we made the webrender detection code in gfxPlatform.cpp smarter. See bug
    // 1494430 comment 12.
    NS_WARNING("No render thread, returning empty memory report");
    p->Resolve(aInitial, __func__);
    return p;
  }

  Get()->PostRunnable(
      NewRunnableMethod<MemoryReport, RefPtr<MemoryReportPromise::Private>>(
          "wr::RenderThread::DoAccumulateMemoryReport", Get(),
          &RenderThread::DoAccumulateMemoryReport, aInitial, p));

  return p;
}

void RenderThread::SetBatteryInfo(const hal::BatteryInformation& aBatteryInfo) {
  MOZ_ASSERT(XRE_IsGPUProcess());

  auto batteryInfo = mBatteryInfo.Lock();
  batteryInfo.ref() = Some(aBatteryInfo);
}

bool RenderThread::GetPowerIsCharging() {
  MOZ_ASSERT(XRE_IsGPUProcess());

  auto batteryInfo = mBatteryInfo.Lock();
  if (batteryInfo.ref().isSome()) {
    return batteryInfo.ref().ref().charging();
  }

  gfxCriticalNoteOnce << "BatteryInfo is not set";
  MOZ_ASSERT_UNREACHABLE("unexpected to be called");
  return false;
}

void RenderThread::AddRenderer(wr::WindowId aWindowId,
                               UniquePtr<RendererOGL> aRenderer) {
  MOZ_ASSERT(IsInRenderThread());
  LOG("RenderThread::AddRenderer() aWindowId %" PRIx64 "", AsUint64(aWindowId));

  if (mHasShutdown) {
    return;
  }

  mRenderers[aWindowId] = std::move(aRenderer);
  sRendererCount = mRenderers.size();

  auto windows = mWindowInfos.Lock();
  windows->emplace(AsUint64(aWindowId), new WindowInfo());
  mWrNotifierEventsQueues.emplace(AsUint64(aWindowId),
                                  new std::queue<WrNotifierEvent>);
}

void RenderThread::RemoveRenderer(wr::WindowId aWindowId) {
  MOZ_ASSERT(IsInRenderThread());
  LOG("RenderThread::RemoveRenderer() aWindowId %" PRIx64 "",
      AsUint64(aWindowId));

  if (mHasShutdown) {
    return;
  }

  mRenderers.erase(aWindowId);
  sRendererCount = mRenderers.size();

  if (mRenderers.empty()) {
    if (mHandlingDeviceReset) {
      ClearSingletonGL();
    }
    mHandlingDeviceReset = false;
    mHandlingWebRenderError = false;
  }

  auto windows = mWindowInfos.Lock();
  auto it = windows->find(AsUint64(aWindowId));
  MOZ_ASSERT(it != windows->end());
  windows->erase(it);

  // Defer std::deque<WrNotifierEvent> remove, RemoveRenderer() is called in
  // HandleWrNotifierEvents().
  RefPtr<Runnable> runnable =
      NS_NewRunnableFunction("RenderThread::RemoveRenderer", [aWindowId]() {
        auto* self = RenderThread::Get();
        auto it = self->mWrNotifierEventsQueues.find(AsUint64(aWindowId));
        if (it == self->mWrNotifierEventsQueues.end()) {
          return;
        }
        self->mWrNotifierEventsQueues.erase(it);
      });
  RenderThread::Get()->PostRunnable(runnable.forget());
}

RendererOGL* RenderThread::GetRenderer(wr::WindowId aWindowId) {
  MOZ_ASSERT(IsInRenderThread());

  auto it = mRenderers.find(aWindowId);
  MOZ_ASSERT(it != mRenderers.end());

  if (it == mRenderers.end()) {
    return nullptr;
  }

  return it->second.get();
}

size_t RenderThread::RendererCount() const {
  MOZ_ASSERT(IsInRenderThread());
  return mRenderers.size();
}

void RenderThread::UpdateActiveRendererCount() {
  MOZ_ASSERT(IsInRenderThread());
  size_t num_active = 0;
  for (const auto& it : mRenderers) {
    if (!it.second->IsPaused()) {
      num_active++;
    }
  }
  sActiveRendererCount = num_active;
}

void RenderThread::WrNotifierEvent_WakeUp(WrWindowId aWindowId,
                                          bool aCompositeNeeded) {
  auto windows = mWindowInfos.Lock();
  auto it = windows->find(AsUint64(aWindowId));
  if (it == windows->end()) {
    MOZ_ASSERT(false);
    return;
  }

  WindowInfo* info = it->second.get();

  info->mPendingWrNotifierEvents.emplace(
      WrNotifierEvent::WakeUp(aCompositeNeeded));
  PostWrNotifierEvents(aWindowId, info);
}

void RenderThread::WrNotifierEvent_NewFrameReady(
    WrWindowId aWindowId, wr::FramePublishId aPublishId,
    const wr::FrameReadyParams* aParams) {
  auto windows = mWindowInfos.Lock();
  auto it = windows->find(AsUint64(aWindowId));
  if (it == windows->end()) {
    MOZ_ASSERT(false);
    return;
  }
  WindowInfo* info = it->second.get();

  info->mPendingWrNotifierEvents.emplace(
      WrNotifierEvent::NewFrameReady(aPublishId, aParams));
  PostWrNotifierEvents(aWindowId, info);
}

void RenderThread::WrNotifierEvent_ExternalEvent(WrWindowId aWindowId,
                                                 size_t aRawEvent) {
  UniquePtr<RendererEvent> evt(reinterpret_cast<RendererEvent*>(aRawEvent));
  {
    auto windows = mWindowInfos.Lock();
    auto it = windows->find(AsUint64(aWindowId));
    if (it == windows->end()) {
      MOZ_ASSERT(false);
      return;
    }
    WindowInfo* info = it->second.get();

    info->mPendingWrNotifierEvents.emplace(
        WrNotifierEvent::ExternalEvent(std::move(evt)));
    PostWrNotifierEvents(aWindowId, info);
  }
}

void RenderThread::PostWrNotifierEvents(WrWindowId aWindowId) {
  {
    auto windows = mWindowInfos.Lock();
    auto it = windows->find(AsUint64(aWindowId));
    if (it == windows->end()) {
      MOZ_ASSERT(false);
      return;
    }
    WindowInfo* info = it->second.get();
    PostWrNotifierEvents(aWindowId, info);
  }
}

void RenderThread::PostWrNotifierEvents(WrWindowId aWindowId,
                                        WindowInfo* aInfo) {
  // Runnable has already been triggered.
  if (aInfo->mWrNotifierEventsRunnable) {
    return;
  }

  // Runnable has not been triggered yet.
  RefPtr<nsIRunnable> runnable = NewRunnableMethod<WrWindowId>(
      "RenderThread::HandleWrNotifierEvents", this,
      &RenderThread::HandleWrNotifierEvents, aWindowId);
  aInfo->mWrNotifierEventsRunnable = runnable;
  PostRunnable(runnable.forget());
}

void RenderThread::HandleWrNotifierEvents(WrWindowId aWindowId) {
  MOZ_ASSERT(IsInRenderThread());

  auto eventsIt = mWrNotifierEventsQueues.find(AsUint64(aWindowId));
  if (eventsIt == mWrNotifierEventsQueues.end()) {
    return;
  }
  auto* events = eventsIt->second.get();

  {
    auto windows = mWindowInfos.Lock();
    auto infoIt = windows->find(AsUint64(aWindowId));
    if (infoIt == windows->end()) {
      MOZ_ASSERT(false);
      return;
    }
    WindowInfo* info = infoIt->second.get();
    info->mWrNotifierEventsRunnable = nullptr;

    if (events->empty() && !info->mPendingWrNotifierEvents.empty()) {
      events->swap(info->mPendingWrNotifierEvents);
    }
  }

  bool handleNext = true;

  while (!events->empty() && handleNext) {
    auto& front = events->front();
    switch (front.mTag) {
      case WrNotifierEvent::Tag::WakeUp:
        WrNotifierEvent_HandleWakeUp(aWindowId, front.FrameReadyParams());
        handleNext = false;
        break;
      case WrNotifierEvent::Tag::NewFrameReady:
        WrNotifierEvent_HandleNewFrameReady(aWindowId, front.PublishId(),
                                            front.FrameReadyParams());
        handleNext = false;
        break;
      case WrNotifierEvent::Tag::ExternalEvent:
        WrNotifierEvent_HandleExternalEvent(aWindowId, front.ExternalEvent());
        break;
    }
    events->pop();
  }

  {
    auto windows = mWindowInfos.Lock();
    auto it = windows->find(AsUint64(aWindowId));
    if (it == windows->end()) {
      return;
    }
    WindowInfo* info = it->second.get();

    if (!events->empty() || !info->mPendingWrNotifierEvents.empty()) {
      PostWrNotifierEvents(aWindowId, info);
    }
  }
}

void RenderThread::WrNotifierEvent_HandleWakeUp(
    wr::WindowId aWindowId, const wr::FrameReadyParams& aParams) {
  MOZ_ASSERT(IsInRenderThread());

  bool isTrackedFrame = false;
  HandleFrameOneDoc(aWindowId, aParams, isTrackedFrame, Nothing());
}

void RenderThread::WrNotifierEvent_HandleNewFrameReady(
    wr::WindowId aWindowId, wr::FramePublishId aPublishId,
    const wr::FrameReadyParams& aParams) {
  MOZ_ASSERT(IsInRenderThread());

  bool isTrackedFrame = true;
  HandleFrameOneDoc(aWindowId, aParams, isTrackedFrame, Some(aPublishId));
}

void RenderThread::WrNotifierEvent_HandleExternalEvent(
    wr::WindowId aWindowId, UniquePtr<RendererEvent> aRendererEvent) {
  MOZ_ASSERT(IsInRenderThread());

  RunEvent(aWindowId, std::move(aRendererEvent), /* aViaWebRender */ true);
}

void RenderThread::BeginRecordingForWindow(wr::WindowId aWindowId,
                                           const TimeStamp& aRecordingStart,
                                           wr::PipelineId aRootPipelineId) {
  MOZ_ASSERT(IsInRenderThread());
  RendererOGL* renderer = GetRenderer(aWindowId);
  MOZ_ASSERT(renderer);

  renderer->BeginRecording(aRecordingStart, aRootPipelineId);
}

Maybe<layers::FrameRecording> RenderThread::EndRecordingForWindow(
    wr::WindowId aWindowId) {
  MOZ_ASSERT(IsInRenderThread());

  RendererOGL* renderer = GetRenderer(aWindowId);
  MOZ_ASSERT(renderer);
  return renderer->EndRecording();
}

void RenderThread::HandleFrameOneDoc(wr::WindowId aWindowId,
                                     const wr::FrameReadyParams& aParams,
                                     bool aTrackedFrame,
                                     Maybe<FramePublishId> aPublishId) {
  MOZ_ASSERT(IsInRenderThread());

  if (mHasShutdown) {
    return;
  }

  HandleFrameOneDocInner(aWindowId, aParams, aTrackedFrame, aPublishId);

  if (aTrackedFrame) {
    DecPendingFrameCount(aWindowId);
  }
}

void RenderThread::HandleFrameOneDocInner(wr::WindowId aWindowId,
                                          const wr::FrameReadyParams& aParams,
                                          bool aTrackedFrame,
                                          Maybe<FramePublishId> aPublishId) {
  if (IsDestroyed(aWindowId)) {
    return;
  }

  if (mHandlingDeviceReset) {
    return;
  }

  PendingFrameInfo frame;
  if (aTrackedFrame) {
    // scope lock
    auto windows = mWindowInfos.Lock();
    auto it = windows->find(AsUint64(aWindowId));
    if (it == windows->end()) {
      MOZ_ASSERT(false);
      return;
    }

    WindowInfo* info = it->second.get();
    PendingFrameInfo& frameInfo = info->mPendingFrames.front();

    frame = frameInfo;
  } else {
    // Just give the frame info default values.
    frame = {TimeStamp::Now(), VsyncId()};
  }

  // Sadly this doesn't include the lock, since we don't have the frame there
  // yet.
  glean::wr::time_to_render_start.AccumulateRawDuration(TimeStamp::Now() -
                                                        frame.mStartTime);

  // It is for ensuring that PrepareForUse() is called before
  // RenderTextureHost::Lock().
  HandleRenderTextureOps();

  if (aPublishId.isSome()) {
    SetFramePublishId(aWindowId, aPublishId.ref());
  }

  RendererStats stats = {0};

  UpdateAndRender(aWindowId, frame.mStartId, frame.mStartTime, aParams,
                  /* aReadbackSize */ Nothing(),
                  /* aReadbackFormat */ Nothing(),
                  /* aReadbackBuffer */ Nothing(), &stats);

  // The start time is from WebRenderBridgeParent::CompositeToTarget. From that
  // point until now (when the frame is finally pushed to the screen) is
  // equivalent to the COMPOSITE_TIME metric in the non-WR codepath.
  TimeDuration compositeDuration = TimeStamp::Now() - frame.mStartTime;
  mozilla::glean::gfx::composite_time.AccumulateRawDuration(compositeDuration);
  PerfStats::RecordMeasurement(PerfStats::Metric::Compositing,
                               compositeDuration);
  if (stats.frame_build_time > 0.0) {
    TimeDuration fbTime =
        TimeDuration::FromMilliseconds(stats.frame_build_time);
    mozilla::glean::wr::framebuild_time.AccumulateRawDuration(fbTime);
    PerfStats::RecordMeasurement(PerfStats::Metric::FrameBuilding, fbTime);
  }
}

void RenderThread::SetClearColor(wr::WindowId aWindowId, wr::ColorF aColor) {
  if (mHasShutdown) {
    return;
  }

  if (!IsInRenderThread()) {
    PostRunnable(NewRunnableMethod<wr::WindowId, wr::ColorF>(
        "wr::RenderThread::SetClearColor", this, &RenderThread::SetClearColor,
        aWindowId, aColor));
    return;
  }

  if (IsDestroyed(aWindowId)) {
    return;
  }

  auto it = mRenderers.find(aWindowId);
  MOZ_ASSERT(it != mRenderers.end());
  if (it != mRenderers.end()) {
    wr_renderer_set_clear_color(it->second->GetRenderer(), aColor);
  }
}

void RenderThread::SetProfilerUI(wr::WindowId aWindowId,
                                 const nsACString& aUI) {
  if (mHasShutdown) {
    return;
  }

  if (!IsInRenderThread()) {
    PostRunnable(NewRunnableMethod<wr::WindowId, nsCString>(
        "wr::RenderThread::SetProfilerUI", this, &RenderThread::SetProfilerUI,
        aWindowId, nsCString(aUI)));
    return;
  }

  auto it = mRenderers.find(aWindowId);
  if (it != mRenderers.end()) {
    it->second->SetProfilerUI(aUI);
  }
}

void RenderThread::PostEvent(wr::WindowId aWindowId,
                             UniquePtr<RendererEvent> aEvent) {
  PostRunnable(
      NewRunnableMethod<wr::WindowId, UniquePtr<RendererEvent>&&, bool>(
          "wr::RenderThread::PostEvent", this, &RenderThread::RunEvent,
          aWindowId, std::move(aEvent), /* aViaWebRender */ false));
}

void RenderThread::RunEvent(wr::WindowId aWindowId,
                            UniquePtr<RendererEvent> aEvent,
                            bool aViaWebRender) {
  MOZ_ASSERT(IsInRenderThread());

#ifndef DEBUG
  const auto maxDurationMs = 2 * 1000;
  const auto start = TimeStamp::Now();
  const auto delayMs = static_cast<uint32_t>(
      (start - aEvent->mCreationTimeStamp).ToMilliseconds());
  // Check for the delay only if RendererEvent is delivered without using
  // WebRender. Its delivery via WebRender can be very slow.
  if (aViaWebRender && (delayMs > maxDurationMs)) {
    gfxCriticalNote << "Calling " << aEvent->Name()
                    << "::Run: is delayed: " << delayMs;
  }
#endif

  aEvent->Run(*this, aWindowId);
  aEvent = nullptr;

#ifndef DEBUG
  const auto end = TimeStamp::Now();
  const auto durationMs = static_cast<uint32_t>((end - start).ToMilliseconds());
  if (durationMs > maxDurationMs) {
    gfxCriticalNote << "NewRenderer::Run is slow: " << durationMs;
  }
#endif
}

static void NotifyDidRender(layers::CompositorBridgeParent* aBridge,
                            const RefPtr<const WebRenderPipelineInfo>& aInfo,
                            VsyncId aCompositeStartId,
                            TimeStamp aCompositeStart, TimeStamp aRenderStart,
                            TimeStamp aEnd, bool aRender,
                            RendererStats aStats) {
  if (aRender && aBridge->GetWrBridge()) {
    // We call this here to mimic the behavior in LayerManagerComposite, as to
    // not change what Talos measures. That is, we do not record an empty frame
    // as a frame.
    aBridge->GetWrBridge()->RecordFrame();
  }

  aBridge->NotifyDidRender(aCompositeStartId, aCompositeStart, aRenderStart,
                           aEnd, &aStats);

  for (const auto& epoch : aInfo->Raw().epochs) {
    aBridge->NotifyPipelineRendered(epoch.pipeline_id, epoch.epoch,
                                    aCompositeStartId, aCompositeStart,
                                    aRenderStart, aEnd, &aStats);
  }

  if (aBridge->GetWrBridge()) {
    aBridge->GetWrBridge()->RetrySkippedComposite();
  }
}

static void NotifyDidStartRender(layers::CompositorBridgeParent* aBridge) {
  if (aBridge->GetWrBridge()) {
    aBridge->GetWrBridge()->RetrySkippedComposite();
  }
}

void RenderThread::SetFramePublishId(wr::WindowId aWindowId,
                                     FramePublishId aPublishId) {
  MOZ_ASSERT(IsInRenderThread());

  auto it = mRenderers.find(aWindowId);
  MOZ_ASSERT(it != mRenderers.end());
  if (it == mRenderers.end()) {
    return;
  }
  auto& renderer = it->second;

  renderer->SetFramePublishId(aPublishId);
}

void RenderThread::UpdateAndRender(
    wr::WindowId aWindowId, const VsyncId& aStartId,
    const TimeStamp& aStartTime, const wr::FrameReadyParams& aParams,
    const Maybe<gfx::IntSize>& aReadbackSize,
    const Maybe<wr::ImageFormat>& aReadbackFormat,
    const Maybe<Range<uint8_t>>& aReadbackBuffer, RendererStats* aStats,
    bool* aNeedsYFlip) {
  AUTO_PROFILER_LABEL("RenderThread::UpdateAndRender", GRAPHICS);
  MOZ_ASSERT(IsInRenderThread());
  MOZ_ASSERT(aParams.render || aReadbackBuffer.isNothing());

  auto it = mRenderers.find(aWindowId);
  MOZ_ASSERT(it != mRenderers.end());
  if (it == mRenderers.end()) {
    return;
  }

  TimeStamp start = TimeStamp::Now();

  auto& renderer = it->second;

  std::string markerName = "Composite #" + std::to_string(AsUint64(aWindowId));
  AutoProfilerTracing tracingCompositeMarker(
      "Paint", markerName.c_str(), geckoprofiler::category::GRAPHICS,
      Some(renderer->GetCompositorBridge()->GetInnerWindowId()));

  bool render = aParams.render;
  if (renderer->IsPaused()) {
    render = false;
  }
  LOG("RenderThread::UpdateAndRender() aWindowId %" PRIx64 " aRender %d",
      AsUint64(aWindowId), render);

  layers::CompositorThread()->Dispatch(
      NewRunnableFunction("NotifyDidStartRenderRunnable", &NotifyDidStartRender,
                          renderer->GetCompositorBridge()));

  wr::RenderedFrameId latestFrameId;
  if (render) {
    latestFrameId = renderer->UpdateAndRender(aReadbackSize, aReadbackFormat,
                                              aReadbackBuffer, aNeedsYFlip,
                                              aParams, aStats);
  } else {
    renderer->Update();
  }
  // Check graphics reset status even when rendering is skipped.
  renderer->CheckGraphicsResetStatus(
      gfx::DeviceResetDetectPlace::WR_POST_UPDATE,
      /* aForce */ false);

  TimeStamp end = TimeStamp::Now();
  RefPtr<const WebRenderPipelineInfo> info = renderer->GetLastPipelineInfo();

  layers::CompositorThread()->Dispatch(
      NewRunnableFunction("NotifyDidRenderRunnable", &NotifyDidRender,
                          renderer->GetCompositorBridge(), info, aStartId,
                          aStartTime, start, end, render, *aStats));

  RefPtr<layers::Fence> fence;

  if (latestFrameId.IsValid()) {
    fence = renderer->GetAndResetReleaseFence();

    // Wait for GPU after posting NotifyDidRender, since the wait is not
    // necessary for the NotifyDidRender.
    // The wait is necessary for Textures recycling of AsyncImagePipelineManager
    // and for avoiding GPU queue is filled with too much tasks.
    // WaitForGPU's implementation is different for each platform.
    auto timerId = glean::wr::gpu_wait_time.Start();
    renderer->WaitForGPU();
    glean::wr::gpu_wait_time.StopAndAccumulate(std::move(timerId));
  } else {
    // Update frame id for NotifyPipelinesUpdated() when rendering does not
    // happen, either because rendering was not requested or the frame was
    // canceled. Rendering can sometimes be canceled if UpdateAndRender is
    // called when the window is not yet ready (not mapped or 0 size).
    latestFrameId = renderer->UpdateFrameId();
  }

  RenderedFrameId lastCompletedFrameId = renderer->GetLastCompletedFrameId();

  RefPtr<layers::AsyncImagePipelineManager> pipelineMgr =
      renderer->GetCompositorBridge()->GetAsyncImagePipelineManager();
  // pipelineMgr should always be non-null here because it is only nulled out
  // after the WebRenderAPI instance for the CompositorBridgeParent is
  // destroyed, and that destruction blocks until the renderer thread has
  // removed the relevant renderer. And after that happens we should never reach
  // this code at all; it would bail out at the mRenderers.find check above.
  MOZ_ASSERT(pipelineMgr);
  pipelineMgr->NotifyPipelinesUpdated(info, latestFrameId, lastCompletedFrameId,
                                      std::move(fence));
}

void RenderThread::Pause(wr::WindowId aWindowId) {
  MOZ_ASSERT(IsInRenderThread());
  LOG("RenderThread::Pause() aWindowId %" PRIx64 "", AsUint64(aWindowId));

  auto it = mRenderers.find(aWindowId);
  MOZ_ASSERT(it != mRenderers.end());
  if (it == mRenderers.end()) {
    gfxCriticalNote << "RenderThread cannot find renderer for window "
                    << gfx::hexa(aWindowId) << " to pause.";
    return;
  }
  auto& renderer = it->second;
  renderer->Pause();

  UpdateActiveRendererCount();
}

bool RenderThread::Resume(wr::WindowId aWindowId) {
  MOZ_ASSERT(IsInRenderThread());
  LOG("enderThread::Resume() aWindowId %" PRIx64 "", AsUint64(aWindowId));

  auto it = mRenderers.find(aWindowId);
  MOZ_ASSERT(it != mRenderers.end());
  if (it == mRenderers.end()) {
    gfxCriticalNote << "RenderThread cannot find renderer for window "
                    << gfx::hexa(aWindowId) << " to resume.";
    return false;
  }
  auto& renderer = it->second;
  bool resumed = renderer->Resume();

  UpdateActiveRendererCount();

  return resumed;
}

void RenderThread::NotifyIdle() {
  if (!IsInRenderThread()) {
    PostRunnable(NewRunnableMethod("RenderThread::NotifyIdle", this,
                                   &RenderThread::NotifyIdle));

    return;
  }

  wr_chunk_pool_purge(mChunkPool);
}

bool RenderThread::TooManyPendingFrames(wr::WindowId aWindowId) {
  const int64_t maxFrameCount = 1;

  // Too many pending frames if pending frames exit more than maxFrameCount
  // or if RenderBackend is still processing a frame.

  auto windows = mWindowInfos.Lock();
  auto it = windows->find(AsUint64(aWindowId));
  if (it == windows->end()) {
    MOZ_ASSERT(false);
    return true;
  }
  WindowInfo* info = it->second.get();

  if (info->PendingCount() > maxFrameCount) {
    return true;
  }
  // If there is no ongoing frame build, we accept a new frame.
  return info->mPendingFrameBuild > 0;
}

bool RenderThread::IsDestroyed(wr::WindowId aWindowId) {
  auto windows = mWindowInfos.Lock();
  auto it = windows->find(AsUint64(aWindowId));
  if (it == windows->end()) {
    return true;
  }

  return it->second->mIsDestroyed;
}

void RenderThread::SetDestroyed(wr::WindowId aWindowId) {
  auto windows = mWindowInfos.Lock();
  auto it = windows->find(AsUint64(aWindowId));
  if (it == windows->end()) {
    MOZ_ASSERT(false);
    return;
  }
  it->second->mIsDestroyed = true;
}

void RenderThread::IncPendingFrameCount(wr::WindowId aWindowId,
                                        const VsyncId& aStartId,
                                        const TimeStamp& aStartTime) {
  auto windows = mWindowInfos.Lock();
  auto it = windows->find(AsUint64(aWindowId));
  if (it == windows->end()) {
    MOZ_ASSERT(false);
    return;
  }
  it->second->mPendingFrameBuild++;
  it->second->mPendingFrames.push(PendingFrameInfo{aStartTime, aStartId});
}

void RenderThread::DecPendingFrameBuildCount(wr::WindowId aWindowId) {
  auto windows = mWindowInfos.Lock();
  auto it = windows->find(AsUint64(aWindowId));
  if (it == windows->end()) {
    MOZ_ASSERT(false);
    return;
  }
  WindowInfo* info = it->second.get();
  MOZ_RELEASE_ASSERT(info->mPendingFrameBuild >= 1);
  info->mPendingFrameBuild--;
}

void RenderThread::DecPendingFrameCount(wr::WindowId aWindowId) {
  auto windows = mWindowInfos.Lock();
  auto it = windows->find(AsUint64(aWindowId));
  if (it == windows->end()) {
    MOZ_ASSERT(false);
    return;
  }
  WindowInfo* info = it->second.get();
  info->mPendingFrames.pop();
}

void RenderThread::RegisterExternalImage(
    const wr::ExternalImageId& aExternalImageId,
    already_AddRefed<RenderTextureHost> aTexture) {
  MutexAutoLock lock(mRenderTextureMapLock);

  if (mHasShutdown) {
    return;
  }
  MOZ_ASSERT(mRenderTextures.find(aExternalImageId) == mRenderTextures.end());
  RefPtr<RenderTextureHost> texture = aTexture;
  if (texture->SyncObjectNeeded()) {
    mSyncObjectNeededRenderTextures.emplace(aExternalImageId, texture);
  }
  mRenderTextures.emplace(aExternalImageId, texture);

#ifdef DEBUG
  int32_t maxAllowedIncrease =
      StaticPrefs::gfx_testing_assert_render_textures_increase();

  if (maxAllowedIncrease <= 0) {
    mRenderTexturesLastTime = -1;
  } else {
    if (mRenderTexturesLastTime < 0) {
      mRenderTexturesLastTime = static_cast<int32_t>(mRenderTextures.size());
    }
    MOZ_ASSERT((static_cast<int32_t>(mRenderTextures.size()) -
                mRenderTexturesLastTime) < maxAllowedIncrease);
  }
#endif
}

void RenderThread::UnregisterExternalImage(
    const wr::ExternalImageId& aExternalImageId) {
  MutexAutoLock lock(mRenderTextureMapLock);
  if (mHasShutdown) {
    return;
  }
  auto it = mRenderTextures.find(aExternalImageId);
  if (it == mRenderTextures.end()) {
    return;
  }

  auto& texture = it->second;
  if (texture->SyncObjectNeeded()) {
    MOZ_RELEASE_ASSERT(
        mSyncObjectNeededRenderTextures.erase(aExternalImageId) == 1);
  }

  if (!IsInRenderThread()) {
    // The RenderTextureHost should be released in render thread. So, post the
    // deletion task here.
    // The shmem and raw buffer are owned by compositor ipc channel. It's
    // possible that RenderTextureHost is still exist after the shmem/raw buffer
    // deletion. Then the buffer in RenderTextureHost becomes invalid. It's fine
    // for this situation. Gecko will only release the buffer if WR doesn't need
    // it. So, no one will access the invalid buffer in RenderTextureHost.
    RefPtr<RenderTextureHost> texture = it->second;
    mRenderTextures.erase(it);
    mRenderTexturesDeferred.emplace_back(std::move(texture));
    PostRunnable(NewRunnableMethod(
        "RenderThread::DeferredRenderTextureHostDestroy", this,
        &RenderThread::DeferredRenderTextureHostDestroy));
  } else {
    mRenderTextures.erase(it);
  }
}

void RenderThread::DestroyExternalImagesSyncWait(
    const std::vector<wr::ExternalImageId>&& aIds) {
  if (!IsInRenderThread()) {
    layers::SynchronousTask task("Destroy external images");

    RefPtr<Runnable> runnable = NS_NewRunnableFunction(
        "RenderThread::DestroyExternalImagesSyncWait::Runnable",
        [&task, ids = std::move(aIds)]() {
          layers::AutoCompleteTask complete(&task);
          RenderThread::Get()->DestroyExternalImages(std::move(ids));
        });

    PostRunnable(runnable.forget());
    task.Wait();
    return;
  }
  DestroyExternalImages(std::move(aIds));
}

void RenderThread::DestroyExternalImages(
    const std::vector<wr::ExternalImageId>&& aIds) {
  MOZ_ASSERT(IsInRenderThread());

  std::vector<RefPtr<RenderTextureHost>> hosts;
  {
    MutexAutoLock lock(mRenderTextureMapLock);
    if (mHasShutdown) {
      return;
    }

    for (auto& id : aIds) {
      auto it = mRenderTextures.find(id);
      if (it == mRenderTextures.end()) {
        continue;
      }
      hosts.emplace_back(it->second);
    }
  }

  for (auto& host : hosts) {
    host->Destroy();
  }
}

void RenderThread::PrepareForUse(const wr::ExternalImageId& aExternalImageId) {
  AddRenderTextureOp(RenderTextureOp::PrepareForUse, aExternalImageId);
}

void RenderThread::NotifyNotUsed(const wr::ExternalImageId& aExternalImageId) {
  AddRenderTextureOp(RenderTextureOp::NotifyNotUsed, aExternalImageId);
}

void RenderThread::NotifyForUse(const wr::ExternalImageId& aExternalImageId) {
  AddRenderTextureOp(RenderTextureOp::NotifyForUse, aExternalImageId);
}

void RenderThread::AddRenderTextureOp(
    RenderTextureOp aOp, const wr::ExternalImageId& aExternalImageId) {
  MOZ_ASSERT(!IsInRenderThread());

  MutexAutoLock lock(mRenderTextureMapLock);

  auto it = mRenderTextures.find(aExternalImageId);
  MOZ_ASSERT(it != mRenderTextures.end());
  if (it == mRenderTextures.end()) {
    return;
  }

  RefPtr<RenderTextureHost> texture = it->second;
  mRenderTextureOps.emplace_back(aOp, std::move(texture));

  if (mRenderTextureOpsRunnable) {
    // Runnable was already triggered
    return;
  }

  RefPtr<nsIRunnable> runnable =
      NewRunnableMethod("RenderThread::HandleRenderTextureOps", this,
                        &RenderThread::HandleRenderTextureOps);
  mRenderTextureOpsRunnable = runnable;
  PostRunnable(runnable.forget());
}

void RenderThread::HandleRenderTextureOps() {
  MOZ_ASSERT(IsInRenderThread());

  std::list<std::pair<RenderTextureOp, RefPtr<RenderTextureHost>>>
      renderTextureOps;
  {
    MutexAutoLock lock(mRenderTextureMapLock);
    mRenderTextureOps.swap(renderTextureOps);
    mRenderTextureOpsRunnable = nullptr;
  }

  for (auto& it : renderTextureOps) {
    switch (it.first) {
      case RenderTextureOp::PrepareForUse:
        it.second->PrepareForUse();
        break;
      case RenderTextureOp::NotifyForUse:
        it.second->NotifyForUse();
        break;
      case RenderTextureOp::NotifyNotUsed:
        it.second->NotifyNotUsed();
        break;
    }
  }
}

RefPtr<RenderTextureHostUsageInfo> RenderThread::GetOrMergeUsageInfo(
    const wr::ExternalImageId& aExternalImageId,
    RefPtr<RenderTextureHostUsageInfo> aUsageInfo) {
  MutexAutoLock lock(mRenderTextureMapLock);
  if (mHasShutdown) {
    return nullptr;
  }
  auto it = mRenderTextures.find(aExternalImageId);
  if (it == mRenderTextures.end()) {
    return nullptr;
  }

  auto& texture = it->second;
  return texture->GetOrMergeUsageInfo(lock, aUsageInfo);
}

void RenderThread::UnregisterExternalImageDuringShutdown(
    const wr::ExternalImageId& aExternalImageId) {
  MOZ_ASSERT(IsInRenderThread());
  MutexAutoLock lock(mRenderTextureMapLock);
  MOZ_ASSERT(mHasShutdown);
  mRenderTextures.erase(aExternalImageId);
}

bool RenderThread::SyncObjectNeeded() {
  MOZ_ASSERT(IsInRenderThread());
  MutexAutoLock lock(mRenderTextureMapLock);
  return !mSyncObjectNeededRenderTextures.empty();
}

void RenderThread::DeferredRenderTextureHostDestroy() {
  MutexAutoLock lock(mRenderTextureMapLock);
  mRenderTexturesDeferred.clear();
}

RenderTextureHost* RenderThread::GetRenderTexture(
    const wr::ExternalImageId& aExternalImageId) {
  MutexAutoLock lock(mRenderTextureMapLock);
  auto it = mRenderTextures.find(aExternalImageId);
  MOZ_ASSERT(it != mRenderTextures.end());
  if (it == mRenderTextures.end()) {
    return nullptr;
  }
  return it->second;
}

std::tuple<RenderTextureHost*, RefPtr<RenderTextureHostUsageInfo>>
RenderThread::GetRenderTextureAndUsageInfo(
    const wr::ExternalImageId& aExternalImageId) {
  MutexAutoLock lock(mRenderTextureMapLock);
  auto it = mRenderTextures.find(aExternalImageId);
  MOZ_ASSERT(it != mRenderTextures.end());
  if (it == mRenderTextures.end()) {
    return {};
  }
  return {it->second, it->second->GetTextureHostUsageInfo(lock)};
}

void RenderThread::InitDeviceTask() {
  MOZ_ASSERT(IsInRenderThread());
  MOZ_ASSERT(!mSingletonGL);
  LOG("RenderThread::InitDeviceTask()");

  const auto start = TimeStamp::Now();

  if (gfx::gfxVars::UseSoftwareWebRender()) {
    // Ensure we don't instantiate any shared GL context when SW-WR is used.
    return;
  }

  nsAutoCString err;
  CreateSingletonGL(err);
  if (gfx::gfxVars::UseWebRenderProgramBinaryDisk()) {
    mProgramCache = MakeUnique<WebRenderProgramCache>(ThreadPool().Raw());
  }
  // Query the shared GL context to force the
  // lazy initialization to happen now.
  SingletonGL();

  if (mShaders) {
    // Kick off shader warmup, outside the InitDeviceTask so that this thread
    // becomes available to handle other messages from the Compositor.
    PostResumeShaderWarmupRunnable();
  }

  const auto maxDurationMs = 3 * 1000;
  const auto end = TimeStamp::Now();
  const auto durationMs = static_cast<uint32_t>((end - start).ToMilliseconds());
  if (durationMs > maxDurationMs) {
    gfxCriticalNoteOnce << "RenderThread::InitDeviceTask is slow: "
                        << durationMs;
  }
}

void RenderThread::PostResumeShaderWarmupRunnable() {
  RefPtr<Runnable> runnable =
      NewRunnableMethod("RenderThread::ResumeShaderWarmup", this,
                        &RenderThread::ResumeShaderWarmup);
  PostRunnable(runnable.forget());
}

void RenderThread::ResumeShaderWarmup() {
  if (mShaders) {
    bool needAnotherWarmupStep = mShaders->ResumeWarmup();
    if (needAnotherWarmupStep) {
      PostResumeShaderWarmupRunnable();
    }
  }
}

void RenderThread::PostRunnable(already_AddRefed<nsIRunnable> aRunnable) {
  nsCOMPtr<nsIRunnable> runnable = aRunnable;
  mThread->Dispatch(runnable.forget());
}

void RenderThread::HandleDeviceReset(gfx::DeviceResetDetectPlace aPlace,
                                     gfx::DeviceResetReason aReason) {
  MOZ_ASSERT(IsInRenderThread());

  // This happens only on simulate device reset.
  if (aReason == gfx::DeviceResetReason::FORCED_RESET) {
    if (!mHandlingDeviceReset) {
      mHandlingDeviceReset = true;

      MutexAutoLock lock(mRenderTextureMapLock);
      mRenderTexturesDeferred.clear();
      for (const auto& entry : mRenderTextures) {
        entry.second->ClearCachedResources();
      }

      // All RenderCompositors will be destroyed by the GPUProcessManager in
      // either OnRemoteProcessDeviceReset via the GPUChild, or
      // OnInProcessDeviceReset here directly.
      gfx::GPUProcessManager::GPUProcessManager::NotifyDeviceReset(
          gfx::DeviceResetReason::FORCED_RESET, aPlace);
    }
    return;
  }

  if (mHandlingDeviceReset) {
    return;
  }

  mHandlingDeviceReset = true;

#ifndef XP_WIN
  // On Windows, see DeviceManagerDx::MaybeResetAndReacquireDevices.
  gfx::GPUProcessManager::RecordDeviceReset(aReason);
#endif

  {
    MutexAutoLock lock(mRenderTextureMapLock);
    mRenderTexturesDeferred.clear();
    for (const auto& entry : mRenderTextures) {
      entry.second->ClearCachedResources();
    }
  }

  // All RenderCompositors will be destroyed by the GPUProcessManager in
  // either OnRemoteProcessDeviceReset via the GPUChild, or
  // OnInProcessDeviceReset here directly.
  // On Windows, device will be re-created before sessions re-creation.
  if (XRE_IsGPUProcess()) {
    gfx::GPUProcessManager::GPUProcessManager::NotifyDeviceReset(aReason,
                                                                 aPlace);
  } else {
#ifndef XP_WIN
    // FIXME(aosmond): Do we need to do this on Windows? nsWindow::OnPaint
    // seems to do its own detection for the parent process.
    gfx::GPUProcessManager::GPUProcessManager::NotifyDeviceReset(aReason,
                                                                 aPlace);
#endif
  }
}

bool RenderThread::IsHandlingDeviceReset() {
  MOZ_ASSERT(IsInRenderThread());
  return mHandlingDeviceReset;
}

void RenderThread::SimulateDeviceReset() {
  if (!IsInRenderThread()) {
    PostRunnable(NewRunnableMethod("RenderThread::SimulateDeviceReset", this,
                                   &RenderThread::SimulateDeviceReset));
  } else {
    // When this function is called GPUProcessManager::SimulateDeviceReset()
    // already triggers destroying all CompositorSessions before re-creating
    // them.
    HandleDeviceReset(gfx::DeviceResetDetectPlace::WR_SIMULATE,
                      gfx::DeviceResetReason::FORCED_RESET);
  }
}

static void DoNotifyWebRenderError(WebRenderError aError) {
  layers::CompositorManagerParent::NotifyWebRenderError(aError);
}

void RenderThread::NotifyWebRenderError(WebRenderError aError) {
  MOZ_ASSERT(IsInRenderThread());

  layers::CompositorThread()->Dispatch(NewRunnableFunction(
      "DoNotifyWebRenderErrorRunnable", &DoNotifyWebRenderError, aError));
}

void RenderThread::HandleWebRenderError(WebRenderError aError) {
  MOZ_ASSERT(IsInRenderThread());
  if (mHandlingWebRenderError) {
    return;
  }

  NotifyWebRenderError(aError);

  {
    MutexAutoLock lock(mRenderTextureMapLock);
    mRenderTexturesDeferred.clear();
    for (const auto& entry : mRenderTextures) {
      entry.second->ClearCachedResources();
    }
  }
  mHandlingWebRenderError = true;
  // WebRender is going to be disabled by
  // GPUProcessManager::NotifyWebRenderError()
}

bool RenderThread::IsHandlingWebRenderError() {
  MOZ_ASSERT(IsInRenderThread());
  return mHandlingWebRenderError;
}

gl::GLContext* RenderThread::SingletonGL() {
  nsAutoCString err;
  auto* gl = SingletonGL(err);
  if (!err.IsEmpty()) {
    gfxCriticalNote << err.get();
  }
  return gl;
}

void RenderThread::CreateSingletonGL(nsACString& aError) {
  MOZ_ASSERT(IsInRenderThread());
  LOG("RenderThread::CreateSingletonGL()");

  mSingletonGL = CreateGLContext(aError);
  mSingletonGLIsForHardwareWebRender = !gfx::gfxVars::UseSoftwareWebRender();
}

gl::GLContext* RenderThread::SingletonGL(nsACString& aError) {
  MOZ_ASSERT(IsInRenderThread());
  if (!mSingletonGL) {
    CreateSingletonGL(aError);
    mShaders = nullptr;
  }
  if (mSingletonGL && mSingletonGLIsForHardwareWebRender && !mShaders) {
    mShaders = MakeUnique<WebRenderShaders>(mSingletonGL, mProgramCache.get());
  }

  return mSingletonGL.get();
}

gl::GLContext* RenderThread::SingletonGLForCompositorOGL() {
  MOZ_RELEASE_ASSERT(gfx::gfxVars::UseSoftwareWebRender());

  if (mSingletonGLIsForHardwareWebRender) {
    // Clear singleton GL, since GLContext is for hardware WebRender.
    ClearSingletonGL();
  }
  return SingletonGL();
}

void RenderThread::ClearSingletonGL() {
  MOZ_ASSERT(IsInRenderThread());
  LOG("RenderThread::ClearSingletonGL()");

  if (mSurfacePool) {
    mSurfacePool->DestroyGLResourcesForContext(mSingletonGL);
  }
  if (mProgramsForCompositorOGL) {
    mProgramsForCompositorOGL->Clear();
    mProgramsForCompositorOGL = nullptr;
  }
  mShaders = nullptr;
  mSingletonGL = nullptr;
}

RefPtr<layers::ShaderProgramOGLsHolder>
RenderThread::GetProgramsForCompositorOGL() {
  if (!mSingletonGL) {
    return nullptr;
  }

  if (!mProgramsForCompositorOGL) {
    mProgramsForCompositorOGL =
        MakeAndAddRef<layers::ShaderProgramOGLsHolder>(mSingletonGL);
  }
  return mProgramsForCompositorOGL;
}

RefPtr<layers::SurfacePool> RenderThread::SharedSurfacePool() {
#if defined(XP_DARWIN) || defined(MOZ_WAYLAND)
  if (!mSurfacePool) {
    size_t poolSizeLimit =
        StaticPrefs::gfx_webrender_compositor_surface_pool_size_AtStartup();
    mSurfacePool = layers::SurfacePool::Create(poolSizeLimit);
  }
#endif
  return mSurfacePool;
}

void RenderThread::ClearSharedSurfacePool() { mSurfacePool = nullptr; }

static void GLAPIENTRY DebugMessageCallback(GLenum aSource, GLenum aType,
                                            GLuint aId, GLenum aSeverity,
                                            GLsizei aLength,
                                            const GLchar* aMessage,
                                            const GLvoid* aUserParam) {
  constexpr const char* kContextLost = "Context has been lost.";

  if (StaticPrefs::gfx_webrender_gl_debug_message_critical_note_AtStartup() &&
      aSeverity == LOCAL_GL_DEBUG_SEVERITY_HIGH) {
    auto message = std::string(aMessage, aLength);
    // When content lost happned, error messages are flooded by its message.
    if (message != kContextLost) {
      gfxCriticalNote << message;
    } else {
      gfxCriticalNoteOnce << message;
    }
  }

  if (StaticPrefs::gfx_webrender_gl_debug_message_print_AtStartup()) {
    gl::GLContext* gl = (gl::GLContext*)aUserParam;
    gl->DebugCallback(aSource, aType, aId, aSeverity, aLength, aMessage);
  }
}

// static
void RenderThread::MaybeEnableGLDebugMessage(gl::GLContext* aGLContext) {
  if (!aGLContext) {
    return;
  }

  bool enableDebugMessage =
      StaticPrefs::gfx_webrender_gl_debug_message_critical_note_AtStartup() ||
      StaticPrefs::gfx_webrender_gl_debug_message_print_AtStartup();

  if (enableDebugMessage &&
      aGLContext->IsExtensionSupported(gl::GLContext::KHR_debug)) {
    aGLContext->fEnable(LOCAL_GL_DEBUG_OUTPUT);
    aGLContext->fDisable(LOCAL_GL_DEBUG_OUTPUT_SYNCHRONOUS);
    aGLContext->fDebugMessageCallback(&DebugMessageCallback, (void*)aGLContext);
    aGLContext->fDebugMessageControl(LOCAL_GL_DONT_CARE, LOCAL_GL_DONT_CARE,
                                     LOCAL_GL_DONT_CARE, 0, nullptr, true);
  }
}

WebRenderShaders::WebRenderShaders(gl::GLContext* gl,
                                   WebRenderProgramCache* programCache) {
  mGL = gl;
  mShaders =
      wr_shaders_new(gl, programCache ? programCache->Raw() : nullptr,
                     StaticPrefs::gfx_webrender_precache_shaders_AtStartup());
}

WebRenderShaders::~WebRenderShaders() {
  mGL->MakeCurrent();
  wr_shaders_delete(mShaders);
}

bool WebRenderShaders::ResumeWarmup() {
  mGL->MakeCurrent();
  return wr_shaders_resume_warmup(mShaders);
}

WebRenderThreadPool::WebRenderThreadPool(bool low_priority) {
  mThreadPool = wr_thread_pool_new(low_priority);
}

WebRenderThreadPool::~WebRenderThreadPool() { Release(); }

void WebRenderThreadPool::Release() {
  if (mThreadPool) {
    wr_thread_pool_delete(mThreadPool);
    mThreadPool = nullptr;
  }
}

MaybeWebRenderGlyphRasterThread::MaybeWebRenderGlyphRasterThread(bool aEnable) {
  if (aEnable) {
    mThread = wr_glyph_raster_thread_new();
  } else {
    mThread = nullptr;
  }
}

MaybeWebRenderGlyphRasterThread::~MaybeWebRenderGlyphRasterThread() {
  if (mThread) {
    wr_glyph_raster_thread_delete(mThread);
  }
}

WebRenderProgramCache::WebRenderProgramCache(wr::WrThreadPool* aThreadPool) {
  MOZ_ASSERT(aThreadPool);

  nsAutoString path;
  if (gfx::gfxVars::UseWebRenderProgramBinaryDisk()) {
    path.Append(gfx::gfxVars::ProfDirectory());
  }
  mProgramCache = wr_program_cache_new(&path, aThreadPool);
  if (gfx::gfxVars::UseWebRenderProgramBinaryDisk()) {
    wr_try_load_startup_shaders_from_disk(mProgramCache);
  }
}

WebRenderProgramCache::~WebRenderProgramCache() {
  wr_program_cache_delete(mProgramCache);
}

}  // namespace mozilla::wr

#ifdef XP_WIN
static already_AddRefed<gl::GLContext> CreateGLContextANGLE(
    nsACString& aError) {
  const RefPtr<ID3D11Device> d3d11Device =
      gfx::DeviceManagerDx::Get()->GetCompositorDevice();
  if (!d3d11Device) {
    aError.Assign("RcANGLE(no compositor device for EGLDisplay)"_ns);
    return nullptr;
  }

  nsCString failureId;
  const auto lib = gl::GLLibraryEGL::Get(&failureId);
  if (!lib) {
    aError.Assign(
        nsPrintfCString("RcANGLE(load EGL lib failed: %s)", failureId.get()));
    return nullptr;
  }

  const auto egl = lib->CreateDisplay(d3d11Device.get());
  if (!egl) {
    aError.Assign(nsPrintfCString("RcANGLE(create EGLDisplay failed: %s)",
                                  failureId.get()));
    return nullptr;
  }

  gl::CreateContextFlags flags = gl::CreateContextFlags::PREFER_ES3;

  if (StaticPrefs::gfx_webrender_prefer_robustness_AtStartup()) {
    flags |= gl::CreateContextFlags::PREFER_ROBUSTNESS;
  }

  if (egl->IsExtensionSupported(
          gl::EGLExtension::MOZ_create_context_provoking_vertex_dont_care)) {
    flags |= gl::CreateContextFlags::PROVOKING_VERTEX_DONT_CARE;
  }

  // Create GLContext with dummy EGLSurface, the EGLSurface is not used.
  // Instread we override it with EGLSurface of SwapChain's back buffer.

  auto gl = gl::GLContextEGL::CreateWithoutSurface(egl, {flags}, &failureId);
  if (!gl || !gl->IsANGLE()) {
    aError.Assign(nsPrintfCString("RcANGLE(create GL context failed: %p, %s)",
                                  gl.get(), failureId.get()));
    return nullptr;
  }

  if (!gl->MakeCurrent()) {
    aError.Assign(
        nsPrintfCString("RcANGLE(make current GL context failed: %p, %x)",
                        gl.get(), gl->mEgl->mLib->fGetError()));
    return nullptr;
  }

  return gl.forget();
}
#endif

#if defined(MOZ_WIDGET_ANDROID) || defined(MOZ_WIDGET_GTK)
static already_AddRefed<gl::GLContext> CreateGLContextEGL() {
  // Create GLContext with dummy EGLSurface.
  bool forHardwareWebRender = true;
  // SW-WR uses CompositorOGL in native compositor.
  if (gfx::gfxVars::UseSoftwareWebRender()) {
    forHardwareWebRender = false;
  }
  RefPtr<gl::GLContext> gl =
      gl::GLContextProviderEGL::CreateForCompositorWidget(
          nullptr, forHardwareWebRender, /* aForceAccelerated */ true);
  if (!gl || !gl->MakeCurrent()) {
    gfxCriticalNote << "Failed GL context creation for hardware WebRender: "
                    << forHardwareWebRender;
    return nullptr;
  }
  return gl.forget();
}
#endif

#ifdef XP_DARWIN
static already_AddRefed<gl::GLContext> CreateGLContextCGL() {
  nsCString failureUnused;
  return gl::GLContextProvider::CreateHeadless(
      {gl::CreateContextFlags::ALLOW_OFFLINE_RENDERER |
       gl::CreateContextFlags::FORBID_SOFTWARE},
      &failureUnused);
}
#endif

static already_AddRefed<gl::GLContext> CreateGLContext(nsACString& aError) {
  RefPtr<gl::GLContext> gl;

#ifdef XP_WIN
  if (gfx::gfxVars::UseWebRenderANGLE()) {
    gl = CreateGLContextANGLE(aError);
  }
#elif defined(MOZ_WIDGET_ANDROID)
  gl = CreateGLContextEGL();
#elif defined(MOZ_WIDGET_GTK)
  if (gfx::gfxVars::UseEGL()) {
    gl = CreateGLContextEGL();
  }
#elif XP_DARWIN
  gl = CreateGLContextCGL();
#endif

  wr::RenderThread::MaybeEnableGLDebugMessage(gl);

  return gl.forget();
}

extern "C" {

void wr_notifier_wake_up(mozilla::wr::WrWindowId aWindowId,
                         bool aCompositeNeeded) {
  // wake_up is used for things like propagating debug options or memory
  // pressure events, so we are not tracking pending frame counts.
  mozilla::wr::RenderThread::Get()->WrNotifierEvent_WakeUp(aWindowId,
                                                           aCompositeNeeded);
}

void wr_notifier_new_frame_ready(wr::WrWindowId aWindowId,
                                 wr::FramePublishId aPublishId,
                                 const wr::FrameReadyParams* aParams) {
  auto* renderThread = mozilla::wr::RenderThread::Get();
  renderThread->DecPendingFrameBuildCount(aWindowId);

  renderThread->WrNotifierEvent_NewFrameReady(aWindowId, aPublishId, aParams);
}

void wr_notifier_external_event(mozilla::wr::WrWindowId aWindowId,
                                size_t aRawEvent) {
  mozilla::wr::RenderThread::Get()->WrNotifierEvent_ExternalEvent(
      mozilla::wr::WindowId(aWindowId), aRawEvent);
}

static void NotifyScheduleRender(mozilla::wr::WrWindowId aWindowId,
                                 wr::RenderReasons aReasons) {
  RefPtr<mozilla::layers::CompositorBridgeParent> cbp = mozilla::layers::
      CompositorBridgeParent::GetCompositorBridgeParentFromWindowId(aWindowId);
  if (cbp) {
    cbp->ScheduleComposition(aReasons);
  }
}

void wr_schedule_render(mozilla::wr::WrWindowId aWindowId,
                        wr::RenderReasons aReasons) {
  layers::CompositorThread()->Dispatch(NewRunnableFunction(
      "NotifyScheduleRender", &NotifyScheduleRender, aWindowId, aReasons));
}

static void ScheduleFrameAfterSceneBuild(
    mozilla::wr::WrWindowId aWindowId,
    const RefPtr<const wr::WebRenderPipelineInfo>& aInfo) {
  RefPtr<mozilla::layers::CompositorBridgeParent> cbp = mozilla::layers::
      CompositorBridgeParent::GetCompositorBridgeParentFromWindowId(aWindowId);
  if (cbp) {
    cbp->ScheduleFrameAfterSceneBuild(aInfo);
  }
}

void wr_schedule_frame_after_scene_build(
    mozilla::wr::WrWindowId aWindowId,
    mozilla::wr::WrPipelineInfo* aPipelineInfo) {
  RefPtr<wr::WebRenderPipelineInfo> info = new wr::WebRenderPipelineInfo();
  info->Raw() = std::move(*aPipelineInfo);
  layers::CompositorThread()->Dispatch(
      NewRunnableFunction("ScheduleFrameAfterSceneBuild",
                          &ScheduleFrameAfterSceneBuild, aWindowId, info));
}

}  // extern C
