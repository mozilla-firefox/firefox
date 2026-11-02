/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef InputData_h_
#define InputData_h_

#include "Units.h"
#include "mozilla/DefineEnum.h"
#include "mozilla/EventForwards.h"
#include "mozilla/Maybe.h"
#include "mozilla/MouseEvents.h"  // for WidgetPointerHelper::Angle
#include "mozilla/ScrollTypes.h"
#include "mozilla/TextEvents.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/WheelHandlingHelper.h"  // for WheelDeltaAdjustmentStrategy
#include "mozilla/gfx/MatrixFwd.h"
#include "mozilla/ipc/IPCForwards.h"
#include "mozilla/layers/APZPublicUtils.h"
#include "mozilla/layers/KeyboardScrollAction.h"
#include "nsDebug.h"
#include "nsPoint.h"
#include "nsTArray.h"

template <class E>
struct already_AddRefed;
class nsIWidget;

enum TouchPointerState : uint8_t;

namespace mozilla {

namespace layers {
class APZBridgeChild;
class PAPZBridgeParent;
}  // namespace layers

namespace dom {
class Touch;
}  // namespace dom

// clang-format off
MOZ_DEFINE_ENUM(
  InputType, (
    MULTITOUCH_INPUT,
    MOUSE_INPUT,
    PANGESTURE_INPUT,
    PINCHGESTURE_INPUT,
    TAPGESTURE_INPUT,
    SCROLLWHEEL_INPUT,
    KEYBOARD_INPUT
));
// clang-format on

class MultiTouchInput;
class MouseInput;
class PanGestureInput;
class PinchGestureInput;
class TapGestureInput;
class ScrollWheelInput;
class KeyboardInput;

// This looks unnecessary now, but as we add more and more classes that derive
// from InputType (eventually probably almost as many as *Events.h has), it
// will be more and more clear what's going on with a macro that shortens the
// definition of the RTTI functions.
#define INPUTDATA_AS_CHILD_TYPE(type, enumID)                       \
  const type& As##type() const {                                    \
    MOZ_ASSERT(mInputType == enumID, "Invalid cast of InputData."); \
    return (const type&)*this;                                      \
  }                                                                 \
  type& As##type() {                                                \
    MOZ_ASSERT(mInputType == enumID, "Invalid cast of InputData."); \
    return (type&)*this;                                            \
  }

/** Base input data class. Should never be instantiated. */
class InputData {
 public:
  // Warning, this class is serialized and sent over IPC. Any change to its
  // fields must be reflected in its ParamTraits<>, in nsGUIEventIPC.h
  InputType mInputType;
  // Time that this data is relevant to. This only really matters when this data
  // is used as an event.
  TimeStamp mTimeStamp;
  // The sequence number of the last potentially focus changing event handled
  // by APZ. This is used to track when that event has been processed by
  // content, and focus can be reconfirmed for async keyboard scrolling.
  uint64_t mFocusSequenceNumber;

  // The LayersId of the content process that the corresponding WidgetEvent
  // should be dispatched to.
  layers::LayersId mLayersId;

  /**
   * An optional identifier for the callback associated with this input event.
   * This ID is used to reference a specific callback for a synthesized event,
   * if one is present. If no callback is associated, this value will be empty.
   */
  Maybe<uint64_t> mCallbackId;

  Modifiers modifiers;

  INPUTDATA_AS_CHILD_TYPE(MultiTouchInput, MULTITOUCH_INPUT)
  INPUTDATA_AS_CHILD_TYPE(MouseInput, MOUSE_INPUT)
  INPUTDATA_AS_CHILD_TYPE(PanGestureInput, PANGESTURE_INPUT)
  INPUTDATA_AS_CHILD_TYPE(PinchGestureInput, PINCHGESTURE_INPUT)
  INPUTDATA_AS_CHILD_TYPE(TapGestureInput, TAPGESTURE_INPUT)
  INPUTDATA_AS_CHILD_TYPE(ScrollWheelInput, SCROLLWHEEL_INPUT)
  INPUTDATA_AS_CHILD_TYPE(KeyboardInput, KEYBOARD_INPUT)

  virtual ~InputData();
  explicit InputData(InputType aInputType);

 protected:
  InputData(InputType aInputType, TimeStamp aTimeStamp, Modifiers aModifiers);
  InputData(InputType aInputType, TimeStamp aTimeStamp,
            const Maybe<uint64_t>& aCallback, Modifiers aModifiers);
};

