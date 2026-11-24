/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/CompositorBridgeChild.h"

#include <stddef.h>  // for size_t

#include "base/task.h"  // for NewRunnableMethod, etc
#include "gfxConfig.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/StaticPrefs_layers.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/gfx/CanvasManagerChild.h"
#include "mozilla/gfx/GPUProcessManager.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/layers/APZCTreeManagerChild.h"
#include "mozilla/layers/APZChild.h"
#include "mozilla/layers/CanvasChild.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/CompositorManagerChild.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/IAPZCTreeManager.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/layers/PTextureChild.h"
#include "mozilla/layers/SyncObject.h"     // for SyncObjectClient
#include "mozilla/layers/TextureClient.h"  // for TextureClient
#include "mozilla/layers/WebRenderBridgeChild.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/mozalloc.h"  // for operator new, etc
#include "nsDebug.h"           // for NS_WARNING
#include "nsISupportsImpl.h"   // for MOZ_COUNT_CTOR, etc
#include "nsTArray.h"          // for nsTArray, nsTArray_Impl
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"
#if defined(XP_WIN)
#  include "WinUtils.h"
#endif
#include "mozilla/widget/CompositorWidget.h"
#ifdef MOZ_WIDGET_SUPPORTS_OOP_COMPOSITING
#  include "mozilla/widget/CompositorWidgetChild.h"
#endif
#include "VsyncSource.h"

using mozilla::gfx::GPUProcessManager;

