/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/CompositorBridgeParent.h"

#include <stdint.h>  // for uint64_t
#include <stdio.h>   // for fprintf, stdout

#include <utility>  // for pair

#include "TreeTraversal.h"            // for ForEachNode
#include "apz/src/APZCTreeManager.h"  // for APZCTreeManager
#include "base/process.h"             // for ProcessId
#include "gfxContext.h"               // for gfxContext
#include "gfxPlatform.h"              // for gfxPlatform
#ifdef MOZ_WIDGET_GTK
#  include "gfxPlatformGtk.h"  // for gfxPlatform
#endif
#include "mozilla/AutoRestore.h"      // for AutoRestore
#include "mozilla/ClearOnShutdown.h"  // for ClearOnShutdown
#include "mozilla/DebugOnly.h"        // for DebugOnly
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_layers.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/gfx/2D.h"  // for DrawTarget
#include "mozilla/gfx/GPUParent.h"
#include "mozilla/gfx/GPUProcessManager.h"
#include "mozilla/gfx/Point.h"    // for IntSize
#include "mozilla/gfx/Rect.h"     // for IntSize
#include "mozilla/gfx/gfxVars.h"  // for gfxVars
#include "mozilla/glean/GfxMetrics.h"
#include "mozilla/layers/APZCTreeManagerParent.h"  // for APZCTreeManagerParent
#include "mozilla/layers/APZSampler.h"             // for APZSampler
#include "mozilla/layers/APZThreadUtils.h"         // for APZThreadUtils
#include "mozilla/layers/APZUpdater.h"             // for APZUpdater
#include "mozilla/layers/AsyncImagePipelineManager.h"
#include "mozilla/layers/CompositionRecorder.h"  // for CompositionRecorder
#include "mozilla/layers/Compositor.h"           // for Compositor
#include "mozilla/layers/CompositorAnimationStorage.h"  // for CompositorAnimationStorage
#include "mozilla/layers/CompositorManagerParent.h"  // for CompositorManagerParent
#include "mozilla/layers/CompositorOGL.h"            // for CompositorOGL
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/CompositorTypes.h"
#include "mozilla/layers/CompositorVsyncScheduler.h"
#include "mozilla/layers/ContentCompositorBridgeParent.h"
#include "mozilla/layers/FrameUniformityData.h"
#include "mozilla/layers/GeckoContentController.h"
#include "mozilla/layers/ImageBridgeParent.h"
#include "mozilla/layers/LayerTreeLog.h"
#include "mozilla/layers/LayerTreeOwnerTracker.h"
#include "mozilla/layers/LayersTypes.h"
#include "mozilla/layers/OMTASampler.h"
#include "mozilla/layers/RemoteContentController.h"
#include "mozilla/layers/UiCompositorControllerParent.h"
#include "mozilla/layers/WebRenderBridgeParent.h"
#include "mozilla/media/MediaSystemResourceService.h"  // for MediaSystemResourceService
#include "mozilla/mozalloc.h"                          // for operator new, etc
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/webrender/WebRenderAPI.h"
#ifndef MOZ_WIDGET_UIKIT
#  include "mozilla/widget/HeadlessCompositorWidgetParent.h"
#endif
#include "nsCOMPtr.h"         // for already_AddRefed
#include "nsDebug.h"          // for NS_ASSERTION, etc
#include "nsISupportsImpl.h"  // for MOZ_COUNT_CTOR, etc
#include "nsIWidget.h"        // for nsIWidget
#include "nsTArray.h"         // for nsTArray
#include "nsThreadUtils.h"    // for NS_IsMainThread
#ifdef XP_WIN
#  include "mozilla/layers/CompositorD3D11.h"
#  include "mozilla/widget/WinCompositorWidget.h"
#endif
#include "mozilla/Hal.h"
#include "mozilla/HalTypes.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/VsyncDispatcher.h"
#include "mozilla/ipc/ProtocolTypes.h"
#if defined(XP_WIN) || defined(MOZ_WIDGET_GTK)
#  include "VsyncSource.h"
#endif
#include "mozilla/widget/CompositorWidget.h"
#ifdef MOZ_WIDGET_SUPPORTS_OOP_COMPOSITING
#  include "mozilla/widget/CompositorWidgetParent.h"
#endif
#ifdef XP_WIN
#  include "mozilla/gfx/DeviceManagerDx.h"
#endif

