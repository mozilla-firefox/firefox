/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageBridgeParent.h"

#include <stdint.h>  // for uint64_t, uint32_t

#include "CompositableHost.h"  // for CompositableParent, Create
#include "base/process.h"      // for ProcessId
#include "base/task.h"         // for CancelableTask, DeleteTask, etc
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Hal.h"       // for hal::SetCurrentThreadPriority()
#include "mozilla/HalTypes.h"  // for hal::THREAD_PRIORITY_COMPOSITOR
#include "mozilla/Monitor.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/gfx/Point.h"  // for IntSize
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/MessageChannel.h"  // for MessageChannel, etc
#include "mozilla/layers/BufferTexture.h"
#include "mozilla/layers/CompositableTransactionParent.h"
#include "mozilla/layers/Compositor.h"
#include "mozilla/layers/LayerTreeLog.h"
#include "mozilla/layers/LayersMessages.h"  // for EditReply
#include "mozilla/layers/PImageBridgeParent.h"
#include "mozilla/layers/RemoteTextureMap.h"
#include "mozilla/layers/TextureHost.h"
#include "mozilla/layers/TextureHostOGL.h"  // for TextureHostOGL
#include "mozilla/media/MediaSystemResourceManagerParent.h"  // for MediaSystemResourceManagerParent
#include "mozilla/mozalloc.h"        // for operator new, etc
#include "nsDebug.h"                 // for NS_ASSERTION, etc
#include "nsISupportsImpl.h"         // for ImageBridgeParent::Release, etc
#include "nsTArray.h"                // for nsTArray, nsTArray_Impl
#include "nsTArrayForwardDeclare.h"  // for nsTArray
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"  // for XRE_GetAsyncIOEventTarget

#if defined(XP_WIN)
#  include "mozilla/layers/TextureD3D11.h"
#endif

