/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_AsyncPanZoomController_h
#define mozilla_layers_AsyncPanZoomController_h

#include "Units.h"
#include "mozilla/layers/CompositorScrollUpdate.h"
#include "mozilla/layers/GeckoContentController.h"
#include "mozilla/layers/RepaintRequest.h"
#include "mozilla/layers/SampleTime.h"
#include "mozilla/layers/ScrollbarData.h"
#include "mozilla/layers/ZoomConstraints.h"
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/EventForwards.h"
#include "mozilla/Monitor.h"
#include "mozilla/RecursiveMutex.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ScrollTypes.h"
#include "mozilla/StaticPrefs_apz.h"
#include "mozilla/UniquePtr.h"
#include "InputData.h"
#include "Axis.h"  // for Axis, Side, etc.
#include "ExpectedGeckoMetrics.h"
#include "FlingAccelerator.h"
#include "InputQueue.h"
#include "APZUtils.h"
#include "LayersTypes.h"
#include "mozilla/gfx/Matrix.h"
#include "nsRegion.h"
#include "nsTArray.h"
#include "PotentialCheckerboardDurationTracker.h"
#include "RecentEventsBuffer.h"  // for RecentEventsBuffer
#include "SampledAPZCState.h"

#include <iosfwd>

namespace mozilla {

namespace ipc {

class SharedMemory;

}  // namespace ipc

namespace wr {
struct MinimapData;
struct SampledScrollOffset;
}  // namespace wr

namespace layers {

class AsyncDragMetrics;
class APZCTreeManager;
struct ScrollableLayerGuid;
class CompositorController;
class GestureEventListener;
struct AsyncTransform;
class AsyncPanZoomAnimation;
class StackScrollerFlingAnimation;
template <typename FlingPhysics>
class GenericFlingAnimation;
class AndroidFlingPhysics;
class DesktopFlingPhysics;
class InputBlockState;
struct FlingHandoffState;
class TouchBlockState;
class PanGestureBlockState;
class OverscrollHandoffChain;
struct OverscrollHandoffState;
class StateChangeNotificationBlocker;
class CheckerboardEvent;
class OverscrollEffectBase;
class WidgetOverscrollEffect;
class GenericOverscrollEffect;
class AndroidSpecificState;
struct KeyboardScrollAction;
struct ZoomTarget;

namespace apz {
struct AsyncScrollThumbTransformer;
}

// Base class for grouping platform-specific APZC state variables.
class PlatformSpecificStateBase {
 public:
  virtual ~PlatformSpecificStateBase() = default;
  virtual AndroidSpecificState* AsAndroidSpecificState() { return nullptr; }
  // PLPPI = "ParentLayer pixels per (Screen) inch"
  virtual AsyncPanZoomAnimation* CreateFlingAnimation(
      AsyncPanZoomController& aApzc, const FlingHandoffState& aHandoffState,
      float aPLPPI);
  virtual UniquePtr<VelocityTracker> CreateVelocityTracker(Axis* aAxis);

  static void InitializeGlobalState() {}
};

/*
 * Represents a transform from the ParentLayer coordinate space of an APZC
 * to the ParentLayer coordinate space of its parent APZC.
 * Each layer along the way contributes to the transform. We track
 * contributions that are perspective transforms separately, as sometimes
 * these require special handling.
 */
struct AncestorTransform {
  gfx::Matrix4x4 mTransform;
  gfx::Matrix4x4 mPerspectiveTransform;

  AncestorTransform() = default;

  AncestorTransform(const gfx::Matrix4x4& aTransform,
                    bool aTransformIsPerspective) {
    (aTransformIsPerspective ? mPerspectiveTransform : mTransform) = aTransform;
  }

  AncestorTransform(const gfx::Matrix4x4& aTransform,
                    const gfx::Matrix4x4& aPerspectiveTransform)
      : mTransform(aTransform), mPerspectiveTransform(aPerspectiveTransform) {}

  gfx::Matrix4x4 CombinedTransform() const {
    return mTransform * mPerspectiveTransform;
  }

  bool ContainsPerspectiveTransform() const {
    return !mPerspectiveTransform.IsIdentity();
  }

  gfx::Matrix4x4 GetPerspectiveTransform() const {
    return mPerspectiveTransform;
  }

  friend AncestorTransform operator*(const AncestorTransform& aA,
                                     const AncestorTransform& aB) {
    return AncestorTransform{
        aA.mTransform * aB.mTransform,
        aA.mPerspectiveTransform * aB.mPerspectiveTransform};
  }
};

// Flags returned by AsyncPanZoomController::ArePointerEventsConsumable().
// See the function for more details.
struct PointerEventsConsumableFlags {
  // The APZC has room to pan or zoom in response to the touch event.
  bool mHasRoom = false;

  // The panning or zooming is allowed by the touch-action property.
  bool mAllowedByTouchAction = false;

  bool IsConsumable() const { return mHasRoom && mAllowedByTouchAction; }
  friend bool operator==(const PointerEventsConsumableFlags& aLhs,
                         const PointerEventsConsumableFlags& aRhs);
  friend std::ostream& operator<<(std::ostream& aOut,
                                  const PointerEventsConsumableFlags& aFlags);
};

/**
 * Controller for all panning and zooming logic. Any time a user input is
 * detected and it must be processed in some way to affect what the user sees,
 * it goes through here. Listens for any input event from InputData and can
 * optionally handle WidgetGUIEvent-derived touch events, but this must be done
 * on the main thread. Note that this class completely cross-platform.
 *
 * Input events originate on the UI thread of the platform that this runs on,
 * and are then sent to this class. This class processes the event in some way;
 * for example, a touch move will usually lead to a panning of content (though
 * of course there are exceptions, such as if content preventDefaults the event,
 * or if the target frame is not scrollable). The compositor interacts with this
 * class by locking it and querying it for the current transform matrix based on
 * the panning and zooming logic that was invoked on the UI thread.
 *
 * Currently, each outer DOM window (i.e. a website in a tab, but not any
 * subframes) has its own AsyncPanZoomController. In the future, to support
 * asynchronously scrolled subframes, we want to have one AsyncPanZoomController
 * per frame.
 */
class AsyncPanZoomController {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AsyncPanZoomController)

  typedef mozilla::MonitorAutoLock MonitorAutoLock;
  typedef mozilla::gfx::Matrix4x4 Matrix4x4;
  typedef mozilla::layers::RepaintRequest::ScrollOffsetUpdateType
      RepaintUpdateType;

 public:
  enum GestureBehavior {
    // The platform code is responsible for forwarding gesture events here. We
    // will not attempt to generate gesture events from MultiTouchInputs.
    DEFAULT_GESTURES,
    // An instance of GestureEventListener is used to detect gestures. This is
    // handled completely internally within this class.
    USE_GESTURE_DETECTOR
  };

  /**
   * Gets the DPI from the tree manager.
   */
  float GetDPI() const;

  /**
   * Constant describing the tolerance in distance we use, multiplied by the
   * device DPI, before we start panning the screen. This is to prevent us from
   * accidentally processing taps as touch moves, and from very short/accidental
   * touches moving the screen.
   * Note: It's an abuse of the 'Coord' class to use it to represent a 2D
   *       distance, but it's the closest thing we currently have.
   */
  ScreenCoord GetTouchStartTolerance() const;
  /**
   * Same as GetTouchStartTolerance, but the tolerance for how far the touch
   * has to move before it starts allowing touchmove events to be dispatched
   * to content, for non-scrollable content.
   */
  ScreenCoord GetTouchMoveTolerance() const;
  /**
   * Same as GetTouchStartTolerance, but the tolerance for how close the second
   * tap has to be to the first tap in order to be counted as part of a
   * multi-tap gesture (double-tap or one-touch-pinch).
   */
  ScreenCoord GetSecondTapTolerance() const;

  AsyncPanZoomController(LayersId aLayersId, APZCTreeManager* aTreeManager,
                         const RefPtr<InputQueue>& aInputQueue,
                         GeckoContentController* aController,
                         GestureBehavior aGestures = DEFAULT_GESTURES);

  // --------------------------------------------------------------------------
  // These methods must only be called on the gecko thread.
  //

  /**
   * Read the various prefs and do any global initialization for all APZC
   * instances. This must be run on the gecko thread before any APZC instances
   * are actually used for anything meaningful.
   */
  static void InitializeGlobalState();

  // --------------------------------------------------------------------------
  // These methods must only be called on the controller/UI thread.
  //

  /**
   * Kicks an animation to zoom to a rect. This may be either a zoom out or zoom
   * in. The actual animation is done on the sampler thread after being set
   * up.
   */
  void ZoomToRect(const ZoomTarget& aZoomTarget, const uint32_t aFlags);

  /**
   * Updates any zoom constraints contained in the <meta name="viewport"> tag.
   */
  void UpdateZoomConstraints(const ZoomConstraints& aConstraints);

  /**
   * Schedules a runnable to run on the controller/UI thread at some time
   * in the future.
   */
  void PostDelayedTask(already_AddRefed<Runnable> aTask, int aDelayMs);

  // --------------------------------------------------------------------------
  // These methods must only be called on the sampler thread.
  //

  /**
   * Advances any animations currently running to the given timestamp.
   * This may be called multiple times with the same timestamp.
   *
   * The return value indicates whether or not any currently running animation
   * should continue. If true, the compositor should schedule another composite.
   */
  bool AdvanceAnimations(const SampleTime& aSampleTime);

