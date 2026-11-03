/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_APZBridge_h
#define mozilla_layers_APZBridge_h

#include "Units.h"                  // for LayoutDeviceIntPoint
#include "mozilla/EventForwards.h"  // for WidgetInputEvent, nsEventStatus
#include "mozilla/layers/APZPublicUtils.h"  // for APZWheelAction, DispatchToContent
#include "mozilla/layers/LayersTypes.h"          // for ScrollDirections
#include "mozilla/layers/ScrollableLayerGuid.h"  // for ScrollableLayerGuid
#include "nsISupportsImpl.h"

namespace mozilla {

class InputData;

namespace layers {

class APZBridgeParent;
class AsyncPanZoomController;
class InputBlockState;
class KeyboardMap;
class TouchBlockState;
struct ScrollableLayerGuid;
struct TargetConfirmationFlags;
struct PointerEventsConsumableFlags;

// This enum class is used for communicating between APZ and the browser gesture
// support code. APZ needs to wait for the browser to send this response just
// like APZ waits for the content's response if there's an APZ ware event
// listener in the content process.
enum class BrowserGestureResponse : bool {
  NotConsumed = false,  // Representing the browser doesn't consume the gesture
  Consumed =
      true,  // Representing the browser has started consuming the gesture.
};

enum class APZHandledPlace : uint8_t {
  Unhandled = 0,         // we know for sure that the event will not be handled
                         // by either the root APZC or others
  HandledByRoot = 1,     // we know for sure that the event will be handled
                         // by the root content APZC
  HandledByContent = 2,  // we know for sure it will be handled by a non-root
                         // APZC or by an event listener using preventDefault()
                         // in a document
  Invalid = 3,
  Last = Invalid
};

struct APZHandledResult {
  APZHandledPlace mPlace = APZHandledPlace::Invalid;
  SideBits mScrollableDirections = SideBits::eNone;
  ScrollDirections mOverscrollDirections = ScrollDirections();

  APZHandledResult() = default;
  // A constructor for cases where we have the target of the input block this
  // event is part of, the target might be adjusted to be the root in the
  // ScrollingDownWillMoveDynamicToolbar case.
  //
  // NOTE: There's a case where |aTarget| is the APZC for the root content but
  // |aPlace| has to be `HandledByContent`, for example, the root content has
  // an event handler using preventDefault() in the callback, so call sites of
  // this function should be responsible to set a proper |aPlace|.
  APZHandledResult(APZHandledPlace aPlace,
                   const AsyncPanZoomController* aTarget,
                   bool aPopulateDirectionsForUnhandled = false);
  APZHandledResult(APZHandledPlace aPlace, SideBits aScrollableDirections,
                   ScrollDirections aOverscrollDirections)
      : mPlace(aPlace),
        mScrollableDirections(aScrollableDirections),
        mOverscrollDirections(aOverscrollDirections) {}

  bool IsHandledByContent() const {
    return mPlace == APZHandledPlace::HandledByContent;
  }
  bool IsHandledByRoot() const {
    return mPlace == APZHandledPlace::HandledByRoot;
  }
  bool operator==(const APZHandledResult& aOther) const {
    return mPlace == aOther.mPlace &&
           mScrollableDirections == aOther.mScrollableDirections &&
           mOverscrollDirections == aOther.mOverscrollDirections;
  }

  // Compute an initial (optional) APZHandledResult for an event.
  // If aDispatchToContent==false, the result is guaranteed to not be empty.
  static Maybe<APZHandledResult> Initialize(
      const AsyncPanZoomController* aInitialTarget,
      DispatchToContent aDispatchToContent);

  // Update |aHandledResult| with information specific to touch events.
  // If aDispatchToContent==false, the resulting value of |aHandledResult|
  // is guarateed to not be empty.
  static void UpdateForTouchEvent(Maybe<APZHandledResult>& aHandledResult,
                                  const InputBlockState& aBlock,
                                  PointerEventsConsumableFlags aConsumableFlags,
                                  const AsyncPanZoomController* aTarget,
                                  DispatchToContent aDispatchToContent);
};

/**
 * Represents the outcome of APZ receiving and processing an input event.
 * This is returned from APZBridge::ReceiveInputEvent() and related APIs.
 */
struct APZEventResult {
  /**
   * Creates a default result with a status of eIgnore, no block ID, and empty
   * target guid.
   */
  APZEventResult();

  /**
   * Creates a result with a status of eIgnore, no block ID, the guid of the
   * given initial target, and an APZHandledResult if we are sure the event
   * is not going to be dispatched to contents.
   */
  APZEventResult(const RefPtr<AsyncPanZoomController>& aInitialTarget,
                 TargetConfirmationFlags aFlags);

