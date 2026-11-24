/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_IMAGEBRIDGECHILD_H
#define MOZILLA_GFX_IMAGEBRIDGECHILD_H

#include <stddef.h>  // for size_t
#include <stdint.h>  // for uint32_t, uint64_t

#include <unordered_map>

#include "ImageContainer.h"
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"  // for override
#include "mozilla/Mutex.h"
#include "mozilla/ReentrantMonitor.h"  // for ReentrantMonitor, etc
#include "mozilla/RefPtr.h"            // for already_AddRefed
#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/Rect.h"
#include "mozilla/layers/CompositableForwarder.h"
#include "mozilla/layers/CompositorTypes.h"
#include "mozilla/layers/PImageBridgeChild.h"
#include "mozilla/layers/TextureForwarder.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "nsRegion.h"  // for nsIntRegion

namespace mozilla {
namespace ipc {
class Shmem;
}  // namespace ipc

namespace layers {

class ImageClient;
class ImageContainer;
class ImageContainerListener;
class ImageBridgeParent;
class CompositableClient;
struct CompositableTransaction;
class Image;
class TextureClient;
class SynchronousTask;

/**
 * Returns true if the current thread is the ImageBrdigeChild's thread.
 *
 * Can be called from any thread.
 */
bool InImageBridgeChildThread();

/**
 * The ImageBridge protocol is meant to allow ImageContainers to forward images
 * directly to the compositor thread/process without using the main thread.
 *
 * ImageBridgeChild is a CompositableForwarder just like ShadowLayerForwarder.
 * This means it also does transactions with the compositor thread/process,
 * except that the transactions are restricted to operations on the
 * Compositables and cannot contain messages affecting layers directly.
 *
 * ImageBridgeChild is also a ISurfaceAllocator. It can be used to allocate or
 * deallocate data that is shared with the compositor. The main differerence
 * with other ISurfaceAllocators is that some of its overriden methods can be
 * invoked from any thread.
 *
 * There are three important phases in the ImageBridge protocol. These three
 * steps can do different things depending if (A) the ImageContainer uses
 * ImageBridge or (B) it does not use ImageBridge:
 *
 * - When an ImageContainer calls its method SetCurrentImage:
 *   - (A) The image is sent directly to the compositor process through the
 *   ImageBridge IPDL protocol.
 *   On the compositor side the image is stored in a global table that
 *   associates the image with an ID corresponding to the ImageContainer, and a
 *   composition is triggered.
 *   - (B) Since it does not have an ImageBridge, the image is not sent yet.
 *   instead the will be sent to the compositor during the next layer
 *   transaction (on the main thread).
 *
 * - During a Layer transaction:
 *   - (A) The ImageContainer uses ImageBridge. The image is already available
 *   to the compositor process because it has been sent with SetCurrentImage.
 *   Yet, the CompositableHost on the compositor side will needs the ID
 *   referring to the ImageContainer to access the Image. So during the Swap
 *   operation that happens in the transaction, we swap the container ID rather
 *   than the image data.
 *   - (B) Since the ImageContainer does not use ImageBridge, the image data is
 *   swaped.
 *
 * - During composition:
 *   - (A) The CompositableHost has an AsyncID, it looks up the ID in the
 *   global table to see if there is an image. If there is no image, nothing is
 *   rendered.
 *   - (B) The CompositableHost has image data rather than an ID (meaning it is
 *   not using ImageBridge), then it just composites the image data normally.
 *
 * This means that there might be a possibility for the ImageBridge to send the
 * first frame before the first layer transaction that will pass the container
 * ID to the CompositableHost happens. In this (unlikely) case the layer is not
 * composited until the layer transaction happens. This means this scenario is
 * not harmful.
 *
 * Since sending an image through imageBridge triggers compositing, the main
 * thread is not used at all (except for the very first transaction that
 * provides the CompositableHost with an AsyncID).
 */
class ImageBridgeChild final : public PImageBridgeChild,
                               public CompositableForwarder,
                               public TextureForwarder {
  friend class ImageContainer;

  typedef nsTArray<AsyncParentMessageData> AsyncParentMessageArray;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ImageBridgeChild, override);

  RefPtr<TextureForwarder> GetTextureForwarder() override { return this; }
  LayersIPCActor* GetLayersIPCActor() override { return this; }

  /**
   * Creates the image bridge with a dedicated thread for ImageBridgeChild.
   *
   * We may want to use a specifi thread in the future. In this case, use
   * CreateWithThread instead.
   */
  static void InitSameProcess(uint32_t aNamespace);

