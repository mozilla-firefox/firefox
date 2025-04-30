/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_EventStateManager_h_
#define mozilla_EventStateManager_h_

#include "mozilla/EventForwards.h"

#include "nsIObserver.h"
#include "nsWeakReference.h"
#include "nsCOMPtr.h"
#include "nsCOMArray.h"
#include "nsCycleCollectionParticipant.h"
#include "nsIWeakReferenceUtils.h"
#include "nsRefPtrHashtable.h"
#include "mozilla/Attributes.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/layers/APZPublicUtils.h"
#include "mozilla/dom/Record.h"
#include "Units.h"
#include "WheelHandlingHelper.h"  // for WheelDeltaAdjustmentStrategy

class nsFrameLoader;
class nsIContent;
class nsICookieJarSettings;
class nsIDocShell;
class nsIDocShellTreeItem;
class nsIFrame;
class imgIContainer;
class nsIDocumentViewer;
class nsITimer;
class nsIWidget;
class nsPresContext;

enum class FormControlType : uint8_t;

namespace mozilla {

class EditorBase;
class EnterLeaveDispatcher;
class IMEContentObserver;
class ScrollbarsForWheel;
class ScrollContainerFrame;
class TextControlElement;
class WheelTransaction;

namespace dom {
class DataTransfer;
class Document;
class Element;
class Selection;
class BrowserParent;
class RemoteDragStartData;

}  // namespace dom

class OverOutElementsWrapper final : public nsISupports {
  ~OverOutElementsWrapper() = default;

 public:
  enum class BoundaryEventType : bool { Mouse, Pointer };
  explicit OverOutElementsWrapper(BoundaryEventType aType) : mType(aType) {}

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(OverOutElementsWrapper)

  already_AddRefed<nsIWidget> GetLastOverWidget() const;

  void ContentRemoved(nsIContent& aContent);
  void WillDispatchOverAndEnterEvent(nsIContent* aOverEventTarget);
  void DidDispatchOverAndEnterEvent(
      nsIContent* aOriginalOverTargetInComposedDoc,
      nsIWidget* aOverEventTargetWidget);
  [[nodiscard]] bool IsDispatchingOverEventOn(
      nsIContent* aOverEventTarget) const {
    MOZ_ASSERT(aOverEventTarget);
    return LastOverEventTargetIsOutEventTarget() &&
           mDeepestEnterEventTarget == aOverEventTarget;
  }
  void WillDispatchOutAndOrLeaveEvent() {
    // Store the first "out" event target or the deepest "leave" event target
    // which we fire and don't refire "out" event to that element while the
    // first "out" event is still ongoing.
    mDispatchingOutOrDeepestLeaveEventTarget = mDeepestEnterEventTarget;
  }
  void DidDispatchOutAndOrLeaveEvent() {
    StoreOverEventTargetAndDeepestEnterEventTarget(nullptr);
    mDispatchingOutOrDeepestLeaveEventTarget = nullptr;
  }
  [[nodiscard]] bool IsDispatchingOutEventOnLastOverEventTarget() const {
    return mDispatchingOutOrDeepestLeaveEventTarget &&
           mDispatchingOutOrDeepestLeaveEventTarget == mDeepestEnterEventTarget;
  }
  void OverrideOverEventTarget(nsIContent* aOverEventTarget) {
    StoreOverEventTargetAndDeepestEnterEventTarget(aOverEventTarget);
    // We don't need the widget for aOverEventTarget because this method is used
    // for adjusting the "over" event target for the following "out" event
    // dispatch.
    mLastOverWidget = nullptr;
  }

  [[nodiscard]] nsIContent* GetDeepestLeaveEventTarget() const {
    // The last deepest "enter" event targe (it may be same as the last "over"
    // target) is the deepest "leave" event target.
    return mDeepestEnterEventTarget;
  }
  [[nodiscard]] nsIContent* GetOutEventTarget() const {
    // The last deepest "enter" event target is same as the "over" event target
    // unless it's never been removed from the DOM tree.  If and only if the
    // last "over" event target has not been removed from the DOM tree, it's
    // the next "out" event target.  Once the last "over" target is removed,
    // "out" event should not be fired on the target nor its ancestor.
    return LastOverEventTargetIsOutEventTarget()
               ? mDeepestEnterEventTarget.get()
               : nullptr;
  }

  /**
   * Called when EventStateManager::PreHandleEvent() receives an event which
   * should be treated as the deadline to restore the last "over" event target
   * as the next "out" event target and for avoiding to dispatch redundant
   * "over" event on the same target again when it was removed but reconnected.
   * If the last "over" event target was reconnected under the last deepest
   * "enter" event target, this restores the last "over" event target.
   * Otherwise, makes the instance forget the last "over" target because the
   * user maybe has seen that the last "over" target is completely removed from
   * the tree.
   *
   * @param aEvent      The event which the caller received.  If this is set to
   *                    nullptr or not a mouse event, this forgets the pending
   *                    last "over" event target.
   */
  void TryToRestorePendingRemovedOverTarget(const WidgetEvent* aEvent);

  /**
   * Return true if we have a pending removing last "over" event target at least
   * for the weak reference to it.  In other words, when this returns true, we
   * need to handle the pending removing "over" event target.
   */
  [[nodiscard]] bool MaybeHasPendingRemovingOverEventTarget() const {
    return mPendingRemovingOverEventTarget;
  }

 private:
  /**
   * Whether the last "over" event target is the target of "out" event if you
   * dispatch "out" event.
   */
  [[nodiscard]] bool LastOverEventTargetIsOutEventTarget() const {
    MOZ_ASSERT_IF(mDeepestEnterEventTargetIsOverEventTarget,
                  mDeepestEnterEventTarget);
    MOZ_ASSERT_IF(mDeepestEnterEventTargetIsOverEventTarget,
                  !MaybeHasPendingRemovingOverEventTarget());
    return mDeepestEnterEventTargetIsOverEventTarget;
  }

  void StoreOverEventTargetAndDeepestEnterEventTarget(
      nsIContent* aOverEventTargetAndDeepestEnterEventTarget);
  void UpdateDeepestEnterEventTarget(nsIContent* aDeepestEnterEventTarget);

  nsCOMPtr<nsIContent> GetPendingRemovingOverEventTarget() const {
    nsCOMPtr<nsIContent> pendingRemovingOverEventTarget =
        do_QueryReferent(mPendingRemovingOverEventTarget);
    return pendingRemovingOverEventTarget.forget();
  }

  // The deepest event target of the last "enter" event.  If
  // mDeepestEnterEventTargetIsOverEventTarget is true, this is the last "over"
  // event target too.  If it's set to false, this is an ancestor of the last
  // "over" event target which is not removed from the DOM tree.
  nsCOMPtr<nsIContent> mDeepestEnterEventTarget;

  // The last "over" event target which will be considered as disconnected or
  // connected later because web apps may remove the "over" event target
  // temporarily and reconnect it to the deepest "enter" target immediately.
  // In such case, we should keep treating it as the last "over" event target
  // as the next "out" event target.
  // FYI: This needs to be a weak pointer.  Otherwise, the leak window checker
  // of mochitests will detect windows in the closed tabs which ran tests
  // synthesizing mouse moves because while a <browser> is stored with a strong
  // pointer, the child window is also grabbed by the element.
  nsWeakPtr mPendingRemovingOverEventTarget;

  // While we're dispatching "over" and "enter" events, this is set to the
  // "over" event target.  If it's removed from the DOM tree, this is set to
  // nullptr.
  nsCOMPtr<nsIContent> mDispatchingOverEventTarget;

