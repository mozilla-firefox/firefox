/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_InputQueue_h
#define mozilla_layers_InputQueue_h

#include <unordered_map>

#include "APZUtils.h"
#include "DragTracker.h"
#include "InputBlockState.h"
#include "InputData.h"
#include "mozilla/EventForwards.h"
#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/layers/TouchCounter.h"
#include "nsTArray.h"

namespace mozilla {

class InputData;
class MultiTouchInput;
class ScrollWheelInput;

namespace layers {

class AsyncPanZoomController;
class InputBlockState;
class CancelableBlockState;
class TouchBlockState;
class WheelBlockState;
class DragBlockState;
class PanGestureBlockState;
class PinchGestureBlockState;
class KeyboardBlockState;
class AsyncDragMetrics;
class QueuedInput;
struct APZEventResult;
struct APZHandledResult;
enum class BrowserGestureResponse : bool;

// Indicating whether an incoming MULTITOUCH_MOVE event is the first touch-move
// in the current touch block (i.e. the very first one after the touch-start).
enum class InitialTouchMove : bool { No, Yes };

using InputBlockCallback = std::function<void(uint64_t aInputBlockId,
                                              APZHandledResult aHandledResult)>;

struct InputBlockCallbackInfo {
  nsEventStatus mEagerStatus;
  InputBlockCallback mCallback;
};

class InputQueueIterator {
  using Iterator = nsTArray<UniquePtr<QueuedInput>>::iterator;

 public:
  InputQueueIterator() = default;  // "null" iterator
  InputQueueIterator(Iterator aCurrent, Iterator aEnd)
      : mCurrent(aCurrent), mEnd(aEnd) {}

  explicit operator bool() const { return mCurrent != mEnd; }
  QueuedInput* operator*() const { return mCurrent->get(); }
  QueuedInput* operator->() const { return mCurrent->get(); }
  void operator++() { ++mCurrent; }

 private:
  Iterator mCurrent;
  Iterator mEnd;
};

/**
 * This class stores incoming input events, associated with "input blocks",
 * until they are ready for handling.
 */
class InputQueue {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(InputQueue)

 public:
  /**
   * @param aTouchSwipableDirectionsCallback called to notify the widget of the
   * swipable directions of a touch block.
   */
  explicit InputQueue(
      TouchSwipableDirectionsCallback&& aTouchSwipableDirectionsCallback);