  bool UpdateAnimation(const RecursiveMutexAutoLock& aProofOfLock,
                       const SampleTime& aSampleTime,
                       nsTArray<RefPtr<Runnable>>* aOutDeferredTasks);

  // --------------------------------------------------------------------------
  // These methods must only be called on the updater thread.
  //

  /**
   * A shadow layer update has arrived. |aScrollMetdata| is the new
   * ScrollMetadata for the container layer corresponding to this APZC.
   * |aIsFirstPaint| is a flag passed from the shadow
   * layers code indicating that the scroll metadata being sent with this call
   * are the initial metadata and the initial paint of the frame has just
   * happened.
   */
  void NotifyLayersUpdated(const ScrollMetadata& aScrollMetadata,
                           bool aIsFirstPaint, bool aThisLayerTreeUpdated);

  /**
   * The platform implementation must set the compositor controller so that we
   * can request composites.
   */
  void SetCompositorController(CompositorController* aCompositorController);

  // --------------------------------------------------------------------------
  // These methods can be called from any thread.
  //

  /**
   * Shut down the controller/UI thread state and prepare to be
   * deleted (which may happen from any thread).
   */
  void Destroy();

  /**
   * Returns true if Destroy() has already been called on this APZC instance.
   */
  bool IsDestroyed() const;

  /**
   * Returns the transform to take something from the coordinate space of the
   * last thing we know gecko painted, to the coordinate space of the last thing
   * we asked gecko to paint. In cases where that last request has not yet been
   * processed, this is needed to transform input events properly into a space
   * gecko will understand.
   *
   * This is meant to be called in the context of computing a chain of
   * transforms used for transforming event coordinates (specifically,
   * APZCTreeManager::GetApzcToGeckoTransform() and
   * HitTestingTreeNode::GetTransformToGecko()). The caller needs to pass
   * in |aForLayersId| the LayersId of the content for which the chain will be
   * used. If this content is in an out-of-process subdocument, the returned
   * transform includes the painted resolution transform (see bug 1827330 for
   * details).
   */
  Matrix4x4 GetTransformToLastDispatchedPaint(
      const AsyncTransformComponents& aComponents, LayersId aForLayersId) const;

  /**
   * Returns the number of CSS pixels of checkerboard according to the metrics
   * in this APZC. The argument provided by the caller is the composition bounds
   * of this APZC, additionally clipped by the composition bounds of any
   * ancestor APZCs, accounting for all the async transforms.
   */
  uint32_t GetCheckerboardMagnitude(
      const ParentLayerRect& aClippedCompositionBounds) const;

  /**
   * Report the number of CSSPixel-milliseconds of checkerboard to telemetry.
   * See GetCheckerboardMagnitude for documentation of the
   * aClippedCompositionBounds argument that needs to be provided by the caller.
   */
  void ReportCheckerboard(const SampleTime& aSampleTime,
                          const ParentLayerRect& aClippedCompositionBounds);

  /**
   * Flush any active checkerboard report that's in progress. This basically
   * pretends like any in-progress checkerboard event has terminated, and pushes
   * out the report to the checkerboard reporting service and telemetry. If the
   * checkerboard event has not really finished, it will start a new event
   * on the next composite.
   */
  void FlushActiveCheckerboardReport();

  /**
   * See documentation on corresponding method in APZPublicUtils.h
   */
  static gfx::IntSize GetDisplayportAlignmentMultiplier(
      const ScreenSize& aBaseSize);

  enum class ZoomInProgress {
    No,
    Yes,
  };

  /**
   * Recalculates the displayport. Ideally, this should paint an area bigger
   * than the composite-to dimensions so that when you scroll down, you don't
   * checkerboard immediately. This includes a bunch of logic, including
   * algorithms to bias painting in the direction of the velocity and other
   * such things.
   */
  static const ScreenMargin CalculatePendingDisplayPort(
      const FrameMetrics& aFrameMetrics, const ParentLayerPoint& aVelocity,
      ZoomInProgress aZoomInProgress);

  nsEventStatus HandleDragEvent(const MouseInput& aEvent,
                                const AsyncDragMetrics& aDragMetrics,
                                OuterCSSCoord aInitialThumbPos,
                                const CSSRect& aInitialScrollableRect);

  /**
   * Handler for events which should not be intercepted by the touch listener.
   */
  nsEventStatus HandleInputEvent(
      const InputData& aEvent,
      const ScreenToParentLayerMatrix4x4& aTransformToApzc);

  /**
   * Handler for gesture events.
   * Currently some gestures are detected in GestureEventListener that calls
   * APZC back through this handler in order to avoid recursive calls to
   * APZC::HandleInputEvent() which is supposed to do the work for
   * ReceiveInputEvent().
   */
  nsEventStatus HandleGestureEvent(const InputData& aEvent);

  /**
   * Start autoscrolling this APZC, anchored at the provided location.
   */
  void StartAutoscroll(const ScreenPoint& aAnchorLocation);

  /**
   * Stop autoscrolling this APZC.
   */
  void StopAutoscroll();

  /**
   * Populates the provided object (if non-null) with the scrollable guid of
   * this apzc.
   */
  void GetGuid(ScrollableLayerGuid* aGuidOut) const;

  /**
   * Returns the scrollable guid of this apzc.
   */
  ScrollableLayerGuid GetGuid() const;

  /**
   * Returns true if this APZC instance is for the layer identified by the guid.
   */
  bool Matches(const ScrollableLayerGuid& aGuid);

  /**
   * Returns true if the tree manager of this APZC is the same as the one
   * passed in.
   */
  bool HasTreeManager(const APZCTreeManager* aTreeManager) const;

  void StartAnimation(already_AddRefed<AsyncPanZoomAnimation> aAnimation);

  /**
   * Cancels any currently running animation.
   * aFlags is a bit-field to provide specifics of how to cancel the animation.
   * See CancelAnimationFlags.
   */
  void CancelAnimation(CancelAnimationFlags aFlags = Default);

  /**
   * Clear any overscroll on this APZC.
   */
  void ClearOverscroll();
  void ClearPhysicalOverscroll();

  /**
   * Returns whether this APZC has scroll snap points.
   */
  bool HasScrollSnapping() const {
    return mScrollMetadata.GetSnapInfo().HasScrollSnapping();
  }

  /**
   * Returns whether this APZC has room to be panned (in any direction).
   */
  bool IsPannable() const;

  /**
   * Returns whether this APZC represents a scroll info layer.
   */
  bool IsScrollInfoLayer() const;

  /**
   * Returns true if the APZC has been flung with a velocity greater than the
   * stop-on-tap fling velocity threshold (which is pref-controlled).
   */
  bool IsFlingingFast() const;

  /**
   * Returns whether this APZC is currently autoscrolling.
   */
  bool IsAutoscroll() const {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    return mState == AUTOSCROLL;
  }

  /**
   * Returns the identifier of the touch in the last touch event processed by
   * this APZC. This should only be called when the last touch event contained
   * only one touch.
   */
  int32_t GetLastTouchIdentifier() const;

  /**
   * Returns the matrix that transforms points from global screen space into
   * this APZC's ParentLayer space.
   * To respect the lock ordering, mRecursiveMutex must NOT be held when calling
   * this function (since this function acquires the tree lock).
   */
  ScreenToParentLayerMatrix4x4 GetTransformToThis() const;

  /**
   * Convert the vector |aVector|, rooted at the point |aAnchor|, from
   * this APZC's ParentLayer coordinates into screen coordinates.
   * The anchor is necessary because with 3D tranforms, the location of the
   * vector can affect the result of the transform.
   * To respect the lock ordering, mRecursiveMutex must NOT be held when calling
   * this function (since this function acquires the tree lock).
   */
  ScreenPoint ToScreenCoordinates(const ParentLayerPoint& aVector,
                                  const ParentLayerPoint& aAnchor) const;

  /**
   * Convert the vector |aVector|, rooted at the point |aAnchor|, from
   * screen coordinates into this APZC's ParentLayer coordinates.
   * The anchor is necessary because with 3D tranforms, the location of the
   * vector can affect the result of the transform.
   * To respect the lock ordering, mRecursiveMutex must NOT be held when calling
   * this function (since this function acquires the tree lock).
   */
  ParentLayerPoint ToParentLayerCoordinates(const ScreenPoint& aVector,
                                            const ScreenPoint& aAnchor) const;

  /**
   * Same as above, but uses an ExternalPoint as the anchor.
   */
  ParentLayerPoint ToParentLayerCoordinates(const ScreenPoint& aVector,
                                            const ExternalPoint& aAnchor) const;

  /**
   * Combines an offset defined as an external point, with a window-relative
   * offset to give an absolute external point.
   */
  static ExternalPoint ToExternalPoint(const ExternalPoint& aScreenOffset,
                                       const ScreenPoint& aScreenPoint);

  /**
   * Gets a vector where the head is the given point, and the tail is
   * the touch start position.
   */
  ScreenPoint PanVector(const ExternalPoint& aPos) const;

  // Return whether or not a wheel event will be able to scroll in either
  // direction.
  bool CanScroll(const InputData& aEvent) const;

  // Return the directions in which this APZC allows handoff (as governed by
  // overscroll-behavior).
  ScrollDirections GetAllowedHandoffDirections(
      HandoffConsumer aConsumer = HandoffConsumer::Scrolling) const;

  // Return the directions in which this APZC allows overscrolling.
  ScrollDirections GetOverscrollableDirections() const;

