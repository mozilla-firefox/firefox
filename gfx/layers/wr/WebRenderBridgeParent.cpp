/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/WebRenderBridgeParent.h"

#include "mozmemory.h"
#include "CompositableHost.h"
#include "gfxEnv.h"
#include "gfxPlatform.h"
#include "gfxOTSUtils.h"
#include "GeckoProfiler.h"
#include "GLContext.h"
#include "GLContextProvider.h"
#include "GLLibraryLoader.h"
#include "nsExceptionHandler.h"
#include "mozilla/Range.h"
#include "mozilla/EnumeratedRange.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/gfx/GPUParent.h"
#include "mozilla/glean/GfxMetrics.h"
#include "mozilla/layers/AnimationHelper.h"
#include "mozilla/layers/APZSampler.h"
#include "mozilla/layers/APZUpdater.h"
#include "mozilla/layers/Compositor.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/CompositorAnimationStorage.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/CompositorVsyncScheduler.h"
#include "mozilla/layers/ImageBridgeParent.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/IpcResourceUpdateQueue.h"
#include "mozilla/layers/OMTASampler.h"
#include "mozilla/layers/RemoteTextureMap.h"
#include "mozilla/layers/SharedSurfacesParent.h"
#include "mozilla/layers/TextureHost.h"
#include "mozilla/layers/AsyncImagePipelineManager.h"
#include "mozilla/layers/UiCompositorControllerParent.h"
#include "mozilla/layers/WebRenderImageHost.h"
#include "mozilla/layers/WebRenderTextureHost.h"
#include "mozilla/ProfilerMarkerTypes.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Unused.h"
#include "mozilla/webrender/RenderTextureHostSWGL.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/widget/CompositorWidget.h"

#ifdef XP_WIN
#  include "mozilla/gfx/DeviceManagerDx.h"
#  include "mozilla/widget/WinCompositorWidget.h"
#endif
#if defined(MOZ_WIDGET_GTK)
#  include "mozilla/widget/GtkCompositorWidget.h"
#endif

bool is_in_main_thread() { return NS_IsMainThread(); }

bool is_in_compositor_thread() {
  return mozilla::layers::CompositorThreadHolder::IsInCompositorThread();
}

bool is_in_render_thread() {
  return mozilla::wr::RenderThread::IsInRenderThread();
}

bool gecko_profiler_thread_is_being_profiled() {
  return profiler_thread_is_being_profiled(ThreadProfilingFeatures::Any);
}

bool is_glcontext_gles(void* const glcontext_ptr) {
  MOZ_RELEASE_ASSERT(glcontext_ptr);
  return reinterpret_cast<mozilla::gl::GLContext*>(glcontext_ptr)->IsGLES();
}

bool is_glcontext_angle(void* glcontext_ptr) {
  MOZ_ASSERT(glcontext_ptr);

  mozilla::gl::GLContext* glcontext =
      reinterpret_cast<mozilla::gl::GLContext*>(glcontext_ptr);
  if (!glcontext) {
    return false;
  }
  return glcontext->IsANGLE();
}

const char* gfx_wr_resource_path_override() {
  return gfxPlatform::WebRenderResourcePathOverride();
}

bool gfx_wr_use_optimized_shaders() {
  return mozilla::gfx::gfxVars::UseWebRenderOptimizedShaders();
}

void gfx_critical_note(const char* msg) { gfxCriticalNote << msg; }

void gfx_critical_error(const char* msg) { gfxCriticalError() << msg; }

void gecko_printf_stderr_output(const char* msg) { printf_stderr("%s\n", msg); }

void* get_proc_address_from_glcontext(void* glcontext_ptr,
                                      const char* procname) {
  mozilla::gl::GLContext* glcontext =
      reinterpret_cast<mozilla::gl::GLContext*>(glcontext_ptr);
  MOZ_ASSERT(glcontext);
  if (!glcontext) {
    return nullptr;
  }
  const auto& loader = glcontext->GetSymbolLoader();
  MOZ_ASSERT(loader);

  const auto ret = loader->GetProcAddress(procname);
  return reinterpret_cast<void*>(ret);
}

static CrashReporter::Annotation FromWrCrashAnnotation(
    mozilla::wr::CrashAnnotation aAnnotation) {
  switch (aAnnotation) {
    case mozilla::wr::CrashAnnotation::CompileShader:
      return CrashReporter::Annotation::GraphicsCompileShader;
    case mozilla::wr::CrashAnnotation::DrawShader:
      return CrashReporter::Annotation::GraphicsDrawShader;
    default:
      MOZ_ASSERT_UNREACHABLE("Unhandled annotation!");
      return CrashReporter::Annotation::Count;
  }
}

extern "C" {

void gfx_wr_set_crash_annotation(mozilla::wr::CrashAnnotation aAnnotation,
                                 const char* aValue) {
  MOZ_ASSERT(aValue);

  auto annotation = FromWrCrashAnnotation(aAnnotation);
  if (annotation == CrashReporter::Annotation::Count) {
    return;
  }

  CrashReporter::RecordAnnotationCString(annotation, aValue);
}

void gfx_wr_clear_crash_annotation(mozilla::wr::CrashAnnotation aAnnotation) {
  auto annotation = FromWrCrashAnnotation(aAnnotation);
  if (annotation == CrashReporter::Annotation::Count) {
    return;
  }

  CrashReporter::UnrecordAnnotation(annotation);
}
}

namespace mozilla::layers {

using namespace mozilla::gfx;

#ifdef MOZ_MEMORY
static bool sAllocAsjustmentTaskCancelled = false;
static bool sIncreasedDirtyPageThreshold = false;

static void ResetDirtyPageModifier();

static void ScheduleResetMaxDirtyPageModifier() {
  NS_DelayedDispatchToCurrentThread(
      NewRunnableFunction("ResetDirtyPageModifier", &ResetDirtyPageModifier),
      100  // In ms.
  );
}

static void NeedIncreasedMaxDirtyPageModifier() {
  if (sIncreasedDirtyPageThreshold) {
    sAllocAsjustmentTaskCancelled = true;
    return;
  }

  moz_set_max_dirty_page_modifier(3);
  sIncreasedDirtyPageThreshold = true;

  ScheduleResetMaxDirtyPageModifier();
}

static void ResetDirtyPageModifier() {
  if (!sIncreasedDirtyPageThreshold) {
    return;
  }

  if (sAllocAsjustmentTaskCancelled) {
    sAllocAsjustmentTaskCancelled = false;
    ScheduleResetMaxDirtyPageModifier();
    return;
  }

  moz_set_max_dirty_page_modifier(0);

  wr::RenderThread* renderThread = wr::RenderThread::Get();
  if (renderThread) {
    renderThread->NotifyIdle();
  }

  jemalloc_free_excess_dirty_pages();

  sIncreasedDirtyPageThreshold = false;
}
#else
// Don't bother doing anything of the memory allocator doesn't support this.
static void NeedIncreasedMaxDirtyPageModifier() {}
#endif

LazyLogModule gWebRenderBridgeParentLog("WebRenderBridgeParent");
#define LOG(...) \
  MOZ_LOG(gWebRenderBridgeParentLog, LogLevel::Debug, (__VA_ARGS__))

class ScheduleObserveLayersUpdate : public wr::NotificationHandler {
 public:
  ScheduleObserveLayersUpdate(RefPtr<CompositorBridgeParentBase> aBridge,
                              LayersId aLayersId, bool aIsActive)
      : mBridge(std::move(aBridge)),
        mLayersId(aLayersId),
        mIsActive(aIsActive) {}

  void Notify(wr::Checkpoint) override {
    CompositorThread()->Dispatch(NewRunnableMethod<LayersId, int>(
        "ObserveLayersUpdate", mBridge,
        &CompositorBridgeParentBase::ObserveLayersUpdate, mLayersId,
        mIsActive));
  }

 protected:
  RefPtr<CompositorBridgeParentBase> mBridge;
  LayersId mLayersId;
  bool mIsActive;
};

class SceneBuiltNotification : public wr::NotificationHandler {
 public:
  SceneBuiltNotification(WebRenderBridgeParent* aParent, wr::Epoch aEpoch,
                         TimeStamp aTxnStartTime)
      : mParent(aParent), mEpoch(aEpoch), mTxnStartTime(aTxnStartTime) {}

  void Notify(wr::Checkpoint) override {
    auto startTime = this->mTxnStartTime;
    RefPtr<WebRenderBridgeParent> parent = mParent;
    wr::Epoch epoch = mEpoch;
    CompositorThread()->Dispatch(NS_NewRunnableFunction(
        "SceneBuiltNotificationRunnable", [parent, epoch, startTime]() {
          auto endTime = TimeStamp::Now();
          if (profiler_thread_is_being_profiled_for_markers()) {
            PROFILER_MARKER("CONTENT_FULL_PAINT_TIME", GRAPHICS,
                            MarkerTiming::Interval(startTime, endTime),
                            ContentBuildMarker);
          }
          mozilla::glean::gfx_content::full_paint_time.AccumulateRawDuration(
              endTime - startTime);
          parent->NotifySceneBuiltForEpoch(epoch, endTime);
        }));
  }

 protected:
  RefPtr<WebRenderBridgeParent> mParent;
  wr::Epoch mEpoch;
  TimeStamp mTxnStartTime;
};

class WebRenderBridgeParent::ScheduleSharedSurfaceRelease final
    : public wr::NotificationHandler {
 public:
  explicit ScheduleSharedSurfaceRelease(WebRenderBridgeParent* aWrBridge)
      : mWrBridge(aWrBridge), mSurfaces(20) {}

  ~ScheduleSharedSurfaceRelease() override {
    if (!mSurfaces.IsEmpty()) {
      MOZ_ASSERT_UNREACHABLE("Unreleased surfaces!");
      gfxCriticalNote << "ScheduleSharedSurfaceRelease destroyed non-empty";
      NotifyInternal(/* aFromCheckpoint */ false);
    }
  }

  void Add(const wr::ImageKey& aKey, const wr::ExternalImageId& aId) {
    mSurfaces.AppendElement(wr::ExternalImageKeyPair{aKey, aId});
  }

  void Notify(wr::Checkpoint) override {
    NotifyInternal(/* aFromCheckpoint */ true);
  }

 private:
  void NotifyInternal(bool aFromCheckpoint) {
    CompositorThread()->Dispatch(
        NewRunnableMethod<nsTArray<wr::ExternalImageKeyPair>, bool>(
            "ObserveSharedSurfaceRelease", mWrBridge,
            &WebRenderBridgeParent::ObserveSharedSurfaceRelease,
            std::move(mSurfaces), aFromCheckpoint));
  }

  RefPtr<WebRenderBridgeParent> mWrBridge;
  nsTArray<wr::ExternalImageKeyPair> mSurfaces;
};

class MOZ_STACK_CLASS AutoWebRenderBridgeParentAsyncMessageSender final {
 public:
  explicit AutoWebRenderBridgeParentAsyncMessageSender(
      WebRenderBridgeParent* aWebRenderBridgeParent,
      nsTArray<OpDestroy>* aDestroyActors = nullptr)
      : mWebRenderBridgeParent(aWebRenderBridgeParent),
        mActorsToDestroy(aDestroyActors) {
    mWebRenderBridgeParent->SetAboutToSendAsyncMessages();
  }

  ~AutoWebRenderBridgeParentAsyncMessageSender() {
    mWebRenderBridgeParent->SendPendingAsyncMessages();
    if (mActorsToDestroy) {
      // Destroy the actors after sending the async messages because the latter
      // may contain references to some actors.
      for (const auto& op : *mActorsToDestroy) {
        mWebRenderBridgeParent->DestroyActor(op);
      }
    }
  }

