/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_MouseEvents_h_
#define mozilla_MouseEvents_h_

#include <stdint.h>

#include "mozilla/BasicEvents.h"
#include "mozilla/EventForwards.h"
#include "mozilla/Logging.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/Maybe.h"
#include "mozilla/dom/DataTransfer.h"
#include "mozilla/dom/Event.h"
#include "mozilla/ipc/IPCForwards.h"
#include "nsCOMPtr.h"

namespace mozilla {

namespace dom {
class PBrowserParent;
class PBrowserChild;
class PBrowserBridgeParent;
}  // namespace dom

class WidgetPointerEvent;
}  // namespace mozilla

namespace mozilla {
class WidgetPointerEventHolder final {
 public:
  nsTArray<WidgetPointerEvent> mEvents;
  NS_INLINE_DECL_REFCOUNTING(WidgetPointerEventHolder)

 private:
  virtual ~WidgetPointerEventHolder() = default;
};

/******************************************************************************
 * mozilla::WidgetPointerHelper
 ******************************************************************************/

class WidgetPointerHelper {
 public:
  struct Tilt {
    bool operator==(const Tilt&) const = default;
    int32_t mX = 0;
    int32_t mY = 0;
  };

  struct Angle {
    bool operator==(const Angle&) const = default;
    double mAltitude = 0.0;
    double mAzimuth = 0.0;
  };

  uint32_t pointerId = 0;
  Maybe<Tilt> mTilt;
  int32_t twist = 0;
  Maybe<Angle> mAngle;
  float tangentialPressure = 0.0f;
  bool convertToPointer = true;
  // When convertToPointerRawUpdate is set to true, the event or the touch may
  // cause ePointerRawUpdate event in PresShell::HandleEvent() if it's requested
  // by the web app.  This is set to false if the source mouse event or the
  // source touch move event is not dispatched immediately by BrowserChild
  // because BrowserChild dispatches only eMouseRawUpdate or eTouchRawUpdate to
  // dispatch ePointerRawUpdate immediately.
  bool convertToPointerRawUpdate = true;
  RefPtr<WidgetPointerEventHolder> mCoalescedWidgetEvents;

  WidgetPointerHelper() = default;
  WidgetPointerHelper(uint32_t aPointerId, uint32_t aTiltX, uint32_t aTiltY,
                      uint32_t aTwist = 0, float aTangentialPressure = 0)
      : pointerId(aPointerId),
        mTilt(Some(
            Tilt{static_cast<int32_t>(aTiltX), static_cast<int32_t>(aTiltY)})),
        twist(aTwist),
        tangentialPressure(aTangentialPressure),
        convertToPointer(true) {
    MOZ_ASSERT(aTiltX <= INT32_MAX);
    MOZ_ASSERT(aTiltY <= INT32_MAX);
  }

  WidgetPointerHelper(const WidgetPointerHelper&) = default;
  WidgetPointerHelper(WidgetPointerHelper&&) = default;
  WidgetPointerHelper& operator=(const WidgetPointerHelper&) = default;
  WidgetPointerHelper& operator=(WidgetPointerHelper&&) = default;

  constexpr static double kPi =
#ifdef M_PI
      M_PI;
#else
      3.14159265358979323846;
#endif
  constexpr static double kHalfPi =
#ifdef M_PI_2
      M_PI_2;
#else
      1.57079632679489661923;
#endif
  constexpr static double kDoublePi = kPi * 2;

  constexpr static double GetDefaultAltitudeAngle() { return kHalfPi; }
  constexpr static double GetDefaultAzimuthAngle() { return 0.0; }

  double ComputeAltitudeAngle() const {
    if (mAngle.isSome()) {
      return mAngle->mAltitude;
    }
    if (mTilt.isSome()) {
      return ComputeAltitudeAngle(mTilt->mX, mTilt->mY);
    }
    return GetDefaultAltitudeAngle();
  }
  double ComputeAzimuthAngle() const {
    if (mAngle.isSome()) {
      return mAngle->mAzimuth;
    }
    if (mTilt.isSome()) {
      return ComputeAzimuthAngle(mTilt->mX, mTilt->mY);
    }
    return GetDefaultAzimuthAngle();
  }

  static double ComputeAltitudeAngle(int32_t aTiltX, int32_t aTiltY);
  static double ComputeAzimuthAngle(int32_t aTiltX, int32_t aTiltY);

  constexpr static int32_t GetDefaultTiltX() { return 0; }
  constexpr static int32_t GetDefaultTiltY() { return 0; }

  int32_t ComputeTiltX() const {
    if (mTilt.isSome()) {
      return mTilt->mX;
    }
    if (mAngle.isSome()) {
      return ComputeTiltX(mAngle->mAltitude, mAngle->mAzimuth);
    }
    return GetDefaultTiltX();
  }
  int32_t ComputeTiltY() const {
    if (mTilt.isSome()) {
      return mTilt->mY;
    }
    if (mAngle.isSome()) {
      return ComputeTiltY(mAngle->mAltitude, mAngle->mAzimuth);
    }
    return GetDefaultTiltY();
  }

  static double ComputeTiltX(double aAltitudeAngle, double aAzimuthAngle);
  static double ComputeTiltY(double aAltitudeAngle, double aAzimuthAngle);

  void AssignPointerHelperData(const WidgetPointerHelper& aEvent,
                               bool aCopyCoalescedEvents = false) {
    pointerId = aEvent.pointerId;
    mTilt = aEvent.mTilt;
    twist = aEvent.twist;
    tangentialPressure = aEvent.tangentialPressure;
    convertToPointer = aEvent.convertToPointer;
    convertToPointerRawUpdate = aEvent.convertToPointerRawUpdate;
    if (aCopyCoalescedEvents) {
      mCoalescedWidgetEvents = aEvent.mCoalescedWidgetEvents;
    }
  }

