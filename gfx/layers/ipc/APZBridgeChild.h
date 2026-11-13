/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_APZBridgeChild_h
#define mozilla_layers_APZBridgeChild_h

#include "mozilla/layers/APZBridge.h"
#include "mozilla/layers/GeckoContentControllerTypes.h"
#include "mozilla/layers/PAPZBridgeChild.h"

namespace mozilla {
namespace layers {

class RemoteCompositorSession;

class APZBridgeChild : public PAPZBridgeChild, public APZBridge {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(APZBridgeChild, final)
  using TapType = GeckoContentController_TapType;

 public:
  static RefPtr<APZBridgeChild> Create(const uint64_t& aProcessToken,
                                       Endpoint<PAPZBridgeChild>&& aEndpoint);

  void Destroy();

  void SetCompositorSession(RemoteCompositorSession* aSession);

  APZEventResult ReceiveInputEvent(
      InputData& aEvent,
      InputBlockCallback&& aCallback = InputBlockCallback()) override;

  void SetKeyboardMap(const KeyboardMap& aKeyboardMap) override;

  void SetDPI(float aDpiValue) override;

  void SetBrowserGestureResponse(uint64_t aInputBlockId,
                                 BrowserGestureResponse aResponse) override;

  void StartAutoscroll(const ScrollableLayerGuid& aGuid,
                       const ScreenPoint& aAnchorLocation) override;

  void StopAutoscroll(const ScrollableLayerGuid& aGuid) override;

  void SetLongTapEnabled(bool aTapGestureEnabled) override;

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvHandleTap(
      const TapType& aType, const LayoutDevicePoint& aPoint,
      const Modifiers& aModifiers, const ScrollableLayerGuid& aGuid,
      const uint64_t& aInputBlockId,
      const Maybe<DoubleTapToZoomMetrics>& aDoubleTapToZoomMetrics);

  mozilla::ipc::IPCResult RecvCallInputBlockCallback(
      uint64_t aInputBlockId, const APZHandledResult& handledResult);

  mozilla::ipc::IPCResult RecvNotifyPinchGesture(
      const PinchGestureType& aType, const ScrollableLayerGuid& aGuid,
      const LayoutDevicePoint& aFocusPoint,
      const LayoutDeviceCoord& aSpanChange, const Modifiers& aModifiers);

  mozilla::ipc::IPCResult RecvCancelAutoscroll(
      const ScrollableLayerGuid::ViewID& aScrollId);

  mozilla::ipc::IPCResult RecvNotifyScaleGestureComplete(
      const ScrollableLayerGuid::ViewID& aScrollId, float aScale);

  mozilla::ipc::IPCResult RecvLayerTransforms(
      nsTArray<MatrixMessage>&& aTransforms);

  mozilla::ipc::IPCResult RecvUpdateOverscrollVelocity(
      const ScrollableLayerGuid& aGuid, const float& aX, const float& aY,
      const bool& aIsRootContent);

  mozilla::ipc::IPCResult RecvUpdateOverscrollOffset(
      const ScrollableLayerGuid& aGuid, const float& aX, const float& aY,
      const bool& aIsRootContent);

  mozilla::ipc::IPCResult RecvHideDynamicToolbar();

  mozilla::ipc::IPCResult RecvNotifyTouchSwipableDirections(
      const uint64_t& aInputBlockId, const SideBits& aSwipableDirections);

 protected:
  void ProcessUnhandledEvent(LayoutDeviceIntPoint* aRefPoint,
                             ScrollableLayerGuid* aOutTargetGuid,
                             uint64_t* aOutFocusSequenceNumber,
                             LayersId* aOutLayersId) override;

  void UpdateWheelTransaction(
      LayoutDeviceIntPoint aRefPoint, EventMessage aEventMessage,
      const Maybe<ScrollableLayerGuid>& aTargetGuid) override;

  void ActorDestroy(ActorDestroyReason aWhy) override;

  explicit APZBridgeChild(const uint64_t& aProcessToken);
  virtual ~APZBridgeChild();

 private:
  void Open(Endpoint<PAPZBridgeChild>&& aEndpoint);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  void HandleTapOnMainThread(
      const TapType& aType, const LayoutDevicePoint& aPoint,
      const Modifiers& aModifiers, const ScrollableLayerGuid& aGuid,
      const uint64_t& aInputBlockId,
      const Maybe<DoubleTapToZoomMetrics>& aDoubleTapToZoomMetrics);

  void NotifyPinchGestureOnMainThread(const PinchGestureType& aType,
                                      const LayoutDevicePoint& aFocusPoint,
                                      const LayoutDeviceCoord& aSpanChange,
                                      const Modifiers& aModifiers);

  void NotifyScaleGestureCompleteOnMainThread(float aScale);

  void NotifyLayerTransformsOnMainThread(nsTArray<MatrixMessage>&& aTransforms);

  void UpdateOverscrollVelocityOnMainThread(const ScrollableLayerGuid& aGuid,
                                            float aX, float aY,
                                            bool aIsRootContent);

  void UpdateOverscrollOffsetOnMainThread(const ScrollableLayerGuid& aGuid,
                                          float aX, float aY,
                                          bool aIsRootContent);

  void HideDynamicToolbarOnMainThread();

  void NotifyTouchSwipableDirectionsOnMainThread(uint64_t aInputBlockId,
                                                 SideBits aSwipableDirections);

  // Returns null if the compositor session has already been torn down.
  GeckoContentController* GetContentController();

  bool mIsOpen;
  uint64_t mProcessToken;
  // Currently, this can only be used by the main thread
  MOZ_NON_OWNING_REF RemoteCompositorSession* mCompositorSession = nullptr;

  using InputBlockCallbackMap =
      std::unordered_map<uint64_t, InputBlockCallback>;
  InputBlockCallbackMap mInputBlockCallbacks;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_APZBridgeChild_h