namespace mozilla {
namespace layers {

static int sShmemCreationCounter = 0;

static void ResetShmemCounter() { sShmemCreationCounter = 0; }

static void ShmemAllocated(CompositorBridgeChild* aProtocol) {
  sShmemCreationCounter++;
  if (sShmemCreationCounter > 256) {
    aProtocol->SendSyncWithCompositor();
    ResetShmemCounter();
    MOZ_PERFORMANCE_WARNING(
        "gfx", "The number of shmem allocations is too damn high!");
  }
}

static StaticRefPtr<CompositorBridgeChild> sCompositorBridge;

Atomic<int32_t> KnowsCompositor::sSerialCounter(0);

CompositorBridgeChild::CompositorBridgeChild(CompositorManagerChild* aManager)
    : mCompositorManager(aManager),
      mIdNamespace(0),
      mResourceId(0),
      mCanSend(false),
      mActorDestroyed(false),
      mPaused(false),
      mForceSyncFlushRendering(false),
      mThread(NS_GetCurrentThread()),
      mProcessToken(0),
      mSectionAllocator(nullptr) {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
}

CompositorBridgeChild::~CompositorBridgeChild() {
  if (mCanSend) {
    gfxCriticalError() << "CompositorBridgeChild was not deinitialized";
  }
}

bool CompositorBridgeChild::IsSameProcess() const {
  return OtherChildID() == XRE_GetChildID();
}

void CompositorBridgeChild::PrepareFinalDestroy() {
  // Because of medium high priority DidComposite, we need to repost to
  // medium high priority queue to ensure the actor is destroyed after possible
  // pending DidComposite message.
  nsCOMPtr<nsIRunnable> runnable =
      NewRunnableMethod("CompositorBridgeChild::AfterDestroy", this,
                        &CompositorBridgeChild::AfterDestroy);
  NS_DispatchToCurrentThreadQueue(runnable.forget(),
                                  EventQueuePriority::MediumHigh);
}

void CompositorBridgeChild::AfterDestroy() {
  // Note that we cannot rely upon mCanSend here because we already set that to
  // false to prevent normal IPDL calls from being made after SendWillClose.
  // The only time we should not issue Send__delete__ is if the actor is already
  // destroyed, e.g. the compositor process crashed.
  if (!mActorDestroyed) {
    // We saw this send fail quite often with "Channel closing", probably a race
    // with the other side closing or some event scheduling order.
    if (GetIPCChannel()->CanSend()) {
      Send__delete__(this);
    }
    mActorDestroyed = true;
  }

  if (sCompositorBridge == this) {
    sCompositorBridge = nullptr;
  }
}

void CompositorBridgeChild::Destroy() {
  // This must not be called from the destructor!
  mTexturesWaitingNotifyNotUsed.clear();

  // Destroying the layer manager may cause all sorts of things to happen, so
  // let's make sure there is still a reference to keep this alive whatever
  // happens.
  RefPtr<CompositorBridgeChild> selfRef = this;

  if (mSectionAllocator) {
    delete mSectionAllocator;
    mSectionAllocator = nullptr;
  }

  if (mLayerManager) {
    mLayerManager->Destroy();
    mLayerManager = nullptr;
  }

  if (!mCanSend) {
    // We may have already called destroy but still have lingering references
    // or CompositorBridgeChild::ActorDestroy was called. Ensure that we do our
    // post destroy clean up no matter what. It is safe to call multiple times.
    NS_GetCurrentThread()->Dispatch(
        NewRunnableMethod("CompositorBridgeChild::PrepareFinalDestroy", selfRef,
                          &CompositorBridgeChild::PrepareFinalDestroy));
    return;
  }

  AutoTArray<PWebRenderBridgeChild*, 16> wrBridges;
  ManagedPWebRenderBridgeChild(wrBridges);
  for (int i = wrBridges.Length() - 1; i >= 0; --i) {
    RefPtr<WebRenderBridgeChild> wrBridge =
        static_cast<WebRenderBridgeChild*>(wrBridges[i]);
    wrBridge->Destroy(/* aIsSync */ false);
  }

  AutoTArray<PAPZChild*, 16> apzChildren;
  ManagedPAPZChild(apzChildren);
  for (PAPZChild* child : apzChildren) {
    (void)child->SendDestroy();
  }

  const ManagedContainer<PTextureChild>& textures = ManagedPTextureChild();
  for (const auto& key : textures) {
    RefPtr<TextureClient> texture = TextureClient::AsTextureClient(key);

    if (texture) {
      texture->Destroy();
    }
  }

  // The WillClose message is synchronous, so we know that after it returns
  // any messages sent by the above code will have been processed on the
  // other side.
  SendWillClose();
  mCanSend = false;

  // We no longer care about unexpected shutdowns, in the remote process case.
  mProcessToken = 0;

  // The call just made to SendWillClose can result in IPC from the
  // CompositorBridgeParent to the CompositorBridgeChild (e.g. caused by the
  // destruction of shared memory). We need to ensure this gets processed by the
  // CompositorBridgeChild before it gets destroyed. It suffices to ensure that
  // events already in the thread get processed before the
  // CompositorBridgeChild is destroyed, so we add a task to the thread to
  // handle compositor destruction.

  // From now on we can't send any message message.
  NS_GetCurrentThread()->Dispatch(
      NewRunnableMethod("CompositorBridgeChild::PrepareFinalDestroy", selfRef,
                        &CompositorBridgeChild::PrepareFinalDestroy));
}

// static
void CompositorBridgeChild::ShutDown() {
  if (sCompositorBridge) {
    sCompositorBridge->Destroy();
    SpinEventLoopUntil("CompositorBridgeChild::ShutDown"_ns,
                       [&]() { return !sCompositorBridge; });
  }
}

void CompositorBridgeChild::InitForContent(uint32_t aNamespace) {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(aNamespace);

  if (RefPtr<CompositorBridgeChild> old = sCompositorBridge.forget()) {
    // Note that at this point, ActorDestroy may not have been called yet,
    // meaning mCanSend is still true. In this case we will try to send a
    // synchronous WillClose message to the parent, and will certainly get
    // a false result and a MsgDropped processing error. This is okay.
    old->Destroy();
  }

  mCanSend = true;
  mIdNamespace = aNamespace;

  sCompositorBridge = this;
}

void CompositorBridgeChild::InitForWidget(uint64_t aProcessToken,
                                          uint32_t aNamespace) {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(aProcessToken);
  MOZ_RELEASE_ASSERT(aNamespace);

  mCanSend = true;
  mProcessToken = aProcessToken;
  mIdNamespace = aNamespace;
}

RefPtr<WebRenderLayerManager> CompositorBridgeChild::CreateLayerManager(
    nsIWidget* aWidget, wr::PipelineId aPipelineId, nsCString& aError) {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess(),
                     "Must only be called for widget CompositorBridgeChild");
  MOZ_RELEASE_ASSERT(!mLayerManager, "Must only be called once");

