/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "InputData.h"

#include <type_traits>

#include "UnitTransforms.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/SwipeTracker.h"
#include "mozilla/TextEvents.h"
#include "mozilla/TouchEvents.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/dom/Touch.h"
#include "mozilla/dom/WheelEventBinding.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsThreadUtils.h"

namespace mozilla {

using namespace dom;

template WidgetMouseEvent MouseInput::ToWidgetEvent(nsIWidget* aWidget) const;
template WidgetPointerEvent MouseInput::ToWidgetEvent(nsIWidget* aWidget) const;
template WidgetDragEvent MouseInput::ToWidgetEvent(nsIWidget* aWidget) const;

InputData::~InputData() = default;

InputData::InputData(InputType aInputType)
    : mInputType(aInputType),
      mFocusSequenceNumber(0),
      mLayersId{0},
      modifiers(0) {}

InputData::InputData(InputType aInputType, TimeStamp aTimeStamp,
                     Modifiers aModifiers)
    : mInputType(aInputType),
      mTimeStamp(aTimeStamp),
      mFocusSequenceNumber(0),
      mLayersId{0},
      modifiers(aModifiers) {}

InputData::InputData(InputType aInputType, TimeStamp aTimeStamp,
                     const Maybe<uint64_t>& aCallback, Modifiers aModifiers)
    : mInputType(aInputType),
      mTimeStamp(aTimeStamp),
      mFocusSequenceNumber(0),
      mLayersId{0},
      mCallbackId(aCallback),
      modifiers(aModifiers) {}

SingleTouchData::SingleTouchData(int32_t aIdentifier,
                                 ScreenIntPoint aScreenPoint,
                                 ScreenSize aRadius, float aRotationAngle,
                                 float aForce)
    : mIdentifier(aIdentifier),
      mScreenPoint(aScreenPoint),
      mRadius(aRadius),
      mRotationAngle(aRotationAngle),
      mForce(aForce) {}

SingleTouchData::SingleTouchData(int32_t aIdentifier,
                                 ParentLayerPoint aLocalScreenPoint,
                                 ScreenSize aRadius, float aRotationAngle,
                                 float aForce)
    : mIdentifier(aIdentifier),
      mLocalScreenPoint(aLocalScreenPoint),
      mRadius(aRadius),
      mRotationAngle(aRotationAngle),
      mForce(aForce) {}

SingleTouchData::SingleTouchData()
    : mIdentifier(0), mRotationAngle(0.0), mForce(0.0) {}

already_AddRefed<Touch> SingleTouchData::ToNewDOMTouch() const {
  MOZ_ASSERT(NS_IsMainThread(),
             "Can only create dom::Touch instances on main thread");
  auto touch = MakeRefPtr<Touch>(
      mIdentifier,
      LayoutDeviceIntPoint::Truncate(mScreenPoint.x, mScreenPoint.y),
      LayoutDeviceIntPoint::Truncate(mRadius.width, mRadius.height),
      mRotationAngle, mForce);
  if (mAngle) {
    touch->mAngle = mAngle;
  } else {
    touch->mTilt.emplace(mTiltX, mTiltY);
  }
  touch->twist = mTwist;
  return touch.forget();
}

MultiTouchInput::MultiTouchInput(MultiTouchType aType, uint32_t aTime,
                                 TimeStamp aTimeStamp, Modifiers aModifiers)
    : InputData(MULTITOUCH_INPUT, aTimeStamp, aModifiers),
      mType(aType),
      mHandledByAPZ(false) {}

MultiTouchInput::MultiTouchInput()
    : InputData(MULTITOUCH_INPUT),
      mType(MULTITOUCH_START),
      mHandledByAPZ(false) {}

MultiTouchInput::MultiTouchInput(const WidgetTouchEvent& aTouchEvent)
    : InputData(MULTITOUCH_INPUT, aTouchEvent.mTimeStamp,
                aTouchEvent.mCallbackId, aTouchEvent.mModifiers),
      mHandledByAPZ(aTouchEvent.mFlags.mHandledByAPZ),
      mButton(aTouchEvent.mButton),
      mButtons(aTouchEvent.mButtons),
      mInputSource(aTouchEvent.mInputSource) {
  MOZ_ASSERT(NS_IsMainThread(),
             "Can only copy from WidgetTouchEvent on main thread");

  switch (aTouchEvent.mMessage) {
    case eTouchStart:
      mType = MULTITOUCH_START;
      break;
    case eTouchMove:
      mType = MULTITOUCH_MOVE;
      break;
    case eTouchEnd:
      mType = MULTITOUCH_END;
      break;
    case eTouchCancel:
      mType = MULTITOUCH_CANCEL;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Did not assign a type to a MultiTouchInput");
      break;
  }

  mScreenOffset = ViewAs<ExternalPixel>(
      aTouchEvent.mWidget->WidgetToScreenOffset(),
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent);

  for (size_t i = 0; i < aTouchEvent.mTouches.Length(); i++) {
    const Touch* domTouch = aTouchEvent.mTouches[i];

    // Extract data from weird interfaces.
    int32_t identifier = domTouch->Identifier();
    int32_t radiusX = domTouch->RadiusX(CallerType::System);
    int32_t radiusY = domTouch->RadiusY(CallerType::System);
    float rotationAngle = domTouch->RotationAngle(CallerType::System);
    float force = domTouch->Force(CallerType::System);

    SingleTouchData data(
        identifier,
        ViewAs<ScreenPixel>(
            domTouch->mRefPoint,
            PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent),
        ScreenSize((float)radiusX, (float)radiusY), rotationAngle, force);

    if (domTouch->mTilt) {
      data.mTiltX = domTouch->mTilt->mX;
      data.mTiltY = domTouch->mTilt->mY;
    }
    data.mTwist = domTouch->twist;
    data.mAngle = domTouch->mAngle;

    mTouches.AppendElement(data);
  }
}

void MultiTouchInput::Translate(const ScreenPoint& aTranslation) {
  ScreenIntPoint translation = RoundedToInt(aTranslation);

  for (auto& touchData : mTouches) {
    for (auto& historicalData : touchData.mHistoricalData) {
      historicalData.mScreenPoint.MoveBy(translation.x, translation.y);
    }
    touchData.mScreenPoint.MoveBy(translation.x, translation.y);
  }
}

WidgetTouchEvent MultiTouchInput::ToWidgetEvent(nsIWidget* aWidget) const {
  MOZ_ASSERT(NS_IsMainThread(),
             "Can only convert To WidgetTouchEvent on main thread");
  MOZ_ASSERT(mInputSource ==
                 mozilla::dom::MouseEvent_Binding::MOZ_SOURCE_TOUCH ||
             mInputSource == mozilla::dom::MouseEvent_Binding::MOZ_SOURCE_PEN);

  EventMessage touchEventMessage = eVoidEvent;
  switch (mType) {
    case MULTITOUCH_START:
      touchEventMessage = eTouchStart;
      break;
    case MULTITOUCH_MOVE:
      touchEventMessage = eTouchMove;
      break;
    case MULTITOUCH_END:
      touchEventMessage = eTouchEnd;
      break;
    case MULTITOUCH_CANCEL:
      touchEventMessage = eTouchCancel;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE(
          "Did not assign a type to WidgetTouchEvent in MultiTouchInput");
      break;
  }

  WidgetTouchEvent event(true, touchEventMessage, aWidget);
  if (touchEventMessage == eVoidEvent) {
    return event;
  }

  event.mModifiers = this->modifiers;
  event.mTimeStamp = this->mTimeStamp;
  event.mFlags.mHandledByAPZ = mHandledByAPZ;
  event.mFocusSequenceNumber = mFocusSequenceNumber;
  event.mLayersId = mLayersId;
  event.mInputSource = mInputSource;
  event.mButton = mButton;
  event.mButtons = mButtons;

  for (size_t i = 0; i < mTouches.Length(); i++) {
    *event.mTouches.AppendElement() = mTouches[i].ToNewDOMTouch();
  }

  return event;
}

int32_t MultiTouchInput::IndexOfTouch(int32_t aTouchIdentifier) {
  for (size_t i = 0; i < mTouches.Length(); i++) {
    if (mTouches[i].mIdentifier == aTouchIdentifier) {
      return (int32_t)i;
    }
  }
  return -1;
}

bool MultiTouchInput::TransformToLocal(
    const ScreenToParentLayerMatrix4x4& aTransform) {
  for (auto& touchData : mTouches) {
    for (auto& historicalData : touchData.mHistoricalData) {
      Maybe<ParentLayerIntPoint> historicalPoint =
          UntransformBy(aTransform, historicalData.mScreenPoint);
      if (!historicalPoint) {
        return false;
      }
      historicalData.mLocalScreenPoint = *historicalPoint;
    }
    Maybe<ParentLayerIntPoint> point =
        UntransformBy(aTransform, touchData.mScreenPoint);
    if (!point) {
      return false;
    }
    touchData.mLocalScreenPoint = *point;
  }
  return true;
}

MouseInput::MouseInput()
    : InputData(MOUSE_INPUT),
      mType(MOUSE_NONE),
      mButtonType(NONE),
      mInputSource(0),
      mButtons(0),
      mHandledByAPZ(false),
      mPreventClickEvent(false),
      mIgnoreCapturingContent(false),
      mSynthesizeMoveAfterDispatch(false) {}

MouseInput::MouseInput(MouseType aType, ButtonType aButtonType,
                       uint16_t aInputSource, int16_t aButtons,
                       const ScreenPoint& aPoint, TimeStamp aTimeStamp,
                       Modifiers aModifiers)
    : InputData(MOUSE_INPUT, aTimeStamp, aModifiers),
      mType(aType),
      mButtonType(aButtonType),
      mInputSource(aInputSource),
      mButtons(aButtons),
      mOrigin(aPoint),
      mHandledByAPZ(false),
      mPreventClickEvent(false),
      mIgnoreCapturingContent(false),
      mSynthesizeMoveAfterDispatch(false) {}

MouseInput::MouseInput(const WidgetMouseEvent& aMouseEvent)
    : InputData(MOUSE_INPUT, aMouseEvent.mTimeStamp, aMouseEvent.mCallbackId,
                aMouseEvent.mModifiers),
      mType(MOUSE_NONE),
      mButtonType(NONE),
      mClickCount(aMouseEvent.mClickCount),
      mInputSource(aMouseEvent.mInputSource),
      mButtons(aMouseEvent.mButtons),
      mHandledByAPZ(aMouseEvent.mFlags.mHandledByAPZ),
      mPreventClickEvent(aMouseEvent.mClass == eMouseEventClass &&
                         static_cast<const WidgetMouseEvent&>(aMouseEvent)
                             .mClickEventPrevented),
      mIgnoreCapturingContent((aMouseEvent.mClass == eMouseEventClass ||
                               aMouseEvent.mClass == ePointerEventClass) &&
                              static_cast<const WidgetMouseEvent&>(aMouseEvent)
                                  .mIgnoreCapturingContent),
      mSynthesizeMoveAfterDispatch(
          (aMouseEvent.mClass == eMouseEventClass ||
           aMouseEvent.mClass == ePointerEventClass) &&
          static_cast<const WidgetMouseEvent&>(aMouseEvent)
              .mSynthesizeMoveAfterDispatch) {
  MOZ_ASSERT(NS_IsMainThread(),
             "Can only copy from WidgetTouchEvent on main thread");

  switch (aMouseEvent.mButton) {
    case MouseButton::ePrimary:
      mButtonType = MouseInput::PRIMARY_BUTTON;
      break;
    case MouseButton::eMiddle:
      mButtonType = MouseInput::MIDDLE_BUTTON;
      break;
    case MouseButton::eSecondary:
      mButtonType = MouseInput::SECONDARY_BUTTON;
      break;
    case MouseButton::eX1:
      mButtonType = MouseInput::X1_BUTTON;
      break;
    case MouseButton::eX2:
      mButtonType = MouseInput::X2_BUTTON;
      break;
    default:
      mButtonType = MouseInput::NONE;
      break;
  }

  switch (aMouseEvent.mMessage) {
    case eMouseMove:
      mType = MOUSE_MOVE;
      break;
    case eMouseUp:
      mType = MOUSE_UP;
      break;
    case eMouseDown:
      mType = MOUSE_DOWN;
      break;
    case eDragStart:
      mType = MOUSE_DRAG_START;
      break;
    case eDragEnd:
      mType = MOUSE_DRAG_END;
      break;
    case eDragEnter:
      mType = MOUSE_DRAG_ENTER;
      break;
    case eDragOver:
      mType = MOUSE_DRAG_OVER;
      break;
    case eDragExit:
      mType = MOUSE_DRAG_EXIT;
      break;
    case eDrop:
      mType = MOUSE_DROP;
      break;
    case eMouseEnterIntoWidget:
      mType = MOUSE_WIDGET_ENTER;
      break;
    case eMouseExitFromWidget:
      mType = MOUSE_WIDGET_EXIT;
      break;
    case eMouseHitTest:
      mType = MOUSE_HITTEST;
      break;
    case eContextMenu:
      mType = MOUSE_CONTEXTMENU;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Mouse event type not supported");
      break;
  }

  mOrigin = ScreenPoint(ViewAs<ScreenPixel>(
      aMouseEvent.mRefPoint,
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent));
}

bool MouseInput::IsLeftButton() const { return mButtonType == PRIMARY_BUTTON; }

bool MouseInput::TransformToLocal(
    const ScreenToParentLayerMatrix4x4& aTransform) {
  Maybe<ParentLayerPoint> point = UntransformBy(aTransform, mOrigin);
  if (!point) {
    return false;
  }
  mLocalOrigin = *point;

  return true;
}

bool MouseInput::IsPointerEventType() const {
  return mType == MOUSE_CONTEXTMENU;
}

template <class WidgetMouseOrPointerEvent>
WidgetMouseOrPointerEvent MouseInput::ToWidgetEvent(nsIWidget* aWidget) const {
  MOZ_ASSERT(NS_IsMainThread(),
             "Can only convert To WidgetTouchEvent on main thread");

  const DebugOnly<bool> isPointerEvent =
      std::is_same_v<WidgetMouseOrPointerEvent, WidgetPointerEvent>;
  const DebugOnly<bool> isMouseEvent =
      std::is_same_v<WidgetMouseOrPointerEvent, WidgetMouseEvent>;
  const DebugOnly<bool> isDragEvent =
      std::is_same_v<WidgetMouseOrPointerEvent, WidgetDragEvent>;
  MOZ_ASSERT(!IsPointerEventType() || isPointerEvent,
             "Please use ToWidgetEvent<WidgetPointerEvent>() for the instance");
  MOZ_ASSERT(IsPointerEventType() || isMouseEvent || isDragEvent,
             "Please use ToWidgetEvent<WidgetMouseEvent>() or "
             "ToWidgetEvent<WidgetDragEvent>() for the instance");

  EventMessage msg = eVoidEvent;
  Maybe<WidgetMouseEvent::ExitFrom> exitFrom;
  switch (mType) {
    case MOUSE_MOVE:
      msg = eMouseMove;
      break;
    case MOUSE_UP:
      msg = eMouseUp;
      break;
    case MOUSE_DOWN:
      msg = eMouseDown;
      break;
    case MOUSE_DRAG_START:
      msg = eDragStart;
      break;
    case MOUSE_DRAG_END:
      msg = eDragEnd;
      break;
    case MOUSE_DRAG_ENTER:
      msg = eDragEnter;
      break;
    case MOUSE_DRAG_OVER:
      msg = eDragOver;
      break;
    case MOUSE_DRAG_EXIT:
      msg = eDragExit;
      break;
    case MOUSE_DROP:
      msg = eDrop;
      break;
    case MOUSE_WIDGET_ENTER:
      msg = eMouseEnterIntoWidget;
      break;
    case MOUSE_WIDGET_EXIT:
      msg = eMouseExitFromWidget;
      exitFrom = Some(WidgetMouseEvent::ePlatformChild);
      break;
    case MOUSE_HITTEST:
      msg = eMouseHitTest;
      break;
    case MOUSE_CONTEXTMENU:
      msg = eContextMenu;
      MOZ_ASSERT(mButtonType == MouseInput::SECONDARY_BUTTON);
      break;
    default:
      MOZ_ASSERT_UNREACHABLE(
          "Did not assign a type to WidgetMouseEvent in MouseInput");
      break;
  }

  WidgetMouseOrPointerEvent event(true, msg, aWidget);

  if (msg == eVoidEvent) {
    return event;
  }

  switch (mButtonType) {
    case MouseInput::PRIMARY_BUTTON:
      event.mButton = MouseButton::ePrimary;
      break;
    case MouseInput::MIDDLE_BUTTON:
      event.mButton = MouseButton::eMiddle;
      break;
    case MouseInput::SECONDARY_BUTTON:
      event.mButton = MouseButton::eSecondary;
      break;
    case MouseInput::X1_BUTTON:
      event.mButton = MouseButton::eX1;
      break;
    case MouseInput::X2_BUTTON:
      event.mButton = MouseButton::eX2;
      break;
    case MouseInput::NONE:
    default:
      break;
  }

  event.mButtons = mButtons;
  event.mModifiers = modifiers;
  event.mTimeStamp = mTimeStamp;
  event.mLayersId = mLayersId;
  event.mFlags.mHandledByAPZ = mHandledByAPZ;
  event.mRefPoint = RoundedToInt(ViewAs<LayoutDevicePixel>(
      mOrigin,
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent));
  event.mClickCount = mClickCount;
  event.mInputSource = mInputSource;
  event.mFocusSequenceNumber = mFocusSequenceNumber;
  event.mExitFrom = std::move(exitFrom);
  event.mClickEventPrevented = mPreventClickEvent;
  event.mIgnoreCapturingContent = mIgnoreCapturingContent;
  event.mSynthesizeMoveAfterDispatch = mSynthesizeMoveAfterDispatch;
  event.mCallbackId = mCallbackId;

  return event;
}

PanGestureInput::PanGestureInput()
    : InputData(PANGESTURE_INPUT),
      mType(PANGESTURE_MAYSTART),
      mLineOrPageDeltaX(0),
      mLineOrPageDeltaY(0),
      mUserDeltaMultiplierX(1.0),
      mUserDeltaMultiplierY(1.0),
      mHandledByAPZ(false),
      mOverscrollBehaviorAllowsSwipe(false),
      mSimulateMomentum(false),
      mIsNoLineOrPageDelta(true),
      mMayTriggerSwipe(false) {}

PanGestureInput::PanGestureInput(PanGestureType aType, TimeStamp aTimeStamp,
                                 const ScreenPoint& aPanStartPoint,
                                 const ScreenPoint& aPanDisplacement,
                                 Modifiers aModifiers)
    : InputData(PANGESTURE_INPUT, aTimeStamp, aModifiers),
      mType(aType),
      mPanStartPoint(aPanStartPoint),
      mPanDisplacement(aPanDisplacement),
      mLineOrPageDeltaX(0),
      mLineOrPageDeltaY(0),
      mUserDeltaMultiplierX(1.0),
      mUserDeltaMultiplierY(1.0),
      mHandledByAPZ(false),
      mOverscrollBehaviorAllowsSwipe(false),
      mSimulateMomentum(false),
      mIsNoLineOrPageDelta(true) {
  mMayTriggerSwipe =
      SwipeTracker::CanTriggerSwipe(*this, SwipeInputSource::Touchpad);
}

PanGestureInput::PanGestureInput(PanGestureType aType, TimeStamp aTimeStamp,
                                 const ScreenPoint& aPanStartPoint,
                                 const ScreenPoint& aPanDisplacement,
                                 Modifiers aModifiers,
                                 IsEligibleForSwipe aIsEligibleForSwipe)
    : PanGestureInput(aType, aTimeStamp, aPanStartPoint, aPanDisplacement,
                      aModifiers) {
  mMayTriggerSwipe &= bool(aIsEligibleForSwipe);
}

void PanGestureInput::SetLineOrPageDeltas(int32_t aLineOrPageDeltaX,
                                          int32_t aLineOrPageDeltaY) {
  mLineOrPageDeltaX = aLineOrPageDeltaX;
  mLineOrPageDeltaY = aLineOrPageDeltaY;
  mIsNoLineOrPageDelta = false;
}

bool PanGestureInput::IsMomentum() const {
  switch (mType) {
    case PanGestureInput::PANGESTURE_MOMENTUMSTART:
    case PanGestureInput::PANGESTURE_MOMENTUMPAN:
    case PanGestureInput::PANGESTURE_MOMENTUMEND:
      return true;
    default:
      return false;
  }
}

WidgetWheelEvent PanGestureInput::ToWidgetEvent(nsIWidget* aWidget) const {
  WidgetWheelEvent wheelEvent(true, eWheel, aWidget);
  wheelEvent.mModifiers = this->modifiers;
  wheelEvent.mTimeStamp = mTimeStamp;
  wheelEvent.mLayersId = mLayersId;
  wheelEvent.mRefPoint = RoundedToInt(ViewAs<LayoutDevicePixel>(
      mPanStartPoint,
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent));
  wheelEvent.mButtons = 0;
  wheelEvent.mMayHaveMomentum = true;  // pan inputs may have momentum
  wheelEvent.mIsMomentum = IsMomentum();
  wheelEvent.mLineOrPageDeltaX = mLineOrPageDeltaX;
  wheelEvent.mLineOrPageDeltaY = mLineOrPageDeltaY;
  wheelEvent.mDeltaX = mPanDisplacement.x;
  wheelEvent.mDeltaY = mPanDisplacement.y;
  wheelEvent.mFlags.mHandledByAPZ = mHandledByAPZ;
  wheelEvent.mFocusSequenceNumber = mFocusSequenceNumber;
  wheelEvent.mIsNoLineOrPageDelta = mIsNoLineOrPageDelta;
  if (mDeltaType == PanGestureInput::PANDELTA_PAGE) {
    // widget/gtk is currently the only consumer that uses delta type
    // PANDELTA_PAGE
    // Emulate legacy widget/gtk behavior
    wheelEvent.mDeltaMode = WheelEvent_Binding::DOM_DELTA_LINE;
    wheelEvent.mScrollType = WidgetWheelEvent::SCROLL_ASYNCHRONOUSLY;
    wheelEvent.mDeltaX *= 3;
    wheelEvent.mDeltaY *= 3;
  } else {
    wheelEvent.mDeltaMode = WheelEvent_Binding::DOM_DELTA_PIXEL;
  }
  return wheelEvent;
}

bool PanGestureInput::TransformToLocal(
    const ScreenToParentLayerMatrix4x4& aTransform) {
  Maybe<ParentLayerPoint> panStartPoint =
      UntransformBy(aTransform, mPanStartPoint);
  if (!panStartPoint) {
    return false;
  }
  mLocalPanStartPoint = *panStartPoint;

  if (mDeltaType == PanGestureInput::PANDELTA_PAGE) {
    // Skip transforming the pan displacement because we want
    // raw page proportion counts.
    mLocalPanDisplacement = ViewAs<ParentLayerPixel>(
        mPanDisplacement, PixelCastJustification::DeltaIsPageProportion);
    return true;
  }

  Maybe<ParentLayerPoint> panDisplacement =
      UntransformVector(aTransform, mPanDisplacement, mPanStartPoint);
  if (!panDisplacement) {
    return false;
  }
  mLocalPanDisplacement = *panDisplacement;
  return true;
}

ScreenPoint PanGestureInput::UserMultipliedPanDisplacement() const {
  return ScreenPoint(mPanDisplacement.x * mUserDeltaMultiplierX,
                     mPanDisplacement.y * mUserDeltaMultiplierY);
}

ParentLayerPoint PanGestureInput::UserMultipliedLocalPanDisplacement() const {
  return ParentLayerPoint(mLocalPanDisplacement.x * mUserDeltaMultiplierX,
                          mLocalPanDisplacement.y * mUserDeltaMultiplierY);
}

static int32_t TakeLargestInt(gfx::Coord* aCoord) {
  int32_t result(aCoord->value);  // truncate towards zero
  aCoord->value -= result;
  return result;
}

/* static */ gfx::IntPoint PanGestureInput::GetIntegerDeltaForEvent(
    bool aIsStart, float x, float y) {
  static gfx::Point sAccumulator(0.0f, 0.0f);
  if (aIsStart) {
    sAccumulator = gfx::Point(0.0f, 0.0f);
  }
  sAccumulator.x += x;
  sAccumulator.y += y;
  return gfx::IntPoint(TakeLargestInt(&sAccumulator.x),
                       TakeLargestInt(&sAccumulator.y));
}

PinchGestureInput::PinchGestureInput()
    : InputData(PINCHGESTURE_INPUT),
      mType(PINCHGESTURE_START),
      mSource(UNKNOWN),
      mHandledByAPZ(false) {}

PinchGestureInput::PinchGestureInput(
    PinchGestureType aType, PinchGestureSource aSource, TimeStamp aTimeStamp,
    const ExternalPoint& aScreenOffset, const ScreenPoint& aFocusPoint,
    ScreenCoord aCurrentSpan, ScreenCoord aPreviousSpan, Modifiers aModifiers)
    : InputData(PINCHGESTURE_INPUT, aTimeStamp, aModifiers),
      mType(aType),
      mSource(aSource),
      mFocusPoint(aFocusPoint),
      mScreenOffset(aScreenOffset),
      mCurrentSpan(aCurrentSpan),
      mPreviousSpan(aPreviousSpan),
      mLineOrPageDeltaY(0),
      mHandledByAPZ(false) {}

bool PinchGestureInput::TransformToLocal(
    const ScreenToParentLayerMatrix4x4& aTransform) {
  Maybe<ParentLayerPoint> point = UntransformBy(aTransform, mFocusPoint);
  if (!point) {
    return false;
  }
  mLocalFocusPoint = *point;
  return true;
}

WidgetWheelEvent PinchGestureInput::ToWidgetEvent(nsIWidget* aWidget) const {
  WidgetWheelEvent wheelEvent(true, eWheel, aWidget);
  wheelEvent.mModifiers = this->modifiers | MODIFIER_CONTROL;
  wheelEvent.mTimeStamp = mTimeStamp;
  wheelEvent.mLayersId = mLayersId;
  wheelEvent.mRefPoint = RoundedToInt(ViewAs<LayoutDevicePixel>(
      mFocusPoint,
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent));
  wheelEvent.mButtons = 0;
  wheelEvent.mFlags.mHandledByAPZ = mHandledByAPZ;
  wheelEvent.mDeltaMode = WheelEvent_Binding::DOM_DELTA_PIXEL;

  wheelEvent.mDeltaY = ComputeDeltaY(aWidget);

  wheelEvent.mLineOrPageDeltaY = mLineOrPageDeltaY;

  MOZ_ASSERT(mType == PINCHGESTURE_END || wheelEvent.mDeltaY != 0.0);

  return wheelEvent;
}

double PinchGestureInput::ComputeDeltaY(nsIWidget* aWidget) const {
#if defined(XP_DARWIN)
  // This converts the pinch gesture value to a fake wheel event that has the
  // control key pressed so that pages can implement custom pinch gesture
  // handling. It may seem strange that this doesn't use a wheel event with
  // the deltaZ property set, but this matches Chrome's behavior as described
  // at https://code.google.com/p/chromium/issues/detail?id=289887
  //
  // The intent of the formula below is to produce numbers similar to Chrome's
  // implementation of this feature. Chrome implements deltaY using the formula
  // "-100 * log(1 + [event magnification])" which is unfortunately incorrect.
  // All deltas for a single pinch gesture should sum to 0 if the start and end
  // of a pinch gesture end up in the same place. This doesn't happen in Chrome
  // because they followed Apple's misleading documentation, which implies that
  // "1 + [event magnification]" is the scale factor. The scale factor is
  // instead "pow(ratio, [event magnification])" so "[event magnification]" is
  // already in log space.
  //
  // The multiplication by the backing scale factor below counteracts the
  // division by the backing scale factor in WheelEvent.

  // We want to set deltaY to |-100.0 * M * GetDefaultScaleInternal()| where M
  // is [event magnification] but [event magnification] is only available in the
  // macOS widget code so we have to reverse engineer from mCurrentSpan and
  // mPreviousSpan (which are derived from [event magnification]) to get it.
  // Specifically, we know |mCurrentSpan == 100.0| and |mPreviousSpan == 100.0 *
  // (1.0 - M)|. We can calculate deltaY by solving the mPreviousSpan equation
  // for M in terms of mPreviousSpan and plugging that into to the formula for
  // deltaY.
  return (mPreviousSpan - 100.0) *
         (aWidget ? aWidget->GetDefaultScaleInternal() : 1.f);
#else
  // This calculation is based on what the Windows and Linux widget code does.
  // Specifically, it creates a PinchGestureInput with |mCurrentSpan == 100.0 *
  // currentScale| and |mPreviousSpan == 100.0 * lastScale| where currentScale
  // is the scale from the current OS event and lastScale is the scale when the
  // previous OS event happened. On macOS [event magnification] is a relative
  // change in scale factor, ie if the scale factor changed from 1 to 1.1 it
  // will be 0.1, similarly if it changed from 1 to 0.9 it will be -0.1. To
  // calculate the relative scale change on Windows we would calculate |M =
  // currentScale - lastScale = (mCurrentSpan-mPreviousSpan)/100| and use the
  // same formula as the macOS code
  // (|-100.0 * M * GetDefaultScaleInternal()|).

  return (mPreviousSpan - mCurrentSpan) *
         (aWidget ? aWidget->GetDefaultScaleInternal() : 1.f);
#endif
}

bool PinchGestureInput::SetLineOrPageDeltaY(nsIWidget* aWidget) {
  double deltaY = ComputeDeltaY(aWidget);
  if (deltaY == 0 && mType != PINCHGESTURE_END) {
    return false;
  }
  gfx::IntPoint lineOrPageDelta = PinchGestureInput::GetIntegerDeltaForEvent(
      (mType == PINCHGESTURE_START), 0, deltaY);
  mLineOrPageDeltaY = lineOrPageDelta.y;
  if (mLineOrPageDeltaY == 0) {
    // For PINCHGESTURE_SCALE events, don't dispatch them. Note that the delta
    // isn't lost; it remains in the accumulator in GetIntegerDeltaForEvent().
    if (mType == PINCHGESTURE_SCALE) {
      return false;
    }
    // On Windows, drop PINCHGESTURE_START as well (the Windows widget code will
    // defer the START event until we accumulate enough delta).
    // The Linux widget code doesn't support this, so instead set the event's
    // mLineOrPageDeltaY to the smallest nonzero amount in the relevant
    // direction.
    if (mType == PINCHGESTURE_START) {
#ifdef XP_WIN
      return false;
#else
      mLineOrPageDeltaY = (deltaY >= 0) ? 1 : -1;
#endif
    }
    // For PINCHGESTURE_END events, not dispatching a DOMMouseScroll for them is
    // fine.
  }
  return true;
}

/* static */ gfx::IntPoint PinchGestureInput::GetIntegerDeltaForEvent(
    bool aIsStart, float x, float y) {
  static gfx::Point sAccumulator(0.0f, 0.0f);
  if (aIsStart) {
    sAccumulator = gfx::Point(0.0f, 0.0f);
  }
  sAccumulator.x += x;
  sAccumulator.y += y;
  return gfx::IntPoint(TakeLargestInt(&sAccumulator.x),
                       TakeLargestInt(&sAccumulator.y));
}

TapGestureInput::TapGestureInput()
    : InputData(TAPGESTURE_INPUT), mType(TAPGESTURE_LONG) {}

TapGestureInput::TapGestureInput(TapGestureType aType, TimeStamp aTimeStamp,
                                 const ScreenIntPoint& aPoint,
                                 Modifiers aModifiers)
    : InputData(TAPGESTURE_INPUT, aTimeStamp, aModifiers),
      mType(aType),
      mPoint(aPoint) {}

TapGestureInput::TapGestureInput(TapGestureType aType, TimeStamp aTimeStamp,
                                 const ParentLayerPoint& aLocalPoint,
                                 Modifiers aModifiers)
    : InputData(TAPGESTURE_INPUT, aTimeStamp, aModifiers),
      mType(aType),
      mLocalPoint(aLocalPoint) {}

bool TapGestureInput::TransformToLocal(
    const ScreenToParentLayerMatrix4x4& aTransform) {
  Maybe<ParentLayerIntPoint> point = UntransformBy(aTransform, mPoint);
  if (!point) {
    return false;
  }
  mLocalPoint = *point;
  return true;
}

WidgetSimpleGestureEvent TapGestureInput::ToWidgetEvent(
    nsIWidget* aWidget) const {
  WidgetSimpleGestureEvent event(true, eTapGesture, aWidget);

  event.mTimeStamp = mTimeStamp;
  event.mLayersId = mLayersId;
  event.mRefPoint = ViewAs<LayoutDevicePixel>(
      mPoint,
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent);
  event.mButtons = 0;
  event.mClickCount = 1;
  event.mModifiers = modifiers;

  return event;
}

ScrollWheelInput::ScrollWheelInput()
    : InputData(SCROLLWHEEL_INPUT),
      mDeltaType(SCROLLDELTA_LINE),
      mScrollMode(SCROLLMODE_INSTANT),
      mHandledByAPZ(false),
      mDeltaX(0.0),
      mDeltaY(0.0),
      mLineOrPageDeltaX(0),
      mLineOrPageDeltaY(0),
      mScrollSeriesNumber(0),
      mUserDeltaMultiplierX(1.0),
      mUserDeltaMultiplierY(1.0),
      mMayHaveMomentum(false),
      mIsMomentum(false),
      mAPZAction(APZWheelAction::Scroll) {}

ScrollWheelInput::ScrollWheelInput(
    TimeStamp aTimeStamp, Modifiers aModifiers, ScrollMode aScrollMode,
    ScrollDeltaType aDeltaType, const ScreenPoint& aOrigin, double aDeltaX,
    double aDeltaY, bool aAllowToOverrideSystemScrollSpeed,
    WheelDeltaAdjustmentStrategy aWheelDeltaAdjustmentStrategy)
    : InputData(SCROLLWHEEL_INPUT, aTimeStamp, aModifiers),
      mDeltaType(aDeltaType),
      mScrollMode(aScrollMode),
      mOrigin(aOrigin),
      mHandledByAPZ(false),
      mDeltaX(aDeltaX),
      mDeltaY(aDeltaY),
      mLineOrPageDeltaX(0),
      mLineOrPageDeltaY(0),
      mScrollSeriesNumber(0),
      mUserDeltaMultiplierX(1.0),
      mUserDeltaMultiplierY(1.0),
      mMayHaveMomentum(false),
      mIsMomentum(false),
      mAllowToOverrideSystemScrollSpeed(aAllowToOverrideSystemScrollSpeed),
      mWheelDeltaAdjustmentStrategy(aWheelDeltaAdjustmentStrategy),
      mAPZAction(APZWheelAction::Scroll) {}

ScrollWheelInput::ScrollWheelInput(const WidgetWheelEvent& aWheelEvent)
    : InputData(SCROLLWHEEL_INPUT, aWheelEvent.mTimeStamp,
                aWheelEvent.mCallbackId, aWheelEvent.mModifiers),
      mDeltaType(DeltaTypeForDeltaMode(aWheelEvent.mDeltaMode)),
      mScrollMode(SCROLLMODE_INSTANT),
      mHandledByAPZ(aWheelEvent.mFlags.mHandledByAPZ),
      mDeltaX(aWheelEvent.mDeltaX),
      mDeltaY(aWheelEvent.mDeltaY),
      mWheelTicksX(aWheelEvent.mWheelTicksX),
      mWheelTicksY(aWheelEvent.mWheelTicksX),
      mLineOrPageDeltaX(aWheelEvent.mLineOrPageDeltaX),
      mLineOrPageDeltaY(aWheelEvent.mLineOrPageDeltaY),
      mScrollSeriesNumber(0),
      mUserDeltaMultiplierX(1.0),
      mUserDeltaMultiplierY(1.0),
      mMayHaveMomentum(aWheelEvent.mMayHaveMomentum),
      mIsMomentum(aWheelEvent.mIsMomentum),
      mAllowToOverrideSystemScrollSpeed(
          aWheelEvent.mAllowToOverrideSystemScrollSpeed),
      mWheelDeltaAdjustmentStrategy(WheelDeltaAdjustmentStrategy::eNone),
      mAPZAction(APZWheelAction::Scroll) {
  mOrigin = ScreenPoint(ViewAs<ScreenPixel>(
      aWheelEvent.mRefPoint,
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent));
}

ScrollWheelInput::ScrollDeltaType ScrollWheelInput::DeltaTypeForDeltaMode(
    uint32_t aDeltaMode) {
  switch (aDeltaMode) {
    case WheelEvent_Binding::DOM_DELTA_LINE:
      return SCROLLDELTA_LINE;
    case WheelEvent_Binding::DOM_DELTA_PAGE:
      return SCROLLDELTA_PAGE;
    case WheelEvent_Binding::DOM_DELTA_PIXEL:
      return SCROLLDELTA_PIXEL;
    default:
      MOZ_CRASH();
  }
  return SCROLLDELTA_LINE;
}

uint32_t ScrollWheelInput::DeltaModeForDeltaType(ScrollDeltaType aDeltaType) {
  switch (aDeltaType) {
    case ScrollWheelInput::SCROLLDELTA_LINE:
      return WheelEvent_Binding::DOM_DELTA_LINE;
    case ScrollWheelInput::SCROLLDELTA_PAGE:
      return WheelEvent_Binding::DOM_DELTA_PAGE;
    case ScrollWheelInput::SCROLLDELTA_PIXEL:
    default:
      return WheelEvent_Binding::DOM_DELTA_PIXEL;
  }
}

ScrollUnit ScrollWheelInput::ScrollUnitForDeltaType(
    ScrollDeltaType aDeltaType) {
  switch (aDeltaType) {
    case SCROLLDELTA_LINE:
      return ScrollUnit::LINES;
    case SCROLLDELTA_PAGE:
      return ScrollUnit::PAGES;
    case SCROLLDELTA_PIXEL:
      return ScrollUnit::DEVICE_PIXELS;
    default:
      MOZ_CRASH();
  }
  return ScrollUnit::LINES;
}

WidgetWheelEvent ScrollWheelInput::ToWidgetEvent(nsIWidget* aWidget) const {
  WidgetWheelEvent wheelEvent(true, eWheel, aWidget);
  wheelEvent.mModifiers = this->modifiers;
  wheelEvent.mTimeStamp = mTimeStamp;
  wheelEvent.mLayersId = mLayersId;
  wheelEvent.mRefPoint = RoundedToInt(ViewAs<LayoutDevicePixel>(
      mOrigin,
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent));
  wheelEvent.mButtons = 0;
  wheelEvent.mDeltaMode = DeltaModeForDeltaType(mDeltaType);
  wheelEvent.mMayHaveMomentum = mMayHaveMomentum;
  wheelEvent.mIsMomentum = mIsMomentum;
  wheelEvent.mDeltaX = mDeltaX;
  wheelEvent.mDeltaY = mDeltaY;
  wheelEvent.mWheelTicksX = mWheelTicksX;
  wheelEvent.mWheelTicksY = mWheelTicksY;
  wheelEvent.mLineOrPageDeltaX = mLineOrPageDeltaX;
  wheelEvent.mLineOrPageDeltaY = mLineOrPageDeltaY;
  wheelEvent.mAllowToOverrideSystemScrollSpeed =
      mAllowToOverrideSystemScrollSpeed;
  wheelEvent.mFlags.mHandledByAPZ = mHandledByAPZ;
  wheelEvent.mFocusSequenceNumber = mFocusSequenceNumber;
  wheelEvent.mCallbackId = mCallbackId;
  return wheelEvent;
}

bool ScrollWheelInput::TransformToLocal(
    const ScreenToParentLayerMatrix4x4& aTransform) {
  Maybe<ParentLayerPoint> point = UntransformBy(aTransform, mOrigin);
  if (!point) {
    return false;
  }
  mLocalOrigin = *point;
  return true;
}

bool ScrollWheelInput::IsCustomizedByUserPrefs() const {
  return mUserDeltaMultiplierX != 1.0 || mUserDeltaMultiplierY != 1.0;
}

KeyboardInput::KeyboardInput(const WidgetKeyboardEvent& aEvent)
    : InputData(KEYBOARD_INPUT, aEvent.mTimeStamp, aEvent.mModifiers),
      mKeyCode(aEvent.mKeyCode),
      mCharCode(aEvent.mCharCode),
      mHandledByAPZ(false) {
  switch (aEvent.mMessage) {
    case eKeyPress: {
      mType = KeyboardInput::KEY_PRESS;
      break;
    }
    case eKeyUp: {
      mType = KeyboardInput::KEY_UP;
      break;
    }
    case eKeyDown: {
      mType = KeyboardInput::KEY_DOWN;
      break;
    }
    default:
      mType = KeyboardInput::KEY_OTHER;
      break;
  }

  aEvent.GetShortcutKeyCandidates(mShortcutCandidates);
}

KeyboardInput::KeyboardInput()
    : InputData(KEYBOARD_INPUT),
      mType(KEY_DOWN),
      mKeyCode(0),
      mCharCode(0),
      mHandledByAPZ(false) {}

}  // namespace mozilla
