/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _include_gfx_ipc_CanvasManagerParent_h__
#define _include_gfx_ipc_CanvasManagerParent_h__

#include "mozilla/gfx/PCanvasManagerParent.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/StaticMonitor.h"
#include "mozilla/UniquePtr.h"
#include "nsHashtablesFwd.h"
#include "nsTArray.h"

namespace mozilla {
namespace layers {
class CanvasTranslator;
class HostIPCAllocator;
class SharedSurfacesHolder;
class SurfaceDescriptor;
}  // namespace layers

namespace gfx {

class CanvasManagerParent final : public PCanvasManagerParent {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CanvasManagerParent, override);

  static void Init(Endpoint<PCanvasManagerParent>&& aEndpoint,
                   layers::SharedSurfacesHolder* aSharedSurfacesHolder,
                   const dom::ContentParentId& aContentId);

  static void Shutdown();

  static void DisableRemoteCanvas();

  CanvasManagerParent(layers::SharedSurfacesHolder* aSharedSurfacesHolder,
                      const dom::ContentParentId& aContentId);

  void Bind(Endpoint<PCanvasManagerParent>&& aEndpoint);
  void ActorDestroy(ActorDestroyReason aWhy) override;

  already_AddRefed<dom::PWebGLParent> AllocPWebGLParent();
  already_AddRefed<webgpu::PWebGPUParent> AllocPWebGPUParent();
  already_AddRefed<layers::PCanvasParent> AllocPCanvasParent();

  mozilla::ipc::IPCResult RecvInitialize(const uint32_t& aId);
  mozilla::ipc::IPCResult RecvGetSnapshot(
      const uint32_t& aManagerId, const int32_t& aProtocolId,
      const Maybe<RemoteTextureOwnerId>& aOwnerId,
      const Maybe<RawId>& aCommandEncoderId,
      webgl::FrontBufferSnapshotIpc* aResult);

  static mozilla::ipc::IProtocol* GetCanvasActor(
      dom::ContentParentId aContentId, uint32_t aManagerId, int32_t aCanvasId);

  static already_AddRefed<DataSourceSurface> GetCanvasSurface(
      dom::ContentParentId aContentId, uint32_t aManagerId, int32_t aCanvasId,
      uintptr_t aSurfaceId);

 private:
  static void ShutdownInternal();
  static void DisableRemoteCanvasInternal();

  ~CanvasManagerParent() override;

  RefPtr<layers::SharedSurfacesHolder> mSharedSurfacesHolder;
  const dom::ContentParentId mContentId;
  uint32_t mId = 0;

  using ManagerSet = nsTHashSet<RefPtr<CanvasManagerParent>>;
  static ManagerSet sManagers;
};

}  // namespace gfx
}  // namespace mozilla

#endif  // _include_gfx_ipc_CanvasManagerParent_h__