  // Return whether or not a scroll delta will be able to scroll in either
  // direction.
  bool CanScroll(const ParentLayerPoint& aDelta) const;

  // Return whether or not a scroll delta will be able to scroll in either
  // direction with wheel.
  bool CanScrollWithWheel(const ParentLayerPoint& aDelta) const;

  // Return whether or not there is room to scroll this APZC
  // in the given direction.
  bool CanScroll(ScrollDirection aDirection) const;

  // Return the directions in which this APZC is able to scroll.
  SideBits ScrollableDirections() const;

  // Return true if there is room to scroll along with moving the dynamic
  // toolbar.
  //
  // NOTE: This function should be used only for the root content APZC.
  bool CanVerticalScrollWithDynamicToolbar() const;

  // Return true if there is room to scroll downwards.
  bool CanScrollDownwards() const;

  // Return true if there is room to scroll upwards.
  bool CanOverscrollUpwards() const;

  /**
   * Convert a point on the scrollbar from this APZC's ParentLayer coordinates
   * to OuterCSS coordinates relative to the beginning of the scroll track.
   * Only the component in the direction of scrolling is returned.
   */
  OuterCSSCoord ConvertScrollbarPoint(const ParentLayerPoint& aScrollbarPoint,
                                      const ScrollbarData& aThumbData) const;

  void NotifyMozMouseScrollEvent(const nsString& aString) const;

  bool OverscrollBehaviorAllowsSwipe() const;

  //|Metrics()| and |Metrics() const| are getter functions that both return
  // mScrollMetadata.mMetrics

  const FrameMetrics& Metrics() const;
  FrameMetrics& Metrics();

  class AutoRecordCompositorScrollUpdate;
  /**
   * Get the CompositorScrollUpdates to be sent to consumers for the current
   * composite.
   */
  std::vector<CompositorScrollUpdate> GetCompositorScrollUpdates();

 private:
  // Compositor scroll updates since the last time
  // SampleCompositedAsyncTransform() was called.
  // Access to this field should be protected by mRecursiveMutex.
  std::vector<CompositorScrollUpdate> mUpdatesSinceLastSample;

  CompositorScrollUpdate::Metrics GetCurrentMetricsForCompositorScrollUpdate(
      const RecursiveMutexAutoLock& aProofOfApzcLock) const;

 public:
  wr::MinimapData GetMinimapData() const;

  // Returns the cached current frame time.
  SampleTime GetFrameTime() const;

  bool IsZero(const ParentLayerPoint& aPoint) const;
  bool IsZero(ParentLayerCoord aCoord) const;

  bool FuzzyGreater(ParentLayerCoord aCoord1, ParentLayerCoord aCoord2) const;

 private:
  // Get whether the horizontal content of the honoured target of auto-dir
  // scrolling starts from right to left. If you don't know of auto-dir
  // scrolling or what a honoured target means,
  // @see mozilla::WheelDeltaAdjustmentStrategy
  bool IsContentOfHonouredTargetRightToLeft(bool aHonoursRoot) const;

 protected:
  // Protected destructor, to discourage deletion outside of Release():
  virtual ~AsyncPanZoomController();

  /**
   * Helper method for touches beginning. Sets everything up for panning and any
   * multitouch gestures.
   */
  nsEventStatus OnTouchStart(const MultiTouchInput& aEvent);

  /**
   * Helper method for touches moving. Does any transforms needed when panning.
   */
  nsEventStatus OnTouchMove(const MultiTouchInput& aEvent);

  /**
   * Helper method for touches ending. Redraws the screen if necessary and does
   * any cleanup after a touch has ended.
   */
  nsEventStatus OnTouchEnd(const MultiTouchInput& aEvent);

  /**
   * Helper method for touches being cancelled. Treated roughly the same as a
   * touch ending (OnTouchEnd()).
   */
  nsEventStatus OnTouchCancel(const MultiTouchInput& aEvent);

  /**
   * Helper method for scales beginning. Distinct from the OnTouch* handlers in
   * that this implies some outside implementation has determined that the user
   * is pinching.
   */
  nsEventStatus OnScaleBegin(const PinchGestureInput& aEvent);

  /**
   * Helper method for scaling. As the user moves their fingers when pinching,
   * this changes the scale of the page.
   */
  nsEventStatus OnScale(const PinchGestureInput& aEvent);

  /**
   * Helper method for scales ending. Redraws the screen if necessary and does
   * any cleanup after a scale has ended.
   */
  nsEventStatus OnScaleEnd(const PinchGestureInput& aEvent);

  /**
   * Helper methods for handling pan events.
   */
  nsEventStatus OnPanMayBegin(const PanGestureInput& aEvent);
  nsEventStatus OnPanCancelled(const PanGestureInput& aEvent);
  nsEventStatus OnPanBegin(const PanGestureInput& aEvent);
  enum class FingersOnTouchpad {
    Yes,
    No,
  };
  nsEventStatus OnPan(const PanGestureInput& aEvent,
                      FingersOnTouchpad aFingersOnTouchpad);
  nsEventStatus OnPanEnd(const PanGestureInput& aEvent);
  nsEventStatus OnPanMomentumStart(const PanGestureInput& aEvent);
  nsEventStatus OnPanMomentumEnd(const PanGestureInput& aEvent);
  nsEventStatus HandleEndOfPan();
  nsEventStatus OnPanInterrupted(const PanGestureInput& aEvent);

  /**
   * Helper methods for handling scroll wheel events.
   */
  nsEventStatus OnScrollWheel(const ScrollWheelInput& aEvent);

  /**
   * Gets the scroll wheel delta's values in parent-layer pixels from the
   * original delta's values of a wheel input.
   */
  ParentLayerPoint GetScrollWheelDelta(const ScrollWheelInput& aEvent) const;

  /**
   * This function is like GetScrollWheelDelta(aEvent).
   * The difference is the four added parameters provide values as alternatives
   * to the original wheel input's delta values, so |aEvent|'s delta values are
   * ignored in this function, we only use some other member variables and
   * functions of |aEvent|.
   */
  ParentLayerPoint GetScrollWheelDelta(const ScrollWheelInput& aEvent,
                                       double aDeltaX, double aDeltaY,
                                       double aMultiplierX,
                                       double aMultiplierY) const;

  /**
   * This deleted function is used for:
   * 1. avoiding accidental implicit value type conversions of input delta
   *    values when callers intend to call the above function;
   * 2. decoupling the manual relationship between the delta value type and the
   *    above function. If by any chance the defined delta value type in
   *    ScrollWheelInput has changed, this will automatically result in build
   *    time failure, so we can learn of it the first time and accordingly
   *    redefine those parameters' value types in the above function.
   */
  template <typename T>
  ParentLayerPoint GetScrollWheelDelta(ScrollWheelInput&, T, T, T, T) = delete;

  /**
   * Helper methods for handling keyboard events.
   */
  nsEventStatus OnKeyboard(const KeyboardInput& aEvent);

  CSSPoint GetKeyboardDestination(const KeyboardScrollAction& aAction) const;

  // Returns the corresponding ScrollSnapFlags for the given |aAction|.
  // See https://drafts.csswg.org/css-scroll-snap/#scroll-types
  ScrollSnapFlags GetScrollSnapFlagsForKeyboardAction(
      const KeyboardScrollAction& aAction) const;

  /**
   * Helper methods for long press gestures.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  nsEventStatus OnLongPress(const TapGestureInput& aEvent);
  nsEventStatus OnLongPressUp(const TapGestureInput& aEvent);

  /**
   * Helper method for single tap gestures.
   */
  nsEventStatus OnSingleTapUp(const TapGestureInput& aEvent);

  /**
   * Helper method for a single tap confirmed.
   */
  nsEventStatus OnSingleTapConfirmed(const TapGestureInput& aEvent);

  /**
   * Helper method for double taps.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  nsEventStatus OnDoubleTap(const TapGestureInput& aEvent);

  /**
   * Helper method for double taps where the double-tap gesture is disabled.
   */
  nsEventStatus OnSecondTap(const TapGestureInput& aEvent);

  /**
   * Helper method to cancel any gesture currently going to Gecko. Used
   * primarily when a user taps the screen over some clickable content but then
   * pans down instead of letting go (i.e. to cancel a previous touch so that a
   * new one can properly take effect.
   */
  nsEventStatus OnCancelTap(const TapGestureInput& aEvent);

  /**
   * The following five methods modify the scroll offset. For the APZC
   * representing the RCD-RSF, they also recalculate the offset of the layout
   * viewport.
   */

  /**
   * Scroll the scroll frame to an X,Y offset.
   */
  void SetVisualScrollOffset(const CSSPoint& aOffset);

  /**
   * Scroll the scroll frame to an X,Y offset, clamping the resulting scroll
   * offset to the scroll range.
   */
  void ClampAndSetVisualScrollOffset(const CSSPoint& aOffset);

  /**
   * Scroll the scroll frame by an X,Y offset.
   * The resulting scroll offset is not clamped to the scrollable rect;
   * the caller must ensure it stays within range.
   */
  void ScrollBy(const CSSPoint& aOffset);

  /**
   * Scroll the scroll frame by an X,Y offset, clamping the resulting
   * scroll offset to the scroll range.
   */
  void ScrollByAndClamp(const CSSPoint& aOffset);

