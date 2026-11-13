/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_GeckoContentController_h
#define mozilla_layers_GeckoContentController_h

#include "GeckoContentControllerTypes.h"
#include "InputData.h"              // for PinchGestureInput
#include "LayersTypes.h"            // for ScrollDirection
#include "Units.h"                  // for CSSPoint, CSSRect, etc
#include "mozilla/Attributes.h"     // for MOZ_CAN_RUN_SCRIPT
#include "mozilla/DefineEnum.h"     // for MOZ_DEFINE_ENUM
#include "mozilla/EventForwards.h"  // for Modifiers
#include "mozilla/layers/APZThreadUtils.h"
#include "mozilla/layers/MatrixMessage.h"        // for MatrixMessage
#include "mozilla/layers/ScrollableLayerGuid.h"  // for ScrollableLayerGuid, etc
#include "nsISupportsImpl.h"

namespace mozilla {

class Runnable;

namespace layers {

struct DoubleTapToZoomMetrics;
struct RepaintRequest;

class GeckoContentController {
 public:
  using APZStateChange = GeckoContentController_APZStateChange;
  using TapType = GeckoContentController_TapType;
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

  /**
   * Notifies the content side of the most recently computed transforms for
   * each layers subtree to the root. The nsTArray will contain one
   *  MatrixMessage for each layers id in the current APZ tree, along with the
   * corresponding transform.
   */
  virtual void NotifyLayerTransforms(nsTArray<MatrixMessage>&& aTransforms) = 0;

  /**
   * Requests a paint of the given RepaintRequest |aRequest| from Gecko.
   * Implementations per-platform are responsible for actually handling this.
   *
   * This method must always be called on the repaint thread, which depends
   * on the GeckoContentController. For ChromeProcessController it is the
   * Gecko main thread, while for RemoteContentController it is the compositor
   * thread where it can send IPDL messages.
   */
  virtual void RequestContentRepaint(const RepaintRequest& aRequest) = 0;

  /**
   * Requests handling of a tap event. |aPoint| is in LD pixels, relative to the
   * current scroll offset.
   */
  MOZ_CAN_RUN_SCRIPT
  virtual void HandleTap(
      TapType aType, const LayoutDevicePoint& aPoint, Modifiers aModifiers,
      const ScrollableLayerGuid& aGuid, uint64_t aInputBlockId,
      const Maybe<DoubleTapToZoomMetrics>& aDoubleTapTooZoomMetrics) = 0;

  /**
   * When the apz.allow_zooming pref is set to false, the APZ will not
   * translate pinch gestures to actual zooming. Instead, it will call this
   * method to notify gecko of the pinch gesture, and allow it to deal with it
   * however it wishes. Note that this function is not called if the pinch is
   * prevented by content calling preventDefault() on the touch events, or via
   * use of the touch-action property.
   * @param aType One of PINCHGESTURE_START, PINCHGESTURE_SCALE,
   *        PINCHGESTURE_FINGERLIFTED, or PINCHGESTURE_END, indicating the phase
   *        of the pinch.
   * @param aGuid The guid of the APZ that is detecting the pinch. This is
   *        generally the root APZC for the layers id.
   * @param aFocusPoint The focus point of the pinch event.
   * @param aSpanChange For the START or END event, this is always 0.
   *        For a SCALE event, this is the difference in span between the
   *        previous state and the new state.
   * @param aModifiers The keyboard modifiers depressed during the pinch.
   */
  virtual void NotifyPinchGesture(PinchGestureInput::PinchGestureType aType,
                                  const ScrollableLayerGuid& aGuid,
                                  const LayoutDevicePoint& aFocusPoint,
                                  LayoutDeviceCoord aSpanChange,
                                  Modifiers aModifiers) = 0;

  /**
   * Schedules a runnable to run on the controller thread at some time
   * in the future.
   * This method must always be called on the controller thread.
   */
  virtual void PostDelayedTask(already_AddRefed<Runnable> aRunnable,
                               int aDelayMs) {
    APZThreadUtils::DelayedDispatch(std::move(aRunnable), aDelayMs);
  }