  void SetStatusAsConsumeNoDefault() {
    mStatus = nsEventStatus_eConsumeNoDefault;
  }

  void SetStatusAsIgnore() { mStatus = nsEventStatus_eIgnore; }

  // Set mStatus to nsEventStatus_eConsumeDoDefault and set mHandledResult
  // depending on |aTarget|.
  void SetStatusAsConsumeDoDefault(
      const RefPtr<AsyncPanZoomController>& aTarget);

  // Set mStatus to nsEventStatus_eConsumeDoDefault, unlike above
  // SetStatusAsConsumeDoDefault(const RefPtr<AsyncPanZoomController>&) this
  // function doesn't mutate mHandledResult.
  void SetStatusAsConsumeDoDefault() {
    mStatus = nsEventStatus_eConsumeDoDefault;
  }

  // Set mStatus to nsEventStatus_eConsumeDoDefault and set mHandledResult
  // depending on |aBlock|'s target APZC.
  void SetStatusAsConsumeDoDefault(const InputBlockState& aBlock);
  // Set mStatus and mHandledResult for a touch event which is not dropped
  // altogether (i.e. the status is not eConsumeNoDefault).
  void SetStatusForTouchEvent(const InputBlockState& aBlock,
                              TargetConfirmationFlags aFlags,
                              PointerEventsConsumableFlags aConsumableFlags,
                              const AsyncPanZoomController* aTarget);

  // Set mStatus and mHandledResult during in a stat of fast fling.
  void SetStatusForFastFling(const TouchBlockState& aBlock,
                             TargetConfirmationFlags aFlags,
                             PointerEventsConsumableFlags aConsumableFlags,
                             const AsyncPanZoomController* aTarget);

  // DO NOT USE THIS UpdateStatus DIRECTLY. THIS FUNCTION IS ONLY FOR
  // SERIALIZATION / DESERIALIZATION OF THIS STRUCT IN IPC.
  void UpdateStatus(nsEventStatus aStatus) { mStatus = aStatus; }
  nsEventStatus GetStatus() const { return mStatus; };

  // DO NOT USE THIS UpdateHandledResult DIRECTLY. THIS FUNCTION IS ONLY FOR
  // SERIALIZATION / DESERIALIZATION OF THIS STRUCT IN IPC.
  void UpdateHandledResult(const Maybe<APZHandledResult>& aHandledResult) {
    mHandledResult = aHandledResult;
  }
  const Maybe<APZHandledResult>& GetHandledResult() const {
    return mHandledResult;
  }

  bool WillHaveDelayedResult() const {
    return GetStatus() != nsEventStatus_eConsumeNoDefault &&
           !GetHandledResult();
  }

 private:
  /**
   * A status flag indicated how APZ handled the event.
   * The interpretation of each value is as follows:
   *
   * nsEventStatus_eConsumeNoDefault is returned to indicate the
   *   APZ is consuming this event and the caller should discard the event with
   *   extreme prejudice. The exact scenarios under which this is returned is
   *   implementation-dependent and may vary.
   * nsEventStatus_eIgnore is returned to indicate that the APZ code didn't
   *   use this event. This might be because it was directed at a point on
   *   the screen where there was no APZ, or because the thing the user was
   *   trying to do was not allowed. (For example, attempting to pan a
   *   non-pannable document).
   * nsEventStatus_eConsumeDoDefault is returned to indicate that the APZ
   *   code may have used this event to do some user-visible thing. Note that
   *   in some cases CONSUMED is returned even if the event was NOT used. This
   *   is because we cannot always know at the time of event delivery whether
   *   the event will be used or not. So we err on the side of sending
   *   CONSUMED when we are uncertain.
   */
  nsEventStatus mStatus;

  /**
   * This is:
   *  - set to HandledByRoot if we know for sure that the event will be handled
   *    by the root content APZC;
   *  - set to HandledByContent if we know for sure it will not be;
   *  - left empty if we are unsure.
   */
  Maybe<APZHandledResult> mHandledResult;

 public:
  /**
   * The guid of the APZC initially targeted by this event.
   * This will usually be the APZC that handles the event, but in cases
   * where the event is dispatched to content, it may end up being
   * handled by a different APZC.
   */
  ScrollableLayerGuid mTargetGuid;
  /**
   * If this event started or was added to an input block, the id of that
   * input block, otherwise InputBlockState::NO_BLOCK_ID.
   */
  uint64_t mInputBlockId;

  // Whether APZ found a horizontal scroll target.
  bool mTargetCanScrollHorizontally = false;

