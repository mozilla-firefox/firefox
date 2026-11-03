/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_InputBlockState_h
#define mozilla_layers_InputBlockState_h

#include "InputData.h"  // for MultiTouchInput
#include "Units.h"
#include "apz/src/OverscrollHandoffState.h"
#include "mozilla/RefCounted.h"  // for RefCounted
#include "mozilla/RefPtr.h"      // for RefPtr
#include "mozilla/StaticPrefs_apz.h"
#include "mozilla/TimeStamp.h"   // for TimeStamp
#include "mozilla/gfx/Matrix.h"  // for Matrix4x4
#include "mozilla/layers/APZUtils.h"
#include "mozilla/layers/AsyncDragMetrics.h"
#include "mozilla/layers/LayersTypes.h"  // for TouchBehaviorFlags
#include "mozilla/layers/TouchCounter.h"
#include "nsTArray.h"  // for nsTArray

namespace mozilla {
namespace layers {

class AsyncPanZoomController;
class OverscrollHandoffChain;
class CancelableBlockState;
class BrowserGesturableBlockState;
class TouchBlockState;
class WheelBlockState;
class DragBlockState;
class PanGestureBlockState;
class PinchGestureBlockState;
class KeyboardBlockState;
class InputQueueIterator;
enum class BrowserGestureResponse : bool;

/**
 * A base class that stores state common to various input blocks.
 * Note that the InputBlockState constructor acquires the tree lock, so callers
 * from inside AsyncPanZoomController should ensure that the APZC lock is not
 * held.
 */
class InputBlockState : public RefCounted<InputBlockState> {
 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(InputBlockState)

  static const uint64_t NO_BLOCK_ID = 0;

  enum class TargetConfirmationState : uint8_t {
    eUnconfirmed,
    eTimedOut,
    eConfirmed
  };

  InputBlockState(const RefPtr<AsyncPanZoomController>& aTargetApzc,
                  TargetConfirmationFlags aFlags);
  virtual ~InputBlockState() = default;

  virtual CancelableBlockState* AsCancelableBlock() { return nullptr; }
  virtual BrowserGesturableBlockState* AsBrowserGesturableBlock() {
    return nullptr;
  }
  virtual TouchBlockState* AsTouchBlock() { return nullptr; }
  virtual const TouchBlockState* AsTouchBlock() const { return nullptr; }
  virtual WheelBlockState* AsWheelBlock() { return nullptr; }
  virtual DragBlockState* AsDragBlock() { return nullptr; }
  virtual PanGestureBlockState* AsPanGestureBlock() { return nullptr; }
  virtual PinchGestureBlockState* AsPinchGestureBlock() { return nullptr; }
  virtual KeyboardBlockState* AsKeyboardBlock() { return nullptr; }
  virtual Maybe<LayersId> WheelTransactionLayersId() const { return Nothing(); }

  virtual bool SetConfirmedTargetApzc(
      const RefPtr<AsyncPanZoomController>& aTargetApzc,
      TargetConfirmationState aState, InputQueueIterator aFirstInput,
      bool aForScrollbarDrag);
  const RefPtr<AsyncPanZoomController>& GetTargetApzc() const;
  const RefPtr<const OverscrollHandoffChain>& GetOverscrollHandoffChain() const;
  uint64_t GetBlockId() const;

  bool IsTargetConfirmed() const;

  virtual bool ShouldDropEvents() const;

  void SetScrolledApzc(AsyncPanZoomController* aApzc,
                       const OverscrollHandoffState& aOverscrollHandoffState);
  AsyncPanZoomController* GetScrolledApzc() const;
  bool IsDownchainOfScrolledApzc(AsyncPanZoomController* aApzc) const;

  /**
   * Dispatch the event to the target APZC. Mostly this is a hook for
   * subclasses to do any per-event processing they need to.
   */
  virtual void DispatchEvent(const InputData& aEvent) const;

  /**
   * Return true if this input block must stay active if it would otherwise
   * be removed as the last item in the pending queue.
   */
  virtual bool MustStayActive() = 0;

