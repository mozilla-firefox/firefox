/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CanvasManagerParent.h"
#include "gfxPlatform.h"
#include "mozilla/dom/WebGLParent.h"
#include "mozilla/gfx/CanvasRenderThread.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/gfx/GPUParent.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/layers/CanvasTranslator.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/ISurfaceAllocator.h"
#include "mozilla/layers/SharedSurfacesParent.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "mozilla/webgpu/WebGPUParent.h"
#include "nsIThread.h"
#include "nsThreadUtils.h"

namespace mozilla::gfx {

MOZ_RUNINIT CanvasManagerParent::ManagerSet CanvasManagerParent::sManagers;

/* static */ void CanvasManagerParent::Init(
    Endpoint<PCanvasManagerParent>&& aEndpoint,
    layers::SharedSurfacesHolder* aSharedSurfacesHolder,
    const dom::ContentParentId& aContentId) {
  MOZ_ASSERT(layers::CompositorThreadHolder::IsInCompositorThread());

  auto manager =
      MakeRefPtr<CanvasManagerParent>(aSharedSurfacesHolder, aContentId);

  nsCOMPtr<nsIThread> owningThread =
      gfx::CanvasRenderThread::GetCanvasRenderThread();
  MOZ_ASSERT(owningThread);

  owningThread->Dispatch(NewRunnableMethod<Endpoint<PCanvasManagerParent>&&>(
      "CanvasManagerParent::Bind", manager, &CanvasManagerParent::Bind,
      std::move(aEndpoint)));
}

/* static */ void CanvasManagerParent::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIThread> owningThread =
      gfx::CanvasRenderThread::GetCanvasRenderThread();
  MOZ_ASSERT(owningThread);

  NS_DispatchAndSpinEventLoopUntilComplete(
      "CanvasManagerParent::Shutdown"_ns, owningThread,
      NS_NewRunnableFunction("CanvasManagerParent::Shutdown", []() -> void {
        CanvasManagerParent::ShutdownInternal();
      }));
}

/* static */ void CanvasManagerParent::ShutdownInternal() {
  nsTArray<RefPtr<CanvasManagerParent>> actors(sManagers.Count());
  // Do a copy since Close will remove the entry from the set.
  for (const auto& actor : sManagers) {
    actors.AppendElement(actor);
  }

  for (auto const& actor : actors) {
    actor->Close();
  }
}

/* static */ void CanvasManagerParent::DisableRemoteCanvas() {
  NS_DispatchToMainThread(
      NS_NewRunnableFunction("CanvasManagerParent::DisableRemoteCanvas", [] {
        if (XRE_IsGPUProcess()) {
          GPUParent::GetSingleton()->NotifyDisableRemoteCanvas();
        } else {
          gfxPlatform::DisableRemoteCanvas();
        }
      }));

  if (CanvasRenderThread::IsInCanvasRenderThread()) {
    DisableRemoteCanvasInternal();
    return;
  }

  CanvasRenderThread::Dispatch(NS_NewRunnableFunction(
      "CanvasManagerParent::DisableRemoteCanvas",
      [] { CanvasManagerParent::DisableRemoteCanvasInternal(); }));
}

/* static */ void CanvasManagerParent::DisableRemoteCanvasInternal() {
  MOZ_ASSERT(CanvasRenderThread::IsInCanvasRenderThread());

  AutoTArray<RefPtr<layers::CanvasTranslator>, 16> actors;
  for (const auto& manager : sManagers) {
    for (const auto& canvas : manager->ManagedPCanvasParent()) {
      actors.AppendElement(static_cast<layers::CanvasTranslator*>(canvas));
    }
  }

  for (const auto& actor : actors) {
    Unused << NS_WARN_IF(!actor->SendDeactivate());
  }
}

CanvasManagerParent::CanvasManagerParent(
    layers::SharedSurfacesHolder* aSharedSurfacesHolder,
    const dom::ContentParentId& aContentId)
    : mSharedSurfacesHolder(aSharedSurfacesHolder), mContentId(aContentId) {}

CanvasManagerParent::~CanvasManagerParent() = default;

void CanvasManagerParent::Bind(Endpoint<PCanvasManagerParent>&& aEndpoint) {
  if (!aEndpoint.Bind(this)) {
    NS_WARNING("Failed to bind CanvasManagerParent!");
    return;
  }

#ifdef DEBUG
  for (CanvasManagerParent* i : sManagers) {
    MOZ_ASSERT_IF(i->mContentId == mContentId,
                  i->OtherPidMaybeInvalid() == OtherPidMaybeInvalid());
  }
#endif

  sManagers.Insert(this);
}

void CanvasManagerParent::ActorDestroy(ActorDestroyReason aWhy) {
  sManagers.Remove(this);
}

already_AddRefed<dom::PWebGLParent> CanvasManagerParent::AllocPWebGLParent() {
  if (NS_WARN_IF(!gfxVars::AllowWebglOop() &&
                 !StaticPrefs::webgl_out_of_process_force())) {
    MOZ_ASSERT_UNREACHABLE("AllocPWebGLParent without remote WebGL");
    return nullptr;
  }
  return MakeAndAddRef<dom::WebGLParent>(mSharedSurfacesHolder, mContentId);
}