  mLayerManager =
      WebRenderLayerManager::Create(aWidget, this, aPipelineId, aError);
  return mLayerManager;
}

/*static*/
CompositorBridgeChild* CompositorBridgeChild::Get() {
  // This is only expected to be used in child processes. While the parent
  // process does have CompositorBridgeChild instances, it has _multiple_ (one
  // per window), and therefore there is no global singleton available.
  MOZ_RELEASE_ASSERT(!XRE_IsParentProcess());
  return sCompositorBridge;
}

/* static */
bool CompositorBridgeChild::CompositorIsInGPUProcess() {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  if (XRE_IsParentProcess()) {
    return !!GPUProcessManager::Get()->GetGPUChild();
  }

  MOZ_RELEASE_ASSERT(XRE_IsContentProcess());
  CompositorBridgeChild* bridge = CompositorBridgeChild::Get();
  if (!bridge) {
    return false;
  }

  return bridge->OtherChildID() !=
         dom::ContentChild::GetSingleton()->OtherChildID();
}

mozilla::ipc::IPCResult CompositorBridgeChild::RecvDidComposite(
    const LayersId& aId, const nsTArray<TransactionId>& aTransactionIds,
    const TimeStamp& aCompositeStart, const TimeStamp& aCompositeEnd) {
  for (const auto& id : aTransactionIds) {
    if (mLayerManager) {
      MOZ_RELEASE_ASSERT(!aId.IsValid());
      MOZ_RELEASE_ASSERT(mLayerManager->GetBackendType() ==
                         LayersBackend::LAYERS_WR);
      // Hold a reference to keep LayerManager alive. See Bug 1242668.
      RefPtr<WebRenderLayerManager> m = mLayerManager;
      m->DidComposite(id, aCompositeStart, aCompositeEnd);
    } else if (aId.IsValid()) {
      RefPtr<dom::BrowserChild> child = dom::BrowserChild::GetFrom(aId);
      if (child) {
        child->DidComposite(id, aCompositeStart, aCompositeEnd);
      }
    }
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorBridgeChild::RecvNotifyFrameStats(
    nsTArray<FrameStats>&& aFrameStats) {
  gfxPlatform::GetPlatform()->NotifyFrameStats(std::move(aFrameStats));
  return IPC_OK();
}

void CompositorBridgeChild::ActorDestroy(ActorDestroyReason aWhy) {
  if (aWhy == AbnormalShutdown) {
    // If the parent side runs into a problem then the actor will be destroyed.
    // There is nothing we can do in the child side, here sets mCanSend as
    // false.
    gfxCriticalNote << "CompositorBridgeChild receives IPC close with "
                       "reason=AbnormalShutdown";
  }

  mCanSend = false;
  mActorDestroyed = true;

  if (mProcessToken && XRE_IsParentProcess()) {
    GPUProcessManager::Get()->NotifyRemoteActorDestroyed(mProcessToken);
  }
}

bool CompositorBridgeChild::SendWillClose() {
  MOZ_RELEASE_ASSERT(mCanSend);
  return PCompositorBridgeChild::SendWillClose();
}

bool CompositorBridgeChild::SendPause() {
  if (!mCanSend) {
    return false;
  }
  mPaused = true;
  return PCompositorBridgeChild::SendPause();
}

bool CompositorBridgeChild::SendResume() {
  if (!mCanSend) {
    return false;
  }
  mPaused = false;
  return PCompositorBridgeChild::SendResume();
}

bool CompositorBridgeChild::SendResumeAsync() {
  if (!mCanSend) {
    return false;
  }
  mPaused = false;
  return PCompositorBridgeChild::SendResumeAsync();
}

bool CompositorBridgeChild::SendAdoptChild(const LayersId& id,
                                           const LayersId& embedderId) {
  if (!mCanSend) {
    return false;
  }
  return PCompositorBridgeChild::SendAdoptChild(id, embedderId);
}

bool CompositorBridgeChild::SendFlushRendering(
    const wr::RenderReasons& aReasons) {
  if (!mCanSend) {
    return false;
  }
  return PCompositorBridgeChild::SendFlushRendering(aReasons);
}

bool CompositorBridgeChild::SendFlushRenderingAsync(
    const wr::RenderReasons& aReasons) {
  if (mForceSyncFlushRendering) {
    return SendFlushRendering(aReasons);
  }
  if (!mCanSend) {
    return false;
  }
  return PCompositorBridgeChild::SendFlushRenderingAsync(aReasons);
}

void CompositorBridgeChild::SetForceSyncFlushRendering(
    bool aForceSyncFlushRendering) {
  mForceSyncFlushRendering = aForceSyncFlushRendering;
}

bool CompositorBridgeChild::SendStartFrameTimeRecording(
    const uint32_t& bufferSize, uint32_t* startIndex) {
  if (!mCanSend) {
    return false;
  }
  return PCompositorBridgeChild::SendStartFrameTimeRecording(bufferSize,
                                                             startIndex);
}

bool CompositorBridgeChild::SendStopFrameTimeRecording(
    const uint32_t& startIndex, nsTArray<float>* intervals) {
  if (!mCanSend) {
    return false;
  }
  return PCompositorBridgeChild::SendStopFrameTimeRecording(startIndex,
                                                            intervals);
}

mozilla::ipc::IPCResult CompositorBridgeChild::RecvParentAsyncMessages(
    nsTArray<AsyncParentMessageData>&& aMessages) {
  for (AsyncParentMessageArray::index_type i = 0; i < aMessages.Length(); ++i) {
    const AsyncParentMessageData& message = aMessages[i];

    switch (message.type()) {
      case AsyncParentMessageData::TOpNotifyNotUsed: {
        const OpNotifyNotUsed& op = message.get_OpNotifyNotUsed();
        NotifyNotUsed(op.TextureId(), op.fwdTransactionId());
        break;
      }
      default:
        NS_ERROR("unknown AsyncParentMessageData type");
        return IPC_FAIL_NO_REASON(this);
    }
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorBridgeChild::RecvObserveLayersUpdate(
    const LayersId& aLayersId, const bool& aActive) {
  // This message is sent via the window compositor, not the tab compositor -
  // however it still has a layers id.
  MOZ_RELEASE_ASSERT(aLayersId.IsValid());
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());

  if (RefPtr<dom::BrowserParent> tab =
          dom::BrowserParent::GetBrowserParentFromLayersId(aLayersId)) {
    tab->LayerTreeUpdate(aActive);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorBridgeChild::RecvCompositorOptionsChanged(
    const LayersId& aLayersId, const CompositorOptions& aNewOptions) {
  MOZ_RELEASE_ASSERT(aLayersId.IsValid());
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());

  if (RefPtr<dom::BrowserParent> tab =
          dom::BrowserParent::GetBrowserParentFromLayersId(aLayersId)) {
    (void)tab->SendCompositorOptionsChanged(aNewOptions);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorBridgeChild::RecvNotifyJankedAnimations(
    const LayersId& aLayersId, nsTArray<uint64_t>&& aJankedAnimations) {
  if (mLayerManager) {
    MOZ_RELEASE_ASSERT(!aLayersId.IsValid());
    mLayerManager->UpdatePartialPrerenderedAnimations(aJankedAnimations);
  } else if (aLayersId.IsValid()) {
    RefPtr<dom::BrowserChild> child = dom::BrowserChild::GetFrom(aLayersId);
    if (child) {
      child->NotifyJankedAnimations(aJankedAnimations);
    }
  }

  return IPC_OK();
}

void CompositorBridgeChild::HoldUntilCompositableRefReleasedIfNecessary(
    TextureClient* aClient) {
  if (!aClient) {
    return;
  }

  bool waitNotifyNotUsed =
      aClient->GetFlags() & TextureFlags::RECYCLE ||
      aClient->GetFlags() & TextureFlags::WAIT_HOST_USAGE_END;
  if (!waitNotifyNotUsed) {
    return;
  }

  aClient->SetLastFwdTransactionId(
      GetFwdTransactionCounter().mFwdTransactionId);
  mTexturesWaitingNotifyNotUsed.emplace(aClient->GetSerial(), aClient);
}

void CompositorBridgeChild::NotifyNotUsed(uint64_t aTextureId,
                                          uint64_t aFwdTransactionId) {
  auto it = mTexturesWaitingNotifyNotUsed.find(aTextureId);
  if (it != mTexturesWaitingNotifyNotUsed.end()) {
    if (aFwdTransactionId < it->second->GetLastFwdTransactionId()) {
      // Released on host side, but client already requested newer use texture.
      return;
    }
    mTexturesWaitingNotifyNotUsed.erase(it);
  }
}

void CompositorBridgeChild::CancelWaitForNotifyNotUsed(uint64_t aTextureId) {
  mTexturesWaitingNotifyNotUsed.erase(aTextureId);
}

FixedSizeSmallShmemSectionAllocator*
CompositorBridgeChild::GetTileLockAllocator() {
  if (!IPCOpen()) {
    return nullptr;
  }

  if (!mSectionAllocator) {
    mSectionAllocator = new FixedSizeSmallShmemSectionAllocator(this);
  }
  return mSectionAllocator;
}

already_AddRefed<PTextureChild> CompositorBridgeChild::CreateTexture(
    const SurfaceDescriptor& aSharedData, ReadLockDescriptor&& aReadLock,
    LayersBackend aLayersBackend, TextureFlags aFlags,
    const dom::ContentParentId& aContentId, uint64_t aSerial,
    wr::MaybeExternalImageId& aExternalImageId) {
  RefPtr actor = TextureClient::CreateIPDLActor();
  if (!SendPTextureConstructor(actor, aSharedData, std::move(aReadLock),
                               aLayersBackend, aFlags, aSerial,
                               aExternalImageId)) {
    return nullptr;
  }
  return actor.forget();
}

already_AddRefed<CanvasChild> CompositorBridgeChild::GetCanvasChild() {
  MOZ_RELEASE_ASSERT(gfxPlatform::UseRemoteCanvas());
  if (auto* cm = gfx::CanvasManagerChild::Get()) {
    return cm->GetCanvasChild().forget();
  }
  return nullptr;
}

void CompositorBridgeChild::EndCanvasTransaction() {
  if (auto* cm = gfx::CanvasManagerChild::Get()) {
    cm->EndCanvasTransaction();
  }
}

void CompositorBridgeChild::ClearCachedResources() {
  if (auto* cm = gfx::CanvasManagerChild::Get()) {
    cm->ClearCachedResources();
  }
}

bool CompositorBridgeChild::AllocUnsafeShmem(size_t aSize, ipc::Shmem* aShmem) {
  ShmemAllocated(this);
  return PCompositorBridgeChild::AllocUnsafeShmem(aSize, aShmem);
}

bool CompositorBridgeChild::AllocShmem(size_t aSize, ipc::Shmem* aShmem) {
  ShmemAllocated(this);
  return PCompositorBridgeChild::AllocShmem(aSize, aShmem);
}

bool CompositorBridgeChild::DeallocShmem(ipc::Shmem& aShmem) {
  if (!mCanSend) {
    return false;
  }
  return PCompositorBridgeChild::DeallocShmem(aShmem);
}

already_AddRefed<PAPZCTreeManagerChild>
CompositorBridgeChild::AllocPAPZCTreeManagerChild(const LayersId& aLayersId) {
  return MakeAndAddRef<APZCTreeManagerChild>();
}

already_AddRefed<PAPZChild> CompositorBridgeChild::AllocPAPZChild(
    const LayersId& aLayersId) {
  // We send the constructor manually.
  MOZ_CRASH("Should not be called");
  return nullptr;
}

// -

uint64_t CompositorBridgeChild::GetNextResourceId() {
  ++mResourceId;
  MOZ_RELEASE_ASSERT(mResourceId != UINT32_MAX);

  uint64_t id = mIdNamespace;
  id = (id << 32) | mResourceId;

  return id;
}

wr::MaybeExternalImageId CompositorBridgeChild::GetNextExternalImageId() {
  return Some(wr::ToExternalImageId(GetNextResourceId()));
}

wr::PipelineId CompositorBridgeChild::GetNextPipelineId() {
  return wr::AsPipelineId(GetNextResourceId());
}

FwdTransactionCounter& CompositorBridgeChild::GetFwdTransactionCounter() {
  return mCompositorManager->GetFwdTransactionCounter();
}

}  // namespace layers
}  // namespace mozilla