  const ScreenToParentLayerMatrix4x4& GetTransformToApzc() const {
    return mTransformToApzc;
  }

 protected:
  virtual void UpdateTargetApzc(
      const RefPtr<AsyncPanZoomController>& aTargetApzc);

  const AsyncPanZoomController* TargetApzc() const { return mTargetApzc.get(); }

 private:
  // Checks whether |aA| is an ancestor of |aB| (or the same as |aB|) in
  // |mOverscrollHandoffChain|.
  bool IsDownchainOf(AsyncPanZoomController* aA,
                     AsyncPanZoomController* aB) const;

 private:
  RefPtr<AsyncPanZoomController> mTargetApzc;
  bool mRequiresTargetConfirmation;
  const uint64_t mBlockId;

  // The APZC that was actually scrolled by events in this input block.
  // This is used in configurations where a single input block is only
  // allowed to scroll a single APZC (configurations where
  // StaticPrefs::apz_allow_immediate_handoff() is false). Set the first time an
  // input event in this block scrolls an APZC.
  RefPtr<AsyncPanZoomController> mScrolledApzc;

 protected:
  TargetConfirmationState mTargetConfirmed;
  RefPtr<const OverscrollHandoffChain> mOverscrollHandoffChain;

  // Used to transform events from global screen space to |mTargetApzc|'s
  // screen space. It's cached at the beginning of the input block so that
  // all events in the block are in the same coordinate space.
  ScreenToParentLayerMatrix4x4 mTransformToApzc;
};

/**
 * This class represents a set of events that can be cancelled by web content
 * via event listeners.
 *
 * Each cancelable input block can be cancelled by web content, and
 * this information is stored in the mPreventDefault flag. Because web
 * content runs on the Gecko main thread, we cannot always wait for web
 * content's response. Instead, there is a timeout that sets this flag in the
 * case where web content doesn't respond in time. The mContentResponded and
 * mContentResponseTimerExpired flags indicate which of these scenarios
 * occurred.
 */
class CancelableBlockState : public InputBlockState {
 public:
  CancelableBlockState(const RefPtr<AsyncPanZoomController>& aTargetApzc,
                       TargetConfirmationFlags aFlags);

  CancelableBlockState* AsCancelableBlock() override { return this; }

  /**
   * Record whether or not content cancelled this block of events.
   * @param aPreventDefault true iff the block is cancelled.
   * @return false if this block has already received a response from
   *         web content, true if not.
   */
  virtual bool SetContentResponse(bool aPreventDefault);

  /**
   * Record that content didn't respond in time.
   * @return false if this block already timed out, true if not.
   */
  virtual bool TimeoutContentResponse();

  /**
   * Checks if the content response timer has already expired.
   */
  bool IsContentResponseTimerExpired() const;

  /**
   * Checks if the content has responded.
   */
  bool HasContentResponded() const { return mContentResponded; }

  /**
   * @return true iff web content cancelled this block of events.
   */
  bool IsDefaultPrevented() const;

  /**
   * @return true iff this block has received all the information needed
   *         to properly dispatch the events in the block.
   */
  virtual bool IsReadyForHandling() const;

  /**
   * Return a descriptive name for the block kind.
   */
  virtual const char* Type() = 0;

  bool ShouldDropEvents() const override;

  bool HasStateBeenReset() const { return mHasStateBeenReset; };
  void ResetState() { mHasStateBeenReset = true; }

  void ResetContentResponseTimerExpired() {
    mContentResponseTimerExpired = false;
    mContentResponded = false;
  }

 private:
  bool mPreventDefault;
  bool mContentResponded;
  bool mContentResponseTimerExpired;
  bool mHasStateBeenReset;
};

// This class is for a gesture that may be used for browser's gesture such as
// swipe. Before consuming this type of gesture, APZ needs to wait for the
// response from the browser whether the gesture has been used for browser's
// gesture.
class BrowserGesturableBlockState : public CancelableBlockState {
 public:
  BrowserGesturableBlockState* AsBrowserGesturableBlock() override {
    return this;
  }