 private:
  static int32_t GetValidTiltValue(int32_t aTilt);
  static double GetValidAltitudeAngle(double aAltitudeAngle);
  static double GetValidAzimuthAngle(double aAzimuthAngle);
};

/******************************************************************************
 * mozilla::WidgetMouseEventBase
 ******************************************************************************/

class WidgetMouseEventBase : public WidgetInputEvent {
 private:
  friend class dom::PBrowserParent;
  friend class dom::PBrowserChild;
  friend class dom::PBrowserBridgeParent;
  ALLOW_DEPRECATED_READPARAM

 protected:
  WidgetMouseEventBase()
      : mPressure(0),
        mButton(0),
        mButtons(0),
        mInputSource(/* MouseEvent_Binding::MOZ_SOURCE_MOUSE = */ 1) {}
  // Including MouseEventBinding.h here leads to an include loop, so
  // we have to hardcode MouseEvent_Binding::MOZ_SOURCE_MOUSE.

  WidgetMouseEventBase(bool aIsTrusted, EventMessage aMessage,
                       nsIWidget* aWidget, EventClassID aEventClassID,
                       const WidgetEventTime* aTime = nullptr)
      : WidgetInputEvent(aIsTrusted, aMessage, aWidget, aEventClassID, aTime),
        mPressure(0),
        mButton(0),
        mButtons(0),
        mInputSource(/* MouseEvent_Binding::MOZ_SOURCE_MOUSE = */ 1) {}
  // Including MouseEventBinding.h here leads to an include loop, so
  // we have to hardcode MouseEvent_Binding::MOZ_SOURCE_MOUSE.

  WidgetMouseEventBase(const WidgetMouseEventBase&) = default;
  WidgetMouseEventBase(WidgetMouseEventBase&&) = default;
  WidgetMouseEventBase& operator=(const WidgetMouseEventBase&) = default;
  WidgetMouseEventBase& operator=(WidgetMouseEventBase&&) = default;

 public:
  NS_DEFINE_AS_EVENT_OVERRIDE(Widget, MouseEventBase);

  NS_DEFINE_VIRTUAL_DESTRUCTOR_CHECKING_CLASS_VALUE(WidgetMouseEventBase,
                                                    eMouseEventBaseClass,
                                                    eInputEventClass)

  virtual WidgetEvent* Duplicate() const override {
    MOZ_CRASH("WidgetMouseEventBase must not be most-subclass");
  }

  // Finger or touch pressure of event. It ranges between 0.0 and 1.0.
  float mPressure;

  /**
   * Returns computed pressure for mouse button from mPressure, mButtons and
   * mInputSource.
   */
  [[nodiscard]] float ComputeMouseButtonPressure() const;

  // Pressed button ID of mousedown or mouseup event.
  // This is set only when pressing a button causes the event.
  int16_t mButton;

  // Flags of all pressed buttons at the event fired.
  // This is set at any mouse event, don't be confused with |mButton|.
  int16_t mButtons;

  // Possible values a in MouseEvent
  uint16_t mInputSource;

  bool IsLeftButtonPressed() const {
    return !!(mButtons & MouseButtonsFlag::ePrimaryFlag);
  }
  bool IsRightButtonPressed() const {
    return !!(mButtons & MouseButtonsFlag::eSecondaryFlag);
  }
  bool IsMiddleButtonPressed() const {
    return !!(mButtons & MouseButtonsFlag::eMiddleFlag);
  }
  bool Is4thButtonPressed() const {
    return !!(mButtons & MouseButtonsFlag::e4thFlag);
  }
  bool Is5thButtonPressed() const {
    return !!(mButtons & MouseButtonsFlag::e5thFlag);
  }

  void AssignMouseEventBaseData(const WidgetMouseEventBase& aEvent,
                                bool aCopyTargets) {
    AssignInputEventData(aEvent, aCopyTargets);

    mButton = aEvent.mButton;
    mButtons = aEvent.mButtons;
    mPressure = aEvent.mPressure;
    mInputSource = aEvent.mInputSource;
  }

  /**
   * Returns true if left click event.
   */
  bool IsLeftClickEvent() const {
    return mMessage == ePointerClick && mButton == MouseButton::ePrimary;
  }

  /**
   * Returns true if this event changes a button state to "pressed".
   */
  [[nodiscard]] bool IsPressingButton() const {
    MOZ_ASSERT(IsTrusted());
    if (mClass == eMouseEventClass) {
      return mMessage == eMouseDown;
    }
    if (mButton == MouseButton::eNotPressed) {
      return false;
    }
    // If this is an ePointerDown event whose mButton is not "not pressed", this
    // is a button pressing event.
    if (mMessage == ePointerDown) {
      return true;
    }
    // If 2 or more buttons are pressed at same time, they are sent with
    // pointermove rather than pointerdown.  Therefore, let's check whether
    // mButtons contains the proper flag for the pressing button.
    const bool buttonsContainButton = !!(
        mButtons & MouseButtonsFlagToChange(static_cast<MouseButton>(mButton)));
    return mMessage == ePointerMove && buttonsContainButton;
  }

  /**
   * Returns true if this event changes a button state to "released".
   */
  [[nodiscard]] bool IsReleasingButton() const {
    MOZ_ASSERT(IsTrusted());
    if (mClass == eMouseEventClass) {
      return mMessage == eMouseUp;
    }
    if (mButton == MouseButton::eNotPressed) {
      return false;
    }
    // If this is an ePointerUp event whose mButton is not "not pressed", this
    // is a button release event.
    if (mMessage == ePointerUp) {
      return true;
    }
    // If the releasing button is not the last button of pressing buttons, web
    // apps notified by pointermove rather than pointerup.  Therefore, let's
    // check whether mButtons loses the proper flag for the releasing button.
    const bool buttonsLoseTheButton = !(
        mButtons & MouseButtonsFlagToChange(static_cast<MouseButton>(mButton)));
    return mMessage == ePointerMove && buttonsLoseTheButton;
  }