  // While we're dispatching "out" and/or "leave" events, this is set to the
  // "out" event target or the deepest leave event target.  If it's removed from
  // the DOM tree, this is set to nullptr.
  nsCOMPtr<nsIContent> mDispatchingOutOrDeepestLeaveEventTarget;

  // The widget on which we dispatched the last "over" event.  Note that
  // nsIWidget is not cycle collectable.  Therefore, for avoiding unexpected
  // memory leaks, we use nsWeakPtr to store the widget here.
  nsWeakPtr mLastOverWidget;

  const BoundaryEventType mType;

  // Once the last "over" element is removed from the tree, this is set
  // to false.  Then, mDeepestEnterEventTarget may be an ancestor of the
  // "over" element which should be the deepest target of next "leave"
  // element but shouldn't be target of "out" event.
  bool mDeepestEnterEventTargetIsOverEventTarget = false;
};

class EventStateManager : public nsSupportsWeakReference, public nsIObserver {
  friend class mozilla::EnterLeaveDispatcher;
  friend class mozilla::ScrollbarsForWheel;
  friend class mozilla::WheelTransaction;

  using ElementState = dom::ElementState;

  virtual ~EventStateManager();

 public:
  EventStateManager();

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_NSIOBSERVER

  nsresult Init();
  nsresult Shutdown();

  /* The PreHandleEvent method is called before event dispatch to either
   * the DOM or frames.  Any processing which must not be prevented or
   * cancelled should occur here.  Any processing which is intended to
   * be conditional based on either DOM or frame processing should occur in
   * PostHandleEvent.  Any centralized event processing which must occur before
   * DOM or frame event handling should occur here as well.
   *
   * aOverrideClickTarget can be used to indicate which element should be
   * used as the *up target when deciding whether to send click event.
   * This is used when releasing pointer capture. Otherwise null.
   */
  MOZ_CAN_RUN_SCRIPT
  nsresult PreHandleEvent(nsPresContext* aPresContext, WidgetEvent* aEvent,
                          nsIFrame* aTargetFrame, nsIContent* aTargetContent,
                          nsEventStatus* aStatus,
                          nsIContent* aOverrideClickTarget);

  /* The PostHandleEvent method should contain all system processing which
   * should occur conditionally based on DOM or frame processing.  It should
   * also contain any centralized event processing which must occur after
   * DOM and frame processing.
   */
  MOZ_CAN_RUN_SCRIPT
  nsresult PostHandleEvent(nsPresContext* aPresContext, WidgetEvent* aEvent,
                           nsIFrame* aTargetFrame, nsEventStatus* aStatus,
                           nsIContent* aOverrideClickTarget);

  MOZ_CAN_RUN_SCRIPT void PostHandleKeyboardEvent(
      WidgetKeyboardEvent* aKeyboardEvent, nsIFrame* aTargetFrame,
      nsEventStatus& aStatus);

  /**
   * DispatchLegacyMouseScrollEvents() dispatches eLegacyMouseLineOrPageScroll
   * event and eLegacyMousePixelScroll event for compatibility with old Gecko.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void DispatchLegacyMouseScrollEvents(
      nsIFrame* aTargetFrame, WidgetWheelEvent* aEvent, nsEventStatus* aStatus);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY void NotifyDestroyPresContext(
      nsPresContext* aPresContext);

  void ResetHoverState();

  void SetPresContext(nsPresContext* aPresContext);
  void ClearFrameRefs(nsIFrame* aFrame);

  nsIFrame* GetEventTarget();
  already_AddRefed<nsIContent> GetEventTargetContent(WidgetEvent* aEvent);

  // We manage 4 states here: ACTIVE, HOVER, DRAGOVER, URLTARGET
  static bool ManagesState(ElementState aState) {
    return aState == ElementState::ACTIVE || aState == ElementState::HOVER ||
           aState == ElementState::DRAGOVER ||
           aState == ElementState::URLTARGET;
  }

  /**
   * Notify that the given ElementState::* bit has changed for this content.
   * @param aContent Content which has changed states
   * @param aState   Corresponding state flags such as ElementState::FOCUS
   * @return  Whether the content was able to change all states. Returns false
   *                  if a resulting DOM event causes the content node passed in
   *                  to not change states. Note, the frame for the content may
   *                  change as a result of the content state change, because of
   *                  frame reconstructions that may occur, but this does not
   *                  affect the return value.
   */
  bool SetContentState(nsIContent* aContent, ElementState aState);

  nsIContent* GetActiveContent() const { return mActiveContent; }

  void NativeAnonymousContentRemoved(nsIContent* aAnonContent);
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void ContentRemoved(dom::Document* aDocument,
                                                  nsIContent* aContent);

  /**
   * Called when a native anonymous <div> element which is root element of
   * text editor will be removed.
   */
  void TextControlRootWillBeRemoved(TextControlElement& aTextControlElement);

  /**
   * Called when a native anonymous <div> element which is root element of
   * text editor is created.
   */
  void TextControlRootAdded(dom::Element& aAnonymousDivElement,
                            TextControlElement& aTextControlElement);

  bool EventStatusOK(WidgetGUIEvent* aEvent);

  /**
   * EventStateManager stores IMEContentObserver while it's observing contents.
   * Following mehtods are called by IMEContentObserver when it starts to
   * observe or stops observing the content.
   */
  void OnStartToObserveContent(IMEContentObserver* aIMEContentObserver);
  void OnStopObservingContent(IMEContentObserver* aIMEContentObserver);

  /**
   * TryToFlushPendingNotificationsToIME() suggests flushing pending
   * notifications to IME to IMEContentObserver.
   * Doesn't do anything in child processes where flushing happens
   * asynchronously.
   */
  void TryToFlushPendingNotificationsToIME();

  static bool IsKeyboardEventUserActivity(WidgetEvent* aEvent);

  /**
   * Register accesskey on the given element. When accesskey is activated then
   * the element will be notified via Element::PerformAccesskey() method.
   *
   * @param  aElement  the given element
   * @param  aKey      accesskey
   */
  void RegisterAccessKey(dom::Element* aElement, uint32_t aKey);

  /**
   * Unregister accesskey for the given element.
   *
   * @param  aElement  the given element
   * @param  aKey      accesskey
   */
  void UnregisterAccessKey(dom::Element* aElement, uint32_t aKey);

  /**
   * Get accesskey registered on the given element or 0 if there is none.
   *
   * @param  aElement  the given element (must not be null)
   * @return           registered accesskey
   */
  uint32_t GetRegisteredAccessKey(dom::Element* aContent);

  static void GetAccessKeyLabelPrefix(dom::Element* aElement,
                                      nsAString& aPrefix);

  /**
   * HandleAccessKey() looks for access keys which matches with aEvent and
   * execute when it matches with a chrome access key or some content access
   * keys.
   * If the event may match chrome access keys, this handles the access key
   * synchronously (if there are nested ESMs, their HandleAccessKey() are
   * also called recursively).
   * If the event may match content access keys and focused target is a remote
   * process, this does nothing for the content because when this is called,
   * it should already have been handled in the remote process.
   * If the event may match content access keys and focused target is not in
   * remote process but there are some remote children, this will post
   * HandleAccessKey messages to all remote children.
   *
   * @return            true if there is accesskey which aEvent and
   *                    aAccessCharCodes match with.  Otherwise, false.
   *                    I.e., when this returns true, a target is executed
   *                    or focused.
   *                    Note that even if this returns false, a target in
   *                    remote process may be executed or focused
   *                    asynchronously.
   */
  bool HandleAccessKey(WidgetKeyboardEvent* aEvent, nsPresContext* aPresContext,
                       nsTArray<uint32_t>& aAccessCharCodes) {
    return WalkESMTreeToHandleAccessKey(aEvent, aPresContext, aAccessCharCodes,
                                        nullptr, eAccessKeyProcessingNormal,
                                        true);
  }

