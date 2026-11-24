/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VideoBridgeChild.h"

#include "CompositorThread.h"
#include "SynchronousTask.h"
#include "VideoBridgeParent.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/ipc/Endpoint.h"
#include "nsXULAppAPI.h"
#include "transport/runnable_utils.h"

namespace mozilla {
namespace layers {

// Singleton
static StaticMutex sVideoBridgeLock MOZ_UNANNOTATED;
StaticRefPtr<VideoBridgeChild> sVideoBridge;

/* static */
void VideoBridgeChild::StartupForGPUProcess() {
  ipc::Endpoint<PVideoBridgeParent> parentPipe;
  ipc::Endpoint<PVideoBridgeChild> childPipe;

  PVideoBridge::CreateEndpoints(ipc::EndpointProcInfo::Current(),
                                ipc::EndpointProcInfo::Current(), &parentPipe,
                                &childPipe);

  VideoBridgeChild::Open(std::move(childPipe));
  VideoBridgeParent::Open(std::move(parentPipe), VideoBridgeSource::GpuProcess);
}

/* static */
void VideoBridgeChild::Open(Endpoint<PVideoBridgeChild>&& aEndpoint) {
  StaticMutexAutoLock lock(sVideoBridgeLock);
  // The old bridge may still be able to send: when the compositor process
  // goes away, the new endpoint can arrive before the old bridge's channel
  // error has been processed.
  RefPtr<VideoBridgeChild> oldBridge = sVideoBridge.get();
  sVideoBridge = new VideoBridgeChild();

  if (!aEndpoint.Bind(sVideoBridge)) {
    // We can't recover from this.
    MOZ_CRASH("Failed to bind VideoBridgeChild to endpoint");
  }

  if (oldBridge) {
    oldBridge->mReplacedHolder.ResolveIfExists(true, __func__);
  }
}

/* static */
void VideoBridgeChild::Shutdown() {
  StaticMutexAutoLock lock(sVideoBridgeLock);
  if (sVideoBridge) {
    sVideoBridge->mReplacedHolder.RejectIfExists(NS_ERROR_ABORT, __func__);
    sVideoBridge->Close();
    sVideoBridge = nullptr;
  }
}

VideoBridgeChild::VideoBridgeChild()
    : mThread(GetCurrentSerialEventTarget()), mCanSend(true) {}

VideoBridgeChild::~VideoBridgeChild() = default;

RefPtr<VideoBridgeChild> VideoBridgeChild::GetSingleton() {
  StaticMutexAutoLock lock(sVideoBridgeLock);
  return sVideoBridge;
}

/* static */
RefPtr<GenericNonExclusivePromise> VideoBridgeChild::WaitUntilReady() {
  RefPtr<GenericNonExclusivePromise> replaced;
  {
    StaticMutexAutoLock lock(sVideoBridgeLock);
    if (!sVideoBridge || sVideoBridge->CanSend()) {
      return GenericNonExclusivePromise::CreateAndResolve(true, __func__);
    }
    replaced = sVideoBridge->mReplacedHolder.Ensure(__func__);
  }

  // Our replacement may also have been lost by the time we run, so check again.
  return replaced->Then(
      GetCurrentSerialEventTarget(), __func__,
      [](const GenericNonExclusivePromise::ResolveOrRejectValue& aValue)
          -> RefPtr<GenericNonExclusivePromise> {
        if (aValue.IsReject()) {
          return GenericNonExclusivePromise::CreateAndReject(
              aValue.RejectValue(), __func__);
        }
        return WaitUntilReady();
      });
}

bool VideoBridgeChild::AllocUnsafeShmem(size_t aSize, ipc::Shmem* aShmem) {
  if (!mThread->IsOnCurrentThread()) {
    return DispatchAllocShmemInternal(aSize, aShmem, true);  // true: unsafe
  }

  if (!CanSend()) {
    return false;
  }

  return PVideoBridgeChild::AllocUnsafeShmem(aSize, aShmem);
}

bool VideoBridgeChild::AllocShmem(size_t aSize, ipc::Shmem* aShmem) {
  MOZ_RELEASE_ASSERT(CanSend());
  return PVideoBridgeChild::AllocShmem(aSize, aShmem);
}

void VideoBridgeChild::ProxyAllocShmemNow(SynchronousTask* aTask, size_t aSize,
                                          ipc::Shmem* aShmem, bool aUnsafe,
                                          bool* aSuccess) {
  AutoCompleteTask complete(aTask);

  if (!CanSend()) {
    return;
  }

  bool ok = false;
  if (aUnsafe) {
    ok = AllocUnsafeShmem(aSize, aShmem);
  } else {
    ok = AllocShmem(aSize, aShmem);
  }
  *aSuccess = ok;
}

bool VideoBridgeChild::DispatchAllocShmemInternal(size_t aSize,
                                                  ipc::Shmem* aShmem,
                                                  bool aUnsafe) {
  SynchronousTask task("AllocatorProxy alloc");

  bool success = false;
  RefPtr<Runnable> runnable = WrapRunnable(
      RefPtr<VideoBridgeChild>(this), &VideoBridgeChild::ProxyAllocShmemNow,
      &task, aSize, aShmem, aUnsafe, &success);
  GetThread()->Dispatch(runnable.forget());

  task.Wait();

  return success;
}

void VideoBridgeChild::ProxyDeallocShmemNow(SynchronousTask* aTask,
                                            ipc::Shmem* aShmem, bool* aResult) {
  AutoCompleteTask complete(aTask);

  if (!CanSend()) {
    return;
  }
  *aResult = DeallocShmem(*aShmem);
}

bool VideoBridgeChild::DeallocShmem(ipc::Shmem& aShmem) {
  if (GetThread()->IsOnCurrentThread()) {
    if (!CanSend()) {
      return false;
    }
    return PVideoBridgeChild::DeallocShmem(aShmem);
  }

  SynchronousTask task("AllocatorProxy Dealloc");
  bool result = false;

  RefPtr<Runnable> runnable = WrapRunnable(
      RefPtr<VideoBridgeChild>(this), &VideoBridgeChild::ProxyDeallocShmemNow,
      &task, &aShmem, &result);
  GetThread()->Dispatch(runnable.forget());

  task.Wait();
  return result;
}

void VideoBridgeChild::ActorDestroy(ActorDestroyReason aWhy) {
  mCanSend = false;
}

already_AddRefed<PTextureChild> VideoBridgeChild::CreateTexture(
    const SurfaceDescriptor& aSharedData, ReadLockDescriptor&& aReadLock,
    LayersBackend aLayersBackend, TextureFlags aFlags,
    const dom::ContentParentId& aContentId, uint64_t aSerial,
    wr::MaybeExternalImageId& aExternalImageId) {
  MOZ_RELEASE_ASSERT(CanSend());
  RefPtr actor = TextureClient::CreateIPDLActor();
  if (!SendPTextureConstructor(actor, aSharedData, std::move(aReadLock),
                               aLayersBackend, aFlags, aContentId, aSerial)) {
    return nullptr;
  }
  return actor.forget();
}

bool VideoBridgeChild::IsSameProcess() const {
  return OtherChildID() == XRE_GetChildID();
}

void VideoBridgeChild::HandleFatalError(const char* aMsg) {
  dom::ContentChild::FatalErrorIfNotUsingGPUProcess(aMsg, OtherChildID());
}

mozilla::ipc::IPCResult VideoBridgeChild::RecvPing(PingResolver&& aResolver) {
  aResolver(void_t{});
  return IPC_OK();
}

}  // namespace layers
}  // namespace mozilla