  /**
   * Scales the viewport by an amount (note that it multiplies this scale in to
   * the current scale, it doesn't set it to |aScale|). Also considers a focus
   * point so that the page zooms inward/outward from that point.
   */
  void ScaleWithFocus(float aScale, const CSSPoint& aFocus);

  /**
   * Schedules a composite on the compositor thread.
   */
  void ScheduleComposite();

  /**
   * Schedules a composite, and if enough time has elapsed since the last
   * paint, a paint.
   */
  void ScheduleCompositeAndMaybeRepaint();

  /**
   * Gets the start point of the current touch.
   * This only makes sense if a touch is currently happening and OnTouchMove()
   * or the equivalent for pan gestures is being invoked.
   */
  ParentLayerPoint PanStart() const;

  /**
   * Gets a vector of the velocities of each axis.
   */
  const ParentLayerPoint GetVelocityVector() const;

  /**
   * Sets the velocities of each axis.
   */
  void SetVelocityVector(const ParentLayerPoint& aVelocityVector);

  /**
   * Gets the first touch point from a MultiTouchInput.  This gets only
   * the first one and assumes the rest are either missing or not relevant.
   */
  ParentLayerPoint GetFirstTouchPoint(const MultiTouchInput& aEvent);

  /**
   * Gets the relevant point in the event
   * (eg. first touch, or pinch focus point) of the given InputData.
   */
  ExternalPoint GetExternalPoint(const InputData& aEvent);

  /**
   * Gets the relevant point in the event, in external screen coordinates.
   */
  static ExternalPoint GetFirstExternalTouchPoint(
      const MultiTouchInput& aEvent);

  /**
   * Gets the amount by which this APZC is overscrolled along both axes.
   */
  ParentLayerPoint GetOverscrollAmount() const;

 private:
  // Internal version of GetOverscrollAmount() which does not set
  // the test async properties.
  ParentLayerPoint GetOverscrollAmountInternal() const;

 protected:
  /**
   * Returns SideBits where this APZC is overscrolled.
   */
  SideBits GetOverscrollSideBits() const;

  /**
   * Restore the amount by which this APZC is overscrolled along both axes
   * to the specified amount. This is for test-related use; overscrolling
   * as a result of user input should happen via OverscrollBy().
   */
  void RestoreOverscrollAmount(const ParentLayerPoint& aOverscroll);

  /**
   * Sets the panning state basing on the pan direction angle and current
   * touch-action value.
   */
  void HandlePanningWithTouchAction(double angle);

  /**
   * Sets the panning state ignoring the touch action value.
   */
  void HandlePanning(double angle);

  /**
   * Update the panning state and axis locks.
   */
  void HandlePanningUpdate(const ScreenPoint& aDelta);

  /**
   * Set and update the pinch lock
   */
  void HandlePinchLocking(const PinchGestureInput& aEvent);

  /**
   * Sets up anything needed for panning. This takes us out of the "TOUCHING"
   * state and starts actually panning us. We provide the physical pixel
   * position of the start point so that the pan gesture is calculated
   * regardless of if the window/GeckoView moved during the pan.
   */
  nsEventStatus StartPanning(const ExternalPoint& aStartPoint,
                             const TimeStamp& aEventTime);

  /**
   * Wrapper for Axis::UpdateWithTouchAtDevicePoint(). Calls this function for
   * both axes and factors in the time delta from the last update.
   */
  void UpdateWithTouchAtDevicePoint(const MultiTouchInput& aEvent);

  /**
   * Does any panning required due to a new touch event.
   */
  void TrackTouch(const MultiTouchInput& aEvent);

  /**
   * Register the start of a touch or pan gesture at the given position and
   * time.
   */
  void StartTouch(const ParentLayerPoint& aPoint, TimeStamp aTimestamp);

  /**
   * Register the end of a touch or pan gesture at the given time.
   */
  void EndTouch(TimeStamp aTimestamp, Axis::ClearAxisLock aClearAxisLock);

  /**
   * Utility function to send updated FrameMetrics to Gecko so that it can paint
   * the displayport area. Calls into GeckoContentController to do the actual
   * work. This call will use the current metrics. If this function is called
   * from a non-main thread, it will redispatch itself to the main thread, and
   * use the latest metrics during the redispatch.
   */
  void RequestContentRepaint(
      RepaintUpdateType aUpdateType = RepaintUpdateType::eUserAction);

  /**
   * Send Metrics() to Gecko to trigger a repaint. This function may filter
   * duplicate calls with the same metrics. This function must be called on the
   * main thread.
   */
  void RequestContentRepaint(const ParentLayerPoint& aVelocity,
                             const ScreenMargin& aDisplayportMargins,
                             RepaintUpdateType aUpdateType);

  /**
   * Gets the current frame metrics. This is *not* the Gecko copy stored in the
   * layers code.
   */
  const FrameMetrics& GetFrameMetrics() const;

  /**
   * Gets the current scroll metadata. This is *not* the Gecko copy stored in
   * the layers code/
   */
  const ScrollMetadata& GetScrollMetadata() const;

  /**
   * Gets the pointer to the apzc tree manager. All the access to tree manager
   * should be made via this method and not via private variable since this
   * method ensures that no lock is set.
   */
  APZCTreeManager* GetApzcTreeManager() const;

  void AssertOnSamplerThread() const;
  void AssertOnUpdaterThread() const;

  /**
   * Convert ScreenPoint relative to the screen to LayoutDevicePoint relative
   * to the parent document. This excludes the transient compositor transform.
   * NOTE: This must be converted to LayoutDevicePoint relative to the child
   * document before sending over IPC to a child process.
   */
  Maybe<LayoutDevicePoint> ConvertToGecko(const ScreenIntPoint& aPoint);

  enum class AxisLockMode {
    FREE,     /* No locking at all */
    STANDARD, /* Default axis locking mode that remains locked until pan ends */
    STICKY,   /* Allow lock to be broken, with hysteresis */
    DOMINANT_AXIS, /* Only allow movement on one axis */
    BREAKABLE,     /* Allow lock to be broken until the pan ends */
  };

  static AxisLockMode GetAxisLockMode();

  bool UsingStatefulAxisLock() const;

  enum PinchLockMode {
    PINCH_FREE,     /* No locking at all */
    PINCH_STANDARD, /* Default pinch locking mode that remains locked until
                       pinch gesture ends*/
    PINCH_STICKY,   /* Allow lock to be broken, with hysteresis */
  };

  static PinchLockMode GetPinchLockMode();

  // Helper function for OnSingleTapUp(), OnSingleTapConfirmed(), and
  // OnLongPressUp().
  nsEventStatus GenerateSingleTap(GeckoContentController::TapType aType,
                                  const ScreenIntPoint& aPoint,
                                  mozilla::Modifiers aModifiers);

  // Common processing at the end of a touch block.
  void OnTouchEndOrCancel();

  LayersId mLayersId;
  RefPtr<CompositorController> mCompositorController;

  /* Access to the following two fields is protected by the mRefPtrMonitor,
     since they are accessed on the UI thread but can be cleared on the
     updater thread. */
  RefPtr<GeckoContentController> mGeckoContentController
      MOZ_GUARDED_BY(mRefPtrMonitor);
  RefPtr<GestureEventListener> mGestureEventListener
      MOZ_GUARDED_BY(mRefPtrMonitor);
  mutable Monitor mRefPtrMonitor;

  // This is a raw pointer to avoid introducing a reference cycle between
  // AsyncPanZoomController and APZCTreeManager. Since these objects don't
  // live on the main thread, we can't use the cycle collector with them.
  // The APZCTreeManager owns the lifetime of the APZCs, so nulling this
  // pointer out in Destroy() will prevent accessing deleted memory.
  Atomic<APZCTreeManager*> mTreeManager;

  /* Utility functions that return a addrefed pointer to the corresponding
   * fields. */
  already_AddRefed<GeckoContentController> GetGeckoContentController() const;
  already_AddRefed<GestureEventListener> GetGestureEventListener() const;

  PlatformSpecificStateBase* GetPlatformSpecificState();

  /**
   * Convenience functions to get the corresponding fields of mZoomContraints
   * while holding mRecursiveMutex.
   */
  bool ZoomConstraintsAllowZoom() const;
  bool ZoomConstraintsAllowDoubleTapZoom() const;

 protected:
  // Both |mScrollMetadata| and |mLastContentPaintMetrics| are protected by the
  // monitor. Do not read from or modify them without locking.
  ScrollMetadata mScrollMetadata;

  // Protects |mScrollMetadata|, |mLastContentPaintMetrics|, |mState| and
  // |mLastSnapTargetIds|.  Before manipulating |mScrollMetadata|,
  // |mLastContentPaintMetrics| or |mLastSnapTargetIds| the monitor should be
  // held. When setting |mState|, either the SetState() function can be used, or
  // the monitor can be held and then |mState| updated.
  // IMPORTANT: See the note about lock ordering at the top of
  // APZCTreeManager.h. This is mutable to allow entering it from 'const'
  // methods; doing otherwise would significantly limit what methods could be
  // 'const'.
  // FIXME: Please keep in mind that due to some existing coupled relationships
  // among the class members, we should be aware of indirect usage of the
  // monitor-protected members. That is, although this monitor isn't required to
  // be held before manipulating non-protected class members, some functions on
  // those members might indirectly manipulate the protected members; in such
  // cases, the monitor should still be held. Let's take mX.CanScroll for
  // example:
  // Axis::CanScroll(ParentLayerCoord) calls Axis::CanScroll() which calls
  // Axis::GetPageLength() which calls Axis::GetFrameMetrics() which calls
  // AsyncPanZoomController::GetFrameMetrics(), therefore, this monitor should
  // be held before calling the CanScroll function of |mX| and |mY|. These
  // coupled relationships bring us the burden of taking care of when the
  // monitor should be held, so they should be decoupled in the future.
  mutable RecursiveMutex mRecursiveMutex;