/**
 * Data container for a single touch input. Similar to dom::Touch, but used in
 * off-main-thread situations. This is more for just storing touch data, whereas
 * dom::Touch is more useful for dispatching through the DOM (which can only
 * happen on the main thread). dom::Touch also bears the problem of storing
 * pointers to nsIWidget instances which can only be used on the main thread,
 * so if instead we used dom::Touch and ever set these pointers
 * off-main-thread, Bad Things Can Happen(tm).
 *
 * Note that this doesn't inherit from InputData because this itself is not an
 * event. It is only a container/struct that should have any number of instances
 * within a MultiTouchInput.
 *
 * fixme/bug 775746: Make dom::Touch inherit from this class.
 */
class SingleTouchData {
 public:
  // Construct a SingleTouchData from a Screen point.
  // mLocalScreenPoint remains (0,0) unless it's set later.
  SingleTouchData(int32_t aIdentifier, ScreenIntPoint aScreenPoint,
                  ScreenSize aRadius, float aRotationAngle, float aForce);

  // Construct a SingleTouchData from a ParentLayer point.
  // mScreenPoint remains (0,0) unless it's set later.
  // Note: if APZ starts using the radius for anything, we should add a local
  // version of that too, and have this constructor take it as a
  // ParentLayerSize.
  SingleTouchData(int32_t aIdentifier, ParentLayerPoint aLocalScreenPoint,
                  ScreenSize aRadius, float aRotationAngle, float aForce);

  SingleTouchData();

  already_AddRefed<dom::Touch> ToNewDOMTouch() const;

  // Warning, this class is serialized and sent over IPC. Any change to its
  // fields must be reflected in its ParamTraits<>, in nsGUIEventIPC.h

  // Historical data of this touch, which  was coalesced into this event.
  // Touch event coalescing can happen at the system level when the touch
  // screen's sampling frequency is higher than the vsync rate, or when the
  // UI thread is busy. When multiple "samples" of touch data are coalesced into
  // one touch event, the touch event's regular position information is the
  // information from the last sample. And the previous, "coalesced-away"
  // samples are stored in mHistoricalData.

  struct HistoricalTouchData {
    // The timestamp at which the information in this "sample" was originally
    // sampled.
    TimeStamp mTimeStamp;

    // The touch data of this historical sample.
    ScreenIntPoint mScreenPoint;
    ParentLayerPoint mLocalScreenPoint;
    ScreenSize mRadius;
    float mRotationAngle = 0.0f;
    float mForce = 0.0f;
  };
  CopyableTArray<HistoricalTouchData> mHistoricalData;

  // A unique number assigned to each SingleTouchData within a MultiTouchInput
  // so that they can be easily distinguished when handling a touch
  // start/move/end.
  int32_t mIdentifier;

  // Point on the screen that the touch hit, in device pixels. They are
  // coordinates on the screen.
  ScreenIntPoint mScreenPoint;

  // |mScreenPoint| transformed to the local coordinates of the APZC targeted
  // by the hit. This is set and used by APZ.
  ParentLayerPoint mLocalScreenPoint;

  // Radius that the touch covers, i.e. if you're using your thumb it will
  // probably be larger than using your pinky, even with the same force.
  // Radius can be different along x and y. For example, if you press down with
  // your entire finger vertically, the y radius will be much larger than the x
  // radius.
  ScreenSize mRadius;

  float mRotationAngle;

  // How hard the screen is being pressed.
  float mForce;

  int32_t mTiltX = 0;
  int32_t mTiltY = 0;
  int32_t mTwist = 0;

  // Altitude and azimuth angles of the touch, mirroring dom::Touch's mAngle. A
  // touch carries either these angles or mTiltX/mTiltY, not both; when set they
  // take precedence over the tilt values.
  Maybe<WidgetPointerHelper::Angle> mAngle;
};

/**
 * Similar to WidgetTouchEvent, but for use off-main-thread. Also only stores a
 * screen touch point instead of the many different coordinate spaces
 * WidgetTouchEvent stores its touch point in. This includes a way to initialize
 * itself from a WidgetTouchEvent by copying all relevant data over. Note that
 * this copying from WidgetTouchEvent functionality can only be used on the main
 * thread.
 *
 * Stores an array of SingleTouchData.
 */
class MultiTouchInput : public InputData {
 public:
  // clang-format off
  MOZ_DEFINE_ENUM_AT_CLASS_SCOPE(
    MultiTouchType, (
      MULTITOUCH_START,
      MULTITOUCH_MOVE,
      MULTITOUCH_END,
      MULTITOUCH_CANCEL
  ));
  // clang-format on