  /**
   * CheckIfEventMatchesAccessKey() looks for access key which matches with
   * aEvent in the process but won't execute it.
   *
   * @return            true if there is accesskey which aEvent matches with
   *                    in this process.  Otherwise, false.
   */
  bool CheckIfEventMatchesAccessKey(WidgetKeyboardEvent* aEvent,
                                    nsPresContext* aPresContext);

  nsresult SetCursor(StyleCursorKind, imgIContainer*, const ImageResolution&,
                     const Maybe<gfx::IntPoint>& aHotspot, nsIWidget* aWidget,
                     bool aLockCursor);

  void StartHidingCursorWhileTyping(nsIWidget*);

  /**
   * Checks if the current mouse over element matches the given
   * Element (which has a remote frame), and if so, notifies
   * the BrowserParent of the mouse enter.
   * Called when we reconstruct the BrowserParent and need to
   * recompute state on the new object.
   */
  void RecomputeMouseEnterStateForRemoteFrame(dom::Element& aElement);

  nsPresContext* GetPresContext() { return mPresContext; }

  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(EventStateManager, nsIObserver)

  // The manager in this process that is setting the cursor. In the parent
  // process it might be null if a remote process is setting the cursor.
  static EventStateManager* sCursorSettingManager;
  static void ClearCursorSettingManager() { sCursorSettingManager = nullptr; }

  // Checks if the manager in this process has a locked cursor
  static bool CursorSettingManagerHasLockedCursor();

  static EventStateManager* GetActiveEventStateManager() { return sActiveESM; }

  // Sets aNewESM to be the active event state manager, and
  // if aContent is non-null, marks the object as active.
  static void SetActiveManager(EventStateManager* aNewESM,
                               nsIContent* aContent);

  static bool IsRemoteTarget(nsIContent* target);

  static bool IsTopLevelRemoteTarget(nsIContent* aTarget);

  // Returns the kind of APZ action the given WidgetWheelEvent will perform.
  static Maybe<layers::APZWheelAction> APZWheelActionFor(
      const WidgetWheelEvent* aEvent);

  // For some kinds of scrollings, the delta values of WidgetWheelEvent are
  // possbile to be adjusted. This function is used to detect such scrollings
  // and returns a wheel delta adjustment strategy to use, which is corresponded
  // to the kind of the scrolling.
  // It returns WheelDeltaAdjustmentStrategy::eAutoDir if the current default
  // action is auto-dir scrolling which honours the scrolling target(The
  // comments in WheelDeltaAdjustmentStrategy describes the concept in detail).
  // It returns WheelDeltaAdjustmentStrategy::eAutoDirWithRootHonour if the
  // current action is auto-dir scrolling which honours the root element in the
  // document where the scrolling target is(The comments in
  // WheelDeltaAdjustmentStrategy describes the concept in detail).
  // It returns WheelDeltaAdjustmentStrategy::eHorizontalize if the current
  // default action is horizontalized scrolling.
  // It returns WheelDeltaAdjustmentStrategy::eNone to mean no delta adjustment
  // strategy should be used if the scrolling is just a tranditional scrolling
  // whose delta values are never possible to be adjusted.
  static WheelDeltaAdjustmentStrategy GetWheelDeltaAdjustmentStrategy(
      const WidgetWheelEvent& aEvent);

  // Returns user-set multipliers for a wheel event.
  static void GetUserPrefsForWheelEvent(const WidgetWheelEvent* aEvent,
                                        double* aOutMultiplierX,
                                        double* aOutMultiplierY);

  // Holds the point in screen coords that a mouse event was dispatched to,
  // before we went into pointer lock mode. This is constantly updated while
  // the pointer is not locked, but we don't update it while the pointer is
  // locked. This is used by dom::Event::GetScreenCoords() to make mouse
  // events' screen coord appear frozen at the last mouse position while
  // the pointer is locked.
  static CSSIntPoint sLastScreenPoint;

  // Holds the point in client coords of the last mouse event. Used by
  // dom::Event::GetClientCoords() to make mouse events' client coords appear
  // frozen at the last mouse position while the pointer is locked.
  static CSSIntPoint sLastClientPoint;

  /**
   * If the absolute values of mMultiplierX and/or mMultiplierY are equal or
   * larger than this value, the computed scroll amount isn't rounded down to
   * the page width or height.
   */
  static constexpr double MIN_MULTIPLIER_VALUE_ALLOWING_OVER_ONE_PAGE_SCROLL =
      1000.0;

  /**
   * HandleMiddleClickPaste() handles middle mouse button event as pasting
   * clipboard text.  Note that if aEditorBase is nullptr, this only
   * dispatches ePaste event because it's necessary for some web apps which
   * want to implement their own editor and supports middle click paste.
   *
   * @param aPresShell              The PresShell for the ESM.  This lifetime
   *                                should be guaranteed by the caller.
   * @param aMouseEvent             The ePointerClick event which caused the
   *                                paste.
   * @param aStatus                 The event status of aMouseEvent.
   * @param aEditorBase             EditorBase which may be pasted the
   *                                clipboard text by the middle click.
   *                                If there is no editor for aMouseEvent,
   *                                set nullptr.
   */
  MOZ_CAN_RUN_SCRIPT
  nsresult HandleMiddleClickPaste(PresShell* aPresShell,
                                  WidgetMouseEvent* aMouseEvent,
                                  nsEventStatus* aStatus,
                                  EditorBase* aEditorBase);

  static void ConsumeInteractionData(
      dom::Record<nsString, dom::InteractionData>& aInteractions);

  // Stop tracking a possible drag. If aClearInChildProcesses is true, send
  // a notification to any child processes that are in the drag service that
  // tried to start a drag.
  void StopTrackingDragGesture(bool aClearInChildProcesses);

  /**
   * Return the last "mouseover" (or next "mouseout"), the last deepest
   * "mouseenter" (or next deepest "mouseleave") targets.
   */
  const OverOutElementsWrapper* GetExtantMouseBoundaryEventTarget() const {
    return mMouseEnterLeaveHelper;
  }

  nsIContent* GetTrackingDragGestureContent() const {
    return mGestureDownContent;
  }

  // Update the tracked gesture content to the parent of its frame when it's
  // removed, so that the gesture can be continued.
  void NotifyContentWillBeRemovedForGesture(nsIContent& aContent);

  bool IsTrackingDragGesture() const { return mGestureDownContent != nullptr; }

 protected:
  /*
   * If aTargetFrame's widget has a cached cursor value, resets the cursor
   * such that the next call to SetCursor on the widget will force an update
   * of the native cursor. For use in getting puppet widget to update its
   * cursor between mouse exit / enter transitions. This call basically wraps
   * nsIWidget ClearCachedCursor.
   */
  void ClearCachedWidgetCursor(nsIFrame* aTargetFrame);