  [[nodiscard]] int16_t ComputeButtonsBeforeDispatch() const {
    if (IsPressingButton()) {
      return mButtons &
             ~MouseButtonsFlagToChange(static_cast<MouseButton>(mButton));
    }
    if (IsReleasingButton()) {
      return mButtons |
             MouseButtonsFlagToChange(static_cast<MouseButton>(mButton));
    }
    return mButtons;
  }

  [[nodiscard]] static bool InputSourceSupportsHover(uint16_t aInputSource);

  /**
   * Returns true if the input source supports hover state like a mouse.
   */
  [[nodiscard]] bool InputSourceSupportsHover() const {
    return InputSourceSupportsHover(mInputSource);
  }

  /**
   * Returns true if corresponding DOM event should use fractional coordinates.
   */
  [[nodiscard]] bool DOMEventShouldUseFractionalCoords() const;
};

/******************************************************************************
 * mozilla::WidgetMouseEvent
 ******************************************************************************/

// Represents the default action of the X1/X2 mouse button, which might be
// a WidgetCommandEvent or WidgetContentCommandEvent.
class WidgetMouseEventCommand {
 public:
  WidgetMouseEventCommand() = default;
  MOZ_IMPLICIT
  WidgetMouseEventCommand(EventMessage aContentCommand)
      : mContentCommand(aContentCommand) {
    MOZ_ASSERT(mContentCommand >= eContentCommandEventFirst &&
               mContentCommand <= eContentCommandEventLast);
  }
  MOZ_IMPLICIT
  WidgetMouseEventCommand(nsStaticAtom* aBrowserCommand)
      : mBrowserCommand(aBrowserCommand) {}
  explicit operator bool() const { return mBrowserCommand || mContentCommand; }
  MOZ_CAN_RUN_SCRIPT void Run(nsIWidget* aRootWidget);

