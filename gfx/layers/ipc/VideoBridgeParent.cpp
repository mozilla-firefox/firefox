/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VideoBridgeParent.h"

#include "CompositorThread.h"
#include "gfxConfig.h"
#include "gfxPlatform.h"
#include "mozilla/DataMutex.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/PTextureParent.h"
#include "mozilla/layers/SyncObject.h"
#include "mozilla/layers/TextureHost.h"
#include "mozilla/layers/VideoBridgeUtils.h"
#include "mozilla/webrender/RenderThread.h"

#ifdef XP_WIN
#  include "mozilla/gfx/DeviceManagerDx.h"
#endif

namespace mozilla::layers {

using namespace mozilla::ipc;
using namespace mozilla::gfx;

using VideoBridgeTable = EnumeratedArray<VideoBridgeSource, VideoBridgeParent*,
                                         size_t(VideoBridgeSource::_Count)>;

MOZ_RUNINIT static StaticDataMutex<VideoBridgeTable> sVideoBridgeFromProcess(
    "VideoBridges");
static Atomic<bool> sVideoBridgeParentShutDown(false);

VideoBridgeParent::VideoBridgeParent()
    : mMonitor("VideoBridgeParent::mMonitor"),
      mCompositorThreadHolder(CompositorThreadHolder::GetSingleton()),
      mClosed(false) {}

void VideoBridgeParent::RegisterSingleton(VideoBridgeSource aSource) {
  auto videoBridgeFromProcess = sVideoBridgeFromProcess.Lock();
  switch (aSource) {
    case VideoBridgeSource::RddProcess:
    case VideoBridgeSource::GpuProcess:
    case VideoBridgeSource::MFMediaEngineCDMProcess:
      (*videoBridgeFromProcess)[aSource] = this;
      break;
    default:
      MOZ_CRASH("Unhandled case");
  }
}

void VideoBridgeParent::UnregisterSingleton() {
  auto videoBridgeFromProcess = sVideoBridgeFromProcess.Lock();
  for (auto& bridgeParent : *videoBridgeFromProcess) {
    if (bridgeParent == this) {
      bridgeParent = nullptr;
    }
  }
}

VideoBridgeParent::~VideoBridgeParent() { UnregisterSingleton(); }

/* static */
void VideoBridgeParent::Open(Endpoint<PVideoBridgeParent>&& aEndpoint,
                             VideoBridgeSource aSource) {
  RefPtr<VideoBridgeParent> parent = new VideoBridgeParent();
  // Other threads can take a reference from the singleton table at any time,
  // so the bridge must only be published while we hold a strong reference.
  parent->RegisterSingleton(aSource);

  nsIThread* thread = CompositorThread();
  if (!thread || NS_FAILED(thread->Dispatch(
                     NewRunnableMethod<Endpoint<PVideoBridgeParent>&&>(
                         "gfx::layers::VideoBridgeParent::Bind", parent,
                         &VideoBridgeParent::Bind, std::move(aEndpoint))))) {
    parent->UnregisterSingleton();
  }
}

void VideoBridgeParent::Bind(Endpoint<PVideoBridgeParent>&& aEndpoint) {
  if (!aEndpoint.Bind(this)) {
    // We can't recover from this.
    MOZ_CRASH("Failed to bind VideoBridgeParent to endpoint");
  }
}

/* static */
RefPtr<VideoBridgeParent> VideoBridgeParent::GetSingleton(
    const Maybe<VideoBridgeSource>& aSource) {
  MOZ_RELEASE_ASSERT(aSource.isSome());
  auto videoBridgeFromProcess = sVideoBridgeFromProcess.Lock();
  switch (aSource.value()) {
    case VideoBridgeSource::RddProcess:
    case VideoBridgeSource::GpuProcess:
    case VideoBridgeSource::MFMediaEngineCDMProcess:
      return RefPtr{(*videoBridgeFromProcess)[aSource.value()]};
    default:
      MOZ_CRASH("Unhandled case");
  }
}

already_AddRefed<TextureHost> VideoBridgeParent::LookupTextureAsync(
    const dom::ContentParentId& aContentId, uint64_t aSerial) {
  MonitorAutoLock lock(mMonitor);

  // We raced shutting down the actor. This can happen when another thread is
  // keeping the VideoBridgeParent object alive, by waiting for a texture lookup
  // to complete. While the lookup should fail quickly, it may not release the
  // actor in time to have removed it from the singleton array.
  if (NS_WARN_IF(!mCompositorThreadHolder)) {
    return nullptr;
  }

  MOZ_RELEASE_ASSERT(mCompositorThreadHolder->IsInThread());

  const auto i = mTextureMap.find(aSerial);
  if (NS_WARN_IF(i == mTextureMap.end())) {
    return nullptr;
  }

  if (NS_WARN_IF(aContentId != i->second.mContentId)) {
    return nullptr;
  }

  return do_AddRef(i->second.mTextureHost);
}

already_AddRefed<TextureHost> VideoBridgeParent::LookupTexture(
    const dom::ContentParentId& aContentId, uint64_t aSerial) {
  MonitorAutoLock lock(mMonitor);

  // We raced shutting down the actor.
  if (NS_WARN_IF(!mCompositorThreadHolder)) {
    return nullptr;
  }

  auto i = mTextureMap.find(aSerial);
  if (i != mTextureMap.end()) {
    if (NS_WARN_IF(aContentId != i->second.mContentId)) {
      return nullptr;
    }
    return do_AddRef(i->second.mTextureHost);
  }

  // We cannot block on the Compositor thread because that is the thread we get
  // the IPC calls for the update on.
  if (NS_WARN_IF(mCompositorThreadHolder->IsInThread())) {
    MOZ_ASSERT_UNREACHABLE("Should never call on Compositor thread!");
    return nullptr;
  }

  // Canvas may have raced ahead of VideoBridgeParent setting up the
  // PTextureParent IPDL object. This should happen only rarely/briefly. Since
  // we know that the PTexture constructor must be in the send queue, we can
  // block until the IPDL ping comes back.
  bool complete = false;

  auto resolve = [&](void_t&&) {
    MonitorAutoLock lock(mMonitor);
    complete = true;
    lock.NotifyAll();
  };

  auto reject = [&](ipc::ResponseRejectReason) {
    MonitorAutoLock lock(mMonitor);
    complete = true;
    lock.NotifyAll();
  };

  mCompositorThreadHolder->Dispatch(
      NS_NewRunnableFunction("VideoBridgeParent::LookupTexture", [&]() {
        if (CanSend()) {
          SendPing(std::move(resolve), std::move(reject));
        } else {
          reject(ipc::ResponseRejectReason::ChannelClosed);
        }
      }));

  while (!complete) {
    lock.Wait();
  }

  i = mTextureMap.find(aSerial);
  if (NS_WARN_IF(i == mTextureMap.end())) {
    return nullptr;
  }

  if (NS_WARN_IF(aContentId != i->second.mContentId)) {
    return nullptr;
  }

  return do_AddRef(i->second.mTextureHost);
}

void VideoBridgeParent::ActorDestroy(ActorDestroyReason aWhy) {
  bool shutdown = sVideoBridgeParentShutDown;

  if (!shutdown && aWhy == AbnormalShutdown) {
    gfxCriticalNote
        << "VideoBridgeParent receives IPC close with reason=AbnormalShutdown";
  }

  {
    MonitorAutoLock lock(mMonitor);
    // Can't alloc/dealloc shmems from now on.
    mClosed = true;
    mCompositorThreadHolder = nullptr;
  }

  UnregisterSingleton();
}

/* static */
void VideoBridgeParent::Shutdown() {
  CompositorThread()->Dispatch(NS_NewRunnableFunction(
      "VideoBridgeParent::Shutdown",
      []() -> void { VideoBridgeParent::ShutdownInternal(); }));
}

/* static */
void VideoBridgeParent::ShutdownInternal() {
  sVideoBridgeParentShutDown = true;

  nsTArray<RefPtr<VideoBridgeParent>> bridges;

  // We don't want to hold the sVideoBridgeFromProcess lock when the
  // VideoBridgeParent objects are closed without holding a reference to them.
  {
    auto videoBridgeFromProcess = sVideoBridgeFromProcess.Lock();
    for (auto& bridgeParent : *videoBridgeFromProcess) {
      if (bridgeParent) {
        bridges.AppendElement(bridgeParent);
      }
    }
  }

  for (auto& bridge : bridges) {
    bridge->Close();
  }
}

/* static */
void VideoBridgeParent::UnregisterExternalImages() {
  MOZ_RELEASE_ASSERT(sVideoBridgeParentShutDown);

  auto videoBridgeFromProcess = sVideoBridgeFromProcess.Lock();
  for (auto& bridgeParent : *videoBridgeFromProcess) {
    if (bridgeParent) {
      bridgeParent->DoUnregisterExternalImages();
    }
  }
}

void VideoBridgeParent::DoUnregisterExternalImages() {
  const ManagedContainer<PTextureParent>& textures = ManagedPTextureParent();
  for (const auto& key : textures) {
    RefPtr<TextureHost> texture = TextureHost::AsTextureHost(key);

    if (texture) {
      texture->MaybeDestroyRenderTexture();
    }
  }
}

already_AddRefed<PTextureParent> VideoBridgeParent::AllocPTextureParent(
    const SurfaceDescriptor& aSharedData, ReadLockDescriptor& aReadLock,
    const LayersBackend& aLayersBackend, const TextureFlags& aFlags,
    const dom::ContentParentId& aContentId, const uint64_t& aSerial) {
  RefPtr<PTextureParent> parent = TextureHost::CreateIPDLActor(
      this, aSharedData, std::move(aReadLock), aLayersBackend, aFlags,
      aContentId, aSerial, Nothing());

  if (!parent) {
    return nullptr;
  }

  if (aFlags & TextureFlags::REMOTE_TEXTURE) {
    MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
    return nullptr;
  }

  MonitorAutoLock lock(mMonitor);
  mTextureMap.insert(
      {aSerial, {TextureHost::AsTextureHost(parent), aContentId}});
  return parent.forget();
}

// Describes the compositors of this process, independently of any widget.
// This mirrors the choices made by wr::RenderCompositor::Create for a widget
// that allows native compositing.
static TextureFactoryIdentifier GetProcessTextureFactoryIdentifier() {
  const bool software = gfxVars::UseSoftwareWebRender();
  WebRenderCompositor compositorType = WebRenderCompositor::DRAW;
  bool useANGLE = false;
  bool useDComp = false;
  bool useLayerCompositor = false;
  bool useCompositorWnd = false;
  bool supportsD3D11NV12 = false;
  SyncHandle syncHandle = {};

  if (software) {
    if (gfxPlatform::IsHeadless()) {
      compositorType = WebRenderCompositor::SOFTWARE;
    } else {
#if defined(XP_WIN)
      // SWGL can only draw D3D11 textures when compositing with D3D11. This
      // mirrors RenderCompositorD3D11SWGL::Create, which clears
      // AllowSoftwareWebRenderD3D11 if it fails.
      compositorType = gfxVars::AllowSoftwareWebRenderD3D11() &&
                               gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)
                           ? WebRenderCompositor::D3D11
                           : WebRenderCompositor::SOFTWARE;
#elif defined(XP_DARWIN)
      compositorType = WebRenderCompositor::DRAW;
#else
#  if defined(MOZ_WAYLAND)
      if (gfxVars::UseWebRenderCompositor()) {
        compositorType = WebRenderCompositor::DRAW;
      } else
#  endif
          if (gfxVars::AllowSoftwareWebRenderOGL()) {
        compositorType = WebRenderCompositor::OPENGL;
      } else {
        compositorType = WebRenderCompositor::SOFTWARE;
      }
#endif
    }
  } else {
#ifdef XP_WIN
    useANGLE = gfxVars::UseWebRenderANGLE();
    if (useANGLE) {
      useDComp = gfxVars::UseWebRenderDCompWin();
      useLayerCompositor = useDComp && gfxVars::UseWebRenderCompositor();
      if (useDComp) {
        compositorType = WebRenderCompositor::DIRECT_COMPOSITION;
      }
    }
#endif
  }

#ifdef XP_WIN
  useCompositorWnd = XRE_IsGPUProcess() && !gfxPlatform::IsHeadless();
  if (DeviceManagerDx* dm = DeviceManagerDx::Get()) {
    supportsD3D11NV12 = dm->CanUseNV12();
    if (useANGLE || compositorType == WebRenderCompositor::D3D11) {
      // Both ANGLE and D3D11 SWGL share the sync object of the compositor
      // device.
      RefPtr<ID3D11Device> device;
      RefPtr<SyncObjectHost> syncObject;
      dm->GetCompositorDeviceAndSyncObject(&device, &syncObject);
      if (syncObject) {
        syncHandle = syncObject->GetSyncHandle();
      }
    }
  }
#endif

