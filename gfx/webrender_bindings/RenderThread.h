/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_RENDERTHREAD_H
#define MOZILLA_LAYERS_RENDERTHREAD_H

#include "base/basictypes.h"       // for DISALLOW_EVIL_CONSTRUCTORS
#include "base/platform_thread.h"  // for PlatformThreadId
#include "base/thread.h"           // for Thread
#include "base/message_loop.h"
#include "GLTypes.h"  // for GLenum
#include "nsISupportsImpl.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/Hal.h"
#include "mozilla/MozPromise.h"
#include "mozilla/DataMutex.h"
#include "mozilla/Maybe.h"
#include "mozilla/webrender/webrender_ffi.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "mozilla/layers/CompositionRecorder.h"
#include "mozilla/layers/SynchronousTask.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/VsyncDispatcher.h"

#include <list>
#include <queue>
#include <unordered_map>

namespace mozilla {
namespace gl {
class GLContext;
}  // namespace gl
namespace layers {
class CompositorBridgeParent;
class Fence;
class ShaderProgramOGLsHolder;
class SurfacePool;
}  // namespace layers
namespace wr {

typedef MozPromise<MemoryReport, bool, true> MemoryReportPromise;

class RendererOGL;
class RenderTextureHost;
class RenderTextureHostUsageInfo;
class RenderThread;

/// A rayon thread pool that is shared by all WebRender instances within a
/// process.
class WebRenderThreadPool {
 public:
  explicit WebRenderThreadPool(bool low_priority);

  ~WebRenderThreadPool();

  wr::WrThreadPool* Raw() {
    // If this pointer is null we are likely at some late shutdown stage,
    // when threads are no longer safe to interact with.
    MOZ_RELEASE_ASSERT(mThreadPool);
    return mThreadPool;
  }

  /// Prematurely destroys this handle to the thread pool.
  /// After calling this the object is useless.
  void Release();

 protected:
  wr::WrThreadPool* mThreadPool;
};

/// An optional dedicated thread for glyph rasterization shared by all WebRender
/// instances within a process.
class MaybeWebRenderGlyphRasterThread {
 public:
  explicit MaybeWebRenderGlyphRasterThread(bool aEnabled);

  ~MaybeWebRenderGlyphRasterThread();

  bool IsEnabled() const { return mThread != nullptr; }

  const wr::WrGlyphRasterThread* Raw() { return mThread; }

 protected:
  wr::WrGlyphRasterThread* mThread;
};

class WebRenderProgramCache final {
 public:
  explicit WebRenderProgramCache(wr::WrThreadPool* aThreadPool);

  ~WebRenderProgramCache();

  wr::WrProgramCache* Raw() { return mProgramCache; }

 protected:
  wr::WrProgramCache* mProgramCache;
};

class WebRenderShaders final {
 public:
  WebRenderShaders(gl::GLContext* gl, WebRenderProgramCache* programCache);
  ~WebRenderShaders();

  // Returns true if ResumeWarmup() should be called again
  bool ResumeWarmup();

  wr::WrShaders* RawShaders() { return mShaders; }

 protected:
  RefPtr<gl::GLContext> mGL;
  wr::WrShaders* mShaders;
};

class WebRenderPipelineInfo final {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(WebRenderPipelineInfo);

  const wr::WrPipelineInfo& Raw() const { return mPipelineInfo; }
  wr::WrPipelineInfo& Raw() { return mPipelineInfo; }

 protected:
  ~WebRenderPipelineInfo() = default;
  wr::WrPipelineInfo mPipelineInfo;
};

/// Base class for an event that can be scheduled to run on the render thread.
///
/// The event can be passed through the same channels as regular WebRender
/// messages to preserve ordering.
class RendererEvent {
 public:
  RendererEvent() : mCreationTimeStamp(TimeStamp::Now()) {}
  virtual ~RendererEvent() = default;
  virtual void Run(RenderThread& aRenderThread, wr::WindowId aWindow) = 0;
  virtual const char* Name() = 0;

