/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_VIDEOBRIDGECHILD_H
#define MOZILLA_GFX_VIDEOBRIDGECHILD_H

#include "ISurfaceAllocator.h"
#include "TextureForwarder.h"
#include "mozilla/MozPromise.h"
#include "mozilla/layers/PVideoBridgeChild.h"
#include "mozilla/layers/VideoBridgeUtils.h"

namespace mozilla {
namespace layers {

class SynchronousTask;

class VideoBridgeChild final : public PVideoBridgeChild,
                               public TextureForwarder {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(VideoBridgeChild, override);

  static void StartupForGPUProcess();
  static void Shutdown();

  static RefPtr<VideoBridgeChild> GetSingleton();

  // Resolved once the singleton can send, e.g. after we are given a new
  // endpoint because the compositor was recreated. Rejected if we are shut
  // down.
  static RefPtr<GenericNonExclusivePromise> WaitUntilReady();

  mozilla::ipc::IPCResult RecvPing(PingResolver&& aResolver);

  void ActorDestroy(ActorDestroyReason aWhy) override;

  // ISurfaceAllocator
  bool AllocUnsafeShmem(size_t aSize, mozilla::ipc::Shmem* aShmem) override;
  bool AllocShmem(size_t aSize, mozilla::ipc::Shmem* aShmem) override;
  bool DeallocShmem(mozilla::ipc::Shmem& aShmem) override;

  // TextureForwarder
  already_AddRefed<PTextureChild> CreateTexture(
      const SurfaceDescriptor& aSharedData, ReadLockDescriptor&& aReadLock,
      LayersBackend aLayersBackend, TextureFlags aFlags,
      const dom::ContentParentId& aContentId, uint64_t aSerial,
      wr::MaybeExternalImageId& aExternalImageId) override;

  // LayersIPCChannel
  nsISerialEventTarget* GetThread() const override { return mThread; }
  void CancelWaitForNotifyNotUsed(uint64_t aTextureId) override {
    MOZ_ASSERT_UNREACHABLE("NO RECYCLING HERE");
  }

  // ISurfaceAllocator
  bool IsSameProcess() const override;

  bool CanSend() { return mCanSend; }

  static void Open(Endpoint<PVideoBridgeChild>&& aEndpoint);

 protected:
  void HandleFatalError(const char* aMsg) override;
  bool DispatchAllocShmemInternal(size_t aSize, mozilla::ipc::Shmem* aShmem,
                                  bool aUnsafe);
  void ProxyAllocShmemNow(SynchronousTask* aTask, size_t aSize,
                          mozilla::ipc::Shmem* aShmem, bool aUnsafe,
                          bool* aSuccess);
  void ProxyDeallocShmemNow(SynchronousTask* aTask, mozilla::ipc::Shmem* aShmem,
                            bool* aResult);

 private:
  VideoBridgeChild();
  virtual ~VideoBridgeChild();

  nsCOMPtr<nsISerialEventTarget> mThread;
  bool mCanSend;
  // Settled when we are replaced by a new instance, or shut down. Guarded by
  // sVideoBridgeLock.
  MozPromiseHolder<GenericNonExclusivePromise> mReplacedHolder;
};

}  // namespace layers
}  // namespace mozilla

#endif