  MultiTouchInput(MultiTouchType aType, uint32_t aTime, TimeStamp aTimeStamp,
                  Modifiers aModifiers);
  MultiTouchInput();
  MultiTouchInput(MultiTouchInput&&) = default;
  MultiTouchInput(const MultiTouchInput&) = default;
  explicit MultiTouchInput(const WidgetTouchEvent& aTouchEvent);

  MultiTouchInput& operator=(MultiTouchInput&&) = default;
  MultiTouchInput& operator=(const MultiTouchInput&) = default;

  void Translate(const ScreenPoint& aTranslation);

  WidgetTouchEvent ToWidgetEvent(nsIWidget* aWidget) const;

  // Return the index into mTouches of the SingleTouchData with the given
  // identifier, or -1 if there is no such SingleTouchData.
  int32_t IndexOfTouch(int32_t aTouchIdentifier);

  bool TransformToLocal(const ScreenToParentLayerMatrix4x4& aTransform);

  // Warning, this class is serialized and sent over IPC. Any change to its
  // fields must be reflected in its ParamTraits<>, in nsGUIEventIPC.h
  MultiTouchType mType;
  CopyableTArray<SingleTouchData> mTouches;
  // The screen offset of the root widget. This can be changing along with
  // the touch interaction, so we sstore it in the event.
  ExternalPoint mScreenOffset;
  bool mHandledByAPZ;
  // Whether the caller is still considering this touch block for a browser
  // swipe. This is false after the caller accepts or rejects the swipe, or
  // when the caller does not consider it at all on MULTITOUCH_START.
  bool mMayTriggerSwipe = false;
  // These button fields match to the corresponding fields in
  // WidgetMouseEventBase, except mButton defaults to -1 to follow PointerEvent.
  int16_t mButton = eNotPressed;
  int16_t mButtons = 0;
  uint16_t mInputSource = /* MouseEvent_Binding::MOZ_SOURCE_TOUCH = */ 5;
};

class MouseInput : public InputData {
 protected:
  friend mozilla::layers::APZBridgeChild;
  friend mozilla::layers::PAPZBridgeParent;
  ALLOW_DEPRECATED_READPARAM

  MouseInput();

 public:
  // clang-format off
  MOZ_DEFINE_ENUM_AT_CLASS_SCOPE(
    MouseType, (
      MOUSE_NONE,
      MOUSE_MOVE,
      MOUSE_DOWN,
      MOUSE_UP,
      MOUSE_DRAG_START,
      MOUSE_DRAG_END,
      MOUSE_DRAG_ENTER,
      MOUSE_DRAG_OVER,
      MOUSE_DRAG_EXIT,
      MOUSE_DROP,
      MOUSE_WIDGET_ENTER,
      MOUSE_WIDGET_EXIT,
      MOUSE_HITTEST,
      MOUSE_CONTEXTMENU
  ));

  MOZ_DEFINE_ENUM_AT_CLASS_SCOPE(
    ButtonType, (
      PRIMARY_BUTTON,
      MIDDLE_BUTTON,
      SECONDARY_BUTTON,
      // Typically, "back" button
      X1_BUTTON,
      // Typically, "forward" button
      X2_BUTTON,
      NONE
  ));
  // clang-format on

  MouseInput(MouseType aType, ButtonType aButtonType, uint16_t aInputSource,
             int16_t aButtons, const ScreenPoint& aPoint, TimeStamp aTimeStamp,
             Modifiers aModifiers);
  explicit MouseInput(const WidgetMouseEvent& aMouseEvent);

  bool IsLeftButton() const;

  bool TransformToLocal(const ScreenToParentLayerMatrix4x4& aTransform);
  [[nodiscard]] bool IsPointerEventType() const;
  template <typename WidgetMouseOrPointerEvent>
  WidgetMouseOrPointerEvent ToWidgetEvent(nsIWidget* aWidget) const;

  // Warning, this class is serialized and sent over IPC. Any change to its
  // fields must be reflected in its ParamTraits<>, in nsGUIEventIPC.h
  MouseType mType;
  ButtonType mButtonType;
  uint32_t mClickCount = 0;
  uint16_t mInputSource;
  int16_t mButtons;
  ScreenPoint mOrigin;
  ParentLayerPoint mLocalOrigin;
  bool mHandledByAPZ;
  /**
   * If click event should not be fired in the content after the "mousedown"
   * event or following "mouseup", set to true.
   */
  bool mPreventClickEvent;
  bool mIgnoreCapturingContent;
  bool mSynthesizeMoveAfterDispatch;
};