 private:
  // Metadata of the container layer corresponding to this APZC. This is
  // stored here so that it is accessible from the UI/controller thread.
  // These are the metrics at last content paint, the most recent
  // values we were notified of in NotifyLayersUpdate(). Since it represents
  // the Gecko state, it should be used as a basis for untransformation when
  // sending messages back to Gecko.
  ScrollMetadata mLastContentPaintMetadata;
  FrameMetrics& mLastContentPaintMetrics;  // for convenience, refers to
                                           // mLastContentPaintMetadata.mMetrics
  // The last content repaint request.
  RepaintRequest mLastPaintRequestMetrics;
  // The metrics that we expect content to have. This is updated when we
  // request a content repaint, and when we receive a shadow layers update.
  // This allows us to transform events into Gecko's coordinate space.
  ExpectedGeckoMetrics mExpectedGeckoMetrics;

  // This holds important state from the Metrics() at previous times
  // SampleCompositedAsyncTransform() was called. This will always have at least
  // one item. mRecursiveMutex must be held when using or modifying this member.
  // Samples should be inserted to the "back" of the deque and extracted from
  // the "front".
  std::deque<SampledAPZCState> mSampledState;

  // Groups state variables that are specific to a platform.
  // Initialized on first use.
  UniquePtr<PlatformSpecificStateBase> mPlatformSpecificState;

  // This flag is set to true when we are in a axis-locked pan as a result of
  // the touch-action CSS property.
  bool mPanDirRestricted;

  // This flag is set to true when we are in a pinch-locked state. ie: user
  // is performing a two-finger pan rather than a pinch gesture
  bool mPinchLocked;

  // Stores the pinch events that occured within a given timeframe. Used to
  // calculate the focusChange and spanDistance within a fixed timeframe.
  // RecentEventsBuffer is not threadsafe. Should only be accessed on the
  // controller thread.
  RecentEventsBuffer<PinchGestureInput> mPinchEventBuffer;

  // Stores the touch events that occured within a given timeframe. Ussed to
  // determine the direction of a touch scroll, which determines which axis
  // should be locked if STICKY axis locking is used. Should only by accessed
  // on the controller thread.
  RecentEventsBuffer<MultiTouchInput> mTouchScrollEventBuffer;

  // Most up-to-date constraints on zooming. These should always be reasonable
  // values; for example, allowing a min zoom of 0.0 can cause very bad things
  // to happen. Hold mRecursiveMutex when accessing this.
  ZoomConstraints mZoomConstraints;

  // The last time the compositor has sampled the content transform for this
  // frame.
  SampleTime mLastSampleTime;

  // The last sample time at which we submitted a checkerboarding report.
  SampleTime mLastCheckerboardReport;

  // Stores the previous focus point if there is a pinch gesture happening. Used
  // to allow panning by moving multiple fingers (thus moving the focus point).
  ParentLayerPoint mLastZoomFocus;

  // Stores the previous zoom level at which we last sent a ScaleGestureComplete
  // notification.
  CSSToParentLayerScale mLastNotifiedZoom;

  // Accessing mAnimation needs to be protected by mRecursiveMutex
  RefPtr<AsyncPanZoomAnimation> mAnimation;

  UniquePtr<OverscrollEffectBase> mOverscrollEffect;

  // Zoom animation id, used for zooming. This should only be set on the APZC
  // instance for the root content document (i.e. the one we support zooming
  // on). The animation id itself refers to the transform animation id that was
  // set on the stacking context in the WR display list. By changing the
  // transform associated with this id, we can adjust the scaling that WebRender
  // applies, thereby controlling the zoom.
  Maybe<uint64_t> mZoomAnimationId;

  // Position on screen where user first put their finger down.
  ExternalPoint mStartTouch;

  // Accessing mScrollPayload needs to be protected by mRecursiveMutex
  Maybe<CompositionPayload> mScrollPayload;

  // Representing sampled scroll offset generation, this value is bumped up
  // every time this APZC sampled new scroll offset.
  APZScrollGeneration mScrollGeneration;

  friend class Axis;

 public:
  Maybe<CompositionPayload> NotifyScrollSampling();

  /**
   * Invoke |callable|, passing |mLastContentPaintMetrics| as argument,
   * while holding the APZC lock required to access |mLastContentPaintMetrics|.
   * This allows code outside of an AsyncPanZoomController method implementation
   * to access |mLastContentPaintMetrics| without having to make a copy of it.
   * Passes through the return value of |callable|.
   */
  template <typename Callable>
  auto CallWithLastContentPaintMetrics(const Callable& callable) const
      -> decltype(callable(mLastContentPaintMetrics)) {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    return callable(mLastContentPaintMetrics);
  }

  void SetZoomAnimationId(const Maybe<uint64_t>& aZoomAnimationId);
  Maybe<uint64_t> GetZoomAnimationId() const;

  /* ===================================================================
   * The functions and members in this section are used to expose
   * the current async transform state to callers.
   */
 public:
  static const AsyncTransformConsumer eForEventHandling =
      AsyncTransformConsumer::eForEventHandling;
  static const AsyncTransformConsumer eForCompositing =
      AsyncTransformConsumer::eForCompositing;

  /**
   * Get the current scroll offset of the scrollable frame corresponding
   * to this APZC, including the effects of any asynchronous panning and
   * zooming, in ParentLayer pixels.
   */
  ParentLayerPoint GetCurrentAsyncScrollOffset(
      AsyncTransformConsumer aMode) const;

  /**
   * Get the current visual viewport of the scrollable frame corresponding
   * to this APZC, including the effects of any asynchronous panning and
   * zooming, in CSS pixels.
   */
  CSSRect GetCurrentAsyncVisualViewport(AsyncTransformConsumer aMode) const;

  /**
   * Return a visual effect that reflects this apzc's
   * overscrolled state, if any.
   */
  AsyncTransformComponentMatrix GetOverscrollTransform(
      AsyncTransformConsumer aMode) const;

  /**
   * Returns the incremental transformation corresponding to the async pan/zoom
   * in progress. That is, when this transform is multiplied with the layer's
   * existing transform, it will make the layer appear with the desired pan/zoom
   * amount.
   * The transform can have both scroll and zoom components; the caller can
   * request just one or the other, or both, via the |aComponents| parameter.
   * When only the eLayout component is requested, the returned translation
   * should really be a LayerPoint, rather than a ParentLayerPoint, as it will
   * not be scaled by the asynchronous zoom.
   * |aMode| specifies whether the async transform is queried for the purpose of
   * hit testing (eHitTesting) in which case the latest values from |Metrics()|
   * are used, or for compositing (eCompositing) in which case a sampled value
   * from |mSampledState| is used.
   * |aSampleIndex| specifies which sample in |mSampledState| to use.
   */
  AsyncTransform GetCurrentAsyncTransform(
      AsyncTransformConsumer aMode,
      AsyncTransformComponents aComponents = LayoutAndVisual,
      std::size_t aSampleIndex = 0) const;

  /**
   * A variant of GetCurrentAsyncTransform() intended for use when computing
   * the screen-to-apzc and apzc-to-gecko transforms. This includes the
   * overscroll transform, and additionally requires the caller to pass in
   * the LayersId of the APZC whose screen-to-apzc or apzc-to-gecko transform
   * is being computed in |aForLayersId|; if that APZC is in an out-of-process
   * subdocument, and we are the zoomable (root-content) APZC, the returned
   * transform includes the resolution at painting time, not just the async
   * change to the resolution since painting. This is done because
   * out-of-process content expects the screen-to-apzc and apzc-to-gecko
   * transforms to include the painted resolution (see bug 1827330 for details).
   *
   * Should only be called from
   * APZCTreeManager::{GetScreenToApzcTransform(),GetApzcToGeckoTransform()}.
   */
  AsyncTransformComponentMatrix GetAsyncTransformForInputTransformation(
      AsyncTransformComponents aComponents, LayersId aForLayersId) const;

  /**
   * Returns a transform that scales by the resolution that was in place
   * at "painting" (display list building) time. This is used as a
   * component of some transforms related to APZCs in out-of-process
   * subdocuments.
   *
   * Can only be called for the root content APZC.
   */
  Matrix4x4 GetPaintedResolutionTransform() const;

  AutoTArray<wr::SampledScrollOffset, 2> GetSampledScrollOffsets() const;

  /**
   * Returns the "zoom" bits of the transform. This includes both the rasterized
   * (layout device to layer scale) and async (layer scale to parent layer
   * scale) components of the zoom.
   */
  LayoutDeviceToParentLayerScale GetCurrentPinchZoomScale(
      AsyncTransformConsumer aMode) const;

  ParentLayerRect GetCompositionBounds() const {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    return mScrollMetadata.GetMetrics().GetCompositionBounds();
  }

  LayoutDeviceToLayerScale GetCumulativeResolution() const {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    return mScrollMetadata.GetMetrics().GetCumulativeResolution();
  }

  CSSRect GetScrollableRect() const {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    return mScrollMetadata.GetMetrics().GetScrollableRect();
  }

  CSSToParentLayerScale GetZoom() const {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    return Metrics().GetZoom();
  }

