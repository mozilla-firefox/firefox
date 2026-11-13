/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_ChromeProcessController_h
#define mozilla_layers_ChromeProcessController_h

#include "mozilla/RefPtr.h"
#include "mozilla/layers/GeckoContentController.h"
#include "mozilla/layers/MatrixMessage.h"
#include "nsCOMPtr.h"

class nsIDOMWindowUtils;
class nsISerialEventTarget;
class nsIWidget;

namespace mozilla {
class PresShell;
namespace dom {
class Document;
}

namespace layers {

class IAPZCTreeManager;
class APZEventState;
struct DoubleTapToZoomMetrics;

/**
 * ChromeProcessController is a GeckoContentController attached to the root of
 * a compositor's layer tree. It's used directly by APZ by default, and remoted
 * using PAPZ if there is a gpu process.
 *
 * If ChromeProcessController needs to implement a new method on
 * GeckoContentController PAPZ, APZChild, and RemoteContentController must be
 * updated to handle it.
 */
class ChromeProcessController : public mozilla::layers::GeckoContentController {
 protected:
  typedef mozilla::layers::FrameMetrics FrameMetrics;
  typedef mozilla::layers::ScrollableLayerGuid ScrollableLayerGuid;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ChromeProcessController, final);

  explicit ChromeProcessController(nsIWidget* aWidget,
                                   APZEventState* aAPZEventState,
                                   IAPZCTreeManager* aAPZCTreeManager);
  void Destroy() override;

  // GeckoContentController interface
  void NotifyLayerTransforms(nsTArray<MatrixMessage>&& aTransforms) override;
  void RequestContentRepaint(const RepaintRequest& aRequest) override;
  bool IsRepaintThread() override;
  void DispatchToRepaintThread(already_AddRefed<Runnable> aTask) override;
  MOZ_CAN_RUN_SCRIPT
  void HandleTap(
      TapType aType, const mozilla::LayoutDevicePoint& aPoint,
      Modifiers aModifiers, const ScrollableLayerGuid& aGuid,
      uint64_t aInputBlockId,
      const Maybe<DoubleTapToZoomMetrics>& aDoubleTapToZoomMetrics) override;
  void NotifyPinchGesture(PinchGestureInput::PinchGestureType aType,
                          const ScrollableLayerGuid& aGuid,
                          const LayoutDevicePoint& aFocusPoint,
                          LayoutDeviceCoord aSpanChange,
                          Modifiers aModifiers) override;
  void NotifyAPZStateChange(const ScrollableLayerGuid& aGuid,
                            APZStateChange aChange, int aArg,
                            Maybe<uint64_t> aInputBlockId) override;
  MOZ_CAN_RUN_SCRIPT void NotifyMozMouseScrollEvent(
      const ScrollableLayerGuid::ViewID& aScrollId,
      const nsString& aEvent) override;
  void NotifyFlushComplete() override;
  void NotifyAsyncScrollbarDragInitiated(
      uint64_t aDragBlockId, const ScrollableLayerGuid::ViewID& aScrollId,
      ScrollDirection aDirection) override;
  void NotifyAsyncScrollbarDragRejected(
      const ScrollableLayerGuid::ViewID& aScrollId) override;
  void NotifyAsyncAutoscrollRejected(
      const ScrollableLayerGuid::ViewID& aScrollId) override;
  void CancelAutoscroll(const ScrollableLayerGuid& aGuid) override;
  void NotifyScaleGestureComplete(const ScrollableLayerGuid& aGuid,
                                  float aScale) override;
  void NotifyTouchSwipableDirections(uint64_t aInputBlockId,
                                     SideBits aSwipableDirections) override;

  PresShell* GetTopLevelPresShell() const override { return GetPresShell(); }

 protected:
  virtual ~ChromeProcessController();

 private:
  nsCOMPtr<nsIWidget> mWidget;
  RefPtr<APZEventState> mAPZEventState;
  RefPtr<IAPZCTreeManager> mAPZCTreeManager;
  nsCOMPtr<nsISerialEventTarget> mUIThread;

  void InitializeRoot();
  PresShell* GetPresShell() const;
  dom::Document* GetRootDocument() const;
  dom::Document* GetRootContentDocument(
      const ScrollableLayerGuid::ViewID& aScrollId) const;
  void HandleDoubleTap(const mozilla::CSSPoint& aPoint, Modifiers aModifiers,
                       const ScrollableLayerGuid& aGuid,
                       const DoubleTapToZoomMetrics& aDoubleTapToZoomMetrics);
  nsIFrame* GetWidgetFrame() const;
};

}  // namespace layers
}  // namespace mozilla

#endif /* mozilla_layers_ChromeProcessController_h */