  SideBits mSwipableDirectionsForTouch = SideBits::eNone;
};

/**
 * This class lives in the main process, and is accessed via the controller
 * thread (which is the process main thread for desktop, and the Java UI
 * thread for Android). This class exposes some synchronous APIs to deliver
 * incoming input events to APZ and modify them in-place to unapply the APZ
 * async transform. If there is a GPU process, then this class does sync IPC
 * calls over to the GPU process in order to accomplish this. Otherwise,
 * APZCTreeManager overrides and implements these methods directly.
 */
class APZBridge {
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

 public:
  using InputBlockCallback = std::function<void(
      uint64_t aInputBlockId, const APZHandledResult& aHandledResult)>;

  /**
   * General handler for incoming input events. Manipulates the frame metrics
   * based on what type of input it is. For example, a PinchGestureEvent will
   * cause scaling. This should only be called externally to this class, and
   * must be called on the controller thread.
   *
   * This function transforms |aEvent| to have its coordinates in DOM space.
   * This is so that the event can be passed through the DOM and content can
   * handle them. The event may need to be converted to a WidgetInputEvent
   * by the caller if it wants to do this.
   *
   * @param aEvent input event object; is modified in-place
   * @param aCallback an optional callback to be invoked when the input block is
   * ready for handling,
   * @return The result of processing the event. Refer to the documentation of
   * APZEventResult and its field.
   */
  virtual APZEventResult ReceiveInputEvent(
      InputData& aEvent,
      InputBlockCallback&& aCallback = InputBlockCallback()) = 0;

  /**
   * WidgetInputEvent handler. Transforms |aEvent| (which is assumed to be an
   * already-existing instance of an WidgetInputEvent which may be an
   * WidgetTouchEvent) to have its coordinates in DOM space. This is so that the
   * event can be passed through the DOM and content can handle them.
   *
   * NOTE: Be careful of invoking the WidgetInputEvent variant. This can only be
   * called on the main thread. See widget/InputData.h for more information on
   * why we have InputData and WidgetInputEvent separated. If this function is
   * used, the controller thread must be the main thread, or undefined behaviour
   * may occur.
   * NOTE: On unix, mouse events are treated as touch and are forwarded
   * to the appropriate apz as such.
   *
   * See documentation for other ReceiveInputEvent above.
   */
  APZEventResult ReceiveInputEvent(
      WidgetInputEvent& aEvent,
      InputBlockCallback&& aCallback = InputBlockCallback());

  // Returns the kind of wheel event action, if any, that will be (or was)
  // performed by APZ. If this returns true, the event must not perform a
  // synchronous scroll.
  //
  // Even if this returns Nothing(), all wheel events in APZ-aware widgets must
  // be sent through APZ so they are transformed correctly for BrowserParent.
  static Maybe<APZWheelAction> ActionForWheelEvent(WidgetWheelEvent* aEvent);

  /**
   * Set the keyboard shortcuts to use for translating keyboard events.
   */
  virtual void SetKeyboardMap(const KeyboardMap& aKeyboardMap) = 0;

  virtual void SetDPI(float aDpiValue) = 0;

  virtual void SetBrowserGestureResponse(uint64_t aInputBlockId,
                                         BrowserGestureResponse aResponse) = 0;

  virtual void StartAutoscroll(const ScrollableLayerGuid& aGuid,
                               const ScreenPoint& aAnchorLocation) = 0;

  virtual void StopAutoscroll(const ScrollableLayerGuid& aGuid) = 0;

  /**
   * Function used to disable LongTap gestures.
   *
   * On slow running tests, drags and touch events can be misinterpreted
   * as a long tap. This allows tests to disable long tap gesture detection.
   */
  virtual void SetLongTapEnabled(bool aTapGestureEnabled) = 0;

 protected:
  friend class APZBridgeParent;

  // Methods to help process WidgetInputEvents (or manage conversion to/from
  // InputData)

  virtual void ProcessUnhandledEvent(LayoutDeviceIntPoint* aRefPoint,
                                     ScrollableLayerGuid* aOutTargetGuid,
                                     uint64_t* aOutFocusSequenceNumber,
                                     LayersId* aOutLayersId) = 0;

  virtual void UpdateWheelTransaction(
      LayoutDeviceIntPoint aRefPoint, EventMessage aEventMessage,
      const Maybe<ScrollableLayerGuid>& aTargetGuid) = 0;

  virtual ~APZBridge() = default;
};

std::ostream& operator<<(std::ostream& aOut,
                         const APZHandledResult& aHandledResult);

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_APZBridge_h