 private:
  EventMessage mContentCommand = eVoidEvent;
  nsStaticAtom* mBrowserCommand = nullptr;
};

class WidgetMouseEvent : public WidgetMouseEventBase,
                         public WidgetPointerHelper {
 private:
  friend class dom::PBrowserParent;
  friend class dom::PBrowserChild;
  friend class dom::PBrowserBridgeParent;
  ALLOW_DEPRECATED_READPARAM

 public:
  enum Reason : bool { eReal, eSynthesized };

  enum ContextMenuTrigger : uint8_t { eNormal, eContextMenuKey, eControlClick };

  enum ExitFrom : uint8_t {
    ePlatformChild,
    ePlatformTopLevel,
    ePuppet,
    ePuppetParentToPuppetChild
  };

 protected:
  WidgetMouseEvent() = default;

  WidgetMouseEvent(bool aIsTrusted, EventMessage aMessage, nsIWidget* aWidget,
                   EventClassID aEventClassID, Reason aReason,
                   ContextMenuTrigger aContextMenuTrigger,
                   const WidgetEventTime* aTime = nullptr)
      : WidgetMouseEventBase(aIsTrusted, aMessage, aWidget, aEventClassID,
                             aTime),
        mReason(aReason),
        mContextMenuTrigger(aContextMenuTrigger) {}

#ifdef DEBUG
  void AssertContextMenuEventButtonConsistency() const;
#endif

 public:
  NS_DEFINE_AS_EVENT_OVERRIDE(Widget, MouseEvent);

  WidgetMouseEvent(bool aIsTrusted, EventMessage aMessage, nsIWidget* aWidget,
                   Reason aReason = eReal,
                   ContextMenuTrigger aContextMenuTrigger = eNormal,
                   const WidgetEventTime* aTime = nullptr)
      : WidgetMouseEventBase(aIsTrusted, aMessage, aWidget, eMouseEventClass,
                             aTime),
        mReason(aReason),
        mContextMenuTrigger(aContextMenuTrigger) {
    MOZ_ASSERT_IF(aIsTrusted, !IsPointerEventMessage(mMessage));
    if (aMessage == eContextMenu) {
      mButton = (mContextMenuTrigger == eNormal) ? MouseButton::eSecondary
                                                 : MouseButton::ePrimary;
    }
  }

  /**
   * WidgetMouseEvent's copy constructor does not assign some members
   * intentionally. See AssignMouseEventDataOnly() for the detail.
   */
  WidgetMouseEvent(const WidgetMouseEvent& aEvent)
      : WidgetMouseEventBase(aEvent), WidgetPointerHelper(aEvent) {
    AssignMouseEventDataOnly(aEvent);
  }
  /**
   * WidgetMouseEvent's move constructor does not assign some members
   * intentionally. See AssignMouseEventDataOnly() for the detail.
   */
  WidgetMouseEvent(WidgetMouseEvent&& aEvent)
      : WidgetMouseEventBase(
            std::move(static_cast<WidgetMouseEventBase&>(aEvent))),
        WidgetPointerHelper(
            std::move(static_cast<WidgetPointerHelper&>(aEvent))) {
    AssignMouseEventDataOnly(aEvent);
  }
  WidgetMouseEvent& operator=(const WidgetMouseEvent&) = default;
  WidgetMouseEvent& operator=(WidgetMouseEvent&&) = default;

  // We shouldn't allow implicit lossy copymove from a subclass instance to
  // prevent unexpected regressions. Therefore, these are deleted.
  WidgetMouseEvent(const WidgetDragEvent&) = delete;
  WidgetMouseEvent(const WidgetPointerEvent&) = delete;
  WidgetMouseEvent(WidgetDragEvent&&) = delete;
  WidgetMouseEvent(WidgetPointerEvent&&) = delete;
  WidgetMouseEvent& operator=(const WidgetDragEvent&) = delete;
  WidgetMouseEvent& operator=(const WidgetPointerEvent&) = delete;
  WidgetMouseEvent& operator=(WidgetDragEvent&&) = delete;
  WidgetMouseEvent& operator=(WidgetPointerEvent&&) = delete;

  /**
   * Make a copy of WidgetMouseEvent whose members are assigned from aOther even
   * if it's a subclass of WidgetMouseEvent. I.e., the subclass's members will
   * be lost in the new instance.
   *
   * NOTE: This uses the copy constructor of WidgetMouseEvent so that some
   * members are not assigned intentionally.
   */
  static WidgetMouseEvent MakeLossyCopy(const WidgetMouseEvent& aOther,
                                        EventMessage aMouseEventMessage) {
    MOZ_ASSERT(aMouseEventMessage >= eMouseEventFirst &&
               aMouseEventMessage <= eMouseEventLast);
    WidgetMouseEvent copy(aOther);
    copy.mMessage = aMouseEventMessage;
    copy.mClass = eMouseEventClass;
    // Any mouse event message shouldn't have mSpecifiedEventType so that let's
    // clear it.
    copy.mSpecifiedEventType = nullptr;
    // mContextMenuTrigger should be used only when the message is eContextMenu
    // but its trusted event is now a pointer event. So, we can always set it to
    // the default value.
    copy.mContextMenuTrigger = ContextMenuTrigger::eNormal;
    return copy;
  }

#ifdef DEBUG
  virtual ~WidgetMouseEvent() {
    AssertContextMenuEventButtonConsistency();
    NS_ASSERT_EVENT_CLASS_ID(eMouseEventClass, eMouseEventBaseClass);
  }
#endif

  virtual WidgetEvent* Duplicate() const override {
    MOZ_ASSERT(mClass == eMouseEventClass,
               "Duplicate() must be overridden by sub class");
    // Not copying widget, it is a weak reference.
    WidgetMouseEvent* result = new WidgetMouseEvent(
        false, mMessage, nullptr, mReason, mContextMenuTrigger, this);
    result->AssignMouseEventData(*this, true);
    result->mFlags = mFlags;
    return result;
  }

 public:
  // If during mouseup handling we detect that click event might need to be
  // dispatched, this is setup to be the target of the click event.
  nsCOMPtr<dom::EventTarget> mClickTarget;

  // Command to execute if the event is not default prevented.
  WidgetMouseEventCommand mCommand;

  // mReason indicates the reason why the event is fired:
  // - Representing mouse operation.
  // - Synthesized for emulating mousemove event when the content under the
  //   mouse cursor is scrolled.
  Reason mReason = eReal;

  // mContextMenuTrigger is valid only when mMessage is eContextMenu.
  // This indicates if the context menu event is caused by context menu key or
  // other reasons (typically, a click of right mouse button).
  ContextMenuTrigger mContextMenuTrigger = eNormal;

  // mExitFrom contains a value only when mMessage is eMouseExitFromWidget.
  // This indicates if the mouse cursor exits from a top level platform widget,
  // a child widget or a puppet widget.
  Maybe<ExitFrom> mExitFrom;

  // mClickCount may be non-zero value when mMessage is eMouseDown, eMouseUp,
  // ePointerClick or eMouseDoubleClick. The number is count of mouse clicks.
  // Otherwise, this must be 0.
  uint32_t mClickCount = 0;

  // Whether the event should ignore scroll frame bounds during dispatch.
  bool mIgnoreRootScrollFrame = false;

  // Whether the event should be dispatched on a target limited in capturing
  // content.
  bool mIgnoreCapturingContent = false;

  // Whether the event shouldn't cause click event.
  bool mClickEventPrevented = false;

  // If this is set to true while the event is being dispatched,
  // PresShell::EventHandler::FinalizeHandlingEvent will dispatch a synthesized
  // eMouseMove or ePointerMove.
  bool mSynthesizeMoveAfterDispatch = false;

  // The event that triggered this event.
  // This will be available for popupshowing event only.
  RefPtr<dom::Event> mTriggerEvent;

  /**
   * An optional identifier for the callback associated with this wheel event.
   * This ID is used to reference a specific callback for a synthesized event,
   * if one is present. If no callback is associated, this value will be empty.
   */
  Maybe<uint64_t> mCallbackId;

  /**
   * Movement raw delta. Set by the platform widget. When set,
   * MouseEvent::movementX/Y use this value instead of computing a delta from
   * mRefPoint/mLastRefPoint.
   */
  Maybe<LayoutDeviceIntPoint> mMovement;

  void AssignMouseEventData(const WidgetMouseEvent& aEvent, bool aCopyTargets,
                            bool aCopyCoalescedEvents = true) {
    AssignMouseEventBaseData(aEvent, aCopyTargets);
    AssignPointerHelperData(aEvent, aCopyCoalescedEvents);
    AssignMouseEventDataOnly(aEvent);
  }

  void AssignMouseEventDataOnly(const WidgetMouseEvent& aEvent) {
    // NOTE: Intentionally not copying mClickTarget, it should only be used by
    //       the original mouseup event to dispatch the click event.
    mReason = aEvent.mReason;
    mContextMenuTrigger = aEvent.mContextMenuTrigger;
    mExitFrom = aEvent.mExitFrom;
    mClickCount = aEvent.mClickCount;
    mIgnoreRootScrollFrame = aEvent.mIgnoreRootScrollFrame;
    mIgnoreCapturingContent = aEvent.mIgnoreCapturingContent;
    mClickEventPrevented = aEvent.mClickEventPrevented;
    // NOTE: Intentionally not copying mSynthesizeMoveAfterDispatch, it should
    //       only be used by the original event to check whether we need to
    //       synthesize an additional mousemove or pointermove event.
    mTriggerEvent = aEvent.mTriggerEvent;
    // NOTE: Intentionally not copying mCallbackId, it should only be tracked by
    //       the original event or propagated to the cross-process event.
    mMovement = aEvent.mMovement;
    mCommand = aEvent.mCommand;
  }

  /**
   * Returns true if the event is a context menu event caused by key.
   */
  bool IsContextMenuKeyEvent() const {
    return mMessage == eContextMenu && mContextMenuTrigger == eContextMenuKey;
  }

  /**
   * Returns true if the event is a real mouse event.  Otherwise, i.e., it's
   * a synthesized event by scroll or something, returns false.
   */
  [[nodiscard]] bool IsReal() const { return mReason == eReal; }

  /**
   * Returns true if the event is synthesized for scroll or layout change.
   * Do not confuse this with a synthesized event for tests.
   */
  [[nodiscard]] bool IsSynthesized() const { return mReason == eSynthesized; }

  /**
   * Returns true if middle click paste is enabled.
   */
  static bool IsMiddleClickPasteEnabled();
};

// Used for logging WidgetMouseEvent::IsReal() (or
// !WidgetMouseEvent::IsSynthesized())
MOZ_DEFINE_BOOL_PRETTY_PRINTER(RealOrSynthesized, Real, Synthesized);

/******************************************************************************
 * mozilla::WidgetDragEvent
 ******************************************************************************/

class WidgetDragEvent final : public WidgetMouseEvent {
 private:
  friend class mozilla::dom::PBrowserParent;
  friend class mozilla::dom::PBrowserChild;
  ALLOW_DEPRECATED_READPARAM