  const TimeStamp mCreationTimeStamp;
};

/// The render thread is where WebRender issues all of its GPU work, and as much
/// as possible this thread should only serve this purpose.
///
/// The render thread owns the different RendererOGLs (one per window) and
/// implements the RenderNotifier api exposed by the WebRender bindings.
///
/// Callers are not allowed to post tasks to the render thread's event loop
/// directly and must instead use the RendererEvent mechanism which avoids races
/// between the events and WebRender's own messages.
///
/// The GL context(s) should be created and used on this thread only.
/// XXX - I've tried to organize code so that we can potentially avoid making
/// this a singleton since this bad habit has a tendency to bite us later, but
/// I haven't gotten all the way there either, in order to focus on the more
/// important pieces first. So we are a bit in-between (this is totally a
/// singleton but in some places we pretend it's not). Hopefully we can evolve
/// this in a way that keeps the door open to removing the singleton bits.
class RenderThread final {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DELETE_ON_MAIN_THREAD(RenderThread)

 public:
  /// Can be called from any thread.
  static RenderThread* Get();

  /// Can only be called from the main thread.
  static void Start(uint32_t aNamespace);

  /// Can only be called from the main thread.
  static void ShutDown();

  /// Can be called from any thread.
  static bool IsInRenderThread();

  /// Can be called from any thread.
  static already_AddRefed<nsIThread> GetRenderThread();

  // Can be called from any thread. Dispatches an event to the Renderer thread
  // to iterate over all Renderers, accumulates memory statistics, and resolves
  // the return promise.
  static RefPtr<MemoryReportPromise> AccumulateMemoryReport(
      MemoryReport aInitial);

  /// Can only be called from the render thread.
  void AddRenderer(wr::WindowId aWindowId, UniquePtr<RendererOGL> aRenderer);

  /// Can only be called from the render thread.
  void RemoveRenderer(wr::WindowId aWindowId);

  /// Can only be called from the render thread.
  RendererOGL* GetRenderer(wr::WindowId aWindowId);

  /// Automatically forwarded to the render thread.
  void SetClearColor(wr::WindowId aWindowId, wr::ColorF aColor);

  /// Automatically forwarded to the render thread.
  void SetProfilerUI(wr::WindowId aWindowId, const nsACString& aUI);

  /// Automatically forwarded to the render thread.
  void PipelineSizeChanged(wr::WindowId aWindowId, uint64_t aPipelineId,
                           float aWidth, float aHeight);

  /// Post RendererEvent to the render thread.
  void PostEvent(wr::WindowId aWindowId, UniquePtr<RendererEvent> aEvent);

  /// Can only be called from the render thread.
  void SetFramePublishId(wr::WindowId aWindowId, FramePublishId aPublishId);

  /// Can only be called from the render thread.
  void UpdateAndRender(wr::WindowId aWindowId, const VsyncId& aStartId,
                       const TimeStamp& aStartTime,
                       const wr::FrameReadyParams& aParams,
                       const Maybe<gfx::IntSize>& aReadbackSize,
                       const Maybe<wr::ImageFormat>& aReadbackFormat,
                       const Maybe<Range<uint8_t>>& aReadbackBuffer,
                       RendererStats* aStats, bool* aNeedsYFlip = nullptr);

  void Pause(wr::WindowId aWindowId);
  bool Resume(wr::WindowId aWindowId);
  void NotifyIdle();

  /// Can be called from any thread.
  void RegisterExternalImage(const wr::ExternalImageId& aExternalImageId,
                             already_AddRefed<RenderTextureHost> aTexture);

  /// Can be called from any thread.
  void UnregisterExternalImage(const wr::ExternalImageId& aExternalImageId);

  /// Can be called from any thread.
  void DestroyExternalImagesSyncWait(
      const std::vector<wr::ExternalImageId>&& aIds);

  /// Can be called from any thread.
  void PrepareForUse(const wr::ExternalImageId& aExternalImageId);

  /// Can be called from any thread.
  void NotifyNotUsed(const wr::ExternalImageId& aExternalImageId);

  /// Can be called from any thread.
  void NotifyForUse(const wr::ExternalImageId& aExternalImageId);

  void HandleRenderTextureOps();

  /// Can be called from any thread.
  RefPtr<RenderTextureHostUsageInfo> GetOrMergeUsageInfo(
      const wr::ExternalImageId& aExternalImageId,
      RefPtr<RenderTextureHostUsageInfo> aUsageInfo);

  /// Can only be called from the render thread.
  void UnregisterExternalImageDuringShutdown(
      const wr::ExternalImageId& aExternalImageId);

