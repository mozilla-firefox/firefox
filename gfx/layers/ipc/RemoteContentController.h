/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_RemoteContentController_h
#define mozilla_layers_RemoteContentController_h

#include "mozilla/layers/GeckoContentController.h"
#include "mozilla/layers/PAPZParent.h"

namespace mozilla {

namespace dom {
class BrowserParent;
}

namespace layers {

struct DoubleTapToZoomMetrics;
/**
 * RemoteContentController implements PAPZChild and is used to access a
 * GeckoContentController that lives in a different process.
 *
 * RemoteContentController lives on the compositor thread. All methods can
 * be called off the compositor thread and will get dispatched to the right
 * thread, with the exception of RequestContentRepaint and NotifyFlushComplete,
 * which must be called on the repaint thread, which in this case is the
 * compositor thread.
 */
class RemoteContentController final : public GeckoContentController,
                                      public PAPZParent {
  using GeckoContentController::APZStateChange;
  using GeckoContentController::TapType;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RemoteContentController, final);

  explicit RemoteContentController(const LayersId& aLayersId);

  void NotifyLayerTransforms(nsTArray<MatrixMessage>&& aTransforms) override;

  void RequestContentRepaint(const RepaintRequest& aRequest) override;

  MOZ_CAN_RUN_SCRIPT void HandleTap(
      TapType aTapType, const LayoutDevicePoint& aPoint, Modifiers aModifiers,
      const ScrollableLayerGuid& aGuid, uint64_t aInputBlockId,
      const Maybe<DoubleTapToZoomMetrics>& aDoubleTapToZoomMetrics) override;

  void NotifyPinchGesture(PinchGestureInput::PinchGestureType aType,
                          const ScrollableLayerGuid& aGuid,
                          const LayoutDevicePoint& aFocusPoint,
                          LayoutDeviceCoord aSpanChange,
                          Modifiers aModifiers) override;

  bool IsRepaintThread() override;

  void DispatchToRepaintThread(already_AddRefed<Runnable> aTask) override;

  void NotifyAPZStateChange(const ScrollableLayerGuid& aGuid,
                            APZStateChange aChange, int aArg,
                            Maybe<uint64_t> aInputBlockId) override;

  void UpdateOverscrollVelocity(const ScrollableLayerGuid& aGuid, float aX,
                                float aY, bool aIsRootContent) override;

  void UpdateOverscrollOffset(const ScrollableLayerGuid& aGuid, float aX,
                              float aY, bool aIsRootContent) override;

  void HideDynamicToolbar(const ScrollableLayerGuid& aGuid) override;

  void NotifyTouchSwipableDirections(uint64_t aInputBlockId,
                                     SideBits aSwipableDirections) override;

  void NotifyMozMouseScrollEvent(const ScrollableLayerGuid::ViewID& aScrollId,
                                 const nsString& aEvent) override;

  void NotifyFlushComplete() override;

  void NotifyAsyncScrollbarDragInitiated(
      uint64_t aDragBlockId, const ScrollableLayerGuid::ViewID& aScrollId,
      ScrollDirection aDirection) override;
  void NotifyAsyncScrollbarDragRejected(
      const ScrollableLayerGuid::ViewID& aScrollId) override;

  void NotifyAsyncAutoscrollRejected(
      const ScrollableLayerGuid::ViewID& aScrollId) override;

  void CancelAutoscroll(const ScrollableLayerGuid& aScrollId) override;

  void NotifyScaleGestureComplete(const ScrollableLayerGuid& aGuid,
                                  float aScale) override;

  void ActorDestroy(ActorDestroyReason aWhy) override;

  void Destroy() override;
  mozilla::ipc::IPCResult RecvDestroy();

  bool IsRemote() override;

 private:
  virtual ~RemoteContentController();

  const LayersId mLayersId;
  nsCOMPtr<nsISerialEventTarget> mCompositorThread;
  bool mCanSend;

  MOZ_CAN_RUN_SCRIPT void HandleTapOnParentProcessMainThread(
      TapType aTapType, LayoutDevicePoint aPoint, Modifiers aModifiers,
      ScrollableLayerGuid aGuid, uint64_t aInputBlockId,
      const Maybe<DoubleTapToZoomMetrics>& aDoubleTapToZoomMetrics);
  void HandleTapOnGPUProcessMainThread(
      TapType aTapType, LayoutDevicePoint aPoint, Modifiers aModifiers,
      ScrollableLayerGuid aGuid, uint64_t aInputBlockId,
      const Maybe<DoubleTapToZoomMetrics>& aDoubleTapToZoomMetrics);
  void NotifyPinchGestureOnGPUProcessMainThread(
      PinchGestureInput::PinchGestureType aType,
      const ScrollableLayerGuid& aGuid, const LayoutDevicePoint& aFocusPoint,
      LayoutDeviceCoord aSpanChange, Modifiers aModifiers);

  void CancelAutoscrollInProcess(const ScrollableLayerGuid& aScrollId);
  void CancelAutoscrollCrossProcess(const ScrollableLayerGuid& aScrollId);
  void NotifyScaleGestureCompleteInProcess(const ScrollableLayerGuid& aGuid,
                                           float aScale);
  void NotifyScaleGestureCompleteCrossProcess(const ScrollableLayerGuid& aGuid,
                                              float aScale);
};

}  // namespace layers

}  // namespace mozilla

#endif  // mozilla_layers_RemoteContentController_h