already_AddRefed<webgpu::PWebGPUParent>
CanvasManagerParent::AllocPWebGPUParent() {
  if (NS_WARN_IF(!gfxVars::AllowWebGPU())) {
    MOZ_ASSERT_UNREACHABLE("AllocPWebGPUParent without WebGPU");
    return nullptr;
  }

  return MakeAndAddRef<webgpu::WebGPUParent>();
}

mozilla::ipc::IPCResult CanvasManagerParent::RecvInitialize(
    const uint32_t& aId) {
  if (!aId) {
    return IPC_FAIL(this, "invalid id");
  }
  if (mId) {
    return IPC_FAIL(this, "already initialized");
  }
  mId = aId;
  return IPC_OK();
}

already_AddRefed<layers::PCanvasParent>
CanvasManagerParent::AllocPCanvasParent() {
  if (NS_WARN_IF(!gfx::gfxVars::RemoteCanvasEnabled() &&
                 !gfx::gfxVars::UseAcceleratedCanvas2D())) {
    MOZ_ASSERT_UNREACHABLE("AllocPCanvasParent without remote canvas");
    return nullptr;
  }
  if (NS_WARN_IF(!mId)) {
    MOZ_ASSERT_UNREACHABLE("AllocPCanvasParent without ID");
    return nullptr;
  }
  return MakeAndAddRef<layers::CanvasTranslator>(mSharedSurfacesHolder,
                                                 mContentId, mId);
}

mozilla::ipc::IPCResult CanvasManagerParent::RecvGetSnapshot(
    const uint32_t& aManagerId, const int32_t& aProtocolId,
    const Maybe<RemoteTextureOwnerId>& aOwnerId,
    const Maybe<RawId>& aCommandEncoderId,
    webgl::FrontBufferSnapshotIpc* aResult) {
  if (!aManagerId) {
    return IPC_FAIL(this, "invalid id");
  }

  IProtocol* actor = nullptr;
  for (CanvasManagerParent* i : sManagers) {
    if (i->mContentId == mContentId && i->mId == aManagerId) {
      actor = i->Lookup(aProtocolId);
      break;
    }
  }

  if (!actor) {
    return IPC_FAIL(this, "invalid actor");
  }

  if (actor->GetSide() != mozilla::ipc::Side::ParentSide) {
    return IPC_FAIL(this, "unsupported actor");
  }

  webgl::FrontBufferSnapshotIpc buffer;
  switch (actor->GetProtocolId()) {
    case ProtocolId::PWebGLMsgStart: {
      RefPtr<dom::WebGLParent> webgl = static_cast<dom::WebGLParent*>(actor);
      mozilla::ipc::IPCResult rv = webgl->GetFrontBufferSnapshot(&buffer, this);
      if (!rv) {
        return rv;
      }
    } break;
    case ProtocolId::PWebGPUMsgStart: {
      RefPtr<webgpu::WebGPUParent> webgpu =
          static_cast<webgpu::WebGPUParent*>(actor);
      IntSize size;
      if (aOwnerId.isNothing()) {
        return IPC_FAIL(this, "invalid OwnerId");
      }
      if (aCommandEncoderId.isNothing()) {
        return IPC_FAIL(this, "invalid CommandEncoderId");
      }
      uint32_t stride = 0;
      mozilla::ipc::IPCResult rv = webgpu->GetFrontBufferSnapshot(
          this, *aOwnerId, *aCommandEncoderId, buffer.shmem, size, stride);
      if (!rv) {
        return rv;
      }
      buffer.surfSize.x = static_cast<uint32_t>(size.width);
      buffer.surfSize.y = static_cast<uint32_t>(size.height);
      buffer.byteStride = stride;
    } break;
    default:
      return IPC_FAIL(this, "unsupported protocol");
  }

  *aResult = std::move(buffer);
  return IPC_OK();
}

/* static */ mozilla::ipc::IProtocol* CanvasManagerParent::GetCanvasActor(
    dom::ContentParentId aContentId, uint32_t aManagerId, int32_t aCanvasId) {
  IProtocol* actor = nullptr;
  for (CanvasManagerParent* i : sManagers) {
    if (i->mContentId == aContentId && i->mId == aManagerId) {
      actor = i->Lookup(aCanvasId);
      break;
    }
  }
  return actor;
}

/* static */ already_AddRefed<DataSourceSurface>
CanvasManagerParent::GetCanvasSurface(dom::ContentParentId aContentId,
                                      uint32_t aManagerId, int32_t aCanvasId,
                                      uintptr_t aSurfaceId) {
  IProtocol* actor = GetCanvasActor(aContentId, aManagerId, aCanvasId);
  if (!actor) {
    return nullptr;
  }
  switch (actor->GetProtocolId()) {
    case ProtocolId::PCanvasMsgStart:
      return static_cast<layers::CanvasTranslator*>(actor)->WaitForSurface(
          aSurfaceId);
    default:
      MOZ_ASSERT_UNREACHABLE("Unsupported protocol");
      break;
  }
  return nullptr;
}

}  // namespace mozilla::gfx