  void UpdateCursor(nsPresContext*, WidgetMouseEvent*, nsIFrame* aTargetFrame,
                    nsEventStatus* aStatus);
  /**
   * Turn a GUI mouse/pointer event into a mouse/pointer event targeted at the
   * specified content.
   *
   * @return widget which is the nearest widget from the event target frame.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT already_AddRefed<nsIWidget>
  DispatchMouseOrPointerBoundaryEvent(WidgetMouseEvent* aMouseEvent,
                                      EventMessage aMessage,
                                      nsIContent* aTargetContent,
                                      nsIContent* aRelatedContent);
  /**
   * Synthesize DOM pointerover and pointerout events
   */
  void GeneratePointerEnterExit(EventMessage aMessage,
                                WidgetMouseEvent* aEvent);
  /**
   * Synthesize DOM and frame mouseover and mouseout events from this
   * MOUSE_MOVE or MOUSE_EXIT event.
   */
  void GenerateMouseEnterExit(WidgetMouseEvent* aMouseEvent);
  /**
   * Tell this ESM and ESMs in parent documents that the mouse is
   * over some content in this document.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void NotifyMouseOver(
      WidgetMouseEvent* aMouseEvent, nsIContent* aContent);
  /**
   * Tell this ESM and ESMs in affected child documents that the mouse
   * has exited this document's currently hovered content.
   * TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
   *
   * @param aMouseEvent the event that triggered the mouseout
   * @param aMovingInto the content node we've moved into.  This is used to set
   *        the relatedTarget for mouseout events.  Also, if it's non-null
   *        NotifyMouseOut will NOT change the current hover content to null;
   *        in that case the caller is responsible for updating hover state.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void NotifyMouseOut(WidgetMouseEvent* aMouseEvent,
                                                  nsIContent* aMovingInto);
  MOZ_CAN_RUN_SCRIPT void GenerateDragDropEnterExit(
      nsPresContext* aPresContext, WidgetDragEvent* aDragEvent);

  /**
   * Return mMouseEnterLeaveHelper or relevant mPointersEnterLeaveHelper
   * elements wrapper. If mPointersEnterLeaveHelper does not contain wrapper for
   * pointerId it create new one
   */
  OverOutElementsWrapper* GetWrapperByEventID(WidgetMouseEvent* aMouseEvent);

  /**
   * Fire the dragenter and dragexit/dragleave events when the mouse moves to a
   * new target.
   *
   * @param aRelatedTarget relatedTarget to set for the event
   * @param aTargetContent target to set for the event
   * @param aTargetFrame target frame for the event
   */
  MOZ_CAN_RUN_SCRIPT void FireDragEnterOrExit(nsPresContext* aPresContext,
                                              WidgetDragEvent* aDragEvent,
                                              EventMessage aMessage,
                                              nsIContent* aRelatedTarget,
                                              nsIContent* aTargetContent,
                                              AutoWeakFrame& aTargetFrame);
  /**
   * Update the initial drag session data transfer with any changes that occur
   * on cloned data transfer objects used for events.
   */
  void UpdateDragDataTransfer(WidgetDragEvent* dragEvent);

  /**
   * InitAndDispatchClickEvent() dispatches a click event.
   *
   * @param aMouseUpEvent           eMouseUp event which causes the click event.
   *                                EventCausesClickEvents() must return true
   *                                if this event is set to it.
   * @param aStatus                 Returns the result of click event.
   *                                If the status indicates consumed, the
   *                                value won't be overwritten with
   *                                nsEventStatus_eIgnore.
   * @param aMessage                Should be ePointerClick, eMouseDoubleClick
   *                                or ePointerAuxClick.
   * @param aPresShell              The PresShell.
   * @param aMouseUpContent         The event target of aMouseUpEvent.
   * @param aCurrentTarget          Current target of the caller.
   * @param aNoContentDispatch      true if the event shouldn't be exposed to
   *                                web contents (although will be fired on
   *                                document and window).
   * @param aOverrideClickTarget    Preferred click event target.  If this is
   *                                not nullptr, aMouseUpContent and
   *                                aCurrentTarget are ignored.
   */
  MOZ_CAN_RUN_SCRIPT
  static nsresult InitAndDispatchClickEvent(
      WidgetMouseEvent* aMouseUpEvent, nsEventStatus* aStatus,
      EventMessage aMessage, PresShell* aPresShell, nsIContent* aMouseUpContent,
      AutoWeakFrame aCurrentTarget, bool aNoContentDispatch,
      nsIContent* aOverrideClickTarget);

  nsresult SetClickCount(WidgetMouseEvent* aEvent, nsEventStatus* aStatus,
                         nsIContent* aOverrideClickTarget = nullptr);

  /**
   * EventCausesClickEvents() returns true when aMouseEvent is an eMouseUp
   * event and it should cause ePointerClick, eMouseDoubleClick and/or
   * ePointerAuxClick events.  Note that this method assumes that
   * aMouseEvent.mClickCount has already been initialized with SetClickCount().
   */
  static bool EventCausesClickEvents(const WidgetMouseEvent& aMouseEvent);

  /**
   * PostHandleMouseUp() handles default actions of eMouseUp event.
   *
   * @param aMouseUpEvent           eMouseUp event which causes the click event.
   *                                EventCausesClickEvents() must return true
   *                                if this event is set to it.
   * @param aStatus                 Returns the result of event status.
   *                                If one of dispatching event is consumed or
   *                                this does something as default action,
   *                                returns nsEventStatus_eConsumeNoDefault.
   * @param aOverrideClickTarget    Preferred click event target.  If nullptr,
   *                                aMouseUpEvent target and current target
   *                                are used.
   */
  MOZ_CAN_RUN_SCRIPT
  nsresult PostHandleMouseUp(WidgetMouseEvent* aMouseUpEvent,
                             nsEventStatus* aStatus,
                             nsIContent* aOverrideClickTarget);

  /**
   * DispatchClickEvents() dispatches ePointerClick, eMouseDoubleClick and
   * ePointerAuxClick events for aMouseUpEvent.  aMouseUpEvent should cause
   * click event.
   *
   * @param aPresShell              The PresShell.
   * @param aMouseUpEvent           eMouseUp event which causes the click event.
   *                                EventCausesClickEvents() must return true
   *                                if this event is set to it.
   * @param aStatus                 Returns the result of event status.
   *                                If one of dispatching click event is
   *                                consumed, returns
   *                                nsEventStatus_eConsumeNoDefault.
   * @param aMouseUpContent         The event target of aMouseUpEvent.
   * @param aOverrideClickTarget    Preferred click event target.  If this is
   *                                not nullptr, aMouseUpContent and
   *                                current target frame of the ESM are ignored.
   */
  MOZ_CAN_RUN_SCRIPT
  nsresult DispatchClickEvents(PresShell* aPresShell,
                               WidgetMouseEvent* aMouseUpEvent,
                               nsEventStatus* aStatus,
                               nsIContent* aMouseUpContent,
                               nsIContent* aOverrideClickTarget);

  void EnsureDocument(nsPresContext* aPresContext);
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  void FlushLayout(nsPresContext* aPresContext);

  /**
   * The phases of WalkESMTreeToHandleAccessKey processing. See below.
   */
  enum ProcessingAccessKeyState {
    eAccessKeyProcessingNormal = 0,
    eAccessKeyProcessingUp,
    eAccessKeyProcessingDown
  };