namespace mozilla {
namespace layers {

using namespace mozilla::ipc;
using namespace mozilla::gfx;
using namespace mozilla::media;

MOZ_RUNINIT ImageBridgeParent::ImageBridgeMap ImageBridgeParent::sImageBridges;

StaticAutoPtr<mozilla::Monitor> sImageBridgesLock;

static StaticRefPtr<ImageBridgeParent> sImageBridgeParentSingleton;

/* static */
void ImageBridgeParent::Setup() {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  if (!sImageBridgesLock) {
    sImageBridgesLock = new Monitor("ImageBridges");
    mozilla::ClearOnShutdown(&sImageBridgesLock);
  }
}

ImageBridgeParent::ImageBridgeParent(nsISerialEventTarget* aThread,
                                     EndpointProcInfo aChildProcessInfo,
                                     dom::ContentParentId aContentId,
                                     uint32_t aNamespace)
    : mThread(aThread),
      mContentId(aContentId),
      mNamespace(aNamespace),
      mClosed(false),
      mCompositorThreadHolder(CompositorThreadHolder::GetSingleton()) {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  SetOtherEndpointProcInfo(aChildProcessInfo);
  mRemoteTextureTxnScheduler = RemoteTextureTxnScheduler::Create(this);
}

ImageBridgeParent::~ImageBridgeParent() = default;

/* static */
ImageBridgeParent* ImageBridgeParent::CreateSameProcess(uint32_t aNamespace) {
  EndpointProcInfo procInfo = EndpointProcInfo::Current();
  RefPtr<ImageBridgeParent> parent = new ImageBridgeParent(
      CompositorThread(), procInfo, dom::ContentParentId(), aNamespace);

  {
    MonitorAutoLock lock(*sImageBridgesLock);
    MOZ_RELEASE_ASSERT(sImageBridges.count(procInfo.mPid) == 0);
    sImageBridges[procInfo.mPid] = parent;
  }

  sImageBridgeParentSingleton = parent;
  return parent;
}

/* static */
bool ImageBridgeParent::CreateForGPUProcess(
    Endpoint<PImageBridgeParent>&& aEndpoint, uint32_t aNamespace) {
  MOZ_RELEASE_ASSERT(XRE_GetProcessType() == GeckoProcessType_GPU);

  nsCOMPtr<nsISerialEventTarget> compositorThread = CompositorThread();
  if (!compositorThread) {
    return false;
  }

  RefPtr<ImageBridgeParent> parent =
      new ImageBridgeParent(compositorThread, aEndpoint.OtherEndpointProcInfo(),
                            dom::ContentParentId(), aNamespace);

  compositorThread->Dispatch(NewRunnableMethod<Endpoint<PImageBridgeParent>&&>(
      "layers::ImageBridgeParent::Bind", parent, &ImageBridgeParent::Bind,
      std::move(aEndpoint)));

  sImageBridgeParentSingleton = parent;
  return true;
}

/* static */
void ImageBridgeParent::ShutdownInternal() {
  // We make a copy because we don't want to hold the lock while closing and we
  // don't want the object to get freed underneath us.
  nsTArray<RefPtr<ImageBridgeParent>> actors;
  {
    MonitorAutoLock lock(*sImageBridgesLock);
    for (const auto& iter : sImageBridges) {
      actors.AppendElement(iter.second);
    }
  }

  for (auto const& actor : actors) {
    MOZ_RELEASE_ASSERT(!actor->mClosed);
    actor->Close();
  }

  sImageBridgeParentSingleton = nullptr;
}

/* static */
void ImageBridgeParent::Shutdown() {
  CompositorThread()->Dispatch(NS_NewRunnableFunction(
      "ImageBridgeParent::Shutdown",
      []() -> void { ImageBridgeParent::ShutdownInternal(); }));
}

void ImageBridgeParent::ActorDestroy(ActorDestroyReason aWhy) {
  LAYERTREE_LOG("Image bridge namespace {} destroyed, reason {}", mNamespace,
                fmt::enums::format_as(aWhy));
  // Can't alloc/dealloc shmems from now on.
  mClosed = true;

  if (mRemoteTextureTxnScheduler) {
    mRemoteTextureTxnScheduler = nullptr;
  }
  for (const auto& entry : mCompositables) {
    entry.second->OnReleased();
  }
  mCompositables.clear();
  {
    MonitorAutoLock lock(*sImageBridgesLock);
    sImageBridges.erase(OtherPid());
  }
  GetThread()->Dispatch(
      NewRunnableMethod("layers::ImageBridgeParent::DeferredDestroy", this,
                        &ImageBridgeParent::DeferredDestroy));

  // It is very important that this method gets called at shutdown (be it a
  // clean or an abnormal shutdown), because DeferredDestroy is what clears
  // mCompositorThreadHolder. If mCompositorThreadHolder is not null and
  // ActorDestroy is not called, the ImageBridgeParent is leaked which causes
  // the CompositorThreadHolder to be leaked and CompsoitorParent's shutdown
  // ends up spinning the event loop forever, waiting for the compositor thread
  // to terminate.
}

class MOZ_STACK_CLASS AutoImageBridgeParentAsyncMessageSender final {
 public:
  explicit AutoImageBridgeParentAsyncMessageSender(
      ImageBridgeParent* aImageBridge,
      nsTArray<OpDestroy>* aToDestroy = nullptr)
      : mImageBridge(aImageBridge), mToDestroy(aToDestroy) {
    mImageBridge->SetAboutToSendAsyncMessages();
  }

  ~AutoImageBridgeParentAsyncMessageSender() {
    mImageBridge->SendPendingAsyncMessages();
    mImageBridge->ClearAboutToSendAsyncMessages();
    if (mToDestroy) {
      mImageBridge->DestroyActors(*mToDestroy);
    }
  }