 protected:
  WidgetDragEvent()
      : mUserCancelled(false),
        mDefaultPreventedOnContent(false),
        mInHTMLEditorEventListener(false) {}

 public:
  NS_DEFINE_AS_EVENT_OVERRIDE(Widget, DragEvent);

  WidgetDragEvent(bool aIsTrusted, EventMessage aMessage, nsIWidget* aWidget,
                  const WidgetEventTime* aTime = nullptr)
      : WidgetMouseEvent(aIsTrusted, aMessage, aWidget, eDragEventClass, eReal,
                         eNormal, aTime),
        mUserCancelled(false),
        mDefaultPreventedOnContent(false),
        mInHTMLEditorEventListener(false) {}

  NS_DEFINE_VIRTUAL_DESTRUCTOR_CHECKING_CLASS_VALUE(WidgetDragEvent,
                                                    eDragEventClass,
                                                    eMouseEventClass)

  virtual WidgetEvent* Duplicate() const override {
    MOZ_ASSERT(mClass == eDragEventClass,
               "Duplicate() must be overridden by sub class");
    // Not copying widget, it is a weak reference.
    WidgetDragEvent* result =
        new WidgetDragEvent(false, mMessage, nullptr, this);
    result->AssignDragEventData(*this, true);
    result->mFlags = mFlags;
    return result;
  }

  // The dragging data.
  nsCOMPtr<dom::DataTransfer> mDataTransfer;

  // If this is true, user has cancelled the drag operation.
  bool mUserCancelled;
  // If this is true, the drag event's preventDefault() is called on content.
  bool mDefaultPreventedOnContent;
  // If this event is currently being handled by HTMLEditorEventListener.
  bool mInHTMLEditorEventListener;

  // XXX Not tested by test_assign_event_data.html
  void AssignDragEventData(const WidgetDragEvent& aEvent, bool aCopyTargets) {
    AssignMouseEventData(aEvent, aCopyTargets);

    mDataTransfer = aEvent.mDataTransfer;
    // XXX mUserCancelled isn't copied, is this intentionally?
    mUserCancelled = false;
    mDefaultPreventedOnContent = aEvent.mDefaultPreventedOnContent;
    // XXX mInHTMLEditorEventListener isn't copied, is this intentionally?
    mInHTMLEditorEventListener = false;
  }

  bool CanConvertToInputData() const {
    return mMessage == eDragStart || mMessage == eDragEnd ||
           mMessage == eDragEnter || mMessage == eDragOver ||
           mMessage == eDragExit || mMessage == eDrop;
  }

  /**
   * Should be called before dispatching the DOM tree if this event is
   * synthesized for tests because drop effect is initialized before
   * dispatching from widget if it's not synthesized event, but synthesized
   * events are not initialized in the path.
   */
  void InitDropEffectForTests();
};

/******************************************************************************
 * mozilla::WidgetMouseScrollEvent
 *
 * This is used for legacy DOM mouse scroll events, i.e.,
 * DOMMouseScroll and MozMousePixelScroll event.  These events are NOT hanbled
 * by ESM even if widget dispatches them.  Use new WidgetWheelEvent instead.
 ******************************************************************************/

class WidgetMouseScrollEvent : public WidgetMouseEventBase {
 private:
  WidgetMouseScrollEvent() : mDelta(0), mIsHorizontal(false) {}

 public:
  NS_DEFINE_AS_EVENT_OVERRIDE(Widget, MouseScrollEvent);

  WidgetMouseScrollEvent(bool aIsTrusted, EventMessage aMessage,
                         nsIWidget* aWidget,
                         const WidgetEventTime* aTime = nullptr)
      : WidgetMouseEventBase(aIsTrusted, aMessage, aWidget,
                             eMouseScrollEventClass, aTime),
        mDelta(0),
        mIsHorizontal(false) {}

  NS_DEFINE_VIRTUAL_DESTRUCTOR_CHECKING_CLASS_VALUE(WidgetMouseScrollEvent,
                                                    eMouseScrollEventClass,
                                                    eMouseEventBaseClass)

  virtual WidgetEvent* Duplicate() const override {
    MOZ_ASSERT(mClass == eMouseScrollEventClass,
               "Duplicate() must be overridden by sub class");
    // Not copying widget, it is a weak reference.
    WidgetMouseScrollEvent* result =
        new WidgetMouseScrollEvent(false, mMessage, nullptr, this);
    result->AssignMouseScrollEventData(*this, true);
    result->mFlags = mFlags;
    return result;
  }