  /**
   * Walk EMS to look for access key and execute found access key when aExecute
   * is true.
   * If there is registered element for the accesskey given by the key event
   * and modifier mask then call element.PerformAccesskey(), otherwise call
   * WalkESMTreeToHandleAccessKey() recursively, on descendant docshells first,
   * then on the ancestor (with |aBubbledFrom| set to the docshell associated
   * with |this|), until something matches.
   *
   * @param aEvent the keyboard event triggering the acccess key
   * @param aPresContext the presentation context
   * @param aAccessCharCodes list of charcode candidates
   * @param aBubbledFrom is used by an ancestor to avoid calling
   *        WalkESMTreeToHandleAccessKey() on the child the call originally
   *        came from, i.e. this is the child that recursively called us in
   *        its Up phase. The initial caller passes |nullptr| here. This is to
   *        avoid an infinite loop.
   * @param aAccessKeyState Normal, Down or Up processing phase (see enums
   *        above). The initial event receiver uses 'normal', then 'down' when
   *        processing children and Up when recursively calling its ancestor.
   * @param aExecute is true, execute an accesskey if it's found.  Otherwise,
   *        found accesskey won't be executed.
   *
   * @return            true if there is a target which aEvent and
   *                    aAccessCharCodes match with in this process.
   *                    Otherwise, false.  I.e., when this returns true and
   *                    aExecute is true, a target is executed or focused.
   *                    Note that even if this returns false, a target in
   *                    remote process may be executed or focused
   *                    asynchronously.
   */
  bool WalkESMTreeToHandleAccessKey(WidgetKeyboardEvent* aEvent,
                                    nsPresContext* aPresContext,
                                    nsTArray<uint32_t>& aAccessCharCodes,
                                    nsIDocShellTreeItem* aBubbledFrom,
                                    ProcessingAccessKeyState aAccessKeyState,
                                    bool aExecute);

  /**
   * Look for access key and execute found access key if aExecute is true in
   * the instance.
   *
   * @return            true if there is a target which matches with
   *                    aAccessCharCodes and aIsTrustedEvent.  Otherwise,
   *                    false.  I.e., when this returns true and aExecute
   *                    is true, a target is executed or focused.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY bool LookForAccessKeyAndExecute(
      nsTArray<uint32_t>& aAccessCharCodes, bool aIsTrustedEvent,
      bool aIsRepeat, bool aExecute);

  //---------------------------------------------
  // DocShell Focus Traversal Methods
  //---------------------------------------------

  dom::Element* GetFocusedElement();
  bool IsShellVisible(nsIDocShell* aShell);

  // These functions are for mousewheel and pixel scrolling

  class WheelPrefs {
   public:
    static WheelPrefs* GetInstance();
    static void Shutdown();

    /**
     * ApplyUserPrefsToDelta() overrides the wheel event's delta values with
     * user prefs.
     */
    void ApplyUserPrefsToDelta(WidgetWheelEvent* aEvent);

    /**
     * Returns whether or not ApplyUserPrefsToDelta() would change the delta
     * values of an event.
     */
    void GetUserPrefsForEvent(const WidgetWheelEvent* aEvent,
                              double* aOutMultiplierX, double* aOutMultiplierY);

    /**
     * If ApplyUserPrefsToDelta() changed the delta values with customized
     * prefs, the overflowDelta values would be inflated.
     * CancelApplyingUserPrefsFromOverflowDelta() cancels the inflation.
     */
    void CancelApplyingUserPrefsFromOverflowDelta(WidgetWheelEvent* aEvent);

    /**
     * Computes the default action for the aEvent with the prefs.
     */
    enum Action : uint8_t {
      ACTION_NONE = 0,
      ACTION_SCROLL,
      ACTION_HISTORY,
      ACTION_ZOOM,
      // Horizontalized scrolling means treating vertical wheel scrolling as
      // horizontal scrolling during the process of its default action and
      // plugins handling scrolling. Note that delta values as the event object
      // in a DOM event listener won't be affected, and will be still the
      // original values. For more details, refer to
      // mozilla::WheelDeltaAdjustmentStrategy::eHorizontalize
      ACTION_HORIZONTALIZED_SCROLL,
      ACTION_PINCH_ZOOM,
      ACTION_LAST = ACTION_PINCH_ZOOM,
      // Following actions are used only by internal processing.  So, cannot
      // specified by prefs.
      ACTION_SEND_TO_PLUGIN,
    };
    Action ComputeActionFor(const WidgetWheelEvent* aEvent);

    /**
     * NeedToComputeLineOrPageDelta() returns if the aEvent needs to be
     * computed the lineOrPageDelta values.
     */
    bool NeedToComputeLineOrPageDelta(const WidgetWheelEvent* aEvent);

    /**
     * IsOverOnePageScrollAllowed*() checks whether wheel scroll amount should
     * be rounded down to the page width/height (false) or not (true).
     */
    bool IsOverOnePageScrollAllowedX(const WidgetWheelEvent* aEvent);
    bool IsOverOnePageScrollAllowedY(const WidgetWheelEvent* aEvent);

   private:
    WheelPrefs();
    ~WheelPrefs();

    static void OnPrefChanged(const char* aPrefName, void* aClosure);

    enum Index {
      INDEX_DEFAULT = 0,
      INDEX_ALT,
      INDEX_CONTROL,
      INDEX_META,
      INDEX_SHIFT,
      COUNT_OF_MULTIPLIERS
    };

    /**
     * GetIndexFor() returns the index of the members which should be used for
     * the aEvent.  When only one modifier key of MODIFIER_ALT,
     * MODIFIER_CONTROL, MODIFIER_META or MODIFIER_SHIFT is pressed, returns the
     * index for the modifier.  Otherwise, this return the default index which
     * is used at either no modifier key is pressed or two or modifier keys are
     * pressed.
     */
    Index GetIndexFor(const WidgetWheelEvent* aEvent);

    /**
     * GetPrefNameBase() returns the base pref name for aEvent.
     * It's decided by GetModifierForPref() which modifier should be used for
     * the aEvent.
     *
     * @param aBasePrefName The result, must be "mousewheel.with_*." or
     *                      "mousewheel.default.".
     */
    void GetBasePrefName(Index aIndex, nsACString& aBasePrefName);

    void Init(Index aIndex);

    void Reset();

    /**
     * Retrieve multiplier for aEvent->mDeltaX and aEvent->mDeltaY.
     *
     * Note that if the default action is ACTION_HORIZONTALIZED_SCROLL and the
     * delta values have been adjusted by WheelDeltaHorizontalizer() before this
     * function is called, this function will swap the X and Y multipliers. By
     * doing this, multipliers will still apply to the delta values they
     * originally corresponded to.
     *
     * @param aEvent    The event which is being handled.
     * @param aIndex    The index of mMultiplierX and mMultiplierY.
     *                  Should be result of GetIndexFor(aEvent).
     * @param aMultiplierForDeltaX      Will be set to multiplier for
     *                                  aEvent->mDeltaX.
     * @param aMultiplierForDeltaY      Will be set to multiplier for
     *                                  aEvent->mDeltaY.
     */
    void GetMultiplierForDeltaXAndY(const WidgetWheelEvent* aEvent,
                                    Index aIndex, double* aMultiplierForDeltaX,
                                    double* aMultiplierForDeltaY);

    bool mInit[COUNT_OF_MULTIPLIERS];
    double mMultiplierX[COUNT_OF_MULTIPLIERS];
    double mMultiplierY[COUNT_OF_MULTIPLIERS];
    double mMultiplierZ[COUNT_OF_MULTIPLIERS];
    Action mActions[COUNT_OF_MULTIPLIERS];
    /**
     * action values overridden by .override_x pref.
     * If an .override_x value is -1, same as the
     * corresponding mActions value.
     */
    Action mOverriddenActionsX[COUNT_OF_MULTIPLIERS];

    static WheelPrefs* sInstance;
  };

  /**
   * DeltaDirection is used for specifying whether the called method should
   * handle vertical delta or horizontal delta.
   * This is clearer than using bool.
   */
  enum DeltaDirection { DELTA_DIRECTION_X = 0, DELTA_DIRECTION_Y };

  struct MOZ_STACK_CLASS EventState {
    bool mDefaultPrevented;
    bool mDefaultPreventedByContent;

    EventState()
        : mDefaultPrevented(false), mDefaultPreventedByContent(false) {}
  };