  /**
   * Notifies the InputQueue of a new incoming input event. The APZC that the
   * input event was targeted to should be provided in the |aTarget| parameter.
   * See the documentation on APZCTreeManager::ReceiveInputEvent for info on
   * return values from this function.
   */
  APZEventResult ReceiveInputEvent(
      const RefPtr<AsyncPanZoomController>& aTarget,
      TargetConfirmationFlags aFlags, InputData& aEvent,
      const Maybe<nsTArray<TouchBehaviorFlags>>& aTouchBehaviors = Nothing(),
      InitialTouchMove aInitialTouchMove = InitialTouchMove::No);
  /**
   * This function should be invoked to notify the InputQueue when web content
   * decides whether or not it wants to cancel a block of events. The block
   * id to which this applies should be provided in |aInputBlockId|.
   */
  void ContentReceivedInputBlock(uint64_t aInputBlockId, bool aPreventDefault);
  /**
   * This function should be invoked to notify the InputQueue once the target
   * APZC to handle an input block has been confirmed. In practice this should
   * generally be decidable upon receipt of the input event, but in some cases
   * we may need to query the layout engine to know for sure. The input block
   * this applies to should be specified via the |aInputBlockId| parameter.
   */
  void SetConfirmedTargetApzc(
      uint64_t aInputBlockId,
      const RefPtr<AsyncPanZoomController>& aTargetApzc);
  /**
   * This function is invoked to confirm that the drag block should be handled
   * by the APZ.
   */
  void ConfirmDragBlock(uint64_t aInputBlockId,
                        const RefPtr<AsyncPanZoomController>& aTargetApzc,
                        const AsyncDragMetrics& aDragMetrics);
  /**
   * This function should be invoked to notify the InputQueue of the touch-
   * action properties for the different touch points in an input block. The
   * input block this applies to should be specified by the |aInputBlockId|
   * parameter. If touch-action is not enabled on the platform, this function
   * does nothing and need not be called.
   */
  void SetAllowedTouchBehavior(uint64_t aInputBlockId,
                               const nsTArray<TouchBehaviorFlags>& aBehaviors);
  /**
   * Adds a new touch block at the end of the input queue that has the same
   * allowed touch behaviour flags as the the touch block currently being
   * processed. This should only be called when processing of a touch block
   * triggers the creation of a new touch block. Returns the input block id
   * of the the newly-created block.
   */
  uint64_t InjectNewTouchBlock(AsyncPanZoomController* aTarget);
  /**
   * Returns the pending input block at the head of the queue, if there is one.
   * This may return null if there all input events have been processed.
   */
  InputBlockState* GetCurrentBlock() const;
  /*
   * Returns the current pending input block as a specific kind of block. If
   * GetCurrentBlock() returns null, these functions additionally check the
   * mActiveXXXBlock field of the corresponding input type to see if there is
   * a depleted but still active input block, and returns that if found. These
   * functions may return null if no block is found.
   */
  TouchBlockState* GetCurrentTouchBlock() const;
  WheelBlockState* GetCurrentWheelBlock() const;
  DragBlockState* GetCurrentDragBlock() const;
  PanGestureBlockState* GetCurrentPanGestureBlock() const;
  PinchGestureBlockState* GetCurrentPinchGestureBlock() const;
  KeyboardBlockState* GetCurrentKeyboardBlock() const;
  /**
   * Returns true iff the pending block at the head of the queue is a touch
   * block and is ready for handling.
   */
  bool HasReadyTouchBlock() const;
  /**
   * If there is an active wheel transaction, returns the WheelBlockState
   * representing the transaction. Otherwise, returns null. "Active" in this
   * function name is the same kind of "active" as in mActiveWheelBlock - that
   * is, new incoming wheel events will go into the "active" block.
   */
  WheelBlockState* GetActiveWheelTransaction() const;
  /**
   * Remove all input blocks from the input queue.
   */
  void Clear();
  /**
   * Whether the current pending block allows scroll handoff.
   */
  bool AllowScrollHandoff() const;
  /**
   * If there is currently a drag in progress, return whether or not it was
   * targeted at a scrollbar. If the drag was newly-created and doesn't know,
   * use the provided |aOnScrollbar| to populate that information.
   */
  bool IsDragOnScrollbar(bool aOnScrollbar);

  InputBlockState* GetBlockForId(uint64_t aInputBlockId);

  void AddInputBlockCallback(uint64_t aInputBlockId,
                             InputBlockCallback&& aCallback);

  void SetBrowserGestureResponse(uint64_t aInputBlockId,
                                 BrowserGestureResponse aResponse);

 private:
  ~InputQueue();

  // RAII class for automatically running a timeout task that may
  // need to be run immediately after an event has been queued.
  class AutoRunImmediateTimeout final {
   public:
    explicit AutoRunImmediateTimeout(InputQueue* aQueue);
    ~AutoRunImmediateTimeout();

   private:
    InputQueue* mQueue;
  };

  TouchBlockState* StartNewTouchBlock(
      const RefPtr<AsyncPanZoomController>& aTarget,
      TargetConfirmationFlags aFlags);

  TouchBlockState* StartNewTouchBlockForLongTap(
      const RefPtr<AsyncPanZoomController>& aTarget);

  /**
   * If animations are present for the current pending input block, cancel
   * them as soon as possible.
   */
  void CancelAnimationsForNewBlock(InputBlockState* aBlock,
                                   CancelAnimationFlags aExtraFlags = Default);

  /**
   * If we need to wait for a content response, schedule that now. Returns true
   * if the timeout was scheduled, false otherwise.
   */
  bool MaybeRequestContentResponse(
      const RefPtr<AsyncPanZoomController>& aTarget,
      CancelableBlockState* aBlock);