  // The delta value of mouse scroll event.
  // If the event message is eLegacyMouseLineOrPageScroll, the value indicates
  // scroll amount in lines.  However, if the value is
  // UIEvent::SCROLL_PAGE_UP or UIEvent::SCROLL_PAGE_DOWN, the
  // value inducates one page scroll.  If the event message is
  // eLegacyMousePixelScroll, the value indicates scroll amount in pixels.
  int32_t mDelta;

  // If this is true, it may cause to scroll horizontally.
  // Otherwise, vertically.
  bool mIsHorizontal;

  void AssignMouseScrollEventData(const WidgetMouseScrollEvent& aEvent,
                                  bool aCopyTargets) {
    AssignMouseEventBaseData(aEvent, aCopyTargets);

    mDelta = aEvent.mDelta;
    mIsHorizontal = aEvent.mIsHorizontal;
  }
};

/******************************************************************************
 * mozilla::WidgetWheelEvent
 ******************************************************************************/

class WidgetWheelEvent : public WidgetMouseEventBase {
 private:
  friend class mozilla::dom::PBrowserParent;
  friend class mozilla::dom::PBrowserChild;
  ALLOW_DEPRECATED_READPARAM

  WidgetWheelEvent()
      : mDeltaX(0.0),
        mDeltaY(0.0),
        mDeltaZ(0.0),
        mOverflowDeltaX(0.0),
        mOverflowDeltaY(0.0)
        // Including WheelEventBinding.h here leads to an include loop, so
        // we have to hardcode WheelEvent_Binding::DOM_DELTA_PIXEL.
        ,
        mDeltaMode(/* WheelEvent_Binding::DOM_DELTA_PIXEL = */ 0),
        mLineOrPageDeltaX(0),
        mLineOrPageDeltaY(0),
        mScrollType(SCROLL_DEFAULT),
        mCustomizedByUserPrefs(false),
        mMayHaveMomentum(false),
        mIsMomentum(false),
        mIsNoLineOrPageDelta(false),
        mViewPortIsOverscrolled(false),
        mCanTriggerSwipe(false),
        mAllowToOverrideSystemScrollSpeed(false),
        mDeltaValuesHorizontalizedForDefaultHandler(false) {}

 public:
  NS_DEFINE_AS_EVENT_OVERRIDE(Widget, WheelEvent);

  WidgetWheelEvent(bool aIsTrusted, EventMessage aMessage, nsIWidget* aWidget,
                   const WidgetEventTime* aTime = nullptr)
      : WidgetMouseEventBase(aIsTrusted, aMessage, aWidget, eWheelEventClass,
                             aTime),
        mDeltaX(0.0),
        mDeltaY(0.0),
        mDeltaZ(0.0),
        mOverflowDeltaX(0.0),
        mOverflowDeltaY(0.0)
        // Including WheelEventBinding.h here leads to an include loop, so
        // we have to hardcode WheelEvent_Binding::DOM_DELTA_PIXEL.
        ,
        mDeltaMode(/* WheelEvent_Binding::DOM_DELTA_PIXEL = */ 0),
        mLineOrPageDeltaX(0),
        mLineOrPageDeltaY(0),
        mScrollType(SCROLL_DEFAULT),
        mCustomizedByUserPrefs(false),
        mMayHaveMomentum(false),
        mIsMomentum(false),
        mIsNoLineOrPageDelta(false),
        mViewPortIsOverscrolled(false),
        mCanTriggerSwipe(false),
        mAllowToOverrideSystemScrollSpeed(true),
        mDeltaValuesHorizontalizedForDefaultHandler(false) {}

  NS_DEFINE_VIRTUAL_DESTRUCTOR_CHECKING_CLASS_VALUE(WidgetWheelEvent,
                                                    eWheelEventClass,
                                                    eMouseEventBaseClass)

  virtual WidgetEvent* Duplicate() const override {
    MOZ_ASSERT(mClass == eWheelEventClass,
               "Duplicate() must be overridden by sub class");
    // Not copying widget, it is a weak reference.
    WidgetWheelEvent* result =
        new WidgetWheelEvent(false, mMessage, nullptr, this);
    result->AssignWheelEventData(*this, true);
    result->mFlags = mFlags;
    return result;
  }

  // Scroll gestures that start at the edge of the scrollable range can result
  // in a swipe gesture. For the first wheel event of such a gesture, call
  // TriggersSwipe() after the event has been processed in order to find out
  // whether a swipe should be started.
  bool TriggersSwipe() const {
    return mCanTriggerSwipe && mViewPortIsOverscrolled &&
           this->mOverflowDeltaX != 0.0;
  }

  // NOTE: mDeltaX, mDeltaY and mDeltaZ may be customized by
  //       mousewheel.*.delta_multiplier_* prefs which are applied by
  //       EventStateManager.  So, after widget dispatches this event,
  //       these delta values may have different values than before.
  double mDeltaX;
  double mDeltaY;
  double mDeltaZ;

  // The mousewheel tick counts.
  double mWheelTicksX = 0.0;
  double mWheelTicksY = 0.0;

  enum class DeltaModeCheckingState : uint8_t {
    // Neither deltaMode nor the delta values have been accessed.
    Unknown,
    // The delta values have been accessed, without checking deltaMode first.
    Unchecked,
    // The deltaMode has been checked.
    Checked,
  };

  // For compat reasons, we might expose a DOM_DELTA_LINE event as
  // DOM_DELTA_PIXEL instead. Whether we do that depends on whether the event
  // has been asked for the deltaMode before the deltas. If it has, we assume
  // that the page will correctly handle DOM_DELTA_LINE. This variable tracks
  // that state. See bug 1392460.
  DeltaModeCheckingState mDeltaModeCheckingState =
      DeltaModeCheckingState::Unknown;