  /**
   * SendLineScrollEvent() dispatches a DOMMouseScroll event for the
   * WidgetWheelEvent.  This method shouldn't be called for non-trusted
   * wheel event because it's not necessary for compatiblity.
   *
   * @param aTargetFrame        The event target of wheel event.
   * @param aEvent              The original Wheel event.
   * @param aState              The event which should be set to the dispatching
   *                            event.  This also returns the dispatched event
   *                            state.
   * @param aDelta              The delta value of the event.
   * @param aDeltaDirection     The X/Y direction of dispatching event.
   */
  MOZ_CAN_RUN_SCRIPT void SendLineScrollEvent(nsIFrame* aTargetFrame,
                                              WidgetWheelEvent* aEvent,
                                              EventState& aState,
                                              int32_t aDelta,
                                              DeltaDirection aDeltaDirection);

  /**
   * SendPixelScrollEvent() dispatches a MozMousePixelScroll event for the
   * WidgetWheelEvent.  This method shouldn't be called for non-trusted
   * wheel event because it's not necessary for compatiblity.
   *
   * @param aTargetFrame        The event target of wheel event.
   * @param aEvent              The original Wheel event.
   * @param aState              The event which should be set to the dispatching
   *                            event.  This also returns the dispatched event
   *                            state.
   * @param aPixelDelta         The delta value of the event.
   * @param aDeltaDirection     The X/Y direction of dispatching event.
   */
  MOZ_CAN_RUN_SCRIPT void SendPixelScrollEvent(nsIFrame* aTargetFrame,
                                               WidgetWheelEvent* aEvent,
                                               EventState& aState,
                                               int32_t aPixelDelta,
                                               DeltaDirection aDeltaDirection);

  /**
   * ComputeScrollTargetAndMayAdjustWheelEvent() returns the scrollable frame
   * which should be scrolled.
   *
   * @param aTargetFrame        The event target of the wheel event.
   * @param aEvent              The handling mouse wheel event.
   * @param aOptions            The options for finding the scroll target.
   *                            Callers should use COMPUTE_*.
   * @return                    The scrollable frame which should be scrolled.
   */
  // These flags are used in ComputeScrollTargetAndMayAdjustWheelEvent().
  // Callers should use COMPUTE_*.
  enum {
    PREFER_MOUSE_WHEEL_TRANSACTION = 0x00000001,
    PREFER_ACTUAL_SCROLLABLE_TARGET_ALONG_X_AXIS = 0x00000002,
    PREFER_ACTUAL_SCROLLABLE_TARGET_ALONG_Y_AXIS = 0x00000004,
    START_FROM_PARENT = 0x00000008,
    INCLUDE_PLUGIN_AS_TARGET = 0x00000010,
    // Indicates the wheel scroll event being computed is an auto-dir scroll, so
    // its delta may be adjusted after being computed.
    MAY_BE_ADJUSTED_BY_AUTO_DIR = 0x00000020,
  };
  enum ComputeScrollTargetOptions {
    // At computing scroll target for legacy mouse events, we should return
    // first scrollable element even when it's not scrollable to the direction.
    COMPUTE_LEGACY_MOUSE_SCROLL_EVENT_TARGET = 0,
    // Default action prefers the scrolled element immediately before if it's
    // still under the mouse cursor.  Otherwise, it prefers the nearest
    // scrollable ancestor which will be scrolled actually.
    COMPUTE_DEFAULT_ACTION_TARGET =
        (PREFER_MOUSE_WHEEL_TRANSACTION |
         PREFER_ACTUAL_SCROLLABLE_TARGET_ALONG_X_AXIS |
         PREFER_ACTUAL_SCROLLABLE_TARGET_ALONG_Y_AXIS),
    COMPUTE_DEFAULT_ACTION_TARGET_WITH_AUTO_DIR =
        (COMPUTE_DEFAULT_ACTION_TARGET | MAY_BE_ADJUSTED_BY_AUTO_DIR),
    // Look for the nearest scrollable ancestor which can be scrollable with
    // aEvent.
    COMPUTE_SCROLLABLE_ANCESTOR_ALONG_X_AXIS =
        (PREFER_ACTUAL_SCROLLABLE_TARGET_ALONG_X_AXIS | START_FROM_PARENT),
    COMPUTE_SCROLLABLE_ANCESTOR_ALONG_Y_AXIS =
        (PREFER_ACTUAL_SCROLLABLE_TARGET_ALONG_Y_AXIS | START_FROM_PARENT),
    COMPUTE_SCROLLABLE_ANCESTOR_ALONG_X_AXIS_WITH_AUTO_DIR =
        (COMPUTE_SCROLLABLE_ANCESTOR_ALONG_X_AXIS |
         MAY_BE_ADJUSTED_BY_AUTO_DIR),
    COMPUTE_SCROLLABLE_ANCESTOR_ALONG_Y_AXIS_WITH_AUTO_DIR =
        (COMPUTE_SCROLLABLE_ANCESTOR_ALONG_Y_AXIS |
         MAY_BE_ADJUSTED_BY_AUTO_DIR),
  };

  // Compute the scroll target.
  // The delta values in the wheel event may be changed if the event is for
  // auto-dir scrolling. For information on auto-dir,
  // @see mozilla::WheelDeltaAdjustmentStrategy
  ScrollContainerFrame* ComputeScrollTargetAndMayAdjustWheelEvent(
      nsIFrame* aTargetFrame, WidgetWheelEvent* aEvent,
      ComputeScrollTargetOptions aOptions);

  ScrollContainerFrame* ComputeScrollTargetAndMayAdjustWheelEvent(
      nsIFrame* aTargetFrame, double aDirectionX, double aDirectionY,
      WidgetWheelEvent* aEvent, ComputeScrollTargetOptions aOptions);

  ScrollContainerFrame* ComputeScrollTarget(
      nsIFrame* aTargetFrame, WidgetWheelEvent* aEvent,
      ComputeScrollTargetOptions aOptions) {
    MOZ_ASSERT(!(aOptions & MAY_BE_ADJUSTED_BY_AUTO_DIR),
               "aEvent may be modified by auto-dir");
    return ComputeScrollTargetAndMayAdjustWheelEvent(aTargetFrame, aEvent,
                                                     aOptions);
  }

  ScrollContainerFrame* ComputeScrollTarget(
      nsIFrame* aTargetFrame, double aDirectionX, double aDirectionY,
      WidgetWheelEvent* aEvent, ComputeScrollTargetOptions aOptions) {
    MOZ_ASSERT(!(aOptions & MAY_BE_ADJUSTED_BY_AUTO_DIR),
               "aEvent may be modified by auto-dir");
    return ComputeScrollTargetAndMayAdjustWheelEvent(
        aTargetFrame, aDirectionX, aDirectionY, aEvent, aOptions);
  }

  /**
   * GetScrollAmount() returns the scroll amount in app uints of one line or
   * one page.  If the wheel event scrolls a page, returns the page width and
   * height.  Otherwise, returns line height for both its width and height.
   *
   * @param aScrollContainerFrame A frame which will be scrolled by the event.
   *                              The result of
   *                              ComputeScrollTargetAndMayAdjustWheelEvent() is
   *                              expected for this value.
   *                              This can be nullptr if there is no scrollable
   *                              frame.  Then, this method uses root frame's
   *                              line height or visible area's width and
   *                              height.
   */
  nsSize GetScrollAmount(nsPresContext* aPresContext, WidgetWheelEvent* aEvent,
                         ScrollContainerFrame* aScrollContainerFrame);