  CSSRect GetVisualViewport() const {
    MOZ_ASSERT(IsRootContent());
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    return Metrics().GetVisualViewport();
  }

  CSSPoint GetLayoutScrollOffset() const {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    return Metrics().GetLayoutScrollOffset();
  }

  // Returns the delta for the given InputData.
  ParentLayerPoint GetDeltaForEvent(const InputData& aEvent) const;

  /**
   * Get the current scroll range of the scrollable frame coreesponding to this
   * APZC.
   */
  CSSRect GetCurrentScrollRangeInCssPixels() const;

  bool AllowOneTouchPinch() const;

 private:
  /**
   * Advances to the next sample, if there is one, the list of sampled states
   * stored in mSampledState. This will make the result of
   * |GetCurrentAsyncTransform(eForCompositing)| and similar functions reflect
   * the async scroll offset and zoom of the next sample. See also
   * SampleCompositedAsyncTransform which creates the samples.
   */
  void AdvanceToNextSample();

  /**
   * Returns whether we have changes to the scroll offsets which need to be
   * sampled in the next couple of frames (it depends on how many offsets we
   * have, currently it's two).
   */
  bool HavePendingFrameDelayedOffset() const;

  /**
   * Samples the composited async transform, storing the result into
   * mSampledState. This will make the result of
   * |GetCurrentAsyncTransform(eForCompositing)| and similar functions reflect
   * the async scroll offset and zoom stored in |Metrics()| when the sample
   * is activated via some future call to |AdvanceToNextSample|.
   *
   * Returns true if the newly sampled value is different from the last
   * sampled value.
   */
  bool SampleCompositedAsyncTransform(
      const RecursiveMutexAutoLock& aProofOfLock);

  /**
   * Updates the sample at the front of mSampledState with the latest
   * metrics. This makes the result of
   * |GetCurrentAsyncTransform(eForCompositing)| reflect the current Metrics().
   */
  void ResampleCompositedAsyncTransform(
      const RecursiveMutexAutoLock& aProofOfLock);

  /*
   * Helper functions to query the async layout viewport, scroll offset, and
   * zoom either directly from |Metrics()|, or from cached variables that
   * store the required value from the last time it was sampled by calling
   * SampleCompositedAsyncTransform(), depending on who is asking.
   */
  CSSRect GetEffectiveLayoutViewport(AsyncTransformConsumer aMode,
                                     const RecursiveMutexAutoLock& aProofOfLock,
                                     std::size_t aSampleIndex = 0) const;
  CSSPoint GetEffectiveScrollOffset(AsyncTransformConsumer aMode,
                                    const RecursiveMutexAutoLock& aProofOfLock,
                                    std::size_t aSampleIndex = 0) const;
  CSSToParentLayerScale GetEffectiveZoom(
      AsyncTransformConsumer aMode, const RecursiveMutexAutoLock& aProofOfLock,
      std::size_t aSampleIndex = 0) const;

  /**
   * Returns the visible portion of the content scrolled by this APZC, in
   * CSS pixels. The caller must have acquired the mRecursiveMutex lock.
   */
  CSSRect GetVisibleRect(const RecursiveMutexAutoLock& aProofOfLock) const;

  /**
   * Returns a pair of displacements both in logical/physical units for
   * |aEvent|.
   */
  std::tuple<ParentLayerPoint, ScreenPoint> GetDisplacementsForPanGesture(
      const PanGestureInput& aEvent);

  CSSPoint ToCSSPixels(ParentLayerPoint value) const;
  CSSCoord ToCSSPixels(ParentLayerCoord value) const;

 private:
  friend class AutoApplyAsyncTestAttributes;
  friend class AutoDynamicToolbarHider;

  bool SuppressAsyncScrollOffset() const;

  /**
   * Applies |mTestAsyncScrollOffset| and |mTestAsyncZoom| to this
   * AsyncPanZoomController. Calls |SampleCompositedAsyncTransform| to ensure
   * that the GetCurrentAsync* functions consider the test offset and zoom in
   * their computations.
   */
  void ApplyAsyncTestAttributes(const RecursiveMutexAutoLock& aProofOfLock);

  /**
   * Sets this AsyncPanZoomController's FrameMetrics to |aPrevFrameMetrics| and
   * calls |SampleCompositedAsyncTransform| to unapply any test values applied
   * by |ApplyAsyncTestAttributes|.
   */
  void UnapplyAsyncTestAttributes(const RecursiveMutexAutoLock& aProofOfLock,
                                  const FrameMetrics& aPrevFrameMetrics,
                                  const ParentLayerPoint& aPrevOverscroll);

  /* ===================================================================
   * The functions and members in this section are used to manage
   * the state that tracks what this APZC is doing with the input events.
   */
 protected:
  enum PanZoomState {
    NOTHING,  /* no touch-start events received */
    FLING,    /* all touches removed, but we're still scrolling page */
    TOUCHING, /* one touch-start event received */

    PANNING,          /* panning the frame */
    PANNING_LOCKED_X, /* touch-start followed by move (i.e. panning with axis
                         lock) X axis */
    PANNING_LOCKED_Y, /* as above for Y axis */

    PAN_MOMENTUM, /* like PANNING, but controlled by momentum PanGestureInput
                     events */

    PINCHING, /* nth touch-start, where n > 1. this mode allows pan and zoom */
    ANIMATING_ZOOM,       /* animated zoom to a new rect */
    OVERSCROLL_ANIMATION, /* Spring-based animation used to relieve overscroll
                             once the finger is lifted. */
    SMOOTH_SCROLL,        /* Smooth scrolling to destination, with physics
                             controlled by prefs specific to the scroll origin. */
    SMOOTHMSD_SCROLL,     /* SmoothMSD scrolling to destination. Used by
                             CSSOM-View smooth scroll-behavior */
    WHEEL_SCROLL,    /* Smooth scrolling to a destination for a wheel event. */
    KEYBOARD_SCROLL, /* Smooth scrolling to a destination for a keyboard event.
                      */
    AUTOSCROLL,      /* Autoscroll animation. */
    SCROLLBAR_DRAG   /* Async scrollbar drag. */
  };
  // This is in theory protected by |mRecursiveMutex|; that is, it should be
  // held whenever this is updated. In practice though... see bug 897017.
  PanZoomState mState;

  AxisX mX;
  AxisY mY;

  /**
   * Returns wheter the given input state is a user pan-gesture.
   *
   * Note: momentum pan gesture states are not considered a panning state.
   */
  static bool IsPanningState(PanZoomState aState);

  /**
   * Returns wheter a delayed transform end is queued.
   */
  bool IsDelayedTransformEndSet();

  /**
   * Returns wheter a delayed transform end is queued.
   */
  void SetDelayedTransformEnd(bool aDelayedTransformEnd);

  /**
   * Returns whether the specified PanZoomState does not need to be reset when
   * a scroll offset update is processed.
   */
  static bool CanHandleScrollOffsetUpdate(PanZoomState aState);

  /**
   * Determine whether a main-thread scroll offset update should result in
   * a call to CancelAnimation() (which interrupts in-progress animations and
   * gestures).
   *
   * If the update is a relative update, |aRelativeDelta| contains its amount.
   * If the update is not a relative update, GetMetrics() should already reflect
   * the new offset at the time of the call.
   */
  bool ShouldCancelAnimationForScrollUpdate(
      const Maybe<CSSPoint>& aRelativeDelta);

 private:
  friend class StateChangeNotificationBlocker;
  friend class ThreadSafeStateChangeNotificationBlocker;
  /**
   * A counter of how many StateChangeNotificationBlockers are active.
   * A non-zero count will prevent state change notifications from
   * being dispatched. Only code that holds mRecursiveMutex should touch this.
   */
  int mNotificationBlockers;

  /**
   * Helper to set the current state, without content controller events
   * for the state change. This is useful in cases where the content
   * controller events may need to be delayed.
   */
  PanZoomState SetStateNoContentControllerDispatch(PanZoomState aNewState);

  /**
   * Helper to set the current state. Holds mRecursiveMutex before actually
   * setting it and fires content controller events based on state changes.
   * Always set the state using this call, do not set it directly.
   */
  void SetState(PanZoomState aNewState);
  /**
   * Helper for getting the current state which acquires mRecursiveMutex
   * before accessing the field.
   */
  PanZoomState GetState() const;
  /**
   * Fire content controller notifications about state changes, assuming no
   * StateChangeNotificationBlocker has been activated.
   */
  void DispatchStateChangeNotification(PanZoomState aOldState,
                                       PanZoomState aNewState);

  /**
   * Send a TransformBegin notification followed by a TransformEnd
   * notification.
   */
  void SendTransformBeginAndEnd();

  /**
   * Internal helpers for checking general state of this apzc.
   */
  bool IsInTransformingState() const;
  static bool IsTransformingState(PanZoomState aState);

  /* ===================================================================
   * The functions and members in this section are used to manage
   * blocks of touch events and the state needed to deal with content
   * listeners.
   */
 public:
  /**
   * Flush a repaint request if one is needed, without throttling it with the
   * paint throttler.
   */
  void FlushRepaintForNewInputBlock();

  /**
   * Given an input event and the touch block it belongs to, check if the
   * event can lead to a panning/zooming behavior.
   * This is used for logic related to the pointer events spec (figuring out
   * when to dispatch the pointercancel event), as well as an input to the
   * computation of the APZHandledResult for the event (used on Android to
   * govern dynamic toolbar and pull-to-refresh behaviour).
   */
  PointerEventsConsumableFlags ArePointerEventsConsumable(
      const TouchBlockState* aBlock, const MultiTouchInput& aInput) const;