  /// Can only be called from the render thread.
  RenderTextureHost* GetRenderTexture(
      const wr::ExternalImageId& aExternalImageId);

  /// Can only be called from the render thread.
  std::tuple<RenderTextureHost*, RefPtr<RenderTextureHostUsageInfo>>
  GetRenderTextureAndUsageInfo(const wr::ExternalImageId& aExternalImageId);

  /// Can be called from any thread.
  bool IsDestroyed(wr::WindowId aWindowId);
  /// Can be called from any thread.
  void SetDestroyed(wr::WindowId aWindowId);
  /// Can be called from any thread.
  bool TooManyPendingFrames(wr::WindowId aWindowId);
  /// Can be called from any thread.
  void IncPendingFrameCount(wr::WindowId aWindowId, const VsyncId& aStartId,
                            const TimeStamp& aStartTime);
  /// Can be called from any thread.
  void DecPendingFrameBuildCount(wr::WindowId aWindowId);
  void DecPendingFrameCount(wr::WindowId aWindowId);

  // RenderNotifier implementation
  void WrNotifierEvent_WakeUp(WrWindowId aWindowId, bool aCompositeNeeded);
  void WrNotifierEvent_NewFrameReady(WrWindowId aWindowId,
                                     wr::FramePublishId aPublishId,
                                     const wr::FrameReadyParams* aParams);
  void WrNotifierEvent_ExternalEvent(WrWindowId aWindowId, size_t aRawEvent);

  /// Can be called from any thread.
  WebRenderThreadPool& ThreadPool() { return mThreadPool; }

  /// Thread pool for low priority scene building
  /// Can be called from any thread.
  WebRenderThreadPool& ThreadPoolLP() { return mThreadPoolLP; }

  /// A pool of large memory chunks used by the per-frame allocators.
  WrChunkPool* MemoryChunkPool() { return mChunkPool; }

  /// Optional global glyph raster thread.
  /// Can be called from any thread.
  MaybeWebRenderGlyphRasterThread& GlyphRasterThread() {
    return mGlyphRasterThread;
  }

  /// Returns the cache used to serialize shader programs to disk, if enabled.
  ///
  /// Can only be called from the render thread.
  WebRenderProgramCache* GetProgramCache() {
    MOZ_ASSERT(IsInRenderThread());
    return mProgramCache.get();
  }

  /// Can only be called from the render thread.
  WebRenderShaders* GetShaders() {
    MOZ_ASSERT(IsInRenderThread());
    return mShaders.get();
  }

  /// Can only be called from the render thread.
  gl::GLContext* SingletonGL(nsACString& aError);
  gl::GLContext* SingletonGL();
  gl::GLContext* SingletonGLForCompositorOGL();
  void ClearSingletonGL();
  RefPtr<layers::SurfacePool> SharedSurfacePool();
  void ClearSharedSurfacePool();

  RefPtr<layers::ShaderProgramOGLsHolder> GetProgramsForCompositorOGL();

  /// Can only be called from the render thread.
  void HandleDeviceReset(gfx::DeviceResetDetectPlace aPlace,
                         gfx::DeviceResetReason aReason);
  /// Can only be called from the render thread.
  bool IsHandlingDeviceReset();
  /// Can be called from any thread.
  void SimulateDeviceReset();

  /// Can only be called from the render thread.
  void NotifyWebRenderError(WebRenderError aError);

  /// Can only be called from the render thread.
  void HandleWebRenderError(WebRenderError aError);
  /// Can only be called from the render thread.
  bool IsHandlingWebRenderError();

  /// Can only be called from the render thread.
  bool SyncObjectNeeded();

  size_t RendererCount() const;
  size_t ActiveRendererCount() const { return sActiveRendererCount; };
  void UpdateActiveRendererCount();

  void BeginRecordingForWindow(wr::WindowId aWindowId,
                               const TimeStamp& aRecordingStart,
                               wr::PipelineId aRootPipelineId);

  Maybe<layers::FrameRecording> EndRecordingForWindow(wr::WindowId aWindowId);

  static void MaybeEnableGLDebugMessage(gl::GLContext* aGLContext);

  void SetBatteryInfo(const hal::BatteryInformation& aBatteryInfo);
  bool GetPowerIsCharging();