namespace mozilla {

namespace layers {

using namespace mozilla::ipc;
using namespace mozilla::gfx;

using base::ProcessId;

/* static*/
StaticMonitor CompositorBridgeParent::sIndirectLayerTreesLock;

/* static */
MOZ_RUNINIT CompositorBridgeParent::LayerTreeMap
    CompositorBridgeParent::sIndirectLayerTrees MOZ_GUARDED_BY(
        CompositorBridgeParent::sIndirectLayerTreesLock);

void EraseLayerState(LayersId aId);

CompositorBridgeParentBase::CompositorBridgeParentBase(
    CompositorManagerParent* aManager, uint32_t aNamespace)
    : mCanSend(true), mCompositorManager(aManager), mNamespace(aNamespace) {}

CompositorBridgeParentBase::~CompositorBridgeParentBase() = default;

ProcessId CompositorBridgeParentBase::GetChildProcessId() { return OtherPid(); }

dom::ContentParentId CompositorBridgeParentBase::GetContentId() {
  return mCompositorManager->GetContentId();
}

void CompositorBridgeParentBase::NotifyNotUsed(PTextureParent* aTexture,
                                               uint64_t aTransactionId) {
  RefPtr<TextureHost> texture = TextureHost::AsTextureHost(aTexture);
  if (!texture) {
    return;
  }

  if (!(texture->GetFlags() & TextureFlags::RECYCLE) &&
      !(texture->GetFlags() & TextureFlags::WAIT_HOST_USAGE_END)) {
    return;
  }

  uint64_t textureId = TextureHost::GetTextureSerial(aTexture);
  mPendingAsyncMessage.AppendElement(
      OpNotifyNotUsed(textureId, aTransactionId));
}

void CompositorBridgeParentBase::SendAsyncMessage(
    Span<const AsyncParentMessageData> aMessage) {
  (void)SendParentAsyncMessages(aMessage);
}

bool CompositorBridgeParentBase::AllocShmem(size_t aSize, ipc::Shmem* aShmem) {
  return PCompositorBridgeParent::AllocShmem(aSize, aShmem);
}

bool CompositorBridgeParentBase::AllocUnsafeShmem(size_t aSize,
                                                  ipc::Shmem* aShmem) {
  return PCompositorBridgeParent::AllocUnsafeShmem(aSize, aShmem);
}

bool CompositorBridgeParentBase::DeallocShmem(ipc::Shmem& aShmem) {
  return PCompositorBridgeParent::DeallocShmem(aShmem);
}

bool CompositorBridgeParentBase::OwnsExternalImageId(
    const wr::ExternalImageId& aId) const {
  return mNamespace == static_cast<uint32_t>(wr::AsUint64(aId) >> 32);
}

bool CompositorBridgeParentBase::OwnsPipelineId(
    const wr::PipelineId& aPipelineId) const {
  return mNamespace == aPipelineId.mNamespace;
}

CompositorBridgeParent::LayerTreeState::LayerTreeState()
    : mApzcTreeManagerParent(nullptr),
      mApzBridgeParent(nullptr),
      mParent(nullptr),
      mContentCompositorBridgeParent(nullptr) {}

CompositorBridgeParent::LayerTreeState::~LayerTreeState() {
  if (mController) {
    mController->Destroy();
  }
}

template <typename Lambda>
inline void CompositorBridgeParent::ForEachIndirectLayerTree(
    const Lambda& aCallback) {
  sIndirectLayerTreesLock.AssertCurrentThreadOwns();
  for (auto it = sIndirectLayerTrees.begin(); it != sIndirectLayerTrees.end();
       it++) {
    LayerTreeState* state = &it->second;
    if (state->mParent == this) {
      aCallback(state, it->first);
    }
  }
}

/*static*/ template <typename Lambda>
inline void CompositorBridgeParent::ForEachWebRenderBridgeParent(
    const Lambda& aCallback) {
  sIndirectLayerTreesLock.AssertCurrentThreadOwns();
  for (auto& it : sIndirectLayerTrees) {
    LayerTreeState* state = &it.second;
    if (state->mWrBridge) {
      aCallback(state->mWrBridge);
    }
  }
}

void CompositorBridgeParent::FinishShutdown() {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  // TODO: this should be empty by now...
  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  sIndirectLayerTrees.clear();
}

CompositorBridgeParent::CompositorBridgeParent(
    CompositorManagerParent* aManager, uint32_t aNamespace,
    CSSToLayoutDeviceScale aScale, const TimeDuration& aVsyncRate,
    const CompositorOptions& aOptions, bool aUseExternalSurfaceSize,
    const gfx::IntSize& aSurfaceSize, uint64_t aInnerWindowId)
    : CompositorBridgeParentBase(aManager, aNamespace),
      mWidget(nullptr),
      mScale(aScale),
      mVsyncRate(aVsyncRate),
      mPaused(false),
      mHaveCompositionRecorder(false),
      mIsForcedFirstPaint(false),
      mUseExternalSurfaceSize(aUseExternalSurfaceSize),
      mEGLSurfaceSize(aSurfaceSize),
      mOptions(aOptions),
      mRootLayerTreeID{0},
      mInnerWindowId(aInnerWindowId),
      mCompositorScheduler(nullptr),
      mAnimationStorage(nullptr) {}

void CompositorBridgeParent::InitSameProcess(widget::CompositorWidget* aWidget,
                                             const LayersId& aLayerTreeId) {
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  mWidget = aWidget;
  mRootLayerTreeID = aLayerTreeId;
#if defined(XP_WIN)
  // when run in headless mode, no WinCompositorWidget is created
  if (widget::WinCompositorWidget* windows = mWidget->AsWindows()) {
    windows->SetRootLayerTreeID(mRootLayerTreeID);
  }
#endif

  Initialize();
}

bool CompositorBridgeParent::IsPaused() {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  return mPaused;
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvInitialize(
    const LayersId& aRootLayerTreeId) {
  MOZ_RELEASE_ASSERT(XRE_IsGPUProcess());

  mRootLayerTreeID = aRootLayerTreeId;
  LAYERTREE_LOG("Widget compositor for root layer tree {:x} initialized",
                mRootLayerTreeID);
#ifdef XP_WIN
  // headless mode is probably always same-process; but just in case...
  if (widget::WinCompositorWidget* windows = mWidget->AsWindows()) {
    windows->SetRootLayerTreeID(mRootLayerTreeID);
  }
#endif

  Initialize();
  return IPC_OK();
}

void CompositorBridgeParent::Initialize() {
  MOZ_RELEASE_ASSERT(
      CompositorThread(),
      "The compositor thread must be Initialized before instanciating a "
      "CompositorBridgeParent.");

  if (mOptions.UseAPZ()) {
    MOZ_RELEASE_ASSERT(!mApzcTreeManager);
    MOZ_RELEASE_ASSERT(!mApzSampler);
    MOZ_RELEASE_ASSERT(!mApzUpdater);
    mApzcTreeManager = APZCTreeManager::Create(mRootLayerTreeID, mScale);
    mApzSampler = new APZSampler(mApzcTreeManager, true);
    mApzUpdater = new APZUpdater(mApzcTreeManager, true);
  }

  CompositorAnimationStorage* animationStorage = GetAnimationStorage();
  mOMTASampler = new OMTASampler(animationStorage, mRootLayerTreeID);

  mPaused = mOptions.InitiallyPaused();

  {  // scope lock
    StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
    EnsureLayerTreeStateUnderLock(mRootLayerTreeID, lock).mParent = this;
  }
}

LayersId CompositorBridgeParent::RootLayerTreeId() {
  MOZ_RELEASE_ASSERT(mRootLayerTreeID.IsValid());
  return mRootLayerTreeID;
}

CompositorBridgeParent::~CompositorBridgeParent() {
  MOZ_DIAGNOSTIC_ASSERT(
      !mCanSend,
      "ActorDestroy or RecvWillClose should have been called first.");
  MOZ_DIAGNOSTIC_ASSERT(mRefCnt == 0,
                        "ActorDealloc should have been called first.");
  nsTArray<PTextureParent*> textures;
  ManagedPTextureParent(textures);
  // We expect all textures to be destroyed by now.
  MOZ_DIAGNOSTIC_ASSERT(textures.Length() == 0);
  for (unsigned int i = 0; i < textures.Length(); ++i) {
    RefPtr<TextureHost> tex = TextureHost::AsTextureHost(textures[i]);
    tex->DeallocateDeviceData();
  }
  // Check if WebRender/Compositor was shutdown.
  if (mWrBridge) {
    gfxCriticalNote << "CompositorBridgeParent destroyed without shutdown";
  }
}

void CompositorBridgeParent::ForceIsFirstPaint() {
  if (mWrBridge) {
    mIsForcedFirstPaint = true;
  }
}

void CompositorBridgeParent::StopAndClearResources() {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  mPaused = true;

  // We need to clear the APZ tree before we destroy the WebRender API below,
  // because in the case of async scene building that will shut down the updater
  // thread and we need to run the task before that happens.
  MOZ_RELEASE_ASSERT((mApzSampler != nullptr) == (mApzcTreeManager != nullptr));
  MOZ_RELEASE_ASSERT((mApzUpdater != nullptr) == (mApzcTreeManager != nullptr));
  if (mApzUpdater) {
    mApzSampler->Destroy();
    mApzSampler = nullptr;
    mApzUpdater->ClearTree(mRootLayerTreeID);
    mApzUpdater = nullptr;
    mApzcTreeManager = nullptr;
  }

  if (mWrBridge) {
    // Ensure we are not holding the sIndirectLayerTreesLock when destroying
    // the WebRenderBridgeParent instances because it may block on WR.
    std::vector<RefPtr<WebRenderBridgeParent>> indirectBridgeParents;
    {  // scope lock
      StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
      ForEachIndirectLayerTree([&](LayerTreeState* lts, LayersId) -> void {
        if (lts->mWrBridge) {
          indirectBridgeParents.emplace_back(lts->mWrBridge.forget());
        }
        lts->mParent = nullptr;
      });
    }
    for (const RefPtr<WebRenderBridgeParent>& bridge : indirectBridgeParents) {
      bridge->Destroy();
    }
    indirectBridgeParents.clear();

    RefPtr<wr::WebRenderAPI> api = mWrBridge->GetWebRenderAPI();
    // Only clear it if the entry still exists. By shutdown the entry for
    // mRootLayerTreeID may already have been erased, and inserting one here
    // just to null this field would wrongly resurrect it.
    CallWithLayerTreeState(mRootLayerTreeID, [](LayerTreeState& aState) {
      aState.mWebRenderAPI = nullptr;
    });
    // Ensure we are not holding the sIndirectLayerTreesLock here because we
    // are going to block on WR threads in order to shut it down properly.
    mWrBridge->Destroy();
    mWrBridge = nullptr;

    if (api) {
      // Make extra sure we are done cleaning WebRender up before continuing.
      // After that we wont have a way to talk to a lot of the webrender parts.
      api->FlushSceneBuilder();
      api = nullptr;
    }

    if (mAsyncImageManager) {
      mAsyncImageManager->Destroy();
      // WebRenderAPI should be already destructed
      mAsyncImageManager = nullptr;
    }
  }

  // This must be destroyed now since it accesses the widget.
  if (mCompositorScheduler) {
    mCompositorScheduler->Destroy();
    mCompositorScheduler = nullptr;
  }

  if (mOMTASampler) {
    mOMTASampler->Destroy();
    mOMTASampler = nullptr;
  }

  // After this point, it is no longer legal to access the widget.
  mWidget = nullptr;

  // Clear mAnimationStorage here to ensure that the compositor thread
  // still exists when we destroy it.
  mAnimationStorage = nullptr;
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvWillClose() {
  LAYERTREE_LOG("Closing widget compositor for root layer tree {:x}",
                mRootLayerTreeID);
  StopAndClearResources();
  // Once we get the WillClose message, the client side is going to go away
  // soon and we can't be guaranteed that sending messages will work.
  mCanSend = false;
  return IPC_OK();
}

void CompositorBridgeParent::DeferredDestroy() {
  MOZ_RELEASE_ASSERT(!NS_IsMainThread());
  mSelfRef = nullptr;
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvPause() {
  LAYERTREE_LOG("Widget paused compositor for root layer tree {:x}",
                mRootLayerTreeID);
  PauseComposition();
  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvResume() {
  bool resumed = ResumeComposition();
  LAYERTREE_LOG("Widget resumed compositor for root layer tree {:x}: {}",
                mRootLayerTreeID, resumed);
  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvResumeAsync() {
  bool resumed = ResumeComposition();
  LAYERTREE_LOG("Widget resumed compositor for root layer tree {:x}: {}",
                mRootLayerTreeID, resumed);
  return IPC_OK();
}

mozilla::ipc::IPCResult
CompositorBridgeParent::RecvWaitOnTransactionProcessed() {
  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvFlushRendering(
    const wr::RenderReasons& aReasons) {
  if (mWrBridge) {
    mWrBridge->FlushRendering(aReasons, /* aBlocking */ true);
    return IPC_OK();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvNotifyMemoryPressure() {
  NotifyMemoryPressure();
  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvFlushRenderingAsync(
    const wr::RenderReasons& aReasons) {
  if (mWrBridge) {
    mWrBridge->FlushRendering(aReasons, /* aBlocking */ false);
    return IPC_OK();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvForcePresent(
    const wr::RenderReasons& aReasons) {
  if (mWrBridge) {
    mWrBridge->ScheduleForcedGenerateFrame(aReasons);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvStartFrameTimeRecording(
    const uint32_t& aBufferSize, uint32_t* aOutStartIndex) {
  if (mWrBridge) {
    *aOutStartIndex = mWrBridge->StartFrameTimeRecording(aBufferSize);
  } else {
    *aOutStartIndex = 0;
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvStopFrameTimeRecording(
    const uint32_t& aStartIndex, nsTArray<float>* intervals) {
  if (mWrBridge) {
    mWrBridge->StopFrameTimeRecording(aStartIndex, *intervals);
  }
  return IPC_OK();
}

void CompositorBridgeParent::ActorDestroy(ActorDestroyReason why) {
  LAYERTREE_LOG(
      "Widget compositor for root layer tree {:x} destroyed, reason {}",
      mRootLayerTreeID, fmt::enums::format_as(why));
  mCanSend = false;

  StopAndClearResources();

  {  // scope lock
    StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
    sIndirectLayerTrees.erase(mRootLayerTreeID);
  }

  // There are chances that the ref count reaches zero on the main thread
  // shortly after this function returns while some ipdl code still needs to run
  // on this thread. We must keep the compositor parent alive untill the code
  // handling message reception is finished on this thread.
  mSelfRef = this;
  NS_GetCurrentThread()->Dispatch(
      NewRunnableMethod("layers::CompositorBridgeParent::DeferredDestroy", this,
                        &CompositorBridgeParent::DeferredDestroy));
}

void CompositorBridgeParent::ScheduleRenderOnCompositorThread(
    wr::RenderReasons aReasons) {
  MOZ_RELEASE_ASSERT(CompositorThread());
  CompositorThread()->Dispatch(NewRunnableMethod<wr::RenderReasons>(
      "layers::CompositorBridgeParent::ScheduleComposition", this,
      &CompositorBridgeParent::ScheduleComposition, aReasons));
}

void CompositorBridgeParent::PauseComposition() {
  MOZ_RELEASE_ASSERT(
      CompositorThreadHolder::IsInCompositorThread(),
      "PauseComposition() can only be called on the compositor thread");

  if (!mPaused) {
    mPaused = true;

    TimeStamp now = TimeStamp::Now();
    if (mWrBridge) {
      mWrBridge->Pause();
      NotifyPipelineRendered(mWrBridge->PipelineId(),
                             mWrBridge->GetCurrentEpoch(), VsyncId(), now, now,
                             now);
    }
  }
}

bool CompositorBridgeParent::ResumeComposition() {
  MOZ_RELEASE_ASSERT(
      CompositorThreadHolder::IsInCompositorThread(),
      "ResumeComposition() can only be called on the compositor thread");

  bool resumed = mWidget->OnResumeComposition();
  resumed = resumed && mWrBridge->Resume();

  if (!resumed) {
#ifdef MOZ_WIDGET_ANDROID
    // We can't get a surface. This could be because the activity changed
    // between the time resume was scheduled and now.
    __android_log_print(
        ANDROID_LOG_INFO, "CompositorBridgeParent",
        "Unable to renew compositor surface; remaining in paused state");
#endif
    return false;
  }

  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  mPaused = false;

  mCompositorScheduler->ForceComposeToTarget(wr::RenderReasons::WIDGET, nullptr,
                                             nullptr);
  return true;
}

void CompositorBridgeParent::SetEGLSurfaceRect(int x, int y, int width,
                                               int height) {
  NS_ASSERTION(mUseExternalSurfaceSize,
               "Compositor created without UseExternalSurfaceSize provided");
  mEGLSurfaceSize.SizeTo(width, height);
}

bool CompositorBridgeParent::ResumeCompositionAndResize(int x, int y, int width,
                                                        int height) {
  SetEGLSurfaceRect(x, y, width, height);
  return ResumeComposition();
}

void CompositorBridgeParent::ScheduleComposition(wr::RenderReasons aReasons) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  if (mPaused) {
    return;
  }

  if (mWrBridge) {
    mWrBridge->ScheduleGenerateFrame(aReasons);
  }
}

already_AddRefed<PAPZCTreeManagerParent>
CompositorBridgeParent::AllocPAPZCTreeManagerParent(const LayersId& aLayersId) {
  // This should only ever get called in the GPU process.
  MOZ_RELEASE_ASSERT(XRE_IsGPUProcess());
  // We should only ever get this if APZ is enabled in this compositor.
  MOZ_RELEASE_ASSERT(mOptions.UseAPZ());
  // The mApzcTreeManager and mApzUpdater should have been created via
  // RecvInitialize()
  MOZ_RELEASE_ASSERT(mApzcTreeManager);
  MOZ_RELEASE_ASSERT(mApzUpdater);
  // The main process should pass in 0 because we assume mRootLayerTreeID
  MOZ_RELEASE_ASSERT(!aLayersId.IsValid());

  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  CompositorBridgeParent::LayerTreeState& state =
      EnsureLayerTreeStateUnderLock(mRootLayerTreeID, lock);
  MOZ_RELEASE_ASSERT(state.mParent.get() == this);
  MOZ_RELEASE_ASSERT(!state.mApzcTreeManagerParent);

  auto treeManager = MakeRefPtr<APZCTreeManagerParent>(
      mRootLayerTreeID, mApzcTreeManager, mApzUpdater);
  state.mApzcTreeManagerParent = treeManager;

  return treeManager.forget();
}

void CompositorBridgeParent::SetAPZBridgeParent(
    const LayersId& aLayersId, RefPtr<APZBridgeParent>&& aApzBridgeParent) {
  MOZ_RELEASE_ASSERT(XRE_IsGPUProcess());
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  StaticMonitorAutoLock lock(CompositorBridgeParent::sIndirectLayerTreesLock);
  CompositorBridgeParent::LayerTreeState& state =
      EnsureLayerTreeStateUnderLock(aLayersId, lock);
  MOZ_RELEASE_ASSERT(!state.mApzBridgeParent);
  state.mApzBridgeParent = std::move(aApzBridgeParent);
}

already_AddRefed<APZCTreeManagerParent>
CompositorBridgeParent::AllocateAPZCTreeManagerParent(
    const StaticMonitorAutoLock& aProofOfLayerTreeStateLock,
    const LayersId& aLayersId, LayerTreeState& aState) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MOZ_RELEASE_ASSERT(aState.mParent == this);
  MOZ_RELEASE_ASSERT(mApzcTreeManager);
  MOZ_RELEASE_ASSERT(mApzUpdater);

  // BrowserChild::ReinitRendering can create a new PAPZCTreeManager for a
  // layer tree whose previous actor is still alive. The newest one wins.
  auto treeManager = MakeRefPtr<APZCTreeManagerParent>(
      aLayersId, mApzcTreeManager, mApzUpdater);
  aState.mApzcTreeManagerParent = treeManager;
  return treeManager.forget();
}

already_AddRefed<PAPZParent> CompositorBridgeParent::AllocPAPZParent(
    const LayersId& aLayersId) {
  // This is the CompositorBridgeParent for a window, and so should only be
  // creating a PAPZ instance if it lives in the GPU process. Instances that
  // live in the UI process should going through SetControllerForLayerTree.
  MOZ_RELEASE_ASSERT(XRE_IsGPUProcess());

  // We should only ever get this if APZ is enabled on this compositor.
  MOZ_RELEASE_ASSERT(mOptions.UseAPZ());

  // The main process should pass in 0 because we assume mRootLayerTreeID
  MOZ_RELEASE_ASSERT(!aLayersId.IsValid());

  auto controller = MakeRefPtr<RemoteContentController>(mRootLayerTreeID);

  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  CompositorBridgeParent::LayerTreeState& state =
      EnsureLayerTreeStateUnderLock(mRootLayerTreeID, lock);
  MOZ_RELEASE_ASSERT(!state.mController);
  state.mController = controller;

  return controller.forget();
}

RefPtr<APZSampler> CompositorBridgeParent::GetAPZSampler() const {
  return mApzSampler;
}

RefPtr<APZUpdater> CompositorBridgeParent::GetAPZUpdater() const {
  return mApzUpdater;
}

RefPtr<OMTASampler> CompositorBridgeParent::GetOMTASampler() const {
  return mOMTASampler;
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvDynamicToolbarOffsetChanged(
    const int32_t& aOffset) {
  SetFixedLayerMargins(0, aOffset);
  return IPC_OK();
}

CompositorBridgeParent*
CompositorBridgeParent::GetCompositorBridgeParentFromLayersId(
    const LayersId& aLayersId) {
  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  return EnsureLayerTreeStateUnderLock(aLayersId, lock).mParent;
}

/*static*/
RefPtr<CompositorBridgeParent>
CompositorBridgeParent::GetCompositorBridgeParentFromWindowId(
    const wr::WindowId& aWindowId) {
  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  for (auto it = sIndirectLayerTrees.begin(); it != sIndirectLayerTrees.end();
       it++) {
    LayerTreeState* state = &it->second;
    if (!state->mWrBridge) {
      continue;
    }
    // state->mWrBridge might be a root WebRenderBridgeParent or one of a
    // content process, but in either case the state->mParent will be the same.
    // So we don't need to distinguish between the two.
    if (RefPtr<wr::WebRenderAPI> api = state->mWrBridge->GetWebRenderAPI()) {
      if (api->GetId() == aWindowId) {
        return state->mParent;
      }
    }
  }
  return nullptr;
}

bool CompositorBridgeParent::SetTestSampleTime(const LayersId& aId,
                                               const TimeStamp& aTime) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  if (aTime.IsNull()) {
    return false;
  }

  mTestTime = Some(aTime);
  if (mApzcTreeManager) {
    mApzcTreeManager->SetTestSampleTime(mTestTime);
  }

  if (mWrBridge) {
    mWrBridge->FlushRendering(wr::RenderReasons::TESTING, /* aBlocking */ true);
    return true;
  }

  return true;
}

void CompositorBridgeParent::LeaveTestMode(const LayersId& aId) {
  mTestTime = Nothing();
  if (mApzcTreeManager) {
    mApzcTreeManager->SetTestSampleTime(mTestTime);
  }
}

CompositorAnimationStorage* CompositorBridgeParent::GetAnimationStorage() {
  if (!mAnimationStorage) {
    mAnimationStorage = new CompositorAnimationStorage(this);
  }
  return mAnimationStorage;
}

void CompositorBridgeParent::NotifyJankedAnimations(
    const JankedAnimations& aJankedAnimations) {
  MOZ_RELEASE_ASSERT(!aJankedAnimations.empty());

  if (StaticPrefs::layout_animation_prerender_partial_jank()) {
    return;
  }

  for (const auto& entry : aJankedAnimations) {
    const LayersId& layersId = entry.first;
    const nsTArray<uint64_t>& animations = entry.second;
    if (layersId == mRootLayerTreeID) {
      if (mWrBridge) {
        (void)SendNotifyJankedAnimations(LayersId{0}, animations);
      }
      // It unlikely happens multiple processes have janked animations at same
      // time, so it should be fine with enumerating sIndirectLayerTrees every
      // time.
    } else if (const LayerTreeState* state = GetLayerTreeState(layersId)) {
      if (ContentCompositorBridgeParent* cpcp =
              state->mContentCompositorBridgeParent) {
        (void)cpcp->SendNotifyJankedAnimations(layersId, animations);
      }
    }
  }
}

void CompositorBridgeParent::SetTestAsyncScrollOffset(
    const LayersId& aLayersId, const ScrollableLayerGuid::ViewID& aScrollId,
    const CSSPoint& aPoint) {
  if (mApzUpdater) {
    MOZ_RELEASE_ASSERT(aLayersId.IsValid());
    mApzUpdater->SetTestAsyncScrollOffset(aLayersId, aScrollId, aPoint);
  }
}

void CompositorBridgeParent::SetTestAsyncZoom(
    const LayersId& aLayersId, const ScrollableLayerGuid::ViewID& aScrollId,
    const LayerToParentLayerScale& aZoom) {
  if (mApzUpdater) {
    MOZ_RELEASE_ASSERT(aLayersId.IsValid());
    mApzUpdater->SetTestAsyncZoom(aLayersId, aScrollId, aZoom);
  }
}

void CompositorBridgeParent::FlushApzRepaints(const LayersId& aLayersId) {
  MOZ_RELEASE_ASSERT(mApzUpdater);
  MOZ_RELEASE_ASSERT(aLayersId.IsValid());
  mApzUpdater->RunOnControllerThread(
      aLayersId, NS_NewRunnableFunction(
                     "layers::CompositorBridgeParent::FlushApzRepaints",
                     [=]() { APZCTreeManager::FlushApzRepaints(aLayersId); }));
}

void CompositorBridgeParent::GetAPZTestData(const LayersId& aLayersId,
                                            APZTestData* aOutData) {
  if (mApzUpdater) {
    MOZ_RELEASE_ASSERT(aLayersId.IsValid());
    mApzUpdater->GetAPZTestData(aLayersId, aOutData);
  }
}

void CompositorBridgeParent::GetFrameUniformity(const LayersId& aLayersId,
                                                FrameUniformityData* aOutData) {
}

void CompositorBridgeParent::SetConfirmedTargetAPZC(
    const LayersId& aLayersId, const uint64_t& aInputBlockId,
    nsTArray<ScrollableLayerGuid>&& aTargets) {
  if (!mApzcTreeManager || !mApzUpdater) {
    return;
  }
  // Need to specifically bind this since it's overloaded.
  void (APZCTreeManager::*setTargetApzcFunc)(
      uint64_t, const nsTArray<ScrollableLayerGuid>&) =
      &APZCTreeManager::SetTargetAPZC;
  RefPtr<Runnable> task =
      NewRunnableMethod<uint64_t,
                        StoreCopyPassByRRef<nsTArray<ScrollableLayerGuid>>>(
          "layers::CompositorBridgeParent::SetConfirmedTargetAPZC",
          mApzcTreeManager.get(), setTargetApzcFunc, aInputBlockId,
          std::move(aTargets));
  mApzUpdater->RunOnUpdaterThread(aLayersId, task.forget());
}

void CompositorBridgeParent::SetFixedLayerMargins(ScreenIntCoord aTop,
                                                  ScreenIntCoord aBottom) {
  if (mApzcTreeManager) {
    mApzcTreeManager->SetFixedLayerMargins(aTop, aBottom);
  }

  ScheduleComposition(wr::RenderReasons::RESIZE);
}

void CompositorBridgeParent::EndWheelTransaction(
    const LayersId& aLayersId,
    PWebRenderBridgeParent::EndWheelTransactionResolver&& aResolve) {
  if (mApzcTreeManager) {
    mApzcTreeManager->EndWheelTransaction(std::move(aResolve));
  } else {
    // The boolean value will never used so it doesn't matter whether it's true
    // or false.
    aResolve(true);
  }
}

void CompositorBridgeParent::NotifyVsync(const VsyncEvent& aVsync,
                                         const LayersId& aLayersId) {
  MOZ_RELEASE_ASSERT(XRE_GetProcessType() == GeckoProcessType_GPU);
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  auto it = sIndirectLayerTrees.find(aLayersId);
  if (it == sIndirectLayerTrees.end()) return;

  CompositorBridgeParent* cbp = it->second.mParent;
  if (!cbp || !cbp->mWidget) return;

  RefPtr<VsyncObserver> obs = cbp->mWidget->GetVsyncObserver();
  if (!obs) return;

  obs->NotifyVsync(aVsync);
}

/* static */
void CompositorBridgeParent::ScheduleForcedComposition(
    const LayersId& aLayersId, wr::RenderReasons aReasons) {
  MOZ_RELEASE_ASSERT(XRE_GetProcessType() == GeckoProcessType_GPU);
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  auto it = sIndirectLayerTrees.find(aLayersId);
  if (it == sIndirectLayerTrees.end()) {
    return;
  }

  CompositorBridgeParent* cbp = it->second.mParent;
  if (!cbp || !cbp->mWidget) {
    return;
  }

  if (cbp->mWrBridge) {
    cbp->mWrBridge->ScheduleForcedGenerateFrame(aReasons);
  }
}

/* static */ void CompositorBridgeParent::DisconnectWrBridge(
    WebRenderBridgeParent* aWrBridge) {
  auto layersId = wr::AsLayersId(aWrBridge->PipelineId());

  if (!aWrBridge->IsRootWebRenderBridgeParent()) {
    bool ownedByOtherBridge = WithIndirectLayerTreesLock(
        [&](const StaticMonitorAutoLock& aProofOfLock) {
          LayerTreeState* state =
              GetLayerTreeStateUnderLock(layersId, aProofOfLock);
          return state && state->mWrBridge && state->mWrBridge != aWrBridge;
        });
    if (!ownedByOtherBridge) {
      EraseLayerState(layersId);
    }
    return;
  }

  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  auto it = sIndirectLayerTrees.find(layersId);
  if (it != sIndirectLayerTrees.end()) {
    MOZ_ASSERT_IF(it->second.mWrBridge, it->second.mWrBridge == aWrBridge);
    it->second.mWrBridge = nullptr;
    it->second.mWebRenderAPI = nullptr;
  }
}

void CompositorBridgeParent::DisconnectApzcTreeManager(
    APZCTreeManagerParent* aTreeManager) {
  StaticMonitorAutoLock lock(CompositorBridgeParent::sIndirectLayerTreesLock);
  auto iter = CompositorBridgeParent::sIndirectLayerTrees.find(
      aTreeManager->GetLayersId());
  if (iter == CompositorBridgeParent::sIndirectLayerTrees.end()) {
    return;
  }

  // The tree manager may not be the one registered for its layers id, for
  // example the dummy one ContentCompositorBridgeParent creates when the layer
  // tree has no parent compositor.
  CompositorBridgeParent::LayerTreeState& state = iter->second;
  if (state.mApzcTreeManagerParent == aTreeManager) {
    state.mApzcTreeManagerParent = nullptr;
  }
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvNotifyChildCreated(
    const LayersId& aChild, const LayersId& aEmbedderId,
    CompositorOptions* aOptions) {
  LayersId oldEmbedder;
  {
    StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
    oldEmbedder = NotifyChildCreated(aChild, aEmbedderId, "NotifyChildCreated");
  }
  RefreshEmbedPolicies(aEmbedderId, oldEmbedder);
  *aOptions = mOptions;
  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvNotifyChildRecreated(
    const LayersId& aChild, const LayersId& aEmbedderId,
    CompositorOptions* aOptions) {
  {
    StaticMonitorAutoLock lock(sIndirectLayerTreesLock);

    if (sIndirectLayerTrees.find(aChild) != sIndirectLayerTrees.end()) {
      LAYERTREE_LOG("Layer tree {:x} cannot be recreated, it already exists",
                    aChild);
      NS_WARNING("Invalid to register the same layer tree twice");
      return IPC_FAIL_NO_REASON(this);
    }

    NotifyChildCreated(aChild, aEmbedderId, "NotifyChildRecreated");
  }
  RefreshEmbedPolicies(aEmbedderId, LayersId{});
  *aOptions = mOptions;
  return IPC_OK();
}

LayersId CompositorBridgeParent::NotifyChildCreated(LayersId aChild,
                                                    LayersId aEmbedderId,
                                                    const char* aMessage) {
  sIndirectLayerTreesLock.AssertCurrentThreadOwns();
  LayerTreeState& state = sIndirectLayerTrees.try_emplace(aChild).first->second;
  LayersId oldEmbedder = state.mEmbedderLayersId;
  LAYERTREE_LOG(
      "Layer tree {:x} attached to root layer tree {:x} by {}, embedder {:x}",
      aChild, mRootLayerTreeID, aMessage, aEmbedderId);
  state.mParent = this;
  state.mEmbedderLayersId = aEmbedderId;
  return oldEmbedder;
}

/* static */
void CompositorBridgeParent::RefreshEmbedPolicies(LayersId aNewEmbedder,
                                                  LayersId aOldEmbedder) {
  if (!StaticPrefs::gfx_webrender_enforce_embed_policy_AtStartup()) {
    return;
  }
  AutoTArray<RefPtr<WebRenderBridgeParent>, 2> bridges;
  {
    StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
    auto addBridge = [&](LayersId aEmbedder) {
      LayerTreeState* state = GetLayerTreeStateUnderLock(aEmbedder, lock);
      if (state && state->mWrBridge) {
        bridges.AppendElement(state->mWrBridge);
      }
    };
    addBridge(aNewEmbedder);
    if (aOldEmbedder != aNewEmbedder) {
      addBridge(aOldEmbedder);
    }
  }
  for (const auto& bridge : bridges) {
    bridge->RefreshEmbedPolicy();
  }
}

/* static */
void CompositorBridgeParent::CollectEmbeddedChildNamespaces(
    LayersId aEmbedder, nsTArray<uint32_t>& aOut) {
  if (!aEmbedder.IsValid()) {
    return;
  }
  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  for (const auto& [layersId, state] : sIndirectLayerTrees) {
    if (state.mEmbedderLayersId == aEmbedder) {
      aOut.AppendElement(wr::AsPipelineId(layersId).mNamespace);
    }
  }
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvMapAndNotifyChildCreated(
    const LayersId& aChild, const LayersId& aEmbedderId,
    const base::ProcessId& aOwnerPid, CompositorOptions* aOptions) {
  // We only use this message when the remote compositor is in the GPU process.
  // It is harmless to call it, though.
  MOZ_RELEASE_ASSERT(XRE_IsGPUProcess());

  LayerTreeOwnerTracker::Get()->Map(aChild, aOwnerPid);

  LayersId oldEmbedder;
  {
    StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
    oldEmbedder =
        NotifyChildCreated(aChild, aEmbedderId, "MapAndNotifyChildCreated");
  }
  RefreshEmbedPolicies(aEmbedderId, oldEmbedder);
  *aOptions = mOptions;
  return IPC_OK();
}

enum class CompositorOptionsChangeKind {
  eSupported,
  eBestEffort,
  eUnsupported
};

static CompositorOptionsChangeKind ClassifyCompositorOptionsChange(
    const CompositorOptions& aOld, const CompositorOptions& aNew) {
  if (aOld == aNew) {
    return CompositorOptionsChangeKind::eSupported;
  }
  if (aOld.EqualsIgnoringApzEnablement(aNew)) {
    return CompositorOptionsChangeKind::eBestEffort;
  }
  return CompositorOptionsChangeKind::eUnsupported;
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvAdoptChild(
    const LayersId& child, const LayersId& embedderId) {
  RefPtr<APZUpdater> oldApzUpdater;
  RefPtr<APZCTreeManagerParent> parent;
  bool apzEnablementChanged = false;
  RefPtr<WebRenderBridgeParent> childWrBridge;

  // Before adopting the child, save the old compositor's root content
  // controller. We may need this to clear old layer transforms associated
  // with the child.
  // This is outside the lock because GetGeckoContentControllerForRoot()
  // does its own locking.
  RefPtr<GeckoContentController> oldRootController =
      GetGeckoContentControllerForRoot(child);

  {  // scope lock
    StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
    CompositorBridgeParent::LayerTreeState& childState =
        EnsureLayerTreeStateUnderLock(child, lock);
    // If child is already belong to this CompositorBridgeParent,
    // no need to handle adopting child.
    if (childState.mParent == this) {
      return IPC_OK();
    }

    if (childState.mParent) {
      switch (ClassifyCompositorOptionsChange(childState.mParent->mOptions,
                                              mOptions)) {
        case CompositorOptionsChangeKind::eUnsupported: {
          MOZ_RELEASE_ASSERT(
              false,
              "Moving tab between windows whose compositor options"
              "differ in unsupported ways. Things may break in "
              "unexpected ways");
          break;
        }
        case CompositorOptionsChangeKind::eBestEffort: {
          NS_WARNING(
              "Moving tab between windows with different APZ enablement. "
              "This is supported on a best-effort basis, but some things may "
              "break.");
          apzEnablementChanged = true;
          break;
        }
        case CompositorOptionsChangeKind::eSupported: {
          // The common case, no action required.
          break;
        }
      }
      LAYERTREE_LOG("Layer tree {:x} moving from root layer tree {:x} to {:x}",
                    child, childState.mParent->mRootLayerTreeID,
                    mRootLayerTreeID);
      oldApzUpdater = childState.mParent->mApzUpdater;
    }
    if (mWrBridge) {
      childWrBridge = childState.mWrBridge;
    }
    parent = childState.mApzcTreeManagerParent;
  }

  if (childWrBridge) {
    MOZ_RELEASE_ASSERT(mWrBridge);
    RefPtr<wr::WebRenderAPI> api = mWrBridge->GetWebRenderAPI();
    api = api->Clone();
    wr::Epoch newEpoch = childWrBridge->UpdateWebRender(
        mWrBridge->CompositorScheduler(), std::move(api),
        mWrBridge->AsyncImageManager(),
        mWrBridge->GetTextureFactoryIdentifier());
    // Pretend we composited, since parent CompositorBridgeParent was replaced.
    TimeStamp now = TimeStamp::Now();
    NotifyPipelineRendered(childWrBridge->PipelineId(), newEpoch, VsyncId(),
                           now, now, now);
  }

  LayersId oldEmbedder;
  {
    StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
    // Update sIndirectLayerTrees[child].mParent after
    // WebRenderBridgeParent::UpdateWebRender().
    oldEmbedder = NotifyChildCreated(child, embedderId, "AdoptChild");
  }
  RefreshEmbedPolicies(embedderId, oldEmbedder);

  if (oldApzUpdater) {
    // If we are moving a child from an APZ-enabled window to an APZ-disabled
    // window (which can happen if e.g. a WebExtension moves a tab into a
    // popup window), try to handle it gracefully by clearing the old layer
    // transforms associated with the child. (Since the new compositor is
    // APZ-disabled, there will be nothing to update the transforms going
    // forward.)
    if (!mApzUpdater && oldRootController) {
      // Tell the old APZCTreeManager not to send any more layer transforms
      // for this layers ids.
      oldApzUpdater->MarkAsDetached(child);

      // Clear the current transforms.
      nsTArray<MatrixMessage> clear;
      clear.AppendElement(MatrixMessage(Nothing(), ScreenRect(), child));
      oldRootController->NotifyLayerTransforms(std::move(clear));
    }
  }
  if (mApzUpdater) {
    if (parent) {
      MOZ_RELEASE_ASSERT(mApzcTreeManager);
      parent->ChildAdopted(mApzcTreeManager, mApzUpdater);
    }
    mApzUpdater->NotifyLayerTreeAdopted(child, oldApzUpdater);
  }
  if (apzEnablementChanged) {
    (void)SendCompositorOptionsChanged(child, mOptions);
  }
  return IPC_OK();
}

already_AddRefed<PWebRenderBridgeParent>
CompositorBridgeParent::AllocPWebRenderBridgeParent(
    const wr::PipelineId& aPipelineId, const LayoutDeviceIntSize& aSize,
    const WindowKind& aWindowKind) {
  MOZ_RELEASE_ASSERT(wr::AsLayersId(aPipelineId) == mRootLayerTreeID);
  MOZ_RELEASE_ASSERT(!mWrBridge);
  MOZ_RELEASE_ASSERT(!mCompositorScheduler);
  MOZ_ASSERT(mWidget);

#ifdef XP_WIN
  if (mWidget && mWidget->AsWindows()) {
    const auto options = mWidget->GetCompositorOptions();
    if (!options.UseSoftwareWebRender() &&
        (DeviceManagerDx::Get()->CanUseDComp() ||
         gfxVars::UseWebRenderFlipSequentialWin())) {
      mWidget->AsWindows()->EnsureCompositorWindow();
    } else if (options.UseSoftwareWebRender() &&
               mWidget->AsWindows()->GetCompositorHwnd()) {
      mWidget->AsWindows()->DestroyCompositorWindow();
    }
  }
#endif

  RefPtr<widget::CompositorWidget> widget = mWidget;
  wr::WrWindowId windowId = wr::NewWindowId();
  if (mApzUpdater) {
    // If APZ is enabled, we need to register the APZ updater with the window id
    // before the updater thread is created in WebRenderAPI::Create, so
    // that the callback from the updater thread can find the right APZUpdater.
    mApzUpdater->SetWebRenderWindowId(windowId);
  }
  if (mApzSampler) {
    // Same as for mApzUpdater, but for the sampler thread.
    mApzSampler->SetWebRenderWindowId(windowId);
  }
  if (mOMTASampler) {
    // Same, but for the OMTA sampler.
    mOMTASampler->SetWebRenderWindowId(windowId);
  }

  const RefPtr<nsIThread> renderThread = wr::RenderThread::GetRenderThread();
  wr::WebRenderAPI::Create(this, std::move(widget), windowId, aSize,
                           aWindowKind)
      ->Then(
          renderThread, __func__,
          [self = RefPtr{this}](
              wr::WebRenderAPI::CreatePromise::ResolveOrRejectValue&& aResult) {
            // Still on the Renderer thread, store the result of WebRenderAPI
            // creation in mWrApiResult, and notify anybody waiting that the
            // result is available.
            MonitorAutoLock lock(self->mWrApiResultMonitor);
            if (aResult.IsResolve()) {
              MOZ_RELEASE_ASSERT(aResult.ResolveValue());
              self->mWrApiResult.emplace(aResult.ResolveValue());
            } else {
              self->mWrApiResult.emplace(Err(aResult.RejectValue()));
            }
            lock.NotifyAll();
            return MozPromise<Ok, Ok, true>::CreateAndResolve(Ok{}, __func__);
          })
      ->Then(GetCurrentSerialEventTarget(), __func__, [self = RefPtr{this}]() {
        // Then finally complete WebRenderBridgeParent initialization back on
        // the Compositor thread.
        self->EnsureWebRenderBridgeParentInitialized();
      });

  mWrBridge =
      MakeRefPtr<WebRenderBridgeParent>(this, aPipelineId, mWidget, mVsyncRate);
  {  // scope lock
    StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
    CompositorBridgeParent::LayerTreeState& state =
        EnsureLayerTreeStateUnderLock(mRootLayerTreeID, lock);
    MOZ_RELEASE_ASSERT(state.mWrBridge == nullptr);
    state.mWrBridge = mWrBridge;
  }
  return do_AddRef(mWrBridge);
}

void CompositorBridgeParent::EnsureWebRenderBridgeParentInitialized() {
  MOZ_RELEASE_ASSERT(NS_IsInCompositorThread());

  if (mWrBridgeInitialized) {
    return;
  }
  mWrBridgeInitialized = true;

  // Wait for WebRenderAPI creation to complete on the Renderer thread.
  mozilla::Result<RefPtr<wr::WebRenderAPI>, nsCString> result = [this]() {
    MonitorAutoLock lock(mWrApiResultMonitor);
    while (!mWrApiResult) {
      lock.Wait();
    }
    return mWrApiResult.extract();
  }();

  // If the bridge has already been destroyed there is nothing to do. Note that
  // we early return *after* extracting the Api from mWrApiResult to ensure the
  // API will be destroyed.
  if (!mWrBridge) {
    return;
  }

  if (result.isErr()) {
    mWrBridge->FinishInitializationError(result.unwrapErr());
    return;
  }

  RefPtr<wr::WebRenderAPI> api = result.unwrap();
#ifdef MOZ_WIDGET_ANDROID
  // On Android, WebRenderAPI::Resume() call is triggered from Java side. But
  // Java side does not know about fallback to RenderCompositorOGLSWGL. In this
  // fallback case, RenderCompositor::Resume() needs to be called from gfx code.
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  if (!mPaused && mWidget->GetCompositorOptions().UseSoftwareWebRender() &&
      mWidget->GetCompositorOptions().AllowSoftwareWebRenderOGL()) {
    api->Resume();
  }
#endif

  wr::TransactionBuilder txn(api);
  txn.SetRootPipeline(mWrBridge->PipelineId());
  api->SendTransaction(txn);

  bool useCompositorWnd = false;
#ifdef XP_WIN
  // Headless mode uses HeadlessWidget.
  if (mWidget->AsWindows()) {
    useCompositorWnd = !!mWidget->AsWindows()->GetCompositorHwnd();
  }
#endif
  mAsyncImageManager =
      new AsyncImagePipelineManager(api->Clone(), useCompositorWnd);
  RefPtr<AsyncImagePipelineManager> asyncMgr = mAsyncImageManager;

  {
    StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
    EnsureLayerTreeStateUnderLock(mRootLayerTreeID, lock).mWebRenderAPI = api;
  }

  mWrBridge->FinishInitialization(std::move(api), std::move(asyncMgr));

  mAsyncImageManager->SetTextureFactoryIdentifier(
      mWrBridge->GetTextureFactoryIdentifier());

  mCompositorScheduler = mWrBridge->CompositorScheduler();
  MOZ_RELEASE_ASSERT(mCompositorScheduler);
}

void CompositorBridgeParent::NotifyMemoryPressure() {
  if (mWrBridge) {
    RefPtr<wr::WebRenderAPI> api = mWrBridge->GetWebRenderAPI();
    if (api) {
      api->NotifyMemoryPressure();
    }
  }
}

void CompositorBridgeParent::AccumulateMemoryReport(wr::MemoryReport* aReport) {
  if (mWrBridge) {
    RefPtr<wr::WebRenderAPI> api = mWrBridge->GetWebRenderAPI();
    if (api) {
      api->AccumulateMemoryReport(aReport);
    }
  }
}

/*static*/
void CompositorBridgeParent::InitializeStatics() {
  gfxVars::SetForceSubpixelAAWherePossibleListener(&UpdateQualitySettings);
  gfxVars::SetWebRenderDebugFlagsListener(&UpdateDebugFlags);
  gfxVars::SetWebRenderBoolParametersListener(&UpdateWebRenderBoolParameters);
  gfxVars::SetWebRenderBatchingLookbackListener(&UpdateWebRenderParameters);
  gfxVars::SetWebRenderBlobTileSizeListener(&UpdateWebRenderParameters);
  gfxVars::SetWebRenderSlowCpuFrameThresholdListener(
      &UpdateWebRenderParameters);
  gfxVars::SetWebRenderBatchedUploadThresholdListener(
      &UpdateWebRenderParameters);
  gfxVars::SetWebRenderPathAaSamplesListener(&UpdateWebRenderParameters);

  gfxVars::SetWebRenderProfilerUIListener(&UpdateWebRenderProfilerUI);
}

/*static*/
void CompositorBridgeParent::UpdateQualitySettings() {
  if (!CompositorThreadHolder::IsInCompositorThread()) {
    if (CompositorThread()) {
      CompositorThread()->Dispatch(
          NewRunnableFunction("CompositorBridgeParent::UpdateQualitySettings",
                              &CompositorBridgeParent::UpdateQualitySettings));
    }

    // If there is no compositor thread, e.g. due to shutdown, then we can
    // safefully just ignore this request.
    return;
  }

  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  ForEachWebRenderBridgeParent([&](WebRenderBridgeParent* wrBridge) -> void {
    if (!wrBridge->IsRootWebRenderBridgeParent()) {
      return;
    }
    wrBridge->UpdateQualitySettings();
  });
}

/*static*/
void CompositorBridgeParent::UpdateDebugFlags() {
  if (!CompositorThreadHolder::IsInCompositorThread()) {
    if (CompositorThread()) {
      CompositorThread()->Dispatch(
          NewRunnableFunction("CompositorBridgeParent::UpdateDebugFlags",
                              &CompositorBridgeParent::UpdateDebugFlags));
    }

    // If there is no compositor thread, e.g. due to shutdown, then we can
    // safefully just ignore this request.
    return;
  }

  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  ForEachWebRenderBridgeParent([&](WebRenderBridgeParent* wrBridge) -> void {
    if (!wrBridge->IsRootWebRenderBridgeParent()) {
      return;
    }
    wrBridge->UpdateDebugFlags();
  });
}

/*static*/
void CompositorBridgeParent::UpdateWebRenderBoolParameters() {
  if (!CompositorThreadHolder::IsInCompositorThread()) {
    if (CompositorThread()) {
      CompositorThread()->Dispatch(NewRunnableFunction(
          "CompositorBridgeParent::UpdateWebRenderBoolParameters",
          &CompositorBridgeParent::UpdateWebRenderBoolParameters));
    }

    return;
  }

  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  ForEachWebRenderBridgeParent([&](WebRenderBridgeParent* wrBridge) -> void {
    if (!wrBridge->IsRootWebRenderBridgeParent()) {
      return;
    }
    wrBridge->UpdateBoolParameters();
  });
}

/*static*/
void CompositorBridgeParent::UpdateWebRenderParameters() {
  if (!CompositorThreadHolder::IsInCompositorThread()) {
    if (CompositorThread()) {
      CompositorThread()->Dispatch(NewRunnableFunction(
          "CompositorBridgeParent::UpdateWebRenderParameters",
          &CompositorBridgeParent::UpdateWebRenderParameters));
    }

    return;
  }

  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  ForEachWebRenderBridgeParent([&](WebRenderBridgeParent* wrBridge) -> void {
    if (!wrBridge->IsRootWebRenderBridgeParent()) {
      return;
    }
    wrBridge->UpdateParameters();
  });
}

/*static*/
void CompositorBridgeParent::UpdateWebRenderProfilerUI() {
  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  ForEachWebRenderBridgeParent([&](WebRenderBridgeParent* wrBridge) -> void {
    if (!wrBridge->IsRootWebRenderBridgeParent()) {
      return;
    }
    wrBridge->UpdateProfilerUI();
  });
}

RefPtr<WebRenderBridgeParent> CompositorBridgeParent::GetWebRenderBridgeParent()
    const {
  return mWrBridge;
}

Maybe<TimeStamp> CompositorBridgeParent::GetTestingTimeStamp() const {
  return mTestTime;
}

void EraseLayerState(LayersId aId) {
  RefPtr<APZUpdater> apz;
  RefPtr<WebRenderBridgeParent> wrBridge;

  CompositorBridgeParent::WithIndirectLayerTreesLock(
      [&](const StaticMonitorAutoLock& aProof) {
        auto* state =
            CompositorBridgeParent::GetLayerTreeStateUnderLock(aId, aProof);
        if (state) {
          CompositorBridgeParent* parent = state->mParent;
          if (parent) {
            apz = parent->GetAPZUpdater();
          }
          wrBridge = state->mWrBridge;
          LAYERTREE_LOG("Erasing layer tree {:x}", aId);
          CompositorBridgeParent::EraseLayerTreeStateUnderLock(aId, aProof);
        }
      });

  if (apz) {
    apz->NotifyLayerTreeRemoved(aId);
  }

  if (wrBridge) {
    wrBridge->Destroy();
  }
}

/*static*/
void CompositorBridgeParent::DeallocateLayerTreeId(LayersId aId) {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  // Here main thread notifies compositor to remove an element from
  // sIndirectLayerTrees. This removed element might be queried soon.
  // Checking the elements of sIndirectLayerTrees exist or not before using.
  if (!CompositorThread()) {
    gfxCriticalError() << "Attempting to post to an invalid Compositor Thread";
    return;
  }
  CompositorThread()->Dispatch(
      NewRunnableFunction("EraseLayerStateRunnable", &EraseLayerState, aId));
}

static void UpdateControllerForLayersId(LayersId aLayersId,
                                        GeckoContentController* aController) {
  // Adopt ref given to us by SetControllerForLayerTree()
  CompositorBridgeParent::WithIndirectLayerTreesLock(
      [&](const StaticMonitorAutoLock& aProof) {
        CompositorBridgeParent::EnsureLayerTreeStateUnderLock(aLayersId, aProof)
            .mController =
            already_AddRefed<GeckoContentController>(aController);
      });
}

ScopedLayerTreeRegistration::ScopedLayerTreeRegistration(
    LayersId aLayersId, GeckoContentController* aController)
    : mLayersId(aLayersId) {
  CompositorBridgeParent::WithIndirectLayerTreesLock(
      [&](const StaticMonitorAutoLock& aProof) {
        CompositorBridgeParent::EnsureLayerTreeStateUnderLock(aLayersId, aProof)
            .mController = aController;
      });
}

ScopedLayerTreeRegistration::~ScopedLayerTreeRegistration() {
  CompositorBridgeParent::WithIndirectLayerTreesLock(
      [&](const StaticMonitorAutoLock& aProof) {
        CompositorBridgeParent::EraseLayerTreeStateUnderLock(mLayersId, aProof);
      });
}

/*static*/
void CompositorBridgeParent::SetControllerForLayerTree(
    LayersId aLayersId, GeckoContentController* aController) {
  // This ref is adopted by UpdateControllerForLayersId().
  aController->AddRef();
  CompositorThread()->Dispatch(NewRunnableFunction(
      "UpdateControllerForLayersIdRunnable", &UpdateControllerForLayersId,
      aLayersId, aController));
}

/*static*/
already_AddRefed<IAPZCTreeManager> CompositorBridgeParent::GetAPZCTreeManager(
    LayersId aLayersId) {
  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  LayerTreeMap::iterator cit = sIndirectLayerTrees.find(aLayersId);
  if (sIndirectLayerTrees.end() == cit) {
    return nullptr;
  }
  LayerTreeState* lts = &cit->second;

  RefPtr<IAPZCTreeManager> apzctm =
      lts->mParent ? lts->mParent->mApzcTreeManager.get() : nullptr;
  return apzctm.forget();
}

struct VsyncMarker : public BaseMarkerType<VsyncMarker> {
  static constexpr const char* Name = "VsyncTimestamp";
  static constexpr const char* Description =
      "Tracks when a vsync occurs according to the HardwareComposer";
  using MS = MarkerSchema;
  static constexpr MS::Location Locations[] = {
      MS::Location::MarkerChart,
      MS::Location::MarkerTable,
  };
};

static void InsertVsyncProfilerMarker(TimeStamp aVsyncTimestamp) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  if (profiler_thread_is_being_profiled_for_markers()) {
    profiler_add_marker("VsyncTimestamp", geckoprofiler::category::GRAPHICS,
                        MarkerTiming::InstantAt(aVsyncTimestamp),
                        VsyncMarker{});
  }
}

/*static */
void CompositorBridgeParent::PostInsertVsyncProfilerMarker(
    TimeStamp aVsyncTimestamp) {
  // Called in the vsync thread
  if (profiler_is_active() && CompositorThreadHolder::IsActive()) {
    CompositorThread()->Dispatch(
        NewRunnableFunction("InsertVsyncProfilerMarkerRunnable",
                            InsertVsyncProfilerMarker, aVsyncTimestamp));
  }
}

already_AddRefed<widget::PCompositorWidgetParent>
CompositorBridgeParent::AllocPCompositorWidgetParent(
    const CompositorWidgetInitData& aInitData) {
#if defined(MOZ_WIDGET_SUPPORTS_OOP_COMPOSITING)
  if (mWidget) {
    // Should not create two widgets on the same compositor.
    return nullptr;
  }

#  ifndef MOZ_WIDGET_UIKIT
  if (aInitData.type() ==
      CompositorWidgetInitData::THeadlessCompositorWidgetInitData) {
    RefPtr widget = MakeRefPtr<widget::HeadlessCompositorWidgetParent>(
        aInitData.get_HeadlessCompositorWidgetInitData(), mOptions);
    mWidget = widget;
    return widget.forget();
  }
#  endif

  RefPtr widget =
      MakeRefPtr<widget::CompositorWidgetParent>(aInitData, mOptions);

  // Sending the constructor acts as initialization as well.
  mWidget = widget;
  return widget.forget();
#else
  return nullptr;
#endif
}

#ifdef XP_MACOSX
mozilla::ipc::IPCResult
CompositorBridgeParent::RecvPCompositorWidgetConstructor(
    PCompositorWidgetParent* actor, CompositorWidgetInitData&& aInitData) {
  if (aInitData.type() ==
      CompositorWidgetInitData::THeadlessCompositorWidgetInitData) {
    return IPC_OK();
  }
  // macOS CocoaCompositorWidget (a superclass of the platform-specific
  // CompositorWidgetParent) requires an extra step to pass aInitData
  // with move semantics, because IPDL can't generate move semantics
  // in the constructor. The macOS-specific aInitData contains an
  // Endpoint, so it *must* use move semantics.
  auto* widget = static_cast<widget::CompositorWidgetParent*>(actor);
  widget->Init(std::move(aInitData));
  return IPC_OK();
}
#endif

CompositorController*
CompositorBridgeParent::LayerTreeState::GetCompositorController() const {
  return mParent;
}

void CompositorBridgeParent::ScheduleFrameAfterSceneBuild(
    RefPtr<const wr::WebRenderPipelineInfo> aInfo) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  if (mPaused) {
    return;
  }

  if (mWrBridge) {
    mWrBridge->ScheduleFrameAfterSceneBuild(aInfo);
  }
}

void CompositorBridgeParent::NotifyDidRender(const VsyncId& aCompositeStartId,
                                             TimeStamp& aCompositeStart,
                                             TimeStamp& aRenderStart,
                                             TimeStamp& aCompositeEnd,
                                             wr::RendererStats* aStats) {
  if (!mWrBridge) {
    return;
  }

  MOZ_RELEASE_ASSERT(mWrBridge->IsRootWebRenderBridgeParent());

  RefPtr<UiCompositorControllerParent> uiController =
      UiCompositorControllerParent::GetFromRootLayerTreeId(mRootLayerTreeID);

  if (uiController && mIsForcedFirstPaint) {
    uiController->NotifyFirstPaint();
    mIsForcedFirstPaint = false;
  }

  nsTArray<CompositionPayload> payload =
      mWrBridge->TakePendingScrollPayload(aCompositeStartId);
  if (!payload.IsEmpty()) {
    RecordCompositionPayloadsPresented(aCompositeEnd, payload);
  }

  nsTArray<ImageCompositeNotificationInfo> notifications;
  mWrBridge->ExtractImageCompositeNotifications(&notifications);
  if (!notifications.IsEmpty()) {
    (void)ImageBridgeParent::NotifyImageComposites(notifications);
  }
}

bool CompositorBridgeParent::sStable = false;
uint32_t CompositorBridgeParent::sFramesComposited = 0;

/* static */ void CompositorBridgeParent::ResetStable() {
  if (!CompositorThreadHolder::IsInCompositorThread()) {
    if (CompositorThread()) {
      CompositorThread()->Dispatch(
          NewRunnableFunction("CompositorBridgeParent::ResetStable",
                              &CompositorBridgeParent::ResetStable));
    }

    // If there is no compositor thread, e.g. due to shutdown, then we can
    // safefully just ignore this request.
    return;
  }

  sStable = false;
  sFramesComposited = 0;
}

void CompositorBridgeParent::MaybeDeclareStable() {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  if (sStable) {
    return;
  }

  // Once we render as many frames as the threshold, we declare this instance of
  // the GPU process 'stable'. This causes the parent process to always respawn
  // the GPU process if it crashes.
  if (++sFramesComposited >=
      StaticPrefs::layers_gpu_process_stable_frame_threshold()) {
    sStable = true;

    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "CompositorBridgeParent::MaybeDeclareStable", []() -> void {
          if (XRE_IsParentProcess()) {
            GPUProcessManager* gpm = GPUProcessManager::Get();
            if (gpm) {
              gpm->OnProcessDeclaredStable();
            }
          } else {
            gfx::GPUParent* gpu = gfx::GPUParent::GetSingleton();
            if (gpu && gpu->CanSend()) {
              (void)gpu->SendDeclareStable();
            }
          }
        }));
  }
}

void CompositorBridgeParent::NotifyPipelineRendered(
    const wr::PipelineId& aPipelineId, const wr::Epoch& aEpoch,
    const VsyncId& aCompositeStartId, TimeStamp& aCompositeStart,
    TimeStamp& aRenderStart, TimeStamp& aCompositeEnd,
    wr::RendererStats* aStats) {
  if (!mWrBridge || !mAsyncImageManager) {
    return;
  }

  bool isRoot = mWrBridge->PipelineId() == aPipelineId;
  RefPtr<WebRenderBridgeParent> wrBridge =
      isRoot ? mWrBridge
             : RefPtr<WebRenderBridgeParent>(
                   mAsyncImageManager->GetWrBridge(aPipelineId));
  if (!wrBridge) {
    return;
  }

  CompositorBridgeParentBase* compBridge =
      isRoot ? this : wrBridge->GetCompositorBridge();
  if (!compBridge) {
    return;
  }

  MOZ_RELEASE_ASSERT(isRoot == wrBridge->IsRootWebRenderBridgeParent());

  wrBridge->RemoveEpochDataPriorTo(aEpoch);

  nsTArray<FrameStats> stats;
  nsTArray<TransactionId> transactions;

  RefPtr<UiCompositorControllerParent> uiController =
      UiCompositorControllerParent::GetFromRootLayerTreeId(mRootLayerTreeID);

  wrBridge->FlushTransactionIdsForEpoch(
      aEpoch, aCompositeStartId, aCompositeStart, aRenderStart, aCompositeEnd,
      uiController, aStats, stats, transactions);
  if (transactions.IsEmpty()) {
    MOZ_RELEASE_ASSERT(stats.IsEmpty());
    return;
  }

  MaybeDeclareStable();

  LayersId layersId = isRoot ? LayersId{0} : wrBridge->GetLayersId();
  (void)compBridge->SendDidComposite(layersId, transactions, aCompositeStart,
                                     aCompositeEnd);

  if (!stats.IsEmpty()) {
    (void)SendNotifyFrameStats(stats);
  }
}

RefPtr<AsyncImagePipelineManager>
CompositorBridgeParent::GetAsyncImagePipelineManager() const {
  return mAsyncImageManager;
}

/* static */ CompositorBridgeParent::LayerTreeState*
CompositorBridgeParent::GetLayerTreeStateInternal(LayersId aId) {
  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  LayerTreeMap::iterator cit = sIndirectLayerTrees.find(aId);
  if (sIndirectLayerTrees.end() == cit) {
    return nullptr;
  }
  return &cit->second;
}

/* static */
bool CompositorBridgeParent::HasLayerTreeState(LayersId aId) {
  return GetLayerTreeStateInternal(aId) != nullptr;
}

/* static */ CompositorBridgeParent::LayerTreeState*
CompositorBridgeParent::GetLayerTreeState(LayersId aId) {
  // Only the compositor thread should use this method variant
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  return GetLayerTreeStateInternal(aId);
}

/* static */
bool CompositorBridgeParent::CallWithLayerTreeState(
    LayersId aId,
    const std::function<void(CompositorBridgeParent::LayerTreeState&)>& aFunc) {
  // Note that this does not make things universally threadsafe just because the
  // sIndirectLayerTreesLock mutex is held. This is because the compositor
  // thread can mutate the LayerTreeState outside the lock. It does however
  // ensure that the *storage* for the LayerTreeState remains stable, since we
  // should always hold the lock when adding/removing entries to the map.
  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  LayerTreeMap::iterator cit = sIndirectLayerTrees.find(aId);
  if (sIndirectLayerTrees.end() == cit) {
    return false;
  }
  aFunc(cit->second);
  return true;
}

/* static */ CompositorBridgeParent::LayerTreeState*
CompositorBridgeParent::GetLayerTreeStateUnderLock(
    LayersId aId, const StaticMonitorAutoLock& aProofOfLock) {
  sIndirectLayerTreesLock.AssertCurrentThreadOwns();
  LayerTreeMap::iterator it = sIndirectLayerTrees.find(aId);
  if (sIndirectLayerTrees.end() == it) {
    return nullptr;
  }
  return &it->second;
}

/* static */ CompositorBridgeParent::LayerTreeState&
CompositorBridgeParent::EnsureLayerTreeStateUnderLock(
    LayersId aId, const StaticMonitorAutoLock& aProofOfLock) {
  sIndirectLayerTreesLock.AssertCurrentThreadOwns();
  return sIndirectLayerTrees.try_emplace(aId).first->second;
}

/* static */ void CompositorBridgeParent::EraseLayerTreeStateUnderLock(
    LayersId aId, const StaticMonitorAutoLock& aProofOfLock) {
  sIndirectLayerTreesLock.AssertCurrentThreadOwns();
  sIndirectLayerTrees.erase(aId);
}

static CompositorBridgeParent::LayerTreeState* GetStateForRoot(
    LayersId aContentLayersId, const StaticMonitorAutoLock& aProofOfLock) {
  CompositorBridgeParent::LayerTreeState* contentState =
      CompositorBridgeParent::GetLayerTreeStateUnderLock(aContentLayersId,
                                                         aProofOfLock);

  // |contentState| is the state for the content process, but we want the
  // APZCTMParent for the parent process owning that content process. So we have
  // to jump to the LayerTreeState for the root layer tree id for that layer
  // tree, and use the mApzcTreeManagerParent from that. This should also work
  // with nested content processes, because RootLayerTreeId() will bypass any
  // intermediate processes' ids and go straight to the root.
  if (contentState && contentState->mParent) {
    LayersId rootLayersId = contentState->mParent->RootLayerTreeId();
    return CompositorBridgeParent::GetLayerTreeStateUnderLock(rootLayersId,
                                                              aProofOfLock);
  }

  // Don't return contentState, that would be a lie!
  return nullptr;
}

/* static */
RefPtr<APZBridgeParent> CompositorBridgeParent::GetApzBridgeParentForRoot(
    LayersId aContentLayersId) {
  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  CompositorBridgeParent::LayerTreeState* state =
      GetStateForRoot(aContentLayersId, lock);
  return state ? state->mApzBridgeParent : nullptr;
}

/* static */
GeckoContentController*
CompositorBridgeParent::GetGeckoContentControllerForRoot(
    LayersId aContentLayersId) {
  StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
  CompositorBridgeParent::LayerTreeState* state =
      GetStateForRoot(aContentLayersId, lock);
  return state ? state->mController.get() : nullptr;
}

already_AddRefed<PTextureParent> CompositorBridgeParent::AllocPTextureParent(
    const SurfaceDescriptor& aSharedData, ReadLockDescriptor& aReadLock,
    const LayersBackend& aLayersBackend, const TextureFlags& aFlags,
    const uint64_t& aSerial, const wr::MaybeExternalImageId& aExternalImageId) {
  return TextureHost::CreateIPDLActor(
      this, aSharedData, std::move(aReadLock), aLayersBackend, aFlags,
      mCompositorManager->GetContentId(), aSerial, aExternalImageId);
}

bool CompositorBridgeParent::IsSameProcess() const {
  return OtherPid() == base::GetCurrentProcId();
}

struct ContentFrameMarker : public BaseMarkerType<ContentFrameMarker> {
  static constexpr const char* Name = "CONTENT_FRAME_TIME";
  using MS = MarkerSchema;
  static constexpr MS::Location Locations[] = {
      MS::Location::MarkerChart,
      MS::Location::MarkerTable,
  };
};

int32_t RecordContentFrameTime(
    const VsyncId& aTxnId, const TimeStamp& aVsyncStart,
    const TimeStamp& aTxnStart, const VsyncId& aCompositeId,
    const TimeStamp& aCompositeEnd, const TimeDuration& aFullPaintTime,
    const TimeDuration& aVsyncRate, bool aContainsSVGGroup,
    bool aRecordUploadStats, wr::RendererStats* aStats /* = nullptr */) {
  double latencyMs = (aCompositeEnd - aTxnStart).ToMilliseconds();
  double latencyNorm = latencyMs / aVsyncRate.ToMilliseconds();
  int32_t fracLatencyNorm = lround(latencyNorm * 100.0);

  if (profiler_thread_is_being_profiled_for_markers()) {
    profiler_add_marker("CONTENT_FRAME_TIME", geckoprofiler::category::GRAPHICS,
                        MarkerTiming::Interval(aTxnStart, aCompositeEnd),
                        ContentFrameMarker{});
  }

  mozilla::glean::gfx_content_frame_time::from_paint.AccumulateSingleSample(
      static_cast<unsigned long long>(fracLatencyNorm));

  if (!(aTxnId == VsyncId()) && aVsyncStart) {
    latencyMs = (aCompositeEnd - aVsyncStart).ToMilliseconds();
    latencyNorm = latencyMs / aVsyncRate.ToMilliseconds();
    fracLatencyNorm = lround(latencyNorm * 100.0);
    int32_t result = fracLatencyNorm;
    mozilla::glean::gfx_content_frame_time::from_vsync.AccumulateSingleSample(
        static_cast<unsigned long long>(fracLatencyNorm));

    if (aContainsSVGGroup) {
      mozilla::glean::gfx_content_frame_time::with_svg.AccumulateSingleSample(
          static_cast<unsigned long long>(fracLatencyNorm));
    }

    // Record glean::gfx_content_frame_time::reason
    //
    // Note that deseralizing a layers update (RecvUpdate) can delay the receipt
    // of the composite vsync message
    // (CompositorBridgeParent::CompositeToTarget), since they're using the same
    // thread. This can mean that compositing might start significantly late,
    // but this code will still detect it as having successfully started on the
    // right vsync (which is somewhat correct). We'd now have reduced time left
    // in the vsync interval to finish compositing, so the chances of a missed
    // frame increases. This is effectively including the RecvUpdate work as
    // part of the 'compositing' phase for this metric, but it isn't included in
    // COMPOSITE_TIME, and *is* included in CONTENT_FULL_PAINT_TIME.
    //
    // Also of note is that when the root WebRenderBridgeParent decides to
    // skip a composite (due to the Renderer being busy), that won't notify
    // child WebRenderBridgeParents. That failure will show up as the
    // composite starting late (since it did), but it's really a fault of a
    // slow composite on the previous frame, not a slow
    // CONTENT_FULL_PAINT_TIME. It would be nice to have a separate bucket for
    // this category (scene was ready on the next vsync, but we chose not to
    // composite), but I can't find a way to locate the right child
    // WebRenderBridgeParents from the root. WebRender notifies us of the
    // child pipelines contained within a render, after it finishes, but I
    // can't see how to query what child pipeline would have been rendered,
    // when we choose to not do it.
    if (fracLatencyNorm < 200) {
      // Success
      mozilla::glean::gfx_content_frame_time::reason
          .EnumGet(glean::gfx_content_frame_time::ReasonLabel::eOnTime)
          .Add();
    } else {
      if (aCompositeId == VsyncId()) {
        // aCompositeId is 0, possibly something got trigged from
        // outside vsync?
        mozilla::glean::gfx_content_frame_time::reason
            .EnumGet(glean::gfx_content_frame_time::ReasonLabel::eNoVsyncNoId)
            .Add();
      } else if (aTxnId >= aCompositeId) {
        // Vsync ids are nonsensical, maybe we're trying to catch up?
        mozilla::glean::gfx_content_frame_time::reason
            .EnumGet(glean::gfx_content_frame_time::ReasonLabel::eNoVsync)
            .Add();
      } else if (aCompositeId - aTxnId > 1) {
        // Composite started late (and maybe took too long as well)
        if (aFullPaintTime >= TimeDuration::FromMilliseconds(20)) {
          mozilla::glean::gfx_content_frame_time::reason
              .EnumGet(glean::gfx_content_frame_time::ReasonLabel::
                           eMissedCompositeLong)
              .Add();
        } else if (aFullPaintTime >= TimeDuration::FromMilliseconds(10)) {
          mozilla::glean::gfx_content_frame_time::reason
              .EnumGet(glean::gfx_content_frame_time::ReasonLabel::
                           eMissedCompositeMid)
              .Add();
        } else if (aFullPaintTime >= TimeDuration::FromMilliseconds(5)) {
          mozilla::glean::gfx_content_frame_time::reason
              .EnumGet(glean::gfx_content_frame_time::ReasonLabel::
                           eMissedCompositeLow)
              .Add();
        } else {
          mozilla::glean::gfx_content_frame_time::reason
              .EnumGet(
                  glean::gfx_content_frame_time::ReasonLabel::eMissedComposite)
              .Add();
        }
      } else {
        // Composite started on time, but must have taken too long.
        mozilla::glean::gfx_content_frame_time::reason
            .EnumGet(glean::gfx_content_frame_time::ReasonLabel::eSlowComposite)
            .Add();
      }
    }

    if (aRecordUploadStats) {
      if (aStats) {
        latencyMs -= (double(aStats->resource_upload_time) / 1000000.0);
        latencyNorm = latencyMs / aVsyncRate.ToMilliseconds();
        fracLatencyNorm = lround(latencyNorm * 100.0);
      }
      mozilla::glean::gfx_content_frame_time::without_resource_upload
          .AccumulateSingleSample(
              static_cast<unsigned long long>(fracLatencyNorm));

      mozilla::glean::gfx_content_frame_time::without_resource_upload
          .AccumulateSingleSample(
              static_cast<unsigned long long>(fracLatencyNorm));
    }
    return result;
  }

  return 0;
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvBeginRecording(
    const TimeStamp& aRecordingStart, BeginRecordingResolver&& aResolve) {
  if (mHaveCompositionRecorder) {
    aResolve(false);
    return IPC_OK();
  }

  if (mWrBridge) {
    mWrBridge->BeginRecording(aRecordingStart);
  }

  mHaveCompositionRecorder = true;
  aResolve(true);

  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvEndRecording(
    EndRecordingResolver&& aResolve) {
  if (!mHaveCompositionRecorder) {
    aResolve(Nothing());
    return IPC_OK();
  }

  if (mWrBridge) {
    mWrBridge->EndRecording()->Then(
        NS_GetCurrentThread(), __func__,
        [resolve{aResolve}](FrameRecording&& recording) {
          resolve(Some(std::move(recording)));
        },
        [resolve{aResolve}]() { resolve(Nothing()); });
  } else {
    aResolve(Nothing());
  }

  mHaveCompositionRecorder = false;

  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorBridgeParent::RecvCheckAndClearWRDidRasterize(
    const LayersId& aId, bool* aDidRasterize) {
  *aDidRasterize = false;

  if (mWrBridge) {
    if (RefPtr<wr::WebRenderAPI> api = mWrBridge->GetWebRenderAPI()) {
      *aDidRasterize = api->CheckAndClearDidRasterize();
    }
  }

  return IPC_OK();
}

void CompositorBridgeParent::FlushPendingWrTransactionEventsWithWait() {
  if (!mWrBridge) {
    return;
  }

  std::vector<RefPtr<WebRenderBridgeParent>> bridgeParents;
  {  // scope lock
    StaticMonitorAutoLock lock(sIndirectLayerTreesLock);
    ForEachIndirectLayerTree([&](LayerTreeState* lts, LayersId) -> void {
      if (lts->mWrBridge) {
        bridgeParents.emplace_back(lts->mWrBridge);
      }
    });
  }

  for (auto& bridge : bridgeParents) {
    bridge->FlushPendingWrTransactionEventsWithWait();
  }
}

void RecordCompositionPayloadsPresented(
    const TimeStamp& aCompositionEndTime,
    const nsTArray<CompositionPayload>& aPayloads) {
  if (aPayloads.Length()) {
    TimeStamp presented = aCompositionEndTime;
    for (const CompositionPayload& payload : aPayloads) {
      if (profiler_thread_is_being_profiled_for_markers()) {
        MOZ_RELEASE_ASSERT(payload.mType <= kHighestCompositionPayloadType);
        nsAutoCString name(
            kCompositionPayloadTypeNames[uint8_t(payload.mType)]);
        name.AppendLiteral(" Payload Presented");
        // This doesn't really need to be a text marker. Once we have a version
        // of profiler_add_marker that accepts both a start time and an end
        // time, we could use that here.
        nsPrintfCString text(
            "Latency: %dms",
            int32_t((presented - payload.mTimeStamp).ToMilliseconds()));
        PROFILER_MARKER_TEXT(
            name, GRAPHICS,
            MarkerTiming::Interval(payload.mTimeStamp, presented), text);
      }

      if (payload.mType == CompositionPayloadType::eKeyPress) {
        glean::performance_interaction::keypress_present_latency
            .AccumulateRawDuration(presented - payload.mTimeStamp);
      } else if (payload.mType == CompositionPayloadType::eAPZScroll) {
        mozilla::glean::gfx::scroll_present_latency.AccumulateRawDuration(
            presented - payload.mTimeStamp);
      } else if (payload.mType ==
                 CompositionPayloadType::eMouseUpFollowedByClick) {
        glean::performance_interaction::mouseup_click_present_latency
            .AccumulateRawDuration(presented - payload.mTimeStamp);
      }
    }
  }
}

}  // namespace layers
}  // namespace mozilla