  // The amount of scrolling per line or page, without accounting for mouse
  // wheel transactions etc.
  //
  // Computed by EventStateManager::DeltaAccumulator::InitLineOrPageDelta.
  nsSize mScrollAmount;

  // overflowed delta values for scroll, these values are set by
  // EventStateManger.  If the default action of the wheel event isn't scroll,
  // these values are always zero.  Otherwise, remaining delta values which are
  // not used by scroll are set.
  // NOTE: mDeltaX, mDeltaY and mDeltaZ may be modified by EventStateManager.
  //       However, mOverflowDeltaX and mOverflowDeltaY indicate unused original
  //       delta values which are not applied the delta_multiplier prefs.
  //       So, if widget wanted to know the actual direction to be scrolled,
  //       it would need to check the mDeltaX and mDeltaY.
  double mOverflowDeltaX;
  double mOverflowDeltaY;

  // Should be one of WheelEvent_Binding::DOM_DELTA_*
  uint32_t mDeltaMode;

  // If widget sets mLineOrPageDelta, EventStateManager will dispatch
  // eLegacyMouseLineOrPageScroll event for compatibility.  Note that the delta
  // value means pages if the mDeltaMode is DOM_DELTA_PAGE, otherwise, lines.
  int32_t mLineOrPageDeltaX;
  int32_t mLineOrPageDeltaY;

  // When the default action for an wheel event is moving history or zooming,
  // need to chose a delta value for doing it.
  int32_t GetPreferredIntDelta() {
    if (!mLineOrPageDeltaX && !mLineOrPageDeltaY) {
      return 0;
    }
    if (mLineOrPageDeltaY && !mLineOrPageDeltaX) {
      return mLineOrPageDeltaY;
    }
    if (mLineOrPageDeltaX && !mLineOrPageDeltaY) {
      return mLineOrPageDeltaX;
    }
    if ((mLineOrPageDeltaX < 0 && mLineOrPageDeltaY > 0) ||
        (mLineOrPageDeltaX > 0 && mLineOrPageDeltaY < 0)) {
      return 0;  // We cannot guess the answer in this case.
    }
    return (Abs(mLineOrPageDeltaX) > Abs(mLineOrPageDeltaY))
               ? mLineOrPageDeltaX
               : mLineOrPageDeltaY;
  }

  // Scroll type
  // The default value is SCROLL_DEFAULT, which means EventStateManager will
  // select preferred scroll type automatically.
  enum ScrollType : uint8_t {
    SCROLL_DEFAULT,
    SCROLL_SYNCHRONOUSLY,
    SCROLL_ASYNCHRONOUSLY,
    SCROLL_SMOOTHLY
  };
  ScrollType mScrollType;

  // If the delta values are computed from prefs, this value is true.
  // Otherwise, i.e., they are computed from native events, false.
  bool mCustomizedByUserPrefs;

  // true if the momentum events directly tied to this event may follow it.
  bool mMayHaveMomentum;
  // true if the event is caused by momentum.
  bool mIsMomentum;

  // If device event handlers don't know when they should set mLineOrPageDeltaX
  // and mLineOrPageDeltaY, this is true.  Otherwise, false.
  // If mIsNoLineOrPageDelta is true, ESM will generate
  // eLegacyMouseLineOrPageScroll events when accumulated delta values reach
  // a line height.
  bool mIsNoLineOrPageDelta;

  // Whether or not the parent of the currently overscrolled frame is the
  // ViewPort. This is false in situations when an element on the page is being
  // overscrolled (such as a text field), but true when the 'page' is being
  // overscrolled.
  bool mViewPortIsOverscrolled;

  // The wheel event can trigger a swipe to start if it's overscrolling the
  // viewport.
  bool mCanTriggerSwipe;

  // If mAllowToOverrideSystemScrollSpeed is true, the scroll speed may be
  // overridden.  Otherwise, the scroll speed won't be overridden even if
  // it's enabled by the pref.
  bool mAllowToOverrideSystemScrollSpeed;

  // After the event's default action handler has adjusted its delta's values
  // for horizontalizing a vertical wheel scroll, this variable will be set to
  // true.
  bool mDeltaValuesHorizontalizedForDefaultHandler;

  /**
   * An optional identifier for the callback associated with this wheel event.
   * This ID is used to reference a specific callback for a synthesized event,
   * if one is present. If no callback is associated, this value will be empty.
   */
  Maybe<uint64_t> mCallbackId;

  void AssignWheelEventData(const WidgetWheelEvent& aEvent, bool aCopyTargets) {
    AssignMouseEventBaseData(aEvent, aCopyTargets);

    mDeltaX = aEvent.mDeltaX;
    mDeltaY = aEvent.mDeltaY;
    mDeltaZ = aEvent.mDeltaZ;
    mDeltaMode = aEvent.mDeltaMode;
    mScrollAmount = aEvent.mScrollAmount;
    mCustomizedByUserPrefs = aEvent.mCustomizedByUserPrefs;
    mMayHaveMomentum = aEvent.mMayHaveMomentum;
    mIsMomentum = aEvent.mIsMomentum;
    mIsNoLineOrPageDelta = aEvent.mIsNoLineOrPageDelta;
    mLineOrPageDeltaX = aEvent.mLineOrPageDeltaX;
    mLineOrPageDeltaY = aEvent.mLineOrPageDeltaY;
    mScrollType = aEvent.mScrollType;
    mOverflowDeltaX = aEvent.mOverflowDeltaX;
    mOverflowDeltaY = aEvent.mOverflowDeltaY;
    mViewPortIsOverscrolled = aEvent.mViewPortIsOverscrolled;
    mCanTriggerSwipe = aEvent.mCanTriggerSwipe;
    mAllowToOverrideSystemScrollSpeed =
        aEvent.mAllowToOverrideSystemScrollSpeed;
    mDeltaValuesHorizontalizedForDefaultHandler =
        aEvent.mDeltaValuesHorizontalizedForDefaultHandler;
    // NOTE: Intentionally not copying mCallbackId, it should only be tracked by
    //       the original event or propagated to the cross-process event.
  }