  return TextureFactoryIdentifier(
      software ? WebRenderBackend::SOFTWARE : WebRenderBackend::HARDWARE,
      compositorType, XRE_GetProcessType(), /* aMaxTextureSize */ 4096,
      useANGLE, useDComp, useLayerCompositor, useCompositorWnd, false, false,
      false, supportsD3D11NV12, syncHandle);
}

mozilla::ipc::IPCResult VideoBridgeParent::RecvGetTextureFactoryIdentifier(
    GetTextureFactoryIdentifierResolver&& aResolver) {
  aResolver(GetProcessTextureFactoryIdentifier());
  return IPC_OK();
}

void VideoBridgeParent::RemoveTexture(uint64_t aSerial) {
  RefPtr<TextureHost> textureHost;
  {
    MonitorAutoLock lock(mMonitor);
    auto i = mTextureMap.find(aSerial);
    if (i != mTextureMap.end()) {
      textureHost = std::move(i->second.mTextureHost);
      mTextureMap.erase(i);
    }
  }
}

void VideoBridgeParent::SendAsyncMessage(
    Span<const AsyncParentMessageData> aMessage) {
  MOZ_ASSERT_UNREACHABLE("AsyncMessages not supported");
}

bool VideoBridgeParent::AllocShmem(size_t aSize, ipc::Shmem* aShmem) {
  {
    MonitorAutoLock lock(mMonitor);
    if (mClosed) {
      return false;
    }
  }
  return PVideoBridgeParent::AllocShmem(aSize, aShmem);
}

bool VideoBridgeParent::AllocUnsafeShmem(size_t aSize, ipc::Shmem* aShmem) {
  {
    MonitorAutoLock lock(mMonitor);
    if (mClosed) {
      return false;
    }
  }
  return PVideoBridgeParent::AllocUnsafeShmem(aSize, aShmem);
}

bool VideoBridgeParent::DeallocShmem(ipc::Shmem& aShmem) {
  {
    MonitorAutoLock lock(mMonitor);
    if (mCompositorThreadHolder && !mCompositorThreadHolder->IsInThread()) {
      mCompositorThreadHolder->Dispatch(NS_NewRunnableFunction(
          "gfx::layers::VideoBridgeParent::DeallocShmem",
          [self = RefPtr{this}, shmem = std::move(aShmem)]() mutable {
            self->DeallocShmem(shmem);
          }));
      return true;
    }

    if (mClosed) {
      return false;
    }
  }

  return PVideoBridgeParent::DeallocShmem(aShmem);
}

bool VideoBridgeParent::IsSameProcess() const {
  return OtherPid() == base::GetCurrentProcId();
}

void VideoBridgeParent::NotifyNotUsed(PTextureParent* aTexture,
                                      uint64_t aTransactionId) {}

}  // namespace mozilla::layers