  /**
   * DoScrollText() scrolls the scroll container frame for aEvent.
   */
  void DoScrollText(ScrollContainerFrame* aScrollContainerFrame,
                    WidgetWheelEvent* aEvent);

  MOZ_CAN_RUN_SCRIPT
  void DoScrollHistory(int32_t direction);
  void DoScrollZoom(nsIFrame* aTargetFrame, int32_t adjustment);
  void ChangeZoom(bool aIncrease);

  /**
   * DeltaAccumulator class manages delta values for dispatching DOMMouseScroll
   * event.  If wheel events are caused by pixel scroll only devices or
   * the delta values are customized by prefs, this class stores the delta
   * values and set lineOrPageDelta values.
   */
  class DeltaAccumulator {
   public:
    static DeltaAccumulator* GetInstance() {
      if (!sInstance) {
        sInstance = new DeltaAccumulator;
      }
      return sInstance;
    }

    static void Shutdown() {
      delete sInstance;
      sInstance = nullptr;
    }

    bool IsInTransaction() { return mHandlingDeltaMode != UINT32_MAX; }

    /**
     * InitLineOrPageDelta() stores pixel delta values of WidgetWheelEvents
     * which are caused if it's needed.  And if the accumulated delta becomes a
     * line height, sets lineOrPageDeltaX and lineOrPageDeltaY automatically.
     */
    void InitLineOrPageDelta(nsIFrame* aTargetFrame, EventStateManager* aESM,
                             WidgetWheelEvent* aEvent);

    /**
     * Reset() resets all members.
     */
    void Reset();

    /**
     * ComputeScrollAmountForDefaultAction() computes the default action's
     * scroll amount in device pixels with mPendingScrollAmount*.
     */
    nsIntPoint ComputeScrollAmountForDefaultAction(
        WidgetWheelEvent* aEvent, const nsIntSize& aScrollAmountInDevPixels);

   private:
    DeltaAccumulator()
        : mX(0.0),
          mY(0.0),
          mPendingScrollAmountX(0.0),
          mPendingScrollAmountY(0.0),
          mHandlingDeltaMode(UINT32_MAX),
          mIsNoLineOrPageDeltaDevice(false) {}

    double mX;
    double mY;

    // When default action of a wheel event is scroll but some delta values
    // are ignored because the computed amount values are not integer, the
    // fractional values are saved by these members.
    double mPendingScrollAmountX;
    double mPendingScrollAmountY;

    TimeStamp mLastTime;

    uint32_t mHandlingDeltaMode;
    bool mIsNoLineOrPageDeltaDevice;

    static DeltaAccumulator* sInstance;
  };

  // end mousewheel functions

  /*
   * When a touch gesture is about to start, this function determines what
   * kind of gesture interaction we will want to use, based on what is
   * underneath the initial touch point.
   * Currently it decides between panning (finger scrolling) or dragging
   * the target element, as well as the orientation to trigger panning and
   * display visual boundary feedback. The decision is stored back in aEvent.
   */
  void DecideGestureEvent(WidgetGestureNotifyEvent* aEvent,
                          nsIFrame* targetFrame);

  // routines for the d&d gesture tracking state machine
  void BeginTrackingDragGesture(nsPresContext* aPresContext,
                                WidgetMouseEvent* aDownEvent,
                                nsIFrame* aDownFrame);

  void SetGestureDownPoint(WidgetGUIEvent* aEvent);

  LayoutDeviceIntPoint GetEventRefPoint(WidgetEvent* aEvent) const;

  friend class mozilla::dom::BrowserParent;
  void BeginTrackingRemoteDragGesture(nsIContent* aContent,
                                      dom::RemoteDragStartData* aDragStartData);

  MOZ_CAN_RUN_SCRIPT
  void GenerateDragGesture(nsPresContext* aPresContext,
                           WidgetInputEvent* aEvent);

  /**
   * When starting a dnd session, UA must fire a pointercancel event and stop
   * firing the subsequent pointer events.
   */
  MOZ_CAN_RUN_SCRIPT
  void MaybeFirePointerCancel(WidgetInputEvent* aEvent);

  /**
   * Determine which node the drag should be targeted at.
   * This is either the node clicked when there is a selection, or, for HTML,
   * the element with a draggable property set to true.
   *
   * aSelectionTarget - target to check for selection
   * aDataTransfer - data transfer object that will contain the data to drag
   * aAllowEmptyDataTransfer - [out] set to true, if dnd operation can be
   *                           started even if DataTransfer is empty
   * aSelection - [out] set to the selection to be dragged
   * aTargetNode - [out] the draggable node, or null if there isn't one
   * aPrincipal - [out] set to the triggering principal of the drag, or null
   *                    if it's from browser chrome or OS
   * aCookieJarSettings - [out] set to the cookieJarSettings of the drag, or
   *                            null if it's from browser chrome or OS.
   */
  void DetermineDragTargetAndDefaultData(
      nsPIDOMWindowOuter* aWindow, nsIContent* aSelectionTarget,
      dom::DataTransfer* aDataTransfer, bool* aAllowEmptyDataTransfer,
      dom::Selection** aSelection,
      dom::RemoteDragStartData** aRemoteDragStartData, nsIContent** aTargetNode,
      nsIPrincipal** aPrincipal, nsIContentSecurityPolicy** aCsp,
      nsICookieJarSettings** aCookieJarSettings);

  /*
   * Perform the default handling for the dragstart event and set up a
   * drag for aDataTransfer if it contains any data. Returns true if a drag has
   * started.
   *
   * aDragEvent - the dragstart event
   * aDataTransfer - the data transfer that holds the data to be dragged
   * aAllowEmptyDataTransfer - if true, dnd can be started even if there is no
   *                           data to drag
   * aDragTarget - the target of the drag
   * aSelection - the selection to be dragged
   * aData - information pertaining to a drag started in a child process
   * aPrincipal - the triggering principal of the drag, or null if it's from
   *              browser chrome or OS
   * aCookieJarSettings - the cookieJarSettings of the drag. or null if it's
   *                      from browser chrome or OS.
   */
  MOZ_CAN_RUN_SCRIPT
  bool DoDefaultDragStart(
      nsPresContext* aPresContext, WidgetDragEvent* aDragEvent,
      dom::DataTransfer* aDataTransfer, bool aAllowEmptyDataTransfer,
      nsIContent* aDragTarget, dom::Selection* aSelection,
      dom::RemoteDragStartData* aDragStartData, nsIPrincipal* aPrincipal,
      nsIContentSecurityPolicy* aCsp, nsICookieJarSettings* aCookieJarSettings);

  /**
   * Set the fields of aEvent to reflect the mouse position and modifier keys
   * that were set when the user first pressed the mouse button (stored by
   * BeginTrackingDragGesture). aEvent->mWidget must be
   * mCurrentTarget->GetNearestWidget().
   */
  void FillInEventFromGestureDown(WidgetMouseEvent* aEvent);

  MOZ_CAN_RUN_SCRIPT
  nsresult DoContentCommandEvent(WidgetContentCommandEvent* aEvent);
  MOZ_CAN_RUN_SCRIPT
  nsresult DoContentCommandInsertTextEvent(WidgetContentCommandEvent* aEvent);
  MOZ_CAN_RUN_SCRIPT
  nsresult DoContentCommandReplaceTextEvent(WidgetContentCommandEvent* aEvent);
  nsresult DoContentCommandScrollEvent(WidgetContentCommandEvent* aEvent);

  dom::BrowserParent* GetCrossProcessTarget();
  bool IsTargetCrossProcess(WidgetGUIEvent* aEvent);