/**
 * Encapsulation class for pan events, can be used off-main-thread.
 * These events are currently only used for scrolling on desktop.
 */
class PanGestureInput : public InputData {
  friend struct IPC::ParamTraits<PanGestureInput>;

 protected:
  friend mozilla::layers::APZBridgeChild;
  friend mozilla::layers::PAPZBridgeParent;
  ALLOW_DEPRECATED_READPARAM

  PanGestureInput();

 public:
  // clang-format off
  MOZ_DEFINE_ENUM_AT_CLASS_SCOPE(
    PanGestureType, (
      // MayStart: Dispatched before any actual panning has occurred but when a
      // pan gesture is probably about to start, for example when the user
      // starts touching the touchpad. Should interrupt any ongoing APZ
      // animation and can be used to trigger scrollability indicators (e.g.
      // flashing overlay scrollbars).
      PANGESTURE_MAYSTART,

      // Cancelled: Dispatched after MayStart when no pan gesture is going to
      // happen after all, for example when the user lifts their fingers from a
      // touchpad without having done any scrolling.
      PANGESTURE_CANCELLED,

      // Start: A pan gesture is starting.
      // For devices that do not support the MayStart event type, this event can
      // be used to interrupt ongoing APZ animations.
      PANGESTURE_START,

      // Pan: The actual pan motion by mPanDisplacement.
      PANGESTURE_PAN,

      // End: The pan gesture has ended, for example because the user has lifted
      // their fingers from a touchpad after scrolling.
      // Any potential momentum events fire after this event.
      PANGESTURE_END,

      // The following momentum event types are used in order to control the pan
      // momentum animation. Using these instead of our own animation ensures
      // that the animation curve is OS native and that the animation stops
      // reliably if it is cancelled by the user.

      // MomentumStart: Dispatched between the End event of the actual
      // user-controlled pan, and the first MomentumPan event of the momentum
      // animation.
      PANGESTURE_MOMENTUMSTART,

      // MomentumPan: The actual momentum motion by mPanDisplacement.
      PANGESTURE_MOMENTUMPAN,

      // MomentumEnd: The momentum animation has ended, for example because the
      // momentum velocity has gone below the stopping threshold, or because the
      // user has stopped the animation by putting their fingers on a touchpad.
      PANGESTURE_MOMENTUMEND,

      // Interrupted:: A pan gesture started being handled by an APZC but
      // subsequent pan events might have been consumed by other operations
      // which haven't been handled by the APZC (e.g. full zoom).
      PANGESTURE_INTERRUPTED
  ));

  MOZ_DEFINE_ENUM_AT_CLASS_SCOPE(
    PanDeltaType, (
      // There are three kinds of scroll delta modes in Gecko: "page", "line"
      // and "pixel". Touchpad pan gestures only support "page" and "pixel".
      //
      // NOTE: PANDELTA_PAGE currently replicates Gtk behavior
      // (see AsyncPanZoomController::OnPan).
      PANDELTA_PAGE,
      PANDELTA_PIXEL
  ));
  // clang-format on

  PanGestureInput(PanGestureType aType, TimeStamp aTimeStamp,
                  const ScreenPoint& aPanStartPoint,
                  const ScreenPoint& aPanDisplacement, Modifiers aModifiers);

  enum class IsEligibleForSwipe : bool { No, Yes };
  PanGestureInput(PanGestureType aType, TimeStamp aTimeStamp,
                  const ScreenPoint& aPanStartPoint,
                  const ScreenPoint& aPanDisplacement, Modifiers aModifiers,
                  IsEligibleForSwipe aIsEligibleForSwipe);

  void SetLineOrPageDeltas(int32_t aLineOrPageDeltaX,
                           int32_t aLineOrPageDeltaY);

  bool IsMomentum() const;

  WidgetWheelEvent ToWidgetEvent(nsIWidget* aWidget) const;

  bool TransformToLocal(const ScreenToParentLayerMatrix4x4& aTransform);

  ScreenPoint UserMultipliedPanDisplacement() const;
  ParentLayerPoint UserMultipliedLocalPanDisplacement() const;