  static void InitWithGPUProcess(Endpoint<PImageBridgeChild>&& aEndpoint,
                                 uint32_t aNamespace);
  static bool InitForContent(Endpoint<PImageBridgeChild>&& aEndpoint,
                             uint32_t aNamespace);
  static bool ReinitForContent(Endpoint<PImageBridgeChild>&& aEndpoint,
                               uint32_t aNamespace);

  /**
   * Destroys the image bridge by calling DestroyBridge, and destroys the
   * ImageBridge's thread.
   *
   * If you don't want to destroy the thread, call DestroyBridge directly
   * instead.
   */
  static void ShutDown();

  /**
   * returns the singleton instance.
   *
   * can be called from any thread.
   */
  static RefPtr<ImageBridgeChild> GetSingleton();

  static void IdentifyCompositorTextureHost(
      const TextureFactoryIdentifier& aIdentifier);

  void BeginTransaction();
  void EndTransaction();

  FixedSizeSmallShmemSectionAllocator* GetTileLockAllocator() override;

  /**
   * Returns the ImageBridgeChild's thread.
   *
   * Can be called from any thread.
   */
  nsISerialEventTarget* GetThread() const override;

  void SyncWithCompositor(
      const Maybe<uint64_t>& aWindowID = Nothing()) override;

  already_AddRefed<PTextureChild> AllocPTextureChild(
      const SurfaceDescriptor& aSharedData, ReadLockDescriptor& aReadLock,
      const LayersBackend& aLayersBackend, const TextureFlags& aFlags,
      const uint64_t& aSerial,
      const wr::MaybeExternalImageId& aExternalImageId);

  PMediaSystemResourceManagerChild* AllocPMediaSystemResourceManagerChild();
  bool DeallocPMediaSystemResourceManagerChild(
      PMediaSystemResourceManagerChild* aActor);

  mozilla::ipc::IPCResult RecvParentAsyncMessages(
      nsTArray<AsyncParentMessageData>&& aMessages);

  mozilla::ipc::IPCResult RecvDidComposite(
      nsTArray<ImageCompositeNotification>&& aNotifications);

  mozilla::ipc::IPCResult RecvReportFramesDropped(
      const CompositableHandle& aHandle, const uint32_t& aFrames);

  // Create an ImageClient from any thread.
  RefPtr<ImageClient> CreateImageClient(CompositableType aType,
                                        ImageContainer* aImageContainer);

  // Create an ImageClient from the ImageBridge thread.
  RefPtr<ImageClient> CreateImageClientNow(CompositableType aType,
                                           ImageContainer* aImageContainer);

  void UpdateImageClient(RefPtr<ImageContainer> aContainer);

  void UpdateCompositable(const RefPtr<ImageContainer> aContainer,
                          const RemoteTextureId aTextureId,
                          const RemoteTextureOwnerId aOwnerId,
                          const gfx::IntSize aSize, const TextureFlags aFlags,
                          const RefPtr<FwdTransactionTracker> aTracker);

  /**
   * Clear Images in host.
   */
  void ClearImagesInHost(ImageClient* aClient, ImageContainer* aContainer,
                         ClearImagesType aType);

  void WaitFlushTasks();

  bool IPCOpen() const override { return mCanSend; }

 private:
  /**
   * This must be called by the static function DeleteImageBridgeSync defined
   * in ImageBridgeChild.cpp ONLY.
   */
  virtual ~ImageBridgeChild();

  // Helpers for dispatching.
  void CreateImageClientSync(SynchronousTask* aTask,
                             RefPtr<ImageClient>* result,
                             CompositableType aType,
                             ImageContainer* aImageContainer);

  void ClearImagesInHostSync(SynchronousTask* aTask, ImageClient* aClient,
                             ImageContainer* aContainer, ClearImagesType aType);

  void ProxyAllocShmemNow(SynchronousTask* aTask, size_t aSize,
                          mozilla::ipc::Shmem* aShmem, bool aUnsafe,
                          bool* aSuccess);
  void ProxyDeallocShmemNow(SynchronousTask* aTask, mozilla::ipc::Shmem* aShmem,
                            bool* aResult);

  void UpdateTextureFactoryIdentifier(
      const TextureFactoryIdentifier& aIdentifier);

 public:
  // CompositableForwarder

  void Connect(CompositableClient* aCompositable,
               ImageContainer* aImageContainer) override;

  bool UsesImageBridge() const override { return true; }

  /**
   * See CompositableForwarder::UseTextures
   */
  void UseTextures(CompositableClient* aCompositable,
                   const nsTArray<TimedTextureClient>& aTextures) override;