  void SetNeedsToWaitForBrowserGestureResponse(
      bool aWaitForBrowserGestureResponse);

  bool IsWaitingForBrowserGestureResponse() const;

  void SetBrowserGestureResponse(const BrowserGestureResponse aResponse);

  bool ShouldDropEvents() const override;

 protected:
  // This is an intermediate class.
  BrowserGesturableBlockState(const RefPtr<AsyncPanZoomController>& aTargetApzc,
                              TargetConfirmationFlags aFlags);
  // The waiting state
  bool mWaitingForBrowserGestureResponse;
  // Whether the browser is consuming this block as a gesture and so APZ should
  // drop its events.
  bool mStartedBrowserGesture;
};

/**
 * A single block of wheel events.
 */
class WheelBlockState : public CancelableBlockState {
 public:
  WheelBlockState(const RefPtr<AsyncPanZoomController>& aTargetApzc,
                  TargetConfirmationFlags aFlags,
                  const ScrollWheelInput& aEvent);

  bool SetContentResponse(bool aPreventDefault) override;
  bool MustStayActive() override;
  const char* Type() override;
  bool SetConfirmedTargetApzc(const RefPtr<AsyncPanZoomController>& aTargetApzc,
                              TargetConfirmationState aState,
                              InputQueueIterator aFirstInput,
                              bool aForScrollbarDrag) override;

  WheelBlockState* AsWheelBlock() override { return this; }

  /**
   * Determine whether this wheel block is accepting new events.
   */
  bool ShouldAcceptNewEvent() const;

  /**
   * Call to check whether a wheel event will cause the current transaction to
   * timeout.
   */
  bool MaybeTimeout(const ScrollWheelInput& aEvent);

  /**
   * Called from APZCTM when a mouse move or drag+drop event occurs, before
   * the event has been processed.
   */
  void OnMouseMove(const ScreenIntPoint& aPoint,
                   const Maybe<ScrollableLayerGuid>& aTargetGuid);

  /**
   * Returns whether or not the block is participating in a wheel transaction.
   * This means that the block is the most recent input block to be created,
   * and no events have occurred that would require scrolling a different
   * frame.
   *
   * @return True if in a transaction, false otherwise.
   */
  bool InTransaction() const;

  /**
   * Mark the block as no longer participating in a wheel transaction. This
   * will force future wheel events to begin a new input block.
   */
  void EndTransaction();

  /**
   * @return Whether or not overscrolling is prevented for this wheel block.
   */
  bool AllowScrollHandoff() const;

  /**
   * Called to check and possibly end the transaction due to a timeout.
   *
   * NOTE: This is marked as MOZ_CAN_RUN_SCRIPT_BOUNDARY because this may
   * dispatch a chrome only event for some automated tests. However, it's
   * enabled only with a pref and the content cannot listen to the event.
   * Therefore, this may run script, but shouldn't cause any problems except
   * when the specific tests does something tricky.
   *
   * @return True if the transaction ended, false otherwise.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY bool MaybeTimeout(const TimeStamp& aTimeStamp);

  /**
   * Update the wheel transaction state for a new event.
   */
  void Update(ScrollWheelInput& aEvent);

  ScrollDirections GetAllowedScrollDirections() const {
    return mAllowedScrollDirections;
  }

  Maybe<LayersId> WheelTransactionLayersId() const override;

 protected:
  void UpdateTargetApzc(
      const RefPtr<AsyncPanZoomController>& aTargetApzc) override;

 private:
  TimeStamp mLastEventTime;
  TimeStamp mLastMouseMove;
  uint32_t mScrollSeriesCounter;
  bool mTransactionEnded;
  bool mIsScrollable = true;
  ScrollDirections mAllowedScrollDirections;
};

/**
 * A block of mouse events that are part of a drag
 */
class DragBlockState : public CancelableBlockState {
 public:
  DragBlockState(const RefPtr<AsyncPanZoomController>& aTargetApzc,
                 TargetConfirmationFlags aFlags, const MouseInput& aEvent);

  bool MustStayActive() override;
  const char* Type() override;

  bool HasReceivedMouseUp();
  void MarkMouseUpReceived();

