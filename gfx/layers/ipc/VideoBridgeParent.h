/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef gfx_layers_ipc_VideoBridgeParent_h_
#define gfx_layers_ipc_VideoBridgeParent_h_

#include "mozilla/Monitor.h"
#include "mozilla/layers/ISurfaceAllocator.h"
#include "mozilla/layers/PVideoBridgeParent.h"

namespace mozilla::layers {

enum class VideoBridgeSource : uint8_t;
class CompositorThreadHolder;

class VideoBridgeParent final : public PVideoBridgeParent,
                                public HostIPCAllocator,
                                public mozilla::ipc::IShmemAllocator {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(VideoBridgeParent, final);

  static RefPtr<VideoBridgeParent> GetSingleton(
      const Maybe<VideoBridgeSource>& aSource);

  static void Open(Endpoint<PVideoBridgeParent>&& aEndpoint,
                   VideoBridgeSource aSource);
  static void Shutdown();
  static void UnregisterExternalImages();

  already_AddRefed<TextureHost> LookupTextureAsync(
      const dom::ContentParentId& aContentId, uint64_t aSerial);
  already_AddRefed<TextureHost> LookupTexture(
      const dom::ContentParentId& aContentId, uint64_t aSerial);
  void RemoveTexture(uint64_t aSerial);

  // PVideoBridgeParent
  void ActorDestroy(ActorDestroyReason aWhy) override;
  already_AddRefed<PTextureParent> AllocPTextureParent(
      const SurfaceDescriptor& aSharedData, ReadLockDescriptor& aReadLock,
      const LayersBackend& aLayersBackend, const TextureFlags& aFlags,
      const dom::ContentParentId& aContentId, const uint64_t& aSerial);
  mozilla::ipc::IPCResult RecvGetTextureFactoryIdentifier(
      GetTextureFactoryIdentifierResolver&& aResolver);

  // HostIPCAllocator
  base::ProcessId GetChildProcessId() override { return OtherPid(); }
  void NotifyNotUsed(PTextureParent* aTexture,
                     uint64_t aTransactionId) override;
  void SendAsyncMessage(Span<const AsyncParentMessageData>) override;

  // ISurfaceAllocator
  IShmemAllocator* AsShmemAllocator() override { return this; }
  bool IsSameProcess() const override;
  bool IPCOpen() const override { return !mClosed; }

  // IShmemAllocator
  bool AllocShmem(size_t aSize, mozilla::ipc::Shmem* aShmem) override;

  bool AllocUnsafeShmem(size_t aSize, mozilla::ipc::Shmem* aShmem) override;

  bool DeallocShmem(mozilla::ipc::Shmem& aShmem) override;

 private:
  ~VideoBridgeParent();

  VideoBridgeParent();
  void Bind(Endpoint<PVideoBridgeParent>&& aEndpoint);
  static void ShutdownInternal();

  void DoUnregisterExternalImages();

  void RegisterSingleton(VideoBridgeSource aSource);
  void UnregisterSingleton();

  struct TextureHolder {
    RefPtr<TextureHost> mTextureHost;
    dom::ContentParentId mContentId;
  };

  Monitor mMonitor;
  RefPtr<CompositorThreadHolder> mCompositorThreadHolder
      MOZ_GUARDED_BY(mMonitor);
  std::map<uint64_t, TextureHolder> mTextureMap MOZ_GUARDED_BY(mMonitor);
  bool mClosed;
};

}  // namespace mozilla::layers

#endif  // gfx_layers_ipc_VideoBridgeParent_h_