 private:
  WebRenderBridgeParent* mWebRenderBridgeParent;
  nsTArray<OpDestroy>* mActorsToDestroy;
};

WebRenderBridgeParent::WebRenderBridgeParent(
    CompositorBridgeParentBase* aCompositorBridge,
    const wr::PipelineId& aPipelineId, widget::CompositorWidget* aWidget,
    CompositorVsyncScheduler* aScheduler, RefPtr<wr::WebRenderAPI>&& aApi,
    RefPtr<AsyncImagePipelineManager>&& aImageMgr, TimeDuration aVsyncRate)
    : mCompositorBridge(aCompositorBridge),
      mPipelineId(aPipelineId),
      mWidget(aWidget),
      mApi(aApi),
      mAsyncImageManager(aImageMgr),
      mCompositorScheduler(aScheduler),
      mVsyncRate(aVsyncRate),
      mWrEpoch{0},
      mIdNamespace(aApi->GetNamespace()),
#if defined(MOZ_WIDGET_ANDROID)
      mScreenPixelsTarget(nullptr),
#endif
      mBlobTileSize(256),
      mSkippedCompositeReasons(wr::RenderReasons::NONE),
      mDestroyed(false),
      mIsFirstPaint(true),
      mPendingScrollPayloads("WebRenderBridgeParent::mPendingScrollPayloads") {
  MOZ_ASSERT(mAsyncImageManager);
  LOG("WebRenderBridgeParent::WebRenderBridgeParent() PipelineId %" PRIx64
      " Id %" PRIx64 " root %d",
      wr::AsUint64(mPipelineId), wr::AsUint64(mApi->GetId()),
      IsRootWebRenderBridgeParent());

  mAsyncImageManager->AddPipeline(mPipelineId, this);
  if (IsRootWebRenderBridgeParent()) {
    MOZ_ASSERT(!mCompositorScheduler);
    mCompositorScheduler = new CompositorVsyncScheduler(this, mWidget);
    UpdateDebugFlags();
    UpdateQualitySettings();
    UpdateProfilerUI();
    UpdateParameters();
    // Start with the cached bool parameter bits inverted so that we update them
    // all.
    mBoolParameterBits = ~gfxVars::WebRenderBoolParameters();
    UpdateBoolParameters();
  }
  mRemoteTextureTxnScheduler =
      RemoteTextureTxnScheduler::Create(aCompositorBridge);
}

WebRenderBridgeParent::WebRenderBridgeParent(const wr::PipelineId& aPipelineId,
                                             nsCString&& aError)
    : mCompositorBridge(nullptr),
      mPipelineId(aPipelineId),
      mWrEpoch{0},
      mIdNamespace{0},
      mInitError(aError),
      mDestroyed(true),
      mIsFirstPaint(false),
      mPendingScrollPayloads("WebRenderBridgeParent::mPendingScrollPayloads") {
  LOG("WebRenderBridgeParent::WebRenderBridgeParent() PipelineId %" PRIx64 "",
      wr::AsUint64(mPipelineId));
}

WebRenderBridgeParent::~WebRenderBridgeParent() {
  LOG("WebRenderBridgeParent::WebRenderBridgeParent() PipelineId %" PRIx64 "",
      wr::AsUint64(mPipelineId));
}

/* static */
WebRenderBridgeParent* WebRenderBridgeParent::CreateDestroyed(
    const wr::PipelineId& aPipelineId, nsCString&& aError) {
  return new WebRenderBridgeParent(aPipelineId, std::move(aError));
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvEnsureConnected(
    TextureFactoryIdentifier* aTextureFactoryIdentifier,
    MaybeIdNamespace* aMaybeIdNamespace, nsCString* aError) {
  if (mDestroyed) {
    *aTextureFactoryIdentifier =
        TextureFactoryIdentifier(LayersBackend::LAYERS_NONE);
    *aMaybeIdNamespace = Nothing();
    if (mInitError.IsEmpty()) {
      // Got destroyed after we initialized but before the handshake finished?
      aError->AssignLiteral("FEATURE_FAILURE_WEBRENDER_INITIALIZE_RACE");
    } else {
      *aError = std::move(mInitError);
    }
    return IPC_OK();
  }

  MOZ_ASSERT(mIdNamespace.mHandle != 0);
  *aTextureFactoryIdentifier = GetTextureFactoryIdentifier();
  *aMaybeIdNamespace = Some(mIdNamespace);

  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvShutdown() {
  return HandleShutdown();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvShutdownSync() {
  return HandleShutdown();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::HandleShutdown() {
  Destroy();
  IProtocol* mgr = Manager();
  if (!Send__delete__(this)) {
    return IPC_FAIL_NO_REASON(mgr);
  }
  return IPC_OK();
}

void WebRenderBridgeParent::Destroy() {
  if (mDestroyed) {
    return;
  }
  LOG("WebRenderBridgeParent::Destroy() PipelineId %" PRIx64 " Id %" PRIx64
      " root %d",
      wr::AsUint64(mPipelineId), wr::AsUint64(mApi->GetId()),
      IsRootWebRenderBridgeParent());

  mDestroyed = true;
  if (mRemoteTextureTxnScheduler) {
    mRemoteTextureTxnScheduler = nullptr;
  }
  if (mWebRenderBridgeRef) {
    // Break mutual reference
    mWebRenderBridgeRef->Clear();
    mWebRenderBridgeRef = nullptr;
  }
  for (const auto& entry : mCompositables) {
    entry.second->OnReleased();
  }
  mCompositables.clear();
  ClearResources();
}

struct WROTSAlloc {
  wr::Vec<uint8_t> mVec;

  void* Grow(void* aPtr, size_t aLength) {
    if (aLength > mVec.Capacity()) {
      mVec.Reserve(aLength - mVec.Capacity());
    }
    return mVec.inner.data;
  }
  wr::Vec<uint8_t> ShrinkToFit(void* aPtr, size_t aLength) {
    wr::Vec<uint8_t> result(std::move(mVec));
    result.inner.length = aLength;
    return result;
  }
  void Free(void* aPtr) {}
};

static bool ReadRawFont(const OpAddRawFont& aOp, wr::ShmSegmentsReader& aReader,
                        wr::TransactionBuilder& aUpdates) {
  wr::Vec<uint8_t> sourceBytes;
  Maybe<Range<uint8_t>> ptr =
      aReader.GetReadPointerOrCopy(aOp.bytes(), sourceBytes);
  if (ptr.isNothing()) {
    gfxCriticalNote << "No read pointer from reader for sanitizing font "
                    << aOp.key().mHandle;
    return false;
  }
  Range<uint8_t>& source = ptr.ref();
  // Attempt to sanitize the font before passing it along for updating.
  // Ensure that we're not strict here about font types, since any font that
  // failed generating a descriptor might end up here as raw font data.
  size_t lengthHint = gfxOTSContext::GuessSanitizedFontSize(
      source.begin().get(), source.length(), false);
  if (!lengthHint) {
    gfxCriticalNote << "Could not determine font type for sanitizing font "
                    << aOp.key().mHandle;
    return false;
  }
  gfxOTSExpandingMemoryStream<WROTSAlloc> output(lengthHint);
  gfxOTSContext otsContext;
  if (!otsContext.Process(&output, source.begin().get(), source.length())) {
    gfxCriticalNote << "Failed sanitizing font " << aOp.key().mHandle;
    return false;
  }
  wr::Vec<uint8_t> bytes = output.forget();

  aUpdates.AddRawFont(aOp.key(), bytes, aOp.fontIndex());
  return true;
}

bool WebRenderBridgeParent::UpdateResources(
    const nsTArray<OpUpdateResource>& aResourceUpdates,
    const nsTArray<RefCountedShmem>& aSmallShmems,
    const nsTArray<ipc::Shmem>& aLargeShmems,
    wr::TransactionBuilder& aUpdates) {
  wr::ShmSegmentsReader reader(aSmallShmems, aLargeShmems);
  UniquePtr<ScheduleSharedSurfaceRelease> scheduleRelease;

  while (GPUParent::MaybeFlushMemory()) {
    // If the GPU process has memory pressure, preemptively unmap some of our
    // shared memory images. If we are in the parent process, the expiration
    // tracker itself will listen for the memory pressure event.
    if (!SharedSurfacesParent::AgeAndExpireOneGeneration()) {
      break;
    }
  }

  bool success = true;
  for (const auto& cmd : aResourceUpdates) {
    switch (cmd.type()) {
      case OpUpdateResource::TOpAddImage: {
        const auto& op = cmd.get_OpAddImage();
        if (!MatchesNamespace(op.key())) {
          MOZ_ASSERT_UNREACHABLE("Stale image key (add)!");
          break;
        }

        wr::Vec<uint8_t> bytes;
        if (reader.Read(op.bytes(), bytes)) {
          aUpdates.AddImage(op.key(), op.descriptor(), bytes);
        } else {
          gfxCriticalNote << "TOpAddImage failed";
          success = false;
        }
        break;
      }
      case OpUpdateResource::TOpUpdateImage: {
        const auto& op = cmd.get_OpUpdateImage();
        if (!MatchesNamespace(op.key())) {
          MOZ_ASSERT_UNREACHABLE("Stale image key (update)!");
          break;
        }

        wr::Vec<uint8_t> bytes;
        if (reader.Read(op.bytes(), bytes)) {
          aUpdates.UpdateImageBuffer(op.key(), op.descriptor(), bytes);
        } else {
          gfxCriticalNote << "TOpUpdateImage failed";
          success = false;
        }
        break;
      }
      case OpUpdateResource::TOpAddBlobImage: {
        const auto& op = cmd.get_OpAddBlobImage();
        if (!MatchesNamespace(op.key())) {
          MOZ_ASSERT_UNREACHABLE("Stale blob image key (add)!");
          break;
        }

        wr::Vec<uint8_t> bytes;
        if (reader.Read(op.bytes(), bytes)) {
          aUpdates.AddBlobImage(op.key(), op.descriptor(), mBlobTileSize, bytes,
                                wr::ToDeviceIntRect(op.visibleRect()));
        } else {
          gfxCriticalNote << "TOpAddBlobImage failed";
          success = false;
        }

        break;
      }
      case OpUpdateResource::TOpUpdateBlobImage: {
        const auto& op = cmd.get_OpUpdateBlobImage();
        if (!MatchesNamespace(op.key())) {
          MOZ_ASSERT_UNREACHABLE("Stale blob image key (update)!");
          break;
        }

        wr::Vec<uint8_t> bytes;
        if (reader.Read(op.bytes(), bytes)) {
          aUpdates.UpdateBlobImage(op.key(), op.descriptor(), bytes,
                                   wr::ToDeviceIntRect(op.visibleRect()),
                                   wr::ToLayoutIntRect(op.dirtyRect()));
        } else {
          gfxCriticalNote << "TOpUpdateBlobImage failed";
          success = false;
        }
        break;
      }
      case OpUpdateResource::TOpSetBlobImageVisibleArea: {
        const auto& op = cmd.get_OpSetBlobImageVisibleArea();
        if (!MatchesNamespace(op.key())) {
          MOZ_ASSERT_UNREACHABLE("Stale blob image key (visible)!");
          break;
        }
        aUpdates.SetBlobImageVisibleArea(op.key(),
                                         wr::ToDeviceIntRect(op.area()));
        break;
      }
      case OpUpdateResource::TOpAddSnapshotImage: {
        const auto& op = cmd.get_OpAddSnapshotImage();
        if (!MatchesNamespace(wr::AsImageKey(op.key()))) {
          MOZ_ASSERT_UNREACHABLE("Stale snapshot image key (add)!");
          break;
        }
        aUpdates.AddSnapshotImage(op.key());
        break;
      }
      case OpUpdateResource::TOpDeleteSnapshotImage: {
        const auto& op = cmd.get_OpDeleteSnapshotImage();
        if (!MatchesNamespace(wr::AsImageKey(op.key()))) {
          MOZ_ASSERT_UNREACHABLE("Stale snapshot image key (remove)!");
          break;
        }
        aUpdates.DeleteSnapshotImage(op.key());
        break;
      }
      case OpUpdateResource::TOpAddSharedExternalImage: {
        const auto& op = cmd.get_OpAddSharedExternalImage();
        // gfxCriticalNote is called on error
        if (!AddSharedExternalImage(op.externalImageId(), op.key(), aUpdates)) {
          success = false;
        }
        break;
      }
      case OpUpdateResource::TOpPushExternalImageForTexture: {
        const auto& op = cmd.get_OpPushExternalImageForTexture();
        CompositableTextureHostRef texture;
        texture = TextureHost::AsTextureHost(op.texture().AsParent());
        // gfxCriticalNote is called on error
        if (!PushExternalImageForTexture(op.externalImageId(), op.key(),
                                         texture, op.isUpdate(), aUpdates)) {
          success = false;
        }
        break;
      }
      case OpUpdateResource::TOpUpdateSharedExternalImage: {
        const auto& op = cmd.get_OpUpdateSharedExternalImage();
        // gfxCriticalNote is called on error
        if (!UpdateSharedExternalImage(op.externalImageId(), op.key(),
                                       op.dirtyRect(), aUpdates,
                                       scheduleRelease)) {
          success = false;
        }
        break;
      }
      case OpUpdateResource::TOpAddRawFont: {
        if (!ReadRawFont(cmd.get_OpAddRawFont(), reader, aUpdates)) {
          success = false;
        }
        break;
      }
      case OpUpdateResource::TOpAddFontDescriptor: {
        const auto& op = cmd.get_OpAddFontDescriptor();
        if (!MatchesNamespace(op.key())) {
          MOZ_ASSERT_UNREACHABLE("Stale font key (add descriptor)!");
          break;
        }

        wr::Vec<uint8_t> bytes;
        if (reader.Read(op.bytes(), bytes)) {
          aUpdates.AddFontDescriptor(op.key(), bytes, op.fontIndex());
        } else {
          gfxCriticalNote << "TOpAddFontDescriptor failed";
          success = false;
        }
        break;
      }
      case OpUpdateResource::TOpAddFontInstance: {
        const auto& op = cmd.get_OpAddFontInstance();
        if (!MatchesNamespace(op.instanceKey()) ||
            !MatchesNamespace(op.fontKey())) {
          MOZ_ASSERT_UNREACHABLE("Stale font key (add instance)!");
          break;
        }

        wr::Vec<uint8_t> variations;
        if (reader.Read(op.variations(), variations)) {
          aUpdates.AddFontInstance(op.instanceKey(), op.fontKey(),
                                   op.glyphSize(), op.options().ptrOr(nullptr),
                                   op.platformOptions().ptrOr(nullptr),
                                   variations);
        } else {
          gfxCriticalNote << "TOpAddFontInstance failed";
          success = false;
        }
        break;
      }
      case OpUpdateResource::TOpDeleteImage: {
        const auto& op = cmd.get_OpDeleteImage();
        if (!MatchesNamespace(op.key())) {
          // TODO(aosmond): We should also assert here, but the callers are less
          // careful about checking when cleaning up their old keys. We should
          // perform an audit on image key usage.
          break;
        }

        DeleteImage(op.key(), aUpdates);
        break;
      }
      case OpUpdateResource::TOpDeleteBlobImage: {
        const auto& op = cmd.get_OpDeleteBlobImage();
        if (!MatchesNamespace(op.key())) {
          MOZ_ASSERT_UNREACHABLE("Stale blob image key (delete)!");
          break;
        }

        aUpdates.DeleteBlobImage(op.key());
        break;
      }
      case OpUpdateResource::TOpDeleteFont: {
        const auto& op = cmd.get_OpDeleteFont();
        if (!MatchesNamespace(op.key())) {
          MOZ_ASSERT_UNREACHABLE("Stale font key (delete)!");
          break;
        }

        aUpdates.DeleteFont(op.key());
        break;
      }
      case OpUpdateResource::TOpDeleteFontInstance: {
        const auto& op = cmd.get_OpDeleteFontInstance();
        if (!MatchesNamespace(op.key())) {
          MOZ_ASSERT_UNREACHABLE("Stale font instance key (delete)!");
          break;
        }

        aUpdates.DeleteFontInstance(op.key());
        break;
      }
      case OpUpdateResource::T__None:
        break;
    }
  }

  if (scheduleRelease) {
    // When software WR is enabled, shared surfaces are read during rendering
    // rather than copied to the texture cache.
    wr::Checkpoint when = mApi->GetBackendType() == WebRenderBackend::SOFTWARE
                              ? wr::Checkpoint::FrameRendered
                              : wr::Checkpoint::FrameTexturesUpdated;
    aUpdates.Notify(when, std::move(scheduleRelease));
  }

  MOZ_ASSERT(success);
  return success;
}

bool WebRenderBridgeParent::AddSharedExternalImage(
    wr::ExternalImageId aExtId, wr::ImageKey aKey,
    wr::TransactionBuilder& aResources) {
  if (!MatchesNamespace(aKey)) {
    MOZ_ASSERT_UNREACHABLE("Stale shared external image key (add)!");
    return true;
  }

  auto key = wr::AsUint64(aKey);
  auto it = mSharedSurfaceIds.find(key);
  if (it != mSharedSurfaceIds.end()) {
    gfxCriticalNote << "Readding known shared surface: " << key;
    return false;
  }

  RefPtr<DataSourceSurface> dSurf = SharedSurfacesParent::Acquire(aExtId);
  if (!dSurf) {
    gfxCriticalNote
        << "DataSourceSurface of SharedSurfaces does not exist for extId:"
        << wr::AsUint64(aExtId);
    return false;
  }

  mSharedSurfaceIds.insert(std::make_pair(key, aExtId));

  // Prefer raw buffers, unless our backend requires native textures.
  IntSize surfaceSize = dSurf->GetSize();
  TextureHost::NativeTexturePolicy policy =
      TextureHost::BackendNativeTexturePolicy(mApi->GetBackendType(),
                                              surfaceSize);
  auto imageType =
      policy == TextureHost::NativeTexturePolicy::REQUIRE
          ? wr::ExternalImageType::TextureHandle(wr::ImageBufferKind::Texture2D)
          : wr::ExternalImageType::Buffer();
  wr::ImageDescriptor descriptor(surfaceSize, dSurf->Stride(),
                                 dSurf->GetFormat());
  aResources.AddExternalImage(aKey, descriptor, aExtId, imageType, 0);
  return true;
}

bool WebRenderBridgeParent::PushExternalImageForTexture(
    wr::ExternalImageId aExtId, wr::ImageKey aKey, TextureHost* aTexture,
    bool aIsUpdate, wr::TransactionBuilder& aResources) {
  if (!MatchesNamespace(aKey)) {
    MOZ_ASSERT_UNREACHABLE("Stale texture external image key!");
    return true;
  }

  if (!aTexture) {
    gfxCriticalNote << "TextureHost does not exist for extId:"
                    << wr::AsUint64(aExtId);
    return false;
  }

  auto op = aIsUpdate ? TextureHost::UPDATE_IMAGE : TextureHost::ADD_IMAGE;
  WebRenderTextureHost* wrTexture = aTexture->AsWebRenderTextureHost();
  if (wrTexture) {
    Range<wr::ImageKey> keys(&aKey, 1);
    wrTexture->PushResourceUpdates(aResources, op, keys,
                                   wrTexture->GetExternalImageKey());
    auto it = mTextureHosts.find(wr::AsUint64(aKey));
    MOZ_ASSERT((it == mTextureHosts.end() && !aIsUpdate) ||
               (it != mTextureHosts.end() && aIsUpdate));
    if (it != mTextureHosts.end()) {
      // Release Texture if it exists.
      ReleaseTextureOfImage(aKey);
    }
    mTextureHosts.emplace(wr::AsUint64(aKey),
                          CompositableTextureHostRef(aTexture));
    return true;
  }

  RefPtr<DataSourceSurface> dSurf = aTexture->GetAsSurface();
  if (!dSurf) {
    gfxCriticalNote
        << "TextureHost does not return DataSourceSurface for extId:"
        << wr::AsUint64(aExtId);
    return false;
  }

  DataSourceSurface::MappedSurface map;
  if (!dSurf->Map(gfx::DataSourceSurface::MapType::READ, &map)) {
    gfxCriticalNote << "DataSourceSurface failed to map for Image for extId:"
                    << wr::AsUint64(aExtId);
    return false;
  }

  IntSize size = dSurf->GetSize();
  wr::ImageDescriptor descriptor(size, map.mStride, dSurf->GetFormat());
  wr::Vec<uint8_t> data;
  data.PushBytes(Range<uint8_t>(map.mData, size.height * map.mStride));

  if (op == TextureHost::UPDATE_IMAGE) {
    aResources.UpdateImageBuffer(aKey, descriptor, data);
  } else {
    aResources.AddImage(aKey, descriptor, data);
  }

  dSurf->Unmap();

  return true;
}

bool WebRenderBridgeParent::UpdateSharedExternalImage(
    wr::ExternalImageId aExtId, wr::ImageKey aKey,
    const ImageIntRect& aDirtyRect, wr::TransactionBuilder& aResources,
    UniquePtr<ScheduleSharedSurfaceRelease>& aScheduleRelease) {
  if (!MatchesNamespace(aKey)) {
    MOZ_ASSERT_UNREACHABLE("Stale shared external image key (update)!");
    return true;
  }

  auto key = wr::AsUint64(aKey);
  auto it = mSharedSurfaceIds.find(key);
  if (it == mSharedSurfaceIds.end()) {
    gfxCriticalNote << "Updating unknown shared surface: " << key;
    return false;
  }

  RefPtr<DataSourceSurface> dSurf;
  if (it->second == aExtId) {
    dSurf = SharedSurfacesParent::Get(aExtId);
  } else {
    dSurf = SharedSurfacesParent::Acquire(aExtId);
  }

  if (!dSurf) {
    gfxCriticalNote << "Shared surface does not exist for extId:"
                    << wr::AsUint64(aExtId);
    return false;
  }

  if (!(it->second == aExtId)) {
    // We already have a mapping for this image key, so ensure we release the
    // previous external image ID. This can happen when an image is animated,
    // and it is changing the external image that the animation points to.
    if (!aScheduleRelease) {
      aScheduleRelease = MakeUnique<ScheduleSharedSurfaceRelease>(this);
    }
    aScheduleRelease->Add(aKey, it->second);
    it->second = aExtId;
  }

  // Prefer raw buffers, unless our backend requires native textures.
  IntSize surfaceSize = dSurf->GetSize();
  TextureHost::NativeTexturePolicy policy =
      TextureHost::BackendNativeTexturePolicy(mApi->GetBackendType(),
                                              surfaceSize);
  auto imageType =
      policy == TextureHost::NativeTexturePolicy::REQUIRE
          ? wr::ExternalImageType::TextureHandle(wr::ImageBufferKind::Texture2D)
          : wr::ExternalImageType::Buffer();
  wr::ImageDescriptor descriptor(surfaceSize, dSurf->Stride(),
                                 dSurf->GetFormat());
  aResources.UpdateExternalImageWithDirtyRect(
      aKey, descriptor, aExtId, imageType, wr::ToDeviceIntRect(aDirtyRect), 0,
      /* aNormalizedUvs */ false);

  return true;
}

void WebRenderBridgeParent::ObserveSharedSurfaceRelease(
    const nsTArray<wr::ExternalImageKeyPair>& aPairs,
    const bool& aFromCheckpoint) {
  if (!mDestroyed) {
    Unused << SendWrReleasedImages(aPairs);
  }

  if (!aFromCheckpoint && mAsyncImageManager) {
    // We failed to receive a checkpoint notification, so we are releasing these
    // surfaces blind. Let's wait until the next epoch to complete releasing.
    for (const auto& pair : aPairs) {
      mAsyncImageManager->HoldExternalImage(mPipelineId, mWrEpoch, pair.id);
    }
    return;
  }

  // We hit the checkpoint, so we know we can safely release the surfaces now.
  for (const auto& pair : aPairs) {
    SharedSurfacesParent::Release(pair.id);
  }
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvUpdateResources(
    const wr::IdNamespace& aIdNamespace,
    nsTArray<OpUpdateResource>&& aResourceUpdates,
    nsTArray<RefCountedShmem>&& aSmallShmems,
    nsTArray<ipc::Shmem>&& aLargeShmems) {
  const bool isValidMessage = aIdNamespace == mIdNamespace;

  if (mDestroyed || !isValidMessage) {
    wr::IpcResourceUpdateQueue::ReleaseShmems(this, aSmallShmems);
    wr::IpcResourceUpdateQueue::ReleaseShmems(this, aLargeShmems);
    return IPC_OK();
  }

  LOG("WebRenderBridgeParent::RecvUpdateResources() PipelineId %" PRIx64
      " Id %" PRIx64 " root %d",
      wr::AsUint64(mPipelineId), wr::AsUint64(mApi->GetId()),
      IsRootWebRenderBridgeParent());

  wr::TransactionBuilder txn(mApi);
  txn.SetLowPriority(!IsRootWebRenderBridgeParent());

  Unused << GetNextWrEpoch();

  bool success =
      UpdateResources(aResourceUpdates, aSmallShmems, aLargeShmems, txn);
  wr::IpcResourceUpdateQueue::ReleaseShmems(this, aSmallShmems);
  wr::IpcResourceUpdateQueue::ReleaseShmems(this, aLargeShmems);

  // Even when txn.IsResourceUpdatesEmpty() is true, there could be resource
  // updates. It is handled by WebRenderTextureHostWrapper. In this case
  // txn.IsRenderedFrameInvalidated() becomes true.
  if (!txn.IsResourceUpdatesEmpty() || txn.IsRenderedFrameInvalidated()) {
    // There are resource updates, then we update Epoch of transaction.
    txn.UpdateEpoch(mPipelineId, mWrEpoch);
    mAsyncImageManager->SetWillGenerateFrame();
    ScheduleGenerateFrame(wr::RenderReasons::RESOURCE_UPDATE);
  } else {
    // If TransactionBuilder does not have resource updates nor display list,
    // ScheduleGenerateFrame is not triggered via SceneBuilder and there is no
    // need to update WrEpoch.
    // Then we want to rollback WrEpoch. See Bug 1490117.
    RollbackWrEpoch();
  }

  mApi->SendTransaction(txn);

  if (!success) {
    return IPC_FAIL(this, "Invalid WebRender resource data shmem or address.");
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvDeleteCompositorAnimations(
    nsTArray<uint64_t>&& aIds) {
  if (mDestroyed) {
    return IPC_OK();
  }

  LOG("WebRenderBridgeParent::RecvDeleteCompositorAnimations() PipelineId "
      "%" PRIx64 " Id %" PRIx64 " root %d",
      wr::AsUint64(mPipelineId), wr::AsUint64(mApi->GetId()),
      IsRootWebRenderBridgeParent());

  // Once mWrEpoch has been rendered, we can delete these compositor animations
  mCompositorAnimationsToDelete.push(
      CompositorAnimationIdsForEpoch(mWrEpoch, std::move(aIds)));
  return IPC_OK();
}

void WebRenderBridgeParent::RemoveEpochDataPriorTo(
    const wr::Epoch& aRenderedEpoch) {
  if (RefPtr<OMTASampler> sampler = GetOMTASampler()) {
    sampler->RemoveEpochDataPriorTo(mCompositorAnimationsToDelete,
                                    mActiveAnimations, aRenderedEpoch);
  }
}

bool WebRenderBridgeParent::IsRootWebRenderBridgeParent() const {
  return !!mWidget;
}

void WebRenderBridgeParent::BeginRecording(const TimeStamp& aRecordingStart) {
  mApi->BeginRecording(aRecordingStart, mPipelineId);
}

RefPtr<wr::WebRenderAPI::EndRecordingPromise>
WebRenderBridgeParent::EndRecording() {
  return mApi->EndRecording();
}

void WebRenderBridgeParent::AddPendingScrollPayload(
    CompositionPayload& aPayload, const VsyncId& aCompositeStartId) {
  auto pendingScrollPayloads = mPendingScrollPayloads.Lock();
  nsTArray<CompositionPayload>* payloads =
      pendingScrollPayloads->GetOrInsertNew(aCompositeStartId.mId);

  payloads->AppendElement(aPayload);
}

nsTArray<CompositionPayload> WebRenderBridgeParent::TakePendingScrollPayload(
    const VsyncId& aCompositeStartId) {
  auto pendingScrollPayloads = mPendingScrollPayloads.Lock();
  nsTArray<CompositionPayload> payload;
  if (nsTArray<CompositionPayload>* storedPayload =
          pendingScrollPayloads->Get(aCompositeStartId.mId)) {
    payload.AppendElements(std::move(*storedPayload));
    pendingScrollPayloads->Remove(aCompositeStartId.mId);
  }
  return payload;
}

CompositorBridgeParent* WebRenderBridgeParent::GetRootCompositorBridgeParent()
    const {
  if (!mCompositorBridge) {
    return nullptr;
  }

  if (IsRootWebRenderBridgeParent()) {
    // This WebRenderBridgeParent is attached to the root
    // CompositorBridgeParent.
    return static_cast<CompositorBridgeParent*>(mCompositorBridge);
  }

  // Otherwise, this WebRenderBridgeParent is attached to a
  // ContentCompositorBridgeParent so we have an extra level of
  // indirection to unravel.
  CompositorBridgeParent::LayerTreeState* lts =
      CompositorBridgeParent::GetIndirectShadowTree(GetLayersId());
  if (!lts) {
    return nullptr;
  }
  return lts->mParent;
}

RefPtr<WebRenderBridgeParent>
WebRenderBridgeParent::GetRootWebRenderBridgeParent() const {
  CompositorBridgeParent* cbp = GetRootCompositorBridgeParent();
  if (!cbp) {
    return nullptr;
  }

  return cbp->GetWebRenderBridgeParent();
}

void WebRenderBridgeParent::UpdateAPZFocusState(const FocusTarget& aFocus) {
  CompositorBridgeParent* cbp = GetRootCompositorBridgeParent();
  if (!cbp) {
    return;
  }
  LayersId rootLayersId = cbp->RootLayerTreeId();
  if (RefPtr<APZUpdater> apz = cbp->GetAPZUpdater()) {
    apz->UpdateFocusState(rootLayersId, GetLayersId(), aFocus);
  }
}

void WebRenderBridgeParent::UpdateAPZScrollData(const wr::Epoch& aEpoch,
                                                WebRenderScrollData&& aData) {
  CompositorBridgeParent* cbp = GetRootCompositorBridgeParent();
  if (!cbp) {
    return;
  }
  LayersId rootLayersId = cbp->RootLayerTreeId();
  if (RefPtr<APZUpdater> apz = cbp->GetAPZUpdater()) {
    apz->UpdateScrollDataAndTreeState(rootLayersId, GetLayersId(), aEpoch,
                                      std::move(aData));
  }
}

void WebRenderBridgeParent::UpdateAPZScrollOffsets(
    ScrollUpdatesMap&& aUpdates, uint32_t aPaintSequenceNumber) {
  CompositorBridgeParent* cbp = GetRootCompositorBridgeParent();
  if (!cbp) {
    return;
  }
  LayersId rootLayersId = cbp->RootLayerTreeId();
  if (RefPtr<APZUpdater> apz = cbp->GetAPZUpdater()) {
    apz->UpdateScrollOffsets(rootLayersId, GetLayersId(), std::move(aUpdates),
                             aPaintSequenceNumber);
  }
}

void WebRenderBridgeParent::SetAPZSampleTime() {
  CompositorBridgeParent* cbp = GetRootCompositorBridgeParent();
  if (!cbp) {
    return;
  }
  if (RefPtr<APZSampler> apz = cbp->GetAPZSampler()) {
    SampleTime animationTime;
    if (Maybe<TimeStamp> testTime = cbp->GetTestingTimeStamp()) {
      animationTime = SampleTime::FromTest(*testTime);
    } else {
      animationTime = mCompositorScheduler->GetLastComposeTime();
    }
    TimeDuration frameInterval = cbp->GetVsyncInterval();
    // As with the non-webrender codepath in AsyncCompositionManager, we want to
    // use the timestamp for the next vsync when advancing animations.
    if (frameInterval != TimeDuration::Forever()) {
      animationTime = animationTime + frameInterval;
    }
    apz->SetSampleTime(animationTime);
  }
}

bool WebRenderBridgeParent::SetDisplayList(
    const LayoutDeviceRect& aRect, ipc::ByteBuf&& aDLItems,
    ipc::ByteBuf&& aDLCache, ipc::ByteBuf&& aSpatialTreeDL,
    const wr::BuiltDisplayListDescriptor& aDLDesc,
    const nsTArray<OpUpdateResource>& aResourceUpdates,
    const nsTArray<RefCountedShmem>& aSmallShmems,
    const nsTArray<ipc::Shmem>& aLargeShmems, const TimeStamp& aTxnStartTime,
    wr::TransactionBuilder& aTxn, wr::Epoch aWrEpoch) {
  bool success =
      UpdateResources(aResourceUpdates, aSmallShmems, aLargeShmems, aTxn);

  wr::Vec<uint8_t> dlItems(std::move(aDLItems));
  wr::Vec<uint8_t> dlCache(std::move(aDLCache));
  wr::Vec<uint8_t> dlSpatialTreeData(std::move(aSpatialTreeDL));

  if (IsRootWebRenderBridgeParent()) {
    LayoutDeviceIntSize widgetSize = mWidget->GetClientSize();
    LayoutDeviceIntRect rect =
        LayoutDeviceIntRect(LayoutDeviceIntPoint(), widgetSize);
    aTxn.SetDocumentView(rect);
  }
  aTxn.SetDisplayList(aWrEpoch, mPipelineId, aDLDesc, dlItems, dlCache,
                      dlSpatialTreeData);

  MaybeNotifyOfLayers(aTxn, true);

  if (!IsRootWebRenderBridgeParent()) {
    aTxn.Notify(wr::Checkpoint::SceneBuilt, MakeUnique<SceneBuiltNotification>(
                                                this, aWrEpoch, aTxnStartTime));
  }

  NeedIncreasedMaxDirtyPageModifier();

  mApi->SendTransaction(aTxn);

  // We will schedule generating a frame after the scene
  // build is done, so we don't need to do it here.
  return success;
}

bool WebRenderBridgeParent::ProcessDisplayListData(
    DisplayListData& aDisplayList, wr::Epoch aWrEpoch,
    const TimeStamp& aTxnStartTime, bool aValidTransaction,
    bool aRenderOffscreen, const VsyncId& aVsyncId) {
  wr::TransactionBuilder txn(mApi, /* aUseSceneBuilderThread */ true,
                             mRemoteTextureTxnScheduler, mFwdTransactionId);
  Maybe<wr::AutoTransactionSender> sender;

  if (aDisplayList.mScrollData && !aDisplayList.mScrollData->Validate()) {
    // If the scroll data is invalid, the entire transaction needs to be dropped
    // because the scroll data and the display list cross-reference each other.
    MOZ_ASSERT(
        false,
        "Content sent malformed scroll data (or validation check has a bug)");
    aValidTransaction = false;
  }

  if (!aValidTransaction) {
    return true;
  }

  MOZ_ASSERT(aDisplayList.mIdNamespace == mIdNamespace);

  // Note that this needs to happen before the display list transaction is
  // sent to WebRender, so that the UpdateHitTestingTree call is guaranteed to
  // be in the updater queue at the time that the scene swap completes.
  if (aDisplayList.mScrollData) {
    UpdateAPZScrollData(aWrEpoch, std::move(aDisplayList.mScrollData.ref()));
  }

  if (aRenderOffscreen) {
    TimeStamp start = TimeStamp::Now();
    txn.GenerateFrame(aVsyncId, false, wr::RenderReasons::SNAPSHOT);
    wr::RenderThread::Get()->IncPendingFrameCount(mApi->GetId(), aVsyncId,
                                                  start);
  }

  txn.SetLowPriority(!IsRootWebRenderBridgeParent());
  sender.emplace(mApi, &txn);
  bool success = true;

  success =
      ProcessWebRenderParentCommands(aDisplayList.mCommands, txn) && success;

  if (aDisplayList.mDLItems && aDisplayList.mDLCache &&
      aDisplayList.mDLSpatialTree) {
    success =
        SetDisplayList(
            aDisplayList.mRect, std::move(aDisplayList.mDLItems.ref()),
            std::move(aDisplayList.mDLCache.ref()),
            std::move(aDisplayList.mDLSpatialTree.ref()), aDisplayList.mDLDesc,
            aDisplayList.mResourceUpdates, aDisplayList.mSmallShmems,
            aDisplayList.mLargeShmems, aTxnStartTime, txn, aWrEpoch) &&
        success;
  }
  return success;
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvSetDisplayList(
    DisplayListData&& aDisplayList, nsTArray<OpDestroy>&& aToDestroy,
    const uint64_t& aFwdTransactionId, const TransactionId& aTransactionId,
    const bool& aContainsSVGGroup, const VsyncId& aVsyncId,
    const TimeStamp& aVsyncStartTime, const TimeStamp& aRefreshStartTime,
    const TimeStamp& aTxnStartTime, const nsACString& aTxnURL,
    const TimeStamp& aFwdTime, nsTArray<CompositionPayload>&& aPayloads,
    const bool& aRenderOffscreen) {
  if (mDestroyed) {
    for (const auto& op : aToDestroy) {
      DestroyActor(op);
    }
    wr::IpcResourceUpdateQueue::ReleaseShmems(this, aDisplayList.mSmallShmems);
    wr::IpcResourceUpdateQueue::ReleaseShmems(this, aDisplayList.mLargeShmems);
    return IPC_OK();
  }

  LOG("WebRenderBridgeParent::RecvSetDisplayList() PipelineId %" PRIx64
      " Id %" PRIx64 " root %d",
      wr::AsUint64(mPipelineId), wr::AsUint64(mApi->GetId()),
      IsRootWebRenderBridgeParent());

  if (!IsRootWebRenderBridgeParent()) {
    CrashReporter::RecordAnnotationNSCString(CrashReporter::Annotation::URL,
                                             aTxnURL);
  }

  CompositorBridgeParent* cbp = GetRootCompositorBridgeParent();
  uint64_t innerWindowId = cbp ? cbp->GetInnerWindowId() : 0;
  AUTO_PROFILER_TRACING_MARKER_INNERWINDOWID("Paint", "SetDisplayList",
                                             GRAPHICS, innerWindowId);
  UpdateFwdTransactionId(aFwdTransactionId);

  // This ensures that destroy operations are always processed. It is not safe
  // to early-return from RecvDPEnd without doing so.
  AutoWebRenderBridgeParentAsyncMessageSender autoAsyncMessageSender(
      this, &aToDestroy);

  wr::Epoch wrEpoch = GetNextWrEpoch();

  mReceivedDisplayList = true;

  if (aDisplayList.mScrollData && aDisplayList.mScrollData->IsFirstPaint()) {
    mIsFirstPaint = true;
  }

  bool validTransaction = aDisplayList.mIdNamespace == mIdNamespace;
  bool success =
      ProcessDisplayListData(aDisplayList, wrEpoch, aTxnStartTime,
                             validTransaction, aRenderOffscreen, aVsyncId);

  if (!IsRootWebRenderBridgeParent()) {
    aPayloads.AppendElement(
        CompositionPayload{CompositionPayloadType::eContentPaint, aFwdTime});
  }

  HoldPendingTransactionId(wrEpoch, aTransactionId, aContainsSVGGroup, aVsyncId,
                           aVsyncStartTime, aRefreshStartTime, aTxnStartTime,
                           aTxnURL, aFwdTime, mIsFirstPaint,
                           std::move(aPayloads));
  mIsFirstPaint = false;

  if (!validTransaction) {
    // Pretend we composited since someone is wating for this event,
    // though DisplayList was not pushed to webrender.
    if (CompositorBridgeParent* cbp = GetRootCompositorBridgeParent()) {
      TimeStamp now = TimeStamp::Now();
      cbp->NotifyPipelineRendered(mPipelineId, wrEpoch, VsyncId(), now, now,
                                  now);
    }
  }

  wr::IpcResourceUpdateQueue::ReleaseShmems(this, aDisplayList.mSmallShmems);
  wr::IpcResourceUpdateQueue::ReleaseShmems(this, aDisplayList.mLargeShmems);

  if (!success) {
    return IPC_FAIL(this, "Failed to process DisplayListData.");
  }

  return IPC_OK();
}

bool WebRenderBridgeParent::ProcessEmptyTransactionUpdates(
    TransactionData& aData, bool* aScheduleComposite) {
  *aScheduleComposite = false;
  wr::TransactionBuilder txn(mApi, /* aUseSceneBuilderThread */ true,
                             mRemoteTextureTxnScheduler, mFwdTransactionId);
  txn.SetLowPriority(!IsRootWebRenderBridgeParent());

  if (!aData.mScrollUpdates.IsEmpty()) {
    UpdateAPZScrollOffsets(std::move(aData.mScrollUpdates),
                           aData.mPaintSequenceNumber);
  }

  // Update WrEpoch for UpdateResources() and ProcessWebRenderParentCommands().
  // WrEpoch is used to manage ExternalImages lifetimes in
  // AsyncImagePipelineManager.
  Unused << GetNextWrEpoch();

  const bool validTransaction = aData.mIdNamespace == mIdNamespace;
  bool success = true;

  if (validTransaction) {
    success = UpdateResources(aData.mResourceUpdates, aData.mSmallShmems,
                              aData.mLargeShmems, txn);
    if (!aData.mCommands.IsEmpty()) {
      success = ProcessWebRenderParentCommands(aData.mCommands, txn) && success;
    }
  }

  MaybeNotifyOfLayers(txn, true);

  // Even when txn.IsResourceUpdatesEmpty() is true, there could be resource
  // updates. It is handled by WebRenderTextureHostWrapper. In this case
  // txn.IsRenderedFrameInvalidated() becomes true.
  if (!txn.IsResourceUpdatesEmpty() || txn.IsRenderedFrameInvalidated()) {
    // There are resource updates, then we update Epoch of transaction.
    txn.UpdateEpoch(mPipelineId, mWrEpoch);
    *aScheduleComposite = true;
    NeedIncreasedMaxDirtyPageModifier();
  } else {
    // If TransactionBuilder does not have resource updates nor display list,
    // ScheduleGenerateFrame is not triggered via SceneBuilder and there is no
    // need to update WrEpoch.
    // Then we want to rollback WrEpoch. See Bug 1490117.
    RollbackWrEpoch();
  }

  if (!txn.IsEmpty()) {
    mApi->SendTransaction(txn);
  }

  if (*aScheduleComposite) {
    mAsyncImageManager->SetWillGenerateFrame();
  }

  return success;
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvEmptyTransaction(
    const FocusTarget& aFocusTarget, Maybe<TransactionData>&& aTransactionData,
    nsTArray<OpDestroy>&& aToDestroy, const uint64_t& aFwdTransactionId,
    const TransactionId& aTransactionId, const VsyncId& aVsyncId,
    const TimeStamp& aVsyncStartTime, const TimeStamp& aRefreshStartTime,
    const TimeStamp& aTxnStartTime, const nsACString& aTxnURL,
    const TimeStamp& aFwdTime, nsTArray<CompositionPayload>&& aPayloads) {
  if (mDestroyed) {
    for (const auto& op : aToDestroy) {
      DestroyActor(op);
    }
    if (aTransactionData) {
      wr::IpcResourceUpdateQueue::ReleaseShmems(this,
                                                aTransactionData->mSmallShmems);
      wr::IpcResourceUpdateQueue::ReleaseShmems(this,
                                                aTransactionData->mLargeShmems);
    }
    return IPC_OK();
  }

  LOG("WebRenderBridgeParent::RecvEmptyTransaction() PipelineId %" PRIx64
      " Id %" PRIx64 " root %d",
      wr::AsUint64(mPipelineId), wr::AsUint64(mApi->GetId()),
      IsRootWebRenderBridgeParent());

  if (!IsRootWebRenderBridgeParent()) {
    CrashReporter::RecordAnnotationNSCString(CrashReporter::Annotation::URL,
                                             aTxnURL);
  }

  AUTO_PROFILER_TRACING_MARKER("Paint", "EmptyTransaction", GRAPHICS);
  UpdateFwdTransactionId(aFwdTransactionId);

  // This ensures that destroy operations are always processed. It is not safe
  // to early-return without doing so.
  AutoWebRenderBridgeParentAsyncMessageSender autoAsyncMessageSender(
      this, &aToDestroy);

  UpdateAPZFocusState(aFocusTarget);

  bool scheduleAnyComposite = false;
  wr::RenderReasons renderReasons = wr::RenderReasons::NONE;

  bool success = true;
  if (aTransactionData) {
    bool scheduleComposite = false;
    success =
        ProcessEmptyTransactionUpdates(*aTransactionData, &scheduleComposite);
    scheduleAnyComposite = scheduleAnyComposite || scheduleComposite;
    renderReasons |= wr::RenderReasons::RESOURCE_UPDATE;
  }

  // If we are going to kick off a new composite as a result of this
  // transaction, or if there are already composite-triggering pending
  // transactions inflight, then set sendDidComposite to false because we will
  // send the DidComposite message after the composite occurs.
  // If there are no pending transactions and we're not going to do a
  // composite, then we leave sendDidComposite as true so we just send
  // the DidComposite notification now.
  bool sendDidComposite =
      !scheduleAnyComposite && mPendingTransactionIds.empty();

  // Only register a value for CONTENT_FRAME_TIME telemetry if we actually drew
  // something. It is for consistency with disabling WebRender.
  HoldPendingTransactionId(mWrEpoch, aTransactionId, false, aVsyncId,
                           aVsyncStartTime, aRefreshStartTime, aTxnStartTime,
                           aTxnURL, aFwdTime,
                           /* aIsFirstPaint */ false, std::move(aPayloads),
                           /* aUseForTelemetry */ scheduleAnyComposite);

  if (scheduleAnyComposite) {
    ScheduleGenerateFrame(renderReasons);
  } else if (sendDidComposite) {
    // The only thing in the pending transaction id queue should be the entry
    // we just added, and now we're going to pretend we rendered it
    MOZ_ASSERT(mPendingTransactionIds.size() == 1);
    if (CompositorBridgeParent* cbp = GetRootCompositorBridgeParent()) {
      TimeStamp now = TimeStamp::Now();
      cbp->NotifyPipelineRendered(mPipelineId, mWrEpoch, VsyncId(), now, now,
                                  now);
    }
  }

  if (aTransactionData) {
    wr::IpcResourceUpdateQueue::ReleaseShmems(this,
                                              aTransactionData->mSmallShmems);
    wr::IpcResourceUpdateQueue::ReleaseShmems(this,
                                              aTransactionData->mLargeShmems);
  }

  if (!success) {
    return IPC_FAIL(this, "Failed to process empty transaction update.");
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvSetFocusTarget(
    const FocusTarget& aFocusTarget) {
  UpdateAPZFocusState(aFocusTarget);
  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvParentCommands(
    const wr::IdNamespace& aIdNamespace,
    nsTArray<WebRenderParentCommand>&& aCommands) {
  if (mDestroyed) {
    return IPC_OK();
  }

  const bool isValidMessage = aIdNamespace == mIdNamespace;
  if (!isValidMessage) {
    return IPC_OK();
  }

  LOG("WebRenderBridgeParent::RecvParentCommands() PipelineId %" PRIx64
      " Id %" PRIx64 " root %d",
      wr::AsUint64(mPipelineId), wr::AsUint64(mApi->GetId()),
      IsRootWebRenderBridgeParent());

  wr::TransactionBuilder txn(mApi);
  txn.SetLowPriority(!IsRootWebRenderBridgeParent());
  bool success = ProcessWebRenderParentCommands(aCommands, txn);
  NeedIncreasedMaxDirtyPageModifier();
  mApi->SendTransaction(txn);

  if (!success) {
    return IPC_FAIL(this, "Invalid parent command found");
  }

  return IPC_OK();
}

bool WebRenderBridgeParent::ProcessWebRenderParentCommands(
    const nsTArray<WebRenderParentCommand>& aCommands,
    wr::TransactionBuilder& aTxn) {
  // Transaction for async image pipeline that uses ImageBridge always need to
  // be non low priority.
  wr::TransactionBuilder txnForImageBridge(mApi->GetRootAPI());
  wr::AutoTransactionSender sender(mApi->GetRootAPI(), &txnForImageBridge);

  bool success = true;
  for (nsTArray<WebRenderParentCommand>::index_type i = 0;
       i < aCommands.Length(); ++i) {
    const WebRenderParentCommand& cmd = aCommands[i];
    switch (cmd.type()) {
      case WebRenderParentCommand::TOpAddPipelineIdForCompositable: {
        const OpAddPipelineIdForCompositable& op =
            cmd.get_OpAddPipelineIdForCompositable();
        AddPipelineIdForCompositable(op.pipelineId(), op.handle(), op.owner(),
                                     aTxn, txnForImageBridge);
        break;
      }
      case WebRenderParentCommand::TOpRemovePipelineIdForCompositable: {
        const OpRemovePipelineIdForCompositable& op =
            cmd.get_OpRemovePipelineIdForCompositable();
        auto* pendingOps = mApi->GetPendingAsyncImagePipelineOps(aTxn);

        RemovePipelineIdForCompositable(op.pipelineId(), pendingOps, aTxn);
        break;
      }
      case WebRenderParentCommand::TOpReleaseTextureOfImage: {
        const OpReleaseTextureOfImage& op = cmd.get_OpReleaseTextureOfImage();
        ReleaseTextureOfImage(op.key());
        break;
      }
      case WebRenderParentCommand::TOpUpdateAsyncImagePipeline: {
        const OpUpdateAsyncImagePipeline& op =
            cmd.get_OpUpdateAsyncImagePipeline();

        auto* pendingOps = mApi->GetPendingAsyncImagePipelineOps(aTxn);
        auto* pendingRemotetextures = mApi->GetPendingRemoteTextureInfoList();

        mAsyncImageManager->UpdateAsyncImagePipeline(
            op.pipelineId(), op.scBounds(), op.rotation(), op.filter(),
            op.mixBlendMode());
        MOZ_ASSERT_IF(IsRootWebRenderBridgeParent(), !pendingRemotetextures);
        mAsyncImageManager->ApplyAsyncImageForPipeline(
            op.pipelineId(), aTxn, txnForImageBridge, pendingOps,
            pendingRemotetextures);
        break;
      }
      case WebRenderParentCommand::TOpUpdatedAsyncImagePipeline: {
        const OpUpdatedAsyncImagePipeline& op =
            cmd.get_OpUpdatedAsyncImagePipeline();
        aTxn.InvalidateRenderedFrame(wr::RenderReasons::ASYNC_IMAGE);

        auto* pendingOps = mApi->GetPendingAsyncImagePipelineOps(aTxn);
        auto* pendingRemotetextures = mApi->GetPendingRemoteTextureInfoList();

        MOZ_ASSERT_IF(IsRootWebRenderBridgeParent(), !pendingRemotetextures);
        mAsyncImageManager->ApplyAsyncImageForPipeline(
            op.pipelineId(), aTxn, txnForImageBridge, pendingOps,
            pendingRemotetextures);
        break;
      }
      case WebRenderParentCommand::TCompositableOperation: {
        if (!ReceiveCompositableUpdate(cmd.get_CompositableOperation())) {
          NS_ERROR("ReceiveCompositableUpdate failed");
        }
        break;
      }
      case WebRenderParentCommand::TOpAddCompositorAnimations: {
        const OpAddCompositorAnimations& op =
            cmd.get_OpAddCompositorAnimations();
        CompositorAnimations data(std::move(op.data()));
        // AnimationHelper::GetNextCompositorAnimationsId() encodes the child
        // process PID in the upper 32 bits of the id, verify that this is as
        // expected.
        if ((data.id() >> 32) != (uint64_t)OtherPid()) {
          gfxCriticalNote << "TOpAddCompositorAnimations bad id";
          success = false;
          continue;
        }
        if (data.animations().Length()) {
          if (RefPtr<OMTASampler> sampler = GetOMTASampler()) {
            sampler->SetAnimations(data.id(), GetLayersId(), data.animations());
            const auto activeAnim = mActiveAnimations.find(data.id());
            if (activeAnim == mActiveAnimations.end()) {
              mActiveAnimations.emplace(data.id(), mWrEpoch);
            } else {
              // Update wr::Epoch if the animation already exists.
              activeAnim->second = mWrEpoch;
            }
          }
        }
        break;
      }
      default: {
        // other commands are handle on the child
        break;
      }
    }
  }

  MOZ_ASSERT(success);
  return success;
}

void WebRenderBridgeParent::FlushSceneBuilds() {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  // Since we are sending transactions through the scene builder thread, we need
  // to block until all the inflight transactions have been processed. This
  // flush message blocks until all previously sent scenes have been built
  // and received by the render backend thread.
  mApi->FlushSceneBuilder();
  // The post-swap hook for async-scene-building calls the
  // ScheduleRenderOnCompositorThread function from the scene builder thread,
  // which then triggers a call to ScheduleGenerateFrame() on the compositor
  // thread. But since *this* function is running on the compositor thread,
  // that scheduling will not happen until this call stack unwinds (or we
  // could spin a nested event loop, but that's more messy). Instead, we
  // simulate it ourselves by calling ScheduleGenerateFrame() directly.
  // Note also that the post-swap hook will run and do another
  // ScheduleGenerateFrame() after we unwind here, so we will end up with an
  // extra render/composite that is probably avoidable, but in practice we
  // shouldn't be calling this function all that much in production so this
  // is probably fine. If it becomes an issue we can add more state tracking
  // machinery to optimize it away.
  ScheduleGenerateFrame(wr::RenderReasons::FLUSH);
}

void WebRenderBridgeParent::FlushFrameGeneration(wr::RenderReasons aReasons) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MOZ_ASSERT(IsRootWebRenderBridgeParent());  // This function is only useful on
                                              // the root WRBP

  // This forces a new GenerateFrame transaction to be sent to the render
  // backend thread, if one is pending. This doesn't block on any other threads.
  if (mCompositorScheduler->NeedsComposite()) {
    mCompositorScheduler->CancelCurrentCompositeTask();
    // Update timestamp of scheduler for APZ and animation.
    mCompositorScheduler->UpdateLastComposeTime();
    MaybeGenerateFrame(VsyncId(), /* aForceGenerateFrame */ true,
                       aReasons | wr::RenderReasons::FLUSH);
  }
}

void WebRenderBridgeParent::FlushFramePresentation() {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  // This sends a message to the render backend thread to send a message
  // to the renderer thread, and waits for that message to be processed. So
  // this effectively blocks on the render backend and renderer threads,
  // following the same codepath that WebRender takes to render and composite
  // a frame.
  mApi->WaitFlushed();
}

void WebRenderBridgeParent::DisableNativeCompositor() {
  // Make sure that SceneBuilder thread does not have a task.
  mApi->FlushSceneBuilder();
  // Disable WebRender's native compositor usage
  mApi->EnableNativeCompositor(false);
  // Ensure we generate and render a frame immediately.
  ScheduleForcedGenerateFrame(wr::RenderReasons::CONFIG_CHANGE);

  mDisablingNativeCompositor = true;
}

void WebRenderBridgeParent::UpdateQualitySettings() {
  if (!IsRootWebRenderBridgeParent()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }
  wr::TransactionBuilder txn(mApi);
  txn.UpdateQualitySettings(gfxVars::ForceSubpixelAAWherePossible());
  mApi->SendTransaction(txn);
}

void WebRenderBridgeParent::UpdateDebugFlags() {
  if (!IsRootWebRenderBridgeParent()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  mApi->UpdateDebugFlags(gfxVars::WebRenderDebugFlags());
}

void WebRenderBridgeParent::UpdateProfilerUI() {
  if (!IsRootWebRenderBridgeParent()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  nsCString uiString = gfxVars::GetWebRenderProfilerUIOrDefault();
  mApi->SetProfilerUI(uiString);
}

void WebRenderBridgeParent::UpdateParameters() {
  if (!IsRootWebRenderBridgeParent()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  uint32_t count = gfxVars::WebRenderBatchingLookback();
  mApi->SetBatchingLookback(count);
  mApi->SetInt(wr::IntParameter::BatchedUploadThreshold,
               gfxVars::WebRenderBatchedUploadThreshold());
  uint32_t slow_cpu_frame = gfxVars::WebRenderSlowCpuFrameThreshold();
  mApi->SetFloat(wr::FloatParameter::SlowCpuFrameThreshold, slow_cpu_frame);

  mBlobTileSize = gfxVars::WebRenderBlobTileSize();
}

void WebRenderBridgeParent::UpdateBoolParameters() {
  if (!IsRootWebRenderBridgeParent()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  uint32_t bits = gfxVars::WebRenderBoolParameters();
  uint32_t changedBits = mBoolParameterBits ^ bits;

  for (auto paramName : MakeEnumeratedRange(wr::BoolParameter::Sentinel)) {
    uint32_t i = (uint32_t)paramName;
    if (changedBits & (1 << i)) {
      bool value = (bits & (1 << i)) != 0;
      mApi->SetBool(paramName, value);
    }
  }
  mBoolParameterBits = bits;
}

#if defined(MOZ_WIDGET_ANDROID)
void WebRenderBridgeParent::RequestScreenPixels(
    UiCompositorControllerParent* aController) {
  mScreenPixelsTarget = aController;
}

void WebRenderBridgeParent::MaybeCaptureScreenPixels() {
  if (!mScreenPixelsTarget) {
    return;
  }

  if (mDestroyed) {
    return;
  }

  if (auto* cbp = GetRootCompositorBridgeParent()) {
    cbp->FlushPendingWrTransactionEventsWithWait();
  }

  // This function should only get called in the root WRBP.
  MOZ_ASSERT(IsRootWebRenderBridgeParent());
#  ifdef DEBUG
  CompositorBridgeParent* cbp = GetRootCompositorBridgeParent();
  MOZ_ASSERT(cbp && !cbp->IsPaused());
#  endif

  SurfaceFormat format = SurfaceFormat::R8G8B8A8;  // On android we use RGBA8
  auto client_size = mWidget->GetClientSize();
  size_t buffer_size =
      client_size.width * client_size.height * BytesPerPixel(format);

  ipc::Shmem mem;
  if (!mScreenPixelsTarget->AllocPixelBuffer(buffer_size, &mem)) {
    // Failed to alloc shmem, Just bail out.
    return;
  }

  IntSize size(client_size.width, client_size.height);

  bool needsYFlip = false;
  mApi->Readback(TimeStamp::Now(), size, format,
                 Range<uint8_t>(mem.get<uint8_t>(), buffer_size), &needsYFlip);

  Unused << mScreenPixelsTarget->SendScreenPixels(
      std::move(mem), ScreenIntSize(client_size.width, client_size.height),
      needsYFlip);

  mScreenPixelsTarget = nullptr;
}
#endif

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvGetSnapshot(
    NotNull<PTextureParent*> aTexture, bool* aNeedsYFlip) {
  *aNeedsYFlip = false;
  CompositorBridgeParent* cbp = GetRootCompositorBridgeParent();
  if (mDestroyed || !cbp || cbp->IsPaused()) {
    return IPC_OK();
  }

  if (auto* cbp = GetRootCompositorBridgeParent()) {
    cbp->FlushPendingWrTransactionEventsWithWait();
  }

  LOG("WebRenderBridgeParent::RecvGetSnapshot() PipelineId %" PRIx64
      " Id %" PRIx64 " root %d",
      wr::AsUint64(mPipelineId), wr::AsUint64(mApi->GetId()),
      IsRootWebRenderBridgeParent());

  // This function should only get called in the root WRBP. If this function
  // gets called in a non-root WRBP, we will set mForceRendering in this WRBP
  // but it will have no effect because CompositeToTarget (which reads the
  // flag) only gets invoked in the root WRBP. So we assert that this is the
  // root WRBP (i.e. has a non-null mWidget) to catch violations of this rule.
  MOZ_ASSERT(IsRootWebRenderBridgeParent());

  RefPtr<TextureHost> texture = TextureHost::AsTextureHost(aTexture);
  if (!texture) {
    // We kill the content process rather than have it continue with an invalid
    // snapshot, that may be too harsh and we could decide to return some sort
    // of error to the child process and let it deal with it...
    return IPC_FAIL_NO_REASON(this);
  }

  // XXX Add other TextureHost supports.
  // Only BufferTextureHost is supported now.
  BufferTextureHost* bufferTexture = texture->AsBufferTextureHost();
  if (!bufferTexture) {
    // We kill the content process rather than have it continue with an invalid
    // snapshot, that may be too harsh and we could decide to return some sort
    // of error to the child process and let it deal with it...
    return IPC_FAIL_NO_REASON(this);
  }

  TimeStamp start = TimeStamp::Now();
  MOZ_ASSERT(bufferTexture->GetBufferDescriptor().type() ==
             BufferDescriptor::TRGBDescriptor);
  DebugOnly<uint32_t> stride = ImageDataSerializer::GetRGBStride(
      bufferTexture->GetBufferDescriptor().get_RGBDescriptor());
  uint8_t* buffer = bufferTexture->GetBuffer();
  IntSize size = bufferTexture->GetSize();

  MOZ_ASSERT(buffer);
  // For now the only formats we get here are RGBA and BGRA, and code below is
  // assuming a bpp of 4. If we allow other formats, the code needs adjusting
  // accordingly.
  MOZ_ASSERT(BytesPerPixel(bufferTexture->GetFormat()) == 4);
  uint32_t buffer_size = size.width * size.height * 4;

  // Assert the stride of the buffer is what webrender expects
  MOZ_ASSERT((uint32_t)(size.width * 4) == stride);

  FlushSceneBuilds();
  FlushFrameGeneration(wr::RenderReasons::SNAPSHOT);
  mApi->Readback(start, size, bufferTexture->GetFormat(),
                 Range<uint8_t>(buffer, buffer_size), aNeedsYFlip);

  return IPC_OK();
}

void WebRenderBridgeParent::AddPipelineIdForCompositable(
    const wr::PipelineId& aPipelineId, const CompositableHandle& aHandle,
    const CompositableHandleOwner& aOwner, wr::TransactionBuilder& aTxn,
    wr::TransactionBuilder& aTxnForImageBridge) {
  if (mDestroyed) {
    return;
  }

  MOZ_ASSERT(mAsyncCompositables.find(wr::AsUint64(aPipelineId)) ==
             mAsyncCompositables.end());

  RefPtr<CompositableHost> host;
  switch (aOwner) {
    case CompositableHandleOwner::WebRenderBridge:
      host = FindCompositable(aHandle);
      break;
    case CompositableHandleOwner::ImageBridge: {
      RefPtr<ImageBridgeParent> imageBridge =
          ImageBridgeParent::GetInstance(OtherPid());
      if (!imageBridge) {
        return;
      }
      host = imageBridge->FindCompositable(aHandle);
      break;
    }
  }

  if (!host) {
    return;
  }

  WebRenderImageHost* wrHost = host->AsWebRenderImageHost();
  MOZ_ASSERT(wrHost);
  if (!wrHost) {
    gfxCriticalNote
        << "Incompatible CompositableHost at WebRenderBridgeParent.";
  }

  if (!wrHost) {
    return;
  }

  wrHost->SetWrBridge(aPipelineId, this);
  mAsyncCompositables.emplace(wr::AsUint64(aPipelineId), wrHost);
  mAsyncImageManager->AddAsyncImagePipeline(aPipelineId, wrHost);

  // If this is being called from WebRenderBridgeParent::RecvSetDisplayList,
  // then aTxn might contain a display list that references pipelines that
  // we just added to the async image manager.
  // If we send the display list alone then WR will not yet have the content for
  // the pipelines and so it will emit errors; the SetEmptyDisplayList call
  // below ensure that we provide its content to WR as part of the same
  // transaction.
  mAsyncImageManager->SetEmptyDisplayList(aPipelineId, aTxn,
                                          aTxnForImageBridge);
}

void WebRenderBridgeParent::RemovePipelineIdForCompositable(
    const wr::PipelineId& aPipelineId, AsyncImagePipelineOps* aPendingOps,
    wr::TransactionBuilder& aTxn) {
  if (mDestroyed) {
    return;
  }

  auto it = mAsyncCompositables.find(wr::AsUint64(aPipelineId));
  if (it == mAsyncCompositables.end()) {
    return;
  }
  RefPtr<WebRenderImageHost>& wrHost = it->second;

  wrHost->ClearWrBridge(aPipelineId, this);
  mAsyncImageManager->RemoveAsyncImagePipeline(aPipelineId, aPendingOps, aTxn);
  aTxn.RemovePipeline(aPipelineId);
  mAsyncCompositables.erase(wr::AsUint64(aPipelineId));
}

void WebRenderBridgeParent::DeleteImage(const ImageKey& aKey,
                                        wr::TransactionBuilder& aUpdates) {
  if (mDestroyed) {
    return;
  }

  auto it = mSharedSurfaceIds.find(wr::AsUint64(aKey));
  if (it != mSharedSurfaceIds.end()) {
    mAsyncImageManager->HoldExternalImage(mPipelineId, mWrEpoch, it->second);
    mSharedSurfaceIds.erase(it);
  }

  aUpdates.DeleteImage(aKey);
}

void WebRenderBridgeParent::ReleaseTextureOfImage(const wr::ImageKey& aKey) {
  if (mDestroyed) {
    return;
  }

  uint64_t id = wr::AsUint64(aKey);
  CompositableTextureHostRef texture;
  WebRenderTextureHost* wrTexture = nullptr;

  auto it = mTextureHosts.find(id);
  if (it != mTextureHosts.end()) {
    wrTexture = (*it).second->AsWebRenderTextureHost();
  }
  if (wrTexture) {
    mAsyncImageManager->HoldExternalImage(mPipelineId, mWrEpoch, wrTexture);
  }
  mTextureHosts.erase(id);
}

void WebRenderBridgeParent::MaybeNotifyOfLayers(
    wr::TransactionBuilder& aBuilder, bool aWillHaveLayers) {
  if (mLastNotifiedHasLayers == aWillHaveLayers) {
    return;
  }

  aBuilder.Notify(wr::Checkpoint::SceneBuilt,
                  MakeUnique<ScheduleObserveLayersUpdate>(
                      mCompositorBridge, GetLayersId(), aWillHaveLayers));
  mLastNotifiedHasLayers = aWillHaveLayers;
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvClearCachedResources() {
  if (mDestroyed) {
    return IPC_OK();
  }

  LOG("WebRenderBridgeParent::RecvClearCachedResources() PipelineId %" PRIx64
      " Id %" PRIx64 " root %d",
      wr::AsUint64(mPipelineId), wr::AsUint64(mApi->GetId()),
      IsRootWebRenderBridgeParent());

  if (!IsRootWebRenderBridgeParent()) {
    mApi->FlushPendingWrTransactionEventsWithoutWait();
  }

  // Clear resources
  wr::TransactionBuilder txn(mApi);
  txn.SetLowPriority(true);
  txn.ClearDisplayList(GetNextWrEpoch(), mPipelineId);
  MaybeNotifyOfLayers(txn, false);
  mApi->SendTransaction(txn);

  // Schedule generate frame to clean up Pipeline
  ScheduleGenerateFrame(wr::RenderReasons::CLEAR_RESOURCES);

  ClearAnimationResources();

  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvClearAnimationResources() {
  if (!mDestroyed) {
    ClearAnimationResources();
  }

  return IPC_OK();
}

wr::Epoch WebRenderBridgeParent::UpdateWebRender(
    CompositorVsyncScheduler* aScheduler, RefPtr<wr::WebRenderAPI>&& aApi,
    AsyncImagePipelineManager* aImageMgr,
    const TextureFactoryIdentifier& aTextureFactoryIdentifier) {
  MOZ_ASSERT(!IsRootWebRenderBridgeParent());
  MOZ_ASSERT(aScheduler);
  MOZ_ASSERT(aApi);
  MOZ_ASSERT(aImageMgr);

  if (mDestroyed) {
    return mWrEpoch;
  }

  // Update id name space to identify obsoleted keys.
  // Since usage of invalid keys could cause crash in webrender.
  mIdNamespace = aApi->GetNamespace();
  // XXX Remove it when webrender supports sharing/moving Keys between different
  // webrender instances.
  // XXX It requests client to update/reallocate webrender related resources,
  // but parent side does not wait end of the update.
  // The code could become simpler if we could serialise old keys deallocation
  // and new keys allocation. But we do not do it, it is because client side
  // deallocate old layers/webrender keys after new layers/webrender keys
  // allocation. Without client side's layout refactoring, we could not finish
  // all old layers/webrender keys removals before new layer/webrender keys
  // allocation. In future, we could address the problem.
  Unused << SendWrUpdated(mIdNamespace, aTextureFactoryIdentifier);
  CompositorBridgeParentBase* cBridge = mCompositorBridge;
  // XXX Stop to clear resources if webreder supports resources sharing between
  // different webrender instances.
  ClearResources();
  mCompositorBridge = cBridge;
  mCompositorScheduler = aScheduler;
  mApi = aApi;
  mAsyncImageManager = aImageMgr;

  // Register pipeline to updated AsyncImageManager.
  mAsyncImageManager->AddPipeline(mPipelineId, this);

  LOG("WebRenderBridgeParent::UpdateWebRender() PipelineId %" PRIx64
      " Id %" PRIx64 " root %d",
      wr::AsUint64(mPipelineId), wr::AsUint64(mApi->GetId()),
      IsRootWebRenderBridgeParent());

  return GetNextWrEpoch();  // Update webrender epoch
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvInvalidateRenderedFrame() {
  // This function should only get called in the root WRBP
  MOZ_ASSERT(IsRootWebRenderBridgeParent());
  LOG("WebRenderBridgeParent::RecvInvalidateRenderedFrame() PipelineId %" PRIx64
      " Id %" PRIx64 " root %d",
      wr::AsUint64(mPipelineId), wr::AsUint64(mApi->GetId()),
      IsRootWebRenderBridgeParent());

  InvalidateRenderedFrame(wr::RenderReasons::WIDGET);
  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvScheduleComposite(
    const wr::RenderReasons& aReasons) {
  LOG("WebRenderBridgeParent::RecvScheduleComposite() PipelineId %" PRIx64
      " Id %" PRIx64 " root %d",
      wr::AsUint64(mPipelineId), wr::AsUint64(mApi->GetId()),
      IsRootWebRenderBridgeParent());

  // Caller of LayerManager::ScheduleComposite() expects that it trigger
  // composite. Then we do not want to skip generate frame.
  ScheduleForcedGenerateFrame(aReasons);
  return IPC_OK();
}

void WebRenderBridgeParent::InvalidateRenderedFrame(
    wr::RenderReasons aReasons) {
  if (mDestroyed) {
    return;
  }

  wr::TransactionBuilder fastTxn(mApi, /* aUseSceneBuilderThread */ false);
  fastTxn.InvalidateRenderedFrame(aReasons);
  mApi->SendTransaction(fastTxn);
}

void WebRenderBridgeParent::ScheduleForcedGenerateFrame(
    wr::RenderReasons aReasons) {
  if (mDestroyed) {
    return;
  }

  InvalidateRenderedFrame(aReasons);
  ScheduleGenerateFrame(aReasons);
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvCapture() {
  if (!mDestroyed) {
    mApi->Capture();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvStartCaptureSequence(
    const nsACString& aPath, const uint32_t& aFlags) {
  if (!mDestroyed) {
    mApi->StartCaptureSequence(aPath, aFlags);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvStopCaptureSequence() {
  if (!mDestroyed) {
    mApi->StopCaptureSequence();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvSyncWithCompositor() {
  LOG("WebRenderBridgeParent::RecvSyncWithCompositor() PipelineId %" PRIx64
      " Id %" PRIx64 " root %d",
      wr::AsUint64(mPipelineId), wr::AsUint64(mApi->GetId()),
      IsRootWebRenderBridgeParent());

  if (mDestroyed) {
    return IPC_OK();
  }

  FlushSceneBuilds();
  if (RefPtr<WebRenderBridgeParent> root = GetRootWebRenderBridgeParent()) {
    root->FlushFrameGeneration(wr::RenderReasons::CONTENT_SYNC);
  }
  FlushFramePresentation();
  // Finally, we force the AsyncImagePipelineManager to handle all the
  // pipeline updates produced in the last step, so that it frees any
  // unneeded textures. Then we can return from this sync IPC call knowing
  // that we've done everything we can to flush stuff on the compositor.
  mAsyncImageManager->ProcessPipelineUpdates();

  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvSetConfirmedTargetAPZC(
    const uint64_t& aBlockId, nsTArray<ScrollableLayerGuid>&& aTargets) {
  for (size_t i = 0; i < aTargets.Length(); i++) {
    // Guard against bad data from hijacked child processes
    if (aTargets[i].mLayersId != GetLayersId()) {
      NS_ERROR(
          "Unexpected layers id in RecvSetConfirmedTargetAPZC; dropping "
          "message...");
      return IPC_FAIL(this, "Bad layers id");
    }
  }

  if (mDestroyed) {
    return IPC_OK();
  }
  mCompositorBridge->SetConfirmedTargetAPZC(GetLayersId(), aBlockId,
                                            std::move(aTargets));
  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvSetTestSampleTime(
    const TimeStamp& aTime) {
  if (mDestroyed) {
    return IPC_FAIL_NO_REASON(this);
  }

  if (!mCompositorBridge->SetTestSampleTime(GetLayersId(), aTime)) {
    return IPC_FAIL_NO_REASON(this);
  }
  if (RefPtr<OMTASampler> sampler = GetOMTASampler()) {
    sampler->EnterTestMode();
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvLeaveTestMode() {
  if (mDestroyed) {
    return IPC_FAIL_NO_REASON(this);
  }

  mCompositorBridge->LeaveTestMode(GetLayersId());
  if (RefPtr<OMTASampler> sampler = GetOMTASampler()) {
    sampler->LeaveTestMode();
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvGetAnimationValue(
    const uint64_t& aCompositorAnimationsId, OMTAValue* aValue) {
  if (mDestroyed) {
    return IPC_FAIL_NO_REASON(this);
  }

  if (RefPtr<OMTASampler> sampler = GetOMTASampler()) {
    Maybe<TimeStamp> testingTimeStamp;
    if (CompositorBridgeParent* cbp = GetRootCompositorBridgeParent()) {
      testingTimeStamp = cbp->GetTestingTimeStamp();
    }

    sampler->SampleForTesting(testingTimeStamp);
    *aValue = sampler->GetOMTAValue(aCompositorAnimationsId);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvSetAsyncScrollOffset(
    const ScrollableLayerGuid::ViewID& aScrollId, const float& aX,
    const float& aY) {
  if (mDestroyed) {
    return IPC_OK();
  }
  mCompositorBridge->SetTestAsyncScrollOffset(GetLayersId(), aScrollId,
                                              CSSPoint(aX, aY));
  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvSetAsyncZoom(
    const ScrollableLayerGuid::ViewID& aScrollId, const float& aZoom) {
  if (mDestroyed) {
    return IPC_OK();
  }
  mCompositorBridge->SetTestAsyncZoom(GetLayersId(), aScrollId,
                                      LayerToParentLayerScale(aZoom));
  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvFlushApzRepaints() {
  if (mDestroyed) {
    return IPC_OK();
  }
  mCompositorBridge->FlushApzRepaints(GetLayersId());
  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvGetAPZTestData(
    APZTestData* aOutData) {
  if (mDestroyed) {
    return IPC_FAIL_NO_REASON(this);
  }

  mCompositorBridge->GetAPZTestData(GetLayersId(), aOutData);
  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvGetFrameUniformity(
    FrameUniformityData* aOutData) {
  if (mDestroyed) {
    return IPC_FAIL_NO_REASON(this);
  }

  mCompositorBridge->GetFrameUniformity(GetLayersId(), aOutData);
  return IPC_OK();
}

void WebRenderBridgeParent::ActorDestroy(ActorDestroyReason aWhy) { Destroy(); }

void WebRenderBridgeParent::ResetPreviousSampleTime() {
  if (RefPtr<OMTASampler> sampler = GetOMTASampler()) {
    sampler->ResetPreviousSampleTime();
  }
}

RefPtr<OMTASampler> WebRenderBridgeParent::GetOMTASampler() const {
  CompositorBridgeParent* cbp = GetRootCompositorBridgeParent();
  if (!cbp) {
    return nullptr;
  }
  return cbp->GetOMTASampler();
}

void WebRenderBridgeParent::SetOMTASampleTime() {
  MOZ_ASSERT(IsRootWebRenderBridgeParent());
  if (RefPtr<OMTASampler> sampler = GetOMTASampler()) {
    sampler->SetSampleTime(mCompositorScheduler->GetLastComposeTime().Time());
  }
}

void WebRenderBridgeParent::RetrySkippedComposite() {
  if (!mSkippedComposite) {
    return;
  }

  mSkippedComposite = false;
  if (mCompositorScheduler) {
    mCompositorScheduler->ScheduleComposition(mSkippedCompositeReasons |
                                              RenderReasons::SKIPPED_COMPOSITE);
  }
  mSkippedCompositeReasons = wr::RenderReasons::NONE;
}

void WebRenderBridgeParent::CompositeToTarget(VsyncId aId,
                                              wr::RenderReasons aReasons,
                                              gfx::DrawTarget* aTarget,
                                              const gfx::IntRect* aRect) {
  // This function should only get called in the root WRBP
  MOZ_ASSERT(IsRootWebRenderBridgeParent());

  // The two arguments are part of the CompositorVsyncSchedulerOwner API but in
  // this implementation they should never be non-null.
  MOZ_ASSERT(aTarget == nullptr);
  MOZ_ASSERT(aRect == nullptr);

  LOG("WebRenderBridgeParent::CompositeToTarget() PipelineId %" PRIx64
      " Id %" PRIx64 " root %d",
      wr::AsUint64(mPipelineId), wr::AsUint64(mApi->GetId()),
      IsRootWebRenderBridgeParent());

  CompositorBridgeParent* cbp = GetRootCompositorBridgeParent();
  uint64_t innerWindowId = cbp ? cbp->GetInnerWindowId() : 0;
  AUTO_PROFILER_TRACING_MARKER_INNERWINDOWID("Paint", "CompositeToTarget",
                                             GRAPHICS, innerWindowId);

  bool paused = true;
  if (cbp) {
    paused = cbp->IsPaused();
  }

  if (paused || !mReceivedDisplayList) {
    ResetPreviousSampleTime();
    mCompositionOpportunityId = mCompositionOpportunityId.Next();
    PROFILER_MARKER_TEXT("Discarded composite", GRAPHICS,
                         MarkerInnerWindowId(innerWindowId),
                         paused ? "Paused"_ns : "No display list"_ns);
    return;
  }

  mSkippedComposite =
      wr::RenderThread::Get()->TooManyPendingFrames(mApi->GetId());

  if (mSkippedComposite) {
    // Render thread is busy, try next time.
    mSkippedComposite = true;
    mSkippedCompositeReasons = mSkippedCompositeReasons | aReasons;
    ResetPreviousSampleTime();

    // Record that we skipped presenting a frame for
    // all pending transactions that have finished scene building.
    for (auto& id : mPendingTransactionIds) {
      if (id.mSceneBuiltTime) {
        id.mSkippedComposites++;
      }
    }

    PROFILER_MARKER_TEXT("SkippedComposite", GRAPHICS,
                         MarkerInnerWindowId(innerWindowId),
                         "Too many pending frames");

    glean::gfx::skipped_composites.Add(1);

    return;
  }

  mCompositionOpportunityId = mCompositionOpportunityId.Next();
  MaybeGenerateFrame(aId, /* aForceGenerateFrame */ false, aReasons);
}

TimeDuration WebRenderBridgeParent::GetVsyncInterval() const {
  // This function should only get called in the root WRBP
  MOZ_ASSERT(IsRootWebRenderBridgeParent());
  if (CompositorBridgeParent* cbp = GetRootCompositorBridgeParent()) {
    return cbp->GetVsyncInterval();
  }
  return TimeDuration();
}

void WebRenderBridgeParent::MaybeGenerateFrame(VsyncId aId,
                                               bool aForceGenerateFrame,
                                               wr::RenderReasons aReasons) {
  // This function should only get called in the root WRBP
  MOZ_ASSERT(IsRootWebRenderBridgeParent());
  LOG("WebRenderBridgeParent::MaybeGenerateFrame() PipelineId %" PRIx64
      " Id %" PRIx64 " root %d",
      wr::AsUint64(mPipelineId), wr::AsUint64(mApi->GetId()),
      IsRootWebRenderBridgeParent());

  if (CompositorBridgeParent* cbp = GetRootCompositorBridgeParent()) {
    // Skip WR render during paused state.
    if (cbp->IsPaused()) {
      TimeStamp now = TimeStamp::Now();
      PROFILER_MARKER_TEXT(
          "SkippedComposite", GRAPHICS,
          MarkerOptions(MarkerInnerWindowId(cbp->GetInnerWindowId()),
                        MarkerTiming::InstantAt(now)),
          "CompositorBridgeParent is paused");
      cbp->NotifyPipelineRendered(mPipelineId, mWrEpoch, VsyncId(), now, now,
                                  now);
      return;
    }
  }

  TimeStamp start = TimeStamp::Now();

  // Ensure GenerateFrame is handled on the render backend thread rather
  // than going through the scene builder thread. That way we continue
  // generating frames with the old scene even during slow scene builds.
  wr::TransactionBuilder fastTxn(mApi, false /* useSceneBuilderThread */);
  // Handle transaction that is related to DisplayList.
  wr::TransactionBuilder sceneBuilderTxn(mApi);
  wr::AutoTransactionSender sender(mApi, &sceneBuilderTxn);

  mAsyncImageManager->SetCompositionInfo(start, mCompositionOpportunityId);
  mAsyncImageManager->ApplyAsyncImagesOfImageBridge(sceneBuilderTxn, fastTxn);
  mAsyncImageManager->SetCompositionInfo(TimeStamp(),
                                         CompositionOpportunityId{});

  if (!mAsyncImageManager->GetCompositeUntilTime().IsNull()) {
    // Trigger another CompositeToTarget() call because there might be another
    // frame that we want to generate after this one.
    // It will check if we actually want to generate the frame or not.
    mCompositorScheduler->ScheduleComposition(
        wr::RenderReasons::ASYNC_IMAGE_COMPOSITE_UNTIL);
  }

  bool generateFrame = !fastTxn.IsEmpty() || aForceGenerateFrame;

  if (mAsyncImageManager->GetAndResetWillGenerateFrame()) {
    aReasons |= wr::RenderReasons::ASYNC_IMAGE;
    generateFrame = true;
  }

  if (!generateFrame) {
    // Could skip generating frame now.
    PROFILER_MARKER_TEXT("SkippedComposite", GRAPHICS,
                         MarkerTiming::InstantAt(start),
                         "No reason to generate frame");
    ResetPreviousSampleTime();
    return;
  }

  if (RefPtr<OMTASampler> sampler = GetOMTASampler()) {
    if (sampler->HasAnimations()) {
      ScheduleGenerateFrame(wr::RenderReasons::ANIMATED_PROPERTY);
    }
  }

  SetOMTASampleTime();
  SetAPZSampleTime();

#if defined(ENABLE_FRAME_LATENCY_LOG)
  auto startTime = TimeStamp::Now();
  mApi->SetFrameStartTime(startTime);
#endif

  fastTxn.GenerateFrame(aId, true, aReasons);
  wr::RenderThread::Get()->IncPendingFrameCount(mApi->GetId(), aId, start);

  NeedIncreasedMaxDirtyPageModifier();

  mApi->SendTransaction(fastTxn);

#if defined(MOZ_WIDGET_ANDROID)
  MaybeCaptureScreenPixels();
#endif

  mMostRecentComposite = TimeStamp::Now();

  // During disabling native compositor, webrender needs to render twice.
  // Otherwise, browser flashes black.
  // XXX better fix?
  if (mDisablingNativeCompositor) {
    mDisablingNativeCompositor = false;

    // Ensure we generate and render a frame immediately.
    ScheduleForcedGenerateFrame(aReasons);
  }
}

void WebRenderBridgeParent::HoldPendingTransactionId(
    const wr::Epoch& aWrEpoch, TransactionId aTransactionId,
    bool aContainsSVGGroup, const VsyncId& aVsyncId,
    const TimeStamp& aVsyncStartTime, const TimeStamp& aRefreshStartTime,
    const TimeStamp& aTxnStartTime, const nsACString& aTxnURL,
    const TimeStamp& aFwdTime, const bool aIsFirstPaint,
    nsTArray<CompositionPayload>&& aPayloads, const bool aUseForTelemetry) {
  MOZ_ASSERT(aTransactionId > LastPendingTransactionId());
  mPendingTransactionIds.push_back(PendingTransactionId(
      aWrEpoch, aTransactionId, aContainsSVGGroup, aVsyncId, aVsyncStartTime,
      aRefreshStartTime, aTxnStartTime, aTxnURL, aFwdTime, aIsFirstPaint,
      aUseForTelemetry, std::move(aPayloads)));
}

TransactionId WebRenderBridgeParent::LastPendingTransactionId() {
  TransactionId id{0};
  if (!mPendingTransactionIds.empty()) {
    id = mPendingTransactionIds.back().mId;
  }
  return id;
}

void WebRenderBridgeParent::NotifySceneBuiltForEpoch(
    const wr::Epoch& aEpoch, const TimeStamp& aEndTime) {
  for (auto& id : mPendingTransactionIds) {
    if (id.mEpoch.mHandle == aEpoch.mHandle) {
      id.mSceneBuiltTime = aEndTime;
      break;
    }
  }
}

void WebRenderBridgeParent::ScheduleFrameAfterSceneBuild(
    RefPtr<const wr::WebRenderPipelineInfo> aInfo) {
  MOZ_ASSERT(IsRootWebRenderBridgeParent());
  if (!mCompositorScheduler) {
    return;
  }

  mAsyncImageManager->SetWillGenerateFrame();

  // If the scheduler has a composite more recent than our last composite (which
  // we missed), and we're within the threshold ms of the last vsync, then
  // kick of a late composite.
  TimeStamp lastVsync = mCompositorScheduler->GetLastVsyncTime();
  VsyncId lastVsyncId = mCompositorScheduler->GetLastVsyncId();
  if (lastVsyncId == VsyncId() || !mMostRecentComposite ||
      mMostRecentComposite >= lastVsync ||
      ((TimeStamp::Now() - lastVsync).ToMilliseconds() >
       StaticPrefs::gfx_webrender_late_scenebuild_threshold())) {
    mCompositorScheduler->ScheduleComposition(wr::RenderReasons::SCENE);
    return;
  }

  // Look through all the pipelines contained within the built scene
  // and check which vsync they initiated from.
  const auto& info = aInfo->Raw();
  for (const auto& epoch : info.epochs) {
    WebRenderBridgeParent* wrBridge = this;
    if (!(epoch.pipeline_id == PipelineId())) {
      wrBridge = mAsyncImageManager->GetWrBridge(epoch.pipeline_id);
    }

    if (wrBridge) {
      VsyncId startId = wrBridge->GetVsyncIdForEpoch(epoch.epoch);
      // If any of the pipelines started building on the current vsync (i.e
      // we did all of display list building and scene building within the
      // threshold), then don't do an early composite.
      if (startId == lastVsyncId) {
        mCompositorScheduler->ScheduleComposition(wr::RenderReasons::SCENE);
        return;
      }
    }
  }

  CompositeToTarget(mCompositorScheduler->GetLastVsyncId(),
                    wr::RenderReasons::SCENE, nullptr, nullptr);
}

static void RecordPaintPhaseTelemetry(wr::RendererStats* aStats) {
  if (!aStats || !aStats->full_paint) {
    return;
  }

  const double geckoDL = aStats->gecko_display_list_time;
  const double wrDL = aStats->wr_display_list_time;
  const double sceneBuild = aStats->scene_build_time;
  const double frameBuild = aStats->frame_build_time;
  const double totalMs = geckoDL + wrDL + sceneBuild + frameBuild;

  // If the total time was >= 16ms, then it's likely we missed a frame due to
  // painting. We bucket these metrics separately.
  const bool isLargePaint = totalMs >= 16.0;

  // Split the results based on display list build type, partial or full.
  const bool isFullDisplayList = aStats->full_display_list;

  auto AsPercentage = [&](const double aTimeMs) -> double {
    MOZ_ASSERT(aTimeMs <= totalMs);
    return (aTimeMs / totalMs) * 100.0;
  };

  auto RecordKey = [&](const nsCString& aKey, const double aTimeMs) -> void {
    const auto val = static_cast<uint32_t>(AsPercentage(aTimeMs));
    if (isFullDisplayList) {
      if (isLargePaint) {
        glean::gfx_content::large_paint_phase_weight_full.Get(aKey)
            .AccumulateSingleSample(val);
      } else {
        glean::gfx_content::small_paint_phase_weight_full.Get(aKey)
            .AccumulateSingleSample(val);
      }
    } else {
      if (isLargePaint) {
        glean::gfx_content::large_paint_phase_weight_partial.Get(aKey)
            .AccumulateSingleSample(val);
      } else {
        glean::gfx_content::small_paint_phase_weight_partial.Get(aKey)
            .AccumulateSingleSample(val);
      }
    }
  };

  RecordKey("dl"_ns, geckoDL);
  RecordKey("wrdl"_ns, wrDL);
  RecordKey("sb"_ns, sceneBuild);
  RecordKey("fb"_ns, frameBuild);
}

void WebRenderBridgeParent::FlushTransactionIdsForEpoch(
    const wr::Epoch& aEpoch, const VsyncId& aCompositeStartId,
    const TimeStamp& aCompositeStartTime, const TimeStamp& aRenderStartTime,
    const TimeStamp& aEndTime, UiCompositorControllerParent* aUiController,
    wr::RendererStats* aStats, nsTArray<FrameStats>& aOutputStats,
    nsTArray<TransactionId>& aOutputTransactions) {
  while (!mPendingTransactionIds.empty()) {
    const auto& transactionId = mPendingTransactionIds.front();

    if (aEpoch.mHandle < transactionId.mEpoch.mHandle) {
      break;
    }

    if (!IsRootWebRenderBridgeParent() && !mVsyncRate.IsZero() &&
        transactionId.mUseForTelemetry) {
      auto fullPaintTime =
          transactionId.mSceneBuiltTime
              ? transactionId.mSceneBuiltTime - transactionId.mTxnStartTime
              : TimeDuration::FromMilliseconds(0);

      int32_t contentFrameTime = RecordContentFrameTime(
          transactionId.mVsyncId, transactionId.mVsyncStartTime,
          transactionId.mTxnStartTime, aCompositeStartId, aEndTime,
          fullPaintTime, mVsyncRate, transactionId.mContainsSVGGroup, true,
          aStats);

      RecordPaintPhaseTelemetry(aStats);

      if (StaticPrefs::gfx_logging_slow_frames_enabled_AtStartup() &&
          contentFrameTime > 200) {
        aOutputStats.AppendElement(FrameStats(
            transactionId.mId, aCompositeStartTime, aRenderStartTime, aEndTime,
            contentFrameTime,
            aStats ? (double(aStats->resource_upload_time) / 1000000.0) : 0.0,
            aStats ? (double(aStats->gpu_cache_upload_time) / 1000000.0) : 0.0,
            transactionId.mTxnStartTime, transactionId.mRefreshStartTime,
            transactionId.mFwdTime, transactionId.mSceneBuiltTime,
            transactionId.mSkippedComposites, transactionId.mTxnURL));
      }
    }

#if defined(ENABLE_FRAME_LATENCY_LOG)
    if (transactionId.mRefreshStartTime) {
      int32_t latencyMs =
          lround((aEndTime - transactionId.mRefreshStartTime).ToMilliseconds());
      printf_stderr(
          "From transaction start to end of generate frame latencyMs %d this "
          "%p\n",
          latencyMs, this);
    }
    if (transactionId.mFwdTime) {
      int32_t latencyMs =
          lround((aEndTime - transactionId.mFwdTime).ToMilliseconds());
      printf_stderr(
          "From forwarding transaction to end of generate frame latencyMs %d "
          "this %p\n",
          latencyMs, this);
    }
#endif

    if (aUiController && transactionId.mIsFirstPaint) {
      aUiController->NotifyFirstPaint();
    }

    RecordCompositionPayloadsPresented(aEndTime, transactionId.mPayloads);

    aOutputTransactions.AppendElement(transactionId.mId);
    mPendingTransactionIds.pop_front();
  }
}

LayersId WebRenderBridgeParent::GetLayersId() const {
  return wr::AsLayersId(mPipelineId);
}

void WebRenderBridgeParent::ScheduleGenerateFrame(wr::RenderReasons aReasons) {
  if (mCompositorScheduler) {
    mAsyncImageManager->SetWillGenerateFrame();
    mCompositorScheduler->ScheduleComposition(aReasons);
  }
}

void WebRenderBridgeParent::FlushRendering(wr::RenderReasons aReasons,
                                           bool aBlocking) {
  if (mDestroyed) {
    return;
  }

  if (aBlocking) {
    FlushSceneBuilds();
    FlushFrameGeneration(aReasons);
    FlushFramePresentation();
  } else {
    ScheduleGenerateFrame(aReasons);
  }
}

ipc::IPCResult WebRenderBridgeParent::RecvSetDefaultClearColor(
    const uint32_t& aColor) {
  SetClearColor(gfx::DeviceColor::FromABGR(aColor));
  return IPC_OK();
}

void WebRenderBridgeParent::SetClearColor(const gfx::DeviceColor& aColor) {
  MOZ_ASSERT(IsRootWebRenderBridgeParent());

  if (!IsRootWebRenderBridgeParent() || mDestroyed) {
    return;
  }

  mApi->SetClearColor(aColor);
}

void WebRenderBridgeParent::Pause() {
  MOZ_ASSERT(IsRootWebRenderBridgeParent());
  LOG("WebRenderBridgeParent::Pause() PipelineId %" PRIx64 " Id %" PRIx64
      " root %d",
      wr::AsUint64(mPipelineId), wr::AsUint64(mApi->GetId()),
      IsRootWebRenderBridgeParent());

  if (!IsRootWebRenderBridgeParent() || mDestroyed) {
    return;
  }

  mApi->Pause();
}

bool WebRenderBridgeParent::Resume() {
  MOZ_ASSERT(IsRootWebRenderBridgeParent());
  LOG("WebRenderBridgeParent::Resume() PipelineId %" PRIx64 " Id %" PRIx64
      " root %d",
      wr::AsUint64(mPipelineId), wr::AsUint64(mApi->GetId()),
      IsRootWebRenderBridgeParent());

  if (!IsRootWebRenderBridgeParent() || mDestroyed) {
    return false;
  }

  if (!mApi->Resume()) {
    return false;
  }

  // Ensure we generate and render a frame immediately.
  ScheduleForcedGenerateFrame(wr::RenderReasons::WIDGET);
  return true;
}

void WebRenderBridgeParent::ClearResources() {
  if (!mApi) {
    return;
  }

  if (!IsRootWebRenderBridgeParent()) {
    mApi->FlushPendingWrTransactionEventsWithoutWait();
  }

  LOG("WebRenderBridgeParent::ClearResources() PipelineId %" PRIx64
      " Id %" PRIx64 " root %d",
      wr::AsUint64(mPipelineId), wr::AsUint64(mApi->GetId()),
      IsRootWebRenderBridgeParent());

  wr::Epoch wrEpoch = GetNextWrEpoch();
  mReceivedDisplayList = false;
  // Schedule generate frame to clean up Pipeline
  ScheduleGenerateFrame(wr::RenderReasons::CLEAR_RESOURCES);

  // WrFontKeys and WrImageKeys are deleted during WebRenderAPI destruction.
  for (const auto& entry : mTextureHosts) {
    WebRenderTextureHost* wrTexture = entry.second->AsWebRenderTextureHost();
    MOZ_ASSERT(wrTexture);
    if (wrTexture) {
      mAsyncImageManager->HoldExternalImage(mPipelineId, wrEpoch, wrTexture);
    }
  }
  mTextureHosts.clear();

  for (const auto& entry : mSharedSurfaceIds) {
    mAsyncImageManager->HoldExternalImage(mPipelineId, mWrEpoch, entry.second);
  }
  mSharedSurfaceIds.clear();

  mAsyncImageManager->RemovePipeline(mPipelineId, wrEpoch);

  wr::TransactionBuilder txn(mApi);
  txn.SetLowPriority(true);
  txn.ClearDisplayList(wrEpoch, mPipelineId);

  for (const auto& entry : mAsyncCompositables) {
    wr::PipelineId pipelineId = wr::AsPipelineId(entry.first);
    RefPtr<WebRenderImageHost> host = entry.second;
    host->ClearWrBridge(pipelineId, this);
    mAsyncImageManager->RemoveAsyncImagePipeline(
        pipelineId, /* aPendingOps */ nullptr, txn);
    txn.RemovePipeline(pipelineId);
  }
  mAsyncCompositables.clear();
  txn.RemovePipeline(mPipelineId);
  mApi->SendTransaction(txn);

  ClearAnimationResources();

  if (IsRootWebRenderBridgeParent()) {
    mCompositorScheduler->Destroy();
    mApi->DestroyRenderer();
  }

  mCompositorScheduler = nullptr;
  mAsyncImageManager = nullptr;
  mApi = nullptr;
  mCompositorBridge = nullptr;
}

void WebRenderBridgeParent::ClearAnimationResources() {
  if (RefPtr<OMTASampler> sampler = GetOMTASampler()) {
    sampler->ClearActiveAnimations(mActiveAnimations);
  }
  mActiveAnimations.clear();
  std::queue<CompositorAnimationIdsForEpoch>().swap(
      mCompositorAnimationsToDelete);  // clear queue
}

void WebRenderBridgeParent::SendAsyncMessage(
    const nsTArray<AsyncParentMessageData>& aMessage) {
  MOZ_ASSERT_UNREACHABLE("unexpected to be called");
}

void WebRenderBridgeParent::SendPendingAsyncMessages() {
  MOZ_ASSERT(mCompositorBridge);
  mCompositorBridge->SendPendingAsyncMessages();
}

void WebRenderBridgeParent::SetAboutToSendAsyncMessages() {
  MOZ_ASSERT(mCompositorBridge);
  mCompositorBridge->SetAboutToSendAsyncMessages();
}

void WebRenderBridgeParent::NotifyNotUsed(PTextureParent* aTexture,
                                          uint64_t aTransactionId) {
  MOZ_ASSERT_UNREACHABLE("unexpected to be called");
}

base::ProcessId WebRenderBridgeParent::GetChildProcessId() {
  return OtherPid();
}

dom::ContentParentId WebRenderBridgeParent::GetContentId() {
  MOZ_ASSERT(mCompositorBridge);
  return mCompositorBridge->GetContentId();
}

bool WebRenderBridgeParent::IsSameProcess() const {
  return OtherPid() == base::GetCurrentProcId();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvNewCompositable(
    const CompositableHandle& aHandle, const TextureInfo& aInfo) {
  if (mDestroyed) {
    return IPC_OK();
  }
  if (!AddCompositable(aHandle, aInfo)) {
    return IPC_FAIL_NO_REASON(this);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult WebRenderBridgeParent::RecvReleaseCompositable(
    const CompositableHandle& aHandle) {
  if (mDestroyed) {
    return IPC_OK();
  }
  ReleaseCompositable(aHandle);
  return IPC_OK();
}

TextureFactoryIdentifier WebRenderBridgeParent::GetTextureFactoryIdentifier() {
  MOZ_ASSERT(mApi);

#ifdef XP_WIN
  const bool supportsD3D11NV12 = gfx::DeviceManagerDx::Get()->CanUseNV12();
#else
  const bool supportsD3D11NV12 = false;
#endif

  TextureFactoryIdentifier ident(
      mApi->GetBackendType(), mApi->GetCompositorType(), XRE_GetProcessType(),
      mApi->GetMaxTextureSize(), mApi->GetUseANGLE(), mApi->GetUseDComp(),
      mAsyncImageManager->UseCompositorWnd(), false, false, false,
      supportsD3D11NV12, mApi->GetSyncHandle());
  return ident;
}

wr::Epoch WebRenderBridgeParent::GetNextWrEpoch() {
  MOZ_RELEASE_ASSERT(mWrEpoch.mHandle != UINT32_MAX);
  mWrEpoch.mHandle++;
  return mWrEpoch;
}

void WebRenderBridgeParent::RollbackWrEpoch() {
  MOZ_RELEASE_ASSERT(mWrEpoch.mHandle != 0);
  mWrEpoch.mHandle--;
}

void WebRenderBridgeParent::ExtractImageCompositeNotifications(
    nsTArray<ImageCompositeNotificationInfo>* aNotifications) {
  MOZ_ASSERT(IsRootWebRenderBridgeParent());
  if (mDestroyed) {
    return;
  }
  mAsyncImageManager->FlushImageNotifications(aNotifications);
}

void WebRenderBridgeParent::FlushPendingWrTransactionEventsWithWait() {
  if (mDestroyed || IsRootWebRenderBridgeParent()) {
    return;
  }
  mApi->FlushPendingWrTransactionEventsWithWait();
}

RefPtr<WebRenderBridgeParentRef>
WebRenderBridgeParent::GetWebRenderBridgeParentRef() {
  if (mDestroyed) {
    return nullptr;
  }

  if (!mWebRenderBridgeRef) {
    mWebRenderBridgeRef = new WebRenderBridgeParentRef(this);
  }
  return mWebRenderBridgeRef;
}

WebRenderBridgeParentRef::WebRenderBridgeParentRef(
    WebRenderBridgeParent* aWebRenderBridge)
    : mWebRenderBridge(aWebRenderBridge) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MOZ_ASSERT(mWebRenderBridge);
}

RefPtr<WebRenderBridgeParent> WebRenderBridgeParentRef::WrBridge() {
  return mWebRenderBridge;
}

void WebRenderBridgeParentRef::Clear() {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  mWebRenderBridge = nullptr;
}

WebRenderBridgeParentRef::~WebRenderBridgeParentRef() {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MOZ_ASSERT(!mWebRenderBridge);
}

}  // namespace mozilla::layers