  void SetHandledByAPZ(bool aHandled) { mHandledByAPZ = aHandled; }
  void SetOverscrollBehaviorAllowsSwipe(bool aAllows) {
    mOverscrollBehaviorAllowsSwipe = aAllows;
  }
  void SetSimulateMomentum(bool aSimulate) { mSimulateMomentum = aSimulate; }
  void SetIsNoLineOrPageDelta(bool aIsNoLineOrPageDelta) {
    mIsNoLineOrPageDelta = aIsNoLineOrPageDelta;
  }

  // Returns true if this pan gesture event is elligible for browser swipe
  // gesture considering the overscroll-behavior property of the target
  // scroll container.
  bool AllowsSwipe() const {
    MOZ_ASSERT(mHandledByAPZ);
    return mMayTriggerSwipe && mOverscrollBehaviorAllowsSwipe;
  }

  bool RequiresContentResponseIfCannotScrollHorizontallyInStartDirection();

  static gfx::IntPoint GetIntegerDeltaForEvent(bool aIsStart, float x, float y);

  // Warning, this class is serialized and sent over IPC. Any change to its
  // fields must be reflected in its ParamTraits<>, in nsGUIEventIPC.h
  PanGestureType mType;
  ScreenPoint mPanStartPoint;

  // The delta. This can be non-zero on any type of event.
  ScreenPoint mPanDisplacement;

  // Versions of |mPanStartPoint| and |mPanDisplacement| in the local
  // coordinates of the APZC receiving the pan. These are set and used by APZ.
  ParentLayerPoint mLocalPanStartPoint;
  ParentLayerPoint mLocalPanDisplacement;

  // See lineOrPageDeltaX/Y on WidgetWheelEvent.
  int32_t mLineOrPageDeltaX;
  int32_t mLineOrPageDeltaY;

  // User-set delta multipliers.
  double mUserDeltaMultiplierX;
  double mUserDeltaMultiplierY;

  PanDeltaType mDeltaType = PANDELTA_PIXEL;

  bool mHandledByAPZ : 1;

  // This is used by APZ to communicate to widget code whether the
  // overscroll-behavior of the scroll frame handling this swipe allows
  // non-local overscroll behaviors in the horizontal direction (such as
  // swipe navigation).
  bool mOverscrollBehaviorAllowsSwipe : 1;

  // true if APZ should do a fling animation after this pan ends, like
  // it would with touchscreens. (For platforms that don't emit momentum
  // events.)
  bool mSimulateMomentum : 1;

  // true if the creator of this object does not set the mLineOrPageDeltaX/Y
  // fields and when/if WidgetWheelEvent's are generated from this object wants
  // the corresponding mLineOrPageDeltaX/Y fields in the WidgetWheelEvent to be
  // automatically calculated (upon event dispatch by the EventStateManager
  // code).
  bool mIsNoLineOrPageDelta : 1;

 private:
  // If this is true, and this event started a new input block that couldn't
  // find a scrollable target which is scrollable in the horizontal component
  // of the scroll start direction, then this input block needs to be put on
  // hold until a content response has arrived, even if the block has a
  // confirmed target.
  // This is used by events that can result in a swipe instead of a scroll.
  bool mMayTriggerSwipe : 1;
  void SetMayTriggerSwipe(bool aValue) { mMayTriggerSwipe = aValue; }
};

/**
 * Encapsulation class for pinch events. In general, these will be generated by
 * a gesture listener by looking at SingleTouchData/MultiTouchInput instances
 * and determining whether or not the user was trying to do a gesture.
 */
class PinchGestureInput : public InputData {
 protected:
  friend mozilla::layers::APZBridgeChild;
  friend mozilla::layers::PAPZBridgeParent;
  ALLOW_DEPRECATED_READPARAM

  PinchGestureInput();

 public:
  // clang-format off
  MOZ_DEFINE_ENUM_AT_CLASS_SCOPE(
    PinchGestureType, (
      PINCHGESTURE_START,
      PINCHGESTURE_SCALE,
      // The FINGERLIFTED state is used when a touch-based pinch gesture is
      // terminated by lifting one of the two fingers. The position of the
      // finger that's still down is populated as the focus point.
      PINCHGESTURE_FINGERLIFTED,
      // The END state is used when the pinch gesture is completely terminated.
      // In this state, the focus point should not be relied upon for having
      // meaningful data.
      PINCHGESTURE_END
  ));

