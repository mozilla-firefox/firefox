/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_CompositorBridgeChild_h
#define mozilla_layers_CompositorBridgeChild_h

#include <unordered_map>

#include "base/basictypes.h"  // for DISALLOW_EVIL_CONSTRUCTORS
#include "mozilla/Monitor.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/layers/PCompositorBridgeChild.h"
#include "mozilla/layers/TextureForwarder.h"  // for TextureForwarder
#include "mozilla/webrender/WebRenderTypes.h"
#include "nsCOMPtr.h"          // for nsCOMPtr
#include "nsClassHashtable.h"  // for nsClassHashtable
#include "nsHashKeys.h"        // for nsUint64HashKey
#include "nsISupportsImpl.h"   // for NS_INLINE_DECL_REFCOUNTING
#include "nsIWeakReferenceUtils.h"
#include "nsStringFwd.h"

class nsIWidget;

namespace mozilla {

namespace dom {
class BrowserChild;
}  // namespace dom

namespace widget {
class CompositorWidget;
}  // namespace widget

namespace layers {

using mozilla::dom::BrowserChild;

class IAPZCTreeManager;
class APZCTreeManagerChild;
class CanvasChild;
class CompositorBridgeParent;
class CompositorManagerChild;
class CompositorOptions;
class WebRenderLayerManager;
class TextureClient;
struct FrameMetrics;
struct FwdTransactionCounter;

class CompositorBridgeChild final : public PCompositorBridgeChild,
                                    public TextureForwarder {
  typedef nsTArray<AsyncParentMessageData> AsyncParentMessageArray;

  friend class PCompositorBridgeChild;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CompositorBridgeChild, override);

  explicit CompositorBridgeChild(CompositorManagerChild* aManager);

  /**
   * Initialize the singleton compositor bridge for a content process.
   */
  void InitForContent(uint32_t aNamespace);

  void InitForWidget(uint64_t aProcessToken, uint32_t aNamespace);

  // Creates a layer manager for this compositor bridge. Must only be called
  // once, and only on widget compositor bridges.
  RefPtr<WebRenderLayerManager> CreateLayerManager(nsIWidget* aWidget,
                                                   wr::PipelineId aPipelineId,
                                                   nsCString& aError);

  void Destroy();

  static CompositorBridgeChild* Get();

  // Returns whether the compositor is in the GPU process (false if in the UI
  // process). This may only be called on the main thread.
  static bool CompositorIsInGPUProcess();

  mozilla::ipc::IPCResult RecvDidComposite(
      const LayersId& aId, const nsTArray<TransactionId>& aTransactionIds,
      const TimeStamp& aCompositeStart, const TimeStamp& aCompositeEnd);

  mozilla::ipc::IPCResult RecvNotifyFrameStats(
      nsTArray<FrameStats>&& aFrameStats);

  mozilla::ipc::IPCResult RecvNotifyJankedAnimations(
      const LayersId& aLayersId, nsTArray<uint64_t>&& aJankedAnimations);

  mozilla::ipc::IPCResult RecvParentAsyncMessages(
      nsTArray<AsyncParentMessageData>&& aMessages);
  already_AddRefed<PTextureChild> CreateTexture(
      const SurfaceDescriptor& aSharedData, ReadLockDescriptor&& aReadLock,
      LayersBackend aLayersBackend, TextureFlags aFlags,
      const dom::ContentParentId& aContentId, uint64_t aSerial,
      wr::MaybeExternalImageId& aExternalImageId) override;

  already_AddRefed<CanvasChild> GetCanvasChild() final;

  void EndCanvasTransaction();

  /**
   * Release resources until they are next required.
   */
  void ClearCachedResources();

  // Beware that these methods don't override their super-class equivalent
  // (which are not virtual), they just overload them. All of these Send*
  // methods just add a sanity check (that it is not too late send a message)
  // and forward the call to the super-class's equivalent method. This means
  // that it is correct to call directly the super-class methods, but you won't
  // get the extra safety provided here.
  bool SendWillClose();
  bool SendPause();
  bool SendResume();
  bool SendResumeAsync();
  bool SendAdoptChild(const LayersId& id, const LayersId& embedderId);
  bool SendFlushRendering(const wr::RenderReasons& aReasons);
  bool SendFlushRenderingAsync(const wr::RenderReasons& aReasons);