 private:
  static size_t sRendererCount;
  static size_t sActiveRendererCount;

  enum class RenderTextureOp {
    PrepareForUse,
    NotifyForUse,
    NotifyNotUsed,
  };
  class WrNotifierEvent {
   public:
    enum class Tag {
      WakeUp,
      NewFrameReady,
      ExternalEvent,
    };
    const Tag mTag;

   private:
    WrNotifierEvent(const Tag aTag, wr::FramePublishId aPublishId,
                    wr::FrameReadyParams aParams)
        : mTag(aTag), mPublishId(aPublishId), mParams(aParams) {
      MOZ_ASSERT(mTag == Tag::NewFrameReady);
    }
    WrNotifierEvent(const Tag aTag, wr::FrameReadyParams aParams)
        : mTag(aTag), mParams(aParams) {
      MOZ_ASSERT(mTag == Tag::WakeUp);
    }
    WrNotifierEvent(const Tag aTag, UniquePtr<RendererEvent>&& aRendererEvent)
        : mTag(aTag), mRendererEvent(std::move(aRendererEvent)) {
      MOZ_ASSERT(mTag == Tag::ExternalEvent);
    }

    const wr::FramePublishId mPublishId = wr::FramePublishId::INVALID;
    const wr::FrameReadyParams mParams = {
        .present = false,
        .render = false,
        .scrolled = false,
    };
    UniquePtr<RendererEvent> mRendererEvent;

   public:
    static WrNotifierEvent WakeUp(const bool aCompositeNeeded) {
      wr::FrameReadyParams params = {
          .present = aCompositeNeeded,
          .render = aCompositeNeeded,
          .scrolled = false,
      };
      return WrNotifierEvent(Tag::WakeUp, params);
    }

    static WrNotifierEvent NewFrameReady(FramePublishId aPublishId,
                                         const wr::FrameReadyParams* aParams) {
      return WrNotifierEvent(Tag::NewFrameReady, aPublishId, *aParams);
    }

    static WrNotifierEvent ExternalEvent(
        UniquePtr<RendererEvent>&& aRendererEvent) {
      return WrNotifierEvent(Tag::ExternalEvent, std::move(aRendererEvent));
    }

    const wr::FrameReadyParams& FrameReadyParams() const {
      MOZ_ASSERT(mTag == Tag::NewFrameReady || mTag == Tag::WakeUp,
                 "Unexpected NotiferEvent tag");
      return mParams;
    }
    FramePublishId PublishId() {
      if (mTag == Tag::NewFrameReady) {
        return mPublishId;
      }
      MOZ_ASSERT_UNREACHABLE("Unexpected NotiferEvent tag");
      return FramePublishId::INVALID;
    }
    UniquePtr<RendererEvent> ExternalEvent() {
      if (mTag == Tag::ExternalEvent) {
        MOZ_ASSERT(mRendererEvent);
        return std::move(mRendererEvent);
      }
      MOZ_ASSERT_UNREACHABLE("Unexpected NotiferEvent tag");
      return nullptr;
    }
  };

  explicit RenderThread(RefPtr<nsIThread> aThread);

  void HandleFrameOneDocInner(wr::WindowId aWindowId,
                              const wr::FrameReadyParams& aParams,
                              bool aTrackedFrame,
                              Maybe<FramePublishId> aPublishId);

  void DeferredRenderTextureHostDestroy();
  void ShutDownTask();
  void InitDeviceTask();
  void PostResumeShaderWarmupRunnable();
  void ResumeShaderWarmup();
  void HandleFrameOneDoc(wr::WindowId aWindowId, const wr::FrameReadyParams&,
                         bool aTrackedFrame, Maybe<FramePublishId> aPublishId);
  void RunEvent(wr::WindowId aWindowId, UniquePtr<RendererEvent> aEvent,
                bool aViaWebRender);
  void PostRunnable(already_AddRefed<nsIRunnable> aRunnable);

  void DoAccumulateMemoryReport(MemoryReport,
                                const RefPtr<MemoryReportPromise::Private>&);

  void AddRenderTextureOp(RenderTextureOp aOp,
                          const wr::ExternalImageId& aExternalImageId);

  void CreateSingletonGL(nsACString& aError);

  void DestroyExternalImages(const std::vector<wr::ExternalImageId>&& aIds);

  struct WindowInfo;

