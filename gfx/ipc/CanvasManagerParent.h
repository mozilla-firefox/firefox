/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _include_gfx_ipc_CanvasManagerParent_h_
#define _include_gfx_ipc_CanvasManagerParent_h_

#include "mozilla/StaticMonitor.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/gfx/PCanvasManagerParent.h"
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
      const uint32_t& aManagerId, const ActorId& aProtocolId,
      const Maybe<RemoteTextureOwnerId>& aOwnerId,
      const Maybe<RawId>& aCommandEncoderId,
      const Maybe<RawId>& aCommandBufferId,
      webgl::FrontBufferSnapshotIpc* aResult);

  static mozilla::ipc::IProtocol* GetCanvasActor(
      dom::ContentParentId aContentId, uint32_t aManagerId, ActorId aCanvasId);

  static already_AddRefed<SourceSurface> TryResolveCanvasSurface(
      dom::ContentParentId aContentId, uint32_t aManagerId, ActorId aCanvasId,
      uintptr_t aExportId,
      Maybe<layers::SurfaceDescriptor>* aOutSharedSurfaceDesc = nullptr);

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

#endif  // _include_gfx_ipc_CanvasManagerParent_h_