  /**
   * This can be used, sparingly, to force all flush rendering to be
   * synchronous. This should only be done temporarily, as we want almost
   * all flushes to be async. It is intended to be used for animations
   * that rely on repeated small changes to scene rebuilds. These look
   * better with a consistent frame rate and sync flushes will help
   * generate a stable frame rate.
   */
  void SetForceSyncFlushRendering(bool aForceSyncFlushRendering);

  bool SendStartFrameTimeRecording(const uint32_t& bufferSize,
                                   uint32_t* startIndex);
  bool SendStopFrameTimeRecording(const uint32_t& startIndex,
                                  nsTArray<float>* intervals);
  bool IsSameProcess() const override;

  bool IPCOpen() const override { return mCanSend; }

  bool IsPaused() const { return mPaused; }

  static void ShutDown();

  FwdTransactionCounter& GetFwdTransactionCounter();

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

  FixedSizeSmallShmemSectionAllocator* GetTileLockAllocator() override;

  nsISerialEventTarget* GetThread() const override { return mThread; }

  bool AllocUnsafeShmem(size_t aSize, mozilla::ipc::Shmem* aShmem) override;
  bool AllocShmem(size_t aSize, mozilla::ipc::Shmem* aShmem) override;
  bool DeallocShmem(mozilla::ipc::Shmem& aShmem) override;

  already_AddRefed<PAPZCTreeManagerChild> AllocPAPZCTreeManagerChild(
      const LayersId& aLayersId);
  already_AddRefed<PAPZChild> AllocPAPZChild(const LayersId& aLayersId);

  wr::MaybeExternalImageId GetNextExternalImageId() override;

  wr::PipelineId GetNextPipelineId();

 private:
  // Private destructor, to discourage deletion outside of Release():
  virtual ~CompositorBridgeChild();

  // Must only be called from the paint thread. If the main thread is delaying
  // IPC messages, this forwards all such delayed IPC messages to the I/O thread
  // and resumes IPC.
  void ResumeIPCAfterAsyncPaint();

  void PrepareFinalDestroy();
  void AfterDestroy();

  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvObserveLayersUpdate(const LayersId& aLayersId,
                                                  const bool& aActive);

  mozilla::ipc::IPCResult RecvCompositorOptionsChanged(
      const LayersId& aLayersId, const CompositorOptions& aNewOptions);

  uint64_t GetNextResourceId();

  RefPtr<CompositorManagerChild> mCompositorManager;

  RefPtr<WebRenderLayerManager> mLayerManager;

  uint32_t mIdNamespace;
  uint32_t mResourceId;

  // When not multi-process, hold a reference to the CompositorBridgeParent to
  // keep it alive. This reference should be null in multi-process.
  RefPtr<CompositorBridgeParent> mCompositorBridgeParent;

  DISALLOW_EVIL_CONSTRUCTORS(CompositorBridgeChild);

  // True until the beginning of the two-step shutdown sequence of this actor.
  bool mCanSend;

  // False until the actor is destroyed.
  bool mActorDestroyed;

  bool mPaused;

  bool mForceSyncFlushRendering;

  /**
   * Hold TextureClients refs until end of their usages on host side.
   * It defer calling of TextureClient recycle callback.
   */
  std::unordered_map<uint64_t, RefPtr<TextureClient>>
      mTexturesWaitingNotifyNotUsed;

  nsCOMPtr<nsISerialEventTarget> mThread;

  uint64_t mProcessToken;

  FixedSizeSmallShmemSectionAllocator* mSectionAllocator;

  // TextureClients that must be kept alive during async painting. This
  // is only accessed on the main thread.
  nsTArray<RefPtr<TextureClient>> mTextureClientsForAsyncPaint;

  RefPtr<CanvasChild> mCanvasChild;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_CompositorBrigedChild_h