  APZEventResult ReceiveTouchInput(
      const RefPtr<AsyncPanZoomController>& aTarget,
      TargetConfirmationFlags aFlags, const MultiTouchInput& aEvent,
      const Maybe<nsTArray<TouchBehaviorFlags>>& aTouchBehaviors,
      InitialTouchMove aInitialTouchMove);
  APZEventResult ReceiveMouseInput(
      const RefPtr<AsyncPanZoomController>& aTarget,
      TargetConfirmationFlags aFlags, MouseInput& aEvent);
  APZEventResult ReceiveScrollWheelInput(
      const RefPtr<AsyncPanZoomController>& aTarget,
      TargetConfirmationFlags aFlags, const ScrollWheelInput& aEvent);
  APZEventResult ReceivePanGestureInput(
      const RefPtr<AsyncPanZoomController>& aTarget,
      TargetConfirmationFlags aFlags, const PanGestureInput& aEvent);
  APZEventResult ReceivePinchGestureInput(
      const RefPtr<AsyncPanZoomController>& aTarget,
      TargetConfirmationFlags aFlags, const PinchGestureInput& aEvent);
  APZEventResult ReceiveKeyboardInput(
      const RefPtr<AsyncPanZoomController>& aTarget,
      TargetConfirmationFlags aFlags, const KeyboardInput& aEvent);

  /**
   * Helper function that searches mQueuedInputs for the first block matching
   * the given id, and returns it. If |aOutFirstInput| is non-null, it is
   * populated with a pointer to the first input in mQueuedInputs that
   * corresponds to the block, or null if no such input was found. Note that
   * even if there are no inputs in mQueuedInputs, this function can return
   * non-null if the block id provided matches one of the depleted-but-still-
   * active blocks (mActiveTouchBlock, mActiveWheelBlock, etc.).
   */
  InputBlockState* FindBlockForId(uint64_t aInputBlockId,
                                  InputQueueIterator* aOutFirstInput);
  void ScheduleMainThreadTimeout(const RefPtr<AsyncPanZoomController>& aTarget,
                                 CancelableBlockState* aBlock);
  void MainThreadTimeout(uint64_t aInputBlockId);
  void MaybeLongTapTimeout(uint64_t aInputBlockId);

  // Returns true if there's one more queued event we need to process as a
  // result of switching the active block back to the original touch block from
  // the touch block for long-tap.
  bool ProcessQueue();
  bool CanDiscardBlock(InputBlockState* aBlock);
  void UpdateActiveApzc(const RefPtr<AsyncPanZoomController>& aNewActive);

 private:
  // The queue of input events that have not yet been fully processed.
  // This member must only be accessed on the controller/UI thread.
  nsTArray<UniquePtr<QueuedInput>> mQueuedInputs;

  // These are the most recently created blocks of each input type. They are
  // "active" in the sense that new inputs of that type are associated with
  // them. Note that these pointers may be null if no inputs of the type have
  // arrived, or if the inputs for the type formed a complete block that was
  // then discarded.
  RefPtr<TouchBlockState> mActiveTouchBlock;
  RefPtr<WheelBlockState> mActiveWheelBlock;
  RefPtr<DragBlockState> mActiveDragBlock;
  RefPtr<PanGestureBlockState> mActivePanGestureBlock;
  RefPtr<PinchGestureBlockState> mActivePinchGestureBlock;
  RefPtr<KeyboardBlockState> mActiveKeyboardBlock;

  // In the case where a long-tap event triggered by keeping touching happens
  // we need to keep both the touch block for the long-tap and the original
  // touch block started with `touch-start`. This value holds the original block
  // until the long-tap block is processed.
  RefPtr<TouchBlockState> mPrevActiveTouchBlock;

  // The APZC to which the last event was delivered
  RefPtr<AsyncPanZoomController> mLastActiveApzc;

  // Track touches so we know when to clear mLastActiveApzc
  TouchCounter mTouchCounter;

  // Track mouse inputs so we know if we're in a drag or not
  DragTracker mDragTracker;

  // Temporarily stores a timeout task that needs to be run as soon as
  // as the event that triggered it has been queued.
  RefPtr<Runnable> mImmediateTimeout;

  // Maps input block ids to callbacks that will be invoked when the input block
  // is ready for handling.
  using InputBlockCallbackMap =
      std::unordered_map<uint64_t, InputBlockCallback>;
  InputBlockCallbackMap mInputBlockCallbacks;

  TouchSwipableDirectionsCallback mTouchSwipableDirectionsCallback;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_InputQueue_h
