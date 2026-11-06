/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_AccessibleCaretEventHub_h
#define mozilla_AccessibleCaretEventHub_h

#include "LayoutConstants.h"
#include "mozilla/EventForwards.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WeakPtr.h"
#include "nsCOMPtr.h"
#include "nsDocShell.h"
#include "nsIScrollObserver.h"
#include "nsPoint.h"
#include "nsWeakReference.h"

class nsITimer;

namespace mozilla {
class AccessibleCaretManager;
class PresShell;
class WidgetKeyboardEvent;
class WidgetMouseEvent;
class WidgetTouchEvent;

// -----------------------------------------------------------------------------
// Each PresShell holds a shared pointer to an AccessibleCaretEventHub; each
// AccessibleCaretEventHub holds a unique pointer to an AccessibleCaretManager.
// Thus, there's one AccessibleCaretManager per PresShell.
//
// AccessibleCaretEventHub implements a state pattern. It receives events from
// PresShell and callbacks by observers and listeners, and then relays them to
// the current concrete state which calls necessary event-handling methods in
// AccessibleCaretManager.
//
// We separate AccessibleCaretEventHub from AccessibleCaretManager to make the
// state transitions in AccessibleCaretEventHub testable. We put (nearly) all
// the operations involving PresShell, Selection, and AccessibleCaret
// manipulation in AccessibleCaretManager so that we can mock methods in
// AccessibleCaretManager in gtest. We test the correctness of the state
// transitions by giving events, callbacks, and the return values by mocked
// methods of AccessibleCaretEventHub. See TestAccessibleCaretEventHub.cpp.
//
// Besides dealing with real events, AccessibleCaretEventHub could also
// synthesize fake long-tap events and inject those events to itself on the
// platform lacks eMouseLongTap. Turn on this preference
// "layout.accessiblecaret.use_long_tap_injector" for the fake long-tap events.
//
// Please see the in-tree document for state transition diagram and more
// information.
// HTML: https://firefox-source-docs.mozilla.org/layout/AccessibleCaret.html
// Source rst: layout/docs/AccessibleCaret.rst
//
class AccessibleCaretEventHub : public nsIScrollObserver,
                                public nsSupportsWeakReference {
 public:
  explicit AccessibleCaretEventHub(PresShell* aPresShell);
  void Init();
  void Terminate();

  MOZ_CAN_RUN_SCRIPT
  nsEventStatus HandleEvent(WidgetEvent* aEvent);

  // Call this function to notify the blur event happened.
  MOZ_CAN_RUN_SCRIPT
  void NotifyBlur(bool aIsLeavingDocument);

  NS_DECL_ISUPPORTS

  MOZ_CAN_RUN_SCRIPT_BOUNDARY void DidReflow();

  // Override nsIScrollObserver methods.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void ScrollPositionChanged() override;
  MOZ_CAN_RUN_SCRIPT void AsyncPanZoomStarted() override;
  MOZ_CAN_RUN_SCRIPT void AsyncPanZoomStopped() override;

  // Base state
  class State;
  State* GetState() const;

  MOZ_CAN_RUN_SCRIPT
  void OnSelectionChange(dom::Document* aDocument, dom::Selection* aSelection,
                         int16_t aReason);

  bool ShouldDisableApz() const;

 protected:
  virtual ~AccessibleCaretEventHub() = default;

#define MOZ_DECL_STATE_CLASS_GETTER(aClassName) \
  class aClassName;                             \
  static State* aClassName();

#define MOZ_IMPL_STATE_CLASS_GETTER(aClassName)                           \
  AccessibleCaretEventHub::State* AccessibleCaretEventHub::aClassName() { \
    static class aClassName singleton;                                    \
    return &singleton;                                                    \
  }

  // Concrete state getters
  MOZ_DECL_STATE_CLASS_GETTER(NoActionState)
  MOZ_DECL_STATE_CLASS_GETTER(PressCaretState)
  MOZ_DECL_STATE_CLASS_GETTER(DragCaretState)
  MOZ_DECL_STATE_CLASS_GETTER(PressNoCaretState)
  MOZ_DECL_STATE_CLASS_GETTER(ScrollState)
  MOZ_DECL_STATE_CLASS_GETTER(LongTapState)

  void SetState(State* aState);

  MOZ_CAN_RUN_SCRIPT
  nsEventStatus HandleMouseEvent(WidgetMouseEvent* aEvent);
  MOZ_CAN_RUN_SCRIPT
  nsEventStatus HandleTouchEvent(WidgetTouchEvent* aEvent);
  MOZ_CAN_RUN_SCRIPT
  nsEventStatus HandleKeyboardEvent(WidgetKeyboardEvent* aEvent);

  virtual nsPoint GetTouchEventPosition(WidgetTouchEvent* aEvent,
                                        int32_t aIdentifier) const;
  virtual nsPoint GetMouseEventPosition(WidgetMouseEvent* aEvent) const;

  bool MoveDistanceIsLarge(const nsPoint& aPoint) const;

  void LaunchLongTapInjector();
  void CancelLongTapInjector();

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  static void FireLongTap(nsITimer* aTimer, void* aAccessibleCaretEventHub);

  // Member variables
  State* mState = NoActionState();

  // Will be set to nullptr in Terminate().
  PresShell* MOZ_NON_OWNING_REF mPresShell = nullptr;

  UniquePtr<AccessibleCaretManager> mManager;

  WeakPtr<nsDocShell> mDocShell;

  // Use this timer for injecting a long tap event when APZ is disabled. If APZ
  // is enabled, it will send long tap event to us.
  nsCOMPtr<nsITimer> mLongTapInjectorTimer;

  // Last mouse button down event or touch start event point.
  nsPoint mPressPoint{NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE};

  // For filter multitouch event
  int32_t mActiveTouchId = kInvalidTouchId;

  // Flag to indicate the class has been initialized.
  bool mInitialized = false;

  // Flag to avoid calling Reflow() callback recursively.
  bool mIsInReflowCallback = false;

  static const int32_t kMoveStartToleranceInPixel = 5;
  static const int32_t kInvalidTouchId = -1;
  static const int32_t kDefaultTouchId = 0;  // For mouse event
};

// -----------------------------------------------------------------------------
// The base class for concrete states. A concrete state should inherit from this
// class, and override the methods to handle the events or callbacks. A concrete
// state is also responsible for transforming itself to the next concrete state.
//
class AccessibleCaretEventHub::State {
 public:
  virtual const char* Name() const { return ""; }