  /**
   * Clear internal state relating to touch input handling.
   */
  void ResetTouchInputState();

  /**
     Clear internal state relating to pan gesture input handling.
   */
  void ResetPanGestureInputState();

  /**
   * Gets a ref to the input queue that is shared across the entire tree
   * manager.
   */
  const RefPtr<InputQueue>& GetInputQueue() const;

 private:
  void CancelAnimationAndGestureState();

  RefPtr<InputQueue> mInputQueue;
  InputBlockState* GetCurrentInputBlock() const;
  TouchBlockState* GetCurrentTouchBlock() const;
  bool HasReadyTouchBlock() const;

  PanGestureBlockState* GetCurrentPanGestureBlock() const;
  PinchGestureBlockState* GetCurrentPinchGestureBlock() const;

 private:
  /* ===================================================================
   * The functions and members in this section are used to manage
   * fling animations, smooth scroll animations, and overscroll
   * during a fling or smooth scroll.
   */
 public:
  /**
   * Attempt a fling with the velocity specified in |aHandoffState|.
   * |aHandoffState.mIsHandoff| should be true iff. the fling was handed off
   * from a previous APZC, and determines whether acceleration is applied
   * to the fling.
   * We only accept the fling in the direction(s) in which we are pannable.
   * Returns the "residual velocity", i.e. the portion of
   * |aHandoffState.mVelocity| that this APZC did not consume.
   */
  ParentLayerPoint AttemptFling(const FlingHandoffState& aHandoffState);

  ParentLayerPoint AdjustHandoffVelocityForOverscrollBehavior(
      ParentLayerPoint& aHandoffVelocity) const;

 private:
  friend class StackScrollerFlingAnimation;
  friend class AutoscrollAnimation;
  template <typename FlingPhysics>
  friend class GenericFlingAnimation;
  friend class AndroidFlingPhysics;
  friend class DesktopFlingPhysics;
  friend class OverscrollAnimation;
  friend class SmoothMsdScrollAnimation;
  friend class GenericScrollAnimation;
  friend class WheelScrollAnimation;
  friend class ZoomAnimation;

  friend class GenericOverscrollEffect;
  friend class WidgetOverscrollEffect;
  friend struct apz::AsyncScrollThumbTransformer;

  FlingAccelerator mFlingAccelerator;

  // Indicates if the repaint-during-pinch timer is currently set
  bool mPinchPaintTimerSet;

  // Indicates a delayed transform end notification is queued, and the
  // transform-end timer is currently set. mRecursiveMutex must be held
  // when using or modifying this member.
  bool mDelayedTransformEnd;

  // Deal with overscroll resulting from a fling animation. This is only ever
  // called on APZC instances that were actually performing a fling.
  // The overscroll is handled by trying to hand the fling off to an APZC
  // later in the handoff chain, or if there are no takers, continuing the
  // fling and entering an overscrolled state.
  void HandleFlingOverscroll(
      const ParentLayerPoint& aVelocity, SideBits aOverscrollSideBits,
      const RefPtr<const OverscrollHandoffChain>& aOverscrollHandoffChain,
      const RefPtr<const AsyncPanZoomController>& aScrolledApzc);

  // Start an overscroll animation with the given initial velocity.
  void StartOverscrollAnimation(const ParentLayerPoint& aVelocity,
                                SideBits aOverscrollSideBits);

  // Start a smooth-scrolling animation to the given destination, with physics
  // based on the prefs for the indicated origin.
  void SmoothScrollTo(CSSSnapDestination&& aDestination,
                      ScrollTriggeredByScript aTriggeredByScript,
                      const ScrollOrigin& aOrigin);

  ParentLayerPoint ConvertDestinationToDelta(CSSPoint& aDestination) const;

  // Start a smooth-scrolling animation to the given destination, with MSD
  // physics that is suited for scroll-snapping.
  void SmoothMsdScrollTo(CSSSnapDestination&& aDestination,
                         ScrollTriggeredByScript aTriggeredByScript);

  // Returns whether overscroll is allowed during an event.
  bool AllowScrollHandoffInCurrentBlock() const;

  // Invoked by the pinch repaint timer.
  void DoDelayedRequestContentRepaint();

  // Invoked by the on pan-end handler to ensure that scrollend is only
  // fired once when a momentum pan or scroll snap is triggered as a
  // result of the pan gesture.
  void DoDelayedTransformEndNotification(PanZoomState aOldState);

  // Compute the number of ParentLayer pixels per (Screen) inch at the given
  // point and in the given direction.
  float ComputePLPPI(ParentLayerPoint aPoint,
                     ParentLayerPoint aDirection) const;

  Maybe<CSSPoint> GetCurrentAnimationDestination(
      const RecursiveMutexAutoLock& aProofOfLock) const;

  /* ===================================================================
   * The functions and members in this section are used to make ancestor chains
   * out of APZC instances. These chains can only be walked or manipulated
   * while holding the lock in the associated APZCTreeManager instance.
   */
 public:
  void SetParent(AsyncPanZoomController* aParent) { mParent = aParent; }

  AsyncPanZoomController* GetParent() const { return mParent; }

  /* Returns true if there is no APZC higher in the tree with the same
   * layers id.
   */
  bool HasNoParentWithSameLayersId() const {
    return !mParent || (mParent->mLayersId != mLayersId);
  }

  bool IsRootForLayersId() const {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    return mScrollMetadata.IsLayersIdRoot();
  }

  bool IsRootContent() const {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    return Metrics().IsRootContent();
  }

 private:
  // |mTreeManager| belongs in this section but it's declaration is a bit
  // further above due to initialization-order constraints.

  RefPtr<AsyncPanZoomController> mParent;

  /* ===================================================================
   * The functions and members in this section are used for scrolling,
   * including handing off scroll to another APZC, and overscrolling.
   */

  ScrollableLayerGuid::ViewID GetScrollId() const {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    return Metrics().GetScrollId();
  }

 public:
  ScrollableLayerGuid::ViewID GetScrollHandoffParentId() const {
    return mScrollMetadata.GetScrollParentId();
  }

  /**
   * Attempt to scroll in response to a touch-move from |aStartPoint| to
   * |aEndPoint|, which are in our (transformed) screen coordinates.
   * Due to overscroll handling, there may not actually have been a touch-move
   * at these points, but this function will scroll as if there had been.
   * If this attempt causes overscroll (i.e. the layer cannot be scrolled
   * by the entire amount requested), the overscroll is passed back to the
   * tree manager via APZCTreeManager::DispatchScroll(). If the tree manager
   * does not find an APZC further in the handoff chain to accept the
   * overscroll, and this APZC is pannable, this APZC enters an overscrolled
   * state.
   * |aOverscrollHandoffChain| and |aOverscrollHandoffChainIndex| are used by
   * the tree manager to keep track of which APZC to hand off the overscroll
   * to; this function increments the chain and the index and passes it on to
   * APZCTreeManager::DispatchScroll() in the event of overscroll.
   * Returns true iff. this APZC, or an APZC further down the
   * handoff chain, accepted the scroll (possibly entering an overscrolled
   * state). If this returns false, the caller APZC knows that it should enter
   * an overscrolled state itself if it can.
   * aStartPoint and aEndPoint are modified depending on how much of the
   * scroll gesture was consumed by APZCs in the handoff chain.
   */
  bool AttemptScroll(ParentLayerPoint& aStartPoint, ParentLayerPoint& aEndPoint,
                     OverscrollHandoffState& aOverscrollHandoffState);

  void FlushRepaintForOverscrollHandoff();

  /**
   * If overscrolled, start a snap-back animation and return true. Even if not
   * overscrolled, this function tries to snap back to if there's an applicable
   * scroll snap point.
   * Otherwise return false.
   */
  bool SnapBackIfOverscrolled();

  /**
   * NOTE: Similar to above but this function doesn't snap back to the scroll
   * snap point.
   */
  bool SnapBackIfOverscrolledForMomentum(const ParentLayerPoint& aVelocity);

  /**
   * Build the chain of APZCs along which scroll will be handed off when
   * this APZC receives input events.
   *
   * Notes on lifetime and const-correctness:
   *   - The returned handoff chain is |const|, to indicate that it cannot be
   *     changed after being built.
   *   - When passing the chain to a function that uses it without storing it,
   *     pass it by reference-to-const (as in |const OverscrollHandoffChain&|).
   *   - When storing the chain, store it by RefPtr-to-const (as in
   *     |RefPtr<const OverscrollHandoffChain>|). This ensures the chain is
   *     kept alive. Note that queueing a task that uses the chain as an
   *     argument constitutes storing, as the task may outlive its queuer.
   *   - When passing the chain to a function that will store it, pass it as
   *     |const RefPtr<const OverscrollHandoffChain>&|. This allows the
   *     function to copy it into the |RefPtr<const OverscrollHandoffChain>|
   *     that will store it, while avoiding an unnecessary copy (and thus
   *     AddRef() and Release()) when passing it.
   */
  RefPtr<const OverscrollHandoffChain> BuildOverscrollHandoffChain();