  MOZ_DEFINE_ENUM_AT_CLASS_SCOPE(
    PinchGestureSource, (
      UNKNOWN, // Default initialization value. Should never actually be used.
      TOUCH, // From two-finger pinch gesture
      ONE_TOUCH, // From one-finger pinch gesture
      TRACKPAD, // From trackpad pinch gesture
      MOUSEWHEEL // Synthesized from modifier+mousewheel

      // If adding more items here, increase n_values for the
      // APZ_ZOOM_PINCHSOURCE Telemetry and glean::apz_zoom::pinchsource metrics.
  ));
  // clang-format on

  // Construct a pinch gesture from a Screen point.
  PinchGestureInput(PinchGestureType aType, PinchGestureSource aSource,
                    TimeStamp aTimeStamp, const ExternalPoint& aScreenOffset,
                    const ScreenPoint& aFocusPoint, ScreenCoord aCurrentSpan,
                    ScreenCoord aPreviousSpan, Modifiers aModifiers);

  bool TransformToLocal(const ScreenToParentLayerMatrix4x4& aTransform);

  WidgetWheelEvent ToWidgetEvent(nsIWidget* aWidget) const;

  double ComputeDeltaY(nsIWidget* aWidget) const;

  // Set mLineOrPageDeltaY based on ComputeDeltaY().
  // Return false if the caller should drop this event to ensure
  // that preventDefault() is respected. (More specifically, this will be
  // true for event types other than PINCHGESTURE_END if the computed
  // mLineOrPageDeltaY is zero. In such cases, the resulting DOMMouseScroll
  // event will not be dispatched, which is a problem if the page is relying
  // on DOMMouseScroll to prevent browser zooming).
  // Note that even if the function returns false, the delta from the event
  // is accumulated and available to be sent in a later event.
  bool SetLineOrPageDeltaY(nsIWidget* aWidget);

  static gfx::IntPoint GetIntegerDeltaForEvent(bool aIsStart, float x, float y);

  // Warning, this class is serialized and sent over IPC. Any change to its
  // fields must be reflected in its ParamTraits<>, in nsGUIEventIPC.h
  PinchGestureType mType;

  // Some indication of the input device that generated this pinch gesture.
  PinchGestureSource mSource;

  // Center point of the pinch gesture. That is, if there are two fingers on the
  // screen, it is their midpoint. In the case of more than two fingers, the
  // point is implementation-specific, but can for example be the midpoint
  // between the very first and very last touch. This is in device pixels and
  // are the coordinates on the screen of this midpoint.
  // For PINCHGESTURE_END events, this may hold the last known focus point or
  // just be empty; in any case for END events it should not be relied upon.
  // For PINCHGESTURE_FINGERLIFTED events, this holds the point of the finger
  // that is still down.
  ScreenPoint mFocusPoint;

  // The screen offset of the root widget. This can be changing along with
  // the touch interaction, so we sstore it in the event.
  ExternalPoint mScreenOffset;

  // |mFocusPoint| transformed to the local coordinates of the APZC targeted
  // by the hit. This is set and used by APZ.
  ParentLayerPoint mLocalFocusPoint;

  // The distance between the touches responsible for the pinch gesture.
  ScreenCoord mCurrentSpan;

  // The previous |mCurrentSpan| in the PinchGestureInput preceding this one.
  // This is only really relevant during a PINCHGESTURE_SCALE because when it is
  // of this type then there must have been a history of spans.
  ScreenCoord mPreviousSpan;

  // We accumulate (via GetIntegerDeltaForEvent) the deltaY that would be
  // computed by ToWidgetEvent, and then whenever we get a whole integer
  // value we put it in mLineOrPageDeltaY. Since we only ever use deltaY we
  // don't need a mLineOrPageDeltaX. This field is used to dispatch legacy mouse
  // events which are only dispatched when the corresponding field on
  // WidgetWheelEvent is non-zero.
  int32_t mLineOrPageDeltaY = 0;

  bool mHandledByAPZ = false;
};

/**
 * Encapsulation class for tap events. In general, these will be generated by
 * a gesture listener by looking at SingleTouchData/MultiTouchInput instances
 * and determining whether or not the user was trying to do a gesture.
 */
class TapGestureInput : public InputData {
 protected:
  friend mozilla::layers::APZBridgeChild;
  friend mozilla::layers::PAPZBridgeParent;
  ALLOW_DEPRECATED_READPARAM

  TapGestureInput();