  /**
   * DispatchCrossProcessEvent() try to post aEvent to target remote process.
   * If you need to check if the event is posted to a remote process, you
   * can use aEvent->HasBeenPostedToRemoteProcess().
   */
  void DispatchCrossProcessEvent(WidgetEvent* aEvent,
                                 dom::BrowserParent* aRemoteTarget,
                                 nsEventStatus* aStatus);
  /**
   * HandleCrossProcessEvent() may post aEvent to target remote processes.
   * When it succeeded to post the event to at least one remote process,
   * returns true.  Otherwise, including the case not tried to dispatch to
   * post the event, returns false.
   * If you need to check if the event is posted to at least one remote
   * process, you can use aEvent->HasBeenPostedToRemoteProcess().
   */
  bool HandleCrossProcessEvent(WidgetEvent* aEvent, nsEventStatus* aStatus);

  void ReleaseCurrentIMEContentObserver();

  MOZ_CAN_RUN_SCRIPT void HandleQueryContentEvent(
      WidgetQueryContentEvent* aEvent);

 private:
  // Removes a node from the :hover / :active chain if needed, notifying if the
  // node is not a NAC subtree.
  //
  // Only meant to be called from ContentRemoved and
  // NativeAnonymousContentRemoved.
  void RemoveNodeFromChainIfNeeded(ElementState aState,
                                   nsIContent* aContentRemoved, bool aNotify);

  bool IsEventOutsideDragThreshold(WidgetInputEvent* aEvent) const;

  static inline void DoStateChange(dom::Element* aElement, ElementState aState,
                                   bool aAddState);
  static inline void DoStateChange(nsIContent* aContent, ElementState aState,
                                   bool aAddState);
  static void UpdateAncestorState(nsIContent* aStartNode,
                                  nsIContent* aStopBefore, ElementState aState,
                                  bool aAddState);

  /**
   * Update the attribute mLastRefPoint of the mouse event. It should be
   *     the center of the window while the pointer is locked.
   *     the same value as mRefPoint while there is no known last ref point.
   *     the same value as the last known mRefPoint.
   */
  static void UpdateLastRefPointOfMouseEvent(WidgetMouseEvent* aMouseEvent);

  static void ResetPointerToWindowCenterWhilePointerLocked(
      WidgetMouseEvent* aMouseEvent);

  // Update the last known ref point to the current event's mRefPoint.
  static void UpdateLastPointerPosition(WidgetMouseEvent* aMouseEvent);

  /**
   * Notify target when user has been interaction with some speicific user
   * gestures which are eKeyUp, eMouseUp, eTouchEnd.
   */
  void NotifyTargetUserActivation(WidgetEvent* aEvent,
                                  nsIContent* aTargetContent);

  /**
   * https://html.spec.whatwg.org/multipage/popover.html#light-dismiss-open-popovers.
   */
  MOZ_CAN_RUN_SCRIPT void LightDismissOpenPopovers(WidgetEvent* aEvent,
                                                   nsIContent* aTargetContent);

  already_AddRefed<EventStateManager> ESMFromContentOrThis(
      nsIContent* aContent);

  struct LastMouseDownInfo {
    nsCOMPtr<nsIContent> mLastMouseDownContent;
    Maybe<FormControlType> mLastMouseDownInputControlType;
    uint32_t mClickCount = 0;
  };

  LastMouseDownInfo& GetLastMouseDownInfo(int16_t aButton);

  // These variables are only relevant if we're the cursor-setting manager.
  StyleCursorKind mLockCursor;
  bool mHidingCursorWhileTyping = false;

  // Last mouse event screen point (in device pixel) when mouse was locked, used
  // to restore mouse position after unlocking.
  static LayoutDeviceIntPoint sPreLockScreenPoint;

  // Stores the mRefPoint of the last synthetic mouse move we dispatched
  // to re-center the mouse when we were pointer locked. If this is (-1,-1) it
  // means we've not recently dispatched a centering event. We use this to
  // detect when we receive the synth event, so we can cancel and not send it
  // to content.
  static LayoutDeviceIntPoint sSynthCenteringPoint;

  WeakFrame mCurrentTarget;
  nsCOMPtr<nsIContent> mCurrentTargetContent;
  static AutoWeakFrame sLastDragOverFrame;

  // Stores the mRefPoint (the offset from the widget's origin in device
  // pixels) of the last mouse event.
  static LayoutDeviceIntPoint sLastRefPoint;

  // member variables for the d&d gesture state machine
  LayoutDeviceIntPoint mGestureDownPoint;  // screen coordinates
  // The content to use as target if we start a d&d (what we drag).
  RefPtr<nsIContent> mGestureDownContent;
  // The content of the frame where the mouse-down event occurred. It's the same
  // as the target in most cases but not always - for example when dragging
  // an <area> of an image map this is the image. (bug 289667)
  nsCOMPtr<nsIContent> mGestureDownFrameOwner;
  // Data associated with a drag started in a content process.
  RefPtr<dom::RemoteDragStartData> mGestureDownDragStartData;
  // State of keys when the original gesture-down happened
  Modifiers mGestureModifiers;
  uint16_t mGestureDownButtons;
  int16_t mGestureDownButton;

  LastMouseDownInfo mLastLeftMouseDownInfo;
  LastMouseDownInfo mLastMiddleMouseDownInfo;
  LastMouseDownInfo mLastRightMouseDownInfo;

  nsCOMPtr<nsIContent> mActiveContent;
  nsCOMPtr<nsIContent> mHoverContent;
  static nsCOMPtr<nsIContent> sDragOverContent;
  nsCOMPtr<nsIContent> mURLTargetContent;
  nsCOMPtr<nsINode> mPopoverPointerDownTarget;

  nsPresContext* mPresContext;      // Not refcnted
  RefPtr<dom::Document> mDocument;  // Doesn't necessarily need to be owner

  RefPtr<IMEContentObserver> mIMEContentObserver;

  bool mShouldAlwaysUseLineDeltas : 1;
  bool mShouldAlwaysUseLineDeltasInitialized : 1;

  bool mGestureDownInTextControl : 1;

  bool mInTouchDrag;

  bool m_haveShutdown;

  RefPtr<OverOutElementsWrapper> mMouseEnterLeaveHelper;
  nsRefPtrHashtable<nsUint32HashKey, OverOutElementsWrapper>
      mPointersEnterLeaveHelper;

  // Array for accesskey support
  nsCOMArray<dom::Element> mAccessKeys;

  bool ShouldAlwaysUseLineDeltas();

 public:
  static nsresult UpdateUserActivityTimer(void);

  static bool sNormalLMouseEventInProcess;
  static int16_t sCurrentMouseBtn;

  static EventStateManager* sActiveESM;

  static void ClearGlobalActiveContent(EventStateManager* aClearer);

  // Functions used for click hold context menus
  nsCOMPtr<nsITimer> mClickHoldTimer;
  void CreateClickHoldTimer(nsPresContext* aPresContext, nsIFrame* aDownFrame,
                            WidgetGUIEvent* aMouseDownEvent);
  void KillClickHoldTimer();
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void FireContextClick();

  MOZ_CAN_RUN_SCRIPT static void SetPointerLock(nsIWidget* aWidget,
                                                nsPresContext* aPresContext);
  static void sClickHoldCallback(nsITimer* aTimer, void* aESM);
};

}  // namespace mozilla

// Click and double-click events need to be handled even for content that
// has no frame. This is required for Web compatibility.
#define NS_EVENT_NEEDS_FRAME(event)          \
  ((event)->mMessage != ePointerClick &&     \
   (event)->mMessage != eMouseDoubleClick && \
   (event)->mMessage != ePointerAuxClick)

#endif  // mozilla_EventStateManager_h_
