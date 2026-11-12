/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfacesParent.h"

#include "mozilla/StaticPrefs_image.h"
#include "mozilla/gfx/GPUProcessManager.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/CompositorManagerParent.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/SharedSurfacesMemoryReport.h"
#include "mozilla/layers/SourceSurfaceSharedData.h"
#include "mozilla/webrender/RenderSharedSurfaceTextureHost.h"
#include "mozilla/webrender/RenderThread.h"
#include "nsThreadUtils.h"  // for GetCurrentSerialEventTarget

namespace mozilla {
namespace layers {

using namespace mozilla::gfx;

StaticMutex SharedSurfacesParent::sMutex;
StaticAutoPtr<SharedSurfacesParent> SharedSurfacesParent::sInstance;

StaticMutex SharedSurfacesParent::sIdNamespaceMutex;
MOZ_RUNINIT std::unordered_map<uint32_t, uint32_t>
    SharedSurfacesParent::sIdNamespaces;

void SharedSurfacesParent::MappingTracker::NotifyExpiredLocked(
    SourceSurfaceSharedDataWrapper* aSurface,
    const StaticMutexAutoLock& aAutoLock) {
  RemoveObjectLocked(aSurface, aAutoLock);
  mExpired.AppendElement(aSurface);
}

void SharedSurfacesParent::MappingTracker::TakeExpired(
    nsTArray<RefPtr<gfx::SourceSurfaceSharedDataWrapper>>& aExpired,
    const StaticMutexAutoLock& aAutoLock) {
  aExpired = std::move(mExpired);
}

void SharedSurfacesParent::MappingTracker::InternalTrackerObserver::
    NotifyHandlerEnd() {
  nsTArray<RefPtr<gfx::SourceSurfaceSharedDataWrapper>> expired;
  {
    StaticMutexAutoLock lock(sMutex);
    if (sInstance) {
      sInstance->mTracker.TakeExpired(expired, lock);
    }
  }

  SharedSurfacesParent::ExpireMap(expired);
}

SharedSurfacesParent::SharedSurfacesParent()
    : mTracker(
          StaticPrefs::image_mem_shared_unmap_min_expiration_ms_AtStartup(),
          mozilla::GetCurrentSerialEventTarget()) {}

/* static */
void SharedSurfacesParent::Initialize() {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  StaticMutexAutoLock lock(sMutex);
  if (!sInstance) {
    sInstance = new SharedSurfacesParent();
    sInstance->mTracker.InitLocked(lock);
  }
}

/* static */
void SharedSurfacesParent::ShutdownRenderThread() {
  // The main thread should blocked on waiting for the render thread to
  // complete so this should be safe to release off the main thread.
  MOZ_RELEASE_ASSERT(wr::RenderThread::IsInRenderThread());
  StaticMutexAutoLock lock(sMutex);
  MOZ_RELEASE_ASSERT(sInstance);

  for (const auto& key : sInstance->mSurfaces.Keys()) {
    // There may be lingering consumers of the surfaces that didn't get shutdown
    // yet but since we are here, we know the render thread is finished and we
    // can unregister everything.
    wr::RenderThread::Get()->UnregisterExternalImageDuringShutdown(
        wr::ToExternalImageId(key));
  }
}

/* static */
void SharedSurfacesParent::Shutdown() {
  // The compositor thread and render threads are shutdown, so this is the last
  // thread that could use it. The expiration tracker needs to be freed on the
  // main thread.
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  StaticMutexAutoLock lock(sMutex);
  if (sInstance) {
    sInstance->mTracker.DestroyLocked(lock);
    sInstance = nullptr;
  }
}

/* static */
already_AddRefed<DataSourceSurface> SharedSurfacesParent::Get(
    const wr::ExternalImageId& aId) {
  RefPtr<SourceSurfaceSharedDataWrapper> surface;

  {
    StaticMutexAutoLock lock(sMutex);
    if (!sInstance) {
      gfxCriticalNote << "SSP:Get " << wr::AsUint64(aId) << " shtd";
      return nullptr;
    }

    if (sInstance->mSurfaces.Get(wr::AsUint64(aId), getter_AddRefs(surface))) {
      return surface.forget();
    }
  }

  // We cannot block the compositor thread since that's the thread the necessary
  // IPDL events would come in on.
  if (NS_WARN_IF(CompositorThreadHolder::IsInCompositorThread())) {
    return nullptr;
  }

  // Block until we see the relevant resource come in or the actor is destroyed.
  CompositorManagerParent::WaitForSharedSurface(aId);

  StaticMutexAutoLock lock(sMutex);
  if (!sInstance) {
    gfxCriticalNote << "SSP:Get " << wr::AsUint64(aId) << " shtd";
    return nullptr;
  }

  sInstance->mSurfaces.Get(wr::AsUint64(aId), getter_AddRefs(surface));
  return surface.forget();
}

/* static */
already_AddRefed<DataSourceSurface> SharedSurfacesParent::Acquire(
    const wr::ExternalImageId& aId) {
  StaticMutexAutoLock lock(sMutex);
  if (!sInstance) {
    gfxCriticalNote << "SSP:Acq " << wr::AsUint64(aId) << " shtd";
    return nullptr;
  }

  RefPtr<SourceSurfaceSharedDataWrapper> surface;
  sInstance->mSurfaces.Get(wr::AsUint64(aId), getter_AddRefs(surface));

  if (surface) {
    bool rv = surface->AddConsumer();
    MOZ_RELEASE_ASSERT(!rv);
  }
  return surface.forget();
}

/* static */
bool SharedSurfacesParent::Release(const wr::ExternalImageId& aId,
                                   bool aForCreator) {
  StaticMutexAutoLock lock(sMutex);
  if (!sInstance) {
    return false;
  }

  uint64_t id = wr::AsUint64(aId);
  RefPtr<SourceSurfaceSharedDataWrapper> surface;
  sInstance->mSurfaces.Get(wr::AsUint64(aId), getter_AddRefs(surface));
  if (!surface) {
    return false;
  }

  if (surface->RemoveConsumer(aForCreator)) {
    RemoveTrackingLocked(surface, lock);
    wr::RenderThread::Get()->UnregisterExternalImage(wr::ToExternalImageId(id));
    sInstance->mSurfaces.Remove(id);
  }

  return true;
}

/* static */
void SharedSurfacesParent::AddSameProcess(const wr::ExternalImageId& aId,
                                          SourceSurfaceSharedData* aSurface) {
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  StaticMutexAutoLock lock(sMutex);
  if (!sInstance) {
    gfxCriticalNote << "SSP:Ads " << wr::AsUint64(aId) << " shtd";
    return;
  }

  // If the child bridge detects it is in the combined UI/GPU process, then it
  // will insert a wrapper surface holding the shared memory buffer directly.
  // This is good because we avoid mapping the same shared memory twice, but
  // still allow the original surface to be freed and remove the wrapper from
  // the table when it is no longer needed.
  RefPtr surface = MakeRefPtr<SourceSurfaceSharedDataWrapper>();
  surface->Init(aSurface);

  uint64_t id = wr::AsUint64(aId);
  if (sInstance->mSurfaces.Contains(id)) {
    gfxCriticalNote << "SSP:Ads " << wr::AsUint64(aId) << " dupe";
    SharedSurfacesParent::RemoveTrackingLocked(surface, lock);
    MOZ_DIAGNOSTIC_CRASH("External image ID reused!");
    return;
  }

  auto texture = MakeRefPtr<wr::RenderSharedSurfaceTextureHost>(surface);
  wr::RenderThread::Get()->RegisterExternalImage(aId, texture.forget());

  sInstance->mSurfaces.InsertOrUpdate(id, std::move(surface));
}

/* static */
void SharedSurfacesParent::RemoveAll(uint32_t aNamespace) {
  StaticMutexAutoLock lock(sMutex);
  if (!sInstance) {
    return;
  }

  auto* renderThread = wr::RenderThread::Get();

  // Note that the destruction of a parent may not be cheap if it still has a
  // lot of surfaces still bound that require unmapping.
  for (auto i = sInstance->mSurfaces.Iter(); !i.Done(); i.Next()) {
    if (static_cast<uint32_t>(i.Key() >> 32) != aNamespace) {
      continue;
    }

    SourceSurfaceSharedDataWrapper* surface = i.Data();
    if (surface->HasCreatorRef() &&
        surface->RemoveConsumer(/* aForCreator */ true)) {
      RemoveTrackingLocked(surface, lock);
      if (renderThread) {
        renderThread->UnregisterExternalImage(wr::ToExternalImageId(i.Key()));
      }
      i.Remove();
    }
  }
}

/* static */
void SharedSurfacesParent::AddIdNamespace(const wr::IdNamespace& aIdNamespace,
                                          uint32_t aNamespace) {
  StaticMutexAutoLock lock(sIdNamespaceMutex);
  sIdNamespaces[aIdNamespace.mHandle] = aNamespace;
}

/* static */
void SharedSurfacesParent::RemoveIdNamespace(
    const wr::IdNamespace& aIdNamespace) {
  StaticMutexAutoLock lock(sIdNamespaceMutex);
  sIdNamespaces.erase(aIdNamespace.mHandle);
}

/* static */
already_AddRefed<DataSourceSurface> SharedSurfacesParent::GetForIdNamespace(
    const wr::IdNamespace& aIdNamespace, const wr::ExternalImageId& aId) {
  uint32_t surfaceNamespace;
  {
    StaticMutexAutoLock lock(sIdNamespaceMutex);
    auto i = sIdNamespaces.find(aIdNamespace.mHandle);
    if (NS_WARN_IF(i == sIdNamespaces.end())) {
      return nullptr;
    }
    surfaceNamespace = i->second;
  }

  if (NS_WARN_IF(surfaceNamespace !=
                 static_cast<uint32_t>(wr::AsUint64(aId) >> 32))) {
    MOZ_ASSERT_UNREACHABLE("Wrong namespace?");
    return nullptr;
  }

  return Get(aId);
}

/* static */
void SharedSurfacesParent::Add(const wr::ExternalImageId& aId,
                               SurfaceDescriptorShared&& aDesc,
                               base::ProcessId aPid) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MOZ_RELEASE_ASSERT(aPid != base::GetCurrentProcId());