 public:
  // clang-format off
  MOZ_DEFINE_ENUM_AT_CLASS_SCOPE(
    TapGestureType, (
      TAPGESTURE_LONG,
      TAPGESTURE_LONG_UP,
      TAPGESTURE_UP,
      TAPGESTURE_CONFIRMED,
      TAPGESTURE_DOUBLE,
      TAPGESTURE_SECOND, // See GeckoContentController::TapType::eSecondTap
      TAPGESTURE_CANCEL
  ));
  // clang-format on

  // Construct a tap gesture from a Screen point.
  // mLocalPoint remains (0,0) unless it's set later.
  TapGestureInput(TapGestureType aType, TimeStamp aTimeStamp,
                  const ScreenIntPoint& aPoint, Modifiers aModifiers);

  // Construct a tap gesture from a ParentLayer point.
  // mPoint remains (0,0) unless it's set later.
  TapGestureInput(TapGestureType aType, TimeStamp aTimeStamp,
                  const ParentLayerPoint& aLocalPoint, Modifiers aModifiers);

  bool TransformToLocal(const ScreenToParentLayerMatrix4x4& aTransform);

  WidgetSimpleGestureEvent ToWidgetEvent(nsIWidget* aWidget) const;

  // Warning, this class is serialized and sent over IPC. Any change to its
  // fields must be reflected in its ParamTraits<>, in nsGUIEventIPC.h
  TapGestureType mType;

  // The location of the tap in screen pixels.
  ScreenIntPoint mPoint;

  // The location of the tap in the local coordinates of the APZC receiving it.
  // This is set and used by APZ.
  ParentLayerPoint mLocalPoint;
};

// Encapsulation class for scroll-wheel events. These are generated by mice
// with physical scroll wheels, and on Windows by most touchpads when using
// scroll gestures.
class ScrollWheelInput : public InputData {
 protected:
  friend mozilla::layers::APZBridgeChild;
  friend mozilla::layers::PAPZBridgeParent;
  ALLOW_DEPRECATED_READPARAM

  typedef mozilla::layers::APZWheelAction APZWheelAction;

  ScrollWheelInput();

 public:
  // clang-format off
  MOZ_DEFINE_ENUM_AT_CLASS_SCOPE(
    ScrollDeltaType, (
      // There are three kinds of scroll delta modes in Gecko: "page", "line"
      // and "pixel".
      SCROLLDELTA_LINE,
      SCROLLDELTA_PAGE,
      SCROLLDELTA_PIXEL
  ));

  MOZ_DEFINE_ENUM_AT_CLASS_SCOPE(
    ScrollMode, (
      SCROLLMODE_INSTANT,
      SCROLLMODE_SMOOTH
    )
  );
  // clang-format on

  ScrollWheelInput(TimeStamp aTimeStamp, Modifiers aModifiers,
                   ScrollMode aScrollMode, ScrollDeltaType aDeltaType,
                   const ScreenPoint& aOrigin, double aDeltaX, double aDeltaY,
                   bool aAllowToOverrideSystemScrollSpeed,
                   WheelDeltaAdjustmentStrategy aWheelDeltaAdjustmentStrategy);
  explicit ScrollWheelInput(const WidgetWheelEvent& aEvent);

  static ScrollDeltaType DeltaTypeForDeltaMode(uint32_t aDeltaMode);
  static uint32_t DeltaModeForDeltaType(ScrollDeltaType aDeltaType);
  static mozilla::ScrollUnit ScrollUnitForDeltaType(ScrollDeltaType aDeltaType);

  WidgetWheelEvent ToWidgetEvent(nsIWidget* aWidget) const;
  bool TransformToLocal(const ScreenToParentLayerMatrix4x4& aTransform);

  bool IsCustomizedByUserPrefs() const;

  // The following two functions are for auto-dir scrolling. For detailed
  // information on auto-dir, @see mozilla::WheelDeltaAdjustmentStrategy
  bool IsAutoDir(bool aForce = false) const {
    if (aForce) {
      return true;
    }

    switch (mWheelDeltaAdjustmentStrategy) {
      case WheelDeltaAdjustmentStrategy::eAutoDir:
      case WheelDeltaAdjustmentStrategy::eAutoDirWithRootHonour:
        return true;
      default:
        // Prevent compilation errors generated by -Werror=switch
        break;
    }
    return false;
  }
  // Indicates which element this scroll honours if it's an auto-dir scroll.
  // If true, honour the root element; otherwise, honour the currently scrolling
  // target.
  // Note that if IsAutoDir() returns false, then this function also returns
  // false, but false in this case is meaningless as IsAutoDir() indicates it's
  // not an auto-dir scroll.
  // For detailed information on auto-dir,
  // @see mozilla::WheelDeltaAdjustmentStrategy
  bool HonoursRoot(bool aForce = false) const {
    return WheelDeltaAdjustmentStrategy::eAutoDirWithRootHonour ==
               mWheelDeltaAdjustmentStrategy ||
           aForce;
  }