  DragBlockState* AsDragBlock() override { return this; }

  void SetInitialThumbPos(OuterCSSCoord aThumbPos);
  void SetDragMetrics(const AsyncDragMetrics& aDragMetrics,
                      const CSSRect& aScrollableRect);

  void DispatchEvent(const InputData& aEvent) const override;

 private:
  AsyncDragMetrics mDragMetrics;
  OuterCSSCoord mInitialThumbPos;
  CSSRect mInitialScrollableRect;
  bool mReceivedMouseUp;
};

/**
 * A single block of pan gesture events.
 */
class PanGestureBlockState : public BrowserGesturableBlockState {
 public:
  PanGestureBlockState(const RefPtr<AsyncPanZoomController>& aTargetApzc,
                       TargetConfirmationFlags aFlags,
                       const PanGestureInput& aEvent);

  bool SetContentResponse(bool aPreventDefault) override;
  bool IsReadyForHandling() const override;
  bool MustStayActive() override;
  const char* Type() override;
  bool SetConfirmedTargetApzc(const RefPtr<AsyncPanZoomController>& aTargetApzc,
                              TargetConfirmationState aState,
                              InputQueueIterator aFirstInput,
                              bool aForScrollbarDrag) override;

  PanGestureBlockState* AsPanGestureBlock() override { return this; }

  bool TimeoutContentResponse() override;

  /**
   * @return Whether or not overscrolling is prevented for this block.
   */
  bool AllowScrollHandoff() const;

  bool WasInterrupted() const { return mInterrupted; }

  void SetNeedsToWaitForContentResponse(bool aWaitForContentResponse);

  ScrollDirections GetAllowedScrollDirections() const {
    return mAllowedScrollDirections;
  }

  bool IsWaitingForContentResponse() const {
    return mWaitingForContentResponse;
  }
  Maybe<LayersId> WheelTransactionLayersId() const override;

  void ConfirmForHoldGesture() {
    // Hold gestures get their own input block, but do not generate
    // any events that get to web content (because the PANGESTURE_MAYSTART
    // event has a zero delta). As a result, do not wait for a content
    // response for them because it will never arrive.
    mTargetConfirmed = InputBlockState::TargetConfirmationState::eConfirmed;
  }

 private:
  bool mInterrupted;
  bool mWaitingForContentResponse;
  ScrollDirections mAllowedScrollDirections;
};

/**
 * A single block of pinch gesture events.
 */
class PinchGestureBlockState : public CancelableBlockState {
 public:
  PinchGestureBlockState(const RefPtr<AsyncPanZoomController>& aTargetApzc,
                         TargetConfirmationFlags aFlags);

  bool SetContentResponse(bool aPreventDefault) override;
  bool IsReadyForHandling() const override;
  bool MustStayActive() override;
  const char* Type() override;

  PinchGestureBlockState* AsPinchGestureBlock() override { return this; }

  bool WasInterrupted() const { return mInterrupted; }

  void SetNeedsToWaitForContentResponse(bool aWaitForContentResponse);

  bool IsWaitingForContentResponse() const {
    return mWaitingForContentResponse;
  }

 private:
  bool mInterrupted;
  bool mWaitingForContentResponse;
};

/**
 * This class represents a single touch block. A touch block is
 * a set of touch events that can be cancelled by web content via
 * touch event listeners.
 *
 * Every touch-start event creates a new touch block. In this case, the
 * touch block consists of the touch-start, followed by all touch events
 * up to but not including the next touch-start (except in the case where
 * a long-tap happens, see below). Note that in particular we cannot know
 * when a touch block ends until the next one is started. Most touch
 * blocks are created by receipt of a touch-start event.
 *
 * Every long-tap event also creates a new touch block, since it can also
 * be consumed by web content. In this case, when the long-tap event is
 * dispatched to web content, a new touch block is started to hold the remaining
 * touch events, up to but not including the next touch start (or long-tap).
 *
 * Additionally, if touch-action is enabled, each touch block should
 * have a set of allowed touch behavior flags; one for each touch point.
 * This also requires running code on the Gecko main thread, and so may
 * be populated with some latency. The mAllowedTouchBehaviorSet and
 * mAllowedTouchBehaviors variables track this information.
 */
class TouchBlockState : public BrowserGesturableBlockState {
 public:
  explicit TouchBlockState(const RefPtr<AsyncPanZoomController>& aTargetApzc,
                           TargetConfirmationFlags aFlags,
                           TouchCounter& aTouchCounter);

