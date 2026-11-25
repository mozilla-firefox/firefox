/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MouseEvent.h"

#include "mozilla/BasePrincipal.h"
#include "mozilla/EventForwards.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/ViewportUtils.h"
#include "nsContentUtils.h"
#include "nsIFrame.h"
#include "nsIScreenManager.h"
#include "nsLayoutUtils.h"

namespace mozilla::dom {

static nsIntPoint DevPixelsToCSSPixels(const LayoutDeviceIntPoint& aPoint,
                                       nsPresContext* aContext) {
  return nsIntPoint(aContext->DevPixelsToIntCSSPixels(aPoint.x),
                    aContext->DevPixelsToIntCSSPixels(aPoint.y));
}

MouseEvent::MouseEvent(EventTarget* aOwner, nsPresContext* aPresContext,
                       WidgetMouseEventBase* aEvent)
    : UIEvent(aOwner, aPresContext,
              aEvent ? aEvent
                     : new WidgetMouseEvent(false, eVoidEvent, nullptr,
                                            WidgetMouseEvent::eReal)) {
  // There's no way to make this class' ctor allocate an WidgetMouseScrollEvent.
  // It's not that important, though, since a scroll event is not a real
  // DOM event.

  WidgetMouseEventBase* const mouseEventBase = mEvent->AsMouseEventBase();
  MOZ_ASSERT(mouseEventBase);
  if (aEvent) {
    mEventIsInternal = false;
  } else {
    mEventIsInternal = true;
    mEvent->mRefPoint = LayoutDeviceIntPoint(0, 0);
    mouseEventBase->mInputSource = MouseEvent_Binding::MOZ_SOURCE_UNKNOWN;
  }

  mUseFractionalCoords = mouseEventBase->DOMEventShouldUseFractionalCoords();
  mWidgetOrScreenRelativePoint = mEvent->mRefPoint;

  if (const WidgetMouseEvent* mouseEvent = mouseEventBase->AsMouseEvent()) {
    MOZ_ASSERT(mouseEvent->mReason != WidgetMouseEvent::eSynthesized,
               "Don't dispatch DOM events from synthesized mouse events");
    mDetail = static_cast<int32_t>(mouseEvent->mClickCount);
  }
}

void MouseEvent::InitMouseEventInternal(
    const nsAString& aType, bool aCanBubble, bool aCancelable,
    nsGlobalWindowInner* aView, int32_t aDetail, double aScreenX,
    double aScreenY, double aClientX, double aClientY, bool aCtrlKey,
    bool aAltKey, bool aShiftKey, bool aMetaKey, uint16_t aButton,
    EventTarget* aRelatedTarget) {
  NS_ENSURE_TRUE_VOID(!mEvent->mFlags.mIsBeingDispatched);

  UIEvent::InitUIEvent(aType, aCanBubble, aCancelable, aView, aDetail);

  switch (mEvent->mClass) {
    case eMouseEventClass:
    case eMouseScrollEventClass:
    case eWheelEventClass:
    case eDragEventClass:
    case ePointerEventClass:
    case eSimpleGestureEventClass: {
      WidgetMouseEventBase* mouseEventBase = mEvent->AsMouseEventBase();
      mouseEventBase->mRelatedTarget = aRelatedTarget;
      mouseEventBase->mButton = aButton;
      mouseEventBase->InitBasicModifiers(aCtrlKey, aAltKey, aShiftKey,
                                         aMetaKey);
      mDefaultClientPoint = CSSDoublePoint(aClientX, aClientY);
      mWidgetOrScreenRelativePoint =
          LayoutDeviceDoublePoint(aScreenX, aScreenY);
      mouseEventBase->mRefPoint =
          LayoutDeviceIntPoint::Floor(mWidgetOrScreenRelativePoint);

      WidgetMouseEvent* mouseEvent = mEvent->AsMouseEvent();
      if (mouseEvent) {
        mouseEvent->mClickCount = aDetail;
      }

      mUseFractionalCoords =
          mouseEventBase->DOMEventShouldUseFractionalCoords();
      if (!mUseFractionalCoords) {
        // If we should not use fractional coordinates for this event, we need
        // to drop the fractional part as defined for the backward compatibility
        // when we treated the input values are integer coordinates.  These
        // values will be exposed as screenX, screenY, clientX and clientY as-is
        // too.  That matches with the Pointer Events spec definitions too.
        // https://w3c.github.io/pointerevents/#event-coordinates
        mDefaultClientPoint = CSSIntPoint::Floor(mDefaultClientPoint);
        mWidgetOrScreenRelativePoint =
            LayoutDeviceIntPoint::Floor(mWidgetOrScreenRelativePoint);
      }
      break;
    }
    default:
      break;
  }
}

void MouseEvent::InitMouseEventInternal(
    const nsAString& aType, bool aCanBubble, bool aCancelable,
    nsGlobalWindowInner* aView, int32_t aDetail, double aScreenX,
    double aScreenY, double aClientX, double aClientY, int16_t aButton,
    EventTarget* aRelatedTarget, const nsAString& aModifiersList) {
  NS_ENSURE_TRUE_VOID(!mEvent->mFlags.mIsBeingDispatched);

  Modifiers modifiers = ComputeModifierState(aModifiersList);

  InitMouseEventInternal(
      aType, aCanBubble, aCancelable, aView, aDetail, aScreenX, aScreenY,
      aClientX, aClientY, (modifiers & MODIFIER_CONTROL) != 0,
      (modifiers & MODIFIER_ALT) != 0, (modifiers & MODIFIER_SHIFT) != 0,
      (modifiers & MODIFIER_META) != 0, aButton, aRelatedTarget);

  switch (mEvent->mClass) {
    case eMouseEventClass:
    case eMouseScrollEventClass:
    case eWheelEventClass:
    case eDragEventClass:
    case ePointerEventClass:
    case eSimpleGestureEventClass:
      mEvent->AsInputEvent()->mModifiers = modifiers;
      return;
    default:
      MOZ_CRASH("There is no space to store the modifiers");
  }
}

void MouseEvent::InitializeExtraMouseEventDictionaryMembers(
    const MouseEventInit& aParam) {
  InitModifiers(aParam);
  mEvent->AsMouseEventBase()->mButtons = aParam.mButtons;
  mMovementPoint.x = aParam.mMovementX;
  mMovementPoint.y = aParam.mMovementY;
}

already_AddRefed<MouseEvent> MouseEvent::Constructor(
    const GlobalObject& aGlobal, const nsAString& aType,
    const MouseEventInit& aParam) {
  nsCOMPtr<EventTarget> t = do_QueryInterface(aGlobal.GetAsSupports());
  RefPtr<MouseEvent> e = new MouseEvent(t, nullptr, nullptr);
  bool trusted = e->Init(t);
  e->InitMouseEventInternal(
      aType, aParam.mBubbles, aParam.mCancelable, aParam.mView, aParam.mDetail,
      aParam.mScreenX, aParam.mScreenY, aParam.mClientX, aParam.mClientY,
      aParam.mCtrlKey, aParam.mAltKey, aParam.mShiftKey, aParam.mMetaKey,
      aParam.mButton, aParam.mRelatedTarget);
  e->InitializeExtraMouseEventDictionaryMembers(aParam);
  e->SetTrusted(trusted);
  e->SetComposed(aParam.mComposed);
  MOZ_ASSERT(!trusted || !IsPointerEventMessage(e->mEvent->mMessage),
             "Please use PointerEvent constructor!");
  return e.forget();
}

void MouseEvent::InitNSMouseEvent(const nsAString& aType, bool aCanBubble,
                                  bool aCancelable, nsGlobalWindowInner* aView,
                                  int32_t aDetail, int32_t aScreenX,
                                  int32_t aScreenY, int32_t aClientX,
                                  int32_t aClientY, bool aCtrlKey, bool aAltKey,
                                  bool aShiftKey, bool aMetaKey,
                                  uint16_t aButton, EventTarget* aRelatedTarget,
                                  float aPressure, uint16_t aInputSource) {
  NS_ENSURE_TRUE_VOID(!mEvent->mFlags.mIsBeingDispatched);

  InitMouseEventInternal(aType, aCanBubble, aCancelable, aView, aDetail,
                         aScreenX, aScreenY, aClientX, aClientY, aCtrlKey,
                         aAltKey, aShiftKey, aMetaKey, aButton, aRelatedTarget);

  WidgetMouseEventBase* mouseEventBase = mEvent->AsMouseEventBase();
  mouseEventBase->mPressure = aPressure;
  mouseEventBase->mInputSource = aInputSource;
}

void MouseEvent::DuplicatePrivateData() {
  // If this is a event not created from WidgetMouseEventBase or its subclasses
  // (i.e., created by JS), mDefaultClientPoint and mMovementPoint are
  // initialized as expected values.  Therefore, we don't need to recompute it.
  if (!mEventIsInternal) {
    mDefaultClientPoint = ClientPoint();
    mMovementPoint = GetMovementPoint();
  }
  // However, mPagePoint needs to include the scroll position.  Therefore, we
  // need to compute here.
  mPagePoint = PagePoint();

  // mEvent->mRefPoint is computed by UIEvent::DuplicatePrivateData() with
  // the device pixel scale, but if we need to store fractional values to
  // mWidgetRelativePoint, we need to do same thing by ourselves.
  Maybe<const CSSDoublePoint> maybeScreenPoint;
  if (mUseFractionalCoords) {
    maybeScreenPoint.emplace(ScreenPoint(CallerType::System));
  }
  {
    // mPresContext will be cleared by Event::DuplicatePrivateData(), but we
    // need it after a call of it.  So, we need to grab it.
    RefPtr<nsPresContext> presContext = mPresContext.get();
    UIEvent::DuplicatePrivateData();
    mPresContext = presContext.get();
  }
  // Starting from here, mWidgetOrScreenRelativePoint (and
  // WidgetGUIEvent::mWidget) stores a screen point because we're now don't
  // store widget in mEvent.  Therefore, we cannot compute a screen point from
  // widget relative point without the widget.
  if (maybeScreenPoint.isSome()) {
    // ScreenPoint() has already computed it with the scale of mPresContext.
    // Therefore, we don't need to take care of it again.
    MOZ_ASSERT(!mEvent || !mEvent->AsGUIEvent()->mWidget);
    mWidgetOrScreenRelativePoint =
        maybeScreenPoint.ref() * CSSToLayoutDeviceScale(1);
  } else {
    // As mentioned above, mEvent->mRefPoint is already computed by UIEvent, so,
    // do not need to compute the scale.
    mWidgetOrScreenRelativePoint = mEvent->mRefPoint;
  }
}

void MouseEvent::PreventClickEvent() {
  if (WidgetMouseEvent* mouseEvent = mEvent->AsMouseEvent()) {
    mouseEvent->mClickEventPrevented = true;
  }
}

bool MouseEvent::ClickEventPrevented() {
  if (WidgetMouseEvent* mouseEvent = mEvent->AsMouseEvent()) {
    return mouseEvent->mClickEventPrevented;
  }
  return false;
}

already_AddRefed<Event> MouseEvent::GetTriggerEvent() const {
  if (WidgetMouseEvent* mouseEvent = mEvent->AsMouseEvent()) {
    NS_WARNING_ASSERTION(
        mouseEvent->mMessage == eXULPopupShowing,
        "triggerEvent is supported for popupshowing event only");
    RefPtr<Event> e = mouseEvent->mTriggerEvent;
    return e.forget();
  }
  return nullptr;
}

int16_t MouseEvent::Button() {
  switch (mEvent->mClass) {
    case eMouseEventClass:
    case eMouseScrollEventClass:
    case eWheelEventClass:
    case eDragEventClass:
    case ePointerEventClass:
    case eSimpleGestureEventClass:
      return mEvent->AsMouseEventBase()->mButton;
    default:
      NS_WARNING("Tried to get mouse mButton for non-mouse event!");
      return MouseButton::ePrimary;
  }
}

uint16_t MouseEvent::Buttons() const {
  switch (mEvent->mClass) {
    case eMouseEventClass:
    case eMouseScrollEventClass:
    case eWheelEventClass:
    case eDragEventClass:
    case ePointerEventClass:
    case eSimpleGestureEventClass:
      return mEvent->AsMouseEventBase()->mButtons;
    default:
      MOZ_CRASH("Tried to get mouse buttons for non-mouse event!");
  }
}

already_AddRefed<EventTarget> MouseEvent::GetRelatedTarget() {
  nsCOMPtr<EventTarget> relatedTarget;
  switch (mEvent->mClass) {
    case eMouseEventClass:
    case eMouseScrollEventClass:
    case eWheelEventClass:
    case eDragEventClass:
    case ePointerEventClass:
    case eSimpleGestureEventClass:
      relatedTarget = mEvent->AsMouseEventBase()->mRelatedTarget;
      break;
    default:
      break;
  }

  return EnsureWebAccessibleRelatedTarget(relatedTarget);
}

CSSDoublePoint MouseEvent::ScreenPoint(CallerType aCallerType) const {
  if (mEvent->mFlags.mIsPositionless) {
    return {};
  }

  // If this is a trusted event, mWidgetRelativeOffset is a copy of
  // mEvent->mRefPoint, so, the values are integer.
  // If this is an untrusted event, mWidgetRelativeOffset should be floored when
  // it's initialized.
  MOZ_ASSERT_IF(!mUseFractionalCoords,
                mWidgetOrScreenRelativePoint ==
                    LayoutDeviceIntPoint::Floor(mWidgetOrScreenRelativePoint));
  if (nsContentUtils::ShouldResistFingerprinting(
          aCallerType, GetParentObject(), RFPTarget::MouseEventScreenPoint)) {
    // Sanitize to something sort of like client coords, but not quite
    // (defaulting to (0,0) instead of our pre-specified client coords).
    const CSSDoublePoint clientPoint = Event::GetClientCoords(
        mPresContext, mEvent, mWidgetOrScreenRelativePoint,
        CSSDoublePoint{0, 0});
    return mUseFractionalCoords ? clientPoint : RoundedToInt(clientPoint);
  }

  const CSSDoublePoint screenPoint =
      Event::GetScreenCoords(mPresContext, mEvent, mWidgetOrScreenRelativePoint)
          .extract();
  return mUseFractionalCoords ? screenPoint : RoundedToInt(screenPoint);
}

LayoutDeviceIntPoint MouseEvent::ScreenPointLayoutDevicePix() const {
  const CSSDoublePoint point = ScreenPoint(CallerType::System);
  auto scale = mPresContext ? mPresContext->CSSToDevPixelScale()
                            : CSSToLayoutDeviceScale();
  return LayoutDeviceIntPoint::Round(point * scale);
}

DesktopIntPoint MouseEvent::ScreenPointDesktopPix() const {
  const CSSDoublePoint point = ScreenPoint(CallerType::System);
  auto scale =
      mPresContext
          ? mPresContext->CSSToDevPixelScale() /
                mPresContext->DeviceContext()->GetDesktopToDeviceScale()
          : CSSToDesktopScale();
  return DesktopIntPoint::Round(point * scale);
}

already_AddRefed<nsIScreen> MouseEvent::GetScreen() {
  nsCOMPtr<nsIScreenManager> screenMgr =
      do_GetService("@mozilla.org/gfx/screenmanager;1");
  if (!screenMgr) {
    return nullptr;
  }
  return screenMgr->ScreenForRect(
      DesktopIntRect(ScreenPointDesktopPix(), DesktopIntSize(1, 1)));
}

CSSDoublePoint MouseEvent::PagePoint() const {
  if (mEvent->mFlags.mIsPositionless) {
    return {};
  }

  if (mPrivateDataDuplicated) {
    // mPagePoint should be floored when it started to cache the values after
    // the propagation.
    MOZ_ASSERT_IF(!mUseFractionalCoords,
                  mPagePoint == CSSIntPoint::Floor(mPagePoint));
    return mPagePoint;
  }

  // If this is a trusted event, mWidgetRelativeOffset is a copy of
  // mEvent->mRefPoint, so, the values are integer.
  // If this is an untrusted event, mWidgetRelativeOffset should be floored when
  // it's initialized.
  MOZ_ASSERT_IF(!mUseFractionalCoords,
                mWidgetOrScreenRelativePoint ==
                    LayoutDeviceIntPoint::Floor(mWidgetOrScreenRelativePoint));
  // If this is a trusted event, mDefaultClientPoint should be floored when
  // it started to cache the values after the propagation.
  // If this is an untrusted event, mDefaultClientPoint should be floored when
  // it's initialized.
  MOZ_ASSERT_IF(!mUseFractionalCoords,
                mDefaultClientPoint == CSSIntPoint::Floor(mDefaultClientPoint));
  const CSSDoublePoint pagePoint = Event::GetPageCoords(
      mPresContext, mEvent, mWidgetOrScreenRelativePoint, mDefaultClientPoint);
  return mUseFractionalCoords ? pagePoint : RoundedToInt(pagePoint);
}

CSSDoublePoint MouseEvent::ClientPoint() const {
  if (mEvent->mFlags.mIsPositionless) {
    return {};
  }

  // If this is a trusted event, mWidgetRelativeOffset is a copy of
  // mEvent->mRefPoint, so, the values are integer.
  // If this is an untrusted event, mWidgetRelativeOffset should be floored when
  // it's initialized.
  MOZ_ASSERT_IF(!mUseFractionalCoords,
                mWidgetOrScreenRelativePoint ==
                    LayoutDeviceIntPoint::Floor(mWidgetOrScreenRelativePoint));
  // If this is a trusted event, mDefaultClientPoint should be floored when
  // it started to cache the values after the propagation.
  // If this is an untrusted event, mDefaultClientPoint should be floored when
  // it's initialized.
  MOZ_ASSERT_IF(!mUseFractionalCoords,
                mDefaultClientPoint == CSSIntPoint::Floor(mDefaultClientPoint));
  const CSSDoublePoint clientPoint = Event::GetClientCoords(
      mPresContext, mEvent, mWidgetOrScreenRelativePoint, mDefaultClientPoint);
  return mUseFractionalCoords ? clientPoint : RoundedToInt(clientPoint);
}

CSSDoublePoint MouseEvent::OffsetPoint() const {
  if (mEvent->mFlags.mIsPositionless) {
    return {};
  }

  // If this is a trusted event, mWidgetRelativeOffset is a copy of
  // mEvent->mRefPoint, so, the values are integer.
  // If this is an untrusted event, mWidgetRelativeOffset should be floored when
  // it's initialized.
  MOZ_ASSERT_IF(!mUseFractionalCoords,
                mWidgetOrScreenRelativePoint ==
                    LayoutDeviceIntPoint::Floor(mWidgetOrScreenRelativePoint));
  // If this is a trusted event, mDefaultClientPoint should be floored when
  // it started to cache the values after the propagation.
  // If this is an untrusted event, mDefaultClientPoint should be floored when
  // it's initialized.
  MOZ_ASSERT_IF(!mUseFractionalCoords,
                mDefaultClientPoint == CSSIntPoint::Floor(mDefaultClientPoint));
  RefPtr<nsPresContext> presContext(mPresContext);
  const CSSDoublePoint offsetPoint = Event::GetOffsetCoords(
      presContext, mEvent, mWidgetOrScreenRelativePoint, mDefaultClientPoint);
  return mUseFractionalCoords ? offsetPoint : RoundedToInt(offsetPoint);
}

nsIntPoint MouseEvent::GetMovementPoint() const {
  if (mEvent->mFlags.mIsPositionless) {
    return nsIntPoint(0, 0);
  }

  if (mPrivateDataDuplicated || mEventIsInternal) {
    return mMovementPoint;
  }

  if (!mEvent || !mEvent->AsGUIEvent()->mWidget ||
      (mEvent->mMessage != eMouseMove && mEvent->mMessage != ePointerMove &&
       !(StaticPrefs::dom_event_pointer_rawupdate_movement_enabled() &&
         mEvent->mMessage == ePointerRawUpdate))) {
    // Pointer Lock spec defines that movementX/Y must be zero for all mouse
    // events except mousemove.
    return nsIntPoint(0, 0);
  }

  // If the platform supplied raw delta information, use it instead of
  // calculating from mRefPoint/mLastRefPoint.
  if (WidgetMouseEvent* mouseEvent = mEvent->AsMouseEvent();
      mouseEvent && mouseEvent->mMovement) {
    return nsIntPoint(mouseEvent->mMovement->x, mouseEvent->mMovement->y);
  }

  // Calculate the delta between the last screen point and the current one.
  nsIntPoint current = DevPixelsToCSSPixels(mEvent->mRefPoint, mPresContext);
  nsIntPoint last = DevPixelsToCSSPixels(mEvent->mLastRefPoint, mPresContext);
  return current - last;
}

bool MouseEvent::AltKey() { return mEvent->AsInputEvent()->IsAlt(); }

bool MouseEvent::CtrlKey() { return mEvent->AsInputEvent()->IsControl(); }

bool MouseEvent::ShiftKey() { return mEvent->AsInputEvent()->IsShift(); }

bool MouseEvent::MetaKey() { return mEvent->AsInputEvent()->IsMeta(); }

float MouseEvent::MozPressure(CallerType aCallerType) const {
  if (nsContentUtils::ShouldResistFingerprinting(aCallerType, GetParentObject(),
                                                 RFPTarget::PointerEvents)) {
    // Use the spoofed value from PointerEvent::Pressure
    return Buttons() == 0 ? 0.0f : 0.5f;
  }

  return mEvent->AsMouseEventBase()->mPressure;
}

uint16_t MouseEvent::InputSource(CallerType aCallerType) const {
  if (nsContentUtils::ShouldResistFingerprinting(aCallerType, GetParentObject(),
                                                 RFPTarget::PointerEvents)) {
    return MouseEvent_Binding::MOZ_SOURCE_MOUSE;
  }

  return mEvent->AsMouseEventBase()->mInputSource;
}

}  // namespace mozilla::dom

using namespace mozilla;
using namespace mozilla::dom;

already_AddRefed<MouseEvent> NS_NewDOMMouseEvent(EventTarget* aOwner,
                                                 nsPresContext* aPresContext,
                                                 WidgetMouseEvent* aEvent) {
  RefPtr<MouseEvent> it = new MouseEvent(aOwner, aPresContext, aEvent);
  return it.forget();
}