  if (aDesc.format() != SurfaceFormat::B8G8R8X8 &&
      aDesc.format() != SurfaceFormat::B8G8R8A8) {
    return;
  }

  RefPtr surface = MakeRefPtr<SourceSurfaceSharedDataWrapper>();

  // We preferentially map in new surfaces when they are initially received
  // because we are likely to reference them in a display list soon. The unmap
  // will ensure we add the surface to the expiration tracker. We do it outside
  // the mutex to ensure we always lock the surface mutex first, and our mutex
  // second, to avoid deadlock.
  //
  // Note that the surface wrapper maps in the given handle as read only.
  surface->Init(aDesc.size(), aDesc.stride(), aDesc.format(),
                std::move(aDesc.handle()), aPid);

  StaticMutexAutoLock lock(sMutex);
  if (!sInstance) {
    gfxCriticalNote << "SSP:Add " << wr::AsUint64(aId) << " shtd";
    return;
  }

  uint64_t id = wr::AsUint64(aId);
  if (sInstance->mSurfaces.Contains(id)) {
    gfxCriticalNote << "SSP:Add " << wr::AsUint64(aId) << " dupe";
    SharedSurfacesParent::RemoveTrackingLocked(surface, lock);
    MOZ_DIAGNOSTIC_CRASH("External image ID reused!");
    return;
  }