  TouchBlockState* AsTouchBlock() override { return this; }
  const TouchBlockState* AsTouchBlock() const override { return this; }

  /**
   * Set the allowed touch behavior flags for this block.
   * @return false if this block already has these flags set, true if not.
   */
  bool SetAllowedTouchBehaviors(const nsTArray<TouchBehaviorFlags>& aBehaviors);
  /**
   * If the allowed touch behaviors have been set, populate them into
   * |aOutBehaviors| and return true. Else, return false.
   */
  bool GetAllowedTouchBehaviors(
      nsTArray<TouchBehaviorFlags>& aOutBehaviors) const;

  /**
   * Returns true if the allowed touch behaviours have been set, or if touch
   * action is disabled.
   */
  bool HasAllowedTouchBehaviors() const;

  /**
   * Copy various properties from another block.
   */
  void CopyPropertiesFrom(const TouchBlockState& aOther);

  /**
   * @return true iff this block has received all the information needed
   *         to properly dispatch the events in the block.
   */
  bool IsReadyForHandling() const override;

  /**
   * Sets a flag that indicates this input block occurred while the APZ was
   * in a state of fast flinging. This affects gestures that may be produced
   * from input events in this block.
   */
  void SetDuringFastFling();
  /**
   * @return true iff SetDuringFastFling was called on this block.
   */
  bool IsDuringFastFling() const;
  /**
   * Set the single-tap state flag that indicates that this touch block
   * triggered (1) a click, (2) not a click, or (3) not yet sure it will trigger
   * a click or not.
   */
  void SetSingleTapState(apz::SingleTapState aState);
  apz::SingleTapState SingleTapState() const { return mSingleTapState; }

  /**
   * @return false iff touch-action is enabled and the allowed touch behaviors
   * for this touch block do not allow pinch-zooming.
   */
  bool TouchActionAllowsPinchZoom() const;
  /**
   * @return false iff touch-action is enabled and the allowed touch behaviors
   * for this touch block do not allow double-tap zooming.
   */
  bool TouchActionAllowsDoubleTapZoom() const;
  /**
   * @return false iff touch-action is enabled and the allowed touch behaviors
   *         for the first touch point do not allow panning in the specified
   *         direction(s).
   */
  bool TouchActionAllowsPanningX() const;
  bool TouchActionAllowsPanningY() const;
  bool TouchActionAllowsPanningXY() const;

  /**
   * Notifies the input block of an incoming touch event so that the block can
   * update its internal slop state. "Slop" refers to the area around the
   * initial touchstart where we drop touchmove events so that content doesn't
   * see them. The |aApzcCanConsumeEvents| parameter is factored into how large
   * the slop area is - if this is true the slop area is larger.
   * @return true iff the provided event is a touchmove in the slop area and
   *         so should not be sent to content.
   */
  bool UpdateSlopState(const MultiTouchInput& aInput,
                       bool aApzcCanConsumeEvents);
  enum class InSlop : bool { No, Yes };
  InSlop IsInSlop() const;
  bool ForLongTap() const { return mForLongTap; }
  void SetForLongTap() { mForLongTap = true; }
  bool WasLongTapProcessed() const { return mLongTapWasProcessed; }
  void SetLongTapProcessed() {
    MOZ_ASSERT(!mForLongTap);
    mLongTapWasProcessed = true;
    mIsWaitingLongTapResult = false;
  }

  void SetWaitingLongTapResult(bool aResult) {
    MOZ_ASSERT(!mForLongTap);
    mIsWaitingLongTapResult = aResult;
  }
  bool IsWaitingLongTapResult() const { return mIsWaitingLongTapResult; }

