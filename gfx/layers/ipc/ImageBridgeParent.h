/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef gfx_layers_ipc_ImageBridgeParent_h_
#define gfx_layers_ipc_ImageBridgeParent_h_

#include <stddef.h>  // for size_t
#include <stdint.h>  // for uint32_t, uint64_t

#include "CompositableTransactionParent.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/PImageBridgeParent.h"
#include "nsISupportsImpl.h"
#include "nsTArrayForwardDeclare.h"  // for nsTArray

namespace mozilla {
namespace ipc {
class Shmem;
}  // namespace ipc

namespace layers {

struct ImageCompositeNotificationInfo;
class RemoteTextureTxnScheduler;

/**
 * ImageBridgeParent is the manager Protocol of async Compositables.
 */
class ImageBridgeParent final : public PImageBridgeParent,
                                public CompositableParentManager,
                                public mozilla::ipc::IShmemAllocator {
 public:
  typedef nsTArray<CompositableOperation> EditArray;
  typedef nsTArray<OpDestroy> OpDestroyArray;

 protected:
  ImageBridgeParent(nsISerialEventTarget* aThread,
                    ipc::EndpointProcInfo aChildProcessInfo,
                    dom::ContentParentId aContentId, uint32_t aNamespace);

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ImageBridgeParent, final);

  /**
   * Creates the globals of ImageBridgeParent.
   */
  static void Setup();

  static ImageBridgeParent* CreateSameProcess(uint32_t aNamespace);
  static bool CreateForGPUProcess(Endpoint<PImageBridgeParent>&& aEndpoint,
                                  uint32_t aNamespace);
  static bool CreateForContent(Endpoint<PImageBridgeParent>&& aEndpoint,
                               dom::ContentParentId aContentId,
                               uint32_t aNamespace);
  static void Shutdown();

  IShmemAllocator* AsShmemAllocator() override { return this; }

  void ActorDestroy(ActorDestroyReason aWhy) override;

  // CompositableParentManager
  void SendAsyncMessage(Span<const AsyncParentMessageData>) override;

  void NotifyNotUsed(PTextureParent* aTexture,
                     uint64_t aTransactionId) override;

  GeckoChildID GetChildID() override { return OtherChildID(); }
  dom::ContentParentId GetContentId() override { return mContentId; }

  // PImageBridge
  mozilla::ipc::IPCResult RecvUpdate(EditArray&& aEdits,
                                     OpDestroyArray&& aToDestroy,
                                     const uint64_t& aFwdTransactionId);

  already_AddRefed<PTextureParent> AllocPTextureParent(
      const SurfaceDescriptor& aSharedData, ReadLockDescriptor& aReadLock,
      const LayersBackend& aLayersBackend, const TextureFlags& aFlags,
      const uint64_t& aSerial,
      const wr::MaybeExternalImageId& aExternalImageId);

  mozilla::ipc::IPCResult RecvNewCompositable(const CompositableHandle& aHandle,
                                              const TextureInfo& aInfo);
  mozilla::ipc::IPCResult RecvReleaseCompositable(
      const CompositableHandle& aHandle);

  PMediaSystemResourceManagerParent* AllocPMediaSystemResourceManagerParent();
  bool DeallocPMediaSystemResourceManagerParent(
      PMediaSystemResourceManagerParent* aActor);

  // Shutdown step 1
  mozilla::ipc::IPCResult RecvWillClose();

  nsISerialEventTarget* GetThread() const { return mThread; }

  // IShmemAllocator

  bool AllocShmem(size_t aSize, ipc::Shmem* aShmem) override;

  bool AllocUnsafeShmem(size_t aSize, ipc::Shmem* aShmem) override;

  bool DeallocShmem(ipc::Shmem& aShmem) override;

  bool IsSameProcess() const override;

  static already_AddRefed<ImageBridgeParent> GetInstance(GeckoChildID aId);

  static bool NotifyImageComposites(
      nsTArray<ImageCompositeNotificationInfo>& aNotifications);

  bool UsesImageBridge() const override { return true; }

  bool IPCOpen() const override { return !mClosed; }

 protected:
  void Bind(Endpoint<PImageBridgeParent>&& aEndpoint);

 private:
  virtual ~ImageBridgeParent();

  static void ShutdownInternal();

  void DeferredDestroy();

  bool OwnsExternalImageId(const wr::ExternalImageId& aId) const;

  nsCOMPtr<nsISerialEventTarget> mThread;

  dom::ContentParentId mContentId;
  uint32_t mNamespace;

  bool mClosed;

  /**
   * Map of all living ImageBridgeParent instances
   */
  typedef std::map<GeckoChildID, ImageBridgeParent*> ImageBridgeMap;
  static ImageBridgeMap sImageBridges;

  RefPtr<CompositorThreadHolder> mCompositorThreadHolder;

  RefPtr<RemoteTextureTxnScheduler> mRemoteTextureTxnScheduler;
};

}  // namespace layers
}  // namespace mozilla

#endif  // gfx_layers_ipc_ImageBridgeParent_h_