  void PostWrNotifierEvents(WrWindowId aWindowId);
  void PostWrNotifierEvents(WrWindowId aWindowId, WindowInfo* aInfo);
  void HandleWrNotifierEvents(WrWindowId aWindowId);
  void WrNotifierEvent_HandleWakeUp(wr::WindowId aWindowId,
                                    const wr::FrameReadyParams& aParams);
  void WrNotifierEvent_HandleNewFrameReady(wr::WindowId aWindowId,
                                           wr::FramePublishId aPublishId,
                                           const wr::FrameReadyParams& aParams);
  void WrNotifierEvent_HandleExternalEvent(
      wr::WindowId aWindowId, UniquePtr<RendererEvent> aRendererEvent);

  ~RenderThread();

  RefPtr<nsIThread> const mThread;

  WebRenderThreadPool mThreadPool;
  WebRenderThreadPool mThreadPoolLP;
  WrChunkPool* mChunkPool;
  MaybeWebRenderGlyphRasterThread mGlyphRasterThread;

  UniquePtr<WebRenderProgramCache> mProgramCache;
  UniquePtr<WebRenderShaders> mShaders;
  RefPtr<layers::ShaderProgramOGLsHolder> mProgramsForCompositorOGL;

  // An optional shared GLContext to be used for all
  // windows.
  RefPtr<gl::GLContext> mSingletonGL;
  bool mSingletonGLIsForHardwareWebRender;

  RefPtr<layers::SurfacePool> mSurfacePool;

  std::map<wr::WindowId, UniquePtr<RendererOGL>> mRenderers;

  DataMutex<Maybe<hal::BatteryInformation>> mBatteryInfo;

  struct PendingFrameInfo {
    TimeStamp mStartTime;
    VsyncId mStartId;
  };

  struct WindowInfo {
    int64_t PendingCount() { return mPendingFrames.size(); }
    std::queue<PendingFrameInfo> mPendingFrames;
    uint8_t mPendingFrameBuild = 0;
    bool mIsDestroyed = false;
    RefPtr<nsIRunnable> mWrNotifierEventsRunnable;
    std::queue<WrNotifierEvent> mPendingWrNotifierEvents;
  };

  DataMutex<std::unordered_map<uint64_t, UniquePtr<WindowInfo>>> mWindowInfos;

  std::unordered_map<uint64_t, UniquePtr<std::queue<WrNotifierEvent>>>
      mWrNotifierEventsQueues;

  struct ExternalImageIdHashFn {
    std::size_t operator()(const wr::ExternalImageId& aId) const {
      return HashGeneric(wr::AsUint64(aId));
    }
  };

  Mutex mRenderTextureMapLock;
  std::unordered_map<wr::ExternalImageId, RefPtr<RenderTextureHost>,
                     ExternalImageIdHashFn>
      mRenderTextures MOZ_GUARDED_BY(mRenderTextureMapLock);
  std::unordered_map<wr::ExternalImageId, RefPtr<RenderTextureHost>,
                     ExternalImageIdHashFn>
      mSyncObjectNeededRenderTextures MOZ_GUARDED_BY(mRenderTextureMapLock);
  std::list<std::pair<RenderTextureOp, RefPtr<RenderTextureHost>>>
      mRenderTextureOps MOZ_GUARDED_BY(mRenderTextureMapLock);

  // Used to remove all RenderTextureHost that are going to be removed by
  // a deferred callback and remove them right away without waiting for the
  // callback. On device reset we have to remove all GL related resources right
  // away.
  std::list<RefPtr<RenderTextureHost>> mRenderTexturesDeferred
      MOZ_GUARDED_BY(mRenderTextureMapLock);

  RefPtr<nsIRunnable> mRenderTextureOpsRunnable
      MOZ_GUARDED_BY(mRenderTextureMapLock);

#ifdef DEBUG
  // used for tests only to ensure render textures don't increase
  int32_t mRenderTexturesLastTime MOZ_GUARDED_BY(mRenderTextureMapLock) = -1;
#endif

  // Set from MainThread, read from either MainThread or RenderThread
  bool mHasShutdown;

  // Only accessed from the RenderThread
  bool mHandlingDeviceReset;
  bool mHandlingWebRenderError;
};

}  // namespace wr
}  // namespace mozilla

#endif