 private:
  ImageBridgeParent* mImageBridge;
  nsTArray<OpDestroy>* mToDestroy;
};

mozilla::ipc::IPCResult ImageBridgeParent::RecvUpdate(
    EditArray&& aEdits, OpDestroyArray&& aToDestroy,
    const uint64_t& aFwdTransactionId) {
  AUTO_PROFILER_MARKER("ImageBridgeTransaction", GRAPHICS);
  AUTO_PROFILER_LABEL("ImageBridgeParent::RecvUpdate", GRAPHICS);

  // This ensures that destroy operations are always processed. It is not safe
  // to early-return from RecvUpdate without doing so.
  AutoImageBridgeParentAsyncMessageSender autoAsyncMessageSender(this,
                                                                 &aToDestroy);
  UpdateFwdTransactionId(aFwdTransactionId);

  auto result = IPC_OK();

  for (const auto& edit : aEdits) {
    RefPtr<CompositableHost> compositable =
        FindCompositable(edit.compositable());
    if (!compositable ||
        !ReceiveCompositableUpdate(edit.detail(), WrapNotNull(compositable),
                                   edit.compositable())) {
      result = IPC_FAIL_NO_REASON(this);
      break;
    }
    uint32_t dropped = compositable->GetDroppedFrames();
    if (dropped) {
      (void)SendReportFramesDropped(edit.compositable(), dropped);
    }
  }

  if (mRemoteTextureTxnScheduler) {
    mRemoteTextureTxnScheduler->NotifyTxn(aFwdTransactionId);
  }

  return result;
}

/* static */
bool ImageBridgeParent::CreateForContent(
    Endpoint<PImageBridgeParent>&& aEndpoint, dom::ContentParentId aContentId,
    uint32_t aNamespace) {
  nsCOMPtr<nsISerialEventTarget> compositorThread = CompositorThread();
  if (!compositorThread) {
    return false;
  }

  RefPtr<ImageBridgeParent> bridge =
      new ImageBridgeParent(compositorThread, aEndpoint.OtherEndpointProcInfo(),
                            aContentId, aNamespace);
  compositorThread->Dispatch(NewRunnableMethod<Endpoint<PImageBridgeParent>&&>(
      "layers::ImageBridgeParent::Bind", bridge, &ImageBridgeParent::Bind,
      std::move(aEndpoint)));

  return true;
}

void ImageBridgeParent::Bind(Endpoint<PImageBridgeParent>&& aEndpoint) {
  if (!aEndpoint.Bind(this)) return;

  // If the child process ID was reused by the OS before the ImageBridgeParent
  // object was destroyed, we need to clean it up first.
  RefPtr<ImageBridgeParent> oldActor;
  {
    MonitorAutoLock lock(*sImageBridgesLock);
    ImageBridgeMap::const_iterator i = sImageBridges.find(OtherPid());
    if (i != sImageBridges.end()) {
      oldActor = i->second;
    }
  }

  // We can't hold the lock during Close because it erases itself from the map.
  if (oldActor) {
    MOZ_RELEASE_ASSERT(!oldActor->mClosed);
    oldActor->Close();
  }

  {
    MonitorAutoLock lock(*sImageBridgesLock);
    sImageBridges[OtherPid()] = this;
  }
  LAYERTREE_LOG("Image bridge bound, namespace {}, content {}, pid {}",
                mNamespace, mContentId, OtherPid());
}

mozilla::ipc::IPCResult ImageBridgeParent::RecvWillClose() {
  // If there is any texture still alive we have to force it to deallocate the
  // device data (GL textures, etc.) now because shortly after SenStop() returns
  // on the child side the widget will be destroyed along with it's associated
  // GL context.
  nsTArray<PTextureParent*> textures;
  ManagedPTextureParent(textures);
  for (unsigned int i = 0; i < textures.Length(); ++i) {
    RefPtr<TextureHost> tex = TextureHost::AsTextureHost(textures[i]);
    tex->DeallocateDeviceData();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ImageBridgeParent::RecvNewCompositable(
    const CompositableHandle& aHandle, const TextureInfo& aInfo) {
  RefPtr<CompositableHost> host = AddCompositable(aHandle, aInfo);
  if (!host) {
    return IPC_FAIL_NO_REASON(this);
  }

  host->SetAsyncRef(AsyncCompositableRef(OtherPid(), aHandle));
  return IPC_OK();
}

mozilla::ipc::IPCResult ImageBridgeParent::RecvReleaseCompositable(
    const CompositableHandle& aHandle) {
  ReleaseCompositable(aHandle);
  return IPC_OK();
}

already_AddRefed<PTextureParent> ImageBridgeParent::AllocPTextureParent(
    const SurfaceDescriptor& aSharedData, ReadLockDescriptor& aReadLock,
    const LayersBackend& aLayersBackend, const TextureFlags& aFlags,
    const uint64_t& aSerial, const wr::MaybeExternalImageId& aExternalImageId) {
  if (aFlags & TextureFlags::REMOTE_TEXTURE) {
    MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
    return nullptr;
  }

  switch (aSharedData.type()) {
    case SurfaceDescriptor::TSurfaceDescriptorBuffer:
      break;
    case SurfaceDescriptor::TSurfaceDescriptorGPUVideo:
      break;
    case SurfaceDescriptor::TSurfaceDescriptorRemoteTexture:
      break;
#ifdef MOZ_WIDGET_ANDROID
    case SurfaceDescriptor::TSurfaceTextureDescriptor:
      break;
#elif defined(XP_MACOSX)
    case SurfaceDescriptor::TSurfaceDescriptorMacIOSurface:
      break;
#endif
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
      return nullptr;
  }

  if (aExternalImageId.isSome() &&
      !OwnsExternalImageId(aExternalImageId.ref())) {
    NS_ERROR("We do not own this external image id.");
    return nullptr;
  }
  return TextureHost::CreateIPDLActor(this, aSharedData, std::move(aReadLock),
                                      aLayersBackend, aFlags, mContentId,
                                      aSerial, aExternalImageId);
}

PMediaSystemResourceManagerParent*
ImageBridgeParent::AllocPMediaSystemResourceManagerParent() {
  return new mozilla::media::MediaSystemResourceManagerParent();
}

bool ImageBridgeParent::DeallocPMediaSystemResourceManagerParent(
    PMediaSystemResourceManagerParent* aActor) {
  MOZ_RELEASE_ASSERT(aActor);
  delete static_cast<mozilla::media::MediaSystemResourceManagerParent*>(aActor);
  return true;
}

void ImageBridgeParent::SendAsyncMessage(
    Span<const AsyncParentMessageData> aMessage) {
  (void)SendParentAsyncMessages(aMessage);
}

class ProcessIdComparator {
 public:
  bool Equals(const ImageCompositeNotificationInfo& aA,
              const ImageCompositeNotificationInfo& aB) const {
    return aA.mImageBridgeProcessId == aB.mImageBridgeProcessId;
  }
  bool LessThan(const ImageCompositeNotificationInfo& aA,
                const ImageCompositeNotificationInfo& aB) const {
    return aA.mImageBridgeProcessId < aB.mImageBridgeProcessId;
  }
};

/* static */
bool ImageBridgeParent::NotifyImageComposites(
    nsTArray<ImageCompositeNotificationInfo>& aNotifications) {
  // Group the notifications by destination process ID and then send the
  // notifications in one message per group.
  aNotifications.Sort(ProcessIdComparator());
  uint32_t i = 0;
  bool ok = true;
  while (i < aNotifications.Length()) {
    AutoTArray<ImageCompositeNotification, 1> notifications;
    notifications.AppendElement(aNotifications[i].mNotification);
    uint32_t end = i + 1;
    MOZ_RELEASE_ASSERT(aNotifications[i].mNotification.compositable());
    ProcessId pid = aNotifications[i].mImageBridgeProcessId;
    while (end < aNotifications.Length() &&
           aNotifications[end].mImageBridgeProcessId == pid) {
      notifications.AppendElement(aNotifications[end].mNotification);
      ++end;
    }
    RefPtr<ImageBridgeParent> bridge = GetInstance(pid);
    if (!bridge || bridge->mClosed) {
      i = end;
      continue;
    }
    bridge->SendPendingAsyncMessages();
    if (!bridge->SendDidComposite(notifications)) {
      ok = false;
    }
    i = end;
  }
  return ok;
}

void ImageBridgeParent::DeferredDestroy() { mCompositorThreadHolder = nullptr; }

already_AddRefed<ImageBridgeParent> ImageBridgeParent::GetInstance(
    ProcessId aId) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MonitorAutoLock lock(*sImageBridgesLock);
  ImageBridgeMap::const_iterator i = sImageBridges.find(aId);
  if (i == sImageBridges.end()) {
    NS_WARNING("Cannot find image bridge for process!");
    return nullptr;
  }
  RefPtr<ImageBridgeParent> bridge = i->second;
  return bridge.forget();
}

bool ImageBridgeParent::OwnsExternalImageId(
    const wr::ExternalImageId& aId) const {
  return (mNamespace == static_cast<uint32_t>(wr::AsUint64(aId) >> 32));
}

bool ImageBridgeParent::AllocShmem(size_t aSize, ipc::Shmem* aShmem) {
  if (mClosed) {
    return false;
  }
  return PImageBridgeParent::AllocShmem(aSize, aShmem);
}

bool ImageBridgeParent::AllocUnsafeShmem(size_t aSize, ipc::Shmem* aShmem) {
  if (mClosed) {
    return false;
  }
  return PImageBridgeParent::AllocUnsafeShmem(aSize, aShmem);
}

bool ImageBridgeParent::DeallocShmem(ipc::Shmem& aShmem) {
  if (mClosed) {
    return false;
  }
  return PImageBridgeParent::DeallocShmem(aShmem);
}

bool ImageBridgeParent::IsSameProcess() const {
  return OtherPid() == base::GetCurrentProcId();
}

void ImageBridgeParent::NotifyNotUsed(PTextureParent* aTexture,
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

  if (!IsAboutToSendAsyncMessages()) {
    SendPendingAsyncMessages();
  }
}

}  // namespace layers
}  // namespace mozilla