  void UseRemoteTexture(CompositableClient* aCompositable,
                        const RemoteTextureId aTextureId,
                        const RemoteTextureOwnerId aOwnerId,
                        const gfx::IntSize aSize, const TextureFlags aFlags,
                        const RefPtr<FwdTransactionTracker>& aTracker) override;

  void ReleaseCompositable(const CompositableHandle& aHandle) override;

  void ForgetImageContainer(const CompositableHandle& aHandle);

  /**
   * Hold TextureClient ref until end of usage on host side if
   * TextureFlags::RECYCLE is set. Host side's usage is checked via
   * CompositableRef.
   */
  void HoldUntilCompositableRefReleasedIfNecessary(TextureClient* aClient);

  /**
   * Notify id of Texture When host side end its use. Transaction id is used to
   * make sure if there is no newer usage.
   */
  void NotifyNotUsed(uint64_t aTextureId, uint64_t aFwdTransactionId);

  void CancelWaitForNotifyNotUsed(uint64_t aTextureId) override;

  bool DestroyInTransaction(PTextureChild* aTexture) override;
  bool DestroyInTransaction(const CompositableHandle& aHandle);

  void RemoveTextureFromCompositable(CompositableClient* aCompositable,
                                     TextureClient* aTexture) override;

  void ClearImagesFromCompositable(CompositableClient* aCompositable,
                                   ClearImagesType aType) override;

  // ISurfaceAllocator

  /**
   * See ISurfaceAllocator.h
   * Can be used from any thread.
   * If used outside the ImageBridgeChild thread, it will proxy a synchronous
   * call on the ImageBridgeChild thread.
   */
  bool AllocUnsafeShmem(size_t aSize, mozilla::ipc::Shmem* aShmem) override;
  bool AllocShmem(size_t aSize, mozilla::ipc::Shmem* aShmem) override;

  /**
   * See ISurfaceAllocator.h
   * Can be used from any thread.
   * If used outside the ImageBridgeChild thread, it will proxy a synchronous
   * call on the ImageBridgeChild thread.
   */
  bool DeallocShmem(mozilla::ipc::Shmem& aShmem) override;

  already_AddRefed<PTextureChild> CreateTexture(
      const SurfaceDescriptor& aSharedData, ReadLockDescriptor&& aReadLock,
      LayersBackend aLayersBackend, TextureFlags aFlags,
      const dom::ContentParentId& aContentId, uint64_t aSerial,
      wr::MaybeExternalImageId& aExternalImageId) override;

  bool IsSameProcess() const override;

  FwdTransactionCounter& GetFwdTransactionCounter() override {
    return mFwdTransactionCounter;
  }

  bool InForwarderThread() override { return InImageBridgeChildThread(); }

  void HandleFatalError(const char* aMsg) override;

  wr::MaybeExternalImageId GetNextExternalImageId() override;

 protected:
  explicit ImageBridgeChild(uint32_t aNamespace);
  bool DispatchAllocShmemInternal(size_t aSize, Shmem* aShmem, bool aUnsafe);

  void Bind(Endpoint<PImageBridgeChild>&& aEndpoint);
  void BindSameProcess(RefPtr<ImageBridgeParent> aParent);

  void SendImageBridgeThreadId();

  void WillShutdown();
  void ShutdownStep1(SynchronousTask* aTask);
  void ShutdownStep2(SynchronousTask* aTask);
  void MarkShutDown();

  void ActorDestroy(ActorDestroyReason aWhy) override;

  bool CanSend() const;
  bool CanPostTask() const;

  static void ShutdownSingleton();

 private:
  uint32_t mNamespace;

  CompositableTransaction* mTxn;
  UniquePtr<FixedSizeSmallShmemSectionAllocator> mSectionAllocator;

  mozilla::Atomic<bool> mCanSend;
  mozilla::Atomic<bool> mDestroyed;

  /**
   * Transaction id of CompositableForwarder.
   * It is incrementaed by UpdateFwdTransactionId() in each BeginTransaction()
   * call.
   */
  FwdTransactionCounter mFwdTransactionCounter;

  /**
   * Hold TextureClients refs until end of their usages on host side.
   * It defer calling of TextureClient recycle callback.
   */
  std::unordered_map<uint64_t, RefPtr<TextureClient>>
      mTexturesWaitingNotifyNotUsed;

  /**
   * Mapping from async compositable IDs to image containers.
   */
  Mutex mContainerMapLock MOZ_UNANNOTATED;
  std::unordered_map<uint64_t, RefPtr<ImageContainerListener>>
      mImageContainerListeners;
  RefPtr<ImageContainerListener> FindListener(
      const CompositableHandle& aHandle);
};

}  // namespace layers
}  // namespace mozilla

#endif