  auto texture = MakeRefPtr<wr::RenderSharedSurfaceTextureHost>(surface);
  wr::RenderThread::Get()->RegisterExternalImage(aId, texture.forget());

  sInstance->mSurfaces.InsertOrUpdate(id, std::move(surface));
}

/* static */
void SharedSurfacesParent::Remove(const wr::ExternalImageId& aId) {
  bool rv = Release(aId, /* aForCreator */ true);
  MOZ_RELEASE_ASSERT(rv);
}

/* static */
void SharedSurfacesParent::AddTrackingLocked(
    SourceSurfaceSharedDataWrapper* aSurface,
    const StaticMutexAutoLock& aAutoLock) {
  MOZ_RELEASE_ASSERT(!aSurface->GetExpirationState()->IsTracked());
  MOZ_RELEASE_ASSERT(aSurface->GetConsumers() > 0);
  sInstance->mTracker.AddObjectLocked(aSurface, aAutoLock);
}

/* static */
void SharedSurfacesParent::AddTracking(
    SourceSurfaceSharedDataWrapper* aSurface) {
  StaticMutexAutoLock lock(sMutex);
  if (!sInstance) {
    return;
  }

  AddTrackingLocked(aSurface, lock);
}

/* static */
void SharedSurfacesParent::RemoveTrackingLocked(
    SourceSurfaceSharedDataWrapper* aSurface,
    const StaticMutexAutoLock& aAutoLock) {
  if (!aSurface->GetExpirationState()->IsTracked()) {
    return;
  }

  sInstance->mTracker.RemoveObjectLocked(aSurface, aAutoLock);
}

/* static */
void SharedSurfacesParent::RemoveTracking(
    SourceSurfaceSharedDataWrapper* aSurface) {
  StaticMutexAutoLock lock(sMutex);
  if (!sInstance) {
    return;
  }

  RemoveTrackingLocked(aSurface, lock);
}

/* static */
bool SharedSurfacesParent::AgeOneGenerationLocked(
    nsTArray<RefPtr<SourceSurfaceSharedDataWrapper>>& aExpired,
    const StaticMutexAutoLock& aAutoLock) {
  if (sInstance->mTracker.IsEmptyLocked(aAutoLock)) {
    return false;
  }

  sInstance->mTracker.AgeOneGenerationLocked(aAutoLock);
  sInstance->mTracker.TakeExpired(aExpired, aAutoLock);
  return true;
}

/* static */
bool SharedSurfacesParent::AgeOneGeneration(
    nsTArray<RefPtr<SourceSurfaceSharedDataWrapper>>& aExpired) {
  StaticMutexAutoLock lock(sMutex);
  if (!sInstance) {
    return false;
  }

  return AgeOneGenerationLocked(aExpired, lock);
}

/* static */
bool SharedSurfacesParent::AgeAndExpireOneGeneration() {
  nsTArray<RefPtr<SourceSurfaceSharedDataWrapper>> expired;
  bool aged = AgeOneGeneration(expired);
  ExpireMap(expired);
  return aged;
}

/* static */
void SharedSurfacesParent::ExpireMap(
    nsTArray<RefPtr<SourceSurfaceSharedDataWrapper>>& aExpired) {
  // The surfaces may have lost their last consumer after being taken out of
  // the tracker, since sMutex is not held here. Unmapping them is still safe.
  for (auto& surface : aExpired) {
    surface->ExpireMap();
  }
}

/* static */
void SharedSurfacesParent::AccumulateMemoryReport(
    uint32_t aNamespace, SharedSurfacesMemoryReport& aReport) {
  StaticMutexAutoLock lock(sMutex);
  if (!sInstance) {
    return;
  }

  for (const auto& entry : sInstance->mSurfaces) {
    if (static_cast<uint32_t>(entry.GetKey() >> 32) != aNamespace) {
      continue;
    }

    SourceSurfaceSharedDataWrapper* surface = entry.GetData();
    aReport.mSurfaces.insert(std::make_pair(
        entry.GetKey(),
        SharedSurfacesMemoryReport::SurfaceEntry{
            surface->GetCreatorPid(), surface->GetSize(), surface->Stride(),
            surface->GetConsumers(), surface->HasCreatorRef()}));
  }
}

/* static */
bool SharedSurfacesParent::AccumulateMemoryReport(
    SharedSurfacesMemoryReport& aReport) {
  if (XRE_IsParentProcess()) {
    GPUProcessManager* gpm = GPUProcessManager::Get();
    if (!gpm || gpm->GPUProcessPid() != base::kInvalidProcessId) {
      return false;
    }
  } else if (!XRE_IsGPUProcess()) {
    return false;
  }

  StaticMutexAutoLock lock(sMutex);
  if (!sInstance) {
    return true;
  }

  for (const auto& entry : sInstance->mSurfaces) {
    SourceSurfaceSharedDataWrapper* surface = entry.GetData();
    aReport.mSurfaces.insert(std::make_pair(
        entry.GetKey(),
        SharedSurfacesMemoryReport::SurfaceEntry{
            surface->GetCreatorPid(), surface->GetSize(), surface->Stride(),
            surface->GetConsumers(), surface->HasCreatorRef()}));
  }

  return true;
}

}  // namespace layers
}  // namespace mozilla