  // Warning, this class is serialized and sent over IPC. Any change to its
  // fields must be reflected in its ParamTraits<>, in nsGUIEventIPC.h
  ScrollDeltaType mDeltaType;
  ScrollMode mScrollMode;
  ScreenPoint mOrigin;

  bool mHandledByAPZ;

  // Deltas are in units corresponding to the delta type. For line deltas, they
  // are the number of line units to scroll. The number of device pixels for a
  // horizontal and vertical line unit are in FrameMetrics::mLineScrollAmount.
  // For pixel deltas, these values are in ScreenCoords.
  //
  // The horizontal (X) delta is > 0 for scrolling right and < 0 for scrolling
  // left. The vertical (Y) delta is < 0 for scrolling up and > 0 for
  // scrolling down.
  double mDeltaX;
  double mDeltaY;

  // The number of scroll wheel ticks.
  double mWheelTicksX = 0.0;
  double mWheelTicksY = 0.0;

  // The location of the scroll in local coordinates. This is set and used by
  // APZ.
  ParentLayerPoint mLocalOrigin;

  // See lineOrPageDeltaX/Y on WidgetWheelEvent.
  int32_t mLineOrPageDeltaX;
  int32_t mLineOrPageDeltaY;

  // Indicates the order in which this event was added to a transaction. The
  // first event is 1; if not a member of a transaction, this is 0.
  uint32_t mScrollSeriesNumber;

  // User-set delta multipliers.
  double mUserDeltaMultiplierX;
  double mUserDeltaMultiplierY;

  bool mMayHaveMomentum;
  bool mIsMomentum;
  bool mAllowToOverrideSystemScrollSpeed = false;

  // Sometimes a wheel event input's wheel delta should be adjusted. This member
  // specifies how to adjust the wheel delta.
  WheelDeltaAdjustmentStrategy mWheelDeltaAdjustmentStrategy =
      WheelDeltaAdjustmentStrategy::eNone;

  APZWheelAction mAPZAction;
};

class KeyboardInput : public InputData {
 public:
  typedef mozilla::layers::KeyboardScrollAction KeyboardScrollAction;

  // Note that if you change the first member in this enum(I.e. KEY_DOWN) to one
  // other member, don't forget to update the minimum value in
  // ContiguousEnumSerializer for KeyboardEventType in widget/nsGUIEventIPC
  // accordingly.
  enum KeyboardEventType {
    KEY_DOWN,
    KEY_PRESS,
    KEY_UP,
    // Any other key event such as eAccessKeyNotFound
    KEY_OTHER,

    // Used as an upper bound for ContiguousEnumSerializer
    KEY_SENTINEL,
  };

  explicit KeyboardInput(const WidgetKeyboardEvent& aEvent);

  // Warning, this class is serialized and sent over IPC. Any change to its
  // fields must be reflected in its ParamTraits<>, in nsGUIEventIPC.h

  KeyboardEventType mType;
  uint32_t mKeyCode;
  uint32_t mCharCode;
  CopyableTArray<ShortcutKeyCandidate> mShortcutCandidates;

  bool mHandledByAPZ;

  // The scroll action to perform on a layer for this keyboard input. This is
  // only used in APZ and is NOT serialized over IPC.
  KeyboardScrollAction mAction;

 protected:
  friend mozilla::layers::APZBridgeChild;
  friend mozilla::layers::PAPZBridgeParent;
  ALLOW_DEPRECATED_READPARAM

  KeyboardInput();
};

/**
 * For widgets that support synthesizing native touch events, this function
 * can be used to manage the current state of synthetic pointers. Each widget
 * must maintain its own MultiTouchInput instance and pass it in as the state,
 * along with the desired parameters for the changes. This function returns
 * a new MultiTouchInput object that is ready to be dispatched.
 */
MultiTouchInput UpdateSynthesizedTouchState(
    MultiTouchInput* aState, TimeStamp aTimeStamp, uint32_t aPointerId,
    TouchPointerState aPointerState, LayoutDeviceIntPoint aPoint,
    double aPointerPressure, uint32_t aPointerOrientation);

}  // namespace mozilla

#endif  // InputData_h_
