/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CanvasManagerChild.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/dom/CanvasRenderingContext2D.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/CanvasShutdownManager.h"
#include "mozilla/gfx/Swizzle.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/layers/ActiveResource.h"
#include "mozilla/layers/CanvasChild.h"
#include "mozilla/layers/CompositorManagerChild.h"
#include "mozilla/webgpu/WebGPUChild.h"

namespace mozilla::gfx {

using namespace layers;

// The IPDL actor holds a strong reference to CanvasManagerChild which we use
// to keep it alive. The owning thread will tell us to close when it is
// shutdown, either via CanvasManagerChild::Shutdown for the main thread, or
// via a shutdown callback from ThreadSafeWorkerRef for worker threads.
MOZ_THREAD_LOCAL(CanvasManagerChild*) CanvasManagerChild::sLocalManager;

Atomic<uint32_t> CanvasManagerChild::sNextId(1);

CanvasManagerChild::CanvasManagerChild(dom::ThreadSafeWorkerRef* aWorkerRef,
                                       uint32_t aId)
    : mWorkerRef(aWorkerRef), mId(aId) {}

CanvasManagerChild::~CanvasManagerChild() = default;

void CanvasManagerChild::ActorDestroy(ActorDestroyReason aReason) {
  DestroyInternal();
  if (sLocalManager.get() == this) {
    sLocalManager.set(nullptr);
  }
  mWorkerRef = nullptr;
}

void CanvasManagerChild::DestroyInternal() {
  if (mCanvasChild) {
    mCanvasChild->Destroy();
    mCanvasChild = nullptr;
  }

  if (auto* shutdownManager = CanvasShutdownManager::Get()) {
    shutdownManager->OnRemoteCanvasLost();
  }

  if (mActiveResourceTracker) {
    mActiveResourceTracker->AgeAllGenerations();
    mActiveResourceTracker.reset();
  }
}

void CanvasManagerChild::Destroy() {
  DestroyInternal();

  // The caller has a strong reference. ActorDestroy will clear sLocalManager
  // and mWorkerRef.
  Close();
}

/* static */ void CanvasManagerChild::Shutdown() {
  if (sLocalManager.init()) {
    RefPtr<CanvasManagerChild> manager = sLocalManager.get();
    if (manager) {
      manager->Destroy();
    }
  }
}

/* static */ bool CanvasManagerChild::CreateParent(
    ipc::Endpoint<PCanvasManagerParent>&& aEndpoint) {
  MOZ_ASSERT(NS_IsMainThread());

  CompositorManagerChild* manager = CompositorManagerChild::GetInstance();
  if (!manager || !manager->CanSend()) {
    return false;
  }

  return manager->SendInitCanvasManager(std::move(aEndpoint));
}

/* static */ CanvasManagerChild* CanvasManagerChild::Get() {
  if (NS_WARN_IF(!sLocalManager.init())) {
    return nullptr;
  }

  CanvasManagerChild* managerWeak = sLocalManager.get();
  if (managerWeak) {
    return managerWeak;
  }

  auto* shutdownManager = CanvasShutdownManager::Get();
  if (NS_WARN_IF(!shutdownManager)) {
    return nullptr;
  }

  // We are only used on the main thread, or on worker threads.
  dom::WorkerPrivate* worker = dom::GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT_IF(!worker, NS_IsMainThread());

  ipc::Endpoint<PCanvasManagerParent> parentEndpoint;
  ipc::Endpoint<PCanvasManagerChild> childEndpoint;

  ipc::EndpointProcInfo compositorInfo =
      CompositorManagerChild::GetCompositorProcInfo();
  if (NS_WARN_IF(compositorInfo == ipc::EndpointProcInfo::Invalid())) {
    return nullptr;
  }

  nsresult rv = PCanvasManager::CreateEndpoints(
      compositorInfo, ipc::EndpointProcInfo::Current(), &parentEndpoint,
      &childEndpoint);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  auto manager = MakeRefPtr<CanvasManagerChild>(shutdownManager->GetWorkerRef(),
                                                sNextId++);
  if (NS_WARN_IF(!childEndpoint.Bind(manager))) {
    return nullptr;
  }

  // We can't talk to the compositor process directly from worker threads, but
  // the main thread can via CompositorManagerChild.
  if (worker) {
    worker->DispatchToMainThread(NS_NewRunnableFunction(
        "CanvasManagerChild::CreateParent",
        [parentEndpoint = std::move(parentEndpoint)]() {
          CreateParent(
              std::move(const_cast<ipc::Endpoint<PCanvasManagerParent>&&>(
                  parentEndpoint)));
        }));
  } else if (NS_WARN_IF(!CreateParent(std::move(parentEndpoint)))) {
    return nullptr;
  }

  manager->SendInitialize(manager->Id());
  shutdownManager->OnRemoteCanvasRestored();
  sLocalManager.set(manager);
  return manager;
}

/* static */ CanvasManagerChild* CanvasManagerChild::MaybeGet() {
  if (!sLocalManager.initialized()) {
    return nullptr;
  }

  return sLocalManager.get();
}

void CanvasManagerChild::EndCanvasTransaction() {
  if (!mCanvasChild) {
    return;
  }

  mCanvasChild->EndTransaction();
  if (mCanvasChild->ShouldBeCleanedUp()) {
    mCanvasChild->Destroy();
    mCanvasChild = nullptr;
  }
}

void CanvasManagerChild::ClearCachedResources() {
  if (mCanvasChild) {
    mCanvasChild->ClearCachedResources();
  }
}

void CanvasManagerChild::DeactivateCanvas() {
  mActive = false;
  if (mCanvasChild) {
    mCanvasChild->Destroy();
    mCanvasChild = nullptr;
  }
}

void CanvasManagerChild::BlockCanvas() { mBlocked = true; }

RefPtr<layers::CanvasChild> CanvasManagerChild::GetCanvasChild() {
  if (mBlocked) {
    return nullptr;
  }

  if (!mActive) {
    MOZ_ASSERT(!mCanvasChild);
    return nullptr;
  }

  if (!mCanvasChild) {
    mCanvasChild = MakeAndAddRef<layers::CanvasChild>(mWorkerRef);
    if (!SendPCanvasConstructor(mCanvasChild)) {
      mCanvasChild->Destroy();
      mCanvasChild = nullptr;
    }
  }

  return mCanvasChild;
}

RefPtr<webgpu::WebGPUChild> CanvasManagerChild::GetWebGPUChild() {
  if (PWebGPUChild* actor = LoneManagedOrNullAsserts(ManagedPWebGPUChild())) {
    return static_cast<webgpu::WebGPUChild*>(actor);
  }

  auto actor = MakeRefPtr<webgpu::WebGPUChild>();
  if (!SendPWebGPUConstructor(actor)) {
    return nullptr;
  }
  return actor;
}

layers::ActiveResourceTracker* CanvasManagerChild::GetActiveResourceTracker() {
  if (!mActiveResourceTracker) {
    mActiveResourceTracker = MakeUnique<ActiveResourceTracker>(
        1000, "CanvasManagerChild"_ns, GetCurrentSerialEventTarget());
  }
  return mActiveResourceTracker.get();
}

already_AddRefed<DataSourceSurface> CanvasManagerChild::GetSnapshot(
    uint32_t aManagerId, ActorId aProtocolId,
    const Maybe<RemoteTextureOwnerId>& aOwnerId,
    const Maybe<RawId>& aCommandEncoderId, const Maybe<RawId>& aCommandBufferId,
    SurfaceFormat aFormat, bool aPremultiply, bool aYFlip) {
  if (!CanSend()) {
    return nullptr;
  }

  webgl::FrontBufferSnapshotIpc res;
  if (!SendGetSnapshot(aManagerId, aProtocolId, aOwnerId, aCommandEncoderId,
                       aCommandBufferId, &res)) {
    return nullptr;
  }

  if (!res.shmem || !res.shmem->IsReadable()) {
    return nullptr;
  }

  auto guard = MakeScopeExit([&] { DeallocShmem(res.shmem.ref()); });

  if (!res.surfSize.x || !res.surfSize.y || res.surfSize.x > INT32_MAX ||
      res.surfSize.y > INT32_MAX) {
    return nullptr;
  }

  IntSize size(res.surfSize.x, res.surfSize.y);
  CheckedInt32 stride = CheckedInt32(res.byteStride);
  if (!stride.isValid()) {
    return nullptr;
  }

  CheckedInt32 length = stride * size.height;
  if (!length.isValid() ||
      size_t(length.value()) != res.shmem->Size<uint8_t>()) {
    return nullptr;
  }

  SurfaceFormat format =
      IsOpaque(aFormat) ? SurfaceFormat::B8G8R8X8 : SurfaceFormat::B8G8R8A8;
  RefPtr<DataSourceSurface> surface =
      Factory::CreateDataSourceSurfaceWithStride(size, format, stride.value(),
                                                 /* aZero */ false);
  if (!surface) {
    return nullptr;
  }

  gfx::DataSourceSurface::ScopedMap map(surface,
                                        gfx::DataSourceSurface::READ_WRITE);
  if (!map.IsMapped()) {
    return nullptr;
  }

  // The buffer we may readback from the canvas could be R8G8B8A8, not
  // premultiplied, and/or has its rows iverted. For the general case, we want
  // surfaces represented as premultiplied B8G8R8A8, with its rows ordered top
  // to bottom. Given this path is used for screenshots/SurfaceFromElement,
  // that's the representation we need.
  if (aYFlip) {
    if (aPremultiply) {
      if (!PremultiplyYFlipData(res.shmem->get<uint8_t>(), stride.value(),
                                aFormat, map.GetData(), map.GetStride(), format,
                                size)) {
        return nullptr;
      }
    } else {
      if (!SwizzleYFlipData(res.shmem->get<uint8_t>(), stride.value(), aFormat,
                            map.GetData(), map.GetStride(), format, size)) {
        return nullptr;
      }
    }
  } else if (aPremultiply) {
    if (!PremultiplyData(res.shmem->get<uint8_t>(), stride.value(), aFormat,
                         map.GetData(), map.GetStride(), format, size)) {
      return nullptr;
    }
  } else {
    if (!SwizzleData(res.shmem->get<uint8_t>(), stride.value(), aFormat,
                     map.GetData(), map.GetStride(), format, size)) {
      return nullptr;
    }
  }
  return surface.forget();
}

}  // namespace mozilla::gfx