  MOZ_CAN_RUN_SCRIPT
  virtual nsEventStatus OnPress(AccessibleCaretEventHub* aContext,
                                const nsPoint& aPoint, int32_t aTouchId,
                                EventClassID aEventClass) {
    return nsEventStatus_eIgnore;
  }

  MOZ_CAN_RUN_SCRIPT
  virtual nsEventStatus OnMove(AccessibleCaretEventHub* aContext,
                               const nsPoint& aPoint,
                               WidgetMouseEvent::Reason aReason) {
    return nsEventStatus_eIgnore;
  }

  MOZ_CAN_RUN_SCRIPT
  virtual nsEventStatus OnRelease(AccessibleCaretEventHub* aContext) {
    return nsEventStatus_eIgnore;
  }

  MOZ_CAN_RUN_SCRIPT
  virtual nsEventStatus OnLongTap(AccessibleCaretEventHub* aContext,
                                  const nsPoint& aPoint) {
    return nsEventStatus_eIgnore;
  }

  MOZ_CAN_RUN_SCRIPT
  virtual void OnScrollStart(AccessibleCaretEventHub* aContext) {}
  MOZ_CAN_RUN_SCRIPT
  virtual void OnScrollEnd(AccessibleCaretEventHub* aContext) {}
  MOZ_CAN_RUN_SCRIPT
  virtual void OnScrollPositionChanged(AccessibleCaretEventHub* aContext) {}
  MOZ_CAN_RUN_SCRIPT
  virtual void OnBlur(AccessibleCaretEventHub* aContext,
                      bool aIsLeavingDocument) {}
  MOZ_CAN_RUN_SCRIPT
  virtual void OnSelectionChanged(AccessibleCaretEventHub* aContext,
                                  dom::Document* aDoc, dom::Selection* aSel,
                                  int16_t aReason) {}
  MOZ_CAN_RUN_SCRIPT
  virtual void OnReflow(AccessibleCaretEventHub* aContext) {}
  virtual void Enter(AccessibleCaretEventHub* aContext) {}
  virtual void Leave(AccessibleCaretEventHub* aContext) {}

  explicit State() = default;
  virtual ~State() = default;
  State(const State&) = delete;
  State& operator=(const State&) = delete;
};

}  // namespace mozilla

#endif  // mozilla_AccessibleCaretEventHub_h