  /**
   * Returns true if we are currently on the thread that can send repaint
   * requests.
   */
  virtual bool IsRepaintThread() = 0;

  /**
   * Runs the given task on the "repaint" thread.
   */
  virtual void DispatchToRepaintThread(already_AddRefed<Runnable> aTask) = 0;

  /**
   * General notices of APZ state changes for consumers.
   * |aGuid| identifies the APZC originating the state change.
   * |aChange| identifies the type of state change
   * |aArg| is used by some state changes to pass extra information (see
   *        the documentation for each state change above)
   * |aInputBlockId| is populated for the |eStartTouch| and |eEndTouch|
   *                 state changes and identifies the input block of the
   *                 gesture that triggers the state change.
   */
  virtual void NotifyAPZStateChange(const ScrollableLayerGuid& aGuid,
                                    APZStateChange aChange, int aArg = 0,
                                    Maybe<uint64_t> aInputBlockId = Nothing()) {
  }

  /**
   * Notify content of a MozMouseScrollFailed event.
   */
  MOZ_CAN_RUN_SCRIPT virtual void NotifyMozMouseScrollEvent(
      const ScrollableLayerGuid::ViewID& aScrollId, const nsString& aEvent) {}

  /**
   * Notify content that the repaint requests have been flushed.
   */
  virtual void NotifyFlushComplete() = 0;

  /**
   * If the async scrollbar-drag initiation code kicks in on the APZ side, then
   * we need to let content know that we are dragging the scrollbar. Otherwise,
   * by the time the mousedown events is handled by content, the scrollthumb
   * could already have been moved via a RequestContentRepaint message at a
   * new scroll position, and the mousedown might end up triggering a click-to-
   * scroll on where the thumb used to be.
   */
  virtual void NotifyAsyncScrollbarDragInitiated(
      uint64_t aDragBlockId, const ScrollableLayerGuid::ViewID& aScrollId,
      ScrollDirection aDirection) = 0;
  virtual void NotifyAsyncScrollbarDragRejected(
      const ScrollableLayerGuid::ViewID& aScrollId) = 0;

  virtual void NotifyAsyncAutoscrollRejected(
      const ScrollableLayerGuid::ViewID& aScrollId) = 0;

  virtual void CancelAutoscroll(const ScrollableLayerGuid& aGuid) = 0;

  virtual void NotifyScaleGestureComplete(const ScrollableLayerGuid& aGuid,
                                          float aScale) = 0;

  /**
   * Following functions with empty implementations are Android-specific
   * and handled in the parent process. They are overridden by
   * AndroidContentController in the parent process,
   * and by RemoteContentController for the purpose of routing them from
   * the compositor process to the parent process.
   */
  virtual void UpdateOverscrollVelocity(const ScrollableLayerGuid& aGuid,
                                        float aX, float aY,
                                        bool aIsRootContent) {}
  virtual void UpdateOverscrollOffset(const ScrollableLayerGuid& aGuid,
                                      float aX, float aY, bool aIsRootContent) {
  }
  virtual void HideDynamicToolbar(const ScrollableLayerGuid& aGuid) {}

  /**
   * Notify the widget of the directions in which a touch swipe-to-navigate
   * gesture is allowed for the input block |aInputBlockId|, once the
   * information needed to decide it is available. Only called on the
   * controller for the root layers id. Handled in the parent process by
   * ChromeProcessController. In the GPU process, RemoteContentController
   * forwards it to the parent process.
   */
  virtual void NotifyTouchSwipableDirections(uint64_t aInputBlockId,
                                             SideBits aSwipableDirections) {}

  GeckoContentController() = default;

  /**
   * Needs to be called on the main thread.
   */
  virtual void Destroy() {}

  /**
   * Whether this is RemoteContentController.
   */
  virtual bool IsRemote() { return false; }

  virtual PresShell* GetTopLevelPresShell() const { return nullptr; };

 protected:
  // Protected destructor, to discourage deletion outside of Release():
  virtual ~GeckoContentController() = default;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_GeckoContentController_h