  // System scroll speed settings may be too slow at using Gecko.  In such
  // case, we should override the scroll speed computed with system settings.
  // Following methods return preferred delta values which are multiplied by
  // factors specified by prefs.  If system scroll speed shouldn't be
  // overridden (e.g., this feature is disabled by pref), they return raw
  // delta values.
  double OverriddenDeltaX() const;
  double OverriddenDeltaY() const;

  // Compute the overridden delta value.  This may be useful for suppressing
  // too fast scroll by system scroll speed overriding when widget sets
  // mAllowToOverrideSystemScrollSpeed.
  static double ComputeOverriddenDelta(double aDelta, bool aIsForVertical);

 private:
  static bool sInitialized;
  static bool sIsSystemScrollSpeedOverrideEnabled;
  static int32_t sOverrideFactorX;
  static int32_t sOverrideFactorY;
  static void Initialize();
};

/******************************************************************************
 * mozilla::WidgetPointerEvent
 ******************************************************************************/

class WidgetPointerEvent final : public WidgetMouseEvent {
  friend class mozilla::dom::PBrowserParent;
  friend class mozilla::dom::PBrowserChild;
  ALLOW_DEPRECATED_READPARAM

  WidgetPointerEvent() = default;

 public:
  NS_DEFINE_AS_EVENT_OVERRIDE(Widget, PointerEvent);

  WidgetPointerEvent(bool aIsTrusted, EventMessage aMsg, nsIWidget* w,
                     const WidgetEventTime* aTime)
      : WidgetMouseEvent(aIsTrusted, aMsg, w, ePointerEventClass, eReal,
                         eNormal, aTime) {
    if (aMsg == eContextMenu) {
      mButton = (mContextMenuTrigger == eNormal) ? MouseButton::eSecondary
                                                 : MouseButton::ePrimary;
    }
  }

  WidgetPointerEvent(bool aIsTrusted, EventMessage aMsg, nsIWidget* w,
                     ContextMenuTrigger aContextMenuTrigger = eNormal,
                     const WidgetEventTime* aTime = nullptr)
      : WidgetMouseEvent(aIsTrusted, aMsg, w, ePointerEventClass, eReal,
                         aContextMenuTrigger, aTime) {
    if (aMsg == eContextMenu) {
      mButton = (mContextMenuTrigger == eNormal) ? MouseButton::eSecondary
                                                 : MouseButton::ePrimary;
    }
  }

 private:
  /**
   * Don't make this converting constructor because this kind of code would use
   * this constructor instead of the normal copy constructor:
   *
   * void Foo(const WidgetMouseEvent& aPointerOrMouseEvent) {
   *   WidgetPointerEvent newEvent(aPointerOrMouseEvent);
   * }
   *
   * The author must have intended to write:
   *
   * void Foo(const WidgetMouseEvent& aPointerOrMouseEvent) {
   *   WidgetPointerEvent newEvent(
   *     static_cast<WidgetPointerEvent&>(aPointerOrMouseEvent));
   * }
   *
   * Instead, use MakeCopyFromMouseEvent().
   */
  explicit WidgetPointerEvent(const WidgetMouseEvent& aEvent)
      : WidgetMouseEvent(aEvent) {
    MOZ_ASSERT(!aEvent.AsPointerEvent(),
               "You're using wrong copy constructor, cast the source event to "
               "`const WidgetPointerEvent&`");
    mClass = ePointerEventClass;
  }

 public:
  static inline WidgetPointerEvent MakeCopyFromMouseEvent(
      const WidgetMouseEvent& aPointerOrMouseEvent) {
    if (aPointerOrMouseEvent.mClass == ePointerEventClass) {
      MOZ_ASSERT(aPointerOrMouseEvent.AsPointerEvent());
      return WidgetPointerEvent(
          static_cast<const WidgetPointerEvent&>(aPointerOrMouseEvent));
    }
    MOZ_ASSERT(!aPointerOrMouseEvent.AsPointerEvent());
    return WidgetPointerEvent(aPointerOrMouseEvent);
  }

  explicit WidgetPointerEvent(EventMessage aMsg,
                              const WidgetPointerEvent& aOther)
      : WidgetPointerEvent(aOther.IsTrusted(), aMsg, aOther.mWidget, &aOther) {
    AssignPointerEventData(aOther, false);
  }

  NS_DEFINE_VIRTUAL_DESTRUCTOR_CHECKING_CLASS_VALUE(WidgetPointerEvent,
                                                    ePointerEventClass,
                                                    eMouseEventClass)

  virtual WidgetEvent* Duplicate() const override {
    MOZ_ASSERT(mClass == ePointerEventClass,
               "Duplicate() must be overridden by sub class");
    // Not copying widget, it is a weak reference.
    WidgetPointerEvent* result = new WidgetPointerEvent(
        false, mMessage, nullptr, mContextMenuTrigger, this);
    result->AssignPointerEventData(*this, true);
    result->mFlags = mFlags;
    return result;
  }

  double mWidth = 1.0;
  double mHeight = 1.0;
  bool mIsPrimary = true;
  bool mFromTouchEvent = false;

  // XXX Not tested by test_assign_event_data.html
  void AssignPointerEventData(const WidgetPointerEvent& aEvent,
                              bool aCopyTargets,
                              bool aCopyCoalescedEvents = true) {
    AssignMouseEventData(aEvent, aCopyTargets, aCopyCoalescedEvents);

    mWidth = aEvent.mWidth;
    mHeight = aEvent.mHeight;
    mIsPrimary = aEvent.mIsPrimary;
    mFromTouchEvent = aEvent.mFromTouchEvent;
  }
};

}  // namespace mozilla

#endif  // mozilla_MouseEvents_h_