 private:
  /**
   * A helper function for calling APZCTreeManager::DispatchScroll().
   * Guards against the case where the APZC is being concurrently destroyed
   * (and thus mTreeManager is being nulled out).
   */
  bool CallDispatchScroll(ParentLayerPoint& aStartPoint,
                          ParentLayerPoint& aEndPoint,
                          OverscrollHandoffState& aOverscrollHandoffState);

  void RecordScrollPayload(const TimeStamp& aTimeStamp);

  /**
   * A helper function for overscrolling during panning. This is a wrapper
   * around OverscrollBy() that also implements restrictions on entering
   * overscroll based on the pan angle.
   */
  void OverscrollForPanning(ParentLayerPoint& aOverscroll,
                            const ScreenPoint& aPanDistance);

  /**
   * Try to overscroll by 'aOverscroll'.
   * If we are pannable on a particular axis, that component of 'aOverscroll'
   * is transferred to any existing overscroll.
   */
  void OverscrollBy(ParentLayerPoint& aOverscroll);

  /* ===================================================================
   * The functions and members in this section are used to maintain the
   * area that this APZC instance is responsible for. This is used when
   * hit-testing to see which APZC instance should handle touch events.
   */
 public:
  void SetAncestorTransform(const AncestorTransform& aAncestorTransform) {
    mAncestorTransform = aAncestorTransform;
  }

  Matrix4x4 GetAncestorTransform() const {
    return mAncestorTransform.CombinedTransform();
  }

  bool AncestorTransformContainsPerspective() const {
    return mAncestorTransform.ContainsPerspectiveTransform();
  }

  // Return the perspective transform component of the ancestor transform.
  Matrix4x4 GetAncestorTransformPerspective() const {
    return mAncestorTransform.GetPerspectiveTransform();
  }

  // Returns whether or not this apzc contains the given screen point within
  // its composition bounds.
  bool Contains(const ScreenIntPoint& aPoint) const;

  bool IsInOverscrollGutter(const ScreenPoint& aHitTestPoint) const;
  bool IsInOverscrollGutter(const ParentLayerPoint& aHitTestPoint) const;

  bool IsOverscrolled() const;

  bool IsOverscrollAnimationRunning() const {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    return mState == OVERSCROLL_ANIMATION;
  }

  // IsPhysicallyOverscrolled() checks whether the APZC is overscrolled
  // by an overscroll effect which applies a transform to the APZC's contents.
  bool IsPhysicallyOverscrolled() const;

 private:
  bool IsInInvalidOverscroll() const;

 public:
  bool IsInPanningState() const;

  // Returns whether being in the middle of a gesture. E.g., this APZC has
  // started handling a pan gesture but hasn't yet received pan-end, etc.
  bool IsInScrollingGesture() const;

 private:
  /* This is the cumulative CSS transform for all the layers from (and
   * including) the parent APZC down to (but excluding) this one, and excluding
   * any perspective transforms. */
  AncestorTransform mAncestorTransform;

  /* ===================================================================
   * The functions and members in this section are used for testing
   * and assertion purposes only.
   */
 public:
  /**
   * Gets whether this APZC has performed async key scrolling.
   */
  bool TestHasAsyncKeyScrolled() const { return mTestHasAsyncKeyScrolled; }

  /**
   * Set an extra offset for testing async scrolling.
   */
  void SetTestAsyncScrollOffset(const CSSPoint& aPoint);
  /**
   * Set an extra offset for testing async scrolling.
   */
  void SetTestAsyncZoom(const LayerToParentLayerScale& aZoom);

  LayersId GetLayersId() const { return mLayersId; }

  bool IsAsyncZooming() const {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    return mState == PINCHING || mState == ANIMATING_ZOOM;
  }

 private:
  // The timestamp of the latest touch start event.
  TimeStamp mTouchStartTime;
  // Used for interpolating touch events that cross the touch-start
  // tolerance threshold.
  struct TouchSample {
    ExternalPoint mPosition;
    TimeStamp mTimeStamp;
  };
  // Information about the latest touch event.
  // This is only populated when we're in the TOUCHING state
  // (and thus the last touch event has only one touch point).
  TouchSample mLastTouch;
  // The time duration between mTouchStartTime and the touchmove event that
  // started the pan (the touchmove event that transitioned this APZC from the
  // TOUCHING state to one of the PANNING* states). Only valid while this APZC
  // is in a panning state.
  TimeDuration mTouchStartRestingTimeBeforePan;
  Maybe<ParentLayerCoord> mMinimumVelocityDuringPan;
  // This variable needs to be protected by |mRecursiveMutex|.
  ScrollSnapTargetIds mLastSnapTargetIds;
  // Extra offset to add to the async scroll position for testing
  CSSPoint mTestAsyncScrollOffset;
  // Extra zoom to include in the aync zoom for testing
  LayerToParentLayerScale mTestAsyncZoom;
  uint8_t mTestAttributeAppliers;
  // Flag to track whether or not this APZC has ever async key scrolled.
  bool mTestHasAsyncKeyScrolled;

  /* ===================================================================
   * The functions and members in this section are used for checkerboard
   * recording.
   */
 private:
  // Helper function to update the in-progress checkerboard event, if any.
  void UpdateCheckerboardEvent(const MutexAutoLock& aProofOfLock,
                               uint32_t aMagnitude);

  // Mutex protecting mCheckerboardEvent
  Mutex mCheckerboardEventLock;
  // This is created when this APZC instance is first included as part of a
  // composite. If a checkerboard event takes place, this is destroyed at the
  // end of the event, and a new one is created on the next composite.
  UniquePtr<CheckerboardEvent> mCheckerboardEvent;
  // This is used to track the total amount of time that we could reasonably
  // be checkerboarding. Combined with other info, this allows us to
  // meaningfully say how frequently users actually encounter checkerboarding.
  PotentialCheckerboardDurationTracker mPotentialCheckerboardTracker;

  /* ===================================================================
   * The functions in this section are used for CSS scroll snapping.
   */

  // If moving |aStartPosition| by |aDelta| should trigger scroll snapping,
  // adjust |aDelta| to reflect the snapping (that is, make it a delta that will
  // take us to the desired snap point). The delta is interpreted as being
  // relative to |aStartPosition|, and if a target snap point is found,
  // |aStartPosition| is also updated, to the value of the snap point.
  // |aUnit| affects the snapping behaviour (see ScrollSnapUtils::
  // GetSnapPointForDestination).
  // Returns true iff. a target snap point was found.
  Maybe<CSSSnapDestination> MaybeAdjustDeltaForScrollSnapping(
      ScrollUnit aUnit, ScrollSnapFlags aSnapFlags, ParentLayerPoint& aDelta,
      CSSPoint& aStartPosition);

  // A wrapper function of MaybeAdjustDeltaForScrollSnapping for
  // ScrollWheelInput.
  Maybe<CSSSnapDestination> MaybeAdjustDeltaForScrollSnappingOnWheelInput(
      const ScrollWheelInput& aEvent, ParentLayerPoint& aDelta,
      CSSPoint& aStartPosition);

  Maybe<CSSSnapDestination> MaybeAdjustDestinationForScrollSnapping(
      const KeyboardInput& aEvent, CSSPoint& aDestination,
      ScrollSnapFlags aSnapFlags);

  // Snap to a snap position nearby the current scroll position, if appropriate.
  void ScrollSnap(ScrollSnapFlags aSnapFlags);

  // Snap to a snap position nearby the destination predicted based on the
  // current velocity, if appropriate.
  void ScrollSnapToDestination();

  // Snap to a snap position nearby the provided destination, if appropriate.
  void ScrollSnapNear(const CSSPoint& aDestination, ScrollSnapFlags aSnapFlags);

  // Find a snap point near |aDestination| that we should snap to.
  // Returns the snap point if one was found, or an empty Maybe otherwise.
  // |aUnit| affects the snapping behaviour (see ScrollSnapUtils::
  // GetSnapPointForDestination). It should generally be determined by the
  // type of event that's triggering the scroll.
  Maybe<CSSSnapDestination> FindSnapPointNear(const CSSPoint& aDestination,
                                              ScrollUnit aUnit,
                                              ScrollSnapFlags aSnapFlags);

  // If |aOriginalEvent| crosses the touch-start tolerance threshold, split it
  // into two events: one that just reaches the threshold, and the remainder.
  //
  // |aPanThreshold| is the touch-start tolerance, and |aVectorLength| is
  // the length of the vector from the touch-start position to |aOriginalEvent|.
  // These values could be computed from |aOriginalEvent| but they are
  // passed in for convenience since the caller also needs to compute them.
  //
  // |aExtPoint| is the position of |aOriginalEvent| in External coordinates,
  // and in case of a split is modified by the function to reflect the position
  // of of the first event. This is a workaround for the fact that recomputing
  // the External position from the returned event would require a round-trip
  // through |mScreenPoint| which is an integer.
  Maybe<std::pair<MultiTouchInput, MultiTouchInput>> MaybeSplitTouchMoveEvent(
      const MultiTouchInput& aOriginalEvent, ScreenCoord aPanThreshold,
      float aVectorLength, ExternalPoint& aExtPoint);

  // Fill out the overscroll gutter with the new expanded contents the
  // overscroll amount is inside the new scroll range. Returns the scroll
  // position change delta if filling out happened, CSSPoint() otherwise.
  CSSPoint MaybeFillOutOverscrollGutter(
      const RecursiveMutexAutoLock& aProofOfLock);

  friend std::ostream& operator<<(
      std::ostream& aOut, const AsyncPanZoomController::PanZoomState& aState);
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_PanZoomController_h