  void SetNeedsToWaitTouchMove(bool aNeedsWaitTouchMove) {
    mNeedsWaitTouchMove = aNeedsWaitTouchMove;
  }
  bool IsReadyForCallback() const { return !mNeedsWaitTouchMove; };

  /**
   * Based on the slop origin and the given input event, return a best guess
   * as to the pan direction of this touch block. Returns Nothing() if no guess
   * can be made.
   */
  Maybe<ScrollDirection> GetBestGuessPanDirection(
      const MultiTouchInput& aInput) const;

  /**
   * Returns the number of touch points currently active.
   */
  uint32_t GetActiveTouchCount() const;

  /**
   * Update the active touch count with the given input. This needs to be
   * called for every input in this block, whether it is dispatched or dropped.
   */
  void UpdateTouchCounter(const MultiTouchInput& aInput) const;

  bool MustStayActive() override;
  const char* Type() override;
  TimeDuration GetTimeSinceBlockStart() const;
  bool IsTargetOriginallyConfirmed() const;

  /**
   * Returns true iff |aEvent| is the touchmove on which this block needs
   * to wait for a content response (again) because:
   *   1) this is the first touchmove bailing out of slop (|aWasInSlop|
   *      is Yes), i.e. the block was in slop before |aEvent| arrived;
   *   2) a long-tap event was already fired (or is waiting for its content
   *      response);
   *   3) there are APZ-aware event listeners (i.e. the target was not
   *      originally confirmed) and;
   *   4) the event block has not yet been prevented.
   *
   * Example scenario: content has two event listeners, one for `touchstart`
   * and one for `touchmove`, and the `touchmove` handler calls
   * preventDefault(). If the user keeps touching at a point until a long-tap
   * event fires and then starts moving their finger, APZ has to wait for a
   * content response twice -- once for `touchstart` and once for the first
   * `touchmove` after the long-tap.
   */
  bool NeedsContentResponseAfterLongTap(const MultiTouchInput& aEvent,
                                        InSlop aWasInSlop) const;

 private:
  nsTArray<TouchBehaviorFlags> mAllowedTouchBehaviors;
  bool mAllowedTouchBehaviorSet;
  bool mDuringFastFling;
  bool mInSlop;
  // A long tap involves two touch blocks: the original touch
  // block containing the `touchstart`, and a second one
  // specifically for the long tap. `mForLongTap` is set on the
  // second touch block. `mLongTapWasProcessed` is set
  // on the first touch block after the long tap was processed.
  bool mForLongTap;
  bool mLongTapWasProcessed;

  // A flag representing a state while we are waiting for a content response for
  // the long tap.
  // The reason why we have this flag separately from `mLongTapWasProcessed` is
  // the block is not ready to be processed during the wait, and is ready once
  // after `mLongTapWasProcessed` became true.
  bool mIsWaitingLongTapResult;
  // A flag representing a state that this block still needs to wait for a
  // content response for a touch move event. It will be set just before
  // triggering a long-press event.
  bool mNeedsWaitTouchMove;
  apz::SingleTapState mSingleTapState;
  ScreenIntPoint mSlopOrigin;
  // A reference to the InputQueue's touch counter
  TouchCounter& mTouchCounter;
  TimeStamp mStartTime;
  // The original `mTargetConfirmed`. This is necessary to tell whether there's
  // any APZ-aware event listener in the content after we've got a content
  // response, because in the case of a long-tap event we need to wait a content
  // response again.
  TargetConfirmationState mOriginalTargetConfirmedState;
};

/**
 * This class represents a set of keyboard inputs targeted at the same Apzc.
 */
class KeyboardBlockState : public InputBlockState {
 public:
  explicit KeyboardBlockState(
      const RefPtr<AsyncPanZoomController>& aTargetApzc);

  KeyboardBlockState* AsKeyboardBlock() override { return this; }

  bool MustStayActive() override { return false; }

  /**
   * @return Whether or not overscrolling is prevented for this keyboard block.
   */
  bool AllowScrollHandoff() const { return false; }
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_InputBlockState_h
